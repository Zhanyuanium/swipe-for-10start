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

**Settings and logging.** Three settings: `AnimationStyle` (`Windows 11` /
`Windows 10`, the settle's damping), `GestureStartSpeedPercent` (0-300, the
release-speed carry), and `DiagnosticTrace` (boolean, default false). Every
decision line goes through one `TraceLog` wrapper, which always calls `Wh_Log`
(Windhawk's own debug logging decides whether it is stored) and writes the same
line to `<mod storage>\renderer.log` or `\bridge.log` only while
`DiagnosticTrace` is on. The file line keeps the `t=<GetTickCount64>` prefix the
tools read. The cap is `kTraceMaxLines` (20,000 lines): the line count is seeded
from the existing file and the next write truncates it; the counter resets only
when the truncation succeeded, and a failure stops the file half for the session
with one `Wh_Log` line naming the error. A file that does not exist yet seeds as
empty, while an existing file that cannot be opened or read is **not** seeded:
the next write retries, so a transient share cannot make the cap believe the file
is empty. Only a complete `WriteFile` increments the count; a partial append is
left to the next seed, which re-counts the file itself. The cut has its own `FILE_WRITE_DATA`
handle and the line is written through a `FILE_APPEND_DATA` handle: a handle
with both bits does not append (measured: writes at offset 0). The CRLF is in
the same buffer, so one line is one `WriteFile` and two activations cannot tear
each other's lines. Formatting is
truncating
(`_vsnwprintf_s`/`_snwprintf_s` with `_TRUNCATE`) so a long setting or object
name cannot terminate the host through the CRT invalid-parameter handler. The
setting is read at init and on every settings change; there is no other debug
switch and no file is opened while it is off.

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
deadline service on every wake and from every positioned report, at most once per
100 ms, and only while the peer is advertised. Readiness is
continuing: `RendererReady()` (root attached and not invalidated) is part of the
announcement, and a renderer loss withdraws
(`ipc peer gone reason=renderer-lost terminal=0`) before the bridge can intercept;
the UI thread's renderer event republishes when a usable replacement attaches.
Withdrawal is
recursive and precedes every stop: `gone` plus `controllerDirection=0` plus
`controllerTouch=0`, written before the input thread is stopped and again as it
leaves for any reason. Publication and the terminal withdrawal share one SRW
critical section, so a startup cannot write `alive` after the unload wrote `gone`;
the consumer withdraws before it unregisters its devices, and a renderer
republish that races the unload is refused by the same lock.

**What wakes the input thread.** There is no period. The thread waits on the
stop event, the bridge's intent and executor-ack events, the renderer event, the
cloak event and one high-resolution one-shot timer, and `NextInputDeadlineMs`
arms that timer for the soonest deadline only: the 8 ms coast start (later for a
follow, whose 50 ms entry guard cannot start one earlier), the 18 ms
idle release, the 120 ms candidate watchdog, the 18 ms latch/identity expiry, the
armed close tap, the show/dismiss executor budgets, the settled measurement's
400 ms guaranteed turn, the scroller retry, the fallback watch and the root-hook
bootstrap probe. Nothing due disarms it. The scroller retry is the one deadline
written by the UI thread; it posts a private wake to the input window so a
planner that already read the previous value re-reads it. A held release (a
touch show in flight,
a close in flight with a contact on the panel) contributes no deadline: its hold
ends on a cloak event or an executor answer, and a deadline that could not act
would spin. The tick-domain deadlines are armed at 1 ms when already past
because serving them clears them; the coast start is armed only while ahead.

The open/close state is pushed. `SetWinEventHook(EVENT_OBJECT_CLOAKED,
EVENT_OBJECT_UNCLOAKED)` is installed and removed on the input thread, scoped to
this process and filtered to the CoreWindow HWND; the callback takes no lock,
reads no XAML, queries no DWM and writes no trace — it sets an auto-reset event
and nothing else. `ServiceRootVisibility` reads `DWMWA_CLOAKED` once per signal
and runs the transition code the 20 ms poll used to run. A requested or freshly
observed transition opens `g_visibilityPollUntil`, in which `ServiceDeadlineWork`
runs the slow 100 ms fallback poll (`win state fallback open=...` when it, not
the event, saw the change); a band contact that the shell may open natively opens
the same watch. The watch is bounded (`kVisibilityGraceMs` 1000 ms for a request,
`kVisibilitySettleWatchMs` 500 ms after an observation), so nothing polls while
no transition is outstanding.

The only idle wake is the peer heartbeat: while `peerState=alive`, the wait times
out every 100 ms to refresh `peerTick`; with the peer withheld or gone it is
`INFINITE`. Before its first publication the thread additionally wakes every
100 ms until the renderer attaches, which is the net under the renderer event.
Measured with the menu closed, the old build woke 190.6 times/s and queried
`DwmGetWindowAttribute` 32 times/s; this one wakes 9.2 times/s and queries it
zero times, and the Start host's idle CPU fell from 0.625 % to 0.026 % of one
core (see [../docs/limits.md](../docs/limits.md)).

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
- `StartRevealFollow` — admission only: contact, down point and clocks. The show
  request moved to `TryClaimRevealOpen`, which runs on the first positioned
  report of that contact whose signed inward travel reaches `kAdoptTravelDip`
  (24 DIP), marks the attempt per contact (a refused claim is not retried), and
  then starts the pull generation, drops the previous release record, cancels a
  pending close and calls `PublishOpenClaim`. The claim block is shared with
  `BeginLandingOpen`: entry, insertion, pending open, direction, touch, show
  request; the preconditions and the log line stay at the callers. A band tap
  requests nothing.
- `ActivateRevealFollow` — the live takeover at `RevealEntryProgress`; the
  chase is `visible += clamp(mapped - visible, -rate*dt, +rate*dt)`. The
  candidate branch in `UpdateRevealSession` requires the same 24 DIP signed
  inward travel (or `openClaimAttempted`), so a stationary finger on a shown
  but off-rest panel never acquires `Progress`.
- `RunPendingCloseTap` — despite the name, it requests `exec dismiss`; it is the
  schedule point, not a Windows-key tap. A dismiss that cannot be published
  releases the ownership.
- The pull cancel (`CancelCloseForQualifiedPull`, called from
  `TryClaimRevealOpen`) is the only abort of a close this mod still owns: it
  drops both schedules and republishes the open direction once the contact has
  crossed 24 DIP inward. Contact-down no longer cancels anything; a tap leaves
  the close alone.
- `win close observed` — after the landing, a pull still down (or a release
  inside its readiness window: the release on a closed menu, the landing for a
  deferred record) issues one fresh show and the open adopts it:
  `reveal reopen after close pull=1 late=0 entry=...`. The pull that was under
  the close is the pull that takes the menu back, and it must be qualified:
  `RevealPullQualified` applies the same 24 DIP signed inward travel at the
  contact's last positioned report (an active follow qualifies by construction,
  and the late-release record only exists for a qualified pull). A finger that
  merely rests in the band through the retract is admitted deferred and does not
  reopen it; the landing still logs `reveal defer released candidate=1 down=1`.
  If the ticket says an open
  cancelled a close that still landed, the landing reissues that open
  (`intent open recovered entry=...`); the ticket is bound to the open tick,
  so a landing for another menu cannot serve it. The landing first reconciles an
  already-published `external` intent
  (`intent reconcile ... (before landing recovery)`) because the cloak event and
  the intent event are separate wakes and the landing reads the intent record
  itself before it acts. Both landing opens run the one `BeginLandingOpen`
  transaction; the pull keeps its precedence, and **only a still-down contact
  marks the touch**: a late release record reopens with `controllerTouch=0`
  (`pull=0 late=1`), because that finger is gone and the next real desktop
  click must close the menu. A successful late adoption also clears the pending
  open entry it inherited from the show.
- The 18 ms idle is a one-shot deadline armed at the last report, and it is held
  through a touch show and through a close in flight, so
  the digitizer's pause during the key/hide cannot drop the contact. A band
  candidate has its own watchdog (`kCandidateIdleHoldMs`, 120 ms): a sparse
  report stream must not expire an admitted flick before it reaches 24 DIP, and
  its release clears the latch, the origin, the open-check cache and the
  tracked identity. `newContact` is `!g_contactDown` in both sample handlers: a
  report gap alone is never a new down, so the admission origin survives it.

**Deleted here.** The `VK_LWIN` tap, the close record publishes, the retract
request handler/answer, the reveal claim, the deferred candidate's close
machinery, the `win open continued` ticket (its stamp sat behind the follow's
own `CloseInFlight` guard, so it never fired) and the two-curve style switch.
The remaining close scheduler is the one dismiss path. 0.7.0 also deletes the
fixed-address gate (the file-version compare, the executable-range check, both
prologue probes and the module poll thread) and the animation-request hook with
its debug switch; the root is resolved by symbol.

**Lifecycle.** No pin and no instance guard: Windhawk unloads the image, so the
role quiesces before it returns. The stop event is created before the input
thread; `Wh_ModBeforeUninit` withdraws the peer, signals the thread and joins it
`INFINITE` (never on that thread). The thread unhooks the WinEvent hook, clears
the published cloak handle, closes the cloak event and the deadline timer,
unregisters raw input, releases the
1 ms timer period, destroys its window and unregisters its fixed class under
`&__ImageBase` before exiting; a failed unregister is retried, never returned as
success. Registration is fixed-name and treats `ERROR_CLASS_ALREADY_EXISTS` as
failure, exactly as the executor's class does.
`Wh_ModUninit` runs one UI teardown turn on the captured dispatcher: revokes the
XAML handlers and the settle batch, restores resting progress and the neutral
translation, resets the scroller watches and drops the UI-owned objects
(`[[clang::no_destroy]]` protects a skipped uninit, not a normal one). Counted
module entries are drained through their event; the IPC view and handles close
last. A revoke or thread proof that cannot be obtained keeps waiting rather than
returning success. The root hook is resolved by public symbol
(`StartUI::App::PrepareRootElement`): the `LoadLibraryExW` notification and the
init/AfterInit probes only record the module handle (`GetModuleHandleW`), and
the input thread runs `HookSymbols` on its first tick, because
the first call starts Windhawk's PDB fetch and blocking the shell's loader
thread makes the menu host look hung. The apply (`Wh_ApplyHookOperations`) runs
inside the same symbol-install lock, before the lock is released: `BeforeUninit`
either waits for the complete install or takes the lock first and makes the
install refuse before `HookSymbols`, so no root hook becomes live after the
stop. A false result while the fetch runs is
pending (logged with its attempt, elapsed time and the 120 s deadline) and is
retried; only the deadline produces `unresolved`, no root hook and no peer
service. The peer is published only once the root is attached: `PublishPeerReady`
requires `g_rendererReady` as well as the hook, so a session that missed
`PrepareRootElement` (the cold fetch) advertises nothing and every press stays
native until the host restarts. The reference address `module+0xAD730` is
logged as evidence, not required. A disable/enable
without a host restart reuses the XAML root and has no renderer until the host
restarts (a measured limit, not a bug to paper over).

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

**Launcher symbols.** Every launcher entry point, the `s_pLauncher` slot and
`IsVisible` are resolved by one `WindhawkUtils::SYMBOL_HOOK` array and one
`HookSymbols` call on the loaded `twinui.pcshell.dll`. The names are DIA's
undecorated public-symbol strings for 10.0.26100.9949. The fixed VAs, the
file-version/timestamp compare, the executable-range check and the prologue/body
probes are deleted. The `LoadLibraryExW` notification only records the returned
module; the executor worker runs the resolution, because the first call starts
Windhawk's PDB fetch and blocking a thread Explorer owns freezes the shell. The
worker's wait has a deadline only while that probe is unresolved: it is throttled
to one attempt per 100 ms, so a false result while the fetch runs is
pending (`exec launcher symbols pending attempt=... waited=...ms
deadline=240000ms`), never a refusal. The deadline produces `unresolved` and no
launcher hook; a still-blocked fetch leaves the mod passive (the shell's own
path serves every trigger) but never blocks Explorer. **Once the symbols resolve
or the deadline terminally refuses them, the worker's wait is `INFINITE`**: it
blocks on the stop and Exec events and wakes only for a command. The executor's
own timers — the dismiss delay, the visibility confirmation and the press latch —
are message timers on the launcher thread's window, which the worker never
services, so a pending close adds no wake to it. Each resolved address is
logged against the reference RVA (`exec launcher symbol label=... rva=...
expected=... match=0|1`); the set line
`exec launcher symbols resolved ... count=13 matches=13 match=1` is the evidence
line. One missing symbol refuses the whole set and the hooks are not applied: a
toggle that can open but not close is worse than the shell's path. The private
layout is gated on identity, not names: the fixed file version must be
`10.0.26100.9974` and all 13 resolved RVAs must match the reference. On any
other image the hooks pass, `LauncherNow` returns null, the executor refuses
and the shell serves the press; one
`exec launcher layout unverified ... native=1 reason=...` line states it, and
the verified image logs `exec launcher layout verified ... native=0`. The apply
(`Wh_ApplyHookOperations`) is inside the symbol-install critical section, before
the lock is released: `BeforeUninit` either waits for the complete install or
takes the lock first and makes the install refuse before `HookSymbols`, so no
hook can become live after the stop.

**Executor.** A hidden message-only window, class
`WindhawkSwipeFor10Start.Executor` (fixed, registered under `&__ImageBase`),
created only from a known launcher entry: the first of `OnHotkeyInvoke`,
`Dismiss`, `ShowStartView`, the manager's `Show` or the worker's targeted
`WH_GETMESSAGE` adopts that thread (`exec launcher thread`), and a hook on any
other thread logs `exec launcher thread mismatch` and creates nothing. The
library-load hook never creates the window. `ERROR_CLASS_ALREADY_EXISTS` is a
failure, never adopted. Shutdown is an order: BeforeUninit clears readiness and
signals the stop; Uninit joins the worker **INFINITE**, destroys the one
published window on its owner thread (directly there, otherwise through the
private `kExecTeardownMessage`; `WM_NCDESTROY` signals `g_execWindowGone`), then
unregisters the class under the same instance, closes the handles and the
channel last. The old creator reservation, admission flag, creator drain,
reaper and `CloseCallLease` count are gone; a window or worker that does not go
away blocks the unload. The procedure refuses every dispatch (`kExecMessage`,
timers) for an HWND other than the currently published one. Readiness is
published by the worker itself after its exit guard is installed, and the gate
also reads `g_disabled`, so a late positive write from a stopping worker is
unreadable. A worker waits on `Exec` with a 100 ms timeout and posts
`WM_APP+0x5A`; each timeout runs the launcher symbol probe, so the same thread
owns the fetch and the commands but never a thread Explorer uses. The WndProc
reads the command, validates it and calls the launcher with an empty stack, and
a `WAIT_FAILED`/unexpected exit clears readiness.
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
probe or timer failure is answered `unknown`.

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
  modified press publish `intent=external` and pass. A close intent is a close
  only from the click-away caller or the Win/button caller; the click-away's
  opening suppression (state 1/3, `reason=opening`) and the `controllerTouch`
  suppression require `caller=clickaway`, not the number 7 alone. The same rule
  gates admission: method 7 is click-away evidence, so an **ordinary** caller's
  method-7 Hide passes to the shell (`reason=caller-scope`) instead of becoming a
  close intent — the number alone never makes a close ours, and the 7→6 mapping
  on the Start side needs the click-away trigger as well as the method. The
  own-show
  exception requires the executor's own stack, an unattributed caller and state
  1/3 (`caller=unknown method=7 state=1|3 depth>0 reason=own-show`). Every
  decision line prints caller, method, internal state and executor depth
  together, so a method-7 number cannot stand in for its source. Its capability
  gate is one function (`HideCanServeClose`): peer, caller wrappers verified, started worker,
  recorded launcher thread (never adopted by a Hide), a window on this thread and
  a callable `Dismiss`. Any missing piece passes with
  `exec pass hide method=... reason=no-peer|callers-unverified|no-executor|
  wrong-thread|no-window|no-primitive`, because a native close converted into a
  command nothing can execute is worse than the shell's own close.
- **Caller classification.** The caller is a thread-local category set by a
  wrapper hook around a proven caller's original call, restoring the previous
  value so nesting and re-entry are safe; it is not a return-address match.
  `XamlLauncherState::DismissStartAndCortana` (0x4991C0) is Ordinary,
  `XamlLauncher::DismissSynchronouslyWithoutTransition` (0x48F7C0) is Lock, and
  `ImmersiveMonitorHelper::ActiveZBandChangedForMonitor` (0x24FC50) is
  ClickAway; its body carries the synchronous `mov edx,7; call [vtable+0x48]`
  (0x24FDDE). A ClickAway scope with any other method is treated as unclassified
  and passes external, never as a second click-away. The old fourth return
  address, 0x4916D0, is a WRL delegate invoker with no public symbol; DIA names
  the instantiation after a `CortanaExperienceManager` typed event, but WRL
  shares one invoker per signature, so the name does not identify the event
  source. It forwards `edx` to the same Hide and is not wrapped: a Hide it
  reaches outside a wrapped caller stays `unknown` and native. The measured
  desktop click-away runs inside the `ActiveZBandChangedForMonitor` scope
  (`intent close trigger=clickaway method=7`), so the animation is not lost.

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
  hidden event's stack. A click-away in state 1/3 from the ActiveZBand caller,
  or a Hide(7) inside our own `ShowStartView` in state 1/3 from the executor's
  call stack with an unattributed caller, is the pointer-away check and is
  swallowed (`caller=clickaway reason=opening`, or
  `caller=unknown depth>0 reason=own-show`); otherwise the served open is
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
read order. 0.7.0 deletes the gesture publisher: the four observer hooks
(`InputGestureOperationStarted`/`Ended`, `ReportEdgeGesture`,
`SetAnimationData`), the manager's `Hide` observation hook, the shared gesture
mapping with its seqlock writer, and the inactive-at-unload publication. The
Start half's Raw Input is the product's contact source; the decision and
executor lines are the evidence that replaced it.

0.7.0 also deletes the lifetime machinery the pinned design needed: both image
pins, the Start instance guard, both `ResetReinitState` paths, the bridge
`CloseCallLease`/`g_closeCalls` drain, the creator reservation, admission flag,
creator drain and reaper, and every residual/deferred-success return. `kExecTeardownMessage`
and the destructive first-frame work remain.

**Lifecycle.** No pin, no instance guard, no re-enable state. The Start side joins
its input thread `INFINITE` in BeforeUninit, runs one UI teardown turn, drains the
counted batch entries, then closes the channel. The bridge joins its posting
worker `INFINITE`, destroys the one published window on the launcher thread and
waits for `WM_NCDESTROY`, unregisters the fixed class under `&__ImageBase`, then
closes the handles and the channel. A proof that cannot be obtained (a thread
that does not exit, a delegate that does not revoke, a window that does not go
away) keeps the unload waiting; there is no residual success and no deferred
close. `docs/lifecycle.md` has the order and the blocking cases.

### swipe_ipc_shared.h (merged mod, 216 bytes)
The channel contract. See `docs/ipc.md`. The M2 close/retract slots are **gone**,
and so is the debug command channel (0.7.0): its record, mutex, event and parser
were deleted. The records that ship are the open generation, the
intent and its ack, the executor command and its ack, one controller direction
word, the touch flag, and the peer liveness triple `peerTick` (204,
GetTickCount64 low bits) / `peerState` (208, alive/gone) / `peerEpoch` (212, the
activation identity). Every offset is asserted; the magic is `0x53313053`. There
is **no version word**: the object namespace
(`Local\SwipeFor10Start_SingleMod_*`) is the ABI identity, so an old development
image cannot join and a future layout changes the prefix. The executor validates
each command before any launcher call: show method 1/11/16 with flags under the
`0x8|0x4` mask, dismiss method 1/6 with no flags, cancel method/flags 0; anything
else is refused, never clamped. The producer checks the same shared vocabulary
(`SwipeShowAllowed`/`SwipeDismissAllowed`/`SwipeCloseIntentAllowed`) before it
swallows a press; an unsupported combination is passed to the original at
admission, so the executor refusal is defence in depth. A click-away intent
keeps method 7 as evidence; the Start side maps it to executor method 6.

## Diagnosis (not needed at runtime)

- `start-menu-touch-gesture-probe.wh.cpp` — the early gesture-source experiment
  for the explorer side. It stops its worker before final unload: the probe
  refuses new hook installation from `Wh_ModBeforeUninit` and keeps the
  log/handles when the scan thread does not join.
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
