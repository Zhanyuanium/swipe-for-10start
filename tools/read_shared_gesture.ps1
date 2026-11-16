#requires -Version 7.0
<#
.SYNOPSIS
Sample the explorer gesture bridge shared memory, optionally while injecting a
touch swipe.

Example:
  pwsh -File tools/read_shared_gesture.ps1 -Swipe -StartX 1 -StartY 900 -EndX 900 -EndY 900
#>
[CmdletBinding()]
param(
    [switch]$Swipe,
    [int]$StartX = 1,
    [int]$StartY = 900,
    [int]$EndX = 900,
    [int]$EndY = 900,
    [int]$Steps = 30,
    [int]$DelayMs = 8,
    [int]$DurationMs = 3000,
    [int]$IntervalMs = 15,
    [string]$OutputPath = "$PSScriptRoot\..\artifacts\gesture-shared.csv"
)

$ErrorActionPreference = 'Stop'

$code = @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class GestureSharedReader {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr OpenFileMappingW(uint access, bool inherit, string name);

    [DllImport("kernel32.dll")]
    public static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint offsetHigh, uint offsetLow, UIntPtr bytes);

    [DllImport("kernel32.dll")]
    public static extern bool UnmapViewOfFile(IntPtr view);

    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);

    static volatile bool stop;
    static Thread thread;

    static float FloatAt(IntPtr p, int offset) {
        return BitConverter.Int32BitsToSingle(Marshal.ReadInt32(p, offset));
    }

    static string Read(IntPtr p) {
        int magic = Marshal.ReadInt32(p, 0);
        int sequence = Marshal.ReadInt32(p, 8);
        int state = Marshal.ReadInt32(p, 12);
        int edge = Marshal.ReadInt32(p, 16);
        long sourceId = Marshal.ReadInt64(p, 24);
        long providerId = Marshal.ReadInt64(p, 32);
        float startX = FloatAt(p, 40);
        float startY = FloatAt(p, 44);
        float currentX = FloatAt(p, 48);
        float currentY = FloatAt(p, 52);
        long gestureStartTick = Marshal.ReadInt64(p, 56);
        long lastUpdateTick = Marshal.ReadInt64(p, 64);
        return string.Format(
            "{0},{1},{2},{3},{4},{5},{6:F1},{7:F1},{8:F1},{9:F1},{10},{11}",
            magic, sequence, state, edge, sourceId, providerId,
            startX, startY, currentX, currentY,
            gestureStartTick, lastUpdateTick);
    }

    static void ThreadProc(int durationMs, int intervalMs, string path) {
        IntPtr mapping = OpenFileMappingW(0x0004, false, "Local\\WindhawkStartGestureShared");
        if (mapping == IntPtr.Zero) {
            File.WriteAllText(path, "error,no mapping\n");
            return;
        }

        IntPtr view = MapViewOfFile(mapping, 0x0004, 0, 0, (UIntPtr)96);
        if (view == IntPtr.Zero) {
            File.WriteAllText(path, "error,no view\n");
            CloseHandle(mapping);
            return;
        }

        try {
            string directory = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(directory)) {
                Directory.CreateDirectory(directory);
            }
            using (var writer = new StreamWriter(path, false, new UTF8Encoding(false))) {
                writer.WriteLine("tick,magic,seq,state,edge,src,prov,startX,startY,currentX,currentY,startTick,lastTick");
                long started = Environment.TickCount64;
                while (!stop && Environment.TickCount64 - started < durationMs) {
                    long tick = Environment.TickCount64 - started;
                    writer.WriteLine(tick + "," + Read(view));
                    writer.Flush();
                    Thread.Sleep(intervalMs);
                }
            }
        } finally {
            UnmapViewOfFile(view);
            CloseHandle(mapping);
        }
    }

    public static void Start(int durationMs, int intervalMs, string path) {
        stop = false;
        thread = new Thread(() => ThreadProc(durationMs, intervalMs, path));
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

Add-Type -TypeDefinition $code -Language CSharp

[GestureSharedReader]::Start($DurationMs, $IntervalMs, $OutputPath)

if ($Swipe) {
    Start-Sleep -Milliseconds 500
    $inject = Join-Path $PSScriptRoot 'inject_swipe.ps1'
    & $inject -StartX $StartX -StartY $StartY -EndX $EndX -EndY $EndY -Steps $Steps -DelayMs $DelayMs
    Start-Sleep -Milliseconds 600
} else {
    Start-Sleep -Milliseconds $DurationMs
}

[GestureSharedReader]::Stop()

$rows = Import-Csv $OutputPath
Write-Host ("samples: {0}, file: {1}" -f $rows.Count, $OutputPath)
$interesting = $rows | Where-Object { $_.state -ne '0' -or $_.src -ne '0' -or $_.prov -ne '0' -or $_.edge -ne '-1' }
if ($interesting.Count -eq 0) {
    Write-Host "No gesture state/handle observed."
    return
}
$interesting | Select-Object -First 5 | Format-Table -AutoSize
Write-Host "..."
$interesting | Select-Object -Last 5 | Format-Table -AutoSize
