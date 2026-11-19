# Gesture model — the two state machines and their thresholds

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). The tracked contact these machines run on is defined in [input.md](input.md); the settle they commit into is defined in [animation.md](animation.md); the panel geometry they read is defined in [geometry.md](geometry.md).

A state machine on the raw-input thread drives a `Progress` scalar in a
`CompositionPropertySet`; an expression translates the root visual by
`(1 - Progress) * Direction * Distance`. `Progress` is written whenever the
finger moves 0.5 DIP, so the drag is continuous rather than stepping on a
progress grid. Two gesture families run on that scalar: the **dismiss** (drag
the open menu toward the taskbar edge) and the **reveal** (edge swipe that
opens the menu and then follows the finger).

## Commit thresholds

On release the dismiss commits when the release position passed 20% of the
distance (10% on a vertical axis, where the same drag also scrolls the area
under the press) or the session-average speed exceeded 0.45 DIP/ms; a commit
animates `Progress` to 0 and arms a `VK_LWIN` tap (down 60 ms, up) for the end
of that retract. That tap is the shell's own Start toggle: its close is the one
the bridge's decision passes as a gesture close, so the next Windows press opens
the menu and no re-arm tap exists. Every outcome — dismiss, cancelled reveal,
reveal commit and dismiss cancel — runs one settle: a damped spring that starts
at the component of the release speed toward the target, measured over the
widest eligible window of the last reports — 8-30 ms wide, taken backward from
the newest stored position — projected onto the gesture axis and divided by the
travel left (the full settle model is in [animation.md](animation.md); a
reveal seeds that window from the contact's own reports at its takeover, below).
The commit test above deliberately keeps the whole-gesture average: only the
settle's seam reads the end of the gesture, which is why a finger that dragged
and then rested starts from rest instead of at the speed of the drag.

The reveal and the dismiss share the 20% (10% vertical) travel and the
0.45 DIP/ms session-average thresholds, but their visual anchors differ (panel
edge vs. drag start), so the same physical travel reads as a different
`progress` in each; the decision uses travel, not progress.

## The reveal

The left-edge swipe is Windows' own gesture: `twinui.pcshell.dll`
`TouchEdgeGestureProcessor` calls `CStartExperienceManager::Show(..., 16)` and
`StartUI_.dll` starts its own reveal animation. ExplorerPatcher is not
involved.

The mod cannot suppress that animation: hooking
`StartUI::SplitViewFrame::OnGlobalAnimationRequested` and skipping the `type=0`
request also skips presenting the panel, which leaves a window that DWM reports
as shown but that draws nothing. The hook is installed on every load and passes
every request through: only the debug command `anim=0` drops the open request,
and the switch starts at `anim=1`, so the default never suppresses anything.

Instead the mod takes over after the shell opens the window:

- A touch that starts inside the taskbar's own band while the menu is closed
  becomes an edge candidate. The band is the bar's measured strip plus one bar
  thickness toward the desktop — `804..900` on this bottom auto-hidden layout,
  the same in both hide states because the hidden bar still measures its 48 DIP
  — and the fixed 12 DIP at the screen edge is only the fallback while no
  snapshot carries a band, or when the taskbar probe names no edge it can
  measure. Raw Input sees the contact from the first report, before the shell
  opens anything. One line speaks per contact that lands in the band
  (`reveal admit ... ok=... reason=admitted|renderer|layout|distance|open`),
  naming the test that refused it when it was refused; a contact outside the
  band stays silent.
- The shell's own edge gesture opens the menu from about 20 DIP inside the
  screen, which the fixed strip did not cover: such a pull reached the menu
  with no candidate, the shell opened it, and the keyboard settle ran from 0.
  The taskbar band covers that strip, and a pull that still arrives without a
  candidate is adopted: a contact that began while the menu was closed and has
  travelled at least 24 DIP along the opening axis takes the open over when the
  menu appears under it (`reveal adopt ...`), continuing from the panel's live
  position at the finger. A tap has no travel and is not adopted; a contact
  that began on the open panel is a dismiss and never adopts.
- When the menu opens under a contact, exactly one of four paths runs, in this
  order:

  1. **Live takeover.** A contact still down that has travelled
     `kAdoptTravelDip` (24 DIP) along the opening axis takes the open over: a
     candidate is promoted at the open (`PromoteRevealContact`) instead of
     waiting for a report it may never get, and a contact with no candidate is
     adopted (`reveal adopt ...`). Either way the follow starts at the visible
     edge and then chases the finger, and logs
     `reveal takeover from=... offset=...`: `from` is the position the panel
     shows at that instant — the visible entry while the shell's window was
     cloaked, the live position if it was already above the entry — and `offset`
     is the gap it inherited (`mapped_at_activation - from`). The entry is held
     for the first presentation (`kRevealEntryHoldMs`, 30 ms), so the first
     visible frame is the edge, and then each report moves the visible position
     toward the absolute mapped position at `kSettleSpeedDipPerMs` (2.2 DIP/ms)
     and never past it, so the panel closes the gap in `30 + gap / 2.2`
     milliseconds (600 DIP in ~303 ms) and then stays glued; the
     absolute mapping itself keeps measuring travel, the commit test, `max=` and
     the end-speed window. A constant subtraction is not used: it left the panel
     permanently short and unable to pass `1 - offset`. A
     tap, or a finger resting at the edge, has not travelled 24 DIP and is not
     promoted.
  2. **Late release.** No takeover ran and the pull already lifted: the
     `RevealLateRelease` record below carries it across the open. The record is
     read even when a new contact is already down, because that contact cannot
     be the pull that opened the menu and may be refused on its next report.
  3. **Contested continuation.** This open follows a close whose `Hide` already
     left with the panel above the hidden rest: the settle starts from the
     panel's live position, the close's own settle value, and not from 0
     (`win open continued from=... dur=...`). The ticket is one commit release
     old (300 ms) and a panel the close took to the hidden rest does not qualify.
     With the close's deferral in place no follow activates inside a close, so
     this is a guard, not the routine reopen: a pull that waits for a close is
     deferred and takes over live (or late) after the landing.
  4. **Keyboard settle.** None of the three preceded the open, so it plays from
     rest.

  The suppression test is that actual takeover — `gestureOwnsProgress` is
  `g_reveal.active` — never the existence of a candidate, so a candidate the
  open never reaches cannot take the animation away. The fixed order means a
  fresh open runs one animation, never two and never none, and a settle whose
  start already equals its target inserts that value for every keyframe, so a
  `remaining=0` no-op moves nothing.
- A fast pull can also end before the shell opens the menu. On this machine the
  closed→open transition landed 107-251 ms after the release — 242-251 ms for
  ten fast pulls, 107-117 ms for short slow ones — and by then the candidate had
  ended (`reveal candidate released unopened`), so no contact was left for the
  live adoption to take. A released candidate that travelled at least 24 DIP
  leaves a `RevealLateRelease` record for `kLateRevealWindowMs`: the release
  QPC, the travel, the mapped position the follow would have written (a
  diagnostic only), the end velocity measured at the release from the same
  eight-report replay the takeover uses, and the pull serial. That window is
  300 ms — the measured range plus margin; an earlier 120 ms guess expired at
  124 ms, before the open it existed for. The open consumes it and starts the
  settle at the position the panel actually shows — `TakeProgressNow()`, which
  is the off-screen rest while the menu is hidden, because neither the follow
  nor any settle runs through the cloak — at that velocity: one line
  `reveal adopt late travel=... vel=... age=... gen=... from=... pos=...`
  (`vel` in DIP/ms, `from` the live position the settle starts from, `pos` the
  mapped position the follow would have written, `age` the milliseconds since
  the lift). Measured with the open arriving after the lift on this machine:
  the compositor held `from=0.000` while the record said `pos=0.603`, so
  starting the settle at the mapped position made the first presented frame
  82 % open — the owner's teleport. Ten fast pulls now enter the screen at
  top≈1728-1768 px and reach the 520 px rest in ~370 ms, at
  `vel=4.15-4.48` DIP/ms and `age=241-292`; a plain keyboard open still logs
  `win open dur=409` from rest and no adoption line. The activation reset, a
  live takeover, an admitted newer pull, the consumption and the 300 ms window
  invalidate it; a new down does not, because the contact that arrives first
  cannot be the pull that opened the menu. An open that finds none runs the
  keyboard settle from rest as before.
- Ten interleaved pulls — fast pull, lift, 200 ms, second touch at the edge —
  split across the paths: five consumed the record (`age=204-228`), two
  promoted a live candidate, and none opened without an adopt, a late record or
  a takeover; no `remaining=0` no-op appeared.
- A candidate that has not travelled 24 DIP does not own the open, so a tap on
  the taskbar still starts the keyboard settle (`win open dur=...`, 409 ms on
  the 900 DIP panel).
- A candidate the shell never opens for — a start deeper than the shell's own
  threshold — moves nothing and ends with one line,
  `reveal candidate released unopened travel=... axis=... vel=... pos=...`:
  `vel` is its end speed toward the open in DIP/ms and `pos` the mapped position
  the follow would have written first; the travel and the speed are what the
  closed→open path consumes within 300 ms, and `pos` stays a diagnostic. A
  release with no measured distance to map it through logs the
  bare `travel=... axis=...` and carries nothing; below 24 DIP of travel the
  line stays silent, so a tap is silent. Measured: a pull from 60 DIP inside the
  screen logged `travel=448`, and no keyboard settle played because the menu
  never opened under it.
- Once the menu is open and the contact is still down, the trail mapping reads
  the finger against the panel's trailing edge:

  ```
  mapped = 1 - (finger - panelTrail) / (direction * distance)
  ```

  The trailing edge is the panel side facing away from the taskbar — the one
  that meets the finger. How it is measured, and the band it is accepted
  against, is in [geometry.md](geometry.md). With that anchor, a panel that
  tracks the finger meets it: measured within 0.5 DIP residual at 150, 450 and
  800 DIP on the left taskbar, and within 5 DIP on the bottom. The follow starts
  at the **visible entry** (`RevealEntryProgress`: the smallest progress whose
  translated trail edge sits on the work area's edge, derived from `Distance`,
  the measured trail and the work area, not a constant), not the off-screen rest,
  holds it for the first presentation (30 ms), and then **chases** `mapped` at
  `kSettleSpeedDipPerMs` (2.2 DIP/ms) without passing it, so the gap closes in
  `30 + gap / 2.2` ms. A settle may start at 0
  because it spends time crossing the hidden travel; a follow spends the finger's
  travel, so it starts at the entry, and a constant subtraction is rejected
  because it left the panel permanently short. The mapping keeps its other jobs
  unchanged: travel, the commit test, `max=` and the speed window all read
  `mapped`.
- The takeover resets the end-speed window and then replays the contact's own
  last eight positioned reports through the same trail mapping, in mapped units:
  the window is the finger's, and the chase deliberately moves at the planner's
  rate instead of the finger's. Without that replay a takeover one or two reports
  before the lift left a window of one or two samples, the answer was 0, and the
  panel started from rest while the finger was still moving; auto-hide made it
  common, because the pull first reveals the bar and the menu opens late.
  Measured on this build: a moving release reports `vel=0.71-0.75` DIP/ms, and
  the same pull with auto-hide off reports 0.75 from the screen edge and 0.71
  from 20 DIP in. A finger that rested before the open repeats one value, so the
  window stays flat and the release is `vel=0.00`.
- A contact that starts inside the band while the menu is open is the tail of
  the pull that opened it, not a drag of the panel: once it has moved
  `kBandTailTravelDip` (8 DIP) away from the taskbar it starts no dismiss and
  takes neither `Progress` nor a settle, and one line at most once per 500 ms
  says so (`dismiss skipped band tail travel=...`). The test is the net travel
  from the contact's own down point along the opening axis, so only a return
  within 8 DIP of that point clears it; a press that starts on the panel takes
  the ordinary dismiss path from its first sample, where that travel is 0, so
  the mute never takes a real drag.
- A vertical dismiss (top or bottom taskbar) yields to the menu's own
  scrolling, and the menu scrolls in more than one place. The gate asks the
  region under the press, not one cached viewer: a downward press inside a
  scrolling region only starts a dismiss when that region is already at its
  end, and the same contact starts the dismiss as soon as its own scrolling
  reaches it. The app list and the tile grid are both regions, so a drag in the
  tiles scrolls them instead of closing the menu; a press on the panel
  background, the rail or the taskbar strip is on no region and dismisses. The
  survey and its three states are in
  [geometry.md](geometry.md#the-scroller-gate).
- Release is decided symmetrically with the dismiss gesture: `travel` is how
  far the finger moved away from the edge since the touch began (the release
  value, not the peak), and the menu opens fully when `travel > 20%` of the
  distance (10% on a vertical axis) or the session-average speed exceeds
  0.45 DIP/ms. Otherwise the mod retracts the panel and taps the Windows key at
  the end of that retract, so a short slow pull, or pulling out and pushing
  back, does not open the menu; the bridge passes that Hide at once because the
  `tapped` record says the panel is already gone. The settle starts at the
  position the panel actually shows (`TakeProgressNow()`, the chased visible
  value) and at the end-window component toward the target, not at that average:
  a live takeover that held before lifting settles from wherever the chase had
  reached (a held 512 DIP flick has already closed its gap, so `remaining` is the
  last 112 DIP and the planner gives the 160 ms floor), a short pull that
  stopped before lifting settles from rest, and a pull-back whose last move goes
  toward the taskbar is clamped to 0 for the same reason (the `vel window`
  diagnostic shows that negative slope).
  The release line prints that visible progress and the offset it was rebased by
  (`open follow ... progress=... offset=...`); `travel` and `max` stay the
  finger's real motion.

An earlier version anchored progress to the frame in which the menu was first
seen open and used the root width as the distance; that lagged the finger by
565 DIP and made the commit decision depend on swipe speed. Both are fixed.

## What a close does to a gesture

A close of either family runs the same ownership transition: suppression window
opened, the dismiss and reveal sessions ended, a contact still on the glass
muted so its release cannot settle the panel open, the 1 ms timer period
released, and — for a keyboard close — an armed tap dropped, because the Hide
it serves would otherwise toggle the menu back open. The mute is read from the
contact latch and either gesture family before either owner is cleared, so a
keyboard close that lands while the reveal owns the finger mutes that contact
too. Without that state the next report of the same contact dragged the panel
back and its release settled the menu open over a close that was already
running.

A close exists once its settle starts. A close owns `Progress` from then until
the window is observed cloaked (`win close observed`), at most 600 ms;
`CloseInFlight` is that one answer, and every admission, takeover and settle
reads it. Two shapes, one owner each:

- **Cancel path.** A gesture close still has a `Hide` this mod can abort: its
  Windows tap is armed and runs at the end of the settle. The first report of a
  contact that lands during that settle drops the tap, whatever its point (a pull
  whose first point misses the band never reaches the candidate path, so the
  deadline check alone missed it), and the close is over before the shell is
  touched: no `Hide` runs, the toggle is never advanced, the suppression ends
  with the drop and the panel comes back from where the close had it (an open
  settle from the live position, because a menu left open at `Progress` 0 has no
  pane). The admission names it (`reveal admit ... reason=cancelled`). This is
  the whole window in which the shell can still be spared the close, and — as
  [limits.md](limits.md) records — the only shape the shell can reopen from after
  that close's own gesture.
- **Deferral.** Every other close already has its close in the shell's hand: a
  keyboard close, a Start-button or click-away close (both arrive as the bridge's
  retract request, and are refused instead while a reveal owns the panel), or a
  gesture close whose tap already fired. A pull that lands there is recorded, not
  admitted:
  `reveal admit ... ok=1 reason=deferred`, no `Progress` write, no pull
  generation, no cancel stamp, no mute, and `startedOpen = 0` while the close is
  in flight, so a pull that starts above the band is adopted from its own down
  point once it has travelled 24 DIP. The candidate is otherwise live: its
  reports keep its travel, its end-speed window and its liveness clock, and the
  close's landing releases it (`reveal defer released candidate=... down=...`).
  The open then runs the ordinary path — a live takeover from its own down point
  when the contact is still down, or the `RevealLateRelease` record when it
  lifted first, whose `age` starts at the landing rather than the lift
  (`readyQpc`), because the pull had to wait for the close it must not fight.
  `ActivateRevealFollow` refuses while `CloseInFlight`, so no follow can write
  `Progress` under a retract. That second writer was the flash, and whoever wrote
  last decided whether a replay followed.

A contact still on the glass when a close starts is muted instead (the mute is
read from the contact latch and either gesture family before either owner is
cleared), because that contact is the one the close interrupted, not the pull
that opens the next menu.

The close-contested ticket remains as the guard for an open whose panel the close
did not take to the hidden rest: `win open continued from=... dur=...` settles
from the live position instead of 0. The deferral normally prevents the follow
that stamped it, so in the measured shapes no ticket is stamped and the guard
stays silent. The close record that coordinates the tap with the explorer-side
bridge is in [ipc.md](ipc.md).

Measured on this build (bottom auto-hidden bar, 852 DIP travel, a 10 ms strip
capture of the panel's top edge at x 1900..2100 px): forty runs, ten per close
kind, a pull at +24..+219 ms after the close, fast (512 DIP in 130 ms) and slow
(512 DIP in 1200 ms). Every run ran one open path. Gesture kind: the cancel,
the window never cloaks (the cloak watch records one `0` for the whole run) and
the panel rises from 1511-1691 px to the 526 px rest with no reversal. Win key,
Start button and click-away: `reason=deferred`, `win close observed`, the
deferral released at the landing and `reveal takeover from=0.252` — the visible
entry — then the panel rises from 1577-1690 px to 526 px; 28 of 30 runs had no
reversal, and the two exceptions were one frame of 6 DIP at the end of a slow
pull, where the shell's own presentation settles onto the finger. Every run had
the top edge at the 526 px rest exactly 700 ms after the lift, and its `state
open=1 progress=1.000 motion=0` line says the live position is at rest with no
animation. The window's own bottom edge is 1704 px and the reopen's first
detected frame is 1577-1690 px, one or two 10 ms frames into a rise that the log
starts at the entry, so the panel enters at the edge and not part-way open. Four
more runs released the pull after the landing but before the shell's open: the
deferred record was consumed with `age=14-193` measured from the landing,
`from=0.000` and one open path (`reveal adopt late`), with the same one-rise
verdict and the 526 px rest.

The other half is the state itself: a window that is uncloaked with the panel at
the hidden rest is not an open menu, and no path leaves that pair standing. A
close whose `Hide` landed, or a reopen whose settle never ran, shows the window
with `Progress` 0: nothing of the menu is on screen, every pull is refused
`reason=open` (the window is shown) and muted as a band tail, and the menu stays
dead until a press on the panel runs a dismiss cancel. The visibility poll owns
the pair. Once the close's suppression has ended and no settle, coast, follow or
dismiss drag moves the panel, it logs `win open unhealed progress=...` and
settles the panel up from its live position. The band admission applies the same
reading: `reason=open` refuses only a panel at rest, and a pull that lands on the
shown window with the panel low is admitted as the reveal (`reason=unhealed`), so
repeated pulls cannot all be muted. The poll reads the live position with
`PeekProgressNow`, never `TakeProgressNow`, because it is also where the
late-release settle starts; taking that settle at its first millisecond pins the
hidden rest (measured: `progress take settle at=0.000 elapsed=0
caller=contested` right after `reveal adopt late`), which is exactly how the dead
state was made.
