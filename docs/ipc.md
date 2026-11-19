# IPC — the channel, the close protocol, and the explorer bridge

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). The gestures and settle this protocol schedules are in [gesture.md](gesture.md) and [animation.md](animation.md); the unload guarantees are in [lifecycle.md](lifecycle.md).

## The v2 channel

The two shipping mods and `tools/ipc.ps1` share one session-local shared-memory
section (`mods/swipe_ipc_shared.h`, v2): `Local\SwipeFor10Start_v2_State`,
three events `_Cmd` (manual-reset), `_RetractReq` and `_RetractPlan`
(auto-reset), and `_CmdWrite`, a mutex serialising command writers, which
`tools/ipc.ps1` takes before it writes. Every record is a seqlock: the writer
raises the counter to odd, writes, raises it to even; the reader retries while
it is odd or changed, and a dead writer's counter is moved forward so a reader
never accepts half-written fields. `SwipeIpcState` is 672 bytes with every
field pinned by an `offsetof` assert, so nothing polls and an answer cannot be
mistaken for an earlier one. The reveal claim is the one 32-bit record: it lives
in the layout's reserved words, so adding it kept that size and the version.

Init is a claim/ready handshake: `SwipeIpcOpen` may wait up to the whole
550 ms budget (the 500 ms abandon age plus one 50 ms window), so a young claim
is abandoned inside the call; it tells "not ready" (`ERROR_NOT_READY`) from
"broken" (`ERROR_INVALID_DATA`), and neither side ever recreates a section. An
`initState` outside the four values the header defines is refused at once with
`ERROR_INVALID_DATA` instead of being waited on, and every retry path checks
the deadline, so no state can keep the caller past the 550 ms budget. The
command event wakes the raw input thread, so nothing polls. Both sides record
the sequence numbers they find at attach, so a command or request from before
the process started is ignored instead of replayed.

`pwsh -NoProfile -File tools\ipc.ps1 p=0.5` (also `t=x,y,z`, `e=x,y` and
`anim=0|1`) drives `Progress`, `UIElement.Translation`, the visual `Offset` or
the open storyboard directly, and `-State` prints the counters.

## The open generation

Every closed→open transition publishes a fresh **open generation** on the
channel: that number is the identity of this menu, and a retract request is
bound to it. The same open transition writes the cancelled close kind, so an
unconsumed `tapped` record from the previous menu cannot survive into the next
one, and generation 0 is never a menu: `SwipeIpcReadOpen` reports
`ERROR_NOT_READY` for it.

## The close record

A gesture close publishes the close record three times — the plan at the arm,
the `tapped` record with a fresh sequence immediately before the tap, and the
cancelled kind if a new pull drops it — and only the `tapped` record tells the
bridge that the close this tap causes needs no retract lead. The close record is
an 800 ms window. A dropped close publishes the cancelled kind at once, and the
tap's deadline republication is only the fallback. A Windows-key close that
lands after the `tapped` record is a second close under the same stamp and takes
the request path; one that lands while only the plan is current does too. Only
`tapped` passes a close without a wait.

The bridge binds a `tapped` stamp to the open tick it read and refuses a stamp
older than that open. Open and cancellation are separate records, and equal
ticks do not order them, so that binding is correlation, not causal identity: a
close caused by the tap is inferred, not proven.

The retract answer carries one of three outcomes: the settle the Start side
planned, `kSwipeRetractPlanNone` when nothing started, or
`kSwipeRetractPlanCancel` when a reveal contact owns the panel and the close
must not happen. The cancel answer is published before anything else changes on
the Start side, so a cancelled close publishes no close record, arms no tap,
opens no suppression and writes no `Progress`.

## The gesture close

A gesture close needs no retract command. Its retract starts at the release and
its tap comes at the end of it; the bridge sees an unconsumed `tapped` record
inside its 800 ms window and passes the close at once (`close passed ...
reason=gesture`), because the panel is already off screen. The plan that preceded it
and the cancelled kind are not taps, so neither passes a Hide. The tap is what
updates the Start toggle, and it is the only tap: there is no programmatic
`Hide` and no 120 ms re-arm tap, so a press cannot collide with one.

A close exists once its settle starts. `BeginDismissSettle` reports three
outcomes: not started, started, and started with a lost batch observer (`close
observer lost kind=...`). A lost observer does not put the panel back: the
settle runs and its tap is the close, so the panel never flashes back. Only a
close that provably never started calls `AbandonClose(kind)` (`close abandoned
kind=...`), which restores the panel; that is the whole operation, because such
a close published no plan and armed no tap, so there is nothing to cancel.

A new edge pull during a gesture close's retract cancels that close on the Start
side while its tap is still this mod's own: the armed tap is dropped before it is
sent (the record becomes the cancelled kind), no `Hide` runs, the shell's toggle
is never touched, and the pull's own follow keeps driving the panel. The drop
happens on the new contact's first report, not at the tap's deadline, so a pull
whose first point lands above the band cancels too; the panel the close had
retracted is brought back from its live position. A
gesture close that the user cancels by pulling the
menu open again keeps the live position the retract had reached and then chases
the finger's absolute mapped position at the planner's rate, instead of snapping
to it immediately; once the retract has finished (the panel cloaked or below the
visible entry) the follow starts at the entry, so the panel appears from the
screen edge and closes the gap to the finger.

Every other close — a keyboard close, a Start-button or click-away close, or a
gesture close whose tap already fired — already has its close in the shell's
hand (or, for a reveal that owns the panel, has just been refused), so the Start
side cancels nothing and publishes no record: a pull that lands in that window is
deferred instead (see [gesture.md](gesture.md)). The close record is unchanged by the deferral: the
plan, the `tapped` stamp and the cancelled kind mean what they meant when they
were written, and a deferral publishes none of them.

A close whose tap already fired is not cancellable, because its close is already
in the shell; only a close the decision hook has not yet released can be refused.
The contested ticket remains as the guard for
an open whose panel the close left above the hidden rest: the Start side stamps
it only if a follow activates inside the close, which the deferral normally
prevents, and at the next open it settles from the panel's live position
(`win open continued from=... dur=...`) rather than replaying the keyboard settle
from 0. A close that took the panel to the hidden rest has nothing to continue
and settles from 0 as before.

## The close decision

Every shell close — the Windows key, the taskbar Start button, a click outside,
a display change and the lock screen — passes through `twinui.pcshell.dll`
`XamlLauncherState::HideStartAndCortana(this, method)` (RVA `0x24BCB0` on
10.0.26100.9949), and that entry is where the explorer-side bridge
(`mods/start-menu-touch-gesture-bridge.wh.cpp`, 0.17.0) decides. It is one frame
above `CStartExperienceManager::Hide`, where the 0.16.0 bridge used to hold: this
function writes the launcher's transient state (`5 -> 9`) and the view's `Hidden`
event completes it (`9 -> 0xA`), so a close that is refused here writes nothing,
never cloaks the window and leaves the launcher at 5, and a close that is held
here keeps the state at 5 for the whole hold. A hold on `Hide` instead stranded
the state at 9, which is still in `IsStartVisible`'s visible set, and that was
the dead Windows key.

The bridge probes the target before it installs that hook: the address must lie
in an executable section of the loaded image, the first 24 bytes must equal the
build's prologue, and the body must still carry the state write
(`5 -> 9`), the manager load (`[this+0x38]`) and its Hide slot (`+0x40`). A
failure logs its own `close hook skipped ...` line and leaves only that hook
out; the observation hooks keep working.

What the hook does, by method:

- The Start side's own Windows tap, named by a `tapped` close record that is
  fresh, newer than the attach watermark and not older than the open, is passed
  at once (`close passed ... reason=gesture`): its retract already ran, so the
  panel is gone. This is the gesture close, unchanged.
- Method 7 is the synchronous dismiss: `DismissSynchronouslyWithoutTransition`
  uses it for display changes and the lock screen, and the measured click-away
  enters with the same method through a delegate. It is **never held**, because
  a lock must not wait for a slide-out. It is decided from the Start side's
  standing reveal claim alone: a claim inside its 250 ms lease and bound to the
  menu on screen cancels the close (`close cancelled method=7 ...`), anything
  else passes it at once (`close passed method=7 ...`). A click-away therefore
  closes without the slide-out animation; that is the price of never delaying
  the lock screen, because both arrive as method 7 and the entry cannot tell
  them apart.
- Every other method (the Windows key is 1, the Start button 6 or 7 depending on
  the click path) publishes the `retract` request and waits up to 100 ms for the
  answer. A duration holds the original for that duration plus 30 ms
  (`hide delayed reason=... retract=... lead=...`, unchanged text); the
  no-settle sentinel or no answer passes at once (`hide passed reason=...
  retract=none|unanswered`); `cancel` returns without calling the original
  (`close cancelled method=... ...`).
- No channel, a failed request, no answer and a refused (stale) request all call
  the original at once. The 0.16.0 bridge held a fixed 560 ms lead when no answer
  arrived; the arbiter has no animation to serve then, so the fallback is
  immediate. A disabled or stopped Start mod therefore costs one 100 ms answer
  wait per requested close and no hang.

The observation hook on `CStartExperienceManager::Hide` stays for the unload
drain, the gesture-state clear and the `hide reason=...` line every Hide that
actually runs still writes. Its hold is gone: the pause lives at the decision,
and two holds would delay the same close twice.

## The keyboard close

A Win-key close requests that retract and holds the launcher's decision: the
Start side plans the settle (409 ms for a 900 DIP panel at 2.2 DIP/ms), answers
with its duration on the same channel, and the bridge waits that plus 30 ms
before it calls the original. The request carries the open generation the bridge
saw and a 700 ms deadline: a request whose generation is not current, whose
deadline has passed, or that the bridge already marked done is answered with the
no-settle sentinel and starts nothing. The bridge waits up to 100 ms on the plan
event; the request and the answer share the same section, and the answer echoes
the request's sequence number.

Measured with the menu fully open before the channel moved to shared memory:
the request was written at `23:15:09.588` and the answer at `23:15:09.654`
(66 ms, one 50 ms debug poll in its slow phase), the renderer logged `settle
... dur=409` and `debug retract from=1.000 dur=409`, the bridge logged `hide
delayed reason=1 retract=409 lead=439`, and the window was cloaked 549 ms after
the key-down, 50 ms after the settle it had answered with ended. On the channel
the answer needs no poll: the bridge measured `retract=520 lead=550` with the
request answered on the same event cycle.

Measured for the decision hook (0.17.0, 852 DIP work area, this machine), from
the key or click to the first frame in which the panel moves, and to the window
cloak: Win key 110 ms / 547 ms before, 110 ms / 500 ms after; Start button
141 ms / 563 ms before, 141 ms / 531 ms after; click-away 125 ms / 531 ms
before, 78 ms / 47 ms after. The first motion is the Start mod's retract starting
from the answer, so it did not move; the click-away is the one close whose
animation is gone, and the reason is the method-7 rule above.

The panel's own travel ends sooner than the settle's clock: its bottom edge
went 1351 → 870 → 640 → 448 → 150 px and left the screen 192 ms after the
key-down, 100 ms into the settle, because the fitted spring covers 70% of the
travel in 105 ms. Frames taken 350 ms after the key-down — before the old
350 ms lead expired — and at 500 ms are therefore pixel-identical to the closed
desktop: at the old hide instant nothing of the panel was left to cut on this
layout. The lead now follows the settle instead of a constant, which is what a
menu whose panel reaches closer to the screen edge needs.

A Win press during a gesture retract is a requested close: the plan is current,
so the request reaches the Start side, which drops the armed tap when it
answers, because that tap is what would toggle the menu back open. The earlier
design returned `S_OK` from the bridge's `Hide` without calling it; measured,
one such abort left the Windows key and the taskbar Start button dead until
`StartMenuExperienceHost.exe` restarted, so every path calls the original.

## The reveal claim

The reveal claim is the Start side's standing answer to "does a reveal contact
drive the panel right now", published without a request because a method-7 close
may not wait for one. It is one record under its own 32-bit seqlock in the
reserved words of the layout (`revealSeq`, `revealOpen`, `revealTick`), so
`sizeof(SwipeIpcState)` stays 672 and the channel stays v2:

- `revealOpen` is the low 32 bits of the open tick the reveal belongs to, or 0
  when no reveal drives the panel. The open tick is the one the Start side
  published as the menu's open, and the bridge compares it with the one it read,
  so a claim of the previous menu cannot cancel a close of the next one.
- `revealTick` is the low 32 bits of `GetTickCount64` at the last refresh. The
  Start side refreshes the record from every report and every idle tick, at most
  once per `kSwipeRevealClaimRefreshMs` (50 ms), and writes `revealOpen = 0` when
  the reveal ends. The bridge treats a record older than
  `kSwipeRevealClaimLeaseMs` (250 ms) as no claim, so a Start side that stopped
  refreshing cannot wedge the launcher's close.

The record is written by the Start side's input thread only and read by the
bridge without a wait. Its value is the same `g_reveal.active` the request path
answers `cancel` on, so the two decisions cannot disagree about what "a reveal
owns the panel" means.

## The bridge's unload drain

On unload the bridge keeps new closes out and waits for every counted close-hook
frame to leave before it closes the v2 channel; the wait has no deadline and one
exit. The count is a `CloseCallLease` (RAII, exactly one release per frame, taken
before the hook reads anything) that spans the original call, and it covers both
close hooks — the decision on `HideStartAndCortana` and the observation on
`Hide` — so a count of zero also means no **counted** frame of either hook can
still need the MinHook trampoline; a thread already redirected into a detour but
paused before its first increment is outside any counter the hook body can keep,
and that window belongs to the host's hook-removal boundary. Zero logs `ipc
unload drained waited=... ms`, closes the v2 channel, logs `ipc close` and
returns; that is `drained`. 500 ms is where the wait first reports a call that is
still inside, once (`ipc unload drain over 500 ms closes=...`), and every 10 s
after that it reports `ipc unload stalled closes=... waited=... ms`; `stalled` is
a periodic status, not an outcome: the same unload can report it and then reach
zero and log `drained`. Both reports are diagnostics; neither ends the wait. A
close that does not return, because the shell call under it never came back or a
debugger suspended its thread, blocks the Windhawk reload until it resumes: that
is the price of never handing the host a trampoline a counted frame can still
call.

## The gesture-state publication

The bridge also publishes the taskbar-edge gesture handle/state from
`twinui.pcshell.dll` into `Local\WindhawkStartGestureShared`
(`mods/start_gesture_shared.h`, a seqlock over state, edge, gesture handle and
points), which the shipping Start mod does not read; the consumer probe
(`start-menu-touch-gesture-consumer.wh.cpp`) maps it read-only and logs the
transitions, which is how that publication is verified end to end. The mapping
stays for the explorer process lifetime on purpose, because a consumer and a
late gesture callback may still hold its view.
