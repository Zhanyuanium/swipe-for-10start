#requires -Version 7.0
<#
.SYNOPSIS
Track the menu panel's bottom edge after a half-open hold release, by diffing
each frame against a closed-menu baseline.

.DESCRIPTION
The panel is a translucent dark surface, so every row it covers differs from the
closed desktop. The lowest row with a significant difference is its bottom edge,
whatever content the panel shows. The baseline is captured while the menu is
closed; the caller must ensure that state.

The gesture is the shared touch lifecycle from tools/touch_common.ps1: DOWN,
glide to the hold point, a real hold (an UPDATE every 5 ms), a screenshot-level
edge reading while the contact is still down, then End-Contact. The release
timestamp is the one End-Contact takes immediately before its UP injection, so
`t=0` is the release itself and not the end of the preceding capture. The
keep-alive thread is stopped and joined before that UP, so no UPDATE follows it.

Each tracked frame stamps `t` before its BitBlt and reads the edge after it:
`t` is the capture start, the edge scan costs a few ms on top and does not shift
the stamp. There is no extra sleep between frames; PowerShell's 1 ms sleep takes
15.6 ms on this host and would set the frame rate instead of the capture.

One band (y 400..1420, full width) per frame, one reused DIB.
#>
[CmdletBinding()]
param(
    [int]$StartX = 1500,
    [int]$StartY = 12,
    [int]$HoldX = 1500,
    [int]$HoldY = 452,
    [int]$Steps = 40,
    [int]$StepDelayMs = 10,
    [int]$HoldMs = 400,
    [int]$TrackMs = 500
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'touch_common.ps1')

$code = @'
using System;
using System.Runtime.InteropServices;

public static class SettleDiff {
    [StructLayout(LayoutKind.Sequential)]
    struct BIH { public int biSize, biWidth, biHeight; public short biPlanes, biBitCount;
                 public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter;
                 public int biClrUsed, biClrImportant; }

    const uint SRCCOPY = 0x00CC0020;
    const int W = 2880, Y0 = 400, BH = 1020;

    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateDIBSection(IntPtr dc, ref BIH bmi,
        uint u, out IntPtr bits, IntPtr s, uint o);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
    [DllImport("gdi32.dll")] static extern bool BitBlt(IntPtr d, int x, int y, int w, int h,
        IntPtr s, int sx, int sy, uint rop);

    static IntPtr screen, mem, dib, bits;
    static byte[] buf, baseline;

    public static void Init() {
        screen = GetDC(IntPtr.Zero);
        BIH bmi = new BIH();
        bmi.biSize = Marshal.SizeOf(typeof(BIH));
        bmi.biWidth = W; bmi.biHeight = -BH; bmi.biPlanes = 1; bmi.biBitCount = 32;
        dib = CreateDIBSection(screen, ref bmi, 0, out bits, IntPtr.Zero, 0);
        mem = CreateCompatibleDC(screen);
        SelectObject(mem, dib);
        buf = new byte[W * BH * 4];
    }

    public static void Grab() {
        BitBlt(mem, 0, 0, W, BH, screen, 0, Y0, SRCCOPY);
        Marshal.Copy(bits, buf, 0, buf.Length);
    }

    public static void CaptureBaseline() {
        Grab();
        baseline = (byte[])buf.Clone();
    }

    // Lowest row (screen y) whose mean channel distance from the baseline is
    // above `thr`, scanning inside the panel's column range. `thr` is a
    // brightness distance per pixel, not a time.
    public static int Edge(double thr) {
        int x0 = 520, x1 = 2400;
        int last = -1;
        for (int y = 0; y < BH; y++) {
            long sum = 0;
            int row = y * W * 4;
            for (int x = x0; x < x1; x++) {
                int i = row + x * 4;
                sum += Math.Abs(buf[i] - baseline[i]) +
                       Math.Abs(buf[i + 1] - baseline[i + 1]) +
                       Math.Abs(buf[i + 2] - baseline[i + 2]);
            }
            if (sum > thr * (x1 - x0) * 3) last = y + Y0;
        }
        return last;
    }
}
'@

Add-Type -TypeDefinition $code
[Globalization.CultureInfo]::CurrentCulture = 'en-US'

$edgeThreshold = 18

# The capture reads the physical screen, and the injection needs the same
# coordinate space, so the DPI switch is the shared one.
Set-TouchDpiAware
[SettleDiff]::Init()
[SettleDiff]::CaptureBaseline()

Start-Contact -X $StartX -Y $StartY
Glide-Contact -X $HoldX -Y $HoldY -Steps $Steps -DelayMs $StepDelayMs

Start-ContactHold
try {
    Start-Sleep -Milliseconds $HoldMs
    [SettleDiff]::Grab()
    "HELD edge={0}" -f [SettleDiff]::Edge($edgeThreshold)
} finally {
    try { Stop-ContactHold } finally { End-Contact }
}

$tUp = [TouchCommon.Injector]::ReleaseMs
while ($true) {
    $t = [TouchCommon.Injector]::NowMs() - $tUp
    if ($t -ge $TrackMs) { break }
    [SettleDiff]::Grab()
    "T t={0:F1} edge={1}" -f $t, [SettleDiff]::Edge($edgeThreshold)
}
