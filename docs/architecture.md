# M3.2 architecture — one controller, one executor

> L3-level design record. Start **0.41.0**, bridge **0.22.0**, channel **v5 /
> 736 B**. The measured evidence is quoted with the build and the layout it came
> from; everything below was run on this machine. **Layouts:** *auto-hidden* =
> work `0,0,1440,900`, the bar off screen, band `804..900`, panel
> `236,265..1229,840`, trail 265, Distance 852 (current). *Docked* = work
> `0,0,1440,852`, tray `0,852..1440,900`, same panel, trail 265 (historical).

## What ships

Two Windhawk mods:

- `mods/start-menu-legacy-swipe.wh.cpp` (0.41.0) runs in
  `StartMenuExperienceHost.exe`. It owns the touch gesture, the claim on
  `Progress`, the settle planner, the gates and the decision state. It consumes
  bridge intents and publishes executor commands.
- `mods/start-menu-touch-gesture-bridge.wh.cpp` (0.22.0) runs in
  `explorer.exe`. It turns the Win key, the Start button and the click-away into
  intents, and executes the Start side's `show`/`dismiss`/`cancel` commands on
  the launcher's own thread. It also swallows the shell's own duplicate
  show/close while the controller owns the panel.

The shell still performs every visual transition. Neither mod writes the
launcher's state.

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
launcher entry — the first of `OnHotkeyInvoke`, `Dismiss`, `ShowStartView` or the
manager's `Show` adopts the thread (`exec launcher thread`), and a window is
never created from the `LoadLibraryExW` hook, which runs on whichever thread
loaded twinui. A worker waits on the channel event and posts `WM_APP+0x5A`; the
WndProc reads the command and calls the launcher with an empty stack. The window
class is unique per activation
(`WindhawkSwipeExecutor_<WndProc address>`); a fixed name would return
`ERROR_CLASS_ALREADY_EXISTS` after a Windhawk reinstall and create the window
with the previous image's WndProc, whose channel is already closed.

`IsVisible` is the confirmation. Immediately after `ShowStartView` it is
honestly `0` (the shell is still cloaking); a 300 ms timer on the same thread
reads it again and logs `exec visible ... visible=1`. `Dismiss` is synchronous
enough that its own `visible=0` is the confirmation.

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

**A press is never lost — cancel, latch, or recover.** A close still settling
has either a local dismiss slot not yet requested (a gesture close) or an
executor dismiss armed for a future deadline (an intent close). Only the first
shape is cancelled: the Start side clears the suppression, drops the local slot
and, when a command is already pending, publishes `exec cancel`, then takes the
panel where the retract left it and settles it back to 1
(`intent open cancel from=...`). A press the bridge sees while the shell is
already hiding (`InternalState` 9/11) or while its executor dismiss is armed is
not published at all: it is latched in one slot (tick, trigger, method, flags).
The hidden event arms a 50 ms timer and
the timer serves the same open intent after the event's stack has unwound: a show
at the hidden event's own tick was swallowed (same tick, `exec visible=0`, state
stuck at 3, no Shown), while a show 16 ms after it opened normally. A second
press replaces the slot (`own press latch replaced ...`); the serve logs `own
press served wait=...` (current auto-hidden medians: 219 ms Win close, 210 ms
Start-button, 234 ms click-away; the gesture latch is rare and measured 94 ms).
The 400 ms timer is only the fallback for a landing the event missed. A dismiss
that had already entered the shell's `Dismiss` call when the press landed hides
the window anyway; the landing reissues the open once
(`intent open recovered entry=...`), and the recovery ticket is kept until that
landing or an explicit superseding action (a new close, an external takeover).
The
click-away that the shell issues while the window is appearing (state 1/3), and
the Hide(7) it issues inside our own `ShowStartView` in state 1/3, are the
pointer-away check, not a user click, and are swallowed
(`reason=opening`/`reason=own-show`). Without that, the served open came back
and was hidden in the same millisecond.

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

- no channel, no executor worker or no executor window: the trigger passes
  through untouched (`own pass trigger=... reason=...`), the shell's own path
  serves it;
- launcher not attached: `exec show`/`exec dismiss` is refused, one repair
  releases the direction entry, `controllerTouch` and the pending open entry, so
  the controller state resyncs from the cloak, and the shell's own gesture opens
  the menu;
- no executor ack within 400 ms (a show from its request, a dismiss from the
  deadline it was scheduled for): `exec ack timeout seq=… kind=show|dismiss
  (resync from cloak)`, same repair.

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
judged by that hidden event and `visible=0`, not by an internal `0xA` value;
that value is not a criterion. A gesture close is the same path with its own
release settle (160–520 ms) and no 30 ms lead: its dismiss is requested at the
settle's end.

A Win press during a close is an open because the controller's phase says so,
not because the shell's state does. The open intent takes the live panel, drops
the local dismiss slot, cancels a pending `exec dismiss` and settles back to 1
(`intent open cancel from=...`); the window never hides. The dismiss can already
be inside the shell's `Dismiss` call when that press lands
(a 1–15 ms window); the close then lands and the next poll reissues the open
(`intent open recovered entry=...`), so no press is lost. A press during a
shell hide (state 9/11) or with an armed executor dismiss is latched and served
at the landing instead.

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

**v5** removes the retired M2 slots from the header: the section is 736 bytes
and contains only the command, the open generation, the intent and its ack, the
executor command and its ack, one controller direction word and the touch flag.
No dead field remains; v5 also replaces v4's owning/phase pair with that one
word, so the bridge's toggle rule cannot read new ownership beside a stale
phase.

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
