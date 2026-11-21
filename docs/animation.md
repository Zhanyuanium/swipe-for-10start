# Animation

> M3. The executor's `show`/`dismiss` schedule is on the bridge; every animation
> below runs in `StartMenuExperienceHost.exe` through the panel's `Progress`.

## The one settle spring

Every outcome — keyboard open, gesture release, intent close, reopen, heal —
settles on one damped spring. `AnimationStyle` selects the damping:

| Style | ζ | Source |
|---|---|---|
| `Windows 11` | 1.72 | fitted to the native Quick Settings flyout on this machine |
| `Windows 10` | 0.9 | the shipped legacy feel |

The duration is planned from the travel, not a time:

```
T = clamp(|target - start| * distance / 2.2 DIP/ms, 160 ms, 520 ms)
```

A 852 DIP panel takes 387 ms (bottom auto-hidden; the bottom docked 900 DIP panel
takes 409 ms; the earlier bottom auto-hidden variant also measured 852 DIP and
387 ms). The curve is
generated as samples (`BuildSpringSamples`), scaled by its own value at `T`, so
it lands exactly on the target with no step.
The first keyframe, up to 4 ms in, shows the first segment's average speed
instead of the analytic seam.

## Start speed

Both styles start at the component of the finger's end speed toward the target,
over the travel left, projected on the axis. The end speed is measured from the
newest stored position, which must be at most 30 ms old; the pair spans 8–30 ms,
so a hold whose only reports assert the tip leaves no speed and starts at 0.
`GestureStartSpeedPercent` scales that component; the curve's end-value
normalisation divides by 0.99249 (`Windows 11`) or 0.99589 (`Windows 10`) at zero
initial slope, which is why the seam factor is a computed value, not a constant.

## The open

- keyboard/Start-button open: the settle from rest (`vel=0.00`) to 1, `dur=409`
  on the full travel;
- intent open: the Start side holds the entry (`RevealEntryProgress`, warm
  0.255 bottom auto-hidden, 0.348 bottom docked, cold 0.000) and starts the same
  settle from there when the shell's
  shown event arrives (`win open intent from=... dur=...`). That event is
  `EVENT_OBJECT_UNCLOAKED` for the CoreWindow, delivered to the input thread —
  not a poll. Measured on this build, the settle line lands 31–47 ms after the
  `exec show` ack, which is the shell's own uncloak, not the mod's wake;
- touch: the reveal takeover starts at the entry and chases the mapped finger at
  2.2 DIP/ms, then follows. The rate limit is the planner's own speed; the cost
  is time (a 0.326 gap closes in ~148 ms), and the visible position never
  overshoots the finger. A pull that interrupts a close does not start at the
  entry: the panel is already presented, so the takeover begins at its live
  position, holds nothing, and chases the finger minus the offset the interrupted
  retract had (`reveal takeover ... live=1`).

## The close

`BeginMirrorCloseSettle` is the open curve played backwards from the live
position: the same spring sampled in reverse, full travel, no seam speed, so the
close starts almost still and accelerates into the edge. It is the one close
shape for the Win key, the Start button, the click-away and the intent close.
The gesture close keeps its own release spring (`BeginGestureSettle`) and uses
the mirror only for the shell-triggered family.

At the end of the close the Start side publishes `exec dismiss` at
`settle + 30 ms` for an intent close, or at the settle's end for a gesture close;
the bridge arms a timer (or calls it at once) and calls `Dismiss` on the
launcher's thread. The panel is therefore never hidden mid-slide.

## Interruption

`TakeProgressNow()` pins the live position and stops the animation. The new
settle starts there: a close 172 ms into an open started at `from=0.542`; a Win
press during a held touch started the close from `progress=1.000`; a press during
a close drops the local dismiss slot, cancels the pending executor
command, and settles the panel back from its live position
(`intent open cancel from=...`). A qualified pull during any close this mod still
owns does the same thing and then hands the panel to the follow: the retract stops
where it is, the follow takes over at that position (`live=1`) with the
finger-to-panel offset it found, and its chase starts on the same report instead
of waiting out the 30 ms entry hold. The one close a pull cannot take over is one
whose `Dismiss` call has already returned and whose hide is unconfirmed; a
retract already at or below the entry is refused for the same reason (there is no
visible panel to take). A refused transfer consumes the contact, and the landing
serves no touch open, so the menu stays closed until a new pull. No path
replays from the old target.

## The coast

After 8 ms without a report the panel continues linearly for 200 ms on the last
measured speed; the next report pins the panel at its own mapped position. The
coast is the only animation besides the settle, and a release within it ends it
and plans the settle from the visible value. The 8 ms start and the 200 ms end
are both one-shot deadlines on the input thread, armed only while they are
ahead: with nothing moving there is no tick to start a coast from. A follow's
start is the later of its 8 ms report gap and its 50 ms entry guard, so the
armed wake is one `MaybeCoast` can use instead of one its guard refuses.

## One wake per deadline

No animation is driven by the input thread: the settle, the coast and the
reveal chase are composition animations, so the panel moves with the thread
asleep. The input thread's clock exists only for the decisions around them —
the release, the close tap, the executor budgets and the measurement turn — and
each of those is one one-shot deadline (see [input.md](input.md)). The "heal"
pass that brings back a panel left at the hidden rest is part of the visibility
service; after an open it runs from the settle watch's 100 ms polls, which end
when that watch does.

## Measured lines

```
settle style=win11 dir=close vel=0.00 dur=409 remaining=900 frames=104 zeta=1.72
settle style=win11 dir=open  vel=1.19 dur=333 remaining=733 frames=85  zeta=1.72
open follow travel=475 max=475 progress=0.91 behind=0.000 offset=-0.127 vel=0.00 commit=1
exec dismiss seq=4 armed in=439 ms
motion sample caller=intent-open-cancel phase=settle p=0.542 v=-0.006 qpc=184223117
exec dismiss enter seq=7 gen=18 epoch=1 at=184223189
```

The `remaining` in the close line is the full travel by design (the mirrored
curve), not `(1 - start) * distance`. The `open follow` line gained one field
after `commit=`: `live=0|1`, where 1 says the session took a live retract over and
its commit reads the panel's position instead of the travel. The excerpt above was
captured before the field existed; `reveal takeover` carries the same field, so
the two lines of one session agree about its start rule.

Two lines date an interruption on its two sides, one in each process.
`exec dismiss enter` is written after the launcher call returned, like the
`hr=` line beside it, so it can carry the QPC instant taken immediately before
`Dismiss` was entered (`at`, the same millisecond clock as `qpc`): `hr=` says
only that the primitive returned, never when it entered, so it cannot show
whether a cancel or a fresh press reached the panel before or after the hide
began. `motion sample` is the position and slope `TakeProgressNow` reads at the
takeover, taken once under `g_compositionMutex` with the one `qpcNow` it uses.
`phase` names the motion the value came from — `settle`, `coast`, `rest` with
nothing recorded, `none` without a renderer — and `v` is the signed slope in
Progress per ms, `0.000` once the curve has landed.
