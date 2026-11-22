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
dismiss and reveal sessions; the band admission and the grab's down hit-test; the
reveal entry and the
2.2 DIP/ms chase; the commit thresholds; the settle spring and the coast; the
scroll survey and its gate; the unhealed-window rule; the geometry snapshot and
its lock; the close record; and the peer liveness the bridge's interception
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
armed close tap, the close record's 600 ms repair bound (`RepairExpiredClose`),
the show/dismiss executor budgets, the settled measurement's
400 ms guaranteed turn, the scroller retry, the fallback watch and the root-hook
bootstrap probe. Nothing due disarms it. The scroller retry is the one deadline
written by the UI thread; it posts a private wake to the input window so a
planner that already read the previous value re-reads it. A held release (a
touch show in flight,
a close that owns the panel with a contact on the panel edge) contributes no
deadline: its hold
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
`INFINITE`. A renderer that has not attached is an event, not a poll: the input
thread waits on the renderer-usability event, and the root bootstrap keeps its own
deadline until the symbol answer is ready or refused. If that event could not be
created, the readiness check runs once per heartbeat for a bounded 20 wakeups and
then the thread parks with no timer; there is no unbounded renderer poll.
Measured with the menu closed, the old build woke 190.6 times/s and queried
`DwmGetWindowAttribute` 32 times/s; this one wakes 9.2 times/s and queries it
zero times, and the Start host's idle CPU fell from 0.625 % to 0.026 % of one
core (see [../docs/limits.md](../docs/limits.md)).

The close record. A close is an identity, not an age: one record (`g_close`,
input thread only) with a serial, the tick it was accepted at, the menu it was
accepted against (the open record's tick and generation, diagnostics only), a
phase (`Settling` — the retract runs, this side has not published its dismiss;
`AwaitingHide` — the dismiss is published; `Done`) and the 600 ms repair bound.
`TakeCloseOwnership` is the one acceptance for both families and runs before the
settle's first frame; `EndClose` is the one ending and only retires the record,
because what an ending owes beyond it differs (the landing has cleared the slots,
a cancel has published the open, a repair must leave a command that may still hide
the window identified). The endings are the landing, a pull cancel, a press or
band-pull cancel, an external takeover, an open observed during the close, a
terminal refusal/repair, and the repair bound. `CloseInFlight` reads the phase and
nothing else — the tick fields are data — and a call site that needs "a hide may
still land" asks `CloseHideMayLand` (a published dismiss is queued, armed or
executed-unconfirmed), which is the unhealed heal's second question. Acceptance
logs `close accepted serial=... kind=... open=... gen=... repair=...`; an ending
logs `close ended serial=... kind=... phase=... reason=... age=...ms`.

**M3 paths.**
- `ApplyBridgeIntent` — the only intent consumer. The record is passed to
  `ApplyBridgeIntentRecord`, so the landing reconciliation applies the record it
  examined instead of re-reading. `open` sets the controller's
  open direction; if the window is still shown (`StartWindowIsOpen`) the press
  cancels the close: the local dismiss slot is cleared (`ClearLocalCloseTap`), the
  close record is ended (`open-cancel`), and one settle from `TakeProgressNow()`
  back to 1
  (`intent open cancel from=...`); the cancel writes the recovery ticket
  (`CloseCancelTicket`: open tick + the cancelled executor sequence + a
  deadline) for the dismiss-won race, through the one helper the qualified pull
  also uses (`ArmCloseCancelTicket`), and arms it only for a published command
  (still pending, or executed with its hide unconfirmed). If the window is closed it holds the entry
  (`RevealEntryProgress`), sets `g_intentOpenActive` and asks for `exec show`; a
  show that cannot be published is refused (`intent open refused ...
  reason=no-executor`), the panel is not claimed and the ack says refused.
  `close` cancels both close schedules (`CancelCloseSchedules`: the local slot and
  the executor command), calls `TakeCloseOwnership` (the one acceptance: it stamps
  the close record — serial, acceptance tick, the menu, `Settling` — and publishes
  the closing direction before the settle's first frame), runs
  `BeginMirrorCloseSettle(TakeProgressNow())` and asks for `exec dismiss` at
  `settle + 30 ms` (`noteCloseDismissPublished` moves the record to
  `AwaitingHide`); a dismiss that cannot be published ends the record and releases
  the ownership, and answers refused. `external` revokes both schedules and the
  recovery ticket, ends the close record, drops the touch state and resyncs from
  the cloak. The
  attach publishes `exec cancel target=0` so a record the previous activation left
  queued cannot act on the menu this one opens.
- `ApplyExecAck` / `CheckExecAckTimeout` — one ack per command. A refused show or
  a refused/expired dismiss runs `ReleaseExecutorOwnership`, which releases
  the controller direction, `controllerTouch` and the pending open entry in one
  step so the direction resyncs from the cloak; a refused/expired/superseded
  answer for the recovery ticket's own sequence retires it **before the
  pending-slot gate**, because the open that cancelled the close already cleared
  the slot that gate reads. An `executed` dismiss clears the pending slot
  but keeps the command in `g_pendingDismissConfirm` and its open generation;
  `confirmed` closes it — by the landing itself when the cloak reads
  hidden (`win close observed reason=confirmed-without-transition`) and the
  command's generation is current — or **retains** it when the cloak reads open
  or unknown, for the transition to land under the same command and generation
  (`exec dismiss retained` → `exec dismiss retained served`). A retained command
  carries a bound. A generation that a
  later open replaced is stale (`exec dismiss stale`) and retires without
  landing; a fresh open retires **both** slots — a still-queued exec command is
  cancelled through the cancel path (`exec cancel request ... reason=fresh-open`)
  and a confirmation is dropped (`exec dismiss confirm
  dropped ... (fresh open=...)`), and a retained command that outlives its bound
  retires with `exec dismiss retained expired` and the ordinary repair.
  The stamp is the section's **current** generation at request time even when the
  close intent's own field is 0, and the comparison is exact: a zero stamp is
  valid only while the section still has no open record (no menu has opened
  yet), never as a wildcard for a later menu.
  `unconfirmed`/`unknown` is the same terminal repair, and `superseded` needs no
  repair. A 400 ms silence does the same from `g_pendingShowUntil` for a
  show and from `g_pendingDismissExecAt + 400 ms` for a dismiss (the deadline the
  bridge was told to run at), and publishes an `exec cancel` naming the sequence
  (show: target 0) so the executor cannot read the stale record as its newest
  command. A failed show while a dismiss is pending leaves the close's ownership
  alone. Every terminal dismiss repair ends the close record (`close ended ...
  reason=repair`) before `ReleaseExecutorOwnership`, so the panel is not left
  unreadable for the rest of the record's bound.
- `BeginEdgeCandidate` / `StartRevealFollow` — the down's decision, then
  admission only: contact, down point and clocks. Outside a close the down must
  be inside the band. During a close the point is hit-tested against the surface
  the retract still shows — its mapped progress
  (`RevealProgressDip`) at or above the panel's live one (`PeekProgressNow`), and
  the point inside the panel rectangle's cross range (`PanelCrossContains`, the
  axis the gesture does not slide along) — or
  the band: a granted grab logs `reason=grab`, and a point in the area the
  retract has left, a point perpendicular to and outside the panel, or one that
  cannot be judged (no renderer, stale layout, no
  distance), logs `reason=offsurface` or its own refusal and is muted for the
  rest of the contact. The down's own admission also records its **author**
  (`g_contactOrigin.author`: `Band` for the band's open claim, `Grab` for a
  granted grab, `None` otherwise), and only an authored contact may be adopted or
  promoted later. The show request moved to `TryClaimRevealOpen`, which runs
  on the first positioned report of that contact whose signed inward travel
  reaches `kAdoptTravelDip` (24 DIP), marks the attempt per contact (a refused
  claim is not retried), and then starts the pull generation, drops the previous
  release record, cancels a pending close and calls `PublishOpenClaim`. A grab
  spends the same crossing on `CancelCloseForQualifiedPull` instead
  (`reveal pull qualified ... grab=taken|open|refused`): `taken` hands the
  panel to the follow, `open` is a band down the retract had no panel left for —
  or one whose close has already ended — the explicit open request, but the
  finger follows it from the entry instead of accepting a press's settle (a
  hidden window gets the ordinary show), and `refused` consumes the contact.
  The claim block is shared with the press-only `BeginLandingOpen`
  and with the pull whose retract's window already hid: entry,
  insertion, pending open, direction, touch, show request; the preconditions and
  the log line stay at the callers. A band tap requests nothing.
- `ActivateRevealFollow` — the live takeover. A hidden panel is met at
  `RevealEntryProgress`; a panel a cancelled close left on screen is met where the
  transfer pinned it (`liveTakeover`: no entry floor, no `max(live, entry)`), and
  its chase starts at the takeover sample instead of after `kRevealEntryHoldMs`.
  A grab's activation binds to that take (`liveBound`): it uses `liveStart` and
  does not call `TakeProgressNow` again, so the reversal carries one
  `motion sample caller=pull` and no `caller=activate`. A live takeover without
  the binding still takes, as every other start does. The chase is
  `visible += clamp(target - visible, -rate*dt, +rate*dt)`, where `target` is the
  mapped finger for an ordinary follow and `mapped - liveOffset` for a live one:
  the interrupted retract keeps the finger-to-panel offset it took over instead of
  being driven toward the finger's absolute coordinate. A grab never activates
  here; only the transfer that clears its grant hands it the panel. The
  candidate branch in `UpdateRevealSession` requires the same 24 DIP signed inward
  travel (or `claimAttempted`), so a stationary finger on a shown
  but off-rest panel never acquires `Progress`.
- `RunPendingCloseTap` — despite the name, it requests `exec dismiss`; it is the
  schedule point, not a Windows-key tap. A dismiss that cannot be published ends
  the close record and releases the ownership.
- The pull cancel (`CancelCloseForQualifiedPull`, called from
  `TryClaimRevealOpen`) is the only abort of a close this mod still owns, and it
  is the grab's one opportunity: at the 24 DIP crossing it drops both schedules
  (the local gesture deadline and the armed executor command), ends the close
  record (`pull-cancel`), republishes the
  open direction, arms the same recovery ticket an explicit open request arms, and
  takes the
  retract over from the panel's live position. That is one take: the position is
  pinned and the follow binds to it on the same report (`liveBound`), so no settle
  runs between the cancel and the follow's first move and the panel never passes
  through the open spring. A keyboard, Start-button and click-away close are
  cancellable for the whole retract, because their dismiss is a command the bridge
  holds in an armed timer until the settle's end. A gesture close is cancellable
  during its own settle; once that ends, its command's deadline is the instant it
  was published, so the next crossing has no retract left.
  A refusal consumes the grab: a
  dismiss whose call already returned with its hide
  unconfirmed (`g_pendingDismissConfirm`), a published dismiss at or past that
  deadline (`reason=dispatch`: the bridge may already be inside the call), a
  close with nothing left to cancel, a layout
  with no distance, (for a down on the retract's surface) a retract already at
  or below the entry, and (for that same down) a close that has already ended
  (`reason=no-close`) all answer a named
  reason (`grab=refused reason=...`). The confirmation and the missing distance
  are checked wherever the close stands: neither is a statement about its window.
  A down **in the band** is not refused by either shape of "no retract left" —
  the retract already past the entry, or a close that has already ended. It is
  the same request a press makes in that moment, so the attempt becomes the
  ordinary open (`grab=open window=open|hidden entry=... reason=offsurface|no-close`,
  the reason naming which one routed it): a close still in flight is revoked
  through the press's cancel body (`CancelCloseForOpenRequest`) — both schedules
  dropped, the pending command cancelled, the close record ended, the open
  direction published, the recovery ticket armed — while a close that is already
  over has nothing to revoke, so no ticket is armed, no pending entry is cleared,
  and only the open direction is published. The press's tail is a settle
  (`SettlePressOpen`) and is not shared: a pull has a finger, so the grant is spent
  and the follow starts the panel at `RevealEntryProgress` and chases the mapped
  finger at 2.2 DIP/ms. A window the retract has already hidden has no panel to
  follow, so the ordinary show is claimed (entry, touch, show) and the follow
  binds at that entry (`window=hidden`); an open already on its way is that same
  open, so no second show is requested and the same entry is held for it. A
  refused transfer, not this open, runs
  `ConsumeGrabContact`: the reveal session and the contact's 1 ms
  timing go, panel ownership is republished so the layout deferral ends, and the
  contact gets the same mute a down refused at admission gets — it stays inert
  until its lift or a report that proves a new contact. The open contact keeps its
  session and follows. Contact-down no longer
  cancels anything; a tap leaves the close alone.
- `win close observed` — the landing body (`LandClosedMenu`). The cloak
  transition is its usual caller; the executor's confirmation is the second:
  when the probe reads hidden and the cloak agrees, and the dismiss slot still
  names that command, no
  transition carried the hide (the window was already hidden when `Dismiss` ran,
  a native hide won the race, or the transition was lost), so the same landing
  runs with `win close observed reason=confirmed-without-transition`. The
  command is the identity there too, and nothing pretends a transition happened.
  A landing serves **no touch open of its own**: no contact that is still down,
  no carried travel and no release record turns a landed close into a Show
  (`win close observed ... muted=1` reads the one mute: a contact the close
  consumed at its start, a down refused at admission, or a grab whose transfer
  was refused). A
  grab that crossed 24 DIP while the retract was still cancellable either took it
  (`grab=taken`), was refused and consumed (`grab=refused`), or — a band down —
  became the explicit open request and followed it (`grab=open`). A grant the
  contact has not spent yet is **not** retired by the landing: that contact is on
  its way to its own 24 DIP report, and a landing is not evidence that it gave up.
  `granted`, `edgeCandidate` and the panel ownership the grab holds all survive,
  so the next report still spends the grant — with the close now over, its
  crossing answers `no-close`, and a band down is the ordinary open of a closed
  menu (`grab=open ... reason=no-close`). Only a refusal consumes the grab
  (`ConsumeGrabContact`), and the mute never covers a live grant. A close that
  ended without a landing (a terminal executor repair, or the repair bound ending
  the record under a shown window) leaves the same crossing, and it ends the same
  way.
  The only open
  a landing still owes is the explicit request's obligation: if the ticket says an
  open — a press or a band pull — cancelled a close that still landed, the
  landing reissues that open
  (`intent open recovered entry=...`) through the one `BeginLandingOpen`
  transaction, with `controllerTouch=0`. The ticket is bound to the open tick,
  so a landing for another menu cannot serve it. The landing first reconciles an
  already-published `external` intent
  (`intent reconcile ... (before landing recovery)`) because the cloak event and
  the intent event are separate wakes and the landing reads the intent record
  itself before it acts. A landing is identified as this mod's close by its
  **command or its open-cancel ticket**, never by the close record: the record
  ends with the landing and its repair bound is 600 ms, while a maximum-duration
  close plus launcher dispatch and cloak delivery measured
  562–593 ms over ten runs (mean 578, 7 ms inside the bound at the worst) — so the
  bound would drop a genuine pull held through a slower one.
  The command is also bound to the **open generation** it was stamped with
  (`DismissGenerationCurrent`): a retained confirmation from a menu that a later
  open replaced is stale, and the landing classifies the hide as external.
  A retained command that the transition carries logs
  `exec dismiss retained served command=... gen=...` before the one
  `win close observed`, so the same command and generation are visible as the
  close that landed.
  The record still governs admission and the unhealed rule, and its repair bound
  is the one ending that is not evidence about the panel: it releases the
  direction and logs, while the command and the ticket keep naming the hide.
- The 18 ms idle is a one-shot deadline armed at the last report, and it is held
  through a touch show and through a close that owns the panel, so
  the digitizer's pause during the key/hide cannot drop the contact. A contact
  the close consumed keeps its latch and its mute through that pause: no idle
  expiry runs while a close owns the panel, and a consumed contact ends only on
  its lift report or on a report that proves a new contact (another device, a
  different identifier, or a different asserted node on a device that names no
  contacts; the decoder logs
  `hid consumed ended ... reason=new-contact|device`). One with a Contact
  Identifier also survives silence. A node index can be
  handed to the next finger and the coordinate-only path has no name, so those
  keep the idle fallbacks (the candidate's 120 ms watchdog, then the 18 ms
  latch) once the close has ended. A
  band candidate has its own watchdog (`kCandidateIdleHoldMs`, 120 ms): a sparse
  report stream must not expire an admitted flick before it reaches 24 DIP, and
  its release clears the latch, the origin, the open-check cache and the
  tracked identity. `newContact` is `!g_contactDown` in both sample handlers: a
  report gap alone is never a new down, so the admission origin survives it.

**Deleted here.** The `VK_LWIN` tap, the M2 close record's cross-process
publishes, the retract
request handler/answer, the reveal claim, the deferred candidate's close
machinery, the `win open continued` ticket (its stamp sat behind the follow's
own `CloseInFlight` guard, so it never fired) and the two-curve style switch.
The landing's finger adoption went with them: `RevealPullQualified`, the touch
branch of `BeginLandingOpen`, the landing's deferral release and release-record
consumption (`reveal reopen after close`, `reveal defer released`). The
late-release record lost its `deferred` and `readyQpc` fields with the same
rule: its window is measured from the release, and no landing consumes a record.
A close
consumes every contact on the glass, and a grab gets one transfer attempt.
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
or the deadline terminally refuses them, the worker's wait has no timeout**: it
blocks on the stop, Exec and IntentAck events and wakes only for a command or an
intent answer. The executor's
own timers — the dismiss delay, the visibility confirmation, the press latch,
the press ack bound and the open-generation gate — are message timers on the
launcher thread's window, which
the worker never services, so a pending close or an outstanding press adds no wake
to it. Each resolved address is
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
- `Dismiss_Hook`: method 6 is the Start button's close trigger. Method 5
  (Win+Tab) is the shell's takeover: while the controller owns the panel it is
  not swallowed — the original runs (Task View appears) and the bridge publishes
  `intent=external` first, so the controller retires the dismiss it had pending
  (`exec pass dismiss method=5 caller=wintab reason=external`). Any other
  dismiss is swallowed only while the controller owns the panel. Both button
  methods go through the same dispatcher as the Win key, so a press during a
  retract is a reopen whatever primitive it entered through.
- `HideStartAndCortana_Hook` (0x24BCB0): lock/unknown/Win+Tab and a forwarded
  modified press publish `intent=external` and pass. An admitted click-away is not
  a close by definition: it reaches the one dispatcher, so `closing` makes it the
  reopen and a panel the controller owns makes it the close (method 7 stays the
  click-away evidence, mapped to 6 on the Start side). A click with no panel to
  close and no close to reverse passes to the shell through the dispatcher's one
  no-panel branch (`own press pass trigger=clickaway entry=hide reason=no-panel`),
  which runs the original under the executor guard for an immediate and a queued
  click alike. The click-away's
  opening suppression (state 1/3, `reason=opening`) and the `controllerTouch`
  suppression require `caller=clickaway`, not the number 7 alone. A click-away
  delivered while our own executor `Dismiss` is on the stack (`depth=1`) takes the
  same path instead of the native Hide: the suppressions above apply, then the one
  dispatcher sees `t_execCall` and latches the reopen
  (`own hide clickaway ... reason=executor`). Method 7 is
  click-away evidence, so an **ordinary** caller's
  method-7 Hide passes to the shell (`reason=caller-scope`) instead of becoming a
  click decision — the number alone never makes a click ours, and the 7→6 mapping
  on the Start side needs the click-away trigger as well as the method. An
  ordinary caller's close (method 1/6) is the shell's close decision, not a
  toggle: it enters the same FIFO with entry `hide` and a forced `kind=close`, so
  it cannot overwrite an outstanding press's record; a full queue drops it with
  the depth in the line. The
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
  (`own toggle decide trigger=clickaway ... kind=close`), so the animation is not
  lost.

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
- **Press latch.** One dispatch. A close still settling publishes open now: the
  Start side clears its local slot and any pending command and settles the panel
  back from its live position. Only a close that has left the controller's reach
  latches the press — a hide already in flight (`InternalState` 9/11), or a
  `Dismiss` our executor has entered (`t_execCall`) — in a counted slot: the
  whole `QueuedPress` per owed press, the first press's tick, and the epoch the
  admission was decided under. A second press is appended, never allowed to
  replace the first (`own press latch owed count=2`); the serve publishes the
  first open, queues the remainder, and the ack barrier makes the second the
  opposite direction. Every published open arms the generation gate with the
  section's open generation read just before its publication; the next close —
  latched remainder or an ordinary press after an open — is held until the Start
  side publishes a different open generation (the controller's own statement that
  the new menu exists), with a bounded 20 ms one-shot
  (`own press held ... reason=open-gen`), never `IsVisible` and never the queue
  depth. The bound is 900 ms from the first hold; an expired gate logs
  `own press open gen expired` and serves the close, and a `SetTimer` that cannot
  be armed logs `open gen timer failed` and serves it at once, so the queue never
  parks past its bound. Only a close waits; an open, and a reopen from `closing`
  that restores the same menu without a new generation, pass at once. A held
  close goes back to the head of the FIFO and keeps its admission tick. An
  armed-but-not-dispatched executor dismiss is revocable, so it publishes the open
  rather than deferring it;
  the hidden event then arms a
  50 ms timer and the timer serves the same open intent off the hidden event's
  stack. A show at the hidden event's own tick was swallowed (state stuck at 3),
  so the deferral is the serve point. The arm is posted to the executor window
  when the hidden event runs off the launcher thread, so the slot and its timer
  stay owned by that thread. A newer press — Win key, either Start-button
  primitive or the click-away — is appended to the counted slot
  (`own press latch owed count=...`), never discarding an earlier one; the serve
  publishes the first open and queues the remainder, so their directions
  alternate behind the ack barrier. The serve
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
  window.
- **Press queue.** The channel has one intent record and one event, so two
  presses that decided before the consumer ran used to overwrite each other and
  both read the same direction. Admitted presses now wait in a four-entry FIFO on
  the launcher thread: the first is published at once, and the next only when the
  intent ack for the outstanding sequence arrives or its 1000 ms bound expires.
  The ack is read on this thread; the executor worker (which can wait on the
  `IntentAck` event) posts `kExecIntentAckMessage` and never reads the queue. The
  direction is sampled when a press is published, so the press after a close
  reads `2` and reopens. A queue overflow drops the arriving press with the depth
  in its line (`reason=queue-full`) and never executes it ahead of the queue; a
  press still unpublished past its 1500 ms wait runs the shell's original
  primitive under `t_execCall` (`reason=queue-wait`), and the reserve poll
  re-evaluates that bound so a press behind a held slot does not wait for a serve.
  The bound is paused while a normal press is executing (`servableTick=0`): the
  head's clock restarts when that slot is released, so a press behind a claimed
  predecessor is never aged out ahead of it. An external barrier is not an
  executing press, so its held presses keep the bound. The Start side claims the
  sequence before it copies the record and re-checks the claim after the copy
  (`intent drop ... reason=record-gone`); a claimed record it cannot apply is
  answered `intent revoked seq=...` with `kSwipeAckSuperseded`, so the publisher
  is never left waiting for an ack that cannot come. An ack timeout is then
  resolved by proof: any claim for the sequence is
  `decision=consumed awaiting=ack` — including one that lands between the first
  read and the abort (`reason=claim-after-abort`) — and the next press is not
  served and its bound stays paused until the ack, a changed epoch, or the peer's
  **state** going away (`decision=consumed reason=peer-gone`) releases it. A stale
  heartbeat never releases a claimed press: the controller may be paused between
  two heartbeats with its validated copy in hand. A stale tick releases an
  unclaimed record only; at admission that is `reason=stale-unclaimed`, which is
  the caller's pre-check rather than the last read. Reconciliation re-reads the
  claim itself: a claim found there on the same epoch with the state alive keeps
  the slot (`reason=claimed-at-admission`, `decision=consumed awaiting=ack`) and
  serves nothing until its ack; a claim that lands between that read and the
  abort holds as `reason=claim-after-abort`. Every
  Start branch publishes the resulting direction before its ack, so the press
  after an ack reads the new direction (close then reopen, not two closes).
  `taken != seq` aborts the record (`SwipeIpcAbortIntent`, the counter's odd
  state), re-reads the claim, and only replays when the abort succeeded and no
  claim exists (`decision=replay reason=revoked|epoch-changed`); a record that
  already moved is `decision=superseded reason=record-moved`. A changed
  `peerEpoch` at the watchdog **or at the next admission** is a restart: a taken
  press is consumed without its ack (`reason=epoch-changed`), an untaken one is
  aborted and replayed, the external barrier is released, and the queue advances
  without needing a timer (`own press abandoned ...`). When the admission
  reconciles, it re-reads the launcher (readiness and open/visible) after the
  replay or drain and before deciding the arriving press, so an old native open
  leaves the arriving press closing, not opening again
  (`own press pass ... reason=state-after-replay` if the read fails). The queue's
  own lines are
  `own press queued|published|ack|ack timeout|dropped`. An
  `external` intent clears both the queue and the latch, is published at once,
  and sets the same barrier as a state, not a timer: it stands even when
  `SetTimer` fails (`own external barrier seq=... armed|unarmed`) and is cleared
  only by its ack (`own external ack ... reconciled`), by a changed epoch
  (`released reason=epoch-changed`), or by its peer's state going away
  (`released reason=peer-gone`), with an unclaimed barrier also released by a
  stale tick (`released reason=stale-unclaimed`, checked on the reserve poll and
  at the next admission). A claimed barrier is held through a stale heartbeat; a
  live but silent peer keeps it; the timeout line is
  `decision=reserved`, never a reconciliation guess, and the poll runs any held
  press past its bound.

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

### swipe_ipc_shared.h (merged mod, 224 bytes)
The channel contract. See `docs/ipc.md`. The M2 close/retract slots are **gone**,
and so is the debug command channel (0.7.0): its record, mutex, event and parser
were deleted. The records that ship are the open generation, the
intent and its ack and the `intentTaken` claim, the executor command and its ack,
one controller direction
word, the touch flag, and the peer liveness triple `peerTick` (204,
GetTickCount64 low bits) / `peerState` (208, alive/gone) / `peerEpoch` (212, the
activation identity), plus `intentTaken` (216, the consumer's claim, written
before it acts). Every offset is asserted; the magic is `0x53323053`. There
is **no version word**: the object namespace
(`Local\SwipeFor10Start_SingleMod2_*`) is the ABI identity, so an old development
image cannot join and a future layout changes the prefix (the `2` marks the
224-byte layout). The executor validates
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
