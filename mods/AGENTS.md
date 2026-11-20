# mods/
> L2 | Parent: ../AGENTS.md

## Shipping pair

### start-menu-legacy-swipe.wh.cpp (0.41.0, StartMenuExperienceHost.exe)
The controller and the touch gesture. It reads the digitizer through Raw Input
(the XAML/DirectManipulation sources are empty on this UI), owns the panel's
`Progress`, plans every settle, and decides the toggle from bridge intents.

**What it owns.** The raw HID decode (report IDs, `TipSwitch`/`InRange`,
contact identity and its evidence clock, the coordinate-only fallback); the
dismiss and reveal sessions; the band admission; the reveal entry and the
2.2 DIP/ms chase; the commit thresholds; the settle spring and the coast; the
scroll survey and its gate; the unhealed-window rule; the geometry snapshot and
its lock; the close suppression.

**M3 paths.**
- `ApplyBridgeIntent` — the only intent consumer. `open` sets the controller's
  open direction; if the window is still shown (`StartWindowIsOpen`) the press
  cancels the close: the local dismiss slot is cleared (`ClearLocalCloseTap`), the
  suppression cleared, and one settle from `TakeProgressNow()` back to 1
  (`intent open cancel from=...`); the cancel sets `g_intentCancelPending` for the
  dismiss-won race. If the window is closed it holds the entry
  (`RevealEntryProgress`), sets `g_intentOpenActive` and asks for `exec show`.
  `close` cancels both close schedules (`CancelCloseSchedules`: the local slot and
  the executor command), calls `TakeCloseOwnership` (which publishes the closing
  direction before the settle's first frame), runs
  `BeginMirrorCloseSettle(TakeProgressNow())` and asks for `exec dismiss` at
  `settle + 30 ms`; `external` revokes both schedules and the recovery ticket,
  clears the suppression, drops the touch state and resyncs from the cloak.
- `ApplyExecAck` / `CheckExecAckTimeout` — one ack per command. A refused show or
  a refused/expired dismiss runs `ReleaseExecutorOwnership`, which releases
  `controllerOwning`, `controllerTouch` and the pending open entry in one step so
  `owning/open/phase` resync from the cloak; a 400 ms silence does the same from
  `g_pendingShowUntil` for a show and from `g_pendingDismissExecAt +
  400 ms` for a dismiss (the deadline the bridge was told to run at). A failed
  show while a dismiss is pending leaves the close's ownership alone.
- `StartRevealFollow` — the band show request, once per open
  (`g_intentOpenActive`), sets `controllerTouch` so the 18 ms idle does not end
  the contact while the shell uncloaks.
- `ActivateRevealFollow` — the live takeover at `RevealEntryProgress`; the
  chase is `visible += clamp(mapped - visible, -rate*dt, +rate*dt)`.
- `RunPendingCloseTap` — despite the name, it requests `exec dismiss`; it is the
  schedule point, not a Windows-key tap.
- The pull cancel (`OnHidSampleStep`, a new contact while the dismiss is armed)
  drops both schedules and republishes the open direction, so a menu that stays
  open is an owned open again.
- `win close observed` — after the landing, a pull still down (or a release
  inside the 300 ms window) issues one fresh show and the open adopts it:
  `reveal reopen after close pull=1 late=0 entry=...`. The pull that was under
  the close is the pull that takes the menu back. If the ticket says an open
  cancelled a close that still landed, the landing reissues that open
  (`intent open recovered entry=...`). Both landing opens run the one
  `BeginLandingOpen` transaction; the pull keeps its precedence and marks the
  touch.
- The 18 ms idle is held through a touch show and through a close in flight, so
  the digitizer's pause during the key/hide cannot drop the contact.

**Deleted here.** The `VK_LWIN` tap, the close record publishes, the retract
request handler/answer, the reveal claim, the deferred candidate's close
machinery, the `win open continued` ticket (its stamp sat behind the follow's
own `CloseInFlight` guard, so it never fired) and the two-curve style switch.
The remaining close scheduler is the one dismiss path.

**Lifecycle.** Pinned image (`GetModuleHandleExW(...PIN)`), per-process instance
mutex, counted module entries, stop-event-driven input thread, XAML revokes with
the residual rules, and the per-activation reset. A disable/enable without a host
restart reuses the XAML root and has no renderer until the host restarts (a
measured limit, not a bug to paper over).

### start-menu-touch-gesture-bridge.wh.cpp (0.22.0, explorer.exe)
The launcher executor and the non-touch triggers.

**Launcher read.** `s_pLauncher` at `twinui.pcshell.dll + 0x948E80` is the
outer object; the execution interface is `outer + 0x18`; `[+0x160]` is the
`IXamlLauncherState` and must be `> 0x10000`. `ShowStartView` is
`twinui+0x141AF0`, `Dismiss` `+0x25D670`, `IsVisible` `+0x1F9B40`. One checked
reader answers readiness (`LauncherStateReady`): the hook paths and `LauncherNow`
use it, so the constructor's `0x100` sentinel is never dereferenced and a delayed
callback reacquires the singleton instead of holding a hook's pointer.

**Executor.** A hidden message-only window, class
`WindhawkSwipeExecutor_<WndProc>`, created only from a known launcher entry: the
first of `OnHotkeyInvoke`, `Dismiss`, `ShowStartView` or the manager's `Show`
adopts that thread (`exec launcher thread`), and a hook on any other thread logs
`exec launcher thread mismatch` and creates nothing. The library-load hook never
creates the window. A worker waits on `Exec` and posts `WM_APP+0x5A`; the WndProc
reads the command and calls the launcher with an empty stack. A launcher-thread
watermark (`g_execAcceptedSeq`) accepts each record once, so a double-delivered
signal cannot run a show or a dismiss twice. `show`/`cancel` run now; `dismiss`
arms `kExecDismissTimer` for `executeAt`; an unarmable timer runs the dismiss at
once instead of leaving it armed with no deadline. A record that cannot be read at
its deadline retries for 100 ms, then answers refused. The 300 ms probe logs
`exec visible`; Dismiss is confirmed by `visible=0`. The window class name is
unique per image, so a reinstall does not inherit the old WndProc.

**Triggers.**
- `OnHotkeyInvoke_Hook` (0x14195C): Shift/Ctrl/Alt is a hard gate; the shell's
  own open flag passes; otherwise the one dispatcher decides. It creates the
  executor window on this thread.
- `ShowStartView_Hook`: method 11 is the Start button's open trigger; any other
  show is swallowed only while the controller owns the panel (the duplicate edge
  gesture).
- `Dismiss_Hook`: method 6 is the Start button's close trigger; any other
  dismiss is swallowed only while the controller owns the panel. Both button
  methods go through the same dispatcher as the Win key, so a press during a
  retract is a reopen whatever primitive it entered through.
- `HideStartAndCortana_Hook` (0x24BCB0): lock/unknown/Win+Tab and a forwarded
  modified press publish `intent=external` and pass; method 7 is a close intent
  unless `controllerTouch` is set (the opening contact's shell click-away is
  swallowed); every other Hide is a close intent. Without the proven caller sites
  the hook passes at once (`reason=callers-unverified`).
- **The service gate.** Every decision hook runs `BridgeCanServeOnLauncherThread`
  before it reads the panel: an open channel, a started worker and no disable in
  flight. Without them the trigger passes to the shell (`own pass trigger=...
  reason=...`); nothing is swallowed for a command nobody would execute. A failed
  worker startup is the same case for the life of the attachment.
- `IntentShouldClose`: while `controllerOwning` is set, phase 2 is a reopen
  (close in flight) and an owned phase 1 is a close; otherwise the cloak and the
  internal state (1/3/5 open or opening, 7/9/11 closing) are the shell resync.
  The shell's state is never the direction while the controller owns: a gesture
  close retracts the panel with state 5 and the cloak open until the executor's
  Dismiss. The controller publishes phase 2 in `TakeCloseOwnership` for exactly
  that.
- **Press latch.** One dispatch. A close still settling publishes open now when
  no executor dismiss is armed; the Start side cancels the armed dismiss and the
  close and settles the panel back from its live position. A hide in flight
  (`InternalState` 9/11), or an executor dismiss already armed, latches the
  press (one slot, tick, trigger); the hidden event then arms a 50 ms timer and
  the timer serves the same open intent off the hidden event's stack. A show at
  the hidden event's own tick was swallowed (state stuck at 3), so the deferral
  is the serve point. A second press replaces the slot
  (`own press latch replaced ...`); the serve logs `own press served wait=...`.
  A click-away in state 1/3, or a Hide(7) inside our own `ShowStartView` in
  state 1/3, is the pointer-away check and is swallowed
  (`reason=opening`/`reason=own-show`); otherwise the served open is hidden the
  instant it appears.

**Deleted here.** The retract request/plan wait, the arbitration hold, the
close-record read/seed, `OwnDecideFrom` and its open/close/reopen helpers, the
tapped-close watermark, and (0.22.0) the dormant old press-reopen:
`g_ownPendingOpen`, `g_ownPendingTick`, `kOwnReopenLatchMs`,
`ServePendingReopenNow`, `ArmReopenServe`, `OwnReopenTimerProc`,
`kOwnReopenTimerId`, `kOwnReopenServeDelayMs`, `g_ownReopenTimerArmed`,
`g_ownReopenServedTick` and the cached `g_ownLauncher`. The flag was assigned
`false` only, so nothing could arm that timer; the live reopen paths are the press
latch, the Start side's `intent open cancel` and `intent open recovered`.

**Lifecycle.** Pinned image, the counted close-hook frames and executor
dispatches for the v4 channel, the drain with `drained`/`stalled` reports, and the
re-enable refusal while a close frame or a timed-out worker is still inside. On
unload the ready flag clears, the worker is joined before either handle is closed,
and the launcher thread destroys the window and its three timers with an
acknowledged `WM_CLOSE`.

### swipe_ipc_shared.h (v4, 744 bytes)
The channel contract. See `docs/ipc.md`. The M2 close/retract slots are **gone**
from v4; the records that ship are the open generation, the intent and its ack,
the executor command and its ack, and the controller flags. Every offset is
asserted; the magic is `0x53313053` and the version is 4.

### start_gesture_shared.h
The older gesture mapping (`Local\WindhawkStartGestureShared`), kept for the
process lifetime on purpose: a consumer and a late gesture callback may still
hold its view. The bridge publishes the shell's `SetAnimationData` ids and the
release position there for the probe mods.

## Diagnosis (not needed at runtime)

- `start-menu-touch-gesture-probe.wh.cpp` / `start-menu-touch-gesture-consumer.wh.cpp`
  — the early gesture-source experiments.
- `msgprobe.wh.cpp` — pointer messages and `GetPointerInfo` ownership.
- `inputprobe.wh.cpp` — the Raw Input dump. It must not be enabled beside the
  shipping mod: Raw Input allows one sink per usage per process and the probe
  takes the stream.

## Laws

Complete membership · one line per file · parent link · technical terms first.
Every behaviour sentence is measured; a claim that cannot name its probe and its
log line does not belong here.

[PROTOCOL]: Update this header on change, then check AGENTS.md
