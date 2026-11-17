#requires -Version 7.0
<#
.SYNOPSIS
Capture one Quick Settings open or close animation at ~10 ms per frame.

.DESCRIPTION
ControlCenterWindow exists for the whole session; IsWindowVisible is its
open/closed signal (DWMWA_CLOAKED stays 0). A 1 ms poller records the moment
the window shows or hides while the capture loop samples the screen with a
reused DIB. DwmFlush is not used before each BitBlt: it costs a whole
compositor interval on top of the ~8 ms a screen read-back already takes, and
the read-back returns the last presented frame anyway. Frame times are
therefore the BitBlt start, with a 0-8 ms lag to the presented content; the
visibility poll pins the trigger to 1 ms.

Frames are written as top-down 32bpp BMPs (<prefix>-NN.bmp).
#>
[CmdletBinding()]
param(
    [ValidateSet('open', 'close')]
    [string]$Phase = 'open',
    [int]$X = 2100,
    [int]$Y = 96,
    [int]$W = 780,
    [int]$H = 1240,
    [int]$RunMs = 500,
    [string]$Prefix = 'C:\Users\doubao\AppData\Local\Temp\opencode\qsa'
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

public static class QSAnim {
    [StructLayout(LayoutKind.Sequential)]
    struct BIH { public int biSize, biWidth, biHeight; public short biPlanes, biBitCount;
                 public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter;
                 public int biClrUsed, biClrImportant; }
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr v);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr v);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateDIBSection(IntPtr dc, ref BIH bmi,
        uint u, out IntPtr bits, IntPtr s, uint o);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
    [DllImport("gdi32.dll")] static extern bool BitBlt(IntPtr d, int x, int y, int w, int h,
        IntPtr s, int sx, int sy, uint rop);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);

    static double Ms() { return Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency; }
    static IntPtr screenDc, memDc, dib, bits, target;
    static int W, H;
    static volatile bool stopPoll;
    static List<string> poll = new List<string>();
    static double tOrigin;

    static void MakeAware() {
        IntPtr v = new IntPtr(-4);
        IntPtr t = SetThreadDpiAwarenessContext(v);
        if (t == IntPtr.Zero) SetProcessDpiAwarenessContext(v);
    }

    static void Init(int w, int h) {
        W = w; H = h;
        BIH bmi = new BIH();
        bmi.biSize = Marshal.SizeOf(typeof(BIH));
        bmi.biWidth = w; bmi.biHeight = -h; bmi.biPlanes = 1; bmi.biBitCount = 32;
        dib = CreateDIBSection(screenDc, ref bmi, 0, out bits, IntPtr.Zero, 0);
        memDc = CreateCompatibleDC(screenDc);
        SelectObject(memDc, dib);
    }

    static void Poll() {
        bool last = IsWindowVisible(target);
        while (!stopPoll) {
            bool now = IsWindowVisible(target);
            if (now != last) {
                poll.Add(string.Format("VIS t={0:F2} visible={1}", Ms() - tOrigin,
                                       now ? 1 : 0));
                last = now;
            }
            Thread.Sleep(1);
        }
    }

    public static void WinA() {
        keybd_event(0x5B, 0, 0, UIntPtr.Zero);
        keybd_event(0x41, 0, 0, UIntPtr.Zero);
        Thread.Sleep(40);
        keybd_event(0x41, 0, 2, UIntPtr.Zero);
        keybd_event(0x5B, 0, 2, UIntPtr.Zero);
    }

    static void SaveBmp(string path, byte[] px, int w, int h) {
        using (FileStream fs = new FileStream(path, FileMode.Create, FileAccess.Write))
        using (BinaryWriter bw = new BinaryWriter(fs)) {
            bw.Write((ushort)0x4D42);
            bw.Write(14 + 40 + px.Length);
            bw.Write(0);
            bw.Write(14 + 40);
            bw.Write(40);
            bw.Write(w);
            bw.Write(-h);
            bw.Write((ushort)1);
            bw.Write((ushort)32);
            bw.Write(0);
            bw.Write(px.Length);
            bw.Write(0); bw.Write(0); bw.Write(0); bw.Write(0);
            bw.Write(px);
        }
    }

    public static string Run(string phase, int x, int y, int w, int h, int runMs,
                             string prefix) {
        MakeAware();
        screenDc = GetDC(IntPtr.Zero);
        Init(w, h);
        target = FindWindow("ControlCenterWindow", null);
        tOrigin = Ms();
        StringBuilder sb = new StringBuilder();
        sb.AppendFormat("START phase={0} hwnd={1} visible={2}\n", phase, target,
                        IsWindowVisible(target) ? 1 : 0);

        Thread poller = new Thread(Poll);
        poller.IsBackground = true;
        poller.Start();

        byte[] buf = new byte[w * h * 4];
        List<byte[]> shots = new List<byte[]>();
        List<double> times = new List<double>();

        // A few frames before the key, so the still state is part of the file.
        for (int i = 0; i < 3; i++) {
            BitBlt(memDc, 0, 0, w, h, screenDc, x, y, 0x00CC0020);
            Marshal.Copy(bits, buf, 0, buf.Length);
            shots.Add((byte[])buf.Clone());
            times.Add(Ms() - tOrigin);
        }

        Thread trigger = new Thread(() => WinA());
        trigger.IsBackground = true;
        double tKey = Ms();
        trigger.Start();

        double t0 = Ms();
        while (Ms() - t0 < runMs) {
            BitBlt(memDc, 0, 0, w, h, screenDc, x, y, 0x00CC0020);
            Marshal.Copy(bits, buf, 0, buf.Length);
            shots.Add((byte[])buf.Clone());
            times.Add(Ms() - tOrigin);
        }
        trigger.Join();

        for (int i = 0; i < shots.Count; i++) {
            SaveBmp(prefix + "-" + i.ToString("d2") + ".bmp", shots[i], w, h);
        }

        sb.AppendFormat("KEY t={0:F2}\n", tKey - tOrigin);
        for (int i = 0; i < times.Count; i++) {
            sb.AppendFormat("F {0} t={1:F2}\n", i, times[i]);
        }

        Thread.Sleep(200);
        stopPoll = true;
        poller.Join();
        foreach (string line in poll) sb.AppendLine(line);
        sb.AppendFormat("END visible={0}\n", IsWindowVisible(target) ? 1 : 0);
        return sb.ToString();
    }
}
'@

Add-Type -TypeDefinition $code
$text = [QSAnim]::Run($Phase, $X, $Y, $W, $H, $RunMs, $Prefix)
$text | Where-Object { $_ -notmatch '^F \d+ t=' }
$frames = @($text -split "`r?`n" | Where-Object { $_ -match '^F \d+ t=' })
$times = @($text -split "`r?`n" | Where-Object { $_ -match '^F \d+ t=' } | ForEach-Object { [double](($_ -split 't=')[1]) })
$sd = if ($times.Count -gt 2) {
    $d = @(); for ($i = 1; $i -lt $times.Count; $i++) { $d += $times[$i] - $times[$i-1] }
    $m = ($d | Measure-Object -Average).Average
    $j = ($d | Measure-Object -Maximum).Maximum
    "FRAMES n=$($times.Count) meanGap=$([math]::Round($m,2))ms maxGap=$([math]::Round($j,2))ms"
} else { 'FRAMES n<3' }
$sd
Set-Content -LiteralPath "$Prefix.txt" -Value $text -Encoding utf8
