# Changelog

A tag is the release. Up to `v0.6.2` it named a pair of mods, each with its own
Windhawk `@version`; from `v0.7.0` it names one mod whose `@version` is the tag
without the `v`. The Start/Bridge columns below are historical: they describe the
pair that shipped before the merge.

| Tag | Start | Bridge | Channel | What it is |
|---|---:|---:|---:|---|
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
