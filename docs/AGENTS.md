# docs/
> L2 | Parent: ../AGENTS.md

The project's memory: what was measured, what was falsified, and why each
decision was taken. The user-facing quick start is `../README.md` /
`../README.zh-CN.md`.

architecture.md: M3.1, the shipping design. The launcher read (`s_pLauncher`
outer object, `+0x18` IXamlLauncher, `[+0x160]` state, readiness `> 0x10000`),
the executor thread (the `OnHotkeyInvoke` thread, not the taskbar thread), one
decision/one executor per trigger, the press latch for a hide in flight, the
click-away guards of an opening window, the pull-reopen at a close's landing,
the docked-layout timing/interruption chains, the accepted cloak cap, the v4
channel, and the deleted M2 mechanisms.

input.md: why Raw Input; the report decode (report IDs, `TipSwitch`/`InRange`,
contact identity and its evidence clock, the coordinate-only fallback), the
liveness and idle rules, and the release timers.

gesture.md: the `Progress` model, the dismiss/reveal sessions, the band and the
once-per-open show request, the reveal entry and the 2.2 DIP/ms chase, the one
close path (`TakeProgressNow` → mirrored settle → scheduled `exec dismiss`), the
reopen on a closing panel, the gates and settle ownership.

animation.md: the damped spring (ζ 1.72 / 0.9), the travel-planned duration, the
start-speed seam, the mirrored close, the coast, and the measured lines.

geometry.md: the `GestureGeometry` snapshot and its lock, the panel-trail
measurement and band, the taskbar axis probe, the deferred measurement, the
scroll survey, and the measured layouts.

ipc.md: the v4 channel — names, 744-byte layout, seqlocks, open generation,
intent/ack, executor command/ack, controller flags, init handshake.

lifecycle.md: the pin, the instance guard, the counted entries, the unload
order and residuals, the re-initialisation and the host's unload boundary.

testing.md: the probes/injectors and the captured measurements in `artifacts/`.

limits.md: the measured limits — the shell cap, launcher readiness, the
launcher thread, the touch-show idle suppression, the cold entry, the taskbar
auto-hide state, and the hardware still owed.

README.md: the map and the shipping pair.

## Laws

Every claim names its probe or its log line. A mechanism the code no longer
contains does not get a page; the deletion list lives in architecture.md.

[PROTOCOL]: Update this header on change, then check AGENTS.md
