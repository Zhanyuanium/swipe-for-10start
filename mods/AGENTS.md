# mods/
> L2 | Parent: ../AGENTS.md

## Shipping mod

### swipe-for-10start.wh.cpp (0.7.0, StartMenuExperienceHost.exe + explorer.exe)
One image, installed once and loaded into two hosts that never share a process.
The file holds both halves in named namespaces — `start` (the controller and the
touch gesture) and `bridge` (the launcher executor and the non-touch triggers) —
with the Windows, WinRT, Windhawk and shared headers included once at file
scope. The first operation of `Wh_ModInit` reads the host executable name
(`GetModuleFileNameW`, basename, case-insensitive) and picks `start::Role` or
`bridge::Role`; the four Windhawk entry points forward to that table and no
callback rediscovers the process, and an unknown host installs nothing. The two
runtime lifetimes stay two: explorer can outlive a Start restart, and a
touch-first open can reach the launcher before any Win or Start-button hook ran.
The version follows the project tag; the former Start 0.44.0 and bridge 0.25.0
versions are gone.

The text below describes the Start half; the explorer half follows it.

### The controller (namespace start, StartMenuExperienceHost.exe)
The controller and the touch gesture. It reads the digitizer through Raw Input
(the XAML/DirectManipulation sources are empty on this UI), owns the panel's
`Progress`, plans every settle, and decides the toggle from executor intents.

**What it owns.** The raw HID decode (report IDs, `TipSwitch`/`InRange`,
contact identity and its evidence clock, the coordinate-only fallback); the
dismiss and reveal sessions; the band admission; the reveal entry and the
2.2 DIP/ms chase; the commit thresholds; the settle spring and the coast; the
scroll survey and its gate; the unhealed-window rule; the geometry snapshot and
its lock; the close suppression; and the peer liveness the bridge's interception
gate reads. `peerEpoch` is claimed once per attach; `peerState=alive` is
published only by the input thread once its window, its message loop and its
intent event exist (intent-consumer readiness — a refused raw-input registration
is logged and does not withdraw it), and the tick is refreshed from that thread's
idle tick and every positioned report at most once per 100 ms. Withdrawal is
recursive and precedes every stop: `gone` plus `controllerDirection=0` plus
`controllerTouch=0`, written before the input thread is stopped and again as it
leaves for any reason. Publication and the terminal withdrawal share one SRW
critical section, so a startup cannot write `alive` after the unload wrote `gone`;
the consumer withdraws before it unregisters its devices.

**M3 paths.**
- `ApplyBridgeIntent` — the only intent consumer. The record is passed to
  `ApplyBridgeIntentRecord`, so the landing reconciliation applies the record it
  examined instead of re-reading. `open` sets the controller's
  open direction; if the window is still shown (`StartWindowIsOpen`) the press
  cancels the close: the local dismiss slot is cleared (`ClearLocalCloseTap`), the
  suppression cleared, and one settle from `TakeProgressNow()` back to 1
  (`intent open cancel from=...`); the cancel writes the recovery ticket
  (`CloseCancelTicket`: open tick + the cancelled executor sequence + a
  deadline) for the dismiss-won race, and arms it only for a published command
  (still pending, or executed with its hide unconfirmed). If the window is closed it holds the entry
  (`RevealEntryProgress`), sets `g_intentOpenActive` and asks for `exec show`; a
  show that cannot be published is refused (`intent open refused ...
  reason=no-executor`), the panel is not claimed and the ack says refused.
  `close` cancels both close schedules (`CancelCloseSchedules`: the local slot and
  the executor command), calls `TakeCloseOwnership` (which publishes the closing
  direction before the settle's first frame), runs
  `BeginMirrorCloseSettle(TakeProgressNow())` and asks for `exec dismiss` at
  `settle + 30 ms`; a dismiss that cannot be published releases the ownership and
  answers refused. `external` revokes both schedules and the recovery ticket,
  clears the suppression, drops the touch state and resyncs from the cloak. The
  attach publishes `exec cancel target=0` so a record the previous activation left
  queued cannot act on the menu this one opens.
- `ApplyExecAck` / `CheckExecAckTimeout` — one ack per command. A refused show or
  a refused/expired dismiss runs `ReleaseExecutorOwnership`, which releases
  the controller direction, `controllerTouch` and the pending open entry in one
  step so the direction resyncs from the cloak; a refused/expired/superseded
  answer for the recovery ticket's own sequence retires it **before the
  pending-slot gate**, because the open that cancelled the close already cleared
  the slot that gate reads. An `executed` dismiss clears the pending slot
  but keeps the command in `g_pendingDismissConfirm`; `confirmed` closes it,
  `unconfirmed`/`unknown` is the same terminal repair, and `superseded` needs no
  repair. A 400 ms silence does the same from `g_pendingShowUntil` for a
  show and from `g_pendingDismissExecAt + 400 ms` for a dismiss (the deadline the
  bridge was told to run at), and publishes an `exec cancel` naming the sequence
  (show: target 0) so the executor cannot read the stale record as its newest
  command. A failed show while a dismiss is pending leaves the close's ownership
  alone.
- `StartRevealFollow` — the band show request, once per open
  (`g_intentOpenActive`), sets `controllerTouch` so the 18 ms idle does not end
  the contact while the shell uncloaks. The claim block is shared with
  `BeginLandingOpen` (`PublishOpenClaim`): entry, insertion, pending open,
  direction, touch, show request; the preconditions and the log line stay at the
  callers.
- `ActivateRevealFollow` — the live takeover at `RevealEntryProgress`; the
  chase is `visible += clamp(mapped - visible, -rate*dt, +rate*dt)`.
- `RunPendingCloseTap` — despite the name, it requests `exec dismiss`; it is the
  schedule point, not a Windows-key tap. A dismiss that cannot be published
  releases the ownership.
- The pull cancel (`OnHidSampleStep`, a new contact while the dismiss is armed)
  drops both schedules and republishes the open direction, so a menu that stays
  open is an owned open again.
- `win close observed` — after the landing, a pull still down (or a release
  inside the 300 ms window) issues one fresh show and the open adopts it:
  `reveal reopen after close pull=1 late=0 entry=...`. The pull that was under
  the close is the pull that takes the menu back. If the ticket says an open
  cancelled a close that still landed, the landing reissues that open
  (`intent open recovered entry=...`); the ticket is bound to the open tick,
  so a landing for another menu cannot serve it. The landing first reconciles an
  already-published `external` intent
  (`intent reconcile ... (before landing recovery)`) because the idle timer
  outranks the intent event. Both landing opens run the one `BeginLandingOpen`
  transaction; the pull keeps its precedence and marks the touch.
- The 18 ms idle is held through a touch show and through a close in flight, so
  the digitizer's pause during the key/hide cannot drop the contact.

**Deleted here.** The `VK_LWIN` tap, the close record publishes, the retract
request handler/answer, the reveal claim, the deferred candidate's close
machinery, the `win open continued` ticket (its stamp sat behind the follow's
own `CloseInFlight` guard, so it never fired) and the two-curve style switch.
The remaining close scheduler is the one dismiss path.

**Lifecycle.** Pinned image (`GetModuleHandleExW(...PIN)`), per-process instance
mutex, counted module entries, stop-event-driven input thread, XAML revokes with
the residual rules, the `peerState=gone` publish before every residual return,
and the per-activation reset. A disable/enable without a host
restart reuses the XAML root and has no renderer until the host restarts (a
measured limit, not a bug to paper over).

### The executor (namespace bridge, explorer.exe)
The launcher executor and the non-touch triggers.

**Launcher read.** `s_pLauncher` at `twinui.pcshell.dll + 0x948E80` is the
outer object; the execution interface is `outer + 0x18`; `[+0x160]` is the
`IXamlLauncherState` and must be `> 0x10000`. `ShowStartView` is
`twinui+0x141AF0`, `Dismiss` `+0x25D670`, `IsVisible` `+0x1F9B40`. One checked
reader answers readiness (`LauncherStateReady`): the hook paths, `LauncherNow`
and `OwnResyncFromState` use it, so the constructor's `0x100` sentinel is never
dereferenced. `OwnIsVisible`/`OwnInternalState` refuse an unready pointer
themselves, because the post-call resync reads `[launcher+0x160]` again and the
pre-call check cannot cover the new value; an unready resync logs
`own state resync skipped reason=state-not-ready` and leaves the cached direction
untouched. A delayed callback reacquires the singleton instead of holding a
hook's pointer.

**Executor.** A hidden message-only window, class
`WindhawkSwipeExecutor_<WndProc>`, created only from a known launcher entry: the
first of `OnHotkeyInvoke`, `Dismiss`, `ShowStartView`, the manager's `Show` or the
worker's targeted `WH_GETMESSAGE` adopts that thread (`exec launcher thread`), and
a hook on any other thread logs `exec launcher thread mismatch` and creates
nothing. The library-load hook never creates the window. Window creation and
window shutdown are one protocol: a creator reserves `g_execCreating`, asks
`g_execAdmissionOpen`, publishes the HWND and releases the reservation; the stop
closes admission, drains the reservation, joins the worker, then destroys the
identified owner window (directly on its thread, otherwise through the private
`kExecTeardownMessage` the owner answers from `WM_NCDESTROY`; only that HWND
signals). A re-enable keeps admission closed and drains the reservation **before**
it snapshots the HWND, so a creator that published and released between two reads
cannot be seen as absent; it never clears the reservation to prove quiescence, and
a creator that does not leave makes the enable refuse. The procedure refuses
every dispatch (`kExecMessage`, timers) for an HWND other than the currently
published one, so a survivor from a previous activation cannot act on this
activation's channel. Readiness is published by the worker itself after its exit
guard is installed, and the service gate also requires admission, so a late
positive write from a stopping worker is unreadable. A worker waits on
`Exec` and posts `WM_APP+0x5A`; the WndProc reads the command and calls the
launcher with an empty stack, and a `WAIT_FAILED`/unexpected exit clears readiness.
A launcher-thread watermark (`g_execAcceptedSeq`) accepts each record once, so a
double-delivered signal cannot run a show or a dismiss twice. `show`/`cancel` run
now; `dismiss` arms `kExecDismissTimer` for `executeAt`; an unarmable timer runs
the dismiss at once instead of leaving it armed with no deadline. A record that
cannot be read at its deadline retries ten times at 10 ms (ten timer arms, not a
wall-clock deadline), then answers refused. A record dispatched at or after
`due + 400 ms` is answered `expired` and the primitive is not called; the record's
publisher epoch is captured at acceptance and checked immediately before the
call, a mismatch answering `superseded`; a record that names a menu the section
has already left is answered `superseded menu=... now=...`. The 300 ms probe logs `exec visible`
with `hidden|visible|unknown`; a successful dismiss also owes a
300 ms visibility confirmation (`exec dismiss confirmed|unconfirmed|unknown
seq=... visible=... after=<real ms>`) instead of treating `hr=0` as a hide, and a
probe or timer failure is answered `unknown`. The window class name is
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
  swallowed); every other Hide is a close intent. Its capability gate is one
  function (`HideCanServeClose`): peer, proven caller sites, started worker,
  recorded launcher thread (never adopted by a Hide), a window on this thread and
  a callable `Dismiss`. Any missing piece passes with
  `exec pass hide method=... reason=no-peer|callers-unverified|no-executor|
  wrong-thread|no-window|no-primitive`, because a native close converted into a
  command nothing can execute is worse than the shell's own close.
- **The service gate.** Every decision hook runs `BridgeCanServeOnLauncherThread`
  before it reads the panel, and the peer is the first condition: the Start
  side's `peerState=alive` and a `peerTick` younger than 600 ms, with the clock
  read after the tick. Then an open channel, a started worker and no disable in
  flight, then the recorded launcher thread (`reason=wrong-thread` otherwise),
  then a window created on this thread. The gate returns the **admitting epoch**:
  the same read that proved the peer is the value the latch stores, so a restart
  between admission and latch creation cannot relabel an old press. Without the
  gate the trigger passes to the
  shell (`own pass trigger=... reason=no-peer` and the other reasons); nothing is
  swallowed for a command nobody would execute. The channel alone is not the
  condition: this mod creates it, so it stays valid with the Start mod disabled.
  A failed worker startup is the same case for the life of the attachment. 600 ms
  is a detection budget, not a delivery guarantee.
- `IntentShouldClose`: the controller's one direction word decides. `2` is a
  reopen (close in flight), `1` is a close, and `0` falls back to the cloak and
  the internal state (1/3/5 open or opening, 7/9/11 closing). The shell's state
  is never the direction while the controller owns: a gesture close retracts the
  panel with state 5 and the cloak open until the executor's Dismiss. The
  controller publishes `2` in `TakeCloseOwnership` for exactly that. The
  dispatcher samples the direction once and passes it to both the decision and
  its log line, so the line cannot name a direction the decision did not use.
- **Press latch.** One dispatch. A close still settling publishes open now when
  no executor dismiss is armed; the Start side clears its local slot and any
  pending command and settles the panel back from its live position. A hide in
  flight (`InternalState` 9/11), or an executor dismiss already armed at the
  bridge, latches the
  press (one slot, tick, trigger, and the epoch the admission was decided under);
  the hidden event then arms a
  50 ms timer and the timer serves the same open intent off the hidden event's
  stack. A show at the hidden event's own tick was swallowed (state stuck at 3),
  so the deferral is the serve point. The arm is posted to the executor window
  when the hidden event runs off the launcher thread, so the slot and its timer
  stay owned by that thread. Supersession lives in the shared
  `DispatchToggle`, so any newer press — Win key or either Start-button primitive
  — discards the slot (`own press latch dropped reason=superseded`). The serve
  re-reads the peer and the epoch (`reason=peer-gone|peer-changed`), drops a latch
  whose dismiss was refused or expired (`reason=dismiss-refused|dismiss-expired`)
  or whose hide never landed, and expires after 1200 ms. Timer-arming failures are
  handled, never papered over: a latch whose **initial** timer cannot be armed is
  dropped, and one that cannot be deferred is dropped rather than shown on the
  hidden event's stack. A click-away in state 1/3, or a Hide(7)
  inside our own `ShowStartView` in state 1/3, is the pointer-away check and is
  swallowed (`reason=opening`/`reason=own-show`); otherwise the served open is
  hidden the instant it appears. The latch is a best-effort obligation, not a
  guarantee: a press can still be lost when the peer dies inside the detection
  window, and a swallowed press has no shell fallback.

**Deleted here.** The retract request/plan wait, the arbitration hold, the
close-record read/seed, `OwnDecideFrom` and its open/close/reopen helpers, the
tapped-close watermark, and (0.22.0) the dormant old press-reopen:
`g_ownPendingOpen`, `g_ownPendingTick`, `kOwnReopenLatchMs`,
`ServePendingReopenNow`, `ArmReopenServe`, `OwnReopenTimerProc`,
`kOwnReopenTimerId`, `kOwnReopenServeDelayMs`, `g_ownReopenTimerArmed`,
`g_ownReopenServedTick` and the cached `g_ownLauncher`. The flag was assigned
`false` only, so nothing could arm that timer; the live reopen paths are the press
latch, the Start side's `intent open cancel` and `intent open recovered`.
0.24.0 also deletes the guard types that never had an instance
(`OwnDecisionCallGuard`, `OwnCloseExecutorGuard`, `OwnForeignCloseGuard`), their
thread-locals (`t_ownDecisionCall`, `t_ownForeignClose`, `t_ownCloseExecutor`) and
`g_ownCloseTrigger`, whose only writes were resets. `OwnExternalForwardGuard` and
`t_execCall` remain: both mark a live call. 0.25.0 deletes the generic `WM_CLOSE`
shutdown (replaced by `kExecTeardownMessage`), the readiness publication written
by the thread that called `CreateThread`, and the reaper's HWND-before-creator
read order.

**Lifecycle.** Pinned image, the counted close-hook frames and executor
dispatches for the v7 channel, the drain with `drained`/`stalled` reports, and the
re-enable refusal while a close frame or an unfinished executor teardown is still
inside. On unload admission closes (before readiness clears), the creator
reservation drains, the worker is joined before either handle is closed, and the
identified window and its four timers are destroyed (directly on the owner thread,
otherwise through the private teardown the owner acknowledges). A stop that did
not complete keeps the handles and the
channel (`ipc close deferred`) and the next enable refuses (`exec reinit
refused: ...`) until that join or destruction has completed; a worker that exited
since is reaped. The re-enable drains the creator **before** it snapshots the
HWND, and never clears the reservation itself; a creator that outlives the drain
makes it refuse. See `docs/lifecycle.md` for the test-build hold that stages the
creator/reaper interleaving.

### swipe_ipc_shared.h (v7, 744 bytes)
The channel contract. See `docs/ipc.md`. The M2 close/retract slots are **gone**
from v3 on; the records that ship are the open generation, the intent and its
ack, the executor command and its ack, one controller direction word, the touch
flag, and the peer liveness triple `peerTick` (732, GetTickCount64 low bits) /
`peerState` (736, alive/gone) / `peerEpoch` (740, the activation identity). Every
offset is asserted; the magic is `0x53313053` and the version is 7. v5 replaced
v4's owning/phase pair with that one direction word (the bridge reads one atomic
value, never a pair) and dropped `controllerOpen`, which no reader used. v6 added
the liveness pair and took the section from 736 to 744 bytes. v7 adds `peerEpoch`
at 740, which the 8-byte alignment had already paid for, so the size stays 744 and
the **version word** is what refuses a v6 section handed over by an older build.
0.44.0/0.25.0, the last pair versions before the 0.7.0 merge, add no field: the
ack's outcome word gained values (`confirmed`,
`unconfirmed`, `unknown`, `superseded`) and the visible word gained
`unknown = -1`, both inside the existing `LONG` slots, so the version stays 7 and
`tools/ipc.ps1`/`docs/ipc.md` only document the new names.

### start_gesture_shared.h
The older gesture mapping (`Local\WindhawkStartGestureShared`), kept for the
process lifetime on purpose: a consumer and a late gesture callback may still
hold its view. The bridge publishes the shell's `SetAnimationData` ids and the
release position there for the probe mods.

## Diagnosis (not needed at runtime)

- `start-menu-touch-gesture-probe.wh.cpp` / `start-menu-touch-gesture-consumer.wh.cpp`
  — the early gesture-source experiments. Both stop their worker before final
  unload: the probe refuses new hook installation from `Wh_ModBeforeUninit` and
  keeps the log/handles when the scan thread does not join; the consumer wakes its
  reader, joins it, and keeps the mapping when the join times out.
- `msgprobe.wh.cpp` — pointer messages and `GetPointerInfo` ownership; writes
  `msgprobe.log` into the folder `Wh_GetModStoragePath` returns and mirrors each
  line to the Windhawk log. Its poller
  and summary thread are woken, joined, and released only after the join; a join
  timeout keeps the handle so a later attempt can still join.
- `inputprobe.wh.cpp` — the Raw Input dump; it writes `inputprobe.log` into the
  folder `Wh_GetModStoragePath` returns (both probes use the API, never a
  ProgramData literal). It must not be enabled beside the
  shipping mod: Raw Input allows one sink per usage per process and the probe
  takes the stream. Its window subclasses are detached on their owner threads
  (`kProbeDetachMessage`), never from the unload thread, and a chain with a later
  subclass is reported instead of restored over.

## Laws

Complete membership · one line per file · parent link · technical terms first.
Every behaviour sentence is measured; a claim that cannot name its probe and its
log line does not belong here.

[PROTOCOL]: Update this header on change, then check AGENTS.md
