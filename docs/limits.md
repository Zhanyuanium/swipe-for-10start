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
  latches it (one slot) and, 50 ms after the hidden event, serves it as the same
  open intent (`own press served wait=...`), so the key is never lost. The
  deferral is measured: a show at the hidden event's own tick was swallowed
  (state stuck at 3) while a show 16 ms later opened. The shell still owns the
  245–268 ms cloak after the serve, and a press that lands after the hide is a
  normal open.

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

## Hardware still owed

- One finger end to end on the real ELAN2513 through the final build (the
  injector covers the logic; the hardware's report cadence is the remaining
  difference).
- Two fingers in both lift orders; a pen by hand.
- The taskbar auto-hide reveal after a fresh logon (the stuck state above was
  not reproduced after an explorer restart; the bar stayed hidden).

## Not attempted

- Writing the launcher's `InternalState` or `Progress` from the bridge. The
  bridge only calls `ShowStartView`/`Dismiss`/`IsVisible`.
- Calling the launcher from an arbitrary hook frame. Every call is posted to the
  hidden window on the launcher thread.
- Holding the shell's `Hide`/`Dismiss` with a retract wait. The M3 close
  schedules the dismiss after the settle; there is no arbitration hold.
