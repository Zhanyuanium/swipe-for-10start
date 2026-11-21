#requires -Version 7.0
<#
.SYNOPSIS
Track the panel's visible trailing edge during an interruption, in physical
pixels, by diffing each frame against a closed-menu baseline.

.DESCRIPTION
The mod's log can prove that a takeover happened; it cannot prove that the panel
turned around instead of retracting to the hidden endpoint. This tool measures
the pixels: it captures one narrow band of the panel with a reused DIB, diffs
every frame against a closed-menu baseline, and reports the extreme coordinate of
the changed region along the panel's scan axis — the panel's trailing edge, the
side that faces away from the taskbar. On the live left docked bar (axis 0) that
edge is vertical and the scan runs along x; the axis=1/3 branch scans along y, so
the tool is not left-bar-only.

Geometry comes from the mod, never from this file: the axis, panel rectangle, band
and work area are read from the newest `reveal panelTrail` line written at or
after the run's tick mark, and Distance from the run's `gesture geometry ...
dist=` line (falling back to the work area's extent along the axis). The monitor
scale is the `dismiss` bar rectangle's physical thickness over half the band
(the source smoke.ps1 uses), cross-checked against the physical screen over the
work area's extent on the perpendicular axis, which no taskbar insets. Missing
geometry is refused, never guessed.

Progress uses the mod's own mapping (`RevealProgressDip`): the composition moves
the trail edge by `(1 - Progress) * Dir * Distance`, so
`Progress = 1 - (edge - trail) / (Dir * Distance)` with `Dir` -1 on a left or top
taskbar and +1 on a right or bottom one. The mapping and its numbers are printed
with every run.

Timing: one DIB, no sleep between frames, and each `t` is stamped immediately
before its `BitBlt`; the reported period is what the host delivered (the copy's
own cost is printed as `cap=` in the summary, not in the table).

Triggers. `-WinTap` taps VK_LWIN to open the menu, taps again `-CloseAtMs` later,
and samples the whole close. `-Observe` samples a window whose triggers the caller
performs. Both accept `-TriggerAtMs`/`-Trigger` pairs: `wintap` taps the Win key,
`pull` injects a qualified band pull (DOWN 2 DIP inside the band, 25 DIP inward, a
hold, one real release) through `tools/touch_common.ps1`. In `-WinTap` the offsets
are measured from the close tap, so an offset names a moment inside the retract;
in `-Observe` they are measured from the first sample.

What it cannot prove: the hidden part of the travel (once the panel is behind the
taskbar or the window is cloaked there are no pixels left to read), the panel's
internal state, or anything about a frame the compositor had not presented when
the BitBlt ran. A sample with no changed region is reported as `edge=-1`, never as
progress 0.

.PARAMETER Log
The Start half's `renderer.log`. Default: the standard Windhawk data root under
`%ProgramData%`.

.PARAMETER WinTap
The tool opens and closes the menu itself. This is the default mode.

.PARAMETER Observe
The tool only samples; the caller performs the trigger inside the sample window.

.PARAMETER OpenAtMs
Offset of the opening Win tap from the first sample (win-tap mode only).

.PARAMETER CloseAtMs
Offset of the closing Win tap from the opening tap (win-tap mode only).

.PARAMETER TailMs
How long to sample after the close tap.

.PARAMETER ObserveMs
Sample window length in observe mode.

.PARAMETER TriggerAtMs
Offsets of the `-Trigger` actions, one entry per action; commas or spaces split a
list (`-TriggerAtMs 150,400`). Measured from the close tap in win-tap mode and
from the first sample in observe mode.

.PARAMETER Trigger
The action at the matching offset: `pull` or `wintap` (`-Trigger pull,wintap`).

.PARAMETER Csv
Explicit CSV path (`t,tc,edge_px,edge_dip,progress`). An existing file is refused
unless `-Force` is given, so another tool's artifact cannot be overwritten.

.PARAMETER Threshold
Mean per-channel brightness distance a pixel must exceed to count as changed.

.PARAMETER Majority
Fraction of sampled lines a column (or row) must be changed in to count.

.EXAMPLE
pwsh -File tools/panel_edge_track.ps1 -WinTap -Csv artifacts/panel-edge-wintap-1.csv

.EXAMPLE
pwsh -File tools/panel_edge_track.ps1 -WinTap -TriggerAtMs 150 -Trigger pull `
    -Csv artifacts/panel-edge-wintap-pull-1.csv

.NOTES
[INPUT]: reads renderer.log's `reveal panelTrail` and `gesture geometry` lines;
         dot-sources tools/touch_common.ps1 for the pull; P/Invokes user32,
         gdi32, dwmapi and kernel32; finds the Start window by class and host PID
[OUTPUT]: a `t=... tc=... edge=... progress=...` table, a summary, the trigger
           lines, a RESULT line, and an optional CSV
[POS]: the only tool that measures the panel's trailing edge pixel by pixel; it
       complements smoke.ps1 (which judges the mod's trace) and does not replace
       settle_track.ps1's release-timed bottom-edge series
[PROTOCOL]: Update this header on change, then check AGENTS.md
#>
[CmdletBinding(DefaultParameterSetName = 'WinTap')]
param(
    [Parameter(ParameterSetName = 'Observe')]
    [switch]$Observe,
    [Parameter(ParameterSetName = 'WinTap')]
    [switch]$WinTap,
    [string]$Log = (Join-Path $env:ProgramData 'Windhawk\Engine\ModsWritable\mod-storage\local@swipe-for-10start\renderer.log'),
    [int]$OpenAtMs = 60,
    [int]$CloseAtMs = 700,
    [int]$TailMs = 900,
    [int]$ObserveMs = 1600,
    [string[]]$TriggerAtMs = @(),
    [string[]]$Trigger = @(),
    [string]$Csv = '',
    [switch]$Force,
    [double]$Threshold = 18,
    [double]$Majority = 0.6,
    [int]$GeometryWaitMs = 12000,
    [int]$CooldownMs = 900,
    [int]$PullSteps = 10,
    [int]$PullStepMs = 8,
    [int]$PullHoldMs = 60
)

$ErrorActionPreference = 'Stop'
[Globalization.CultureInfo]::CurrentCulture = 'en-US'

. (Join-Path $PSScriptRoot 'touch_common.ps1')

if ($Csv -and (Test-Path -LiteralPath $Csv) -and -not $Force) {
    throw "'$Csv' already exists; choose another path or pass -Force to replace it"
}

# `pwsh -File` hands `-TriggerAtMs 100,200` over as the single string "100,200"
# (an [int[]] would read it as 100200, a thousands separator), so the lists are
# split here and validated by hand instead of by the binder.
$offsets = [Collections.Generic.List[int]]::new()
foreach ($chunk in $TriggerAtMs) {
    foreach ($piece in ($chunk -split '[,;\s]+')) {
        if ($piece -eq '') { continue }
        $ms = 0
        if (-not [int]::TryParse($piece, [ref]$ms) -or $ms -lt 0) {
            throw "-TriggerAtMs '$piece' is not a non-negative integer"
        }
        $offsets.Add($ms)
    }
}
$kinds = [Collections.Generic.List[string]]::new()
foreach ($chunk in $Trigger) {
    foreach ($piece in ($chunk -split '[,;\s]+')) {
        if ($piece -eq '') { continue }
        if ($piece -ne 'pull' -and $piece -ne 'wintap') {
            throw "-Trigger '$piece' is not pull or wintap"
        }
        $kinds.Add($piece)
    }
}
if ($offsets.Count -ne $kinds.Count) {
    throw ("-TriggerAtMs and -Trigger must name the same number of actions " +
        "(got $($offsets.Count) offsets and $($kinds.Count) actions)")
}

$code = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

public static class PanelEdge
{
    [StructLayout(LayoutKind.Sequential)]
    struct BIH { public int biSize, biWidth, biHeight; public short biPlanes, biBitCount;
                 public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter;
                 public int biClrUsed, biClrImportant; }

    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr v);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateDIBSection(IntPtr dc, ref BIH bmi,
        uint u, out IntPtr bits, IntPtr s, uint o);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
    [DllImport("gdi32.dll")] static extern bool BitBlt(IntPtr d, int x, int y, int w, int h,
        IntPtr s, int sx, int sy, uint rop);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags,
        UIntPtr extra);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr h, int attr,
        out int value, int size);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateWaitableTimerEx(IntPtr attributes, string name, uint flags,
        uint access);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetWaitableTimer(IntPtr timer, ref long due, int period,
        IntPtr routine, IntPtr argument, bool resume);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);

    const uint CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x00000002;
    const uint TIMER_ALL_ACCESS = 0x001F0003;

    static double Ms() { return Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency; }

    [ThreadStatic] static IntPtr waitTimer;

    // The trigger thread's waits need the one-shot high-resolution timer: this
    // host puts a windowless pwsh in a class where Sleep(8) takes 15-16 ms.
    static void WaitMs(double ms)
    {
        if (ms <= 0) return;
        if (waitTimer == IntPtr.Zero)
        {
            waitTimer = CreateWaitableTimerEx(IntPtr.Zero, null,
                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        }
        if (waitTimer == IntPtr.Zero) { Thread.Sleep((int)ms); return; }
        long due = -(long)(ms * 10000.0);
        if (!SetWaitableTimer(waitTimer, ref due, 0, IntPtr.Zero, IntPtr.Zero, false))
        { Thread.Sleep((int)ms); return; }
        WaitForSingleObject(waitTimer, (uint)(ms + 1000));
    }

    static void WaitUntil(double due)
    {
        while (true)
        {
            double left = due - Ms();
            if (left <= 0) return;
            if (left > 3.0) WaitMs(left - 2.0);
            else Thread.SpinWait(300);
        }
    }

    // ---- capture ----
    static IntPtr screen, mem, dib, bits;
    static int X0, Y0, W, H, Lines, stride;
    static bool vertically, increasing;
    static double threshold, majority;
    static byte[] baseBuf, curBuf;
    static bool haveBaseline;
    static int[] linesIdx;

    public static void Aware()
    {
        // PMv2: the DIB is read in physical pixels, and so is the injection.
        try { SetProcessDpiAwarenessContext(new IntPtr(-4)); } catch { }
    }

    public static void Init(int x0, int y0, int w, int h, int lines, double thr,
                            double maj, bool vertical, bool increase)
    {
        Aware();
        if (w < 2 || h < 2) throw new ArgumentException("capture region too small");
        X0 = x0; Y0 = y0; W = w; H = h; Lines = Math.Max(2, lines);
        threshold = thr; majority = maj; vertically = vertical; increasing = increase;
        stride = W * 4;
        int span = vertically ? W : H;
        if (Lines > span) throw new ArgumentException("more sampled lines than the capture spans");
        screen = GetDC(IntPtr.Zero);
        BIH bmi = new BIH();
        bmi.biSize = Marshal.SizeOf(typeof(BIH));
        bmi.biWidth = W; bmi.biHeight = -H; bmi.biPlanes = 1; bmi.biBitCount = 32;
        dib = CreateDIBSection(screen, ref bmi, 0, out bits, IntPtr.Zero, 0);
        if (dib == IntPtr.Zero) throw new InvalidOperationException("CreateDIBSection failed");
        mem = CreateCompatibleDC(screen);
        SelectObject(mem, dib);
        baseBuf = new byte[W * H * 4];
        curBuf = new byte[W * H * 4];
        linesIdx = new int[Lines];
        for (int i = 0; i < Lines; i++) linesIdx[i] = (span - 1) * i / (Lines - 1);
    }

    static void Grab()
    {
        BitBlt(mem, 0, 0, W, H, screen, X0, Y0, 0x00CC0020);
        Marshal.Copy(bits, curBuf, 0, curBuf.Length);
    }

    // Extreme coordinate of the changed region along the scan axis. A column
    // (or row) counts when at least `majority` of the sampled lines differ, and
    // the search runs from the far side inward for a trailing edge that grows
    // toward the desktop, or from the taskbar outward for one that shrinks.
    static int Scan()
    {
        int need = (int)Math.Ceiling(majority * Lines);
        int span = vertically ? H : W;
        int best = -1;
        for (int p = 0; p < span; p++)
        {
            int n = 0;
            for (int i = 0; i < Lines; i++)
            {
                int q = vertically ? (p * stride + linesIdx[i] * 4)
                                    : (linesIdx[i] * stride + p * 4);
                int d = Math.Abs(curBuf[q] - baseBuf[q]) +
                        Math.Abs(curBuf[q + 1] - baseBuf[q + 1]) +
                        Math.Abs(curBuf[q + 2] - baseBuf[q + 2]);
                if (d > threshold * 3.0) n++;
                if (n + (Lines - i - 1) < need) break;
            }
            if (n >= need)
            {
                if (!increasing) { best = p; break; }
                best = p;
            }
        }
        if (best < 0) return -1;
        return (vertically ? Y0 : X0) + best;
    }

    public static void Baseline()
    {
        Grab();
        Array.Copy(curBuf, baseBuf, curBuf.Length);
        haveBaseline = true;
    }

    // One frame against the closed baseline, for the baseline check.
    public static int Frame()
    {
        if (!haveBaseline) throw new InvalidOperationException("no baseline captured");
        Grab();
        return Scan();
    }

    // ---- Start window state (the cloak is the only honest open/closed signal) ----
    public static int Cloaked(uint hostPid)
    {
        IntPtr h = IntPtr.Zero;
        while (true)
        {
            h = FindWindowEx(IntPtr.Zero, h, "Windows.UI.Core.CoreWindow", null);
            if (h == IntPtr.Zero) return -1;
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid != hostPid) continue;
            int v = -1;
            if (DwmGetWindowAttribute(h, 14, out v, 4) != 0) return -1;
            return v;
        }
    }

    public static bool WaitCloak(uint hostPid, bool open, int timeoutMs)
    {
        double end = Ms() + timeoutMs;
        while (Ms() < end)
        {
            int c = Cloaked(hostPid);
            if (c >= 0 && ((c == 0) == open)) return true;
            WaitMs(5);
        }
        return false;
    }

    public static void Tap()
    {
        // 0x5B is VK_LWIN; the key is held 60 ms so the shell reads a press.
        keybd_event(0x5B, 0, 0, UIntPtr.Zero);
        WaitMs(60);
        keybd_event(0x5B, 0, 2, UIntPtr.Zero);
    }

    // ---- touch injection, through tools/touch_common.ps1's own Injector ----
    static Type touchType;
    static MethodInfo mBegin, mGlide, mHold, mEnd;
    static bool contactOpen;
    static int lastX, lastY;

    static void LoadTouch()
    {
        if (touchType != null) return;
        foreach (Assembly a in AppDomain.CurrentDomain.GetAssemblies())
        {
            Type t = null;
            try { t = a.GetType("TouchCommon.Injector", false); } catch { }
            if (t != null) { touchType = t; break; }
        }
        if (touchType == null)
            throw new InvalidOperationException(
                "TouchCommon.Injector is not loaded; dot-source tools/touch_common.ps1 first");
        mBegin = touchType.GetMethod("Begin", new Type[] { typeof(int), typeof(int) });
        mGlide = touchType.GetMethod("Glide",
            new Type[] { typeof(int), typeof(int), typeof(int), typeof(int), typeof(int), typeof(int) });
        mHold = touchType.GetMethod("Hold", new Type[] { typeof(int), typeof(int), typeof(int) });
        mEnd = touchType.GetMethod("End", new Type[] { typeof(int), typeof(int) });
        if (mBegin == null || mGlide == null || mHold == null || mEnd == null)
            throw new InvalidOperationException("TouchCommon.Injector changed shape");
    }

    static string Call(MethodInfo m, params object[] args) { return (string)m.Invoke(null, args); }

    // DOWN, one UPDATE per step inward, a real hold, then one release. A failed
    // step still ends the contact; a failed UP is reported, never hidden.
    static string Pull(int x0, int y0, int x1, int y1, int steps, int stepMs, int holdMs)
    {
        LoadTouch();
        string step = "";
        string r = Call(mBegin, x0, y0);
        contactOpen = (r == "OK");
        lastX = x0; lastY = y0;
        if (r != "OK") return "DOWN " + r;
        r = Call(mGlide, x0, y0, x1, y1, steps, stepMs);
        if (r != "OK") step = "glide " + r;
        lastX = x1; lastY = y1;
        if (step == "")
        {
            r = Call(mHold, x1, y1, holdMs);
            if (r != "OK") step = "hold " + r;
        }
        string up = Call(mEnd, x1, y1);
        if (up == "OK") contactOpen = false;
        if (step != "") return step + "; UP " + up;
        return up == "OK" ? "OK" : "UP " + up;
    }

    // ---- run ----
    static double t0 = -1, tOpen = -1, tClose = -1;
    public static double TOpen { get { return tOpen; } }
    public static double TClose { get { return tClose; } }
    public static string Status = "";
    public static List<string> Triggers = new List<string>();
    public static List<double> Times = new List<double>();
    public static List<int> Edges = new List<int>();
    public static List<double> Caps = new List<double>();
    static volatile bool stopTrig;
    static Thread triggerThread;
    static int[] trigAt;
    static string[] trigKind;
    static int openAt, closeAt, pullX0, pullY0, pullX1, pullY1, pullSteps, pullStepMs, pullHoldMs;
    static uint hostPid;

    public static void ConfigureTriggers(int[] atMs, string[] kind, int openAtMs, int closeAtMs,
                                         uint pid, int x0, int y0, int x1, int y1,
                                         int steps, int stepMs, int holdMs)
    {
        trigAt = (int[])atMs.Clone();
        trigKind = (string[])kind.Clone();
        openAt = openAtMs; closeAt = closeAtMs; hostPid = pid;
        pullX0 = x0; pullY0 = y0; pullX1 = x1; pullY1 = y1;
        pullSteps = steps; pullStepMs = stepMs; pullHoldMs = holdMs;
    }

    static void TriggerLoop()
    {
        try
        {
            if (openAt >= 0)
            {
                WaitUntil(t0 + openAt);
                if (stopTrig) return;
                tOpen = Ms() - t0;
                Tap();
                Triggers.Add(string.Format("TRIG kind=wintap stage=open t={0:F1} ok=1", tOpen));
                if (!WaitCloak(hostPid, true, 2000))
                {
                    Status = "open-not-observed";
                    return;
                }
                WaitUntil(t0 + tOpen + closeAt);
                if (stopTrig) return;
                tClose = Ms() - t0;
                Tap();
                Triggers.Add(string.Format("TRIG kind=wintap stage=close t={0:F1} ok=1", tClose));
            }
            for (int i = 0; i < trigAt.Length; i++)
            {
                WaitUntil(t0 + (tClose > 0 ? tClose : 0) + trigAt[i]);
                if (stopTrig) return;
                double t = Ms() - t0;
                if (trigKind[i] == "wintap")
                {
                    Tap();
                    Triggers.Add(string.Format("TRIG kind=wintap t={0:F1} ok=1", t));
                }
                else
                {
                    string r = Pull(pullX0, pullY0, pullX1, pullY1, pullSteps, pullStepMs, pullHoldMs);
                    Triggers.Add(string.Format(
                        "TRIG kind=pull t={0:F1} from={1},{2} to={3},{4} ok={5}",
                        t, pullX0, pullY0, pullX1, pullY1, r == "OK" ? 1 : 0));
                    if (r != "OK") Status = "pull: " + r;
                }
            }
        }
        catch (Exception e)
        {
            Status = "trigger failed: " + e.Message;
        }
    }

    // t=0 is the first sample. The trigger thread starts after it is known, so
    // the schedule and the table share one clock.
    public static void Run(double planMs, double tailMs)
    {
        t0 = Ms();
        triggerThread = new Thread(TriggerLoop);
        triggerThread.IsBackground = true;
        triggerThread.Start();
        while (true)
        {
            double now = Ms();
            if (tClose > 0 && now - t0 - tClose >= tailMs) break;
            if (now - t0 >= planMs) break;
            double stamp = now - t0;
            Grab();
            int e = Scan();
            double cap = Ms() - now;
            Times.Add(stamp); Edges.Add(e); Caps.Add(cap);
        }
        if (!triggerThread.Join(4000) && Status == "")
            Status = "trigger-thread-join-timeout";
    }

    // Every exit path ends here: the contact is released, or the failure to
    // release it is returned so the caller can report it.
    public static string StopAll()
    {
        stopTrig = true;
        if (triggerThread != null && triggerThread.IsAlive)
        {
            if (!triggerThread.Join(3000))
                return "trigger thread did not stop within 3000 ms; the contact may still be down";
        }
        if (contactOpen)
        {
            string r = Call(mEnd, lastX, lastY);
            if (r != "OK") return "the UP failed (" + r + "); the contact is still down";
            contactOpen = false;
            return "contact was still down; released at " + lastX + "," + lastY;
        }
        return "OK";
    }
}
'@

Add-Type -TypeDefinition $code
[PanelEdge]::Aware()
$screenW = [PanelEdge]::GetSystemMetrics(0)
$screenH = [PanelEdge]::GetSystemMetrics(1)

$mode = if ($PSCmdlet.ParameterSetName -eq 'Observe') { 'observe' } else { 'wintap' }
$runMark = [Environment]::TickCount64
$hostPid = [uint32](Get-Process StartMenuExperienceHost -ErrorAction Stop).Id

# ---------------------------------------------------------------------------
# The mod's own layout, from this run's lines only
# ---------------------------------------------------------------------------
function Get-StartState { return ([PanelEdge]::Cloaked($hostPid) -eq 0) }

function Wait-StartState {
    param([bool]$Want, [int]$TimeoutMs = 4000)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        if ((Get-StartState) -eq $Want) { return $true }
        Start-Sleep -Milliseconds 25
    }
    return $false
}

function Get-RunLines {
    if (-not (Test-Path -LiteralPath $Log)) { return @() }
    return @(Get-Content -LiteralPath $Log | Where-Object {
        $_ -match '^t=(\d+) ' -and [long]$Matches[1] -ge [long]$runMark
    })
}

# The newest `reveal panelTrail` record that carries the axis, the panel
# rectangle and the band: an accepted line wins over a rejected one (a rejected
# rectangle is a mid-animation measurement), but a run whose only record was
# rejected still gets its band instead of no layout at all.
function Get-Layout {
    $lines = @(Get-RunLines)
    $accepted = $null
    $rejected = $null
    foreach ($line in $lines) {
        if ($line -match 'reveal panelTrail rejected .*axis=\d.*rect=.*band=') { $rejected = $line }
        elseif ($line -match 'reveal panelTrail=-?\d+ axis=\d.*rect=.*band=') { $accepted = $line }
    }
    $line = if ($accepted) { $accepted } else { $rejected }
    if (-not $line) { return $null }
    $layout = [pscustomobject]@{
        Line = $line
        Source = if ($accepted) { 'accepted' } else { 'rejected' }
        Axis = -1; Trail = 0.0; Left = 0.0; Top = 0.0; Right = 0.0; Bottom = 0.0
        Low = 0.0; High = 0.0
        WorkLeft = 0.0; WorkTop = 0.0; WorkRight = 0.0; WorkBottom = 0.0
        Distance = 0.0; DistanceSource = ''
        Scale = 0.0; ScaleSource = ''
    }
    if ($line -match 'reveal panelTrail=(-?\d+)') { $layout.Trail = [double]$Matches[1] }
    elseif ($line -match 'reveal panelTrail rejected (-?\d+)') { $layout.Trail = [double]$Matches[1] }
    if ($line -notmatch 'axis=(\d)') { return $layout }
    $layout.Axis = [int]$Matches[1]
    if ($line -match 'rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)') {
        $layout.Left = [double]$Matches[1]; $layout.Top = [double]$Matches[2]
        $layout.Right = [double]$Matches[3]; $layout.Bottom = [double]$Matches[4]
    }
    if ($line -match 'band=(-?\d+),(-?\d+)') {
        $layout.Low = [double]$Matches[1]; $layout.High = [double]$Matches[2]
    }
    if ($line -match 'work=(-?\d+),(-?\d+),(-?\d+),(-?\d+)') {
        $layout.WorkLeft = [double]$Matches[1]; $layout.WorkTop = [double]$Matches[2]
        $layout.WorkRight = [double]$Matches[3]; $layout.WorkBottom = [double]$Matches[4]
    }
    $horizontal = ($layout.Axis -eq 0 -or $layout.Axis -eq 2)
    # Distance is the composition's own scalar: the work area's extent along the
    # axis. The run's `gesture geometry ... dist=` line carries it; the work
    # rectangle yields the same number when that line is missing.
    $distLine = $null
    foreach ($l in $lines) {
        if ($l -match "gesture geometry axis=$($layout.Axis) dist=(-?[\d.]+)") { $distLine = $l }
    }
    if ($distLine) {
        $null = $distLine -match 'dist=(-?[\d.]+)'
        $layout.Distance = [double]$Matches[1]; $layout.DistanceSource = 'gesture geometry dist='
    } elseif ($layout.WorkRight -gt $layout.WorkLeft) {
        $layout.Distance = if ($horizontal) { $layout.WorkRight - $layout.WorkLeft }
                           else { $layout.WorkBottom - $layout.WorkTop }
        $layout.DistanceSource = 'work area extent'
    }
    # The monitor scale: the bar's physical thickness over half the band (what
    # smoke.ps1 uses) when a `dismiss` record names this axis, else the physical
    # screen over the work area's extent on the axis' perpendicular axis, which
    # no taskbar insets. The `dismiss` record is DPI, not layout, so it may come
    # from an earlier session; the two sources are cross-checked when both exist.
    $bandDip = $layout.High - $layout.Low
    if ($bandDip -gt 0) {
        $fromWork = 0.0
        $perpPhysical = if ($horizontal) { $screenH } else { $screenW }
        $perpWork = if ($horizontal) { $layout.WorkBottom - $layout.WorkTop }
                    else { $layout.WorkRight - $layout.WorkLeft }
        if ($perpPhysical -gt 0 -and $perpWork -gt 0) { $fromWork = $perpPhysical / $perpWork }
        $bar = $null
        if (Test-Path -LiteralPath $Log) {
            foreach ($l in (Get-Content -LiteralPath $Log)) {
                if ($l -match ("dismiss axis={0}\b.*rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)" -f $layout.Axis)) {
                    $bar = $Matches
                }
            }
        }
        $fromBar = 0.0
        if ($bar) {
            $barPhysical = if ($horizontal) { [double]$bar[3] - [double]$bar[1] }
                           else { [double]$bar[4] - [double]$bar[2] }
            $fromBar = $barPhysical / ($bandDip / 2.0)
        }
        if ($fromBar -ge 1 -and $fromBar -le 4) {
            $layout.Scale = $fromBar; $layout.ScaleSource = 'taskbar thickness'
        } elseif ($fromWork -ge 1 -and $fromWork -le 4) {
            $layout.Scale = $fromWork; $layout.ScaleSource = 'screen/work area'
        }
        if ($fromBar -ge 1 -and $fromBar -le 4 -and $fromWork -ge 1 -and $fromWork -le 4 -and
            [Math]::Abs($fromBar - $fromWork) -gt 0.05 * $fromWork) {
            Write-Host ("WARN the taskbar-thickness scale {0:F3} and the screen/work scale {1:F3} disagree" -f
                $fromBar, $fromWork)
        }
    }
    return $layout
}

function Test-Layout {
    param($layout)
    if (-not $layout) { return 'no reveal panelTrail line at or after the run mark' }
    $missing = @()
    if ($layout.Axis -lt 0) { $missing += 'axis' }
    if ($layout.Right -le $layout.Left -or $layout.Bottom -le $layout.Top) { $missing += 'rect' }
    if ($layout.High -le $layout.Low) { $missing += 'band' }
    if (-not ($layout.Distance -gt 0)) { $missing += 'distance' }
    if (-not ($layout.Scale -ge 1 -and $layout.Scale -le 4)) { $missing += 'scale' }
    return ($missing -join ',')
}

function Wait-Layout {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $GeometryWaitMs) {
        $l = Get-Layout
        if ($l -and -not (Test-Layout $l)) { return $l }
        Start-Sleep -Milliseconds 100
    }
    return $null
}

function Write-TraceHint {
    Write-Host ("No usable reveal panelTrail record was written after the run mark in")
    Write-Host "  $Log"
    Write-Host 'The mod writes its trace only while DiagnosticTrace is on. Turn it on once'
    Write-Host 'from an elevated shell, then run again:'
    Write-Host '  windhawk-cli mod settings set local@swipe-for-10start DiagnosticTrace=true'
}

# A refusal after the tool opened the menu must not leave it open: one Win tap
# puts the shell back where it was.
function Close-MenuIfOpen {
    if ($mode -eq 'wintap' -and (Get-StartState)) {
        [PanelEdge]::Tap()
        $null = Wait-StartState -Want $false -TimeoutMs 4000
    }
}

# ---------------------------------------------------------------------------
# One open/close cycle that gives this run its geometry
# ---------------------------------------------------------------------------
Write-Host "RUN mode=$mode log=$Log screen=${screenW}x${screenH}"

if ($mode -eq 'wintap') {
    if (Get-StartState) {
        Write-Host 'STATE menu was open; one Win tap closes it before the baseline'
        [PanelEdge]::Tap()
        if (-not (Wait-StartState -Want $false -TimeoutMs 4000)) {
            Write-Host 'REFUSED the menu did not close; nothing captured'
            exit 1
        }
    }
    [PanelEdge]::Tap()
    if (-not (Wait-StartState -Want $true -TimeoutMs 4000)) {
        Write-Host 'REFUSED the menu did not open on a Win tap; nothing captured'
        exit 1
    }
    $layout = Wait-Layout
} else {
    Write-Host 'OBSERVE open and close the menu once: the tool needs this run''s own reveal panelTrail line'
    $layout = Wait-Layout
}

if (-not $layout) {
    Close-MenuIfOpen
    Write-TraceHint; Write-Host 'REFUSED geometry missing; nothing captured'; exit 1
}
$gap = Test-Layout $layout
if ($gap) {
    Close-MenuIfOpen
    Write-Host "REFUSED geometry incomplete (missing: $gap)"; Write-Host "  $($layout.Line)"; exit 1
}

if ($mode -eq 'wintap') {
    # The warm-up open served its purpose; close it so the baseline is the menu.
    [PanelEdge]::Tap()
    if (-not (Wait-StartState -Want $false -TimeoutMs 4000)) {
        Write-Host 'REFUSED the menu did not close; nothing captured'
        exit 1
    }
} else {
    Write-Host 'OBSERVE close the menu now; the baseline is taken when it is hidden'
    if (-not (Wait-StartState -Want $false -TimeoutMs 15000)) {
        Write-Host 'REFUSED the menu stayed open; nothing captured'
        exit 1
    }
}

Write-Host (("LAYOUT axis={0} trail={1}dip rect={2},{3},{4},{5} band={6},{7} " +
        "work={8},{9},{10},{11} distance={12}dip ({13}) scale={14:F3} ({15}) src={16}") -f
    $layout.Axis, $layout.Trail, $layout.Left, $layout.Top, $layout.Right, $layout.Bottom,
    $layout.Low, $layout.High, $layout.WorkLeft, $layout.WorkTop, $layout.WorkRight,
    $layout.WorkBottom, $layout.Distance, $layout.DistanceSource, $layout.Scale,
    $layout.ScaleSource, $layout.Source)
Write-Host "  $($layout.Line)"

# ---------------------------------------------------------------------------
# The scan: along the panel's trailing axis, a 96 px band inside the panel
# ---------------------------------------------------------------------------
$s = $layout.Scale
$horizontal = ($layout.Axis -eq 0 -or $layout.Axis -eq 2)
$dir = if ($layout.Axis -eq 0 -or $layout.Axis -eq 1) { -1.0 } else { 1.0 }
$increasing = ($layout.Axis -eq 0 -or $layout.Axis -eq 1)
$bandMidPx = [int](($layout.Low + $layout.High) / 2.0 * $s)
$panelLeftPx = [int]($layout.Left * $s); $panelTopPx = [int]($layout.Top * $s)
$panelRightPx = [int]($layout.Right * $s); $panelBottomPx = [int]($layout.Bottom * $s)
$trailing = @('right', 'bottom', 'left', 'top')[$layout.Axis]

if ($horizontal) {
    $acrossPx = $panelBottomPx - $panelTopPx
    $bandH = [Math]::Max(32, [Math]::Min(96, [int]($acrossPx / 2)))
    $bandY = [int]($panelTopPx + 0.15 * $acrossPx)
    if ($bandY + $bandH -gt $screenH) { $bandY = $screenH - $bandH }
    if ($layout.Axis -eq 0) {
        $x0 = $bandMidPx; $x1 = [Math]::Min($screenW - 1, $panelRightPx + 8)
    } else {
        $x0 = [Math]::Max(0, $panelLeftPx - 8); $x1 = $bandMidPx
    }
    $capX0 = $x0; $capY0 = $bandY; $capW = $x1 - $x0 + 1; $capH = $bandH
} else {
    $acrossPx = $panelRightPx - $panelLeftPx
    $bandW = [Math]::Max(32, [Math]::Min(96, [int]($acrossPx / 2)))
    $bandX = [int]($panelLeftPx + 0.15 * $acrossPx)
    if ($bandX + $bandW -gt $screenW) { $bandX = $screenW - $bandW }
    if ($layout.Axis -eq 1) {
        $y0 = $bandMidPx; $y1 = [Math]::Min($screenH - 1, $panelBottomPx + 8)
    } else {
        $y0 = [Math]::Max(0, $panelTopPx - 8); $y1 = $bandMidPx
    }
    $capX0 = $bandX; $capY0 = $y0; $capW = $bandW; $capH = $y1 - $y0 + 1
}

Write-Host (("MAP axis={0} trailing={1} dir={2} trail={3}dip distance={4}dip " +
        "progress=1-(edge-trail)/(dir*distance)") -f
    $layout.Axis, $trailing, $dir, $layout.Trail, $layout.Distance)
Write-Host (("REGION x={0}..{1} y={2}..{3} lines=63 threshold={4} majority={5}") -f
    $capX0, ($capX0 + $capW - 1), $capY0, ($capY0 + $capH - 1), $Threshold, $Majority)

[PanelEdge]::Init($capX0, $capY0, $capW, $capH, 63, $Threshold, $Majority, (-not $horizontal), $increasing)

# ---------------------------------------------------------------------------
# The pull's points, from the measured geometry: 2 DIP inside the band, 25 DIP
# inward (the mod qualifies at 24 DIP of signed inward travel).
# ---------------------------------------------------------------------------
$pushIn = 25.0
if ($horizontal) {
    $pullY = [int](($layout.Top + $layout.Bottom) / 2.0 * $s); $pullY2 = $pullY
    if ($layout.Axis -eq 0) {
        $pullX0 = [int](($layout.High - 2) * $s); $pullX1 = [int]($pullX0 + $pushIn * $s)
    } else {
        $pullX0 = [int](($layout.Low + 2) * $s); $pullX1 = [int]($pullX0 - $pushIn * $s)
    }
} else {
    $pullX0 = [int](($layout.Left + $layout.Right) / 2.0 * $s); $pullX1 = $pullX0
    if ($layout.Axis -eq 1) {
        $pullY = [int](($layout.High - 2) * $s); $pullY2 = [int]($pullY + $pushIn * $s)
    } else {
        $pullY = [int](($layout.Low + 2) * $s); $pullY2 = [int]($pullY - $pushIn * $s)
    }
}
Write-Host (("PULL from={0},{1} to={2},{3} steps={4} stepMs={5} holdMs={6}") -f
    $pullX0, $pullY, $pullX1, $pullY2, $PullSteps, $PullStepMs, $PullHoldMs)

# ---------------------------------------------------------------------------
# Baseline, then the run
# ---------------------------------------------------------------------------
if (Get-StartState) {
    Write-Host 'REFUSED the menu is open at the baseline; nothing captured'
    exit 1
}
[PanelEdge]::Baseline()
$verify = [PanelEdge]::Frame()
if ($verify -ne -1) {
    Write-Host ("REFUSED the closed baseline is not clean: a changed region reaches " +
        "$verify px in the frame captured right after it")
    exit 1
}
Write-Host 'BASELINE closed and verified (no changed column)'

Start-Sleep -Milliseconds $CooldownMs
if ($mode -eq 'wintap') {
    $planMs = $OpenAtMs + $CloseAtMs + $TailMs + 250
    [PanelEdge]::ConfigureTriggers($offsets.ToArray(), $kinds.ToArray(), $OpenAtMs, $CloseAtMs, $hostPid,
        $pullX0, $pullY, $pullX1, $pullY2, $PullSteps, $PullStepMs, $PullHoldMs)
} else {
    $planMs = $ObserveMs
    [PanelEdge]::ConfigureTriggers($offsets.ToArray(), $kinds.ToArray(), -1, 0, $hostPid,
        $pullX0, $pullY, $pullX1, $pullY2, $PullSteps, $PullStepMs, $PullHoldMs)
    Write-Host ("CAPTURE now ({0} ms); perform the trigger" -f $ObserveMs)
}

$cleanup = 'OK'
$errorText = ''
try {
    [PanelEdge]::Run($planMs, $TailMs)
} catch {
    $errorText = $_.Exception.Message
} finally {
    try { $cleanup = [PanelEdge]::StopAll() } catch { $cleanup = "stop failed: $($_.Exception.Message)" }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
$tOpen = [PanelEdge]::TOpen
$tClose = [PanelEdge]::TClose
$times = [PanelEdge]::Times
$edges = [PanelEdge]::Edges
$caps = [PanelEdge]::Caps

function Get-Progress {
    param([double]$edgePx)
    if ($edgePx -lt 0) { return $null }
    $p = 1.0 - (($edgePx / $s) - $layout.Trail) / ($dir * $layout.Distance)
    if ($p -lt 0) { return 0.0 }
    if ($p -gt 1) { return 1.0 }
    return $p
}

Write-Host ''
for ($i = 0; $i -lt $times.Count; $i++) {
    $p = Get-Progress $edges[$i]
    $pText = if ($null -eq $p) { '-' } else { '{0:F3}' -f $p }
    if ($tClose -gt 0) {
        't={0,7:F1} tc={1,7:F1} edge={2,5} progress={3}' -f $times[$i], ($times[$i] - $tClose), $edges[$i], $pText
    } else {
        't={0,7:F1} edge={1,5} progress={2}' -f $times[$i], $edges[$i], $pText
    }
}

foreach ($line in [PanelEdge]::Triggers) { Write-Host $line }

$usable = @($edges | Where-Object { $_ -ge 0 })
$period = 0.0; $minGap = 0.0; $maxGap = 0.0; $capMean = 0.0; $capMax = 0.0
if ($times.Count -gt 1) {
    $gaps = @(for ($i = 1; $i -lt $times.Count; $i++) { $times[$i] - $times[$i - 1] })
    $period = ($gaps | Measure-Object -Average).Average
    $minGap = ($gaps | Measure-Object -Minimum).Minimum
    $maxGap = ($gaps | Measure-Object -Maximum).Maximum
}
if ($caps.Count) {
    $capMean = ($caps | Measure-Object -Average).Average
    $capMax = ($caps | Measure-Object -Maximum).Maximum
}
$startEdge = if ($usable.Count) { $usable[0] } else { -1 }
$endEdge = if ($usable.Count) { $usable[$usable.Count - 1] } else { -1 }
$minEdge = if ($usable.Count) { ($usable | Measure-Object -Minimum).Minimum } else { -1 }
$maxEdge = if ($usable.Count) { ($usable | Measure-Object -Maximum).Maximum } else { -1 }
$atClose = -1
if ($tClose -gt 0) {
    for ($i = 0; $i -lt $times.Count; $i++) { if ($times[$i] -le $tClose) { $atClose = $edges[$i] } }
}
$expect = [int]($layout.Trail * $s)

Write-Host ''
Write-Host (("SUMMARY samples={0} usable={1} period={2:F1}ms (min {3:F1} max {4:F1}) cap={5:F1}ms " +
        "(max {6:F1}) start={7} at-close={8} end={9} min={10} max={11} missing={12}") -f
    $times.Count, $usable.Count, $period, $minGap, $maxGap, $capMean, $capMax,
    $startEdge, $atClose, $endEdge, $minEdge, $maxEdge, ($times.Count - $usable.Count))
Write-Host (("ANCHOR tOpen={0} tClose={1} expected-open-edge={2}px measured-max={3}px") -f
    $(if ($tOpen -ge 0) { '{0:F1}' -f $tOpen } else { '-' }),
    $(if ($tClose -ge 0) { '{0:F1}' -f $tClose } else { '-' }), $expect, $maxEdge)
if ($maxEdge -ge 0 -and [Math]::Abs($maxEdge - $expect) -gt 8) {
    Write-Host (("WARN the widest measured edge does not reach trail*scale ({0} vs {1}); the panel " +
        'may not have been fully open during the capture, or the edge lies off the rect') -f
        $maxEdge, $expect)
}
Write-Host "CLEANUP $cleanup"
Write-Host ("STATE at exit cloaked={0} open={1}" -f [PanelEdge]::Cloaked($hostPid), (Get-StartState))

if ($Csv -and $times.Count) {
    $sb = [Text.StringBuilder]::new()
    $null = $sb.AppendLine('t,tc,edge_px,edge_dip,progress')
    for ($i = 0; $i -lt $times.Count; $i++) {
        $p = Get-Progress $edges[$i]
        $tc = if ($tClose -gt 0) { '{0:F1}' -f ($times[$i] - $tClose) } else { '' }
        $edgePx = if ($edges[$i] -ge 0) { [string]$edges[$i] } else { '' }
        $edgeDip = if ($edges[$i] -ge 0) { '{0:F1}' -f ($edges[$i] / $s) } else { '' }
        $pText = if ($null -eq $p) { '' } else { '{0:F3}' -f $p }
        $null = $sb.AppendLine(('{0:F1},{1},{2},{3},{4}' -f $times[$i], $tc, $edgePx, $edgeDip, $pText))
    }
    $target = if ([IO.Path]::IsPathRooted($Csv)) { $Csv } else { Join-Path (Get-Location) $Csv }
    $dirPath = Split-Path -Parent $target
    if ($dirPath -and -not (Test-Path -LiteralPath $dirPath)) {
        $null = New-Item -ItemType Directory -Path $dirPath
    }
    [IO.File]::WriteAllText($target, $sb.ToString())
    Write-Host "CSV $target"
}

$status = [PanelEdge]::Status
$failed = ($usable.Count -eq 0) -or ($status -ne '') -or ($cleanup -ne 'OK') -or ($errorText -ne '')
if ($failed) {
    Write-Host ("RESULT failed status='{0}'{1}" -f $status,
        $(if ($errorText) { " error='$errorText'" } else { '' }))
    exit 1
}
Write-Host 'RESULT ok'
exit 0
