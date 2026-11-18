# swipe-for-10start - touch swipe for ExplorerPatcher's legacy Win10 Start

C++ / Windhawk 2.0 mods + PowerShell 7 test tools
<directory>
mods/ - Windhawk mod sources (9 files: 8 sources - start-menu-legacy-swipe, touch-gesture-bridge, touch-gesture-consumer, touch-gesture-probe, msgprobe, inputprobe, start_gesture_shared.h, swipe_ipc_shared.h - plus AGENTS.md)
tools/ - PowerShell probes, injectors and animation harnesses (16 files: 15 scripts - screenshot, start_menu, inject_swipe, touch_common, touch_hold, follow_hold, inject_swipe_holds, measure_swipe, read_shared_gesture, probe_cursor_follow, ipc, native_flyout_frames, native_flyout_track, fit_settle_spring, settle_track - plus AGENTS.md)
artifacts/ - captured measurements (8 files: cursor-follow, gesture-shared, native-flyout-close, native-flyout-open, settle-trace, start-window, inputprobe.dll and AGENTS.md)
</directory>
<config>
README.md - what works, what was falsified, build and test instructions
.gitignore - Windhawk/MSVC build outputs and local logs
</config>

Two mods ship together: `mods/start-menu-legacy-swipe.wh.cpp` (0.26.0) runs in
`StartMenuExperienceHost.exe` and reads the digitizer through Raw Input because
every XAML/DirectManipulation source on this UI is empty (see README). The
tracked contact is named by the HID Contact Identifier (`0x0D/0x51`) when the
device declares one, else by `(device, node index)` (`identity=contactid|node`),
and a tip-down report without coordinates refreshes liveness without feeding a
position, so the 18 ms idle does not end a contact the device asserts. Every
gesture outcome settles on one damped spring whose shape `AnimationStyle` picks
(`Windows 11`, damping 1.72, fitted to the native Quick Settings flyout on this
machine; `Windows 10`, damping 0.9) and whose duration the travel plans:
`T = clamp(remaining / 2.2 DIP/ms, 160, 520)`, so the 900 DIP panel takes 409 ms.
That settle is the only settle animation, and the only other `Progress` animation
is the coast: a 200 ms linear continuation that starts after 8 ms without a
report and is pinned by the next report or ended by the release. A keyboard open
runs the settle from rest (`vel=0.00`), the curve is scaled by its own value at
`T`, so it lands on the target with no step, and `TakeProgressNow()` is the only
reader that turns the animation's recorded base — its target — into a position,
so every interruption starts where the panel is. Both styles start at the
component of the finger's end speed toward the target, over the travel left,
measured from the widest eligible window of the last reports (8-30 ms) and
projected onto the axis, so a release from a hold starts at zero speed instead
of jumping. 100 % of `GestureStartSpeedPercent` means that component, not the raw
release vector; the curve's end-value normalisation adjusts the seam slightly: it
divides by 0.99249 for `Windows 11` and 0.99589 for `Windows 10` at zero initial
slope, so 1.007 and 1.004 are example seam factors, not constants, and the first
keyframe, up to 4 ms in, shows the first segment's average speed instead of the
analytic seam. Panel and scroller decisions commit inside one lock
(`g_measureMutex`), which every open, close and teardown epoch bump also takes;
all XAML measurement stays outside it, and a layout change under a live contact
is deferred whole and applied once after the release. The vertical gate keeps the
menu's ScrollViewer in three states, and every remeasure publishes `Unknown`
first, so a failed pass cannot leave a previous answer speaking; its retries
back off 100/200/400 ms for three attempts. A gesture close
then taps `VK_LWIN` at the end of that settle: the shell's own key handler
performs the hide, so the Start toggle stays in step and no re-arm tap exists. A
keyboard close instead has the bridge hold `CStartExperienceManager::Hide` for
the settle the Start side answers with. A new pull during the settle drops the
tap before the shell is touched. A close exists only once its settle starts:
`BeginDismissSettle` answers not started, started, or started with a lost batch
observer (`close observer lost kind=...`), and only a close that provably never
started restores the panel, through `AbandonClose(kind, tapSerial)`, which also
cancels that close's own tap and stamp as one operation; the armed tap carries
that identity (`g_pendingTapSerial`). The image is pinned before any thread
exists (`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
GET_MODULE_HANDLE_EX_FLAG_PIN)`, `mod pin image=... pinned=1`), because `void
Wh_ModUninit` cannot veto the unload: the module stays mapped until the process
exits, by design. One instance per process takes
`Local\WindhawkLegacyStartSwipe.instance.<pid>` before the stop event, the
channel or any thread, and a second logs `instance guard refused: another
instance is live in this process` and installs nothing; only a clean unload
releases the mutex, and a re-initialisation of the same pinned image resets the
lifecycle state (`ResetReinitState`). Every entry into module code from the
compositor or the dispatcher is counted from before it can run
(`ModuleEntryLease`, `g_moduleEntryCount`). Unload is prerequisite-driven:
the stop event exists before any thread starts (without it no raw-input thread
runs and the mod logs `hid thread not started (stop event unavailable)`), the
input thread watches it beside its messages in 10 ms slices, the module-scan
thread waits on the same event, the 60 ms Windows tap waits on it and always
releases the key, and unload waits 10 s per worker thread, revokes the XAML
delegates, checks the settle batch, drains `g_moduleEntryCount` to zero, then
frees. A thread that misses its wait keeps both thread handles and leaves every
resource in place and logs `unload residual=...; process must restart to finish
unloading`, or `unload entries outstanding=%d` when a counted entry does not
drain. The Start mod publishes the close record three times (plan at the arm,
`tapped` immediately before the tap, cancelled when a new pull drops it) and
writes the cancelled kind
again with every new open generation, so no `tapped` stamp survives into the next
menu. It answers a bridge `retract` request with the duration it just planned, or
with the no-settle sentinel when the request's open generation is not current,
names generation 0, its 700 ms deadline passed, or the bridge already marked it
done; `mods/start-menu-touch-gesture-bridge.wh.cpp` (0.12.0) runs in
`explorer.exe`, passes the Hide at once for an unconsumed `tapped` record and
otherwise holds it for that answered duration plus 30 ms, or 560 ms when no
answer arrives. It binds a `tapped` stamp to the open tick it read and refuses
one older than that open; a fresh stamp is correlation, not causal identity, and
a Hide caused by the tap is inferred, not proven. On unload it keeps new Hides
out, waits up to 500 ms for in-flight `Hide` calls to drain, then closes the v2
channel (`ipc close`); a Hide that outlives the drain keeps every handle
(`ipc unload residual hides=...; the v2 channel stays until explorer exits`).
The older `start_gesture_shared.h` mapping stays for the process lifetime on
purpose, because a consumer and a late gesture callback may still hold its view.
The two processes talk only through `mods/swipe_ipc_shared.h` (v2): one session-local
section, three events and one command mutex, every record a seqlock with its own
counter, every field pinned by an `offsetof` assert (672 bytes), so nothing polls
and an answer cannot be mistaken for an earlier one. `SwipeIpcOpen` may wait up to
the whole 550 ms init budget, so a young claim is abandoned inside the call;
`SwipeIpcReadOpen` refuses generation 0 with `ERROR_NOT_READY`, and an `initState`
outside the four defined values is refused at once with `ERROR_INVALID_DATA`
instead of being waited on. Both hook probes first prove that the inspected
range lies in an executable section of the loaded image, so
`startui root hook skipped (target not readable)` is a distinct line from a build
or prologue mismatch. `tools/ipc.ps1` is the only command publisher and takes
the mutex. Everything else in `mods/` is diagnosis.

Laws: measure before designing · one data source per claim · docs and code move together

[PROTOCOL]: Update this header on change, then check AGENTS.md
