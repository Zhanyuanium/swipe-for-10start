# swipe-for-10start - touch swipe for ExplorerPatcher's legacy Win10 Start

C++ / Windhawk 2.0 mods + PowerShell 7 test tools
<directory>
mods/ - Windhawk mod sources (8 sources + AGENTS.md: start-menu-legacy-swipe, touch-gesture-bridge, touch-gesture-consumer, touch-gesture-probe, msgprobe, inputprobe, start_gesture_shared.h, swipe_ipc_shared.h)
tools/ - PowerShell probes, injectors and animation harnesses (15 scripts + AGENTS.md)
artifacts/ - captured measurements (6 CSVs + AGENTS.md; `inputprobe.dll` is an untracked local build output of `mods/inputprobe.wh.cpp`)
docs/ - technical documentation (architecture, input, gesture, animation, geometry, ipc, lifecycle, testing, limits, README + AGENTS.md)
</directory>
<config>
README.md - user-facing quick start (English; README.zh-CN.md is the Chinese page)
README.zh-CN.md - the Chinese page of the user-facing quick start
.gitignore - Windhawk/MSVC build outputs and local logs
</config>

Laws: measure before designing · one data source per claim · docs and code move together

## What ships (M3.2, Start 0.41.0 / bridge 0.22.0)

Two mods, one path. `mods/start-menu-legacy-swipe.wh.cpp` runs in
`StartMenuExperienceHost.exe` and owns the touch gesture, the panel claims, the
settle planner, the gates and the controller state.
`mods/start-menu-touch-gesture-bridge.wh.cpp` runs in `explorer.exe` and owns the
launcher call and the non-touch triggers.

The controller decides; the bridge executes. The Win key
(`XamlLauncher::OnHotkeyInvoke`), the Start button (`ShowStartView` method 11,
`Dismiss` method 6) and the click-away (`HideStartAndCortana` method 7) publish
one `intent(seq, kind, trigger, method, flags, open)`; the Start side answers
(`intent ack`) and publishes `exec show`, or runs the one mirrored close settle
and then `exec dismiss`. External takeovers (Win+Tab, lock, display change, a
forwarded modified Win press) publish `intent=external` and are never held.

A press is never lost. The bridge's one dispatch reads the controller's published
direction: `closing` (a close in flight)
makes the press an open and `open` makes it a close. The launcher's
state is only the resync for a panel the controller does not own. That direction
cannot come from the shell: a gesture close retracts the panel with
`InternalState` 5 and the cloak still open for the whole settle, until the
executor's `Dismiss`; the controller publishes `closing` in `TakeCloseOwnership`,
before the settle's first frame. A close still settling publishes open at once:
the controller drops the local dismiss slot and cancels a pending executor
command, then settles the panel back from its live position (`intent open cancel
from=...`); a ticket for that open is kept until a landing or an explicit
superseding action. A dismiss already inside the shell's `Dismiss` call cannot be
recalled; the window hides, and the landing reissues that open once (`intent open
recovered entry=...`). A shell hide in flight (`InternalState` 9/11), or an
executor dismiss already armed at the bridge, latches the press (one slot, with its tick). The
hidden event arms a 50 ms timer and the timer serves the same open intent after
the event's stack has unwound; a show at the event's own tick was swallowed
(state stuck at 3, no Shown). A second press replaces the slot and logs the
replacement; the serve logs its wait.

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
`0x100`). `IsVisible` confirms: honest `0` right after Show (the shell is
cloaking), a 300 ms probe reads `1`; Dismiss is immediate.

The open runs from the band contact: one `exec show` per open, the content settle
starts at `RevealEntryProgress`, and the reveal takeover chases the mapped finger
at 2.2 DIP/ms, then follows (`behind=0.000`). While a touch show or a close is in
flight the 18 ms idle release is held, because the digitizer goes quiet during
the uncloak and during the key/hide. A pull that is still down when a close lands
(or that lifted within the 300 ms late window) is the pull that takes the menu
back: after `win close observed` the controller issues the same show the band
admission would, and the open adopts the live contact or the release record
(`reveal reopen after close pull=1 late=0`). An intent close is
`TakeProgressNow()` + the mirrored settle + `exec dismiss` at +30 ms; a gesture
close asks for the same dismiss at its own settle's end. The bridge's timer runs
`Dismiss` on the launcher thread. The close suppression keeps the visibility
poll from healing the retracted panel before the dismiss lands.

The close is judged by behaviour: the shell's hidden event (`own state
event=hidden open=0`), the executor's `visible=0`, and the next open working.
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

`mods/swipe_ipc_shared.h` **v5**: one session-local section (736 bytes), seven
named objects (State/Cmd/CmdWrite, Intent/IntentAck, Exec/ExecAck), every record a
seqlock with its own counter and an `offsetof` assert. The M2 close/retract slots
are gone; the records are the command, the open generation, the intent and its
ack, the executor command and its ack, and one controller direction word plus
the touch flag. `tools/ipc.ps1`
is the only command publisher and takes the mutex; `-State` reads v5/736.
`SwipeIpcOpen` may wait the whole 550 ms init budget; generation 0 and an unknown
`initState` are refused, never waited on. Both mods open the channel at
`Wh_ModInit`, never in a hook.

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
- One mechanism, one log shape. A decision line and an executor line per
  trigger; no per-trigger branch in either mod.
- A wrong-thread launcher call is not a clean error. Never call from a hook
  frame; post to the executor window.
- The shell owns every visual transition. The mods read and request, never
  write the launcher's state.
- Docs and code move together. Update this header, the L2 map and the L3 header
  of the file you changed.

[PROTOCOL]: Update this header on change, then check AGENTS.md
