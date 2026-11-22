# swipe-for-10start - touch swipe for ExplorerPatcher's legacy Win10 Start

C++ / Windhawk 2.0 mods + PowerShell 7 test tools
<directory>
mods/ - Windhawk mod sources (4 sources + 1 header: swipe-for-10start, touch-gesture-probe, msgprobe, inputprobe; swipe_ipc_shared.h)
tools/ - PowerShell probes, injectors, animation harnesses, the panel-edge tracker, the regression smoke, the store packager and the local installer (16 scripts + AGENTS.md)
artifacts/ - captured measurements (20 CSVs + AGENTS.md; `inputprobe.dll` is an untracked local build output of `mods/inputprobe.wh.cpp`)
docs/ - technical documentation (architecture, input, gesture, animation, geometry, ipc, lifecycle, testing, limits, store, README + AGENTS.md)
</directory>
<config>
README.md - user-facing quick start (English; README.zh-CN.md is the Chinese page)
README.zh-CN.md - the Chinese page of the user-facing quick start
CHANGELOG.md - release history: each tag with the Start/bridge versions and channel it shipped (CHANGELOG.zh-CN.md is the Chinese page)
CHANGELOG.zh-CN.md - the Chinese page of the release history
.gitignore - Windhawk/MSVC build outputs and local logs
</config>

Laws: measure before designing · one data source per claim · docs and code move together

## What ships (M3.4, 0.8.0, one mod)

One mod, two processes. `mods/swipe-for-10start.wh.cpp` is installed once and
loaded into `StartMenuExperienceHost.exe` (namespace `start`) and into
`explorer.exe` (namespace `bridge`): the controller owns the touch gesture, the
panel claims, the settle planner, the gates and the controller state; the
executor owns the launcher call and the non-touch triggers. Wh_ModInit picks one
role table from the host executable name before either half runs, and the other
half's hooks, threads and handles are never created there.

The controller decides; the executor performs. The Win key
(`XamlLauncher::OnHotkeyInvoke`), the Start button (`ShowStartView` method 11,
`Dismiss` method 6) and the click-away (`HideStartAndCortana` method 7) reach one
toggle decision and publish one `intent(seq, kind, trigger, method, flags, open)`;
the Start side applies it — `exec show`, or the one mirrored close settle and then
`exec dismiss` — and answers `intent ack`. A click on the desktop therefore
closes a settled panel and reverses a close that is still in flight, exactly like
the other two presses; method 7 stays the click-away evidence in the intent and
the Start side maps it to the executor's 6. External takeovers (Win+Tab, lock,
display change, a forwarded modified Win press) publish `intent=external` and are
never held; Win+Tab's `Dismiss(5)` runs the shell's original (Task View) and the
record retires the controller's pending dismiss.

The bridge intercepts only while the Start side is alive. The bridge creates the
channel, so a mapped section says nothing about the controller; the Start side
publishes `peerEpoch` (its activation identity, claimed at attach), `peerState`
(alive/gone) and `peerTick` (refreshed from its input thread at most once per
100 ms), and every consuming hook reads them before any decision. Availability is
published **from the ready consumer** — the input thread, once its window, its
message loop and its intent event exist — and withdrawn before that thread is
stopped and again as it leaves; the withdrawal also resets the shared direction and
touch, so a kill while `closing` cannot be read as the next activation's decision.
Readiness is *intent-consumer readiness*: a refused raw-input registration is
logged (`hid reg ... ok=0`) and reported as `touch=`/`pen=` on the alive line, but
it does not withdraw the intent half the interception needs. Publication and the
terminal withdrawal share one critical section; a flag check followed by an
independent store let the startup write `alive` after the unload wrote `gone`. A
trigger with no live peer logs `own pass trigger=... reason=no-peer` (the close
intent logs `exec pass hide method=... reason=no-peer`) and calls the original, so
the shell's own toggle works exactly once. A few presses during a start or a stop
are passed natively; that is the intended trade. 600 ms is a detection budget, not
a delivery guarantee.

The bridge's gate proves a capability, not just a peer: the recorded launcher
thread (checked **before** any launcher-local field, including the existing-window
path), a window this activation created on that thread, a started worker, and for
the directional close intent the callable `Dismiss` the command will need. A Hide
never adopts an unproven thread (`reason=wrong-thread|no-window|no-primitive`).
A wrong-thread launcher call is not a clean error: never call from a hook frame,
and the executor window is created only from a known launcher entry.

A press is recovered, not guaranteed. The bridge's one dispatch reads the
controller's published direction: `closing` (a close in flight)
makes the press an open and `open` makes it a close. The launcher's
state is only the resync for a panel the controller does not own. The click-away
is the same dispatch, so a desktop click reverses a close exactly as the Win key
and the Start button do; a method-7 callback with no panel and no close passes to
the shell instead of opening a closed menu. That direction
cannot come from the shell: a gesture close retracts the panel with
`InternalState` 5 and the cloak still open for the whole settle, until the
executor's `Dismiss`; the controller publishes `closing` in `TakeCloseOwnership`,
before the settle's first frame. A close still settling publishes open at once:
the controller drops the local dismiss slot and cancels a pending executor
command, then settles the panel back from its live position (`intent open cancel
from=...`). The recovery ticket exists **only for a published command**: still
queued or armed when the cancel ran (`cancelled != 0`), or executed with its hide
not yet confirmed. A local-only cancel arms none, because no dismiss is inside
the shell and a later hide of the same menu is not that press's close. It is bound
to that menu's open tick (the tick the open record published), to the executor
sequence it revoked, and to a deadline (that command's deadline plus the ack
budget and the confirmation probe); a refused/expired/superseded answer for
**that sequence** retires it, and the landing serves it only before the deadline.
A still-open poll does not clear it: an entered `Dismiss` may land at any point
while the window is up. A dismiss already inside the shell's `Dismiss` call
cannot be recalled; the window hides, and the landing reissues that open once
(`intent open recovered entry=...`) — after reconciling an `external` intent that
arrived with the landing (applying the record it examined, not a second read),
because the cloak event and the intent event are separate wakes and the landing
reads the intent record itself. A
shell hide in flight (`InternalState` 9/11), or a `Dismiss` our own executor has
already entered, latches the press in a counted slot: the first press is the
reopen the landing owes, each further press is appended, and the serve publishes
the first open and then the remainder in order, so their directions alternate.
Every open the bridge publishes becomes the generation gate: the generation it
was published against is captured before the publication, and the next press,
latched remainder or an ordinary press after an open, waits until the Start side
publishes a different open generation. That record is the controller's own
statement that the new menu exists; `IsVisible` and the queue depth are not used.
The wait is bounded (900 ms from the first hold), and a `SetTimer` that cannot be
armed clears the gate and serves the press at once, so the queue never parks past
its bound. Only a close waits: a reopen from `closing` restores the same menu and
publishes no new generation, and an open is never held. A parked close is put
back at the head of the FIFO, so the order and the admission tick are kept.
The slot keeps the first press's tick and **the epoch the
admission was decided under**. A dismiss that is only armed is not that case: it
is still revocable, so the press publishes the open, the Start side revokes the
command and settles the panel back from its live position. The hidden event arms
a 50 ms timer and the timer
serves the same open intent after the event's stack has unwound; a show at the
event's own tick was swallowed (state stuck at 3, no Shown). The arm is posted to
the executor window when the event runs off the launcher thread. A newer press
from any primitive is appended to the counted latch, not allowed to discard an
earlier one; a serve re-reads the peer and the epoch, a refused or expired dismiss
invalidates the slot, and a latch whose **initial** timer could not be armed is
dropped rather than left with no guaranteed callback. The residual loss window is
the peer's detection budget: a press after the gate closed, or a latch armed
before a kill and served after it, is the shell's. A press the bridge already
swallowed has no shell path left — it is dropped, not passed on.

One press, one intent, and one press at a time. The channel has one intent record,
so a second press that decided before the consumer ran would overwrite the first
and read the same direction; both then meant the same toggle and one user press
was lost. Admitted presses now wait in a four-entry FIFO on the launcher thread:
the first is published, and the next is published only when the ack for the
outstanding sequence arrives or its 1000 ms bound expires, so its direction is
the one the controller published while applying the press before it. The ack event
is waited on by the executor worker and posted to the executor window; the hook
never waits. A press the queue cannot serve is handled by its state, not by a
replay: the arriving press on a full queue is dropped with the depth in its line
(`reason=queue-full`), never executed ahead of the four already queued; a press
still unpublished past its 1500 ms wait runs the shell's own original primitive
under the executor guard (`reason=queue-wait`), and that bound is evaluated by
the reserve poll as well, so a press behind a held slot does not wait for a
serve. The bound is **paused while a normal press is executing** (`servableTick`
0 until the slot is released): the head's 1500 ms restarts then, so a press that
waited behind a claimed predecessor is never aged out ahead of it. An external
barrier is not an executing press, so the presses held behind it keep the bound.
The Start side claims the
sequence **before** it copies the record and re-checks the claim after the copy;
a record the bridge aborted in between is dropped **and answered as superseded**
(`intent drop ... reason=record-gone`, `intent revoked seq=...`), so the
publisher's slot is never left waiting for an ack that cannot come. The ack
timeout then decides by proof, not by the
heartbeat:

- any claim for this sequence is `decision=consumed awaiting=ack`: the take
  proves execution, not that the resulting direction is published, so the next
  press waits for the ack and its queue-age bound stays paused. This includes a
  claim that lands between the timeout's first read and its abort
  (`reason=claim-after-abort`): the controller may already hold a validated copy
  and will apply and ack it. An ack is the bridge's one release: every Start
  branch publishes the resulting direction before it answers, so the press after
  an ack reads the new direction (a close then a reopen, not two same-direction
  intents);
- `taken != seq` aborts the record (the counter goes to its odd state, so no
  consumer can copy or claim it), then re-reads the claim. No claim:
  `decision=replay reason=revoked` runs the original once;
- a record that has already moved cannot be aborted (`decision=superseded
  reason=record-moved`), so nothing is replayed.

A claim is released only by its ack, a changed peer epoch, or a peer that is
gone/stale (`decision=consumed reason=peer-gone`): those are the proofs that no
direction is still coming. A different `peerEpoch` at the watchdog means the
activation that was asked is gone: the replacement seeds `g_lastIntentSeq` past
the record and will never answer, so a taken press is consumed without its ack
(`reason=epoch-changed`), an untaken record is aborted and replayed under the
same rule, and the external barrier is released. The peer words are evidence in
the line; the abort and the claim are the proof.
A take is never replayed: one execution at most, one native execution when the
record is aborted before any claim, and a press lost rather than duplicated if
the controller dies after the take. An
external takeover is published at once and clears the queue and latch; it then
sets the same barrier for the presses that follow, because its record is the one
Start must reconcile before another intent may overwrite it. That barrier is a
state, not a timer: it is set whether or not the watchdog could be armed
(`own external barrier ... armed|unarmed`), and it is cleared only by its ack, by
a changed epoch (`released reason=epoch-changed`), or by a peer that is
gone/stale (`own external barrier ... released
reason=peer-gone`), never by a timeout and never by an arm failure. A live but
silent peer keeps it, and a following press waits; a reserve poll re-checks it
and runs any held press that has outlived its queue bound.

The shell's own click-away while the window is appearing (state 1/3) is
swallowed: it is the pointer-away check of a keyboard or own show, not a user
click. The same Hide(7) issued inside our own `ShowStartView` is swallowed while
the state is 1/3 (`reason=own-show`); otherwise the served open is closed by the
shell the instant it appears. A click-away delivered while our own executor
`Dismiss` is on the stack (`depth=1`, caller `clickaway`) is not passed to the
native Hide: it enters the same dispatcher as a Win press, so the opening/touch
suppressions apply and a click during the hide becomes the latch's reopen
(`own hide clickaway ... reason=executor`, then the usual `own toggle decide ...
kind=open` and `own press latched`).

The bridge's executor is a hidden message-only window on the **launcher
thread** — the thread that runs `OnHotkeyInvoke` and the shell's own
`ShowStartView`. The launcher is the `IXamlLauncher` subobject at
`s_pLauncher + 0x18` (`twinui.pcshell.dll + 0x948E80` holds the outer object);
`[+0x160]` is the `IXamlLauncherState`, ready when `> 0x10000` (the sentinel is
`0x100`). The readiness floor is checked at every read, including the resync after
a forwarded `Show`/`Dismiss`: the pre-call check cannot cover the pointer the
post-call read gets, and an unready value leaves the cached direction untouched
(`own state resync skipped reason=state-not-ready`). Show is confirmed by a 300 ms
`IsVisible` probe (honest `0` right after the call, the shell is cloaking). Dismiss
is **not** immediate in the same sense: `hr=0` says the primitive returned, and a
second 300 ms probe records whether the window actually went away
(`exec dismiss confirmed|unconfirmed|unknown seq=... visible=hidden|visible|unknown`).
A probe with no launcher or a failed call is `unknown`, not hidden; the timer's
elapsed time is printed, and the Start side treats `unconfirmed`/`unknown` as a
terminal repair. A command dispatched at or past 400 ms after its scheduled time
is answered `expired` and never calls the launcher, because the publisher has
already cleared the sequence and released the panel; the bridge and the Start side
share the `>= due + 400` comparison. A record whose publisher activation changed
is answered `superseded` immediately before the call (the epoch is captured when
the record is accepted), and a record that names a menu the section has already
left is answered `superseded menu=... now=...`; the Start side also cancels a
record the previous activation left queued when it attaches. No scheme recalls a call
already inside the shell; an entered dismiss is handled by the landing rules.

The open runs from the band contact, but only after travel: admission records the
contact and its down point, and the one `exec show` of that contact is requested
on the first positioned report whose signed inward travel reaches 24 DIP
(`reveal pull qualified ... grab=none` then `reveal show request ...`). A tap
anywhere in the band — twice the taskbar's thickness — requests nothing and stays
a native press; the shell's own edge gesture (from about 20 DIP) remains the
fallback, and the open it performs is adopted by the band down that authored it
(`g_contactOrigin.author`): adoption needs an author, so a finger that landed on
the desktop or inside the panel is never bound to a keyboard open, and a down on
the open panel is the dismiss and is never promoted. One attempt per contact: a
refused
request is not retried. The content settle starts at `RevealEntryProgress`, and
the reveal takeover chases the mapped finger at 2.2 DIP/ms, then follows
(`behind=0.000`).
A pull that takes a live retract over does not start at the entry: the transfer
takes the panel's position once and pins it, the follow binds to that same value
on the same report (`reveal takeover ... live=1`, no second `motion sample`),
keeps the finger-to-panel offset it took over and begins its chase at that
sample. No settle stands between the cancel and the follow's first move: the
press cancel (`intent open cancel from=...`) is the path that still settles the
panel back from its live position, because a press has no finger to bind to. That
pull is a grab, and while a close owns the panel a down is admitted only when it
lands in the band or on the surface the retract still shows — its mapped progress
at or below the panel's live one, because the panel's trail edge is then at or
beyond the point, **and** the point inside the panel rectangle's cross range
(`PanelCrossContains`: top..bottom on a horizontal axis, left..right on a
vertical one, from the same snapshot rect). A point in the area the retract has
left, or one perpendicular to and outside the panel, is refused
(`reveal admit ... reason=offsurface`) and muted for its contact. The grab spends
its one opportunity at its 24 DIP report: `CancelCloseForQualifiedPull` revokes
both schedules (the local slot and the armed `exec dismiss`), ends the close
record, arms the same
recovery ticket an explicit open request arms, and takes the retract over there
(`grab=taken`). A
refused transfer — the dismiss already
inside the shell, its published dismiss at or past its deadline (the bridge may
already be inside the call, `reason=dispatch`), no distance, nothing left to
cancel, or a down on the retract's surface with the retract already at or below
the entry or with the close already ended (`reason=no-close`) — consumes the
contact (`grab=refused reason=...`): it keeps no open
right until it lifts, every later report is inert, and its release leaves no
record. A down **in the band** at that same point is the same request a press
makes, with the finger still on it: the attempt shares the press's cancel body
(`grab=open`) — both schedules dropped, the close record ended, the open direction
published, the recovery ticket armed — but not its settle. The grant is spent, so
the same report starts the follow at `RevealEntryProgress` and chases the mapped
finger at 2.2 DIP/ms; a window the retract has already hidden gets the ordinary
show and the follow binds at that entry. A close that has already ended is the
same request with nothing left to cancel: a band down asks for the open the
ordinary way (`grab=open window=open|hidden entry=... reason=no-close`), and only
a down on the surface the retract had shown keeps no open right. A refused
transfer runs one transition:
the reveal session and its timing go, panel
ownership is republished, and the contact keeps the mute a down refused at
admission gets, so only a lift or a report that proves a new contact ends it. A
landing retires no grant: a contact still on its way to its own 24 DIP report
keeps its open right. A
close also consumes every contact that was already on the glass when it started.
While a touch show or a close is in flight the 18 ms idle release is held,
because the digitizer goes quiet during the uncloak and during the key/hide. A
contact the close consumed keeps its latch and its mute through that quiet:
silence never ends them. While a close is in flight no idle expiry runs at all,
and a consumed contact ends only on its lift report or on a report that proves a
new contact (another device, a different identifier, or a different asserted node
on a device that names no contacts;
`hid consumed ended ... reason=new-contact|device`). One with a Contact
Identifier also survives silence. A node index
can be handed to the next finger and the coordinate-only path has no name, so
those keep the idle fallbacks (the candidate's 120 ms watchdog, then the 18 ms
latch) once the close has ended.
A landing adopts no finger: after `win close observed` the controller issues no
touch show, and no hover, carried travel or release record turns a landed close
into a Show; a contact the close consumed is inert until it lifts (`win close
observed ... muted=1`). A grab whose grant is still unspent is not consumed: it
spends that grant after the landing and, for a band down, opens the menu as any
pull on a closed menu. The only open a landing still owes is the
explicit, independently
owed request — a press, or a band pull whose cancel armed the ticket — (`intent open
recovered ...`) with its epoch and close
identity, unchanged.
An intent close is
`TakeProgressNow()` + the mirrored settle + `exec dismiss` at +30 ms; a gesture
close asks for the same dismiss at its own settle's end. The bridge's timer runs
`Dismiss` on the launcher thread. The close record — one accepted close with its
serial, acceptance tick, the menu it was accepted against, a phase and the 600 ms
repair bound — keeps the visibility service from healing the retracted panel
before the dismiss lands, and it is the only answer to "is a close in flight?":
no reader asks a clock. The record is retired by its landing, a pull cancel, an
external takeover, a terminal repair, or the repair bound. The bound's ending is
a repair, not an expiry: it logs one `close ended ... reason=repair-deadline`
line, releases the published direction so the bridge's next toggle reads the
cloak, and leaves the dismiss command and the recovery ticket naming the hide
that may still land; the unhealed heal waits for that command to answer
(`CloseHideMayLand`), so a slow hide does not land on a panel that was just
restored under it.

The close is judged by behaviour: the shell's hidden event (`own state
event=hidden open=0`), the executor's `confirmed` (hidden) at its bounded 300 ms
confirmation — `unconfirmed`/`unknown` is a repair, not a hide — and the next open
working. The cloak read is tri-state: a failed read is unknown, never hidden. A
confirmed dismiss on a window the cloak already read hidden, with no transition
left to arrive, finishes through the same landing
(`win close observed reason=confirmed-without-transition`), so the controller
cannot keep publishing `closing` into the next press. When the confirmation and
the cloak disagree (or the cloak cannot answer), the command and its open
generation are kept for the transition, within a bounded wait; a later open
retires them — a queued command is cancelled at the bridge, and a confirmation
is dropped. A close intent's zero generation is stamped with the section's
current one at request time, and a zero stamp is valid only while no open has
been published, never as a wildcard for a newer menu. The transition is not
invented, only the close is completed.
The internal `0xA` value is **not** an acceptance criterion and is no longer
quoted.

Deleted in M3: the `VK_LWIN` tap, the close record and its publishes, the retract
request/answer and its cancel answer, the arbitration hold, the deferred
candidate **as an arbitration mechanism**, and the reveal claim. The one live
use the old candidate left is not arbitration: `BeginEdgeCandidate` (the down's
admission) calls `StartRevealFollow` with `granted=true` while the close record
owns the panel,
so a grab is tracked without writing `Progress`; its one transfer attempt is the
pull cancel, and a refused transfer consumes it — the landing no longer releases
it. The reveal flag that says so is `granted` (not `deferred`), and the claim
attempt is `claimAttempted`. The late-release record's `deferred` and `readyQpc`
fields are deleted: its window is measured from the release, and no landing
consumes a record. The panel-ownership clock (`g_suppressRestoreUntil`) is gone
with the close record: a phase, not an age, answers `CloseInFlight`, and the
settle owns the motion. The
`win open continued` ticket went with them: its only stamp sat behind the same
`CloseInFlight` early return that guards the follow, so it could never fire; the
press cancel, the press latch and the landing's press recovery are the reopen
paths that remain. M3.1 also removes the retired v2 close/retract slots from the
header.

## Channel

`mods/swipe_ipc_shared.h`: one session-local section (224 bytes) and four named
events (`State/Intent/IntentAck/Exec/ExecAck` — five objects), every record a
seqlock with its own counter and an `offsetof` assert, plus two single-word
claims (`controllerDirection`, `intentTaken`). The names carry the merged
mod's own prefix with the layout identity (`SwipeFor10Start_SingleMod2_`) and
there is **no product version
word**: the namespace is the ABI identity, so an older development image cannot
join, and a future layout changes the prefix. The records are the open
generation, the intent and its ack and the taken sequence (the ack is the
bridge's next-press barrier; the take is the timeout's proof), the executor
command and its ack, one
controller direction word plus the touch flag, and the liveness triple
`peerTick` (204) / `peerState` (208) / `peerEpoch` (212) / `intentTaken` (216). The debug command
channel — its record, mutex, event and parser — is **gone**.
`tools/ipc.ps1` is read-only: `-State` reads 224 bytes and prints the records and
the peer epoch.
`SwipeIpcOpen` may wait the whole 550 ms init budget; generation 0 and an unknown
`initState` are refused, never waited on. Each role opens the channel at its
`RoleInit`, never in a hook.

## Machine

Windows 11 10.0.26100.9949, ExplorerPatcher legacy Start
(`Start_ShowClassicMode=1`), 2880x1800 physical at 200 % (1440x900 DIP).
**The bar is not pinned to an edge.** The controller derives the axis, band, panel
rectangle, `Distance` and scale from the live geometry on every read, and the
tools read the same numbers from the mod's own lines (`reveal panelTrail`,
`gesture geometry`, the `dismiss` bar thickness), so the mod, the smoke and the
edge tracker run on whichever edge the user docks to. Measured on this machine,
each on the layout it names:

| layout | axis | work | band | panel rect | trail | Distance | measured |
|---|---|---|---|---|---|---|---|
| left, docked | 0 | `48,0,1440,900` | `0..96` | `60,151..1053,726` | 1053 | 1392 | 2026-11-21 |
| bottom, docked | 3 | `0,0,1440,852` | `804..900` | `236,265..1229,840` | 265 | 852 | 2026-11-20 |
| bottom, auto-hidden | 3 | `0,0,1440,900` | `804..900` | `236,279..1229,840` | 279 | 900 | 2026-11-22 |

The bottom auto-hidden entry is 0.310 at the work edge and 0.363 at the bar's
inner edge; an earlier auto-hidden run measured panel `236,167..1229,840` with
trail 167, and its numbers are historical. Top (`axis=1`) and right (`axis=2`)
are implemented and **unverified on this machine**: the code path is the same
`TrailEdge`/`AxisDirection` mapping, but no run has measured them.

## Rules

- One data source per claim: the cloak decides the shell state, the close record
  decides whether a close is in flight, the controller's
  published direction decides the toggle, the geometry snapshot decides the mapping.
- No period. The Start side's input thread arms one high-resolution one-shot for
  the soonest deadline it has and disarms it when that is served; the open/close
  state is a WinEvent, not a poll, with a bounded 100 ms fallback while a
  transition is outstanding. The only idle wake is the 100 ms `peerTick`
  heartbeat, and only while the peer is advertised. The Explorer worker blocks
  on its events once the launcher symbols resolved or were refused. A change that
  reintroduces a periodic tick or a state poll is a regression.
- A mapped channel is not a live peer. Interception needs the Start side's
  `peerState=alive` and a `peerTick` younger than 600 ms; otherwise the shell's
  own path runs. Availability is published only by the ready consumer and
  withdrawn before it is stopped.
- A live peer is not a usable executor. The gate also proves the recorded launcher
  thread, a window this activation created on it, a running worker, and the
  primitive the decision needs.
- One mechanism, one log shape. A decision line and an executor line per
  trigger; no per-trigger branch in either mod.
- A wrong-thread launcher call is not a clean error. Never call from a hook
  frame; post to the executor window, and never create that window off the
  recorded launcher thread.
- The shell owns every visual transition. The mods read and request, never
  write the launcher's state.
- The trace is a setting. Every decision line calls `Wh_Log`; the file is
  written only while `DiagnosticTrace` is on, one file per role, capped at
  20,000 lines. The smoke requires it and otherwise prints the one command that
  enables it.
- The image is not pinned. Unloading joins every owned thread, runs the UI
  teardown turn, drains the counted entries, destroys the launcher window and
  the raw-input window, and unregisters both fixed classes under `&__ImageBase`
  before the channel closes. A proof that cannot be
  obtained blocks the unload; there is no residual success.
- Docs and code move together. Update this header, the L2 map and the L3 header
  of the file you changed.

[PROTOCOL]: Update this header on change, then check AGENTS.md
