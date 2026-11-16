# mods/

> L2 | Parent: ../AGENTS.md

start-menu-legacy-swipe.wh.cpp: the shipping mod, `StartMenuExperienceHost.exe`. Raw HID touch/pen decode (RIDI_PREPARSEDDATA + HidP_GetUsageValue), axis-aware state machines on a worker thread for both dismiss (drag inside the menu) and reveal (edge swipe; anchored to the panel trailing edge), `Progress` composition expression, dismissal by `VK_LWIN` tap after a 260 ms eased slide-out. Hooks `StartUI::App::PrepareRootElement` (hardcoded RVA) for the root and an experimental `SplitViewFrame::OnGlobalAnimationRequested` hook (`anim=` debug switch, off by default because that request also presents the panel).
msgprobe.wh.cpp: diagnostic. Hooks `GetMessageW`/`PeekMessageW`, logs `WM_POINTER*` and mouse messages. Established that the queue carries only ENTER + DOWN and then `WM_POINTERCAPTURECHANGED`.
inputprobe.wh.cpp: diagnostic. Raw HID listener plus a `CoreWindow`/`SystemUserAdapterWindowClass` WndProc subclass. Took the digitizer stream first and proved per-frame reports are readable. Must not run beside the shipping mod: Raw Input allows one sink per usage per process.
start-menu-touch-gesture-bridge.wh.cpp: `explorer.exe` bridge. Publishes the taskbar edge gesture handle and state from `twinui.pcshell.dll` into shared memory; refuses to hook on a build mismatch. Unused by the shipping mod.
start-menu-touch-gesture-probe.wh.cpp: Phase 0 probe that located `TouchEdgeGestureProcessor` and `ReportEdgeGesture`.
start_gesture_shared.h: shared-memory contract shared by the bridge and its consumer; layout must stay in sync with both.

[PROTOCOL]: Update this header on change, then check AGENTS.md
