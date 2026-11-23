# Technical documentation

> The user-facing quick start is [../README.md](../README.md) (English) /
> [../README.zh-CN.md](../README.zh-CN.md) (中文).

## Verified environment

Windows 11 build 26300.10075 (`twinui.pcshell.dll` fixed 10.0.26100.10068),
ExplorerPatcher 26100.9711 (`Start_ShowClassicMode=1`), Windhawk 2.0 alpha,
2880x1800 physical at 200 % (1440x900 DIP), ELAN2513 touchscreen. The bar is not
pinned; the measured layouts and their dates are in
[../AGENTS.md](../AGENTS.md#machine).

## Map

| File | Topic |
|---|---|
| [architecture.md](architecture.md) | The launcher read and the pin, the executor thread, the gate, the press queue, the command outcomes, and the measured open/close timings. |
| [input.md](input.md) | Why Raw HID, the report decode and contact identity, release detection, and what wakes the input thread. |
| [gesture.md](gesture.md) | The `Progress` model, the dismiss/reveal/grab sessions, the 24 DIP claim, the close record, the gates and the settle ownership. |
| [animation.md](animation.md) | The settle spring, the duration plan, start speed, the close shape, the coast and interruption. |
| [geometry.md](geometry.md) | The snapshot, the trail and band, the axis probe, the deferred apply and the scroll survey. |
| [ipc.md](ipc.md) | The 240-byte channel: names, layout, every record, admission, expiry and the capability word. |
| [lifecycle.md](lifecycle.md) | The unload order of both roles and the proofs that block it. |
| [testing.md](testing.md) | The build gate, the smoke, the harness checks, the producers and the trailing-edge evidence. |
| [development.md](development.md) | The local build, the elevated install, the compile check and the store build. |
| [limits.md](limits.md) | Every measured limit, the hardware still owed, and the unmeasured layouts. |
| [store.md](store.md) | The generated submission file, the collection's constraints and the PR checks. |

## The mod

One mod ships, in one source with two process halves (member list in
[../mods/AGENTS.md](../mods/AGENTS.md)): `start` in
`StartMenuExperienceHost.exe` owns the gesture, the panel, the close record and
the liveness; `bridge` in `explorer.exe` executes the launcher call and turns the
Win key, the Start button and the click-away into intents. Everything else in
`mods/` is diagnosis and is not needed at runtime; `inputprobe` must not be
enabled beside the shipping mod.

The build and install commands are in [development.md](development.md); the tools
are PowerShell 7 / Python and their roster is [../tools/AGENTS.md](../tools/AGENTS.md).
