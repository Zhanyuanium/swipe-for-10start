# swipe-for-10start - touch swipe for ExplorerPatcher's legacy Win10 Start

C++ / Windhawk 2.0 mods + PowerShell 7 test tools
<directory>
mods/ - Windhawk mod sources (6 files: start-menu-legacy-swipe, msgprobe, inputprobe, touch-gesture-bridge, touch-gesture-probe, start_gesture_shared.h)
tools/ - PowerShell probes and injectors (screenshot, start_menu, inject_swipe, touch_hold, inject_swipe_holds, measure_swipe, read_shared_gesture)
artifacts/ - captured measurements (raw input samples, cursor probe, window geometry)
</directory>
<config>
README.md - what works, what was falsified, build and test instructions
.gitignore - Windhawk/MSVC build outputs and local logs
</config>

Two mods ship together. `mods/start-menu-legacy-swipe.wh.cpp` runs in
`StartMenuExperienceHost.exe` and reads the digitizer through Raw Input because
every XAML/DirectManipulation source on this UI is empty (see README). Both of
its close paths share one deferred `VK_LWIN` tap, and that tap is dropped when a
new edge pull starts, so a cancelled close never reaches the shell.
`mods/start-menu-touch-gesture-bridge.wh.cpp` runs in `explorer.exe` and delays
`CStartExperienceManager::Hide` by up to 350 ms so the Start side can retract
the panel for a Windows-key close; a gesture close passes through at once,
announced by the Start mod's `close-mode.txt` stamp. Everything else in `mods/`
is diagnosis.

Laws: measure before designing · one data source per claim · docs and code move together

[PROTOCOL]: Update this header on change, then check AGENTS.md
