# The settle — one damped spring for every outcome

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
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
takeover — starts from the owner's answer. `TakeVisibleProgressNow()` wraps it
for the settles that start while the shell's window may still be cloaked: the
recorded base of a process that has not shown the panel yet is the open rest (1),
and a hidden panel's only visible value for a settle is the off-screen rest, so
that sentinel answers 0. It is the settle rule; the live follow starts at
`RevealEntryProgress` instead and then chases its target (below), because a
follow must not spend the finger's travel on the hidden part of the motion.

A live takeover is the one interruption whose start is not a motion the panel
ever showed: the shell uncloaks the window 60-250 ms into the pull, so the
finger's absolute mapped position is 0.76-1.00. `ActivateRevealFollow` starts the
panel at the **visible entry** — `RevealEntryProgress`, the smallest progress
whose translated trail edge sits on the work area's edge), holds it for the first
presentation (`kRevealEntryHoldMs`, 30 ms) so the first visible frame is exactly
the screen edge, and from there each report moves the visible position toward
the absolute mapped position at `kSettleSpeedDipPerMs`
(2.2 DIP/ms, the planner's own speed) without passing it:
`visible += clamp(mapped - visible, -2.2*dt/Distance, +2.2*dt/Distance)`. The
entry rather than 0 matters because this is a follow: a settle spends time
crossing the hidden travel, but a follow would spend the finger's travel, and at
0 the panel sat one hidden travel behind the finger for the whole gesture. The chase rather than a constant subtraction matters because a
constant left the panel permanently short (0.25-0.27 visible at 450-560 DIP of
finger travel): with the chase the gap closes in `gap / 2.2` ms — 600 DIP in
~303 ms, 100 DIP in ~75 ms, the 30 ms entry hold included — and the panel then
stays glued to a finger that moves at 0.5-5 DIP/ms. The release settle starts from `TakeProgressNow()` — the
chased position at that instant — at the measured end speed, so a held 512 DIP
flick has already closed its gap and plans the 160 ms floor, while a fast flick
released mid-catch-up plans whatever travel is still left.

One owner is also a close's rule, not only a gesture's: while `CloseInFlight` is
true no path may take or write `Progress` (`ActivateRevealFollow` refuses, the
coast and every settle are skipped, the poll starts no open), and the position
the close's own retract reaches is the one the next open continues from. A pull
that lands in that window is deferred and writes nothing, so the retract's curve
is never interrupted by a second writer. Measured over forty close-then-pull
runs: one open path each, and the panel's top at the 526 px rest exactly 700 ms
after the lift.

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
trail mapping, in mapped units: the window belongs to the finger, and the chase
moves at the planner's rate instead of the finger's, so the two must not be
mixed. A menu that opens one or two reports before the lift still measures the
finger. Without the replay the window held one or two samples, the answer was 0,
and the panel started from rest under a moving finger; auto-hide made that
common, because the pull reveals the bar before the menu opens. Measured on this
build: a moving release reports `vel=0.71-0.75` DIP/ms, and a finger that rested
before the open still reports `vel=0.00`.

The same replay measures a pull that lifts before the menu opens, and the
closed→open path starts its settle at the speed that record measured: the
release measured it at the lift, over the same 8-30 ms window from the
same reports, so the freshness and span checks are the ones already described.
The position is the other half and it is not taken from the record: no animation
runs through the cloak, so the panel is still at the off-screen rest when the
window is presented, and `TakeVisibleProgressNow()` is where the settle starts —
the record's mapped position describes a panel that was never on screen. Starting
there made the first presented frame 82 % open. That settle and the live follow
differ on purpose: the settle has no finger and spends time crossing the hidden
travel, while the follow starts at the visible entry and spends the finger's
travel chasing the mapped position. Measured: ten fast pulls the shell opened
223-256 ms after the lift all started at `vel=4.00-4.48` DIP/ms, and a short slow
one at `vel=0.30`, so the seam is the finger's even when the menu arrives a
quarter of a second later. The open runs exactly one of four paths — live takeover, this record, the
contested continuation of a close whose Hide already left, keyboard settle — so
no open plays two animations or none. A pull that lands during a close does not
take `Progress` at all: it is deferred (its candidate keeps its travel, its speed
window and its record, and writes nothing) until the window is observed cloaked,
and only then does one of those paths run, from the position the panel has at
that instant. That is what keeps one owner: with the deferral in place a follow
never activates inside a close, so the ticket the contested continuation reads is
not stamped in the measured shapes and the path stays a guard for an open whose
panel the close did not take to the hidden rest (`win open continued from=...
dur=...`). Starting the keyboard settle at 0 over a panel that is already up was
the rewind that guard exists to prevent. The window
and what clears the record are in [gesture.md](gesture.md).

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
`win open dur=...`. An open the shell performs while a close this mod started was
still in flight is the one exception to "from rest": it logs
`win open during close progress=...` for the state it found, drops the
suppression, and settles from the panel's live position
(`win open during close from=... dur=...`), so the retract's last frame is
continued instead of replayed. One curve serves every open and every close, and
the travel plans the keyboard open's duration exactly as it does a gesture's:
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
