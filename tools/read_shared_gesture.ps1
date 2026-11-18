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
    static int retries;
    static int unstable;
    static bool unpublished;
    static string error;

    // The header the bridge publishes (mods/start_gesture_shared.h):
    // magic 'SGPB', version 1. A reader that skips the check can mistake a
    // mapping with no publisher for a gesture.
    const int Magic = 0x53475042;
    const int Version = 1;
    const int RetryLimit = 64;

    static float FloatAt(IntPtr p, int offset) {
        return BitConverter.Int32BitsToSingle(Marshal.ReadInt32(p, offset));
    }

    // One sample under the seqlock. The header documents the protocol: the
    // writer bumps `sequence` around every update, so an odd counter or a
    // counter that changed between the two reads means the snapshot is torn
    // and must be retried. Without the retry a torn read looks like a state
    // transition that never happened.
    static string Read(IntPtr p, out bool ok) {
        ok = false;
        for (int attempt = 0; attempt < RetryLimit; attempt++) {
            if (Marshal.ReadInt32(p, 0) != Magic) {
                unpublished = true;
                return null;
            }
            int s0 = Marshal.ReadInt32(p, 8);
            if ((s0 & 1) != 0) { retries++; continue; }

            int sequence = s0;
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

            Thread.MemoryBarrier();
            int s1 = Marshal.ReadInt32(p, 8);
            if (s0 != s1) { retries++; continue; }

            ok = true;
            return string.Format(
                "{0},{1},{2},{3},{4},{5},{6:F1},{7:F1},{8:F1},{9:F1},{10},{11}",
                Magic, sequence, state, edge, sourceId, providerId,
                startX, startY, currentX, currentY,
                gestureStartTick, lastUpdateTick);
        }
        return null;
    }

    static void ThreadProc(int durationMs, int intervalMs, string path) {
        IntPtr mapping = OpenFileMappingW(0x0004, false, "Local\\WindhawkStartGestureShared");
        if (mapping == IntPtr.Zero) {
            error = "no mapping (is the gesture bridge enabled?)";
            File.WriteAllText(path, "error,no mapping\n");
            return;
        }

        IntPtr view = MapViewOfFile(mapping, 0x0004, 0, 0, (UIntPtr)96);
        if (view == IntPtr.Zero) {
            error = "no view";
            File.WriteAllText(path, "error,no view\n");
            CloseHandle(mapping);
            return;
        }

        try {
            // The publisher writes magic last, so a mapping that exists but is
            // not published yet gets a short grace instead of a fake sample.
            for (int i = 0; i < 20 && Marshal.ReadInt32(view, 0) != Magic; i++) {
                Thread.Sleep(10);
            }
            int magic = Marshal.ReadInt32(view, 0);
            int version = Marshal.ReadInt32(view, 4);
            if (magic != Magic || version != Version) {
                error = string.Format(
                    "header is magic=0x{0:X8} version={1}, expected 0x{2:X8}/{3}",
                    magic, version, Magic, Version);
                File.WriteAllText(path, "error,bad header\n");
                return;
            }

            string directory = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(directory)) {
                Directory.CreateDirectory(directory);
            }
            using (var writer = new StreamWriter(path, false, new UTF8Encoding(false))) {
                writer.WriteLine("tick,magic,seq,state,edge,src,prov,startX,startY,currentX,currentY,startTick,lastTick");
                long started = Environment.TickCount64;
                while (!stop && Environment.TickCount64 - started < durationMs) {
                    long tick = Environment.TickCount64 - started;
                    bool ok;
                    string row = Read(view, out ok);
                    if (!ok) {
                        if (unpublished) {
                            error = string.Format("section unpublished at t={0} ms", tick);
                            break;
                        }
                        unstable++;
                        Thread.Sleep(1);  // a busy writer: do not hot-spin on it
                        continue;
                    }
                    writer.WriteLine(tick + "," + row);
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
        unpublished = false;
        error = null;
        retries = 0;
        unstable = 0;
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

    public static int Retries { get { return retries; } }
    public static int UnstableSamples { get { return unstable; } }
    public static string Error { get { return error; } }
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

$readError = [GestureSharedReader]::Error
if ($readError) {
    Write-Host ("gesture shared memory: {0}" -f $readError)
    return
}

$rows = @(Import-Csv $OutputPath)
Write-Host ("samples: {0}, file: {1}" -f $rows.Count, $OutputPath)
Write-Host ("seqlock: {0} retries, {1} unstable samples dropped" -f `
    [GestureSharedReader]::Retries, [GestureSharedReader]::UnstableSamples)
$interesting = $rows | Where-Object { $_.state -ne '0' -or $_.src -ne '0' -or $_.prov -ne '0' -or $_.edge -ne '-1' }
if ($interesting.Count -eq 0) {
    Write-Host "No gesture state/handle observed."
    return
}
$interesting | Select-Object -First 5 | Format-Table -AutoSize
Write-Host "..."
$interesting | Select-Object -Last 5 | Format-Table -AutoSize
