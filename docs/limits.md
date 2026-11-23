# Limits and risks

> Each item is a measured limit, a shell-imposed rule or a hardware check still
> owed.

## Shell-imposed

- **The first visible frame is 245–268 ms after `ShowStartView` in the worst
  case.** The bottom auto-hidden Win open showed its first panel pixel at
  +82–187 ms from the key (median +115), the bottom docked open at +190 ms. Early
  uncloak is not attempted — DWM attribute 13 needs the shell's IAM ticket — so
  this is the accepted cap.
- **The band open needs 24 DIP of inward travel; the shell's own gesture opens
  from about 20 DIP.** A tap anywhere in the band (twice the taskbar's thickness)
  requests nothing. The trade: a 20–24 DIP drag is served by the shell, and a drag
  short of both never opens.
- **A stalled symbol fetch leaves the mod passive for that session.**
  `HookSymbols` cannot be cancelled or given a timeout; if the transfer stalls, the
  worker stays inside it and every press runs the shell's path until Explorer
  restarts. Windhawk caches the PDB under `Symbols\<pdb name>\<GUID+age>` and a
  failed load for four hours (one minute for a mod loaded later), so a boot
  without the symbol server stays passive after the network returns. The same
  call delays the unload's join.
- **The private launcher layout is version-locked.** Before any symbol work the
  mapped image's fixed version is read from `RT_VERSION`; a version other than
  `10.0.26100.10068` is refused on the spot (`skipped=version`, no PDB fetch), and
  only a matching version reaches the fetch, where all 13 names must resolve at
  the reference RVAs. A monthly update needs a new reference check, not a sentinel
  probe.
- **The launcher must be attached.** `[outer+0x18+0x160] > 0x10000`; zero means no
  `IXamlLauncherState`, and the constructor's sentinel is `0x100`. A first open
  after a fresh logon can be refused; the shell's own gesture then opens the menu
  and creates the executor window for the next press.
- **The launcher thread is not the taskbar thread.** A `ShowStartView` on the
  taskbar thread fail-fasts explorer (`0xC0000409` at `twinui+0x1BB010`). A window
  on any other thread is refused, and a `Dismiss`/`ShowStartView` from a
  COM/taskbar caller that does not pump passes natively.
- **A reinstalled image does not inherit its window classes.** Both fixed classes
  (`WindhawkLegacyStartSwipeRawInputWindow`, `WindhawkSwipeFor10Start.Executor`)
  are registered under `&__ImageBase`; `ERROR_CLASS_ALREADY_EXISTS` is a failure,
  because a surviving class points at unmapped code.
- **A press inside the shell's hide is deferred, not shown.** Once the shell hides
  (`InternalState` 9/11), or our own `Dismiss` is entered, the bridge latches the
  press and serves it 50 ms after the hidden event: a show at the event's own tick
  was swallowed (state stuck at 3) while one 16 ms later opened. The latch expires
  after 1200 ms, is invalidated by a refused/expired dismiss, and is dropped when
  the peer or epoch moved.

## Configuration and channel

- **The classic Start mode is a load-time gate.** Both halves load only while
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced\Start_ShowClassicMode`
  is 1 (the value ExplorerPatcher's own settings write); the
  `Software\ExplorerPatcher` value is the fallback when that key is absent. A
  style change needs both hosts restarted. Behaviour on the Windows 11 Start is
  unmeasured.
- **The channel is trusted by name, not authenticated.** Any process in the logon
  session can read and publish every record. `peerState`, `peerTick`, `peerEpoch`,
  `peerPid` and `peerUiThread` prove a Start half is alive and which activation it
  is, not that the writer is the Start host.

## Presses and claims

- **A claimed press is spent after 2000 ms.** A hard kill leaves `peerState`
  `alive`; the process handle proves death and releases at once. A frozen host
  cannot be proven dead, so its claim expires at `kClaimAbandonMs` (2000 ms,
  below the 2500 ms queue bound so a press queued behind it still runs) without
  replaying the press (`reason=claim-deadline`). If that host later resumes and
  applies the copy it validated, one input can produce two transitions; the
  alternative was a dead Win key until the host answered.
- **A taken press with no answer is lost, not duplicated.** If the controller dies
  after `intentTaken` and before the ack, the bridge has seen a claim and cannot
  replay. An abort that lands before the copy is the exception: Start's
  superseded answer proves no intent ran, and the bridge replays the original once
  (`reason=revoked-before-copy`), unless the press is past 2500 ms.
- **A queued press older than 2500 ms is dropped.** The queue-age bound pauses
  behind a claimed press, so the absolute cap is measured from `admittedTick`
  (`kPressQueueWaitMs + kPressAckTimeoutMs`).
- **A stale-period press with nothing held passes natively** and cannot be ordered
  against a later press. With a press outstanding or queued, the gate routes it to
  admission (`reason=peer-stale-queued`) so it cannot overtake the earlier one.
- **The click-away reopen needs a real left button.** A method-7 z-band change
  without one (Alt+Tab, a foreground switch, a taskbar activation) is a close. The
  button check excludes a click whose cursor is over the tray's top-level window
  (`Shell_TrayWnd`/`Shell_SecondaryTrayWnd`, clock and tray children included), so
  a taskbar with another root class would be read as a pointer click. The taskbar
  app-button case is **not measured**.
- **A finger or pen tap on the desktop is a pointer, not a mouse button.** A
  virtual-digitizer tap during a Win close closed instead of reopening, so the
  "desktop click is like the Win key" contract covers a **mouse** click; the
  finger/pen tap is owed.

## The idle cost

Measured with the menu closed and nothing touching the screen:

| | input-thread wakes | `DwmGetWindowAttribute` | Start CPU | Explorer CPU |
|---|---|---|---|---|
| before (5 ms period + 20 ms poll) | 190.6 /s | 32 /s | 0.625 % | 1.615 % |
| after (one-shot + cloak event) | 9.2 /s | 0 | 0.026 % | 1.094 % |

The remaining wakes are the `peerTick` heartbeat. A missed cloak event costs
latency, not correctness: every requested or observed transition opens a bounded
watch with a slow 100 ms fallback, and a clean run never writes it.

**A late hide costs latency, not the pull.** The close record owns the retracted
panel and its 800 ms bound is a repair, not the answer to "is a close in flight";
takeover → `win close observed` measured 562–610 ms over ten maximum-duration
closes (mean 578). The repair logs one `close ended ... reason=repair-deadline`,
releases the direction, and leaves the command and ticket naming the hide that may
still land.

## The touch show

- **The digitizer goes quiet during the uncloak and during a close.** The idle
  release is skipped while a touch show is in flight or a close owns the panel
  with a reveal candidate; a real lift is still a report. A held release arms no
  deadline at all.
- **Silence is not a lift for a contact a close consumed.** A contact with a
  Contact Identifier survives silence and ends only on a lift or a proven new
  contact; a node index and the coordinate-only path fall back to the 120 ms
  watchdog and the 18 ms latch once the close ended. The residual for the named
  path is a device that reuses an identifier after a lost lift: the next finger is
  read as a continuation until it lifts.
- **The cold entry is 0.000.** The first open after a host start has no settled
  geometry, so the settle starts at 0 and the takeover chases. A warm bottom
  auto-hidden open uses 0.255; the bottom docked layout used 0.348.
- **The chase is rate-limited at 2.2 DIP/ms.** A 0.558 inherited gap takes ~254 ms
  to close; the panel is glued only after it. The first visible frame is the
  entry, not the finger's mapped position.
- **The virtual digitizer is sparser than the real panel.** A glide's first
  reports can be 30–46 ms apart; that is not a new contact, and a band candidate's
  120 ms watchdog still lets a sparse injected flick reach 24 DIP. The injector is
  a test instrument, not the hardware.

## The taskbar and the display

- **Measured layouts, all on this machine:** left docked 2026-11-21 (`axis=0`,
  panel `60,151..1053,726`, trail 1053, Distance 1392); bottom docked 2026-11-20
  (`axis=3`, band `804..900`, panel `236,265..1229,840`, trail 265, Distance 852);
  bottom auto-hidden 2026-11-22 (`axis=3`, band `804..900`, panel
  `236,279..1229,840`, trail 279, Distance 900). The bar is not pinned, and the
  smoke reads the live numbers from the mod's own lines.
- **The auto-hidden bar did not reveal in one 2026-11-22 session** (`Shell_TrayWnd`
  stayed at `0,1798,2880,1894`; `Win+T` did not draw it), so the taskbar
  app-button case is unmeasured. It did not reproduce after an explorer restart.
- **One upright primary display.** Every measurement is one 2880x1800 panel at
  200 % (1440x900 DIP); the bar's 48 DIP thickness is 96 physical. A taskbar on a
  secondary monitor and any non-zero rotation have no run: the mod derives the
  axis, band, rect and scale from the live bar and panel, but nothing proves the
  digitizer's virtual-desktop coordinates, the secondary scale or the shell's
  reveal there. Treated as unsupported until measured.

## Hardware

- **Passed on the real ELAN2513 through the shipping build:** one finger end to
  end (edge pull, follow, panel drag close, click-away), two fingers in both lift
  orders, and the auto-hide reveal after a fresh logon.
- **Still owed:** a pen by hand; a captured `close dismiss kind=...` from a real
  finger (the only evidence separating the HID producer from the click-away); and
  a consumed finger that stays silent through a landing and then resumes, which
  the virtual digitizer cannot deliver (`InjectTouchInput failed: 1460`).
