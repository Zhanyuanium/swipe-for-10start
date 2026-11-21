# Technical documentation

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。

This tree is the project's memory: what was measured, what was falsified, and
why each design decision was taken. The user-facing quick start is
[../README.md](../README.md) (English) / [../README.zh-CN.md](../README.zh-CN.md)
(中文).

## Verified environment

Everything here was measured, not assumed. Verified on: Windows 11 build
26300.9974, ExplorerPatcher 26100.9711 (`Start_ShowClassicMode=1`), Windhawk
2.0 alpha, 2880x1800 physical at 200% scaling (1440x900 DIP), ELAN2513
touchscreen. The taskbar was on the left for the early measurements and on top
for the vertical-path runs. **Current layout: left, docked** (`axis=0`), measured
2026-11-21: work `48,0,1440,900`, the bar at `0,0..48,900` (96 physical wide),
band `0..96`, panel `60,151..1053,726`, trail 1053, Distance 1392. The bottom
auto-hidden layout (work `0,0,1440,900`, the bar off screen, band `804..900`,
panel `236,265..1229,840`, trail 265, Distance 852), the bottom docked layout
(work `0,0,1440,852`, tray `0,852..1440,900`) and the earlier bottom auto-hidden
variant (panel `236,167..1229,840`, trail 167) are historical; their numbers are
never the current ones. Where a measurement depends on the placement, the text
names it.

## Map

| File | Topic |
|---|---|
| [architecture.md](architecture.md) | M3: the launcher read (`s_pLauncher` outer + `0x18` interface, `[+0x160]` state), the executor thread and the taskbar-thread crash, the one-decision/one-executor contract, the measured open/close/interruption chains, the accepted 245–268 ms cap, and the list of deleted M2 mechanisms. |
| [input.md](input.md) | Why Raw HID: the falsified data sources, the report decode (report IDs, `TipSwitch`/`InRange`, contact identity and its evidence clock, the coordinate-only fallback), release detection and its timers. |
| [gesture.md](gesture.md) | The `Progress` model, the dismiss and reveal state machines, the band and the once-per-open show request, the live takeover's visible entry and 2.2 DIP/ms chase, the one close path (`TakeProgressNow` → mirrored settle → scheduled `exec dismiss`), the grab's down hit-test and one transfer attempt (a refusal consumes the contact; the landing adopts no finger), the gates and the settle ownership. |
| [animation.md](animation.md) | The damped-spring settle, both damping regimes, the 2.2 DIP/ms duration plan, the start-speed seam, the mirrored close, the scheduled dismiss, and the measured lines. |
| [geometry.md](geometry.md) | The `GestureGeometry` snapshot and its lock discipline, the panel-trail measurement and band, the taskbar axis probe (including auto-hide), the deferred-measurement request, the scroll survey and gate, and the measured layouts. |
| [ipc.md](ipc.md) | The merged mod's shared-memory channel: names, 216-byte layout, seqlocks, the open generation, intents and acks, executor commands and acks with their admission vocabulary and expiry rules, the controller direction, peer liveness and activation identity (600 ms is a detection budget), and the init handshake. |
| [lifecycle.md](lifecycle.md) | Quiescing both roles before the image is unmapped: the Start join/UI-teardown/entry-drain order, the bridge worker join/window destruction/class unregistration order, the no-safe-success rule, and the host's unload boundary. |
| [testing.md](testing.md) | The PowerShell/Python probes and injectors, and the captured measurements in `artifacts/`. |
| [limits.md](limits.md) | Known limits and risks, what the real-machine pass covered, and what is still owed (a pen by hand, and the HID dismiss log excerpt). |
| [store.md](store.md) | How the mod is submitted to `ramensoftware/windhawk-mods`: the generated single-file form, the one-file rule, and the checks to run before opening the PR. |

## The mod

One mod ships, in one source with two process halves (full member list in
[../mods/AGENTS.md](../mods/AGENTS.md)):

- `mods/swipe-for-10start.wh.cpp` (0.7.0) — installed once; `Wh_ModInit` selects
  `start::Role` in `StartMenuExperienceHost.exe` or `bridge::Role` in
  `explorer.exe` from the host executable name. The `start` namespace holds the
  whole gesture, follow and animation, the controller that consumes executor
  intents and publishes executor commands, the closing direction it publishes
  before the settle's first frame, the open that cancels a close still on
  screen, the grab that aborts a cancellable close and takes the
  retract over from its live position, the landing that serves only the press
  obligation (no touch open), the
  one close path (a mirrored
  settle from the live position, then `dismiss`), the recovery ticket that
  exists only for a published command and is armed by one helper for both the
  press and the pull, and the peer liveness
  (intent-consumer readiness) the executor gates on.
- The `bridge` namespace runs in `explorer.exe`; it executes
  `ShowStartView`/`Dismiss`/`IsVisible` on the launcher's own thread from a
  hidden window, turns the Win key, the Start button and the click-away into
  intents, reads the toggle direction from the controller's flags and never from
  the shell's lagging state, latches a press that lands once the shell is hiding
  or the executor's own `Dismiss` call is live, and serves it at the landing,
  swallows the shell's duplicate calls and the
  pointer-away close of an opening window, passes every trigger when the Start
  side is not alive, and never decides a toggle itself. It reads the shell's
  state only when the controller publishes no direction: the shell's cloak and
  `InternalState` are the resync for a panel the controller does not own, so an
  executor with no controller still decides from the window.

Everything else in `mods/` (`msgprobe`, `inputprobe`,
`start-menu-touch-gesture-probe`) is diagnosis from the investigation and is not
needed at runtime; `inputprobe`
must not be enabled beside the shipping mod, because Raw Input allows one sink
per usage per process and the probe takes the stream for itself.

## Build and test

The install commands are in [../README.md](../README.md). The mods compile
through Windhawk itself; the test tools are PowerShell 7 / Python — see
[testing.md](testing.md) — and their member list is
[../tools/AGENTS.md](../tools/AGENTS.md).
