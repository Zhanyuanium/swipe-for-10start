#requires -Version 7.0
<#
.SYNOPSIS
Inject a touch drag, keep the contact reporting while the screen is captured,
then lift.

.DESCRIPTION
The virtual digitizer stops reporting after the last injected report, while the
real panel reports at ~170 Hz. A plain hold therefore goes silent for longer
than the mod's 35 ms idle release and the capture shows the post-release state.
This script sends an UPDATE every 10 ms from a background thread for as long as
the screenshot takes, so the reveal follow is still live when the pixels are
read. The UP is sent after the capture.

Coordinates are physical screen pixels (this machine is 2880x1800 at 200%).

.EXAMPLE
pwsh -File tools/follow_hold.ps1 -StartX 0 -StartY 900 -HoldX 900 -HoldY 900
#>
[CmdletBinding()]
param(
    [int]$StartX = 0,
    [int]$StartY = 900,
    [int]$HoldX = 900,
    [int]$HoldY = 900,
    [int]$Steps = 30,
    [int]$StepDelayMs = 10,
    [string]$ScreenshotPath = "$env:LOCALAPPDATA\Temp\opencode\follow.png"
)

$ErrorActionPreference = 'Stop'

$code = @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class TouchFollowHold {
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int x; public int y; }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int left, top, right, bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINTER_INFO {
        public uint pointerType;
        public uint pointerId;
        public uint frameId;
        public uint pointerFlags;
        public IntPtr sourceDevice;
        public IntPtr hwndTarget;
        public POINT ptPixelLocation;
        public POINT ptHimetricLocation;
        public POINT ptPixelLocationRaw;
        public POINT ptHimetricLocationRaw;
        public uint dwTime;
        public uint historyCount;
        public int InputData;
        public uint dwKeyStates;
        public ulong PerformanceCount;
        public int ButtonChangeType;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINTER_TOUCH_INFO {
        public POINTER_INFO pointerInfo;
        public uint touchFlags;
        public uint touchMask;
        public RECT rcContact;
        public RECT rcContactRaw;
        public uint orientation;
        public uint pressure;
    }

    const uint PT_TOUCH = 2;
    const uint INRANGE = 0x00000002;
    const uint INCONTACT = 0x00000004;
    const uint DOWN = 0x00010000;
    const uint UPDATE = 0x00020000;
    const uint UP = 0x00040000;

    static POINTER_TOUCH_INFO[] contacts = new POINTER_TOUCH_INFO[1];
    static volatile bool keepAlive;
    static Thread keepAliveThread;
    static int holdX;
    static int holdY;

    [DllImport("user32.dll")]
    static extern bool InitializeTouchInjection(uint maxCount, uint mode);

    [DllImport("user32.dll", SetLastError = true)]
    static extern bool InjectTouchInput(uint count, POINTER_TOUCH_INFO[] c);

    [DllImport("user32.dll")]
    static extern bool SetProcessDpiAwarenessContext(IntPtr context);

    public static string Begin(int x, int y) {
        SetProcessDpiAwarenessContext(new IntPtr(-4));
        if (!InitializeTouchInjection(256, 3))
            return "InitializeTouchInjection failed: " + Marshal.GetLastWin32Error();
        contacts = new POINTER_TOUCH_INFO[1];
        return Send(x, y, DOWN | INRANGE | INCONTACT);
    }

    public static string MoveTo(int x, int y) {
        return Send(x, y, UPDATE | INRANGE | INCONTACT);
    }

    // The move loop must live in C#: PowerShell's Start-Sleep between steps
    // takes 15-25 ms, which exceeds the mod's 18 ms idle window and splits the
    // gesture into several sessions.
    public static string Glide(int x0, int y0, int x1, int y1, int steps, int delayMs) {
        for (int i = 1; i <= steps; i++) {
            int x = x0 + (x1 - x0) * i / steps;
            int y = y0 + (y1 - y0) * i / steps;
            string r = Send(x, y, UPDATE | INRANGE | INCONTACT);
            if (r != "OK") return "glide " + i + ": " + r;
            Thread.Sleep(delayMs);
        }
        return "OK";
    }

    public static string End(int x, int y) {
        return Send(x, y, UP | INRANGE);
    }

    // The contact must keep reporting while the caller captures the screen.
    // One UPDATE per 10 ms stays above the real panel's report rate and keeps
    // the mod's 35 ms idle release from ending the gesture.
    public static string StartKeepAlive(int x, int y) {
        if (keepAliveThread != null)
            return "keep-alive already running";
        holdX = x;
        holdY = y;
        keepAlive = true;
        keepAliveThread = new Thread(delegate() {
            while (keepAlive) {
                Send(holdX, holdY, UPDATE | INRANGE | INCONTACT);
                Thread.Sleep(10);
            }
        });
        keepAliveThread.IsBackground = true;
        keepAliveThread.Start();
        return "OK";
    }

    public static string StopKeepAlive() {
        keepAlive = false;
        if (keepAliveThread != null) {
            keepAliveThread.Join(500);
            keepAliveThread = null;
        }
        return "OK";
    }

    static string Send(int x, int y, uint flags) {
        contacts[0].pointerInfo.pointerType = PT_TOUCH;
        contacts[0].pointerInfo.pointerId = 0;
        contacts[0].pointerInfo.frameId = 0;
        contacts[0].pointerInfo.pointerFlags = flags;
        contacts[0].pointerInfo.ptPixelLocation = new POINT { x = x, y = y };
        if (!InjectTouchInput(1, contacts))
            return "InjectTouchInput failed: " + Marshal.GetLastWin32Error();
        return "OK";
    }
}
'@

Add-Type -TypeDefinition $code -Language CSharp

$r = [TouchFollowHold]::Begin($StartX, $StartY)
if ($r -ne 'OK') { throw "begin: $r" }

$r = [TouchFollowHold]::Glide($StartX, $StartY, $HoldX, $HoldY, $Steps, $StepDelayMs)
if ($r -ne 'OK') { throw "glide: $r" }

try {
    $r = [TouchFollowHold]::StartKeepAlive($HoldX, $HoldY)
    if ($r -ne 'OK') { throw "keep-alive: $r" }

    # Capture inline: spawning screenshot.ps1 as a child process took long
    # enough that the mod's idle-release timer fired before the pixels were read.
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    } finally {
        $g.Dispose()
    }
    $bmp.Save($ScreenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "SHOT path=$ScreenshotPath width=$($bounds.Width) height=$($bounds.Height) hold=$HoldX,$HoldY"
} finally {
    [TouchFollowHold]::StopKeepAlive() | Out-Null
    $r = [TouchFollowHold]::End($HoldX, $HoldY)
    if ($r -ne 'OK') { throw "end: $r" }
}

'LIFTED'
