#requires -Version 7.0
<#
.SYNOPSIS
Measure whether the system cursor follows an injected touch drag.

Opens Start with VK_LWIN, injects a horizontal touch swipe through the shared
lifecycle in tools/touch_common.ps1, and polls GetCursorPos/GetCursorInfo from a
background thread every ~8 ms. Writes the raw samples to
artifacts/cursor-follow.csv and prints the measurements.

The cursor position is saved first and restored in finally.

Example:
  pwsh -File tools/probe_cursor_follow.ps1
#>
[CmdletBinding()]
param(
    [int]$StartX = 600,
    [int]$StartY = 700,
    [int]$EndX = 200,
    [int]$EndY = 700,
    [int]$Steps = 30,
    [int]$DelayMs = 20,
    [int]$IntervalMs = 8,
    [int]$BaselineMs = 400,
    [int]$TailMs = 1000,
    [int]$StartWaitMs = 1500,
    [string]$OutputPath = "$PSScriptRoot\..\artifacts\cursor-follow.csv"
)

$ErrorActionPreference = 'Stop'

$code = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class CursorFollowProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int x; public int y; }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int left, top, right, bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct CURSORINFO {
        public int cbSize;
        public uint flags;
        public IntPtr hCursor;
        public POINT ptScreenPos;
    }

    public sealed class Sample {
        public long Ms;
        public int X;
        public int Y;
        public uint Flags;
        public bool Showing;
    }

    public const uint CURSOR_SHOWING = 0x0001;

    delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    static extern bool GetCursorPos(out POINT point);

    [DllImport("user32.dll", SetLastError = true)]
    static extern bool GetCursorInfo(ref CURSORINFO info);

    [DllImport("user32.dll")]
    static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetClassName(IntPtr hWnd, StringBuilder name, int count);

    [DllImport("user32.dll")]
    static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    static extern IntPtr WindowFromPoint(POINT point);

    [DllImport("user32.dll")]
    static extern IntPtr GetAncestor(IntPtr hWnd, uint flags);

    [DllImport("dwmapi.dll")]
    static extern int DwmGetWindowAttribute(IntPtr hWnd, int attribute, out int value, int size);

    [DllImport("winmm.dll")]
    static extern uint timeBeginPeriod(uint period);

    [DllImport("winmm.dll")]
    static extern uint timeEndPeriod(uint period);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateWaitableTimerEx(IntPtr attributes, string name, uint flags, uint access);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetWaitableTimer(IntPtr timer, ref long dueTime, int period,
        IntPtr completionRoutine, IntPtr argument, bool resume);

    [DllImport("kernel32.dll")]
    static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

    // This process runs without an interactive window, so it can be placed in a
    // throttled timer class where timeBeginPeriod is ignored and Thread.Sleep(8)
    // takes ~15.6 ms. A high-resolution waitable timer keeps the requested pace.
    const uint CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x00000002;
    const uint TIMER_ALL_ACCESS = 0x1F0003;

    static IntPtr CreateTimer() {
        return CreateWaitableTimerEx(IntPtr.Zero, null, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }

    static void TimerWait(IntPtr timer, int ms) {
        if (timer == IntPtr.Zero) {
            Thread.Sleep(ms);
            return;
        }
        long due = -(long)ms * 10000L;
        if (!SetWaitableTimer(timer, ref due, 0, IntPtr.Zero, IntPtr.Zero, false)) {
            Thread.Sleep(ms);
            return;
        }
        WaitForSingleObject(timer, (uint)(ms + 1000));
    }

    static readonly object gate = new object();
    static List<Sample> samples = new List<Sample>();
    static Thread thread;
    static volatile bool stop;
    static Stopwatch clock;
    static string threadError;

    public static POINT GetCursorPoint() {
        POINT point;
        GetCursorPos(out point);
        return point;
    }

    public static bool SetCursorPoint(int x, int y) {
        return SetCursorPos(x, y);
    }

    public static string CursorState() {
        var info = new CURSORINFO();
        info.cbSize = Marshal.SizeOf(typeof(CURSORINFO));
        bool ok = GetCursorInfo(ref info);
        POINT point;
        GetCursorPos(out point);
        uint flags = ok ? info.flags : 0;
        return string.Format("pos=({0},{1}) flags=0x{2:X} showing={3} hCursor=0x{4:X}",
            point.x, point.y, flags, (flags & CURSOR_SHOWING) != 0,
            ok ? info.hCursor.ToInt64() : 0);
    }

    public static void WinKey(int holdMs) {
        keybd_event(0x5B, 0, 0, UIntPtr.Zero);
        Thread.Sleep(holdMs);
        keybd_event(0x5B, 0, 2, UIntPtr.Zero);
    }

    // Returns "open: ...", "hidden: ..." or "absent". The Start CoreWindow can
    // have WS_VISIBLE cleared while DWM still composites it, so cloaked decides.
    public static string StartWindowState(int pid) {
        IntPtr best = IntPtr.Zero;
        int bestCloaked = int.MaxValue;
        bool bestVisible = false;
        RECT bestRect = new RECT();

        EnumWindows((hWnd, lParam) => {
            uint windowPid;
            GetWindowThreadProcessId(hWnd, out windowPid);
            if (windowPid != (uint)pid) {
                return true;
            }

            var className = new StringBuilder(128);
            GetClassName(hWnd, className, className.Capacity);
            if (className.ToString() != "Windows.UI.Core.CoreWindow") {
                return true;
            }

            int cloaked = 0;
            DwmGetWindowAttribute(hWnd, 14, out cloaked, sizeof(int));
            if (best == IntPtr.Zero || cloaked < bestCloaked) {
                best = hWnd;
                bestCloaked = cloaked;
                bestVisible = IsWindowVisible(hWnd);
                GetWindowRect(hWnd, out bestRect);
            }

            return true;
        }, IntPtr.Zero);

        if (best == IntPtr.Zero) {
            return "absent";
        }

        return string.Format("{0}: hwnd=0x{1:X} cloaked={2} visible={3} rect={4},{5} {6}x{7}",
            bestCloaked == 0 ? "open" : "hidden", best.ToInt64(), bestCloaked,
            bestVisible ? 1 : 0, bestRect.left, bestRect.top,
            bestRect.right - bestRect.left, bestRect.bottom - bestRect.top);
    }

    // Hit-test ground truth: EnumWindows does not reliably list the UWP Start
    // CoreWindow in this environment (see measure_swipe.ps1).
    public static string WindowAt(int x, int y) {
        IntPtr hWnd = GetAncestor(WindowFromPoint(new POINT { x = x, y = y }), 2);
        if (hWnd == IntPtr.Zero) {
            return "none";
        }

        uint pid;
        GetWindowThreadProcessId(hWnd, out pid);
        var className = new StringBuilder(128);
        GetClassName(hWnd, className, className.Capacity);
        string processName;
        try {
            processName = Process.GetProcessById((int)pid).ProcessName;
        } catch {
            processName = "?";
        }
        RECT rect = new RECT();
        GetWindowRect(hWnd, out rect);
        return string.Format("class={0} pid={1} process={2} rect={3},{4} {5}x{6}",
            className, pid, processName, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top);
    }

    static void SampleLoop(int intervalMs, string path, int maxMs) {
        timeBeginPeriod(1);
        IntPtr timer = CreateTimer();
        try {
            string directory = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(directory)) {
                Directory.CreateDirectory(directory);
            }

            using (var writer = new StreamWriter(path, false, new UTF8Encoding(false))) {
                writer.WriteLine("ms,x,y");
                while (!stop && clock.ElapsedMilliseconds < maxMs) {
                    long ms = clock.ElapsedMilliseconds;

                    POINT point;
                    if (!GetCursorPos(out point)) {
                        point = new POINT();
                    }

                    var info = new CURSORINFO();
                    info.cbSize = Marshal.SizeOf(typeof(CURSORINFO));
                    bool infoOk = GetCursorInfo(ref info);

                    var sample = new Sample();
                    sample.Ms = ms;
                    sample.X = point.x;
                    sample.Y = point.y;
                    sample.Flags = infoOk ? info.flags : 0;
                    sample.Showing = infoOk && (info.flags & CURSOR_SHOWING) != 0;

                    lock (gate) {
                        samples.Add(sample);
                    }
                    writer.WriteLine(ms + "," + point.x + "," + point.y);
                    writer.Flush();

                    TimerWait(timer, intervalMs);
                }
            }
        } catch (Exception ex) {
            threadError = ex.Message;
        } finally {
            if (timer != IntPtr.Zero) {
                CloseHandle(timer);
            }
            timeEndPeriod(1);
        }
    }

    public static void StartSampler(int intervalMs, string path, int maxMs) {
        stop = false;
        threadError = null;
        clock = Stopwatch.StartNew();
        lock (gate) {
            samples = new List<Sample>();
        }
        thread = new Thread(() => SampleLoop(intervalMs, path, maxMs));
        thread.IsBackground = true;
        thread.Start();
    }

    public static void StopSampler() {
        stop = true;
        Thread current = thread;
        if (current != null) {
            current.Join(3000);
            thread = null;
        }
    }

    public static long NowMs() {
        Stopwatch current = clock;
        return current == null ? 0 : current.ElapsedMilliseconds;
    }

    public static Sample[] Snapshot() {
        lock (gate) {
            return samples.ToArray();
        }
    }

    public static string SamplerError() {
        return threadError;
    }
}
'@

Add-Type -TypeDefinition $code -Language CSharp
. (Join-Path $PSScriptRoot 'touch_common.ps1')

$exitCode = 0
$original = $null
$injectResult = 'not run'
$gestureStart = 0
$gestureEnd = 0

try {
    Set-TouchDpiAware

    $original = [CursorFollowProbe]::GetCursorPoint()
    Write-Output ("cursor saved: ({0},{1})" -f $original.x, $original.y)
    Write-Output ("cursor before: {0}" -f [CursorFollowProbe]::CursorState())

    $startProcess = Get-Process StartMenuExperienceHost -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $startProcess) {
        Write-Output "start process: not running"
        $startState = 'absent'
    } else {
        $startState = [CursorFollowProbe]::StartWindowState([int]$startProcess.Id)
        Write-Output ("start before: {0}" -f $startState)
    }
    $hitBefore = [CursorFollowProbe]::WindowAt(300, 300)
    Write-Output ("window at (300,300) before: {0}" -f $hitBefore)

    $startOpen = $startState -like 'open:*' -or $hitBefore -like '*process=StartMenuExperienceHost*'
    if ($startOpen) {
        Write-Output "start already open; VK_LWIN not sent"
    } else {
        [CursorFollowProbe]::WinKey(60)
        Start-Sleep -Milliseconds $StartWaitMs
        if ($null -ne $startProcess) {
            Write-Output ("start after win: {0}" -f `
                [CursorFollowProbe]::StartWindowState([int]$startProcess.Id))
        }
        Write-Output ("window at (300,300) after win: {0}" -f [CursorFollowProbe]::WindowAt(300, 300))
    }
    Write-Output ("window at touch start ({0},{1}): {2}" -f `
        $StartX, $StartY, [CursorFollowProbe]::WindowAt($StartX, $StartY))

    $fullOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
    $maxMs = $BaselineMs + ($Steps + 2) * $DelayMs + $TailMs + 2000
    [CursorFollowProbe]::StartSampler($IntervalMs, $fullOutputPath, $maxMs)
    Start-Sleep -Milliseconds $BaselineMs

    $gestureStart = [CursorFollowProbe]::NowMs()
    $injectResult = [TouchCommon.Injector]::Swipe($StartX, $StartY, $EndX, $EndY, $Steps, $DelayMs)
    $gestureEnd = [CursorFollowProbe]::NowMs()
    Write-Output ("inject: {0} (touch {1},{2} -> {3},{4}, {5} steps x {6} ms; window {7}..{8} ms)" -f `
        $injectResult, $StartX, $StartY, $EndX, $EndY, $Steps, $DelayMs, $gestureStart, $gestureEnd)
    if ($injectResult -ne 'OK') {
        $exitCode = 1
    }

    Start-Sleep -Milliseconds $TailMs
    [CursorFollowProbe]::StopSampler()

    if ($null -ne $startProcess) {
        Write-Output ("start after swipe: {0}" -f `
            [CursorFollowProbe]::StartWindowState([int]$startProcess.Id))
    }
    Write-Output ("window at (300,300) after swipe: {0}" -f [CursorFollowProbe]::WindowAt(300, 300))
    Write-Output ("cursor after swipe: {0}" -f [CursorFollowProbe]::CursorState())

    $samplerError = [CursorFollowProbe]::SamplerError()
    if (-not [string]::IsNullOrEmpty($samplerError)) {
        Write-Output ("sampler error: {0}" -f $samplerError)
    }

    $samples = @([CursorFollowProbe]::Snapshot())
    Write-Output ("samples: {0}" -f $samples.Count)
    Write-Output ("csv: {0}" -f $fullOutputPath)

    if ($samples.Count -lt 2) {
        Write-Output "not enough samples to analyze."
        exit 1
    }

    $first = $samples[0]
    $last = $samples[$samples.Count - 1]
    $spanMs = $last.Ms - $first.Ms
    $meanInterval = $spanMs / [double]($samples.Count - 1)
    Write-Output ("poll window: {0} ms; mean interval: {1:F2} ms; rate: {2:F1} Hz" -f `
        $spanMs, $meanInterval, (1000.0 / $meanInterval))

    $intervals = @()
    for ($i = 1; $i -lt $samples.Count; $i++) {
        $intervals += ($samples[$i].Ms - $samples[$i - 1].Ms)
    }
    $sortedIntervals = @($intervals | Sort-Object)
    $p50 = $sortedIntervals[[int][math]::Floor($sortedIntervals.Count * 0.5)]
    $p95 = $sortedIntervals[[int][math]::Floor($sortedIntervals.Count * 0.95)]
    Write-Output ("interval p50/p95/max: {0}/{1}/{2} ms" -f `
        $p50, $p95, $sortedIntervals[$sortedIntervals.Count - 1])

    $firstDelta = -1
    for ($i = 1; $i -lt $samples.Count; $i++) {
        if ($samples[$i].X -ne $samples[$i - 1].X -or $samples[$i].Y -ne $samples[$i - 1].Y) {
            $firstDelta = $i
            break
        }
    }
    if ($firstDelta -lt 0) {
        Write-Output "first non-zero delta: none (cursor never moved)"
    } else {
        $current = $samples[$firstDelta]
        $previous = $samples[$firstDelta - 1]
        Write-Output ("first non-zero delta: ms={0} x={1} y={2} (d={3},{4}; from {5},{6})" -f `
            $current.Ms, $current.X, $current.Y,
            ($current.X - $previous.X), ($current.Y - $previous.Y),
            $previous.X, $previous.Y)
    }

    $minIndex = 0
    $maxIndex = 0
    $movedCount = 0
    $sumAbsDx = 0
    $movingIndexes = New-Object System.Collections.Generic.List[int]
    for ($i = 0; $i -lt $samples.Count; $i++) {
        if ($samples[$i].X -lt $samples[$minIndex].X) { $minIndex = $i }
        if ($samples[$i].X -gt $samples[$maxIndex].X) { $maxIndex = $i }
        if ($i -gt 0) {
            $dx = $samples[$i].X - $samples[$i - 1].X
            $sumAbsDx += [math]::Abs($dx)
            if ([math]::Abs($dx) -gt 2) {
                $movedCount++
                $movingIndexes.Add($i)
            }
        }
    }

    $minSample = $samples[$minIndex]
    $maxSample = $samples[$maxIndex]
    Write-Output ("min x: ms={0} x={1} y={2}" -f $minSample.Ms, $minSample.X, $minSample.Y)
    Write-Output ("max x: ms={0} x={1} y={2}" -f $maxSample.Ms, $maxSample.X, $maxSample.Y)
    Write-Output ("total x travel: span={0} px, sum|dx|={1} px" -f `
        ($maxSample.X - $minSample.X), $sumAbsDx)
    Write-Output ("samples moving x > 2 px: {0} of {1}" -f $movedCount, ($samples.Count - 1))

    if ($movingIndexes.Count -ge 3) {
        $lo = [int][math]::Floor($movingIndexes.Count * 0.2)
        $hi = [int][math]::Ceiling($movingIndexes.Count * 0.8) - 1
        $badSteps = 0
        $worstAscent = 0
        for ($k = $lo + 1; $k -le $hi; $k++) {
            $dx = $samples[$movingIndexes[$k]].X - $samples[$movingIndexes[$k - 1]].X
            if ($dx -gt 2) {
                $badSteps++
                if ($dx -gt $worstAscent) { $worstAscent = $dx }
            }
        }
        $middleCount = $hi - $lo + 1
        $verdict = if ($badSteps -eq 0) { 'YES' } else { 'NO' }
        Write-Output ("middle 60% monotonic decreasing: {0} ({1} movers, {2} ascending steps > 2 px, worst ascent {3} px)" -f `
            $verdict, $middleCount, $badSteps, $worstAscent)
    } else {
        Write-Output ("middle 60% monotonic decreasing: n/a ({0} movers)" -f $movingIndexes.Count)
    }

    $flagGroups = $samples | Group-Object -Property Flags
    $flagText = ($flagGroups | ForEach-Object {
        $flagValue = [uint32]$_.Name
        $showing = (($flagValue -band [CursorFollowProbe]::CURSOR_SHOWING) -ne 0)
        "0x{0:X} (showing={1}) x{2}" -f $flagValue, $showing, $_.Count
    }) -join ', '
    Write-Output ("cursor flags, whole poll: {0}" -f $flagText)

    $showingAll = @($samples | Where-Object { $_.Showing }).Count
    Write-Output ("CURSOR_SHOWING samples: {0} of {1}" -f $showingAll, $samples.Count)

    $window = @($samples | Where-Object { $_.Ms -ge $gestureStart -and $_.Ms -le $gestureEnd })
    if ($window.Count -gt 0) {
        $windowFlags = ($window | Group-Object -Property Flags | ForEach-Object {
            $flagValue = [uint32]$_.Name
            $showing = (($flagValue -band [CursorFollowProbe]::CURSOR_SHOWING) -ne 0)
            "0x{0:X} (showing={1}) x{2}" -f $flagValue, $showing, $_.Count
        }) -join ', '
        $distinctPositions = @($window | ForEach-Object { "{0},{1}" -f $_.X, $_.Y } |
            Sort-Object -Unique).Count
        $changes = 0
        for ($k = 1; $k -lt $window.Count; $k++) {
            if ($window[$k].X -ne $window[$k - 1].X -or $window[$k].Y -ne $window[$k - 1].Y) {
                $changes++
            }
        }
        $windowMs = [math]::Max(1, $gestureEnd - $gestureStart)
        Write-Output ("gesture window: {0} ms, {1} samples, {2} distinct positions, {3} position changes, {4:F2} changes/s" -f `
            $windowMs, $window.Count, $distinctPositions, $changes, (1000.0 * $changes / $windowMs))
        Write-Output ("gesture cursor x: first={0} last={1} min={2} max={3}" -f `
            $window[0].X, $window[$window.Count - 1].X,
            ($window | Measure-Object -Property X -Minimum).Minimum,
            ($window | Measure-Object -Property X -Maximum).Maximum)
        Write-Output ("gesture flags: {0}" -f $windowFlags)
    } else {
        Write-Output "gesture window: no samples"
    }

    Write-Output "last 5 samples:"
    $samples | Select-Object -Last 5 | ForEach-Object {
        Write-Output ("  ms={0} x={1} y={2} flags=0x{3:X}" -f $_.Ms, $_.X, $_.Y, $_.Flags)
    }
}
catch {
    Write-Output ("ERROR: {0}" -f $_.Exception.Message)
    $exitCode = 1
}
finally {
    try { [CursorFollowProbe]::StopSampler() } catch { }
    if ($null -ne $original) {
        [void][CursorFollowProbe]::SetCursorPoint($original.x, $original.y)
        $restored = [CursorFollowProbe]::GetCursorPoint()
        Write-Output ("cursor restored: ({0},{1})" -f $restored.x, $restored.y)
    }
}

exit $exitCode
