# swipe-for-10start - touch swipe for ExplorerPatcher's legacy Win10 Start

C++ / Windhawk 2.0 mods + PowerShell 7 test tools
<directory>
mods/ - Windhawk mod sources (4 sources + 1 header: swipe-for-10start, touch-gesture-probe, msgprobe, inputprobe; swipe_ipc_shared.h)
tools/ - PowerShell probes, injectors, animation harnesses, the regression smoke, the store packager and the local installer (18 scripts + AGENTS.md)
artifacts/ - captured measurements (6 CSVs + AGENTS.md; `inputprobe.dll` is an untracked local build output of `mods/inputprobe.wh.cpp`)
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

## What ships (M3.3, 0.7.0, one mod)

One mod, two processes. `mods/swipe-for-10start.wh.cpp` is installed once and
loaded into `StartMenuExperienceHost.exe` (namespace `start`) and into
`explorer.exe` (namespace `bridge`): the controller owns the touch gesture, the
panel claims, the settle planner, the gates and the controller state; the
executor owns the launcher call and the non-touch triggers. Wh_ModInit picks one
role table from the host executable name before either half runs, and the other
half's hooks, threads and handles are never created there.

The controller decides; the executor performs. The Win key
(`XamlLauncher::OnHotkeyInvoke`), the Start button (`ShowStartView` method 11,
`Dismiss` method 6) and the click-away (`HideStartAndCortana` method 7) publish
one `intent(seq, kind, trigger, method, flags, open)`; the Start side answers
(`intent ack`) and publishes `exec show`, or runs the one mirrored close settle
and then `exec dismiss`. External takeovers (Win+Tab, lock, display change, a
forwarded modified Win press) publish `intent=external` and are never held.

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
state is only the resync for a panel the controller does not own. That direction
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
because the idle timer outranks the intent event. A
shell hide in flight (`InternalState` 9/11), or an executor dismiss already armed
at the bridge, latches the press (one slot, with its tick and **the epoch the
admission was decided under**). The hidden event arms a 50 ms timer and the timer
serves the same open intent after the event's stack has unwound; a show at the
event's own tick was swallowed (state stuck at 3, no Shown). The arm is posted to
the executor window when the event runs off the launcher thread. Supersession
lives in the shared dispatcher, so a newer press from any primitive discards the
slot; a serve re-reads the peer and the epoch, a refused or expired dismiss
invalidates the slot, and a latch whose **initial** timer could not be armed is
dropped rather than left with no guaranteed callback. The residual loss window is
the peer's detection budget: a press after the gate closed, or a latch armed
before a kill and served after it, is the shell's. A press the bridge already
swallowed has no shell path left — it is dropped, not passed on.

The shell's own click-away while the window is appearing (state 1/3) is
swallowed: it is the pointer-away check of a keyboard or own show, not a user
click. The same Hide(7) issued inside our own `ShowStartView` is swallowed while
the state is 1/3 (`reason=own-show`); otherwise the served open is closed by the
shell the instant it appears.

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
(`reveal pull qualified ...` then `reveal show request ...`). A tap anywhere in
the band — twice the taskbar's thickness — requests nothing and stays a native
press; the shell's own edge gesture (from about 20 DIP) remains the fallback and
an open it performs is adopted. One attempt per contact: a refused request is
not retried. The content settle starts at `RevealEntryProgress`, and the reveal
takeover chases the mapped finger at 2.2 DIP/ms, then follows (`behind=0.000`).
While a touch show or a close is in flight the 18 ms idle release is held,
because the digitizer goes quiet during the uncloak and during the key/hide. A
pull that is still down and qualified when a close lands (or that lifted within
the 300 ms late window) is the pull that takes the menu back: after `win close
observed` the controller issues the same show the qualified pull would, and the
open adopts the live contact or the release record (`reveal reopen after close
pull=1 late=0`). "Qualified" is the same 24 DIP at the contact's last positioned
report; a finger merely resting in the band through the retract is not reopened.
An intent close is
`TakeProgressNow()` + the mirrored settle + `exec dismiss` at +30 ms; a gesture
close asks for the same dismiss at its own settle's end. The bridge's timer runs
`Dismiss` on the launcher thread. The close suppression keeps the visibility
poll from healing the retracted panel before the dismiss lands.

The close is judged by behaviour: the shell's hidden event (`own state
event=hidden open=0`), the executor's `confirmed` (hidden) at its bounded 300 ms
confirmation — `unconfirmed`/`unknown` is a repair, not a hide — and the next open
working.
The internal `0xA` value is **not** an acceptance criterion and is no longer
quoted.

Deleted in M3: the `VK_LWIN` tap, the close record and its publishes, the retract
request/answer and its cancel answer, the arbitration hold, the deferred
candidate **as an arbitration mechanism**, and the reveal claim. The deferred
candidate still has one live use, and it is not arbitration:
`BeginEdgeCandidate` (the touch show's band admission) calls `StartRevealFollow`
with `deferred=true` while `CloseInFlight`, so a contact that lands during a
close is tracked without writing `Progress`; the landing (`PollRootVisibility`)
releases it. `CloseInFlight` remains only as the
suppression-window predicate: the settle owns the motion. The
`win open continued` ticket went with them: its only stamp sat behind the same
`CloseInFlight` early return that guards the follow, so it could never fire; the
pull-reopen and the press latch are the reopen paths that remain. M3.1 also
removes the retired v2 close/retract slots from the header.

## Channel

`mods/swipe_ipc_shared.h`: one session-local section (216 bytes) and four named
events (`State/Intent/IntentAck/Exec/ExecAck` — five objects), every record a
seqlock with its own counter and an `offsetof` assert. The names carry the merged
mod's own prefix (`SwipeFor10Start_SingleMod_`) and there is **no product version
word**: the namespace is the ABI identity, so an older development image cannot
join, and a future layout changes the prefix. The records are the open
generation, the intent and its ack, the executor command and its ack, one
controller direction word plus the touch flag, and the liveness triple
`peerTick` (204) / `peerState` (208) / `peerEpoch` (212). The debug command
record (`cmdText`, `Cmd`, `CmdWrite`, the parser) is **gone**.
`tools/ipc.ps1` is read-only: `-State` reads 216 bytes and prints the records and
the peer epoch.
`SwipeIpcOpen` may wait the whole 550 ms init budget; generation 0 and an unknown
`initState` are refused, never waited on. Each role opens the channel at its
`RoleInit`, never in a hook.

## Machine

Windows 11 10.0.26100.9949, ExplorerPatcher legacy Start
(`Start_ShowClassicMode=1`), 2880x1800 physical at 200 % (1440x900 DIP).
**Current layout: auto-hidden** (auto-hide on): work `0,0,1440,900`, the bar off
screen, band `804..900`, panel `236,265..1229,840`, trail 265, Distance 852. The
docked layout (work `0,0,1440,852`, tray `0,852..1440,900`) and the earlier
auto-hidden variant (panel `236,167..1229,840`, trail 167) are historical; their
numbers are not the current ones. The Start button is off screen until the bar
is revealed; the button test hovers the bottom edge first, then clicks physical
`(36,1748)`.

## Rules

- One data source per claim: the cloak decides the shell state, the controller's
  published direction decides the toggle, the geometry snapshot decides the mapping.
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
- The image is not pinned. Unloading joins every owned thread, runs the UI
  teardown turn, drains the counted entries, destroys the launcher window and
  unregisters its class before the channel closes. A proof that cannot be
  obtained blocks the unload; there is no residual success.
- Docs and code move together. Update this header, the L2 map and the L3 header
  of the file you changed.

[PROTOCOL]: Update this header on change, then check AGENTS.md
