# tools/

> L2 | Parent: ../AGENTS.md

screenshot.ps1: full-screen PNG in physical pixels; sets per-monitor-v2 DPI
awareness first, or a 200 % display captures a quarter of the screen.
make_store_mods.ps1: writes the self-contained source into `dist/<profile>/`.
Both profiles inline `swipe_ipc_shared.h` (under a `-Wunused-const-variable`
pragma), drop `-I`, and replace the `==WindhawkModReadme==` body with both
READMEs. `store` (default) also drops `-Wall`/`-Wextra`, adds the collection
metadata and runs `source meta --strict`; `local` keeps the warnings.
install_mods.ps1: builds `local`, installs and enables
`local@swipe-for-10start` (`mod install` leaves a disabled mod disabled), restarts
the menu host unless `-SkipRestart`; `-All` adds the probes. Needs elevation.
smoke.ps1: the one-pass regression smoke. Rows: VK_LWIN open/close, injected
edge pull, injected panel drag close, desktop click close, and three mixed rows
(Win close + click, Start-button close + Win, gesture close + click). A row is
judged by the menu state plus that step's lines; the last step fails on any
error-shaped line.
smoke.ps1 defaults: requires `DiagnosticTrace`, warms a cold executor with one
native open and its close, reads the layout from the mod's own lines after the
run mark, and prints `skipped: ...` for `-SkipClickAway` rows.
start_menu.ps1: reports or forces the open state through `DWMWA_CLOAKED`
(`IsWindowVisible` stays TRUE while hidden). `-EnsureOpen` cannot open an
auto-hidden bar; `-EnsureClosed` is one VK_LWIN tap.
touch_common.ps1: the injection lifecycle every tool dot-sources; DOWN, UPDATE,
a 5 ms keep-alive and one serialised UP. `End-Contact` owns stop, join and UP in
that order. 5 ms is below the mod's 18 ms idle release.
inject_swipe.ps1: one-finger swipe through `InjectTouchInput`, physical pixels;
`-DryRun` prints the sequence. `frameId` 0 and zero raw/himetric points, or the
sequence is rejected.
touch_hold.ps1: drag, hold with the keep-alive, capture, release with
`-ReleaseAtEnd` (`-Lift` is an alias); without it the contact stays down until
the process exits.
follow_hold.ps1: the same capture for the reveal follow; a plain hold goes
silent, so the keep-alive runs through the screenshot.
inject_swipe_holds.ps1: swipe with pauses at waypoints, each a real 5 ms hold.
measure_swipe.ps1: Start window rect during a swipe into
`artifacts/start-window.csv`; asks for 8 ms, gets ~16 ms from the 15.6 ms
quantum.
probe_cursor_follow.ps1: cursor samples during an injected drag into
`artifacts/cursor-follow.csv`, then restores the cursor. The cursor is
suppressed, so every sample is one point.
ipc.ps1: read-only channel view. `-State` prints the records, the peer
epoch/tick/state, `execCapable` and `execPid`; it refuses `initState != ready` (2), a
foreign magic or `layoutBytes != 240`. No command publisher exists.
native_flyout_frames.ps1: the Quick Settings flyout (`Win+A`) as BMPs at ~10 ms,
with `ControlCenterWindow` visibility polled as t=0. Captured with the bar on
top, so its artifacts are a top-taskbar run.
native_flyout_track.py: fits each frame's offset against the settled frame and
prints the `t -> dy` table the settle fit consumes.
fit_settle_spring.py: models the shipped settle against the flyout CSV;
`--damping`, `--fit`, `--model legacy`. The data does not identify the damping.
settle_track.ps1: pulls the menu half open, holds, releases, and tracks the
panel's bottom edge for 500 ms. Its geometry is the top-taskbar layout, so on a
left bar it tracks the wrong edge. t=0 is `End-Contact`'s UP stamp.
panel_edge_track.ps1: the panel's trailing edge in physical pixels during a Win
open or close, from one 96 px band diffed against a closed-menu baseline. The
axis is the mod's `dismiss axis`; `trail * scale` is the fully-open edge.
`-WinTap`, `-Observe`, `-TriggerAtMs`/`-Trigger` choose the driver.
panel_edge_track.ps1 limit: it cannot read the trailing edge on a bottom bar
(`axis=3`) yet, and it refuses a run whose axis, rect, band, `Distance` or scale
is missing from the mod's lines.

[PROTOCOL]: Update this header on change, then check AGENTS.md
