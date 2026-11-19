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
for the vertical-path runs. **Current layout: docked** (auto-hide off): work
`0,0,1440,852`, tray `0,852..1440,900`, band `804..900`, panel
`236,265..1229,840`, trail 265, distance 900. The auto-hidden layout (work
`0,0,1440,900`, panel `236,167..1229,840`, trail 167) is historical; its
numbers are not the docked ones. Where a measurement depends on the placement,
the text names it.

## Map

| File | Topic |
|---|---|
| [architecture.md](architecture.md) | M3: the launcher read (`s_pLauncher` outer + `0x18` interface, `[+0x160]` state), the executor thread and the taskbar-thread crash, the one-decision/one-executor contract, the measured open/close/interruption chains, the accepted 245–268 ms cap, and the list of deleted M2 mechanisms. |
| [input.md](input.md) | Why Raw HID: the falsified data sources, the report decode (report IDs, `TipSwitch`/`InRange`, contact identity and its evidence clock, the coordinate-only fallback), release detection and its timers. |
| [gesture.md](gesture.md) | The `Progress` model, the dismiss and reveal state machines, the band and the once-per-open show request, the live takeover's visible entry and 2.2 DIP/ms chase, the one close path (`TakeProgressNow` → mirrored settle → scheduled `exec dismiss`), the reopen on a closing panel, the gates and the settle ownership. |
| [animation.md](animation.md) | The damped-spring settle, both damping regimes, the 2.2 DIP/ms duration plan, the start-speed seam, the mirrored close, the scheduled dismiss, and the measured lines. |
| [geometry.md](geometry.md) | The `GestureGeometry` snapshot and its lock discipline, the panel-trail measurement and band, the taskbar axis probe (including auto-hide), the deferred-measurement request, the scroll survey and gate, and the measured layouts. |
| [ipc.md](ipc.md) | The v4 shared-memory channel: names, 744-byte layout, seqlocks, the open generation, intents and acks, executor commands and acks, the controller flags, and the init handshake. |
| [lifecycle.md](lifecycle.md) | The image pin, the per-process instance guard, the counted module entries, the unload order and its residuals, re-initialisation, and the host's unload boundary. |
| [testing.md](testing.md) | The PowerShell/Python probes and injectors, and the captured measurements in `artifacts/`. |
| [limits.md](limits.md) | Known limits and risks, and the hardware checks still owed (one finger end to end, two fingers in both lift orders, pen by hand, right-side taskbar by hand). |

## The mods

Two mods ship together (full member list in
[../mods/AGENTS.md](../mods/AGENTS.md)):

- `mods/start-menu-legacy-swipe.wh.cpp` (0.39.0) — runs in
  `StartMenuExperienceHost.exe`; the whole gesture, follow and animation, the
  controller that consumes bridge intents and publishes executor commands, the
  pull-reopen at a close's landing, and the one close path (a mirrored settle
  from the live position, then `dismiss`).
- `mods/start-menu-touch-gesture-bridge.wh.cpp` (0.20.0) — runs in
  `explorer.exe`; executes `ShowStartView`/`Dismiss`/`IsVisible` on the
  launcher's own thread from a hidden window, turns the Win key, the Start
  button and the click-away into intents, latches a press that lands inside the
  shell's hide and serves it at the landing, swallows the shell's duplicate
  calls and the pointer-away close of an opening window, and never decides a
  toggle itself.

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
