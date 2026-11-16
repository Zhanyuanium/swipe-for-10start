#requires -Version 7.0
<#
.SYNOPSIS
Inject a single-finger touch swipe using InjectTouchInput.

Coordinates are physical screen pixels. Use -DryRun to print the sequence
without injecting anything.

Example (left-side taskbar, 2880x1800):
  pwsh -File tools/inject_swipe.ps1 -StartX 1 -StartY 900 -EndX 900 -EndY 900
#>
[CmdletBinding()]
param(
    [int]$StartX = 1,
    [int]$StartY = 900,
    [int]$EndX = 900,
    [int]$EndY = 900,
    [int]$Steps = 60,
    [int]$DelayMs = 10,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$code = @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class TouchSwipe {
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

    [DllImport("user32.dll")]
    public static extern bool InitializeTouchInjection(uint maxCount, uint mode);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool InjectTouchInput(uint count, POINTER_TOUCH_INFO[] contacts);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr context);

    static void Set(POINTER_TOUCH_INFO[] contacts, int x, int y, uint flags) {
        // frameId must stay 0 and raw/himetric coordinates must stay zero for
        // the injection sequence to be accepted. Do not "improve" this.
        contacts[0].pointerInfo.pointerType = PT_TOUCH;
        contacts[0].pointerInfo.pointerId = 0;
        contacts[0].pointerInfo.frameId = 0;
        contacts[0].pointerInfo.pointerFlags = flags;
        contacts[0].pointerInfo.ptPixelLocation = new POINT { x = x, y = y };
    }

    static bool Inject(POINTER_TOUCH_INFO[] contacts, out int error) {
        bool ok = InjectTouchInput(1, contacts);
        error = ok ? 0 : Marshal.GetLastWin32Error();
        return ok;
    }

    public static string Swipe(int x0, int y0, int x1, int y1, int steps, int delayMs) {
        SetProcessDpiAwarenessContext(new IntPtr(-4));

        if (!InitializeTouchInjection(256, 3)) {
            return "InitializeTouchInjection failed: " + Marshal.GetLastWin32Error();
        }

        var contacts = new POINTER_TOUCH_INFO[1];
        int error;

        Set(contacts, x0, y0, DOWN | INRANGE | INCONTACT);
        if (!Inject(contacts, out error)) {
            return "DOWN failed: " + error;
        }

        Thread.Sleep(delayMs);

        for (int i = 1; i <= steps; i++) {
            int x = x0 + (x1 - x0) * i / steps;
            int y = y0 + (y1 - y0) * i / steps;
            Set(contacts, x, y, UPDATE | INRANGE | INCONTACT);
            if (!Inject(contacts, out error)) {
                return "MOVE " + i + " failed: " + error;
            }
            Thread.Sleep(delayMs);
        }

        Set(contacts, x1, y1, UP | INRANGE);
        if (!Inject(contacts, out error)) {
            return "UP failed: " + error;
        }

        return "OK";
    }
}
'@

Add-Type -TypeDefinition $code -Language CSharp

if ($DryRun) {
    Write-Host ("DRY RUN: ({0},{1}) -> ({2},{3}), steps={4}, delay={5}ms" -f `
        $StartX, $StartY, $EndX, $EndY, $Steps, $DelayMs)
    exit 0
}

$result = [TouchSwipe]::Swipe($StartX, $StartY, $EndX, $EndY, $Steps, $DelayMs)
Write-Output $result
if ($result -ne 'OK') { exit 1 }
