# Limits and risks

> M3. Each item is a measured limit, a shell-imposed rule or a hardware check
> still owed. Nothing here is speculative.

## Shell-imposed

- **The first visible frame is 245–268 ms after `ShowStartView` in the worst
  case.** The shell owns the cloak. The bottom auto-hidden Win open showed its
  first panel pixel at **+82–187 ms** from the key (median +115 over three
  runs); the bottom docked keyboard open measured +190 ms. DWM attribute 13
  returns `E_ACCESSDENIED` without the shell's IAM ticket; the owning process
  cannot acquire it before the shell's own uncloak (measured: the window was
  already uncloaked when the ticket was obtained, `before=0`). Early uncloak is
  not attempted: it may reveal an unpainted window. This is the accepted cap.
- **The band open is claimed only after 24 DIP of inward travel, and the shell's
  own edge gesture opens from about 20 DIP.** The threshold is what keeps a tap
  anywhere in the band (twice the taskbar's thickness) from requesting a show.
  The trade is explicit: a drag between roughly 20 and 24 DIP is served by the
  shell's gesture, not by this mod, and a drag that stops short of both never
  opens. Measured on the bottom docked layout: a 23 DIP drag requested nothing
  from this mod and the native gesture opened the menu; a 25 DIP drag requested
  one show; at 24 DIP the request is published in the crossing millisecond and
  reaches `ShowStartView` +15 ms later, and the window uncloaks +125 ms after
  that call.
  A tap on the taskbar background, the clock or the tray requests nothing.
- **A stalled symbol fetch leaves the mod passive for that session.**
  `HookSymbols` runs on the executor worker, never on an Explorer thread, so a
  cold `twinui.pcshell.pdb` fetch cannot freeze the shell. The call itself
  cannot be cancelled or given a timeout: if the transfer stalls, the worker
  stays inside it, the 240 s deadline cannot fire, and every press runs the
  shell's own path until Explorer restarts. The same rule applies to
  `StartUI_.dll` on the Start side. Windhawk caches a symbol file on disk under
  `Symbols\<pdb name>\<GUID+age>\<pdb name>` (the PDB's own GUID and age, so a
  Windows update is a new directory, never a stale file under the old one), and
  it caches a failed symbol load for
  four hours for a mod loaded at startup (one minute for a mod loaded later),
  so a boot without the symbol server stays passive after the network returns;
  enabling the mod's debug logging disables that throttle for the next attempt.
  The same call also delays the INFINITE join at unload: disabling the mod while
  a cold fetch is in flight blocks `Wh_ModUninit` until the call returns. With a
  warm cache the join is immediate.
- **The private launcher layout is enabled only for the verified twinui identity.**
  `launcher+0x160`, `state+0x28` and the state vtable `+0x70` were read from one
  image. The mod requires the fixed file version `10.0.26100.9974` and all 13
  named symbols at the reference RVAs before it enables the decision hooks or
  lets the executor read the launcher. Any other image — even one that keeps all
  13 names — runs the shell's own path; the one line
  `exec launcher layout unverified ... native=1 reason=version|rva` states why.
  This is deliberately version-locked: a monthly update needs a new reference
  check, not a sentinel probe.
- **A teardown that cannot be proved gone blocks the unload.** The image is not
  pinned, so returning while a mod-owned thread, delegate or owner window is
  still reachable would hand it unmapped code. The unload joins threads
  `INFINITE`, waits for the UI teardown turn and the counted entries, and waits
  for `WM_NCDESTROY`; a permanently hung launcher or UI thread therefore blocks
  the disable instead of corrupting the process. There is no bounded-wait
  success path and no residual return.
- **The launcher must be attached.** `[outer+0x18+0x160] > 0x10000`; zero means
  no `IXamlLauncherState`. The constructor's sentinel is `0x100`. A first open
  after a fresh logon can therefore be refused; the shell's own gesture then
  opens the menu and creates the executor window for the next one.
- **The launcher thread is not the taskbar thread.** A `ShowStartView` on the
  taskbar thread fail-fasts explorer (`0xC0000409` at `twinui+0x1BB010`);
  measured. The executor creates its window on the thread that runs
  `OnHotkeyInvoke` / the shell's own `ShowStartView`, and refuses a window on any
  other.
- **A reinstalled Windhawk image does not inherit its window classes.** Both
  fixed classes (`WindhawkLegacyStartSwipeRawInputWindow`,
  `WindhawkSwipeFor10Start.Executor`) are registered under `&__ImageBase` and
  unregistered under it. `ERROR_CLASS_ALREADY_EXISTS` is a failure, never
  adopted: a surviving class from an earlier image points at an unmapped
  procedure, so the Start input thread refuses to run and the executor refuses
  to create its window. A failed unregister is retried instead of returning
  success into an image the class still points at.
- **A press that lands inside the shell's hide is a no-op show.** The bridge
  latches it (one slot, tick, the epoch the admission was decided under) and, 50 ms after the hidden event,
  serves it as the same open intent (`own press served wait=...`). The
  deferral is measured: a show at the hidden event's own tick was swallowed
  (state stuck at 3) while a show 16 ms later opened. The shell still owns the
  245–268 ms cloak after the serve, and a press that lands after the hide is a
  normal open. The latch is bounded, not a guarantee: a newer press supersedes it,
  a refused or expired dismiss invalidates it, a serve whose peer is gone or
  replaced drops it, and it expires after 1200 ms. A press lost to the 600 ms
  liveness budget is served by the shell — but only if the bridge never swallowed
  it. A press already swallowed and then lost (peer or epoch changed before the
  serve) has no shell path left; it is dropped, not passed on.

## The idle cost

The mod is loaded all day on the tablets and touch laptops it targets, so what
it does with nothing happening is a product number, not an implementation
detail. Measured on this build with the menu closed and nothing touching the
screen, for 60 s, with a counter build of each version and the same session
afterwards for the CPU figure:

| | input-thread wakes | `DwmGetWindowAttribute` | Start host CPU | Explorer CPU |
|---|---|---|---|---|
| before (5 ms period + 20 ms poll) | 190.6 /s | 32 /s | 0.625 % of one core | 1.615 % |
| after (one-shot deadlines + cloak event) | 9.2 /s | 0 | 0.026 % | 1.094 % |

The remaining 9.2 wakes/s are the peer heartbeat: `peerTick` must stay younger
than the bridge's 600 ms staleness window, and it is refreshed only while this
activation advertises a service. With the peer withheld or gone the wait is
`INFINITE` and the thread does not wake at all. Explorer's figure includes its
own idle work and the other installed mods; the Explorer half's own share is
the symbol-probe worker, which now blocks on its events instead of waking every
100 ms once the launcher symbols resolved (or were terminally refused).

The 20 ms poll was 32 queries a second, not 50: `kOpenCheckIntervalMs` is
compared against the 15.6 ms tick clock, so the throttle quantised to two
quanta. The new figure is 0 because the cloaked attribute is read only when a
cloak event arrives.

**A missed cloak event costs latency, not correctness.** Every transition the
mod asks for (a show or a dismiss) or has just observed opens a bounded watch
in which the slow fallback polls at 100 ms, so a dropped or delayed
`EVENT_OBJECT_CLOAKED`/`UNCLOAKED` still lands the open or the close; the
measured drill (a build with the hook deliberately not installed) landed both
states with `win state fallback open=1` and `win state fallback open=0`. A clean
run never writes that line. A band pull that the shell opens natively, without
a show request from this mod, opens the same watch when the contact is admitted,
so it is covered too.

**A late hide costs latency, not the pull.** The 600 ms suppression window
(`kRestoreSuppressMs`) governs admission and the unhealed rule, not the
landing's identity: the landing is this mod's close when its dismiss command is
still queued/armed or executed-unconfirmed, or when the open-cancel ticket names
that command. Measured on this machine, close takeover → `win close observed`
was **579–594 ms** for a maximum-duration (520 ms settle) close, so a busy
launcher can pass 600 ms; a pull held through such a close is still released and
reopened (`reveal reopen after close pull=1 late=0`). A landing with no command
of ours is an external close (Win+Tab, lock, a shell hide this mod did not ask
for) and the pull is dropped by design.

## The touch show

- **The digitizer may go quiet during the uncloak and during a close.** The
  idle release would end the contact before the open adopts it; while a
  touch show is in flight (`controllerTouch`/`g_intentOpenActive`), or a close
  is in flight with a reveal candidate, the idle is skipped. A real lift is a
  report and still ends the gesture with `src=report`; a genuine silence after
  those windows ends it with `src=idle`. A held release arms no deadline at all:
  the hold ends on the cloak event or on the executor's answer.
- **The cold entry is 0.000.** The first open after a host start has no settled
  geometry (the measurement is deferred while the panel is owned), so the settle
  starts at 0 and the takeover chases. A warm bottom auto-hidden open uses 0.255
  (the bottom docked layout used 0.348).
- **The chase is rate-limited at 2.2 DIP/ms.** A 0.558 inherited gap takes
  ~254 ms to close; the panel is glued only after it (`behind=0.000`). The first
  visible frame is the entry, not the finger's mapped position; this is the
  accepted cap, not a lost frame. A held finger settles to the mapped position:
  bottom auto-hidden `progress=0.96` at 300 DIP (the edge 1.8 px from the
  finger) and `progress=0.90` at 350 DIP (0.4 px); the bottom docked 300 DIP hold
  measured 2 px.
- **The virtual digitizer's injected reports are sparser than the real panel.**
  A glide's first reports can be 30–46 ms apart. That is no longer read as a new
  contact: a report gap does not end a positively tracked contact, and a band
  candidate has its own 120 ms watchdog (`kCandidateIdleHoldMs`), so a sparse
  injected flick still reaches the 24 DIP claim. The 18 ms idle still ends a
  dismiss session, whose panel reports at ~170 Hz; the test injector is a test
  instrument, not the hardware.

## The taskbar

- **Current layout: left, docked** (`axis=0`): work `48,0,1440,900`, the bar at
  `0,0..48,900` (96 physical wide), band `0..96`, panel `60,151..1053,726`, trail
  1053, Distance 1392; measured 2026-11-21. The bottom docked layout (work
  `0,0,1440,852`, tray `0,852..1440,900`, Distance 852) and the bottom auto-hidden
  layouts are historical and their numbers are not the current ones. The smoke
  reads the axis, band, rectangle and scale from the mod's own log lines, so it
  runs on whichever edge is live.
- The bottom auto-hidden bar once stopped revealing after an injected-touch
  session (`Shell_TrayWnd` stayed at `0,899,1440,947`, `ABM_GETSTATE=1`, Win+`T`
  did not draw it); that state is historical. The Win/touch/click-away paths do
  not depend on the bar either way.
- The bar's 48 DIP thickness (96 physical at 200 %) and the 200 % scaling are
  measured, not assumed.

## Hardware

- **Passed on the real ELAN2513 through the shipping build:** one finger end to
  end (edge pull, follow, panel drag close, click-away), two fingers in both lift
  orders, and the taskbar auto-hide reveal after a fresh logon (the stuck reveal
  above did not reproduce after an explorer restart). The injector's sparser
  report cadence is therefore no longer the difference between the test and the
  hardware.
- **Still owed:** a pen by hand. The one-finger pass also ran the HID dismiss
  producer (`TakeCloseOwnership` → `RequestBridgeDismiss`), because a real
  finger's panel drag is its only producer, but no `close dismiss kind=…` excerpt
  was captured. Take one the next time the mod is touched: that line is the only
  evidence separating the HID producer from the click-away one.

## Not attempted

- Writing the launcher's `InternalState` or `Progress` from the bridge. The
  bridge only calls `ShowStartView`/`Dismiss`/`IsVisible`.
- Calling the launcher from an arbitrary hook frame. Every call is posted to the
  hidden window on the launcher thread.
- Holding the shell's `Hide`/`Dismiss` with a retract wait. The M3 close
  schedules the dismiss after the settle; there is no arbitration hold.
