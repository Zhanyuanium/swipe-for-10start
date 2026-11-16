#requires -Version 7.0
<#
.SYNOPSIS
Inject a left-edge touch swipe that pauses at several x positions.

This is used to distinguish a real finger-following animation (content stays at
the paused position) from a time-based synthetic animation (content keeps
moving while the finger is held).

Example:
  pwsh -File tools/inject_swipe_holds.ps1 -HoldMs 600
#>
[CmdletBinding()]
param(
    [int]$StartX = 1,
    [int]$Y = 900,
    [int]$HoldMs = 600,
    [int]$StepsPerSegment = 20,
    [int]$StepDelayMs = 10,
    [int[]]$Waypoints = @(300, 600, 900)
)

$ErrorActionPreference = 'Stop'

$code = @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class TouchSwipeHolds {
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

    public static string Run(int startX, int y, int holdMs, int stepsPerSegment,
                             int stepDelayMs, int[] waypoints) {
        SetProcessDpiAwarenessContext(new IntPtr(-4));
        if (!InitializeTouchInjection(256, 3)) {
            return "InitializeTouchInjection failed: " + Marshal.GetLastWin32Error();
        }

        var contacts = new POINTER_TOUCH_INFO[1];
        int error;

        Set(contacts, startX, y, DOWN | INRANGE | INCONTACT);
        if (!Inject(contacts, out error)) {
            return "DOWN failed: " + error;
        }
        Thread.Sleep(stepDelayMs);

        int currentX = startX;
        foreach (int x in waypoints) {
            for (int i = 1; i <= stepsPerSegment; i++) {
                int nextX = currentX + (x - currentX) * i / stepsPerSegment;
                Set(contacts, nextX, y, UPDATE | INRANGE | INCONTACT);
                if (!Inject(contacts, out error)) {
                    return "MOVE failed: " + error;
                }
                Thread.Sleep(stepDelayMs);
            }
            currentX = x;

            // Press-and-hold needs repeated frames, otherwise the touch
            // contact is cancelled and the next frame fails with ERROR_TIMEOUT.
            int holdFrames = Math.Max(1, holdMs / 50);
            for (int h = 0; h < holdFrames; h++) {
                Set(contacts, currentX, y, UPDATE | INRANGE | INCONTACT);
                if (!Inject(contacts, out error)) {
                    return "HOLD failed: " + error;
                }
                Thread.Sleep(50);
            }
        }

        Set(contacts, currentX, y, UP | INRANGE);
        if (!Inject(contacts, out error)) {
            return "UP failed: " + error;
        }

        return "OK";
    }
}
'@

Add-Type -TypeDefinition $code -Language CSharp
$result = [TouchSwipeHolds]::Run($StartX, $Y, $HoldMs, $StepsPerSegment, $StepDelayMs, $Waypoints)
Write-Output $result
if ($result -ne 'OK') { exit 1 }
