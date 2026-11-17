# tools/

> L2 | Parent: ../AGENTS.md

screenshot.ps1: full-screen PNG in physical pixels; switches the process to per-monitor-v2 DPI awareness first, otherwise 200% scaling captures only the top-left quarter.
start_menu.ps1: reports or forces the legacy Start menu's open state through `DWMWA_CLOAKED` (`IsWindowVisible` stays TRUE while it is hidden); opens with the taskbar's Start button, closes with one Windows tap.
inject_swipe.ps1: one-finger swipe through `InjectTouchInput`, physical pixels; `frameId` stays 0 and the raw/himetric points stay zero or the sequence is rejected.
touch_hold.ps1: drag to a point, hold, capture, then release; a plain hold goes silent on the virtual digitizer, so a capture taken during it shows the post-release state.
follow_hold.ps1: the same, but keeps an UPDATE in flight every 10 ms while the screenshot is taken; the only way to photograph a live mid-gesture state on this digitizer.
inject_swipe_holds.ps1: swipe with pauses at waypoints, for threshold and coast experiments.
measure_swipe.ps1: band capture around a swipe plus the panel edge per frame; an earlier investigation helper.
read_shared_gesture.ps1: reads the bridge's shared-memory gesture state; an earlier investigation helper.
native_flyout_frames.ps1: captures the native Quick Settings flyout (`Win+A`) as BMPs at ~9 ms with a reused DIB, and polls `IsWindowVisible` on `ControlCenterWindow` every 1 ms as t=0 (`DWMWA_CLOAKED` stays 0 for that window). On this machine the taskbar is on top, so the flyout is anchored top-right and unfolds downward.
native_flyout_track.py: fits each captured frame's vertical offset against the settled frame (Pearson r over the card interior) and prints the `t -> dy` table the settle fit consumes.
fit_settle_spring.py: the two-parameter area fit of the mod's spring (`y0 = 0`) to the measured curve, the residuals per sample, and the initial-slope comparison against the old cubic-bezier.
settle_track.ps1: pulls the menu half open from the taskbar edge, holds, releases, and tracks the panel's bottom edge for 500 ms by diffing each frame against a closed-menu baseline; proves the settle leaves the held position at ~0 speed.

[PROTOCOL]: Update this header on change, then check AGENTS.md
