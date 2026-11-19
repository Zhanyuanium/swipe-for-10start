# M3.1 architecture — one controller, one executor

> L3-level design record for `feat/own-start-state`. Start **0.39.0**,
> bridge **0.20.0**, channel **v4 / 744 B**. The measured evidence is quoted with
> the build and the layout it came from; everything below was run on this
> machine. **Layouts:** *docked* = work `0,0,1440,852`, tray `0,852..1440,900`,
> panel `236,265..1229,840`, trail 265, distance 900 (current). *Auto-hidden* =
> work `0,0,1440,900`, panel `236,167..1229,840`, trail 167 (historical; its
> numbers are never the docked ones).

## What ships

Two Windhawk mods:

- `mods/start-menu-legacy-swipe.wh.cpp` (0.38.0) runs in
  `StartMenuExperienceHost.exe`. It owns the touch gesture, the claim on
  `Progress`, the settle planner, the gates and the decision state. It consumes
  bridge intents and publishes executor commands.
- `mods/start-menu-touch-gesture-bridge.wh.cpp` (0.19.0) runs in
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

Shape: a hidden message-only window owned by the mod, created by a one-shot
`WH_GETMESSAGE` hook on the launcher thread (or directly on the first decision
hook, which already runs there). A worker waits on the channel event and posts
`WM_APP+0x5A`; the WndProc reads the command and calls the launcher with an
empty stack. The window class is unique per activation
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
| Windows key | `XamlLauncher::OnHotkeyInvoke` (`+0x14195C`) | `own win decide state=… kind=open|close` |
| Start button open | `ShowStartView` method 11 | `intent open trigger=startbutton` |
| Start button close | `Dismiss` method 6 | `intent close trigger=startbutton` |
| click-away | `HideStartAndCortana` method 7 | `intent close trigger=clickaway` |
| touch | the Start side's band admission | `reveal show request` |

`IntentShouldClose` is the only toggle rule: the controller's phase (1 opening,
2 closing) decides first, then the shell's cloak and internal state (1/3/5 are
open or opening, 7/9/11 are closing). A press during a close is therefore an
open, and a press during an open is a close. No trigger has its own branch.

**A press is never lost — the latch.** The same dispatch decides from the state:
a close still settling (phase 2, no dismiss armed) is genuinely cancellable and
publishes open now; a shell hide in flight (`InternalState` 9/11), or a dismiss
already armed and due, is latched in one slot (tick, trigger, method, flags) and
served as the same open intent when the hide lands (`own state event=hidden`, or
a closed cloak if the event was missed). A second press replaces the slot
(`own press latch replaced ...`); the serve logs `own press served wait=...`
(measured 31–110 ms; median 78 ms over 30 randomised trials). The timer is only
the fallback for a missed landing. The click-away that the shell issues while the
window is appearing (state 1/3), and the Hide(7) it issues inside our own
`ShowStartView` in state 1/3, are the pointer-away check, not a user click, and
are swallowed (`reason=opening`/`reason=own-show`). Without that, the served open
came back and was hidden in the same millisecond.

The Start side answers each intent (`intent ack`), then publishes the executor
command:

- open: `exec show` immediately; the shell's own show is swallowed while
  `controllerOwning` and the same opening touch is protected by
  `controllerTouch`;
- close: `TakeCloseOwnership` + the mirrored settle from `TakeProgressNow()`,
  then `exec dismiss` at `settle + 30 ms`;
- a new intent that takes over the closing panel publishes `exec cancel` with
  the dismiss sequence; the bridge kills its timer and the menu stays open;
- external takeovers (Win+Tab, lock, display change, a modified forwarded Win
  press) publish `intent=external` and are never held.

Fallbacks, each logged once:

- no channel: the trigger passes through, the shell's own path serves it;
- launcher not attached: `exec show` is refused, the Start side clears
  `controllerOwning`/`controllerTouch` and resyncs from the cloak, and the
  shell's own gesture opens the menu;
- no executor ack within 400 ms: `exec ack timeout seq=… (resync from cloak)`,
  same release.

## The open and the close (docked layout)

Open (touch, docked, warm):

```
contact / hid usages          172817328
exec show request            172817328   (+0)
exec show ... hr=0 visible=0 172817328   (+0; the bridge's 15 ms call is below)
own state event=shown        172817406   (+78 ms)
reveal takeover              172817437   (+109 ms)
exec visible ... visible=1   172817640   (+312 ms)
```

The Win open captured at `x=1200` in a 16×1300 strip: `key=172830139`,
`exec show` `172830218` (+79 ms), `shown` `172830281` (+142 ms), first panel
pixel `172830329` (**+190 ms**), probe `visible=1` `172830531` (+392 ms). The
shell's cloak is the dominant term; the executor removes the shell's 60–250 ms
gesture wait. The auto-hidden layout measured +257 ms for the same pixel; the
docked bar changes the available work area, not the cloak rule.

The touch show is requested once per open (`g_intentOpenActive` guards a second
report while the window is still closed). The entry is
`RevealEntryProgress`; docked it reads **0.348** (warm; the panel trail is 265,
the distance 900). A cold first open still reads 0.000. The first visible frame
is the entry, not 0.

The reveal takeover starts at the entry and chases the mapped finger at
`kSettleSpeedDipPerMs` (2.2 DIP/ms), then follows. Docked, held contact
(`follow_hold`, 114 reports): `reveal takeover from=0.547 offset=-0.127`,
`open follow travel=475 progress=0.91 behind=0.000 commit=1`. A hold at
physical 600 (`y=300 DIP`) gives `progress=0.96 behind=0.000`; the panel's top
edge is then 301 DIP, **2 px** from the finger (the M2 hold was 4 px on the
other layout). The `offset` is the inherited gap at the takeover; the chase
closes it at 2.2 DIP/ms, and `behind=0.000` is the glued state.

A digitizer that goes quiet while the shell uncloaks, or while a close is in
flight, is not a lift: while the touch show or the close suppression is active
the 18 ms idle release is held, so the candidate the open/landing is about to
adopt survives. A real lift is a report and still ends the contact.

Close: a Win, Start-button or click-away intent calls `TakeCloseOwnership`
(sets `g_suppressRestoreUntil`, mutes the contact, clears the reveal) and runs
`BeginMirrorCloseSettle` from `TakeProgressNow()`: the open curve played
backwards, full travel (409 ms at 900 DIP). The dismiss is scheduled at the end
plus 30 ms. The bridge's timer runs `Dismiss`; the shell's hidden event follows
and the Start side logs `win close observed`, clears
`controllerOwning`/`controllerTouch`, and the next open is ready. The close is
judged by that hidden event and `visible=0`, not by an internal `0xA` value;
that value is not a criterion.

A pull that is still down when the close lands (or that lifted inside the
300 ms late window) is the pull that takes the menu back: after
`win close observed` the controller issues one fresh show
(`reveal reopen after close pull=1 late=0 entry=0.348`), and the open adopts the
live contact or consumes the release record. Measured: the Win close during a
held pull at `y=350 DIP`; the landing reopened, `reveal takeover from=0.348
offset=0.558`, the follow continued with 221 reports and ended
`progress=0.91 behind=0.000 commit=1`, menu open.

The suppression is what keeps the visibility poll from "healing" the panel that
the close has retracted: without it the poll saw a shown window with the panel
at 0 and started an open settle between the settle end and the dismiss
(`win open unhealed`), a flash. `TakeCloseOwnership` now sets it for the intent
close too.

Measured interruption:

- open settle interrupted: the Win close 172 ms into the open started at
  `from=0.542` (the live position), not 1.0;
- close interrupted: a second Win 79 ms after the close published `exec cancel`,
  the bridge killed the timer, and the menu stayed open (`visible=1` after
  300 ms);
- drag interrupted: a Win press while a touch was held at `y=399.5` ran the
  close from `progress=1.000`, hid at +437 ms, and the touch's later lift was
  ignored;
- press at the hide: 30 randomised trials (10 Win, 10 Start-button, 10
  click-away closes, press offset random 300–445 ms) gave **0 lost**, 17
  latched-and-served (waits 47–110 ms, median 78), 1 cancelled (settle still
  cancellable), and 12 direct opens (the press landed after the hide). The
  latch is the one path for the last two shapes; the pre-hide shape is the
  cancel.

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
- `CloseInFlight` as the owner of the panel; the suppression window is now set
  by the one close path and the close's dismiss is the owner;
- the `win open continued` ticket and the cancel answer;
- the reveal claim (`revealOpen`/`revealTick`) and the two-curve style switch.

**v4** removes the retired M2 slots from the header: the section is 744 bytes
and contains only the command, the open generation, the intent and its ack, the
executor command and its ack, and the controller flags. No dead field remains.

## The accepted cap, restated

The shell's first visible frame is 245–268 ms after a band show in the worst
case; the docked keyboard open captured its first panel pixel at **+190 ms**
from the key (79 ms to `exec show`, 111 ms to the first pixel). The shell owns
that window; the early-uncloak experiment found DWM attribute 13 returns
`E_ACCESSDENIED` without the shell's IAM ticket, and the owning process cannot
set it before the shell's own uncloak. No early visibility is attempted. The M3
gain is the decision and command latency (31 ms) and the removal of the shell's
60–250 ms gesture wait, not the cloak.
