# Changelog

A tag is the release: it names the pair of mods that shipped together. Each mod
carries its own Windhawk `@version`, shown in Windhawk's mod list, and the two
numbers move independently of the tag. Install both mods from the same tag: the
shared-memory channel version refuses a mismatched pair, and the bridge then
serves nothing.

| Tag | Start | Bridge | Channel | What it is |
|---|---:|---:|---:|---|
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
