# M3.2 architecture — one controller, one executor

> L3-level design record. Mod **0.7.0** (one source, two process halves),
> channel **216 B, no version word**. The measured evidence is quoted with the build and the
> layout it came from; everything below was run on this machine. **Layouts:**
> *auto-hidden* = work `0,0,1440,900`, the bar off screen, band `804..900`, panel
> `236,265..1229,840`, trail 265, Distance 852 (current). *Docked* = work
> `0,0,1440,852`, tray `0,852..1440,900`, same panel, trail 265 (historical).

## What ships

One Windhawk mod, two process halves:

- `mods/swipe-for-10start.wh.cpp` (0.7.0) is installed once and loaded into both
  hosts. Its `start` namespace runs in `StartMenuExperienceHost.exe` and owns the
  touch gesture, the claim on `Progress`, the settle planner, the gates and the
  decision state. It consumes executor intents, publishes executor commands, and
  publishes its own liveness. Its `bridge` namespace runs in `explorer.exe` and
  turns the Win key, the Start button and the click-away into intents, and
  executes the Start side's `show`/`dismiss`/`cancel` commands on the launcher's
  own thread. It also swallows the shell's own duplicate show/close while the
  controller owns the panel. `Wh_ModInit` selects the half from the host
  executable name; the other half's hooks and threads never exist there.

The shell still performs every visual transition. The mod writes no launcher
state.

## The launcher read (corrected)

`twinui.pcshell.dll + 0x948E80` holds `XamlLauncher::s_pLauncher`, the **outer**
object. The shell passes the **`IXamlLauncher` subobject at `outer + 0x18`** to
`ShowStartView` / `Dismiss` / `IsVisible`; that subobject's `[+0x160]` is the
live `IXamlLauncherState`.

Measured in explorer (10.0.26100.9949):

| Read | Value |
|---|---|
| outer | `0x0A22F960`, vtable `twinui+0x77E1C0` |
| outer `[+0x160]` | `0` (never the state) |
| outer `+0x18` | `0x0A22F978`, vtable `twinui+0x77E078` |
| `[outer+0x18+0x160]` | state `0x0755A5C8`, vtable `twinui+0x77E3C8` |
| state vtable `+0x38` | `twinui+0x26F780` (`XamlLauncherState::ShowStartView`) |
| state vtable `+0x40` | `twinui+0x4991C0` (`DismissStartAndCortana`) |
| state vtable `+0x68` | `twinui+0x1B6050` (`IsVisible` query) |
| state `[+0x28]` | `5` while shown |

Readiness: `[+0x160] > 0x10000`. The constructor leaves the sentinel `0x100`;
a zero pointer means no state is attached. A command that finds either refuses
with `reason=not-ready`, and the Start side releases ownership so the shell's
own gesture can open the menu.

The earlier RE note said "read `s_pLauncher` and check `[launcher+0x160]`". The
live read shows the state is on the `+0x18` subobject; using the outer pointer
returns `0x80070490` (`ERROR_NOT_FOUND`), which is exactly the observed failed
first show. The executor uses the subobject for every call.

## The executor thread

The launcher's state machine is apartment-affine. Two threads in explorer are
not interchangeable:

- **launcher thread** — the thread that runs `XamlLauncher::OnHotkeyInvoke` and
  `CStartExperienceManager::Show`; measured `tid=7108` (and `11096` after an
  explorer restart). It owns the state machine.
- **taskbar thread** — the thread that owns `Shell_TrayWnd`; measured
  `tid=5924/6732/8980`. It is not the launcher thread.

A `ShowStartView` executed on the taskbar thread opened the shell's show and then
crashed `explorer.exe` in `twinui.pcshell.dll+0x1BB010` with `0xC0000409`
(fail-fast). A `Dismiss` on that thread happened to work, but the executor does
not rely on the asymmetry: every command runs on the launcher thread.

Shape: a hidden message-only window owned by the mod, created only from a known
launcher entry — the first of `OnHotkeyInvoke`, `Dismiss`, `ShowStartView`, the
manager's `Show` or the worker's targeted `WM_GETMESSAGE` adopts the thread (`exec
launcher thread`), and a window is never created from the `LoadLibraryExW` hook,
which runs on whichever thread loaded twinui. A worker waits on the channel event
and posts `WM_APP+0x5A`; the WndProc reads the command and calls the launcher with
an empty stack. The window class is one fixed name
(`WindhawkSwipeFor10Start.Executor`) registered under `&__ImageBase`;
`ERROR_CLASS_ALREADY_EXISTS` is a failure, never adopted, because a surviving
class from an earlier image points at an unmapped procedure.

Shutdown is an order, not a set of flags. BeforeUninit clears readiness and
signals the worker's stop; Uninit joins the worker **INFINITE**, then destroys the
one published window on its owner thread — directly when it already runs there,
otherwise with the private `kExecTeardownMessage`, whose `WM_NCDESTROY` signals
`g_execWindowGone`. The class is unregistered under the same `&__ImageBase`
instance only after the window is gone, and the worker/stop/window-gone handles
and the channel close last. The old creator reservation, admission flag, creator
drain, reaper and `CloseCallLease` count are gone; the join and the class
ownership are the barriers. A window or worker that does not go away blocks the
unload rather than returning success into an image it still points at. The
procedure also refuses every ordinary dispatch for an HWND other than the
published current one, so a stale message cannot act on this activation.

`IsVisible` is the confirmation. Immediately after `ShowStartView` it is
honestly `0` (the shell is still cloaking); a 300 ms timer on the same thread
reads it again and logs `exec visible ... visible=hidden|visible|unknown`.
`Dismiss` returns before
the shell's hide runs, so `hr=0` plus the visibility at the call is not yet a
hide: a
second 300 ms probe keeps the bounded obligation and logs
`exec dismiss confirmed seq=... visible=hidden` or
`exec dismiss unconfirmed seq=... visible=visible at_call=1 after=<real ms>`; a
probe that cannot answer (no launcher, a failed call, an unarmed timer) is
`unknown`. The Start side treats `unconfirmed` and `unknown` as a terminal
repair — release the direction, drop the ticket, read the cloak on the next poll
— because a dismiss that is not observed to hide the window is not a hide.

A command is not run late. The controller gives each one 400 ms from its
scheduled time (the dismiss's `executeAt`, or the publication tick of a show);
a dispatch that starts at or after `due + 400` is answered `expired` and the
primitive is not
called, because the publisher has already cleared the sequence and released the
panel. The publisher uses the same `>=` comparison. That is not a lease on the
call: a primitive can enter before the deadline and return after the publisher
timed out, and nothing recalls it. What the bridge still checks is the publisher's
activation: the epoch is captured when the record is accepted and compared
immediately before the call, a mismatch answering `superseded`; the Start side
also publishes `cancel(target=0)` at attach for a record the previous activation
left queued. A record that names a menu (a dismiss carries its open generation)
is refused when the section has already moved to a later open, which is the case
a restart leaves behind when the old record is accepted by the new activation.
The publisher does the same from its side: an unanswered dismiss is
cancelled by sequence, and an unanswered show — which has no armed command to
revoke — is superseded by a cancel record.

## One decision, one executor

Triggers and their decision lines:

| Trigger | Hook | Decision |
|---|---|---|
| Windows key | `XamlLauncher::OnHotkeyInvoke` (`+0x14195C`) | `own toggle decide trigger=win … kind=open|close` |
| Start button open | `ShowStartView` method 11 | `own toggle decide trigger=startbutton …` |
| Start button close | `Dismiss` method 6 | `own toggle decide trigger=startbutton …` |
| click-away | `HideStartAndCortana` method 7 | `intent close trigger=clickaway` |
| touch | the Start side's band admission | `reveal show request` |

`IntentShouldClose` is the only toggle rule: the controller's one direction word
decides while it owns the panel (`2` is a close in flight, so the press is an
open; `1` is the open it owns, so the press is a close), and the shell's cloak
and internal state are the resync only for a panel the controller does not own.
The shell cannot be the direction: a gesture close publishes `2` in
`TakeCloseOwnership`, before its first frame, because the window still
reports state 5 and the cloak still reports open for the whole retract, until
the executor's `Dismiss` runs. Reading the shell there made a press during the
retract a second close, and the owner's press did nothing. No trigger has its
own branch.

**The interception precondition is the peer, not the section.** The bridge
creates the channel, so a mapped section proves only that the bridge runs: with
the Start mod disabled the section is still valid and the old gate decided for a
controller that was not listening. Every consuming hook (`OnHotkeyInvoke`,
`OnStartButtonPressed`, `Dismiss`, `ShowStartView`, and the close intent at
`HideStartAndCortana`) reads the Start side's published liveness first:
`peerState == alive` and the `peerTick` younger than 600 ms, with the comparison
clock taken after the tick. Otherwise it logs one
`own pass trigger=... reason=no-peer` line (the hide logs
`exec pass hide method=... reason=no-peer`) and calls the original, so the
shell's own toggle serves the press exactly once.

Availability belongs to the activation that serves. The Start side claims a
`peerEpoch` at attach, withdraws `gone` and resets the shared direction and touch
there, and publishes `alive` from the input thread once that thread's window, its
message loop and the intent event exist and it is about to wait on intents. It
withdraws again before that thread is stopped and on every exit from it. The
publication and the terminal withdrawal share one critical section: a
flag-check-then-store let the startup write `alive` after the unload had written
`gone`, and the bridge then served a controller whose thread was leaving. The
raw-input registrations are not part of readiness — this is **intent-consumer
readiness**; `RegisterHidUsage` logs a refusal and the touch half degrades, while
the intent half the interception depends on still runs. The `ipc peer alive` line
prints `touch=`/`pen=` so the difference is visible. A press during a start or a
stop is served natively by the shell; that is the intended trade, because an
`alive` written before the consumer runs would swallow a press for a service that
does not exist. A press the bridge has already swallowed is a different case:
it is ours, and the recovery paths below decide it.

The close intent's capability is checked separately (`HideCanServeClose`): peer,
the caller wrappers, a started worker, the **recorded** launcher thread (a Hide
never adopts one), a window on that thread, and a callable `Dismiss`. One missing
piece and the shell's close runs, because converting a native close into a
command nothing can execute leaves the window shown over the controller's
retracted panel.

`peerState` is still a detection budget, not a delivery guarantee. A decision
taken inside the 600 ms window can be published after the peer died, and a press
latched inside it can be served after it. The residual windows are the
gate-to-publication gap of one intent (sub-millisecond, not atomic against a kill)
and the latch's whole wait; the latch re-reads the peer and the epoch at its serve
and is dropped when either moved.

**A press is usually recovered — cancel, latch, or recover.** A close still
settling has either a local dismiss slot not yet requested (a gesture close) or an
executor dismiss armed for a future deadline (an intent close). Only the first
shape is cancelled: the Start side clears the suppression, drops the local slot
and, when a command is already pending, publishes `exec cancel`, then takes the
panel where the retract left it and settles it back to 1
(`intent open cancel from=...`). A press the bridge sees while the shell is
already hiding (`InternalState` 9/11) or while its executor dismiss is armed is
not published at all: it is latched in one slot (tick, trigger, method, flags,
and the epoch the admission was decided under, not a fresh read).
The hidden event arms a 50 ms timer and
the timer serves the same open intent after the event's stack has unwound: a show
at the hidden event's own tick was swallowed (same tick, `exec visible=0`, state
stuck at 3, no Shown), while a show 16 ms after it opened normally. The arm is
posted to the executor window when the event runs on another thread, so the slot
and its timer stay owned by the launcher thread. A second
press replaces the slot (`own press latch replaced ...`); the serve logs `own
press served wait=...` (current auto-hidden medians: 219 ms Win close, 210 ms
Start-button, 234 ms click-away; the gesture latch is rare and measured 94 ms).
The 400 ms timer is only the fallback for a landing the event missed. Any newer
press discards the slot through the shared dispatcher
(`own press latch dropped reason=superseded`), a refused or expired dismiss
invalidates it (`reason=dismiss-refused|dismiss-expired`), the serve re-reads the
peer and the epoch (`reason=peer-gone|peer-changed`), and a latch whose first
timer could not be armed is dropped instead of being left with no guaranteed
callback. A dismiss
that had already entered the shell's `Dismiss` call when the press landed hides
the window anyway; the landing reissues the open once
(`intent open recovered entry=...`). The recovery ticket exists **only** for a
command that was published: still queued or armed when the cancel ran, or already
executed but not yet confirmed. A local-only cancel arms no ticket,
because no dismiss is inside the shell and a later hide of the same menu is not
that press's close. It is bound to that menu's open tick, to the sequence it
revoked, and to a deadline (the revoked command's own deadline plus the ack budget
and the confirmation probe); past it, the command cannot still be inside the
shell, so a later landing is not served. A refused, expired or superseded answer
for **that sequence** retires it before the pending-slot gate, which the open
that cancelled the close has already cleared; the landing, a new close and an
external takeover retire it explicitly. A still-open poll does not clear it: an
entered `Dismiss` may land at any point while the window is up. Before a landing
may recover it reconciles an already-published `external` intent
(`intent reconcile ... (before landing recovery)`) and applies the record it
examined, not a second read, because the idle timer is
waited on before the intent event and processing the takeover afterwards would
not revoke a Show the landing had already issued.
The
click-away that the shell issues while the window is appearing (state 1/3), and
the Hide(7) it issues inside our own `ShowStartView` in state 1/3, are the
pointer-away check, not a user click, and are swallowed
(`reason=opening`/`reason=own-show`). Without that, the served open came back
and was hidden in the same millisecond.

**Executed is not hidden.** A successful `Dismiss` answers `executed`; the hide is
a separate observation 300 ms later, answered `confirmed`, `unconfirmed` (still
visible) or `unknown` (no launcher, no state, a failed probe, or no timer). The
Start side keeps the command identity in `g_pendingDismissConfirm` after the
executed ack clears the pending slot, and an unconfirmed or unknown result is a
terminal repair: ownership is released, the ticket dies by its own sequence, and
the next poll reads the cloak. A superseded confirmation is reported separately
and needs no repair.

"Never lost" is the intent of the mechanism, not a theorem. The cases it does not
cover are the ones where the peer itself is gone: a press the bridge never
swallowed that arrives after the 600 ms gate closed is passed to the shell; a
press it did swallow and a latch armed before a kill and served after it are
dropped. Those are detection windows, and the trade is explicit:
the shell's own toggle is always correct, so a lost interception costs one press,
never a wedged menu.

The Start side publishes the executor command and then answers the intent
(`intent ack`); the ack reports the decision and is not a barrier the bridge
waits for before the command:

- open: `exec show` immediately; the shell's own show is swallowed while the
  controller owns the panel and the same opening touch is protected by
  `controllerTouch`;
- close: `TakeCloseOwnership` + the mirrored settle from `TakeProgressNow()`,
  then `exec dismiss` at `settle + 30 ms`;
- a new intent that takes over the closing panel publishes `exec cancel` with
  the dismiss sequence; the bridge kills its timer and the menu stays open;
- external takeovers (Win+Tab, lock, display change, a modified forwarded Win
  press) publish `intent=external`: the Start side revokes both close schedules
  and the recovery ticket, clears the suppression, drops the touch state and
  resyncs from the cloak.

Fallbacks, each logged once:

- no live Start peer, no channel, no executor worker, a call on a thread that is
  not the recorded launcher thread, a missing window or a missing primitive: the
  trigger passes through untouched
  (`own pass trigger=... reason=no-peer|no-executor|wrong-thread|no-window`, and
  `exec pass hide ... reason=no-primitive`), the shell's own path serves it;
- launcher not attached: `exec show`/`exec dismiss` is refused, one repair
  releases the direction entry, `controllerTouch` and the pending open entry, so
  the controller state resyncs from the cloak, and the shell's own gesture opens
  the menu;
- a command that cannot be published at all (no section, no Exec event): the
  request reports failure, the open/close is not claimed, the intent ack says
  refused, and there is no sequence for a timeout to repair;
- no executor ack within 400 ms (a show from its request, a dismiss from the
  deadline it was scheduled for): `exec ack timeout seq=… kind=show|dismiss
  (resync from cloak)`, the same repair, and an `exec cancel` that names the
  sequence (show: target 0) so the executor cannot run the stale record;
- a dispatch that starts more than 400 ms after the record's scheduled time:
  `exec show|dismiss seq=... expired`, answered `kSwipeAckExpired` without a
  launcher call.

## The open and the close (auto-hidden; docked numbers named)

Open (touch, docked, warm):

```
contact / hid usages          172817328
exec show request            172817328   (+0)
exec show ... hr=0 visible=0 172817328   (+0; the bridge's 15 ms call is below)
own state event=shown        172817406   (+78 ms)
reveal takeover              172817437   (+109 ms)
exec visible ... visible=1   172817640   (+312 ms)
```

The Win open, current auto-hidden layout (three captures at `x=1200` in a
16×1600 strip, `top` counts the first panel row): key → `exec show` **+31–32 ms**
→ `own state event=shown` **+46–141 ms** → first panel pixel **+82–187 ms**
(median +115) → `exec visible ... visible=1` **+343–344 ms**. The docked capture
was `key=172830139`, `exec show` +79 ms, `shown` +142 ms, first panel pixel
**+190 ms**, `visible=1` +392 ms. The shell's cloak is the dominant term; the
executor removes the shell's 60–250 ms gesture wait. The earlier auto-hidden
variant measured +257 ms for the same pixel.

The touch show is requested once per open (`g_intentOpenActive` guards a second
report while the window is still closed). The entry is
`RevealEntryProgress`; auto-hidden it reads **0.255** (warm; the panel trail is
265, the Distance 852); the docked layout read 0.348. A cold first open still
reads 0.000. The first visible frame is the entry, not 0.

The reveal takeover starts at the entry and chases the mapped finger at
`kSettleSpeedDipPerMs` (2.2 DIP/ms), then follows. Current auto-hidden, a held
contact (`follow_hold`, 85 reports): `reveal takeover from=0.255 offset=0.338`,
`open follow travel=488 max=488 progress=0.90 behind=0.000 commit=1`; the panel
edge is 350.2 DIP at the held 350 DIP finger, **0.4 px** away. The 300 DIP hold
gives `progress=0.96` and a 299.1 DIP edge, **1.8 px** away; the docked 300 DIP
hold measured 2 px. The `offset` is the inherited gap at the takeover; the chase
closes it at 2.2 DIP/ms, and `behind=0.000` is the glued state.

A digitizer that goes quiet while the shell uncloaks, or while a close is in
flight, is not a lift: while the touch show or the close suppression is active
the 18 ms idle release is held, so the candidate the open/landing is about to
adopt survives. A real lift is a report and still ends the contact.

Close: a Win, Start-button or click-away intent calls `TakeCloseOwnership`
(sets `g_suppressRestoreUntil`, publishes the closing direction before the
settle's first frame, mutes the contact, clears the reveal) and runs
`BeginMirrorCloseSettle` from `TakeProgressNow()`: the open curve played
backwards, full travel (387 ms at the current 852 DIP; 409 ms at the docked 900).
The dismiss is scheduled at the end
plus 30 ms. The bridge's timer runs `Dismiss`; the shell's hidden event follows
and the Start side logs `win close observed`, clears the direction and
`controllerTouch`, and the next open is ready. The close is
judged by that hidden event and the executor's `confirmed` (hidden) at its 300 ms
probe, not by an internal `0xA` value;
that value is not a criterion. `unconfirmed`/`unknown` is a repair, not a hide. A
gesture close is the same path with its own
release settle (160–520 ms) and no 30 ms lead: its dismiss is requested at the
settle's end.

A Win press during a close is an open because the controller's phase says so,
not because the shell's state does. The open intent takes the live panel, drops
the local dismiss slot, cancels a pending `exec dismiss` and settles back to 1
(`intent open cancel from=...`); the window never hides. The dismiss can already
be inside the shell's `Dismiss` call when that press lands
(a 1–15 ms window); the close then lands and the next poll reissues the open
(`intent open recovered entry=...`), so that particular press is recovered — it is
the one race this ticket exists for. A press during a
shell hide (state 9/11) or with an armed executor dismiss is latched and served
at the landing instead. Neither path is a guarantee: the recovery ticket only
serves a landing for the menu it was taken for, the latch is bounded and
revalidated, and a press the 600 ms gate no longer covers is the shell's — unless
the bridge already swallowed it, in which case it is dropped.

A pull that is still down when the close lands (or that lifted inside the
300 ms late window) is the pull that takes the menu back: after
`win close observed` the controller issues one fresh show
(`reveal reopen after close pull=1 late=0 entry=...`), and the open adopts the
live contact or consumes the release record. Measured on the docked layout: the
Win close during a held pull at `y=350 DIP`; the landing reopened, `reveal
takeover from=0.348 offset=0.558`, the follow continued with 221 reports and
ended `progress=0.91 behind=0.000 commit=1`, menu open. The auto-hidden entry is
0.255.

The suppression is what keeps the visibility poll from "healing" the panel that
the close has retracted: without it the poll saw a shown window with the panel
at 0 and started an open settle between the settle end and the dismiss
(`win open unhealed`), a flash. `TakeCloseOwnership` now sets it for the intent
close too.

Measured interruption (current auto-hidden unless named):

- open settle interrupted: the Win close 172 ms into the open started at
  `from=0.542` (the live position), not 1.0;
- drag interrupted: a Win press while a touch was held at `y=399.5` ran the
  close from `progress=1.000`, hid at +437 ms, and the touch's later lift was
  ignored (docked);
- press during a close, 40 randomised trials per kind, press offset random
  50–500 ms: **0 lost**. Win close: 29 latched (waits 94–359 ms, median 219), 11
  direct, 0 cancelled. Start-button close: 30 latched (median 210), 10 direct.
  Click-away close: 21 latched (median 234), 19 direct. Gesture close: 7
  cancelled (`intent open cancel`, the press landed in the retract), 33 direct,
  0 latched, 0 lost; two of the seven were first marked invalid because the
  script's t0 lagged the decoded release by 266 ms (the injected contact ended on
  idle), and the log shows one cancelled and one direct. A separate 20-trial
  gesture run aimed at the settle's end gave 1 latched (94 ms), 1 cancelled, 17
  direct, 1 t0-lag trial, 0 lost.
- the settle-end → dismiss window: 18 of the randomised presses landed after the
  close settle and before the dismiss had completed (Win 8, Start-button 3,
  click-away 6, gesture 1); 0 lost. The 3–4 ms cases were latched and served
  94–141 ms after the press; the later ones opened directly once the hide had
  landed.
- `intent open recovered` (a dismiss that already entered the shell when the
  press arrived) is the safety net for the 1–15 ms race; it did not fire in the
  runs above.

## Mixed cycles (docked)

Ten cycles across the four triggers (Win, Start button, click-away, touch) on
the docked layout, after the latch and the opening guards: every cycle ended
with the menu closed (`START ... open=False`), the bridge log for the series
contains **0** `own state resync`, **0** refused commands, **0** ack timeouts
and **0** dropped commands, and every close logged `own state event=hidden
open=0` and the executor's `visible=0`. The Start-button cycles worked because
the docked bar is always visible (the auto-hidden run could not re-trigger
them). The decision/executor lines for the four triggers are in the session
report.

## Deleted in M3

The M2 mechanisms that no longer exist in the shipping path:

- the `VK_LWIN` close tap and its 60 ms hold;
- the close record (`closeSeq/closeTick/closeKind/closeDelayMs/closeTapped/
  closePull`) and its three publishes;
- the bridge's retract request/answer (`WaitForRetractLead`, the plan wait, the
  three answers) and the Start side's retract handler;
- the arbitration hold at `HideStartAndCortana` and the deferred candidate that
  existed to survive it (the touch show's own flag replaces only the idle
  suppression it needed);
- `CloseInFlight` as the owner of the panel; the predicate now answers only
  whether the suppression window is open, and the settle owns the motion;
- the retract request's cancel answer;
- the reveal claim (`revealOpen`/`revealTick`) and the two-curve style switch;
- the `win open continued` ticket: its only stamp sat behind the same
  `CloseInFlight` early return that guards the follow, so it could never fire;
  the pull-reopen and the press latch are the reopen paths that remain.

0.7.0 also deleted the explorer gesture publisher: the four observer hooks
(`InputGestureOperationStarted`/`Ended`, `ReportEdgeGesture`,
`SetAnimationData`), the `CStartExperienceManager::Hide` observation hook, and
the process-lifetime gesture mapping with its seqlock writer and the
inactive-at-unload publication. The Start half's Raw Input is the product's
contact source; that mapping only fed the Phase 0 probes.

The same release moved the Start root hook from a fixed address to a public
symbol. Deleted with the address: the `StartUI_.dll` file-version compare, the
executable-range check, both prologue probes and the module poll thread that
searched for the DLL for 20 seconds. Deleted with the animation instrumentation:
the animation-request hook, its three argument getters and the debug switch that
suppressed the shell's open storyboard. `PrepareRootElement` is resolved with one
`HookSymbols` call, and that call runs on the mod's input thread: the first
symbol load starts Windhawk's PDB fetch, and blocking the shell's loader thread
makes the menu host look hung to the watchdog, which restarts it and restarts the
fetch. The `LoadLibraryExW` notification only records the returned module; a
false result while the fetch runs is pending with a 120 s deadline, and only the
deadline logs `unresolved`, installs no root hook and withholds the controller
service. The shell keeps pumping for the whole fetch.

The explorer half moved the same way. The launcher entry points, the
`s_pLauncher` slot and `IsVisible` are resolved by one `HookSymbols` call on
`twinui.pcshell.dll`; the fixed VAs, the `twinui` file-version/timestamp gate,
the executable-range check and the prologue/body probes are deleted. The
`LoadLibraryExW` notification only records the returned image; the executor
worker runs the resolution at most once per 100 ms with a 240 s pending
deadline. A still-blocked PDB fetch leaves the mod passive — every press runs
the shell's own path — and never blocks `explorer.exe`, because the fetch runs
on the mod's worker rather than on a thread the shell owns. Every resolved
address is logged against the reference RVA; the set line
`exec launcher symbols resolved ... count=13 matches=13 match=1` is the evidence,
and one missing symbol refuses the whole set instead of installing half a
toggle.

The close caller is a thread-local category set around a wrapper of each proven
caller, not a return-address match: `DismissStartAndCortana` is ordinary,
`DismissSynchronouslyWithoutTransition` is lock/display, and
`ImmersiveMonitorHelper::ActiveZBandChangedForMonitor` is the click-away (its
synchronous `mov edx,7; call [vtable+0x48]` is verified in the binary). The
unnamed `0x4916D0` WRL delegate invoker cannot be reached by symbol and is not
classified; a Hide it reaches outside those scopes stays `unknown` and runs
natively. The measured desktop click-away is inside the ActiveZBand scope, so
it keeps the animated close.

The controller advertises that service only once the root is attached
(`PublishPeerReady` requires `g_rendererReady`, not just the hook). A session
that missed `PrepareRootElement` during the cold fetch has no geometry, so a
press admitted there would be swallowed for a controller that can only pass it
through (`win open restore (no geometry)` was measured); with the gate, the peer
stays unpublished and every press is native until the next host session attaches
a root.

The merged channel is **216 bytes** with one section and four events. The names
use one merged-mod prefix (`Local\SwipeFor10Start_SingleMod_*`) and there is **no
product version word**: the ABI namespace is the identity, so an old development
image cannot join and a future layout changes the prefix. The records are the open
generation, the intent and its ack, the executor command and its ack, one
controller direction word plus the touch flag, and the liveness triple
`peerTick`/`peerState`/`peerEpoch`. The debug command record (`cmdText`, `Cmd`,
`CmdWrite`, its parser) is gone; `tools/ipc.ps1` is read-only. The executor
validates every command before the launcher call (show 1/11/16 with flags under
`0x8|0x4`, dismiss 1/6 with no flags, cancel 0) and refuses anything else instead
of clamping it. A click-away intent keeps Hide method 7 as evidence; the Start
side maps it to executor method 6.

**A note on the retired versions.** v7 added `peerEpoch` at 740 inside v6's
744-byte section; v6 added the liveness pair at 732/736; v5 removed the M2 slots
and replaced the owning/phase pair with one direction word. These are history,
not the current layout; the asserts in `swipe_ipc_shared.h` are the contract.

## The accepted cap, restated

The shell's first visible frame is 245–268 ms after a band show in the worst
case. The current auto-hidden Win open showed its first panel pixel at
**+82–187 ms** from the key (median +115 over three runs; `exec show` +31–32 ms,
`shown` +46–141 ms), and the docked capture at **+190 ms** (79 ms to
`exec show`, 111 ms to the first pixel). The shell owns
that window; the early-uncloak experiment found DWM attribute 13 returns
`E_ACCESSDENIED` without the shell's IAM ticket, and the owning process cannot
set it before the shell's own uncloak. No early visibility is attempted. The M3
gain is the decision and command latency (31 ms) and the removal of the shell's
60–250 ms gesture wait, not the cloak.
