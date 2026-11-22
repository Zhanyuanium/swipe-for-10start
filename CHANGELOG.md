# Changelog

A tag is the release. Up to `v0.6.2` it named a pair of mods, each with its own
Windhawk `@version`; from `v0.7.0` it names one mod whose `@version` is the tag
without the `v`. The Start/Bridge columns below are historical: they describe the
pair that shipped before the merge.

| Tag | Start | Bridge | Channel | What it is |
|---|---:|---:|---:|---|
| `v0.8.0` | 0.8.0 | — | 232 B | a close that can be interrupted: one ownership record, one grab per contact, a press's open for a band pull, a landing that adopts no finger; an executor capability word |
| `v0.7.0` | 0.7.0 | — | 216 B | one mod, two processes: the collection's review answered (symbol hooks, no pin, deadline-driven wakeups, tap rejection, lean channel) |
| `v0.6.2` | 0.44.0 | 0.25.0 | v7 | M3.3: one controller, one executor |
| `v0.6.1` | 0.40.0 | 0.21.0 | v4 | intent direction from the controller state; no press lost after a gesture close |
| `v0.6.0` | 0.39.0 | 0.20.0 | v4 | `PanelController` owns the toggle; the bridge executes `Show`/`Dismiss`; presses latched through a hide |
| `v0.5.0` | 0.36.0 | 0.17.0 | v2 | close arbitration at `XamlLauncherState::HideStartAndCortana`, reveal claim, cancel answer |
| `v0.4.2` | 0.35.0 | 0.16.0 | v2 | close/open ownership parity, deferred pulls, taskbar-click limit |
| `v0.4.1` | 0.34.0 | 0.16.0 | v2 | entry plus chase reveal, one open animation per pull, no window-shown dead state |
| `v0.4.0` | 0.33.0 | 0.16.0 | v2 | shared-memory bridge, taskbar-derived axis, reveal seam and late-release open |
| `v0.3.0` | 0.15.0 | 0.8.0 | — | speed-planned settle, exact landing, panel-rect dismiss guard |
| `v0.2.0` | 0.12.0 | 0.7.0 | — | dismiss and edge-reveal follow the finger on all four taskbar edges, scroll-aware vertical dismisses, one settle planner for both directions |
| `v0.1.0` | 0.6.5 | 0.3.0 | — | legacy Start touch gestures: swipe-to-dismiss and edge-swipe-to-open follow the finger |

`—` means the release predates the shared-memory channel.

## v0.8.0 — a close you can interrupt

Windhawk `@version` 0.8.0. The Win key, the Start button and the touch gesture
now interrupt the same transition the same way, and a close owns the panel from
its first frame to its hard landing instead of being one more animation a finger
can fight.

- **One ownership record.** A close is a serial, the menu it was accepted
  against, a phase (`settling`, `awaiting-hide`, `done`) and a 600 ms repair
  bound. Fifteen readers ask it instead of a clock. The bound's ending is a
  repair: it releases the published direction and leaves the dismiss command and
  the recovery ticket naming the hide that may still land, which is what the
  anti-heal rule waits on, so a slow hide (measured 562–593 ms) cannot land on a
  panel that was just restored under it.
- **A grab, once.** A down during a retract is admitted only when it lands in the
  band or on the surface the retract still shows — both halves of the panel
  rectangle, from the run's own geometry. It spends one transfer at 24 DIP: a
  take that reverses the retract from its live position in one take, with the
  finger's offset kept; a band pull whose surface is gone is the same open
  request a press makes, and follows the finger from the entry; anything else
  consumes the contact with no open right until it lifts.
- **A landing adopts no finger.** The landing serves only an explicit open
  request that lost its race to an entered `Dismiss`. A hand resting through a
  retract no longer resurrects the menu afterwards, and a consumed contact ends
  only on its lift or a report that proves another contact — never on the
  digitizer's quiet through a hide.
- **A capability, not a mapping.** The bridge publishes one `execCapable` word
  only while its worker, the executor window on the recorded launcher thread, the
  verified launcher layout and the show/dismiss primitives all exist. The Start
  side advertises, animates and commands only while that word is 1; a drop
  withdraws the peer, releases the panel and restores its rest. Launcher symbols
  are resolved once per module per install, with a terminal `unresolved` line,
  instead of a retry loop that could stall every press. The reference image is
  `twinui.pcshell.dll` fixed 10.0.26100.10068.
- **Two diagnostic lines.** `exec dismiss enter ... at=` dates the launcher call
  from before it was entered, and `motion sample` records the position and slope
  a takeover read. Both are written only with `DiagnosticTrace`.
- **A tracker for the evidence.** `tools/panel_edge_track.ps1` samples the
  panel's trailing edge for any of the four docks, which is how every claim above
  was judged in pixels rather than in logs.
- **The channel is 232 bytes.** `intentTaken` at 216 and `execCapable` at 224 are
  new in this release; the layout grows from the 216 bytes of the previous tag
  through 224 to 232. The `SingleMod3` namespace is the ABI identity: the tag
  names one mod built from one layout.

Still owed, and named in `docs/limits.md`: the top and right docks are
implemented but unmeasured on this machine, a pen by hand, and a consumed finger
that is silent through a landing and then moves (the virtual digitizer cannot
deliver that report).

## v0.7.0 — one mod, two processes

Windhawk `@version` 0.7.0. The pair is replaced by one mod, installed once and
loaded into both `explorer.exe` and `StartMenuExperienceHost.exe`; the role is
picked at init from the process image name, so the halves can never be installed
or updated separately.

Answers to the collection's review of the two submissions:

- **Symbols instead of addresses.** `StartUI::App::PrepareRootElement` and the
  thirteen `twinui.pcshell.dll` functions are resolved by public symbol on the
  mod's own threads, so the PDB fetch never blocks a host. When the private
  launcher layout does not match the reference image, the offset-dependent
  capability stays off and presses run natively.
- **No image pin.** Unloading joins every owned thread, runs the UI teardown turn,
  destroys the launcher and raw-input windows, and unregisters both fixed classes
  before the channel closes; there is no "residual" success.
- **No periodic tick.** Deadlines arm a single one-shot and the open/close state is
  a WinEvent with a bounded fallback. Idle with the menu closed measures about 9
  wakes/s and no DWM queries, down from about 190 wakes/s and 32 queries/s.
- **A tap does not open Start.** An in-band contact only records a candidate; the
  show is requested after 24 DIP of inward travel. The shell's own edge gesture
  remains the fallback.
- **A lean channel.** Five named objects and a 216-byte record; the version word is
  gone (the namespace is the ABI identity) and so is the debug command channel.
- **A user-facing store page.** The README block describes the gesture, the two
  settings and the limits; the design notes stay in this repository.
- **Close identity.** A landing is bound to its dismiss command and open
  generation; a confirmation that disagrees with the cloak sample is retained for
  the transition within a bounded wait, and a fresh open retires it.

The trace file is no longer written by default: every decision line goes to
`Wh_Log`, and the per-role file is written only while the `DiagnosticTrace`
setting is on (capped at 20,000 lines).

## v0.6.2 — M3.3

Start 0.44.0, bridge 0.25.0, channel v7 (744 bytes). Signed off after six review
rounds and a real-machine pass.

One `PanelController` owns progress, velocity, target and phase; every trigger is
an adapter over the same `Begin/Move/End/Commit`, so a key press is the
degenerate drag. The bridge runs the requested action on the launcher thread and
never writes the launcher's state.

- Cancelling an armed dismiss leaves its cancellation result available to Start,
  so a later unrelated hide is not mistaken for the close that press caused.
- A successful `Dismiss` call is not a confirmed hide: the executor owes a 300 ms
  visibility confirmation and reports `hidden`, `visible` or `unknown`.
- A record dispatched past its deadline is answered without calling the launcher.
- Interception requires a live Start peer and a proven executor: the recorded
  launcher thread, a window this activation created on it, a running worker and
  the callable primitive the decision needs. Otherwise the press passes to
  Windows, which then toggles exactly once.
- Ships with `tools/smoke.ps1`, the one-pass regression smoke over the four
  triggers; two of the review defects were caught by it and by nothing else.

Limits that are part of the contract: Windows owns when the first panel frame
appears (measured worst case 245–268 ms after `ShowStartView`, after which the
panel catches up to the finger); the 600 ms peer-loss detection budget is not a
delivery guarantee, so a press already intercepted can be lost if Start exits;
the panel's contents still receive touches at their original positions while it
is dragged. The real-machine pass covers one finger end to end, two fingers in
both lift orders and the auto-hide reveal after a fresh logon; a pen by hand
remains a manual check. See [docs/limits.md](docs/limits.md).
