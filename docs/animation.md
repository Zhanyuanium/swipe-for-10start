# Animation

> The executor's `show`/`dismiss` schedule runs on the bridge; every animation
> below runs in `StartMenuExperienceHost.exe` through the panel's `Progress`.

## The settle spring

Every outcome — keyboard open, gesture release, intent close, reopen, heal —
settles on one damped spring. `AnimationStyle` picks the damping:

| Style | ζ | Source |
|---|---|---|
| `Windows 11` | 1.72 | fitted to the native Quick Settings flyout on this machine |
| `Windows 10` | 0.9 | the shipped legacy feel |

The duration is planned from the travel, not from a clock:
`T = clamp(|target - start| * distance / 2.2 DIP/ms, 160 ms, 520 ms)`. An 852 DIP
panel takes 387 ms; the bottom docked 900 DIP panel 409 ms. The curve is
generated as samples (`BuildSpringSamples`) and normalised by its own value at
`T`, so it lands exactly with no step. The first keyframe (up to 4 ms) shows the
first segment's average speed, not the analytic seam.

- **Start speed** — both styles start at the component of the finger's end speed
  toward the target, measured from the newest stored position (at most 30 ms old,
  pair span 8–30 ms). `GestureStartSpeedPercent` scales it. The normalisation
  divides by 0.99249 (`Windows 11`) or 0.99589 (`Windows 10`) at zero initial
  slope, so the seam factor is computed, not constant.
- **Open** — a requested open starts the settle from the entry this side held
  (`win open intent from=... dur=...`); an unowned shell show adopts the resting
  value and starts no settle (`win open adopted reason=unowned`). A touch show is
  the reveal takeover: chase at 2.2 DIP/ms, then follow. A pull that interrupts a
  close binds to the position the transfer took and chases the finger minus the
  offset the retract had (`live=1`).
- **Close** — `BeginShellCloseSettle` is the open spring in the closing
  direction: `value(i) = start * (1 - openY(i))`, final sample 0, full travel, no
  seam speed. It is the one shape for the Win key, the Start button, the
  click-away and the intent close; a gesture close keeps its own release spring.
  The dismiss is published at `settle + 30 ms` (intent) or at the settle's end
  (gesture), so the panel is never hidden mid-slide.
- **Interruption** — `TakeProgressNow()` pins the live position and stops the
  animation; the next settle starts there (a close 172 ms into an open started at
  `from=0.542`; a Win press during a held touch started at `progress=1.000`). A
  qualified pull during a cancellable close takes the retract over once, with no
  settle between the cancel and the follow (`live=1`); only a press has no finger
  to bind to and settles back.
- **Coast** — after 8 ms without a report the panel continues linearly for 200 ms
  on the last measured speed; the next report pins it. The 8 ms start and the
  200 ms end are one-shot deadlines armed only while ahead, and a follow's start
  is the later of its 8 ms gap and its 50 ms entry guard.

No animation runs on the input thread: the settle, the coast and the chase are
composition animations, so the panel moves with the thread asleep. The thread's
clock exists only for the decisions around them ([input.md](input.md)).

## The line vocabulary

`remaining` on a close line is the full travel by design, not
`(1 - start) * distance`. `open follow` and `reveal takeover` carry `live=0|1`:
1 means the session took a live retract over and commits on the panel's position
instead of the travel. `motion sample` is the position and slope
`TakeProgressNow` reads at the takeover, taken once; a taken grab has exactly one
(`caller=pull`) and binds the follow to it. `phase` names the motion the value
came from (`settle`, `coast`, `rest`, `none`), `v` is the signed slope in Progress
per ms. `exec dismiss enter` carries the QPC instant taken immediately before
`Dismiss`, because `hr=` says only that the primitive returned.
