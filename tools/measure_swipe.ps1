#requires -Version 7.0
<#
.SYNOPSIS
Sample the Start window rect while injecting a touch swipe.

Writes a CSV with one row per sample and prints a small summary. Useful for
comparing the current legacy behavior with the modded behavior.

Example:
  pwsh -File tools/measure_swipe.ps1 -StartX 1 -StartY 900 -EndX 900 -EndY 900
#>
[CmdletBinding()]
param(
    [int]$StartX = 1,
    [int]$StartY = 900,
    [int]$EndX = 900,
    [int]$EndY = 900,
    [int]$Steps = 60,
    [int]$DelayMs = 10,
    [int]$PreSwipeMs = 300,
    [int]$PostSwipeMs = 800,
    [int]$IntervalMs = 8,
    [string]$OutputPath = "$PSScriptRoot\..\artifacts\start-window.csv"
)

$ErrorActionPreference = 'Stop'

$samplerCode = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class StartWindowSampler {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int left, top, right, bottom; }

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder name, int count);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr hWnd, int attribute, out int value, int size);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr context);

    static volatile bool stop;
    static Thread thread;

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int x; public int y; }

    [DllImport("user32.dll")]
    public static extern IntPtr WindowFromPoint(POINT point);

    [DllImport("user32.dll")]
    public static extern IntPtr GetAncestor(IntPtr hWnd, uint flags);

    static IntPtr FindStartWindow(uint pid, out int cloaked) {
        // EnumWindows does not reliably list the UWP Start CoreWindow in this
        // environment; hit-test a point inside the menu instead.
        var point = new POINT { x = 300, y = 300 };
        IntPtr hWnd = GetAncestor(WindowFromPoint(point), 2);  // GA_ROOT
        cloaked = 0;
        if (hWnd == IntPtr.Zero) {
            return IntPtr.Zero;
        }

        uint windowPid;
        GetWindowThreadProcessId(hWnd, out windowPid);
        if (windowPid != pid) {
            return IntPtr.Zero;
        }

        var className = new StringBuilder(128);
        GetClassName(hWnd, className, className.Capacity);
        if (className.ToString() != "Windows.UI.Core.CoreWindow") {
            return IntPtr.Zero;
        }

        DwmGetWindowAttribute(hWnd, 14, out cloaked, sizeof(int));
        return hWnd;
    }

    static void ThreadProc(uint pid, int durationMs, int intervalMs, string path) {
        var started = Environment.TickCount64;
        using (var writer = new StreamWriter(path, false, new UTF8Encoding(false))) {
            writer.WriteLine("tick,pid,hwnd,visible,cloaked,left,top,right,bottom");
            while (!stop && Environment.TickCount64 - started < durationMs) {
                long tick = Environment.TickCount64 - started;
                uint actualPid;
                try {
                    actualPid = (uint)Process.GetProcessById((int)pid).Id;
                } catch {
                    actualPid = pid;
                }

                int cloaked;
                IntPtr hWnd = FindStartWindow(actualPid, out cloaked);
                RECT rect = new RECT();
                bool visible = hWnd != IntPtr.Zero && IsWindowVisible(hWnd);
                if (hWnd != IntPtr.Zero) {
                    GetWindowRect(hWnd, out rect);
                }
                writer.WriteLine("{0},{1},{2},{3},{4},{5},{6},{7},{8}",
                    tick, actualPid, hWnd.ToInt64(), visible ? 1 : 0, cloaked,
                    rect.left, rect.top, rect.right, rect.bottom);
                writer.Flush();
                Thread.Sleep(intervalMs);
            }
        }
    }

    public static void Start(uint pid, int durationMs, int intervalMs, string path) {
        SetProcessDpiAwarenessContext(new IntPtr(-4));
        stop = false;
        string directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory)) {
            Directory.CreateDirectory(directory);
        }
        thread = new Thread(() => ThreadProc(pid, durationMs, intervalMs, path));
        thread.IsBackground = true;
        thread.Start();
    }

    public static void Stop() {
        stop = true;
        if (thread != null) {
            thread.Join(2000);
            thread = null;
        }
    }
}
'@

Add-Type -TypeDefinition $samplerCode -Language CSharp

$startProcess = Get-Process StartMenuExperienceHost -ErrorAction Stop | Select-Object -First 1
$durationMs = $PreSwipeMs + ($Steps + 2) * $DelayMs + $PostSwipeMs
[StartWindowSampler]::Start([uint32]$startProcess.Id, $durationMs, $IntervalMs, $OutputPath)

Start-Sleep -Milliseconds $PreSwipeMs

$inject = Join-Path $PSScriptRoot 'inject_swipe.ps1'
& $inject -StartX $StartX -StartY $StartY -EndX $EndX -EndY $EndY -Steps $Steps -DelayMs $DelayMs

Start-Sleep -Milliseconds $PostSwipeMs
[StartWindowSampler]::Stop()

$rows = Import-Csv $OutputPath
Write-Host ("samples: {0}, file: {1}" -f $rows.Count, $OutputPath)

# The Start CoreWindow can have WS_VISIBLE cleared while DWM still composites
# it, so presence is determined by the window handle, not IsWindowVisible.
$present = $rows | Where-Object { $_.hwnd -ne '0' }
if ($present.Count -eq 0) {
    Write-Host "Start window was not found during the sample."
    return
}

$first = $present | Select-Object -First 1
$last = $present | Select-Object -Last 1
$minLeft = ($present | Measure-Object -Property left -Minimum).Minimum
$maxLeft = ($present | Measure-Object -Property left -Maximum).Maximum
$minTop = ($present | Measure-Object -Property top -Minimum).Minimum
$maxTop = ($present | Measure-Object -Property top -Maximum).Maximum

Write-Host ("first visible: hwnd={0} rect={1},{2} {3}x{4}" -f `
    $first.hwnd, $first.left, $first.top,
    ([int]$first.right - [int]$first.left), ([int]$first.bottom - [int]$first.top))
Write-Host ("last visible:  hwnd={0} rect={1},{2} {3}x{4}" -f `
    $last.hwnd, $last.left, $last.top,
    ([int]$last.right - [int]$last.left), ([int]$last.bottom - [int]$last.top))
Write-Host ("left range: {0}..{1}; top range: {2}..{3}" -f `
    $minLeft, $maxLeft, $minTop, $maxTop)
