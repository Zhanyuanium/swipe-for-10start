# Limits and risks

> M3. Each item is a measured limit, a shell-imposed rule or a hardware check
> still owed. Nothing here is speculative.

## Shell-imposed

- **The first visible frame is 245–268 ms after `ShowStartView` in the worst
  case.** The shell owns the cloak. The current auto-hidden Win open showed its
  first panel pixel at **+82–187 ms** from the key (median +115 over three
  runs); the docked keyboard open measured +190 ms. DWM attribute 13 returns
  `E_ACCESSDENIED` without the shell's IAM ticket; the owning process cannot
  acquire it before the shell's own uncloak (measured: the window was already
  uncloaked when the ticket was obtained, `before=0`). Early uncloak is not
  attempted: it may reveal an unpainted window. This is the accepted cap.
- **The band open is claimed only after 24 DIP of inward travel, and the shell's
  own edge gesture opens from about 20 DIP.** The threshold is what keeps a tap
  anywhere in the band (twice the taskbar's thickness) from requesting a show.
  The trade is explicit: a drag between roughly 20 and 24 DIP is served by the
  shell's gesture, not by this mod, and a drag that stops short of both never
  opens. Measured on the docked layout: a 23 DIP drag requested nothing from this
  mod and the native gesture opened the menu; a 25 DIP drag requested one show;
  at 24 DIP the request is published in the crossing millisecond and reaches
  `ShowStartView` +15 ms later, and the window uncloaks +125 ms after that call.
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
- **A reinstalled Windhawk image leaves its window class behind.** The class is
  named after the image's WndProc, so a new activation does not inherit the old
  image's procedure and its closed channel.
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

## The touch show

- **The digitizer may go quiet during the uncloak and during a close.** The
  18 ms idle release would end the candidate before the open adopts it; while a
  touch show is in flight (`controllerTouch`/`g_intentOpenActive`), or a close
  is in flight with a reveal candidate, the idle is skipped. A real lift is a
  report and still ends the gesture with `src=report`; a genuine silence after
  those windows ends it with `src=idle`.
- **The cold entry is 0.000.** The first open after a host start has no settled
  geometry (the measurement is deferred while the panel is owned), so the settle
  starts at 0 and the takeover chases. A warm auto-hidden open uses 0.255 (the
  docked layout used 0.348).
- **The chase is rate-limited at 2.2 DIP/ms.** A 0.558 inherited gap takes
  ~254 ms to close; the panel is glued only after it (`behind=0.000`). The first
  visible frame is the entry, not the finger's mapped position; this is the
  accepted cap, not a lost frame. A held finger settles to the mapped position:
  current auto-hidden `progress=0.96` at 300 DIP (the edge 1.8 px from the
  finger) and `progress=0.90` at 350 DIP (0.4 px); the docked 300 DIP hold
  measured 2 px.
- **The virtual digitizer's injected reports are sparser than the real panel.**
  A glide's first reports can be 30–46 ms apart, which the 18 ms idle reads as a
  new contact. The shipping panel reports at ~170 Hz; the test injector is a
  test instrument, not the hardware.

## The taskbar

- **Current layout: auto-hidden** (auto-hide on): work `0,0,1440,900`, the bar
  off screen, band `804..900`, panel `236,265..1229,840`, trail 265, Distance
  852. The Start button is off screen until the bar is revealed; the
  Start-button test hovers the bottom edge first. The docked layout (work
  `0,0,1440,852`, tray `0,852..1440,900`, Distance 900) is historical and its
  work area is never the current one.
- The auto-hidden bar once stopped revealing after an injected-touch session
  (`Shell_TrayWnd` stayed at `0,899,1440,947`, `ABM_GETSTATE=1`, Win+`T` did not
  draw it); that state is historical. The Win/touch/click-away paths do not
  depend on the bar either way.
- The bar's 48 DIP height and the 200 % scaling are measured, not assumed.

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
