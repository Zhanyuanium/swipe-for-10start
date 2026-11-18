#requires -Version 7.0
<#
.SYNOPSIS
The one touch-injection lifecycle the tools share. Dot-source this file; it has
no command line of its own.

.DESCRIPTION
Every tool that drives a finger through InjectTouchInput uses this file, so a
"hold" means the same thing everywhere:

    Start-Contact       DOWN at (x, y), DPI awareness and injection initialised
    Move-Contact        UPDATE at (x, y)
    Glide-Contact       UPDATE along a straight line, paced in C#
    Hold-Contact        UPDATE at the held point every KeepAliveMs, blocking
    Start-ContactHold   the same keep-alive on a background thread, so the caller
    Stop-ContactHold    can capture the screen while the contact stays down
    End-Contact         stops and joins the keep-alive, then UP

The keep-alive interval is [TouchCommon.Injector]::KeepAliveMs, 5 ms. The mod
ends a contact after kIdleReleaseMs = 18 ms without a raw report, so any interval
at or above 18 ms is not a hold: the gesture has already ended. 5 ms is also the
real digitizer's rate (~170 Hz), so the mod reads the injected stream the way it
reads a finger.

Injection is serialised. The contact flags, the last point and every
InjectTouchInput call sit under one lock, and End-Contact stops and joins the
keep-alive thread before it sends the UP. No UPDATE can reach the injector for
that contact after the UP, so the UP is the release, and nothing races it. If
the join times out, the UP is not sent: the thread may still inject an UPDATE,
and the contact state stays open. A failed UP injection also keeps the contact
open and leaves ReleaseMs at 0, so the caller sees that no release happened and
a retry can send it.

End-Contact records the UP timestamp's value immediately before that injection
([TouchCommon.Injector]::ReleaseMs, on the Stopwatch clock) and stores it when
the UP succeeds, so a trace can time the release without taking a stamp after a
capture.

Pacing uses CREATE_WAITABLE_TIMER_HIGH_RESOLUTION. The mod measured the reason:
this host puts a windowless pwsh into a timer class where timeBeginPeriod is
ignored and Sleep(8) takes 15-16 ms, which would sit on the release window.

The DPI setup lives here too: Start-Contact switches the process to
per-monitor-v2 before the first injection, so coordinates are physical pixels and
a later capture measures the physical screen. Set-TouchDpiAware is exported for
tools that capture before they inject.

.NOTES
[INPUT]: dot-sourced by the tools; P/Invokes user32, winmm and kernel32 only
[OUTPUT]: Start-Contact, Move-Contact, Glide-Contact, Hold-Contact,
          Start-ContactHold, Stop-ContactHold, End-Contact, Invoke-ContactSwipe
          and Set-TouchDpiAware, all backed by TouchCommon.Injector
[POS]: the one injection implementation in tools/. Every script keeps only its
       path, its timing policy and its capture logic; no tool calls
       InjectTouchInput directly any more.
[PROTOCOL]: Update this header on change, then check AGENTS.md
#>
[CmdletBinding()]
param()

if (-not ('TouchCommon.Injector' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

namespace TouchCommon
{
    public static class Injector
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct POINT { public int x; public int y; }

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT { public int left, top, right, bottom; }

        [StructLayout(LayoutKind.Sequential)]
        public struct POINTER_INFO
        {
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
        public struct POINTER_TOUCH_INFO
        {
            public POINTER_INFO pointerInfo;
            public uint touchFlags;
            public uint touchMask;
            public RECT rcContact;
            public RECT rcContactRaw;
            public uint orientation;
            public uint pressure;
        }

        // The keep-alive interval, in ms. Two bounds set it: the mod drops a
        // contact after kIdleReleaseMs = 18 ms of silence, so anything near that
        // is a release, and the real panel reports at ~170 Hz (5.9 ms), so a
        // faster stream adds nothing the mod can use. 5 ms is the mod's own idle
        // tick. This is the only interval the tools use; a tool that needs
        // another one must say why next to its use.
        public const int KeepAliveMs = 5;

        const uint PT_TOUCH = 2;
        const uint INRANGE = 0x00000002;
        const uint INCONTACT = 0x00000004;
        const uint DOWN = 0x00010000;
        const uint UPDATE = 0x00020000;
        const uint UP = 0x00040000;

        [DllImport("user32.dll")]
        static extern bool InitializeTouchInjection(uint maxCount, uint mode);

        [DllImport("user32.dll", SetLastError = true)]
        static extern bool InjectTouchInput(uint count, POINTER_TOUCH_INFO[] contacts);

        [DllImport("user32.dll")]
        static extern bool SetProcessDpiAwarenessContext(IntPtr context);

        [DllImport("winmm.dll")]
        static extern uint timeBeginPeriod(uint period);

        const uint CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x00000002;
        const uint TIMER_ALL_ACCESS = 0x001F0003;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateWaitableTimerEx(IntPtr attributes, string name,
            uint flags, uint access);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetWaitableTimer(IntPtr timer, ref long dueTime, int period,
            IntPtr completionRoutine, IntPtr argument, bool resume);

        [DllImport("kernel32.dll")]
        static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

        // One waitable timer per thread, created on first use. The main thread
        // and the keep-alive thread never share a timer: a SetWaitableTimer on a
        // handle another thread is waiting on would steal its wait. The handles
        // live until the process exits, which is what every tool does.
        [ThreadStatic] static IntPtr waitTimer;
        [ThreadStatic] static bool coarseTimer;

        // The lock: contact flags, last point and every injection. Taking it
        // around the UP is what makes the release the last injection.
        static readonly object gate = new object();
        static readonly POINTER_TOUCH_INFO[] contact = new POINTER_TOUCH_INFO[1];
        static bool ready;
        static bool contactOpen;
        static int lastX;
        static int lastY;
        static volatile bool keep;
        static Thread keepThread;
        static string keepError;
        static double releaseMs;

        public static int LastX { get { lock (gate) { return lastX; } } }
        public static int LastY { get { lock (gate) { return lastY; } } }

        // Stopwatch-clock time (ms) of the release. The value is taken
        // immediately before the UP injection and stored only when that
        // injection succeeds; 0 until a contact has ended.
        public static double ReleaseMs { get { return releaseMs; } }

        public static double NowMs()
        {
            return Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency;
        }

        public static void SetDpiAware()
        {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
            try { SetProcessDpiAwarenessContext(new IntPtr(-4)); } catch { }
        }

        static void Wait(int ms)
        {
            if (ms <= 0) return;
            if (coarseTimer) { Thread.Sleep(ms); return; }
            if (waitTimer == IntPtr.Zero)
            {
                waitTimer = CreateWaitableTimerEx(IntPtr.Zero, null,
                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
                if (waitTimer == IntPtr.Zero) { coarseTimer = true; Thread.Sleep(ms); return; }
            }
            long due = -(long)ms * 10000L;
            if (!SetWaitableTimer(waitTimer, ref due, 0, IntPtr.Zero, IntPtr.Zero, false))
            { coarseTimer = true; Thread.Sleep(ms); return; }
            WaitForSingleObject(waitTimer, (uint)(ms + 1000));
        }

        static string EnsureReady()  // caller holds gate
        {
            if (ready) return "OK";
            SetDpiAware();
            if (!InitializeTouchInjection(256, 3))
                return "InitializeTouchInjection failed: " + Marshal.GetLastWin32Error();
            // The mod makes the same request while a contact is down. The
            // process is short-lived, so the exit reverses it; a tool that
            // wants the period back can call timeEndPeriod itself.
            timeBeginPeriod(1);
            ready = true;
            return "OK";
        }

        static string Inject(int x, int y, uint flags)  // caller holds gate
        {
            // frameId must stay 0 and the raw/himetric points must stay zero or
            // the injection sequence is rejected. Do not "improve" this.
            contact[0].pointerInfo.pointerType = PT_TOUCH;
            contact[0].pointerInfo.pointerId = 0;
            contact[0].pointerInfo.frameId = 0;
            contact[0].pointerInfo.pointerFlags = flags;
            contact[0].pointerInfo.ptPixelLocation = new POINT { x = x, y = y };
            if (!InjectTouchInput(1, contact))
                return "InjectTouchInput failed: " + Marshal.GetLastWin32Error();
            lastX = x;
            lastY = y;
            return "OK";
        }

        public static string Begin(int x, int y)
        {
            lock (gate)
            {
                if (contactOpen) return "a contact is already open";
                string r = EnsureReady();
                if (r != "OK") return r;
                r = Inject(x, y, DOWN | INRANGE | INCONTACT);
                if (r == "OK") { contactOpen = true; releaseMs = 0.0; }
                return r;
            }
        }

        public static string Move(int x, int y)
        {
            lock (gate)
            {
                if (!contactOpen) return "no contact is open";
                return Inject(x, y, UPDATE | INRANGE | INCONTACT);
            }
        }

        public static string Glide(int x0, int y0, int x1, int y1, int steps, int delayMs)
        {
            if (steps < 1) return "steps must be >= 1";
            for (int i = 1; i <= steps; i++)
            {
                int x = x0 + (x1 - x0) * i / steps;
                int y = y0 + (y1 - y0) * i / steps;
                string r = Move(x, y);
                if (r != "OK") return "glide " + i + ": " + r;
                Wait(delayMs);
            }
            return "OK";
        }

        public static string Hold(int ms)
        {
            int x, y;
            lock (gate) { if (!contactOpen) return "no contact is open"; x = lastX; y = lastY; }
            return Hold(x, y, ms);
        }

        public static string Hold(int x, int y, int ms)
        {
            string r = Move(x, y);
            if (r != "OK") return r;
            double end = NowMs() + Math.Max(0, ms);
            while (true)
            {
                double left = end - NowMs();
                if (left <= 0) return "OK";
                Wait((int)Math.Min(KeepAliveMs, Math.Ceiling(left)));
                r = Move(x, y);
                if (r != "OK") return r;
            }
        }

        public static string StartHold(int x, int y)
        {
            lock (gate)
            {
                if (!contactOpen) return "no contact is open";
                if (keepThread != null) return "keep-alive already running";
                string r = Inject(x, y, UPDATE | INRANGE | INCONTACT);
                if (r != "OK") return r;
                keepError = null;
                keep = true;
                keepThread = new Thread(delegate()
                {
                    while (keep)
                    {
                        Wait(KeepAliveMs);
                        if (!keep) break;
                        string e;
                        lock (gate)
                        {
                            if (!contactOpen) break;
                            e = Inject(lastX, lastY, UPDATE | INRANGE | INCONTACT);
                        }
                        if (e != "OK") { keepError = e; break; }
                    }
                });
                keepThread.IsBackground = true;
                keepThread.Start();
                return "OK";
            }
        }

        // Stops and joins the keep-alive. `error` is the keep-alive's own
        // injection error, if any. False means the thread is still alive after
        // the join timeout: it may still inject an UPDATE, so the caller must
        // not send the UP and must leave the contact open. The thread reference
        // is kept on a timeout, so a later StopHold retries the same join
        // instead of pretending the thread is gone.
        static bool StopHoldThread(out string error)
        {
            Thread t;
            lock (gate) { keep = false; t = keepThread; }
            if (t != null)
            {
                // Join outside the lock: the thread needs it to finish an
                // injection.
                if (!t.Join(2000))
                {
                    error = "keep-alive thread did not stop within 2000 ms";
                    return false;
                }
                lock (gate) { if (keepThread == t) keepThread = null; }
            }
            string e = keepError;
            keepError = null;
            error = string.IsNullOrEmpty(e) ? null : "keep-alive: " + e;
            return true;
        }

        public static string StopHold()
        {
            string error;
            StopHoldThread(out error);
            return error ?? "OK";
        }

        // The release, under the lock, after the keep-alive thread is joined.
        // The UP is the last injection of the contact. A failed UP is reported
        // as a failure and leaves the contact open, and ReleaseMs stays 0: the
        // caller can see that no release happened, and a retry can send the UP.
        static string EndLocked(int x, int y)  // caller holds gate
        {
            if (!contactOpen) return "no contact is open";
            double stamp = NowMs();  // the value is taken immediately before the UP
            string r = Inject(x, y, UP | INRANGE);
            if (r != "OK")
                return "the UP failed (" + r + "); the contact is still down";
            releaseMs = stamp;
            contactOpen = false;
            return "OK";
        }

        static string EndAfterHold(string hold, int x, int y)
        {
            string r;
            lock (gate) { r = EndLocked(x, y); }
            return hold == null ? r : hold + " (then " + r + ")";
        }

        public static string End(int x, int y)
        {
            string hold;
            if (!StopHoldThread(out hold))
                return hold + "; the UP was not sent, the contact is still down";
            return EndAfterHold(hold, x, y);
        }

        public static string End()
        {
            string hold;
            if (!StopHoldThread(out hold))
                return hold + "; the UP was not sent, the contact is still down";
            string r;
            lock (gate) { r = EndLocked(lastX, lastY); }
            return hold == null ? r : hold + " (then " + r + ")";
        }

        // One contact, one path, one release: DOWN, one UPDATE per step, UP.
        public static string Swipe(int x0, int y0, int x1, int y1, int steps, int delayMs)
        {
            string r = Begin(x0, y0);
            if (r != "OK") return "DOWN " + r;
            Wait(delayMs);
            r = Glide(x0, y0, x1, y1, steps, delayMs);
            if (r != "OK")
            {
                string end = End();
                return end == "OK" ? r : r + "; UP " + end;
            }
            return End(x1, y1);
        }
    }
}
'@
}

function Set-TouchDpiAware {
    [CmdletBinding()]
    param()
    [TouchCommon.Injector]::SetDpiAware()
}

function Start-Contact {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][int]$X,
        [Parameter(Mandatory)][int]$Y
    )
    $r = [TouchCommon.Injector]::Begin($X, $Y)
    if ($r -ne 'OK') { throw "Start-Contact($X,$Y): $r" }
}

function Move-Contact {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][int]$X,
        [Parameter(Mandatory)][int]$Y
    )
    $r = [TouchCommon.Injector]::Move($X, $Y)
    if ($r -ne 'OK') { throw "Move-Contact($X,$Y): $r" }
}

function Glide-Contact {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][int]$X,
        [Parameter(Mandatory)][int]$Y,
        [int]$Steps = 30,
        [int]$DelayMs = 10
    )
    $r = [TouchCommon.Injector]::Glide(
        [TouchCommon.Injector]::LastX, [TouchCommon.Injector]::LastY,
        $X, $Y, $Steps, $DelayMs)
    if ($r -ne 'OK') { throw "Glide-Contact($X,$Y): $r" }
}

# Holds the contact at the last point (or at X,Y) for Ms, sending one UPDATE per
# KeepAliveMs so the mod never sees the silence that ends a contact.
function Hold-Contact {
    [CmdletBinding(DefaultParameterSetName = 'LastPoint')]
    param(
        [Parameter(Mandatory)][int]$Ms,
        [Parameter(ParameterSetName = 'AtPoint', Mandatory)][int]$X,
        [Parameter(ParameterSetName = 'AtPoint', Mandatory)][int]$Y
    )
    $r = if ($PSCmdlet.ParameterSetName -eq 'AtPoint') {
        [TouchCommon.Injector]::Hold($X, $Y, $Ms)
    } else {
        [TouchCommon.Injector]::Hold($Ms)
    }
    if ($r -ne 'OK') { throw "Hold-Contact($Ms ms): $r" }
}

# The same keep-alive on a background thread: the caller can capture while the
# contact stays down, then calls Stop-ContactHold.
function Start-ContactHold {
    [CmdletBinding(DefaultParameterSetName = 'LastPoint')]
    param(
        [Parameter(ParameterSetName = 'AtPoint', Mandatory)][int]$X,
        [Parameter(ParameterSetName = 'AtPoint', Mandatory)][int]$Y
    )
    $r = if ($PSCmdlet.ParameterSetName -eq 'AtPoint') {
        [TouchCommon.Injector]::StartHold($X, $Y)
    } else {
        [TouchCommon.Injector]::StartHold(
            [TouchCommon.Injector]::LastX, [TouchCommon.Injector]::LastY)
    }
    if ($r -ne 'OK') { throw "Start-ContactHold: $r" }
}

# Stops the keep-alive and joins its thread. Throws when the thread did not stop
# within 2 s: the contact is still open, and End-Contact will refuse to send the
# UP for the same reason until a later call joins it.
function Stop-ContactHold {
    [CmdletBinding()]
    param()
    $r = [TouchCommon.Injector]::StopHold()
    if ($r -ne 'OK') { throw "Stop-ContactHold: $r" }
}

# The release: stops and joins the keep-alive, then sends the UP, either at X,Y
# or at the last injected point. End-Contact owns all three steps, so no UPDATE
# can follow the UP. Two failures keep the contact open instead of pretending it
# ended, and both throw what the caller sees:
#   - the join timed out: no UP was sent ("...; the UP was not sent, the contact
#     is still down"), because the keep-alive thread may still inject;
#   - the UP injection failed: "the UP failed (...); the contact is still down",
#     and ReleaseMs stays 0, so a retry can send the UP again.
function End-Contact {
    [CmdletBinding(DefaultParameterSetName = 'LastPoint')]
    param(
        [Parameter(ParameterSetName = 'AtPoint', Mandatory)][int]$X,
        [Parameter(ParameterSetName = 'AtPoint', Mandatory)][int]$Y
    )
    $r = if ($PSCmdlet.ParameterSetName -eq 'AtPoint') {
        [TouchCommon.Injector]::End($X, $Y)
    } else {
        [TouchCommon.Injector]::End()
    }
    if ($r -ne 'OK') { throw "End-Contact: $r" }
}

# DOWN, glide, UP in one call, for tools whose path is one straight swipe.
function Invoke-ContactSwipe {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][int]$StartX,
        [Parameter(Mandatory)][int]$StartY,
        [Parameter(Mandatory)][int]$EndX,
        [Parameter(Mandatory)][int]$EndY,
        [int]$Steps = 30,
        [int]$DelayMs = 10
    )
    $r = [TouchCommon.Injector]::Swipe($StartX, $StartY, $EndX, $EndY, $Steps, $DelayMs)
    if ($r -ne 'OK') { throw "Invoke-ContactSwipe: $r" }
}
