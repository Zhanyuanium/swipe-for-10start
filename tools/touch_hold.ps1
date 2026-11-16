#requires -Version 7.0
<#
.SYNOPSIS
Inject a touch drag, pause mid-gesture, capture the screen, then lift.

.DESCRIPTION
Used to tell a real finger-following translation from an animation that keeps
running on its own: the screenshot is taken while the contact is still down at
the paused position, so the content must be sitting at that position.

Coordinates are physical screen pixels (this machine is 2880x1800 at 200%).

.EXAMPLE
pwsh -File tools/touch_hold.ps1 -StartX 1700 -StartY 900 -HoldX 1000 -HoldY 900
#>
[CmdletBinding()]
param(
    [int]$StartX = 1700,
    [int]$StartY = 900,
    [int]$HoldX = 1000,
    [int]$HoldY = 900,
    [int]$Steps = 30,
    [int]$StepDelayMs = 10,
    [int]$HoldMs = 1200,
    [string]$ScreenshotPath = "$env:LOCALAPPDATA\Temp\opencode\hold.png",
    [switch]$Lift
)

$ErrorActionPreference = 'Stop'

$code = @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class TouchHold {
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
        return Send(x, y, DOWN | INRANGE | INCONTACT, "DOWN");
    }

    public static string MoveTo(int x, int y) {
        return Send(x, y, UPDATE | INRANGE | INCONTACT, "MOVE");
    }

    public static string End(int x, int y) {
        return Send(x, y, UP | INRANGE, "UP");
    }

    static string Send(int x, int y, uint flags, string what) {
        contacts[0].pointerInfo.pointerType = PT_TOUCH;
        contacts[0].pointerInfo.pointerId = 0;
        contacts[0].pointerInfo.frameId = 0;
        contacts[0].pointerInfo.pointerFlags = flags;
        contacts[0].pointerInfo.ptPixelLocation = new POINT { x = x, y = y };
        if (!InjectTouchInput(1, contacts))
            return what + " failed: " + Marshal.GetLastWin32Error();
        return "OK";
    }
}
'@

Add-Type -TypeDefinition $code -Language CSharp

$r = [TouchHold]::Begin($StartX, $StartY)
if ($r -ne 'OK') { throw "begin: $r" }

for ($i = 1; $i -le $Steps; $i++) {
    $x = $StartX + ($HoldX - $StartX) * $i / $Steps
    $y = $StartY + ($HoldY - $StartY) * $i / $Steps
    $r = [TouchHold]::MoveTo([int]$x, [int]$y)
    if ($r -ne 'OK') { throw "move $i`: $r" }
    Start-Sleep -Milliseconds $StepDelayMs
}

Start-Sleep -Milliseconds $HoldMs

# Capture inline: spawning screenshot.ps1 as a child process took long enough
# that the mod's idle-release timer fired before the pixels were read.
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
"SHOT path=$ScreenshotPath width=$($bounds.Width) height=$($bounds.Height)"

if ($Lift) {
    $r = [TouchHold]::End($HoldX, $HoldY)
    if ($r -ne 'OK') { throw "end: $r" }
    'LIFTED'
} else {
    'HELD (contact is still down; run this script again with -Lift or press elsewhere to release)'
}
