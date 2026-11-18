# The settle — one damped spring for every outcome

> Language: English only · 语言：仅此页为英文。用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). The gestures that start a settle are in [gesture.md](gesture.md); the close protocol that schedules the tap is in [ipc.md](ipc.md).

Every gesture outcome — dismiss, cancelled reveal, reveal commit, dismiss
cancel — and every keyboard open or close settles on one damped spring:

```
y(t) = 1 - e^(-zeta*omega*t) * (cos(wd*t) + ((zeta*omega - y0)/wd) * sin(wd*t))
y0   = (gain * velocity) / remaining      wd = omega * sqrt(1 - zeta^2)
```

That is the underdamped form. The default `Windows 11` damping 1.72 is
overdamped, so its response replaces `cos`/`sin` with `cosh`/`sinh` and uses
`wd = omega * sqrt(zeta^2 - 1)`; `Windows 10` at 0.9 uses the form above. Both
come from the same generator, which also carries the critically damped case
between them.

`y` is the normalised travel from the release position to the target and `y0`
its initial slope. The duration is planned from a **speed**, not fitted as a
time, so a larger panel takes longer instead of moving faster:

```
T = clamp(remaining / 2.2 DIP/ms, 160 ms, 520 ms)
```

with `remaining = |target - start| * distance`. The spring's frequency is
derived from `T` (`omega = 5 / (slowest root factor * T)`), so the curve keeps
its shape. That factor is `zeta` while the spring still oscillates and
`zeta - sqrt(zeta^2 - 1)` once it is overdamped, which is the default: five
time constants of the **slowest real root**, not of `zeta`, land on `T`. The
curve is then scaled by its own value at `T`, because five time constants leave
the analytic curve `1 - e^-5`, about 0.7%, short of the target. That remainder
spreads over the whole animation: the samples converge, the motion is 0.7%
larger, there is no step, and the final position is still exact. Writing the
target on the last keyframe hopped 3.8 DIP (7.6 px) after a frame that had
moved under 0.1 DIP, on a 501 DIP travel. Segments are sampled at most every
4 ms, so the polyline stays smooth at 120 Hz.

## Progress ownership

`Progress` has one position owner. `TakeProgressNow()` answers from the
recorded keyframe curve (or from the pinned coast value while the panel is
coasting — a 200 ms linear continuation that starts after 8 ms without a
report, pinned by the next report or ended by the release), pins that value as
the new base and clears the record. The value written as an animation's base is
the **target**, so while that animation runs nothing may read it as a position:
every interruption — retract, dismiss cancel, reveal takeover and drag
takeover — starts from the owner's answer.

## The two shipped parameter sets

Two parameter sets ship, chosen by the **Menu animation style** setting
(`AnimationStyle`, default `Windows 11`). Both run the same spring generator —
only the damping differs, so a style switch changes the feel and never the
mechanism:

- `Windows 11`: damping ratio 1.72, fitted on this machine to the native Quick
  Settings flyout (`Win+A`), which uses the same Fluent motion; the curve is
  stretched to the panel's travel, so the fit supplies the shape and the speed
  supplies the time. See "How the Windows 11 numbers were measured" below.
- `Windows 10`: damping ratio 0.9, the spring this mod shipped with.

The duration is not a setting: the travel left divides by the flyout's
2.2 DIP/ms and clamps to 160-520 ms, so a bigger menu takes longer instead of
moving faster. A Windows-key open runs the same settle from rest, so
`AnimationStyle` also decides how a keyboard open looks.

## Animation start speed

**Animation start speed (% of swipe speed)** (`GestureStartSpeedPercent`,
default 100, clamped 0-300) is the settle's start speed as a percentage of the
finger's end speed toward the target, over the travel left, in either style.
The end speed is two checks on the same report history. Freshness: the newest
stored position must be no more than 30 ms older than the release, or the
answer is 0, so a finger that moved, held with only asserted tip-down reports
and then lifted starts from rest instead of at the speed from before the hold.
Span: from that newest position backward, the oldest sample is the first one at
least 8 ms and at most 30 ms older than it, so the pair is 8-30 ms wide and may
lie up to 60 ms before the release when the stream paused just before the lift.
The change across that pair, projected onto the gesture axis, is what counts:
100 means the component that moves the panel toward where it is going, not the
raw release vector. The end-value normalisation adjusts that seam slightly: it
divides the whole motion by the curve's value at `T`, clamped to 0.5-1, and
that divisor depends on the damping and the initial slope. At zero initial
slope it is 0.99249 for `Windows 11` and 0.99589 for `Windows 10`, so 1.007 is
the Win11 example of the seam factor and 1.004 the Win10 one. The first
keyframe lands up to 4 ms into the curve, so the panel's first segment runs at
that segment's average speed, not at the analytic seam: the continuity with the
finger is approximate. A higher value snaps away faster, 0 starts from rest. A
finger held still before lifting reports `vel=0.00`, so it starts from rest
whatever the setting says; Windows-key opens and closes have no finger speed
either and start from rest too, so this setting does not change them — the
style does.

The reveal seeds that window at its takeover. `ActivateRevealFollow` resets it
and replays the contact's own last eight positioned reports through the same
trail mapping before the first insert, so a menu that opens one or two reports
before the lift still measures the finger. Without the replay the window held
one or two samples, the answer was 0, and the panel started from rest under a
moving finger; auto-hide made that common, because the pull reveals the bar
before the menu opens. Measured on this build: a moving release reports
`vel=0.71-0.75` DIP/ms, and a finger that rested before the open still reports
`vel=0.00`.

A release that is moving away from its target is honestly 0. The component is
projected onto the settle direction and clamped at 0 (`TowardTarget`), so a
pull-back whose last move goes toward the taskbar starts the settle from rest
even when the finger was fast. The diagnostic line proves the sign rather than
the loss: `vel window count=... d=-0.02778 raw=-0.0010` is a window whose
progress fell 0.02778 over its 27 ms span, a slope of -0.0010 progress per
millisecond, and the clamp turns that into `vel=0.00`. That is the rule, not a
missed window. The line is behind `g_debugVelocity`, false in the shipping
build.

## The keyboard path

The keyboard path runs the same settle: a closed→open transition that no
gesture owns calls `BeginGestureSettle(0, 1, 0)`, the gesture open from rest,
so a Win-key open logs `settle style=... dir=open vel=0.00 dur=...` and then
`win open dur=...`. One curve serves every open and every close, and the
travel plans the keyboard open's duration exactly as it does a gesture's:
520 ms at the 1440 DIP travel of this layout, 409 ms for the 900 DIP panel.
`AnimationStyle` applies to it unchanged; `GestureStartSpeedPercent` has no
finger speed to scale, so the keyboard open always starts from rest. A Win
press and an edge pull run one curve, not two. What a keyboard *close* does —
the retract request and the bridge's hold — is in [ipc.md](ipc.md).

## How the Windows 11 numbers were measured

`tools/native_flyout_frames.ps1` captures the native Quick Settings flyout
(`Win+A`) as 32bpp frames; `tools/native_flyout_track.py` fits a vertical
offset of each frame against the settled frame (Pearson `r >= 0.999` over the
card's interior). At capture time the taskbar was on top, so
`ControlCenterWindow` (768x1704 physical) opens anchored to the **top**-right
corner and its content unfolds **downward**: the leading edge is the panel's
*bottom* edge, not the top one, and `dy_hidden = 1252 - 96 = 1156 px` (578 DIP)
is the whole travel.

Sampling was 9.2 ms per frame (a 200 px wide strip, one `BitBlt` into a reused
DIB), and that capture stamped `t` after the `BitBlt` and its copy, so the
times below are late by the read-back (a few ms); the current tool stamps
before the `BitBlt` and records the copy's cost as `cap=`. `DwmFlush` before
each `BitBlt` costs a whole compositor interval on top of the ~8 ms a read-back
already takes, which dropped the rate to 17 ms; the read-back returns the last
presented frame anyway, so the offset to the presented content is 0-8 ms.
`IsWindowVisible` on `ControlCenterWindow` (not `DWMWA_CLOAKED`, which stays 0)
was polled every 1 ms as the time reference.

Normalised displacement, open, `t` from the window becoming visible:

| t (ms) | d | note |
|---|---|---|
| 0 | 0.00 | window visible, panel still above the window top |
| 10.3 | 0.00 | still absent |
| 29.7 | 0.54 | first visible frame |
| 37.5 | 0.60 | |
| 45.7 | 0.65 | |
| 55.9 | 0.69 | |
| 72.1 | 0.76 | |
| 89.8 | 0.81 | |
| 104.7 | 0.85 | |
| 120.4 | 0.89 | |
| 137.6 | 0.92 | |
| 153.9 | 0.94 | |
| 171.4 | 0.96 | |
| 187.2 | 0.97 | |
| 204.1 | 0.98 | |
| 220.1 | 0.99 | |
| 228.8 | 0.993 | |
| 253.0 | 0.999 | last frame that still moved (1 px) |
| 261.6 | 1.000 | at rest |

Duration from the window becoming visible to rest: 262 ms. No overshoot in
either direction (`dy` decays monotonically to 0). The close is the mirror in
kind but not in timing: the panel accelerates upward and is gone 135 ms after
its first moving frame (0.04 at 25 ms, 0.19 at 75 ms, 0.59 at 122 ms, hidden at
~137 ms); no overshoot.

Fitting the mod's spring with `y0 = 0` to this curve (area between the curves
over 0..306 ms) gives **period 102 ms, damping 1.72**: area 10 ms of
displacement, mean `|y - d|` 0.034, and at most 0.012 after the first 90 ms.
The second open run (17 ms frames) fits 84 ms / 1.98 with the same 247 ms
duration, so the ridge is real: the data fixes the decay time (about 50 ms) and
the duration (about 250 ms), not the pair's exact split. The native curve is
faster than any `y0 = 0` spring in its first 30 ms (0.54 of the travel against
0.38); the shell starts that animation with its own initial velocity, which a
settle that must continue the finger cannot copy.

Initial slope at a release half open (`Distance` 1440 DIP, finger 0.4 DIP/ms):
the spring (either style) moves the panel at 0.4 DIP/ms, 4.0 DIP in the first
10 ms. The Windows 11 style's earlier fixed 300 ms eased pair had an initial
slope of 9x its average, i.e. 21.6 DIP/ms and 216 DIP in the first 10 ms — that
is the jump a release at half open produced, and the reason that style now runs
the spring.
