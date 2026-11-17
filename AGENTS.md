# swipe-for-10start - touch swipe for ExplorerPatcher's legacy Win10 Start

C++ / Windhawk 2.0 mods + PowerShell 7 test tools
<directory>
mods/ - Windhawk mod sources (7 files: start-menu-legacy-swipe, msgprobe, inputprobe, touch-gesture-bridge, touch-gesture-probe, touch-gesture-consumer, start_gesture_shared.h)
tools/ - PowerShell probes, injectors and animation harnesses (screenshot, start_menu, inject_swipe, touch_hold, follow_hold, inject_swipe_holds, measure_swipe, read_shared_gesture, native_flyout_frames, native_flyout_track, fit_settle_spring, settle_track)
artifacts/ - captured measurements (raw input samples, cursor probe, window geometry, native flyout displacement curves, shipped settle trace)
</directory>
<config>
README.md - what works, what was falsified, build and test instructions
.gitignore - Windhawk/MSVC build outputs and local logs
</config>

Two mods ship together. `mods/start-menu-legacy-swipe.wh.cpp` runs in
`StartMenuExperienceHost.exe` and reads the digitizer through Raw Input because
every XAML/DirectManipulation source on this UI is empty (see README). Every
gesture outcome settles on one damped spring whose parameters `AnimationStyle`
picks: `Windows 11` (0.102 s / 1.72 / 253 ms, fitted to the native Quick Settings
flyout on this machine) or `Windows 10` (0.25 s / 0.9 / 221 ms). Both start at
the finger's release speed over the travel left, so a release from a hold starts
at zero speed instead of jumping. Both close paths then tap `VK_LWIN` at the end
of that settle: the shell's own key handler performs the hide, so the Start
toggle stays in step and no re-arm tap exists. A new pull during the settle drops
the tap before the shell is touched. The Start mod stamps `close-mode.txt` with
`gesture=0` (or `cancelled` when the tap is dropped);
`mods/start-menu-touch-gesture-bridge.wh.cpp` runs in `explorer.exe` and uses
that stamp to skip its 350 ms keyboard lead for the gesture's Hide. Everything
else in `mods/` is diagnosis.

Laws: measure before designing · one data source per claim · docs and code move together

[PROTOCOL]: Update this header on change, then check AGENTS.md
