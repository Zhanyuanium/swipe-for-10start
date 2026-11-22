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
- **A press that lands inside the shell's hide is a no-op show.** Once the shell
  is hiding (`InternalState` 9/11), or our own executor has entered its `Dismiss`
  call, the bridge latches the press (one slot, tick, the epoch the admission was
  decided under) and, 50 ms after the hidden event,
  serves it as the same open intent (`own press served wait=...`). The
  deferral is measured: a show at the hidden event's own tick was swallowed
  (state stuck at 3) while a show 16 ms later opened. An armed-but-not-yet-run
  dismiss is not latched: it is revocable, so the press publishes the open and the
  Start side cancels the command. A press that lands after the dismiss returned
  but before the hide is observed publishes the open too; if the hide then lands,
  the Start side's recovery ticket reissues that open once. The shell still owns
  the 245–268 ms cloak after the serve, and a press that lands after the hide is
  a normal open. The latch is bounded, not a guarantee: a newer press supersedes
  it, a refused or expired dismiss invalidates it, a serve whose peer is gone or
  replaced drops it, and it expires after 1200 ms. A press lost to the 600 ms
  liveness budget is served by the shell — but only if the bridge never swallowed
  it. A press already swallowed and then lost (peer or epoch changed before the
  serve) has no shell path left; it is dropped, not passed on.

## Presses and claims

- **A hard kill while a press is claimed holds the slot.** `peerState` stays
  `alive` without the unload's withdrawal, so the bridge cannot prove the
  controller gone. The claimed slot and its queue are held until the next attach
  changes `peerEpoch` (`reason=epoch-changed`). Presses queued behind it are
  dropped at the 2500 ms absolute bound (`own press dropped ... reason=stale`)
  rather than toggling minutes later; the 2026-11-22 test froze the host after
  its claim, queued press 2, and logged the drop at `wait=2594`.
- **A taken press with no answer is lost, not duplicated.** If the controller
  dies after `intentTaken` and before the ack, the bridge has seen a claim and
  cannot replay: the press is lost. This is the exactly-once trade. An abort that
  lands before the copy is the exception: Start's superseded answer (`state=7`)
  proves no intent ran, and the bridge replays the original once
  (`reason=revoked-before-copy`), verified on the same build.
- **A queued press older than 2500 ms is dropped.** The queue-age bound pauses
  behind a claimed press, so the absolute cap is measured from `admittedTick`:
  `kPressQueueWaitMs + kPressAckTimeoutMs`. A press the user made during a freeze
  is not run natively when the slot is finally released; only the claimed press's
  own action remains and the queued toggles are gone.
- **A stale-period press with nothing held passes natively.** When the gate fails
  on a stale heartbeat and no press is outstanding or queued, the press runs the
  shell's own path; the mod cannot order it against a later press. With a press
  outstanding or queued, the gate logs `reason=peer-stale-queued` and routes the
  press to admission so it cannot overtake the earlier one. This is the chosen
  trade, not a missed call.
- **The click-away reopen needs a real left button.** A method-7 z-band change
  without a left button down (Alt+Tab, a foreground switch, a taskbar activation)
  is a close, not the reopen that interrupts a close; main (`8b55ddf`) publishes
  the same close. A desktop click with the button down still reopens (5/5 in the
  Round-11 probe). The taskbar app-button case is **not measured**: the
  auto-hidden bar did not reveal a pointer target in the 2026-11-22 session and
  its focused buttons do not expose `InvokePattern`. `ClickAwayPointerNow`
  excludes a click whose cursor window is `Shell_TrayWnd`/`MSTaskListWClass`; a
  taskbar implementation with another class would be read as a pointer click and
  reopen, which is the residual of that rule.
- **Two bounded wakes are state-owned.** The open-generation gate reads its
  generation every 20 ms for at most 900 ms, and a standing press slot or
  external barrier keeps a 100 ms reserve poll for its queue bound. Each stops
  when its state clears; a poll that outlives its slot is killed in
  `HandlePressAckTimeout`. Neither is an idle period.

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

**A late hide costs latency, not the pull.** The close record, not a clock, owns
the retracted panel: one accepted close (serial, acceptance tick, the menu it was
accepted against, a phase and a repair bound) is retired by its landing, a pull
cancel, an external takeover, a terminal repair, or the repair bound itself
(`kRestoreSuppressMs`, 600 ms after acceptance). The landing's identity is still
its dismiss command and its open generation: the landing is this mod's close when
its dismiss command is still queued/armed or executed-unconfirmed, or when the
open-cancel ticket names that command. Measured on this machine, close takeover →
`win close observed` was **562–593 ms over ten maximum-duration (520 ms settle)
closes**, mean 578, so the worst sample sat 7 ms inside the 600 ms bound — a busy
launcher can pass it, which is why the bound is a repair and never the answer to
"is a close in flight". The repair ending is deliberate: it logs one `close ended
... reason=repair-deadline` line, releases the published direction so the bridge's
next toggle reads the cloak and the internal state again, and leaves the dismiss
command and the ticket naming the hide that may still land — the unhealed heal
waits for that command to answer, so a slow hide does not land on a panel that
was just restored under it.
A landing
with no command
of ours is an external close (Win+Tab, lock, a shell hide this mod did not ask
for) and the contact is dropped by design.

**The last part of a retract is a pull, not a press.** A pull held through the
close can still take it over while the command is cancellable: the transfer
cancels it and hands the panel to the finger (`grab=taken`). Once the retract is
at or below the entry there is no panel on screen to take: a down on the surface
the retract had shown, or in the area it has already left, is refused and consumed
(`grab=refused reason=offsurface`), and a down on that surface whose close has
already ended is refused the same way (`reason=no-close`); no touch Show is issued
at the landing. A down **in the band** is not refused by either: it is the same
request a press makes in that same moment, so the close is cancelled through the
press's cancel body (`grab=open`), or, when the close is already over, the same
open is asked for with nothing left to cancel (`grab=open window=hidden` when the
window has hidden). Only that body is shared: the pull keeps its finger,
spends the grant, and the follow starts the panel at the reveal entry and chases
the mapped position at 2.2 DIP/ms, so the panel's motion is the finger's and not a
settle. A window the retract has already hidden gets the ordinary show instead,
and the follow binds at the entry. The pull cannot recall a `Dismiss` already
inside the shell: the recovery ticket is its one fallback, and if that hide still
lands the landing reissues the open once (`intent open recovered entry=...`).
A landing retires no unspent grant: a band pull that outlives the close it was
granted against is still a pull on a closed menu, and the same finger opens it.
A finger whose grab was refused keeps no open right until it lifts, and a pull
that qualifies while the close is still cancellable cancels the command, so no
landing follows.

**No transition is not a stuck close.** A dismiss can be confirmed on a window
the shell had already hidden (a native hide won the race, the hide arrived
before the close was observed, or its transition was lost). The executor's 300 ms
confirmation is then the bound: with the cloak reading hidden and the dismiss
slot still naming that command, the Start side runs the same landing and logs
`win close observed reason=confirmed-without-transition`. If the cloak still reads
open, or the read fails (the read is tri-state; a failure is unknown, never
hidden), the command and its open generation are retained for the transition,
which lands them once (`exec dismiss retained served`) within one more
confirmation budget. A command from a menu that a later open replaced is stale
and cannot land the new hide, and a retained command that no transition reaches
retires with `exec dismiss retained expired` and the ordinary repair. A dismiss
stamped with generation 0 — a close intent that could not read the open record
while the section has none — is valid only while the section still has no open
record; once an open is published it is stale like any other mismatch, and the
fresh open cancels a still-queued command at the bridge. The
direction returns to unowned (open-capable) without another press, and the next
Win press opens exactly once; the transition itself is never synthesized.

## The touch show

- **The digitizer may go quiet during the uncloak and during a close.** The
  idle release would end the contact before the open adopts it; while a
  touch show is in flight (`controllerTouch`/`g_intentOpenActive`), or a close
  owns the panel with a reveal candidate, the idle is skipped. A real lift is a
  report and still ends the gesture with `src=report`; a genuine silence after
  those windows ends it with `src=idle`. A held release arms no deadline at all:
  the hold ends on the cloak event or on the executor's answer. A close that is
  still owed its landing holds the release until that ending arrives, and the
  close's repair bound (600 ms) is the one deadline the record arms.
- **Silence is not a lift for a contact a close consumed.** The latch and the
  dismiss mute survive the quiet through the hide: while a close owns the panel no
  idle expiry runs at all. A consumed contact ends on its lift report or on a
  report that proves a new contact (another device, a different asserted
  identifier, or a different asserted node on a device that names no contacts;
  `hid consumed ended ... reason=new-contact|device`). One with a Contact
  Identifier also survives silence. A node index can be
  handed to the next finger and the coordinate-only path has no name, so those
  two fall back to the idle fallbacks once the close has ended (the 120 ms
  candidate watchdog, then the 18 ms latch). Its risk is the
  original defect, narrowed to those devices: a consumed finger that stays
  silent past the landing and then resumes is read as a new down. On a Contact
  Identifier the mute holds until evidence, so the residual there is the other
  way: a device that reuses an identifier after a lost lift makes the next
  finger a continuation until it lifts.
- **The cold entry is 0.000.** The first open after a host start has no settled
  geometry (the measurement is deferred while the panel is owned), so the settle
  starts at 0 and the takeover chases. A warm bottom auto-hidden open uses 0.255
  (the bottom docked layout used 0.348).
- **The chase is rate-limited at 2.2 DIP/ms.** A 0.558 inherited gap takes
  ~254 ms to close; the panel is glued only after it (`behind=0.000`). The first
  visible frame is the entry, not the finger's mapped position; this is the
  accepted cap, not a lost frame. A pull that takes a live retract over skips the
  entry entirely: it starts at the panel's position and keeps the finger-to-panel
  offset it found (`live=1`), so an interrupted close reverses from where it is
  and a full pull still reaches 1 (the target is clamped). A held finger settles
  to the mapped position:
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

- **Current layout: bottom, auto-hidden** (`axis=3`): work `0,0,1440,900`, band
  `804..900`, panel `236,279..1229,840`, trail 279, Distance 900; measured
  2026-11-22. The left docked layout (`axis=0`, panel `60,151..1053,726`, trail
  1053, Distance 1392) was the 2026-11-21 measurement and is historical. The
  smoke reads the axis, band, rectangle and scale from the mod's own log lines,
  so it runs on whichever edge is live.
- The bottom auto-hidden bar did not reveal in the 2026-11-22 click-away session:
  `Shell_TrayWnd` stayed at `0,1798,2880,1894`, a cursor at the last row and
  `Win+T` focus did not draw it. The taskbar app-button case in the click-away
  measurement is therefore unmeasured; the Win/touch/click-away paths do not
  depend on the bar.
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
- **Still owed:** a consumed finger that is silent through a landing and then
  resumes. The virtual digitizer cannot deliver it (an injected contact that stops
  moving decays and answers `InjectTouchInput failed: 1460`), so the rule is
  supported by the decode path and the mute, not by a capture: a report that names
  the consumed contact is inert, and only a proven different contact ends the
  mute (`hid consumed ended ... reason=new-contact|device`). A real finger resting
  through a Win close and then moving is the one run that closes it.

## Not attempted

- Writing the launcher's `InternalState` or `Progress` from the bridge. The
  bridge only calls `ShowStartView`/`Dismiss`/`IsVisible`.
- Calling the launcher from an arbitrary hook frame. Every call is posted to the
  hidden window on the launcher thread.
- Holding the shell's `Hide`/`Dismiss` with a retract wait. The M3 close
  schedules the dismiss after the settle; there is no arbitration hold.
