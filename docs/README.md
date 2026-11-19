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
for the vertical-path runs; it is now the Windows 11 native taskbar with
auto-hide on, docked at the bottom, so the work area equals the full screen
(`work=0,0,2880,1800`, `mon=0,0,2880,1800`) — the normal state of auto-hide,
not a transient. Where a measurement depends on the placement, the text names
it.

## Map

| File | Topic |
|---|---|
| [input.md](input.md) | Why Raw HID: the falsified data sources, the report decode (report IDs, `TipSwitch`/`InRange`, contact identity and its evidence clock, the coordinate-only fallback), release detection and its timers. |
| [gesture.md](gesture.md) | The `Progress` model, the dismiss and reveal state machines, the taskbar band, the adoption of a late open and the record a lift before it leaves, the live takeover's visible entry and its 2.2 DIP/ms chase of the finger, the close-ownership rule (a close owns `Progress` until the window is observed cloaked; a gesture close whose tap is still armed is cancelled and its panel comes back, every other pull is deferred, released at the landing and served live or late, and the one-open-path order), the shell's own limits on an open during a close, the unhealed-window rule (a shown window with the panel off it is healed, and a pull there opens), the commit thresholds, what a close does to a live gesture. |
| [animation.md](animation.md) | The damped-spring settle: both damping regimes, the 2.2 DIP/ms duration plan, the start-speed seam and the offset-carrying window replay at a reveal takeover or a late release, the one-owner rule (a deferred pull writes nothing while a close is in flight) and the from-live settle of an open the shell performs during a close, the two shipped styles, and the native Quick Settings flyout measurement behind the 1.72 fit. |
| [geometry.md](geometry.md) | The `GestureGeometry` snapshot and its lock discipline, the panel-trail measurement and band, the taskbar axis probe (including auto-hide), the deferred-measurement request, the scroll survey and gate, and the measured layouts. |
| [ipc.md](ipc.md) | The v2 shared-memory channel, the open generation, the plan/tapped/cancelled close record, the retract request/answer, and the bridge's `Hide` policy and unload drain. |
| [lifecycle.md](lifecycle.md) | The image pin, the per-process instance guard, the counted module entries, the unload order and its residuals, re-initialisation, and the host's unload boundary. |
| [testing.md](testing.md) | The PowerShell/Python probes and injectors, and the captured measurements in `artifacts/`. |
| [limits.md](limits.md) | Known limits and risks, and the hardware checks still owed (one finger end to end, two fingers in both lift orders, pen by hand, right-side taskbar by hand). |

## The mods

Two mods ship together (full member list in
[../mods/AGENTS.md](../mods/AGENTS.md)):

- `mods/start-menu-legacy-swipe.wh.cpp` (0.35.0) — runs in
  `StartMenuExperienceHost.exe`; the whole gesture, follow and animation.
- `mods/start-menu-touch-gesture-bridge.wh.cpp` (0.16.0) — runs in
  `explorer.exe`; hooks `CStartExperienceManager::Hide` so keyboard and
  click-away closes play the same slide-out.

Everything else in `mods/` (`msgprobe`, `inputprobe`,
`start-menu-touch-gesture-probe`, `start-menu-touch-gesture-consumer`) is
diagnosis from the investigation and is not needed at runtime; `inputprobe`
must not be enabled beside the shipping mod, because Raw Input allows one sink
per usage per process and the probe takes the stream for itself.

## Build and test

The install commands are in [../README.md](../README.md). The mods compile
through Windhawk itself; the test tools are PowerShell 7 / Python — see
[testing.md](testing.md) — and their member list is
[../tools/AGENTS.md](../tools/AGENTS.md).
