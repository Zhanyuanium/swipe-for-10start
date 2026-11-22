# M3.2 architecture — one controller, one executor

> L3-level design record. Mod **0.8.0** (one source, two process halves),
> channel **224 B, no product version word** (`SingleMod2` namespace). The measured evidence is quoted with the build and the
> layout it came from; everything below was run on this machine. **Layouts:**
> *left docked* (live, measured 2026-11-21) = `axis=0`, work `48,0,1440,900`,
> the bar at `0,0..48,900` (96 physical wide), band `0..96`, panel
> `60,151..1053,726`, trail 1053, Distance 1392. *Bottom auto-hidden*
> (historical) = work `0,0,1440,900`, the bar off screen, band `804..900`, panel
> `236,265..1229,840`, trail 265, Distance 852. *Bottom docked* (historical) =
> work `0,0,1440,852`, tray `0,852..1440,900`, same panel, trail 265.

## What ships

One Windhawk mod, two process halves:

- `mods/swipe-for-10start.wh.cpp` (0.8.0) is installed once and loaded into both
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

## Logging

Every decision line goes through one `TraceLog` wrapper. It always calls
`Wh_Log`, which Windhawk stores only while the mod's own debug logging is
enabled, and it appends the same line to the role's file only while the
`DiagnosticTrace` setting is on: `<mod storage>\renderer.log` in the Start host,
`<mod storage>\bridge.log` in Explorer. The file line keeps the
`t=<GetTickCount64>` prefix the regression tools read. The file is capped at
`kTraceMaxLines` (20,000 lines): the count is seeded from the existing file and
the next write truncates it, and the counter is reset only when the truncation
succeeded. The cut uses its own `FILE_WRITE_DATA` handle and the line is then
written through a `FILE_APPEND_DATA` handle: a handle carrying both bits does not
append (measured: it writes at offset 0 and overwrites the trace from the start).
The CRLF is part of the same buffer, so one line is one `WriteFile` and an append
cannot tear against the other activation's lines. A failed truncation stops the
file half for the session and logs why
once; `Wh_Log` keeps the line. Formatting is truncating
(`_vsnwprintf_s`/`_snwprintf_s` with `_TRUNCATE`), so a long setting or object
name can never reach the CRT invalid-parameter handler. The setting is read at
init and on every settings change; with it off no file is opened, and it is the
only debug switch.

## The launcher read (corrected)

`twinui.pcshell.dll + 0x948E80` holds `XamlLauncher::s_pLauncher`, the **outer**
object. The shell passes the **`IXamlLauncher` subobject at `outer + 0x18`** to
`ShowStartView` / `Dismiss` / `IsVisible`; that subobject's `[+0x160]` is the
live `IXamlLauncherState`.

The offsets are reference-build facts, and a name alone does not prove them. The
capability is enabled only when the loaded image is the identity those offsets
were read from: fixed file version **10.0.26100.9974** and all 13 named symbols
at the reference RVAs. `ModuleFixedFileVersion` reads the fixed version through
`version.dll` (resolved at runtime), and `InstallExplorerHooks` compares every
resolved RVA. On any other image the capability is off: the decision hooks pass
their original, `LauncherNow` returns null, the executor refuses its command,
and the shell serves the press. One line states it:
`exec launcher layout unverified version=... matches=... native=1 reason=version|rva`;
the verified image logs `exec launcher layout verified ... native=0`.

Measured in explorer (10.0.26100.9949); the same RVAs still match the
10.0.26100.9974 image (13/13), which is the identity the gate requires:

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
repair — release the direction, drop the ticket, read the cloak on the next
transition — because a dismiss that is not observed to hide the window is not a
hide. The `hr=` line is written after the call returned, so
`exec dismiss enter seq=... gen=... epoch=... at=...` carries the QPC timestamp
taken immediately before the launcher was entered: that is the point an
interruption on the Start side is dated against.

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
| click-away | `HideStartAndCortana` method 7 | `own toggle decide trigger=clickaway … kind=open|close` |
| touch | the Start side's qualified band pull (24 DIP) | `reveal show request` |

`IntentShouldClose` is the only toggle rule: the controller's one direction word
decides while it owns the panel (`2` is a close in flight, so the press is an
open; `1` is the open it owns, so the press is a close), and the shell's cloak
and internal state are the resync only for a panel the controller does not own.
The shell cannot be the direction: a gesture close publishes `2` in
`TakeCloseOwnership`, before its first frame, because the window still
reports state 5 and the cloak still reports open for the whole retract, until
the executor's `Dismiss` runs. Reading the shell there made a press during the
retract a second close, and the owner's press did nothing. The click-away runs
the same rule and only guards the case the shell cannot produce — a method-7
callback with no open panel and no close to reverse — by passing it to the shell
(`own press pass trigger=clickaway entry=hide reason=no-panel`), with the original
running under the executor guard for an immediate and a queued click alike. No
trigger has its own branch.

**One intent at a time.** The channel has one intent record and one event, so a
second admitted press that decided before the Start side consumed the first would
overwrite the record and read the same direction: both presses would mean the same
toggle and one would be lost. Admitted presses wait in a four-entry FIFO on the
launcher thread. The first is published; the next is published only after the
`intent ack` for the outstanding sequence (the executor worker waits on the ack
event and posts it to the executor window) or its 1000 ms bound expires. The
direction is sampled when the press is published, never at admission, so the
press after a close reads `2` and reopens. A press the queue cannot serve — full,
an ack that never arrives, or a wait past 1500 ms — runs the shell's own original
primitive for that press under the executor guard (`own press fallback …`), so
the shell is never left without its toggle. An `external` intent clears the queue
and the latch.

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
prints `touch=`/`pen=` so the difference is visible. Readiness is continuing: the
input thread also withdraws (`ipc peer gone reason=renderer-lost terminal=0`)
when the root is torn down for replacement or a failed scalar write invalidates
it, and republishes (`ipc peer alive reason=renderer`) only when a usable
replacement is attached; the UI thread signals a renderer event, so a press in
that interval runs natively exactly once. The terminal unload withdrawal still
wins the race. A press during a start or a
stop is served natively by the shell; that is the intended trade, because an
`alive` written before the consumer runs would swallow a press for a service that
does not exist. A press the bridge has already swallowed is a different case:
it is ours, and the recovery paths below decide it.

That thread has no period. It waits on the stop event, the intent and
executor-ack events, the renderer event, the cloak event, and one high-resolution
one-shot armed for the soonest deadline only; `docs/input.md` lists the deadlines
and their clocks. A release that cannot act (a touch show in flight, a close in
flight with a contact on the panel) arms nothing, and with no deadline the timer
is disarmed. The open/closed state is `EVENT_OBJECT_CLOAKED`/`UNCLOAKED` on the
CoreWindow, delivered to that thread; the callback only sets an event, and the
service behind it reads the cloaked attribute once. A bounded 100 ms fallback
poll runs while a requested or just-observed transition is outstanding, so a
missed event costs latency, not state. The one idle wake is the 100 ms
`peerTick` heartbeat, and only while the peer is advertised.

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
executor dismiss armed for a future deadline (an intent close). Both are
cancelled: the Start side ends the close record, drops the local slot and,
when a command is pending, publishes `exec cancel`, then takes the
panel where the retract left it and settles it back to 1
(`intent open cancel from=...`). Only a press the bridge sees while the shell is
already hiding (`InternalState` 9/11), or while our own `Dismiss` call is live on
the launcher thread (`t_execCall`), is
not published at all: it is latched in a counted slot (the first press's tick,
the whole entry per owed press, and the epoch the admission was decided under,
not a fresh read). An armed
executor dismiss is revocable, so it is not a latch condition: the press
publishes the open and the Start side cancels that command.
The hidden event arms a 50 ms timer and
the timer serves the same open intent after the event's stack has unwound: a show
at the hidden event's own tick was swallowed (same tick, `exec visible=0`, state
stuck at 3, no Shown), while a show 16 ms after it opened normally. The arm is
posted to the executor window when the event runs on another thread, so the slot
and its timer stay owned by the launcher thread. A second
press is appended (`own press latch owed count=2`), not replacing the first; the
serve logs `own press served ... owed=2`, publishes the first open, then queues
the remainder so the ack barrier makes the next press the opposite direction.
Every published open arms the open-generation gate against the generation read
just before it; the next close is held (`own press held ... reason=open-gen`, a
bounded 20 ms one-shot) until the Start side publishes a different open
generation, or for at most 900 ms, or until `SetTimer` fails — then it is served
at once, so the queue never parks. A close sent before that record exists would
name the previous menu and be ended by the fresh-open rule; the gate replaces the
cloak watch and also covers an ordinary open from closed (bottom auto-hidden
medians: 219 ms Win close, 210 ms
Start-button, 234 ms click-away; the gesture latch is rare and measured 94 ms).
The 400 ms timer is only the fallback for a landing the event missed. A latch
past its bound is expired with all its owed presses
(`own press latch dropped reason=expired`), a refused or expired dismiss
invalidates it (`reason=dismiss-refused|dismiss-expired`), and a latch filled to
its four entries drops the arriving press (`reason=latch-full`); no press is
reordered to keep it. At its serve the latch re-reads the peer and the epoch
(`reason=peer-gone|peer-changed`), and a latch whose first timer could not be
armed is dropped instead of being left with no guaranteed callback. A dismiss
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
external takeover retire it explicitly. A still-open state read does not clear
it: an
entered `Dismiss` may land at any point while the window is up. Before a landing
may recover it reconciles an already-published `external` intent
(`intent reconcile ... (before landing recovery)`) and applies the record it
examined, not a second read: the cloak event and the intent event are separate
wakes, so processing the takeover after the landing would not revoke a Show the
landing had already issued.
The
click-away that the shell issues while the window is appearing (state 1/3) is
swallowed only from the `ActiveZBand` caller (`caller=clickaway`,
`reason=opening`); the Hide(7) inside our own `ShowStartView` is swallowed only
on the executor's own call stack with an unattributed caller
(`caller=unknown state=1|3 depth>0`, `reason=own-show`). A lock, an unattributed
Hide outside that stack, or any other caller takes the external path first and
passes. Each line prints caller, method, internal state and executor depth
together. Without the guards, the served open came back
and was hidden in the same millisecond.

**Executed is not hidden.** A successful `Dismiss` answers `executed`; the hide is
a separate observation 300 ms later, answered `confirmed`, `unconfirmed` (still
visible) or `unknown` (no launcher, no state, a failed probe, or no timer). The
Start side keeps the command identity in `g_pendingDismissConfirm` after the
executed ack clears the pending slot, and an unconfirmed or unknown result is a
terminal repair: ownership is released, the ticket dies by its own sequence, and
the next transition reads the cloak. A superseded confirmation is reported
separately
and needs no repair.

"Never lost" is the intent of the mechanism, not a theorem. The cases it does not
cover are the ones where the peer itself is gone: a press the bridge never
swallowed that arrives after the 600 ms gate closed is passed to the shell; a
press it did swallow and a latch armed before a kill and served after it are
dropped. Those are detection windows, and the trade is explicit:
the shell's own toggle is always correct, so a lost interception costs one press,
never a wedged menu.

The Start side publishes the executor command and then answers the intent
(`intent ack`); every branch publishes the resulting controller direction before
that answer, so the ack means "direction visible" and is the bridge's one
serialisation point for the next admitted press — the take alone is not:

- open: `exec show` immediately; the shell's own show is swallowed while the
  controller owns the panel and the same opening touch is protected by
  `controllerTouch`;
- close: `TakeCloseOwnership` + the mirrored settle from `TakeProgressNow()`,
  then `exec dismiss` at `settle + 30 ms`;
- a new intent that takes over the closing panel publishes `exec cancel` with
  the dismiss sequence; the bridge kills its timer and the menu stays open;
- external takeovers (Win+Tab, lock, display change, a modified forwarded Win
  press) publish `intent=external`: the Start side revokes both close schedules
  and the recovery ticket, ends the close record, drops the touch state and
  resyncs from the cloak. Win+Tab's `Dismiss(5)` is forwarded to the original by
  the bridge instead of being swallowed, so Task View appears and the pending
  dismiss is retired (`exec pass dismiss method=5 caller=wintab`). The bridge
  publishes the takeover at once and sets a barrier for
  every later publication — a state, not a timer, so it stands even when the
  watchdog cannot be armed (`own external barrier seq=... armed|unarmed`) — so a
  following press cannot overwrite the takeover before Start reads it. The barrier
  is cleared only by that record's ack or by a peer that is gone/stale; a live but
  silent peer keeps it (`decision=reserved`), and the takeover has no original to
  replay.

An unanswered intent is resolved by the record, not by the heartbeat. The Start
side claims the sequence before copying and re-checks the claim after the copy; a
claimed record it cannot apply is answered as superseded, so the publisher is
never left waiting. Any claim keeps the bridge's slot open: the press waits for
the ack (the take proves execution, not that the direction is visible; every
branch publishes the direction before the ack), and the next press's queue-age
bound is paused, so no later press can be reordered ahead of it. Only a changed
`peerEpoch` or a gone/stale peer releases the slot without the ack. An untaken
record is aborted (its counter goes odd, so no consumer can copy or claim it) and
re-read before any replay. A changed epoch at the watchdog **or at the next
admission** is a restart: the replacement seeded `g_lastIntentSeq` past the record
and will never answer, so the record is aborted, replayed if no claim exists, the
external barrier is released, and the queue advances without a timer. The peer
words stay in the line as evidence.

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

## The open and the close (bottom auto-hidden; bottom docked numbers named)

Open (touch, bottom docked, warm):

```
contact / hid usages          172817328
exec show request            172817328   (+0)
exec show ... hr=0 visible=0 172817328   (+0; the bridge's 15 ms call is below)
own state event=shown        172817406   (+78 ms)
reveal takeover              172817437   (+109 ms)
exec visible ... visible=1   172817640   (+312 ms)
```

The Win open, bottom auto-hidden layout (three captures at `x=1200` in a
16×1600 strip, `top` counts the first panel row): key → `exec show` **+31–32 ms**
→ `own state event=shown` **+46–141 ms** → first panel pixel **+82–187 ms**
(median +115) → `exec visible ... visible=1` **+343–344 ms**. The bottom docked
capture was `key=172830139`, `exec show` +79 ms, `shown` +142 ms, first panel
pixel **+190 ms**, `visible=1` +392 ms. The shell's cloak is the dominant term;
the executor removes the shell's 60–250 ms gesture wait. The earlier bottom
auto-hidden variant measured +257 ms for the same pixel.

The touch show is requested once per contact, and only after inward travel:
admission records the contact, and `TryClaimRevealOpen` claims the open on the
first positioned report whose signed inward travel reaches `kAdoptTravelDip`
(24 DIP). `g_intentOpenActive` refuses a second claim. A tap anywhere in the band
requests nothing; a refused claim is marked and not retried, and an open the
shell performs on its own is adopted. Measured on the bottom docked layout, with
the trace on: 31 ms from the first positioned report to the 24 DIP crossing on
a ~1 DIP/ms pull, +15 ms from the crossing to the executor's `ShowStartView`
(request published in the crossing millisecond), and the uncloak +125 ms with the
first captured panel frame +141 ms after `ShowStartView`. A 23 DIP drag requested
nothing from this mod (the shell's own edge gesture, which opens from about
20 DIP, opened the menu); a 25 DIP drag requested one show. The entry is
`RevealEntryProgress`; the bottom auto-hidden layout read **0.255** (warm; the
panel trail is 265, the Distance 852); the bottom docked layout read 0.348. A
cold first open still reads 0.000. The first visible frame is the entry, not 0.

The reveal takeover starts at the entry and chases the mapped finger at
`kSettleSpeedDipPerMs` (2.2 DIP/ms), then follows. A takeover of a panel that is
already presented (a pull that cancelled a close) starts at the live position and
chases the finger minus the offset it found (`live=1`). Bottom auto-hidden, a held
contact (`follow_hold`, 85 reports): `reveal takeover from=0.255 offset=0.338`,
`open follow travel=488 max=488 progress=0.90 behind=0.000 commit=1`; the panel
edge is 350.2 DIP at the held 350 DIP finger, **0.4 px** away. The 300 DIP hold
gives `progress=0.96` and a 299.1 DIP edge, **1.8 px** away; the bottom docked
300 DIP hold measured 2 px. The `offset` is the inherited gap at the takeover;
the chase closes it at 2.2 DIP/ms, and `behind=0.000` is the glued state. Those
sample lines were captured before `live=` existed; `live=0` is what they would
read.

A digitizer that goes quiet while the shell uncloaks, or while a close owns the
panel, is not a lift: while the touch show is in flight or the close record is
live, the 18 ms idle release is held, so the grab the close is carrying survives
long enough to spend its one transfer attempt. A contact the close consumed keeps its
latch and its mute through that quiet: while a close owns the panel no idle
expiry runs, and a Contact Identifier is never ended by silence — the lift
report or a report naming a different identifier ends it. A node index and the
coordinate-only path cannot prove a new contact and keep the 18 ms idle as the
fallback once the close has ended. A real lift is a report and still ends
the contact.

Close: a Win, Start-button or click-away intent calls `TakeCloseOwnership`
(accepts the close record — serial, acceptance tick, the menu, the phase, the
repair bound — publishes the closing direction before the
settle's first frame, consumes every contact on the glass — the dismiss mute and
the reveal flags cleared) and runs
`BeginMirrorCloseSettle` from `TakeProgressNow()`: the open curve played
backwards, full travel (387 ms at the bottom auto-hidden 852 DIP; 409 ms at the
bottom docked 900). The dismiss is scheduled at the end plus 30 ms.
The bridge's timer runs `Dismiss`; the shell's cloak event follows
and the Start side logs `win close observed`, clears the direction and
`controllerTouch`, and the next open is ready. The cloak read is tri-state: a
failed read is unknown, never hidden. A window the shell already had
hidden produces no transition; the executor's 300 ms confirmation then runs the
same landing itself (`win close observed
reason=confirmed-without-transition`), identified by the pending dismiss command
and its open generation, so the direction cannot stay `closing` into the next
press. When the confirmation reads hidden but the cloak still says open or
cannot answer, the command and generation are **retained** for the transition
(`exec dismiss retained`), which then lands them once
(`exec dismiss retained served`); a later open retires both slots — cancelling a
still-queued command at the bridge and dropping the confirmation — and the bound
ends the wait if no transition comes. The dismiss is stamped with the section's
current open generation at request time even when the close intent carries 0,
and the generation comparison is exact: a zero stamp is admissible only while no
open has been published. The close is
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
(a 1–15 ms window); the close then lands and the next transition reissues the
open
(`intent open recovered entry=...`), so that particular press is recovered — it is
the one race this ticket exists for. A press during a
shell hide (state 9/11) or inside our own `Dismiss` call is latched and served
at the landing instead. Neither path is a guarantee: the recovery ticket only
serves a landing for the menu it was taken for, the latch is bounded and
revalidated, and a press the 600 ms gate no longer covers is the shell's — unless
the bridge already swallowed it, in which case it is dropped.

A qualified pull aborts a close the controller still holds, at its 24 DIP report:
`CancelCloseForQualifiedPull` revokes both schedules, arms the same recovery
ticket an explicit open request arms (one helper, `ArmCloseCancelTicket`, serves
both), and hands
the panel to the follow from its live position. That takeover keeps the
finger-to-panel offset it found (`live=1`) instead of starting at the entry, so
an interrupted retract reverses where it is. It is the grab's one opportunity. A
refused transfer — a dismiss whose call already returned with its hide
unconfirmed (`g_pendingDismissConfirm`), a published dismiss at or past its
deadline (`reason=dispatch`: the bridge may already be inside the call), a
retract already at or below the entry for a down on the retract's surface,
a layout with no distance, or nothing left to cancel — consumes the contact
(`grab=refused reason=...`): it keeps no open right until it lifts, and its
release leaves no record. A down in the band at or below the entry — or with the
close already over — is not refused:
it is the same request a press makes in that moment, so the attempt runs the
press's cancel body when there is still a close to revoke
(both schedules dropped, the close record ended, the open direction published,
the recovery ticket armed) and only that body: a pull has a finger, so the grant
is spent and the follow starts at the entry and chases the mapped position, and a
window the retract already hid gets the ordinary show and binds there
(`grab=open window=open|hidden entry=... reason=offsurface|no-close`).

A landing adopts no finger. A close consumes every contact that was on the glass
when it started, and a grab whose transfer is refused is consumed too, so after
`win close observed` the controller issues no touch show and no
carried travel or release record turns the landed close into a Show
(`win close observed ... muted=1`, the one mute a refused down also sets). The
landing retires no grant: a grab still on its way to its own 24 DIP report keeps
it, and a band down among those opens the menu after the landing like any pull on
a closed menu. The
only open a landing still serves is the explicit request's obligation: a press or
a band pull whose cancel armed the ticket and whose dismiss ran anyway
(`intent open recovered entry=...`). A finger whose grab was refused needs a new
pull.
Measured on the bottom docked layout, the earlier build reopened its landing for
a pull held at `y=350 DIP` (`reveal takeover from=0.348 offset=0.558 live=0`,
221 reports, `progress=0.91 behind=0.000 commit=1`); that run is historical. A
contact already on the glass when the close starts is consumed by it, and a grab
that outlives the retract is not consumed by the landing. The bottom
auto-hidden entry is 0.255.

The close record is what keeps the visibility service from "healing" the panel
that
the close has retracted: without it the service saw a shown window with the panel
at 0 and started an open settle between the settle end and the dismiss
(`win open unhealed`), a flash. Both families accept the record, so the intent
close and the gesture close suppress the same way. The record is not the landing's
identity: the
landing is this mod's close when its dismiss command is still queued/armed or
executed-unconfirmed, or when the open-cancel ticket names that command. A
maximum-duration close measured 562–593 ms from takeover to `win close
observed` over ten runs (mean 578, 7 ms inside the record's 600 ms repair bound at
the worst), so a
busy launcher can pass the bound and must not decide a pull's fate (see
[limits.md](limits.md)); the bound's ending is the logged repair, never a silent
expiry.

Measured interruption (bottom auto-hidden unless named):

- open settle interrupted: the Win close 172 ms into the open started at
  `from=0.542` (the live position), not 1.0;
- drag interrupted: a Win press while a touch was held at `y=399.5` ran the
  close from `progress=1.000`, hid at +437 ms, and the touch's later lift was
  ignored (bottom docked);
- press during a close, 40 randomised trials per kind, press offset random
  50–500 ms: **0 lost**. Win close: 29 latched (waits 94–359 ms, median 219), 11
  direct, 0 cancelled. Start-button close: 30 latched (median 210), 10 direct.
  Click-away close: 21 latched (median 234), 19 direct. Gesture close: 7
  cancelled (`intent open cancel`, the press landed in the retract), 33 direct,
  0 latched, 0 lost; two of the seven were first marked invalid because the
  script's t0 lagged the decoded release by 266 ms (the injected contact ended on
  idle), and the log shows one cancelled and one direct. A separate 20-trial
  gesture run aimed at the settle's end gave 1 latched (94 ms), 1 cancelled, 17
  direct, 1 t0-lag trial, 0 lost. These trials ran the build that latched on an
  armed dismiss: the presses counted as latched are the ones the current rule
  cancels (`intent open cancel`) instead, because the timer is revocable; only a
  press inside a hide that had already begun latches now.
- the settle-end → dismiss window: 18 of the randomised presses landed after the
  close settle and before the dismiss had completed (Win 8, Start-button 3,
  click-away 6, gesture 1); 0 lost. The 3–4 ms cases were latched and served
  94–141 ms after the press; the later ones opened directly once the hide had
  landed.
- `intent open recovered` (a dismiss that already entered the shell when the
  press arrived) is the safety net for the 1–15 ms race; it did not fire in the
  runs above.

## Mixed cycles (bottom docked)

Ten cycles across the four triggers (Win, Start button, click-away, touch) on
the bottom docked layout, after the latch and the opening guards: every cycle
ended with the menu closed (`START ... open=False`), the bridge log for the
series contains **0** `own state resync`, **0** refused commands, **0** ack
timeouts and **0** dropped commands, and every close logged
`own state event=hidden open=0` and the executor's `visible=0`. The
Start-button cycles worked because the bottom docked bar is always visible (the
bottom auto-hidden run could not re-trigger them). The decision/executor lines
for the four triggers are in the session report.

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
- the panel-ownership clock (`g_suppressRestoreUntil`): the close record owns the
  panel now, and its tick fields are data, never the answer;
- the retract request's cancel answer;
- the reveal claim (`revealOpen`/`revealTick`) and the two-curve style switch;
- the landing's finger adoption: `RevealPullQualified`, the touch branch of
  `BeginLandingOpen`, the deferral release and the release-record consumption
  (`reveal reopen after close`, `reveal defer released`);
- the late-release deferral fields (`RevealLateRelease::deferred`,
  `readyQpc`): the record's window is measured from its release, and a grab's
  release leaves no record at all;
- the `win open continued` ticket: its only stamp sat behind the same
  `CloseInFlight` early return that guards the follow, so it could never fire;
  the press cancel, the press latch and the landing's press recovery are the
  reopen paths that remain.

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
fetch. The apply (`Wh_ApplyHookOperations`) is inside the same symbol-install
lock as the resolution, so `BeforeUninit` either waits for the complete install or
makes it refuse before `HookSymbols`; no root hook can become live after the stop.
The `LoadLibraryExW` notification only records the returned module, and so
do the init and AfterInit probes (`GetModuleHandleW` only): the input thread's
first tick installs from the record, which keeps the warm case fast without any
symbol work on a shell thread. A
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

The merged channel is **224 bytes** with one section and four events. The names
use one merged-mod prefix (`Local\SwipeFor10Start_SingleMod2_*`) and there is **no
product version word**: the ABI namespace is the identity, so an old development
image cannot join and a future layout changes the prefix. The records are the open
generation, the intent and its ack and the `intentTaken` claim, the executor
command and its ack, one
controller direction word plus the touch flag, and the liveness triple
`peerTick`/`peerState`/`peerEpoch`. The debug command channel — its record,
mutex, event and parser — is gone; `tools/ipc.ps1` is read-only. The executor
validates every command before the launcher call (show 1/11/16 with flags under
`0x8|0x4`, dismiss 1/6 with no flags, cancel 0) and refuses anything else instead
of clamping it. The producer checks the same vocabulary (`SwipeShowAllowed`,
`SwipeDismissAllowed`) before it swallows a press: an unsupported method/flags
combination is passed to the shell's original at admission (`own pass ...
reason=unsupported`), so the executor's refusal is defence in depth, never the
first time the press is validated. A click-away intent's method 7 is evidence
and is mapped to 6 at the producer, but only for the click-away trigger: an
ordinary caller's method-7 Hide is passed to the shell, never converted. The
executor sees 1 or 6 only.

**A note on the retired versions.** v7 added `peerEpoch` at 740 inside v6's
744-byte section; v6 added the liveness pair at 732/736; v5 removed the M2 slots
and replaced the owning/phase pair with one direction word. These are history,
not the current layout; the asserts in `swipe_ipc_shared.h` are the contract.

## The accepted cap, restated

The shell's first visible frame is 245–268 ms after a band show in the worst
case. The bottom auto-hidden Win open showed its first panel pixel at
**+82–187 ms** from the key (median +115 over three runs; `exec show` +31–32 ms,
`shown` +46–141 ms), and the bottom docked capture at **+190 ms** (79 ms to
`exec show`, 111 ms to the first pixel). The shell owns
that window; the early-uncloak experiment found DWM attribute 13 returns
`E_ACCESSDENIED` without the shell's IAM ticket, and the owning process cannot
set it before the shell's own uncloak. No early visibility is attempted. The M3
gain is the decision and command latency (31 ms) and the removal of the shell's
60–250 ms gesture wait, not the cloak.
