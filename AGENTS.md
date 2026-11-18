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

Two mods ship together: `mods/start-menu-legacy-swipe.wh.cpp` (0.29.0) runs in
`StartMenuExperienceHost.exe` and reads the digitizer through Raw Input because
every XAML/DirectManipulation source on this UI is empty (see README). The
tracked contact is named by the HID Contact Identifier (`0x0D/0x51`) when the
device declares one, else by `(device, node index)` (`identity=contactid|node`),
and when several slots carry that identifier the one that asserts contact wins,
with one `hid contact id=... declared down in ... slots; using=...` line.
Liveness for a report that carries no coordinates refreshes only when the report
asserts contact (`TipSwitch`/`InRange`, or a slot state of Contact), never on
identifier presence alone; the liveness clock is
separate from the position clock, so a coordinate-less assertion does not feed a
position and the 18 ms idle does not end a contact the device asserts; the same
idle ends the gesture with `src=idle`, and once no session owns the contact it
clears the contact latch and forgets the tracked identity. The identity carries
its own evidence clock (`lastEvidenceTick`), which moves only when the report
proves the contact — the tracked slot's `TipSwitch`/`InRange` asserts it with no
point, or the report carries a position this path feeds; a name-only report
updates the slot and the identifier for diagnostics and then returns with no
position, no liveness and no clock write. `CheckHidIdleRelease` forgets an
identity whose last evidence is at least the idle window old, admitted or not,
once no session owns the contact, and logs `hid identity expired slot=...
latch=0` when no latch held it. A refused admission therefore cannot strand a
name. A coordinate-only
device applies its Contact Count and its `(0,0)` rule in the
tracked branch too, because it cannot assert a tip for the tracked slot. The
dismiss axis and the panel-acceptance band come from the taskbar itself:
`SHAppBarMessage(ABM_GETTASKBARPOS)` first, cross-checked against the
`Shell_TrayWnd`/`Shell_SecondaryTrayWnd` rectangle (flush to the edge and thin
across it), the work-area inset only when it truly insets, and never the Start
window's own rect; a probe that names nothing keeps the last axis and band and
retries on the next open or broadcast. Every gesture outcome settles on one
damped spring whose shape `AnimationStyle` picks
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
projected onto the axis, so a release from a hold starts at zero speed instead
of jumping. That end speed is two checks. Freshness: the newest stored position
must be no more than 30 ms older than the release, or the answer is 0, so a hold
whose only reports assert the tip leaves no speed from before the hold. Span:
from that newest position backward, the oldest sample is the first at least 8 ms
and at most 30 ms older than it, so the pair is 8-30 ms wide and may lie up to
60 ms before the release. 100 % of `GestureStartSpeedPercent`
means that component, not the raw
release vector; the curve's end-value normalisation adjusts the seam slightly: it
divides by 0.99249 for `Windows 11` and 0.99589 for `Windows 10` at zero initial
slope, so 1.007 and 1.004 are example seam factors, not constants, and the first
keyframe, up to 4 ms in, shows the first segment's average speed instead of the
analytic seam. Panel and scroller decisions commit inside one lock
(`g_measureMutex`), which every open, close and teardown epoch bump also takes;
all XAML measurement stays outside it, and a layout change under a live contact
is deferred whole and applied once after the release; a work-area change reaches
the mod only as a broadcast (`WM_SETTINGCHANGE` with `wParam = SPI_SETWORKAREA`,
`SPI_SETWORKAREA` being `0x002F`), which is why the input window is hidden and
top-level rather than message-only. The apply reservation
(`LayoutApplyGuard`) and the admission claim (`ClaimPanelForAdmission`) are the
two `seq_cst` sides of one handshake: either the layout is deferred whole or
that admission is refused and retried on the next report. A scalar write that
throws after an earlier one landed marks the renderer invalid (`gesture renderer
invalid until a successful reapply`) and refuses gestures until a full reapply. The vertical gate keeps the
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
started restores the panel, through `AbandonClose(kind)`, which is the whole
operation because such a close published no plan and armed no tap. The image
is pinned before any thread
exists (`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
GET_MODULE_HANDLE_EX_FLAG_PIN)`, `mod pin image=... pinned=1`), because `void
Wh_ModUninit` cannot veto the unload: only a successful pin keeps the module
mapped until the process exits. The pin and the instance guard are prerequisites:
either failing returns failure from `Wh_ModInit` with `mod init refused: ...; no
hook, no channel and no thread installed`, and `g_initRefused` short-circuits
the later lifecycle callbacks. One instance per process takes
`Local\WindhawkLegacyStartSwipe.instance.<pid>` before the stop event, the
channel or any thread, and a second logs `instance guard refused: another
instance is live in this process` and installs nothing; the name carries the
PID, so it excludes another instance in that process, not every process in the
session. Only a clean unload releases the mutex, and a re-initialisation of the
same pinned image resets the lifecycle state (`ResetReinitState`) and then runs
one per-activation reset (`ResetActivationState`, logged `activation state
reset`), which the input thread's exit runs too: it clears the tracked contact,
the contact latch, both sessions, the close mute, the deferred flag, the pending
tap and the pull generation it was armed under, the pull serial, the coast, both
velocity windows, the diagnostic latches and the `anim` override. Every entry
into module code from the compositor or the dispatcher is counted from before it
can run
(`ModuleEntryLease`, `g_moduleEntryCount`); every turn posted through
`PostCountedUiTurn` is counted, while the unload's revocation turn uses
`RunAsync` plus an event and waits for it, deliberately not the counted post, so
the unload knows the revocation ran before it frees what that turn holds. Unload
is prerequisite-driven:
the stop event exists before any thread starts (without it no raw-input thread
runs and the mod logs `hid thread not started (stop event unavailable)`), the
input thread watches it beside its messages in 10 ms slices, the module-scan
thread waits on the same event, the 60 ms Windows tap waits on it and always
releases the key, and unload waits 10 s per worker thread, revokes the XAML
delegates, checks the settle batch, drains `g_moduleEntryCount` to zero, then
frees. A thread that misses its wait keeps its handle and leaves every
resource in place and logs `unload residual=...; process must restart to finish
unloading`, or `unload entries outstanding=%d` when a counted entry does not
drain; a joined thread's handle is closed. A XAML revoke that throws keeps its
token and its owner, because that token is the only handle on a handler that can
still fire, and a new registration is refused while one is outstanding (`xaml
size handler left attached; new registration refused`, the same for `loaded`;
the watches log `xaml layout handler left attached; new registration refused`
and `xaml viewchanged handler left attached; new registration refused`): that
root gets no renderer, the layout watch leaves that open to its 400 ms deadline,
and the scroll pass adds no watcher, with
`unload residual=xaml handlers; process must restart to finish unloading` while
one survives. The `SizeChanged` and `Loaded` callbacks
are gated on the setup generation; the layout watch mints a serial and captures
its own measurement request by value, so a callback that outlived a newer watch
measures and revokes nothing; the `ViewChanged` handler captures the viewer it
was registered on and may write the cached offsets only while that element is
`g_scrollerCacheViewer`, the element the committed rect was measured from, and
the cache is `Scrollable`, both under `g_scrollerMutex`; `g_scrollerWatchOwner`
is the separate element whose token must still be revoked, kept for a retry
when its revoke threw, and a replacement registers no watch while it survives
(`xaml viewchanged handler left attached; new registration refused`). After a
disable/enable without a host restart, the reused XAML root means
no new `PrepareRootElement` and no renderer, so a new contact cannot drive the
panel until the host restarts. The Start mod publishes the close record
three times (plan at the arm, `tapped` immediately before the tap, cancelled
when a new pull drops it) and writes the cancelled kind
again with every new open generation, so no `tapped` stamp survives into the next
menu. It answers a bridge `retract` request with the duration it just planned, or
with the no-settle sentinel when the request's open generation is not current,
names generation 0, its 700 ms deadline passed, or the bridge already marked it
done; `mods/start-menu-touch-gesture-bridge.wh.cpp` (0.15.0) runs in
`explorer.exe`, passes the Hide at once for an unconsumed `tapped` record and
otherwise holds it for that answered duration plus 30 ms, or 560 ms when no
answer arrives. It binds a `tapped` stamp to the open tick it read and refuses
one older than that open; a fresh stamp is correlation, not causal identity, and
a Hide caused by the tap is inferred, not proven. On unload it keeps new Hides
out and waits for every counted `Hide` call to leave before it closes the v2
channel (`ipc close`): the count is a `HideCallLease` (RAII, exactly one release
per frame) that spans the original call too, so quiescence is what makes both
that close and the trampoline use safe, and 500 ms is only where a call still
live is reported (`ipc unload drain over 500 ms hides=...; still waiting for
quiescence`); the drain's own bound is 10 s, above the mod's hold (660 ms for the
shipped pair, at most 2.13 s for any answer the channel accepts), and a Hide that
reaches it keeps every handle
(`ipc unload residual hides=...; the v2 channel stays until explorer exits`).
Windhawk 2.0.0-alpha.8 disables the hooks, calls `Wh_ModUninit`, then waits
`ThreadsCallStackWaitForRegions(..., 200, 400)` before removing hooks and
`FreeLibrary`, and discards that wait's result: "the scan found the frame" and
"the frame exited before removal" are two different claims, and only the first is
the host's. The bridge pins its own image as a prerequisite
(`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
GET_MODULE_HANDLE_EX_FLAG_PIN)`, `mod pin image=... pinned=1`); a failed pin
returns FALSE and installs nothing (`mod init refused: the image could not be
pinned; no hook, no channel and no thread installed`, or `mod pin failed
err=...`), because a shell-owned `Hide` thread needs valid return code as much as
a worker would, and the pin keeps a frame that returns after `Wh_ModUninit` in
mapped code with its lease still releasing. A re-enable re-initialises the same
image and refuses while `g_hideCalls` is nonzero (`mod init refused: a Hide from
the previous activation is still inside; ...`); otherwise `ResetReinitState()`
clears `g_disabled`, `g_afterInit`, `g_explorerHooked`, `g_hideDelayEnabled` and
`g_startPointTick`, retries a failed gesture mapping, leaves `g_hideCalls` alone,
and logs `activation state reset`; a channel a timed-out unload kept is reused
rather than remapped.
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
