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
$code = @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class SettleDiff {
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int x; public int y; }
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINTER_INFO {
        public uint pointerType, pointerId, frameId, pointerFlags;
        public IntPtr sourceDevice, hwndTarget;
        public POINT ptPixelLocation, ptHimetricLocation, ptPixelLocationRaw, ptHimetricLocationRaw;
        public uint dwTime, historyCount;
        public int InputData;
        public uint dwKeyStates;
        public ulong PerformanceCount;
        public int ButtonChangeType;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINTER_TOUCH_INFO {
        public POINTER_INFO pointerInfo;
        public uint touchFlags, touchMask;
        public RECT rcContact, rcContactRaw;
        public uint orientation, pressure;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct BIH { public int biSize, biWidth, biHeight; public short biPlanes, biBitCount;
                 public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter;
                 public int biClrUsed, biClrImportant; }

    const uint PT_TOUCH = 2, INRANGE = 2, INCONTACT = 4;
    const uint DOWN = 0x00010000, UPDATE = 0x00020000, UP = 0x00040000;
    const uint SRCCOPY = 0x00CC0020;
    const int W = 2880, Y0 = 400, BH = 1020;

    [DllImport("user32.dll")] static extern bool InitializeTouchInjection(uint max, uint mode);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool InjectTouchInput(uint count, POINTER_TOUCH_INFO[] c);
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateDIBSection(IntPtr dc, ref BIH bmi,
        uint u, out IntPtr bits, IntPtr s, uint o);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
    [DllImport("gdi32.dll")] static extern bool BitBlt(IntPtr d, int x, int y, int w, int h,
        IntPtr s, int sx, int sy, uint rop);

    static POINTER_TOUCH_INFO[] contacts = new POINTER_TOUCH_INFO[1];
    static volatile bool keep;
    static Thread keepThread;
    static int holdX, holdY;
    static IntPtr screen, mem, dib, bits;
    static byte[] buf, baseline;
    static double Ms() { return Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency; }

    static string Send(int x, int y, uint flags) {
        contacts[0].pointerInfo.pointerType = PT_TOUCH;
        contacts[0].pointerInfo.pointerId = 0;
        contacts[0].pointerInfo.frameId = 0;
        contacts[0].pointerInfo.pointerFlags = flags;
        contacts[0].pointerInfo.ptPixelLocation = new POINT { x = x, y = y };
        return InjectTouchInput(1, contacts) ? "OK"
            : "InjectTouchInput failed: " + Marshal.GetLastWin32Error();
    }

    static void InitCapture() {
        screen = GetDC(IntPtr.Zero);
        BIH bmi = new BIH();
        bmi.biSize = Marshal.SizeOf(typeof(BIH));
        bmi.biWidth = W; bmi.biHeight = -BH; bmi.biPlanes = 1; bmi.biBitCount = 32;
        dib = CreateDIBSection(screen, ref bmi, 0, out bits, IntPtr.Zero, 0);
        mem = CreateCompatibleDC(screen);
        SelectObject(mem, dib);
        buf = new byte[W * BH * 4];
    }

    static void Grab() {
        BitBlt(mem, 0, 0, W, BH, screen, 0, Y0, SRCCOPY);
        Marshal.Copy(bits, buf, 0, buf.Length);
    }

    // lowest row (screen y) whose mean channel distance from the baseline is
    // above `thr`, scanning inside the panel's column range
    static int Edge(double thr) {
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

    public static string Run(int x0, int y0, int x1, int y1, int steps, int stepMs,
                             int holdMs, int trackMs) {
        SetProcessDpiAwarenessContext(new IntPtr(-4));
        InitCapture();
        Grab();
        baseline = (byte[])buf.Clone();
        if (!InitializeTouchInjection(256, 3))
            return "InitializeTouchInjection failed: " + Marshal.GetLastWin32Error();

        string r = Send(x0, y0, DOWN | INRANGE | INCONTACT);
        if (r != "OK") return "DOWN " + r;
        for (int i = 1; i <= steps; i++) {
            r = Send(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps,
                     UPDATE | INRANGE | INCONTACT);
            if (r != "OK") return "MOVE " + i + " " + r;
            Thread.Sleep(stepMs);
        }

        holdX = x1; holdY = y1;
        keep = true;
        keepThread = new Thread(delegate() {
            while (keep) { Send(holdX, holdY, UPDATE | INRANGE | INCONTACT); Thread.Sleep(10); }
        });
        keepThread.IsBackground = true;
        keepThread.Start();
        Thread.Sleep(holdMs);

        Grab();
        int held = Edge(18);
        StringBuilder sb = new StringBuilder();
        sb.AppendFormat("HELD edge={0}\n", held);

        r = Send(x1, y1, UP | INRANGE);
        double tUp = Ms();
        if (r != "OK") return "UP " + r;
        keep = false;
        keepThread.Join(500);

        while (Ms() - tUp < trackMs) {
            BitBlt(mem, 0, 0, W, BH, screen, 0, Y0, SRCCOPY);
            double t = Ms() - tUp;
            Marshal.Copy(bits, buf, 0, buf.Length);
            int e = Edge(18);
            sb.AppendFormat("T t={0:F1} edge={1}\n", t, e);
            Thread.Sleep(1);
        }
        return sb.ToString();
    }
}
'@

Add-Type -TypeDefinition $code
[Globalization.CultureInfo]::CurrentCulture = 'en-US'
[SettleDiff]::Run($StartX, $StartY, $HoldX, $HoldY, $Steps, $StepDelayMs, $HoldMs, $TrackMs)
