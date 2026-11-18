# swipe-for-10start

Windhawk mods for the ExplorerPatcher Windows 10 Start menu on Windows 11.

## What this is

ExplorerPatcher replaces Windows 11's Start UI with the legacy `StartUI_.dll`,
which has no consumer for the shell's touch gesture. This repository is the
investigation of where that gesture data actually lives, plus a working
touch/pen swipe-to-dismiss that makes the menu follow the finger.

Verified on: Windows 11 build 26300.9974, ExplorerPatcher 26100.9711
(`Start_ShowClassicMode=1`), Windhawk 2.0 alpha, 2880x1800 physical at 200%
scaling (1440x900 DIP), ELAN2513 touchscreen. The taskbar was on the left for
the early measurements and on top for the vertical-path runs; it is now the
Windows 11 native taskbar with auto-hide on, docked at the bottom, so the work
area equals the full screen (`work=0,0,2880,1800`,
`mon=0,0,2880,1800`) — the normal state of auto-hide, not a transient. Where a
measurement depends on the placement, the text names it.

## Status

| Feature | State |
|---|---|
| Find a per-frame finger source inside `StartMenuExperienceHost.exe` | done, raw HID |
| Menu follows the finger while a contact is down | working |
| Swipe dismiss with commit/cancel thresholds | working; left, top and bottom taskbars verified |
| Dismiss animation (slide out, spring back) | working |
| Pen | `TipSwitch`/`InRange` decoded, hover discarded; not yet exercised by hand |
| Taskbar on left / top / right / bottom | axis and panel band read from the taskbar's own dock record; left, top and bottom verified, the right side exercised earlier through the same path (`axis=2`) |
| Mouse drag to dismiss | deliberately not enabled |
| Reveal (edge swipe to open) following the finger | working, anchored to the panel edge |
| Windows-key close | animated slide-out; the bridge holds the Hide for the settle the Start mod answers with |
| Gesture close | retract, then one Windows tap at its end; the bridge skips its keyboard lead for the `tapped` record |
| Reopen during a close | a new edge pull drops the armed tap, so the shell never sees that close |

## How the working path works

`mods/start-menu-legacy-swipe.wh.cpp` runs inside `StartMenuExperienceHost.exe`:

1. A worker thread registers Raw Input for the digitizer
   (`usUsagePage 0x0D / usUsage 0x04` for touch, `0x0D / 0x02` for pen,
   `RIDEV_INPUTSINK`) on a hidden top-level window. A message-only window would
   not receive the `WM_SETTINGCHANGE` (`SPI_SETWORKAREA`) and `WM_DISPLAYCHANGE`
   broadcasts that move the taskbar, and those broadcasts are the whole
   event-driven axis refresh.
2. Each report is decoded with `GetRawInputDeviceInfoW(RIDI_PREPARSEDDATA)` +
   `HidP_GetUsageValue`: contact collections `0x0D/0x22` (Finger) or `0x0D/0x05`
   carry `0x01/0x30` (X) and `0x01/0x31` (Y); the top-level `0x0D/0x54` is the
   Contact Count. A device whose input caps declare report IDs carries the ID in
   the first byte of every raw report (`RAWINPUTHEADER::wParam` is only
   `RIM_INPUT`/`RIM_INPUTSINK`), and that byte selects the caps; a device without
   report IDs takes the previous path. A report whose ID carries no contact field
   at all — no slot field and no contact count — is an unrelated report ID: it is
   ignored whole, which is distinct
   from a decode failure that logs `hid decodefail`. Otherwise every candidate
   slot's `TipSwitch`, `InRange`, Contact Identifier and X/Y are decoded before a
   contact is chosen.
   `TipSwitch` (`0x0D/0x42`) and `InRange` (`0x0D/0x32`) are button usages, so
   they are read per collection and per report: in range with the tip up is a
   hover and drives nothing, and only the tracked slot's tip going up (or its
   in-range bit clearing) ends the contact while coordinates keep arriving. A
   count-only lift report is not a lift; the 18 ms idle ends the contact instead.
   A tracked report that carries no coordinates refreshes the liveness clock
   only when it asserts contact (`TipSwitch`/`InRange`, or a slot state of
   Contact), never on identifier presence alone; the liveness clock is separate
   from the position clock, so that report cannot feed a position, and the idle
   cannot end a contact the device is still asserting. When several slots carry
   the tracked identifier, the one that asserts contact wins, and one
   `hid contact id=... declared down in ... slots; using=...` line records the
   ambiguity.
   A device with no tip and no in-range usage keeps the count/position path; its
   Contact Count and `(0,0)` rules apply in the tracked branch too, because it
   cannot assert a tip for the tracked slot, and it is the only device family
   that needs those rules.
   Raw values are mapped to pixels through the logical range of the collection
   the point was read from (`cap.LinkCollection`) and then to DIP through the
   window DPI. `hid usages dev=... tip=... inrange=... count=... buttons=...
   reportids=... coordpath=... identity=contactid|node` records once per device
   which usages it exposes, whether it is on the count/position path, and which
   name its tracked contact uses.
3. The tracked contact is followed by its name: the Contact Identifier
   (`0x0D/0x51`) when both the device and the contact carry one
   (`identity=contactid`), the `(device, node index)` pair otherwise
   (`identity=node`). A slot the device hands to another finger therefore does
   not transfer the drag. When a report names a new contact, its real contacts
   are the candidates and the one nearest the last tracked raw position is
   chosen; once tracked, only that contact can move or end the gesture, and a
   report from another slot or another device is ignored. The identity carries
   its own evidence clock: `lastEvidenceTick` moves only when the report proves
   the contact — the tracked slot's `TipSwitch`/`InRange` asserts it with no
   point, or the report carries a position this path feeds. A name-only report
   updates the slot and the identifier for diagnostics, then returns: no
   position, no liveness and no clock write. The same 18 ms idle tick, once no
   session owns the contact, forgets an identity whose last evidence is that old
   — admitted or not — and logs `hid identity expired slot=... latch=0` when no
   contact latch held it, so a refused admission cannot strand a name and make
   every later finger be rejected as another contact.
4. A state machine on that thread drives a `Progress` scalar in a
   `CompositionPropertySet`; an expression translates the root visual by
   `(1 - Progress) * Direction * Distance`.
5. On release it commits when the release position passed 20% of the distance
   (10% on a vertical axis, where the same drag also scrolls the app list) or
   the session-average speed exceeded 0.45 DIP/ms; a commit animates `Progress`
   to 0 and arms a `VK_LWIN` tap (down 60 ms, up) for the end of that retract.
   That tap is the shell's own Start toggle: its key handler performs
   `CStartExperienceManager::Hide`, so the next Windows press opens the menu and
   no re-arm tap exists. Every outcome — dismiss, cancelled reveal, reveal
   commit and dismiss cancel — runs one settle: a damped spring that starts at
   the component of the release speed toward the target, measured over the
   widest eligible window of the last reports — 8-30 ms wide, taken backward
   from the newest stored position — projected onto the gesture axis and
   divided by the travel left. The commit test above deliberately keeps the
   whole-gesture average: only the settle's seam reads the end of the gesture,
   which is why a finger that dragged and then rested starts from rest instead
   of at the speed of the drag.

   ```
   y(t) = 1 - e^(-zeta*omega*t) * (cos(wd*t) + ((zeta*omega - y0)/wd) * sin(wd*t))
   y0   = (gain * velocity) / remaining      wd = omega * sqrt(1 - zeta^2)
   ```

   That is the underdamped form. The default `Windows 11` damping 1.72 is
   overdamped, so its response replaces `cos`/`sin` with `cosh`/`sinh` and uses
   `wd = omega * sqrt(zeta^2 - 1)`; `Windows 10` at 0.9 uses the form above.
   Both come from the same generator, which also carries the critically damped
   case between them.

   `y` is the normalised travel from the release position to the target and `y0`
   its initial slope. The duration is planned from a **speed**, not fitted as a
   time, so a larger panel takes longer instead of moving faster:

   ```
   T = clamp(remaining / 2.2 DIP/ms, 160 ms, 520 ms)
   ```

   with `remaining = |target - start| * distance`. The spring's frequency is
   derived from `T` (`omega = 5 / (slowest root factor * T)`), so the curve keeps
   its shape. That factor is `zeta` while the spring still oscillates and
   `zeta - sqrt(zeta^2 - 1)` once it is overdamped, which is the default: five
   time constants of the **slowest real root**, not of `zeta`, land on `T`. The
   curve is then scaled by its own value at `T`, because five time constants
   leave the analytic curve `1 - e^-5`, about 0.7%, short of the target. That
   remainder spreads over the whole animation: the samples converge, the motion
   is 0.7% larger, there is no step, and the final position is still exact.
   Writing the target on the last keyframe hopped 3.8 DIP (7.6 px) after a frame
   that had moved under 0.1 DIP, on a 501 DIP travel. Segments are sampled at
   most every 4 ms, so the polyline stays smooth at 120 Hz.

   The close's `VK_LWIN` tap is scheduled at `T`. The close record is published
   three times — the plan at the arm, the `tapped` record with a fresh sequence
   immediately before the tap, and the cancelled kind if a new pull drops it —
   and only the `tapped` record tells the bridge that the hide this tap causes
   needs no retract lead.

   `Progress` has one position owner. `TakeProgressNow()` answers from the
   recorded keyframe curve (or from the pinned coast value while the panel is
   coasting), pins that value as the new base and clears the record. The value
   written as an animation's base is the **target**, so while that animation runs
   nothing may read it as a position: every interruption — retract, dismiss
   cancel, reveal takeover and drag takeover — starts from the owner's answer.

   Two parameter sets ship, chosen by the **Menu animation style** setting
   (`AnimationStyle`, default `Windows 11`). Both run the same spring
   generator — only the damping differs, so a style switch changes the feel
   and never the mechanism:

   - `Windows 11`: damping ratio 1.72, fitted on this machine to the native
     Quick Settings flyout (`Win+A`), which uses the same Fluent motion; the
     curve is stretched to the panel's travel, so the fit supplies the shape and
     the speed supplies the time. See "How the Windows 11 numbers were
     measured" below.
   - `Windows 10`: damping ratio 0.9, the spring this mod shipped with.

   The duration is not a setting: the travel left divides by the flyout's
   2.2 DIP/ms and clamps to 160-520 ms, so a bigger menu takes longer instead
   of moving faster. A Windows-key open runs the same settle from rest, so
   `AnimationStyle` also decides how a keyboard open looks.

   **Animation start speed (% of swipe speed)** (`GestureStartSpeedPercent`,
   default 100, clamped 0-300) is the settle's start speed as a percentage of
   the finger's end speed toward the target, over the travel left, in either
   style. The end speed is two checks on the same report history. Freshness:
   the newest stored position must be no more than 30 ms older than the
   release, or the answer is 0, so a finger that moved, held with only asserted
   tip-down reports and then lifted starts from rest instead of at the speed
   from before the hold. Span: from that newest position backward, the oldest
   sample is the first one at least 8 ms and at most 30 ms older than it, so
   the pair is 8-30 ms wide and may lie up to 60 ms before the release when the
   stream paused just before the lift. The change across that pair, projected
   onto the gesture axis, is what counts: 100 means the component that moves
   the panel toward where it is going, not the raw release vector.
   The end-value normalisation adjusts that seam slightly: it divides the whole
   motion by the curve's value at `T`, clamped to 0.5-1, and that divisor depends
   on the damping and the initial slope. At zero initial slope it is 0.99249 for
   `Windows 11` and 0.99589 for `Windows 10`, so 1.007 is the Win11 example of
   the seam factor and 1.004 the Win10 one. The first keyframe lands up to 4 ms
   into the curve, so the panel's first segment runs at that segment's average
   speed, not at the analytic seam: the continuity with the finger is
   approximate. A higher value snaps away faster,
   0 starts from rest. A finger held still before lifting reports `vel=0.00`, so
   it starts from rest whatever the setting says;
   Windows-key opens and closes have no finger speed either and start from rest
   too, so this setting does not change them — the style does.

   中文说明：

   - **菜单动画样式**（`AnimationStyle`，默认 `Windows 11`）只改弹簧的阻尼，
     两个选项跑的是同一条弹簧：`Windows 11` 为 1.72，在本机上对着原生快速
     设置面板（Win+A）拟合；`Windows 10` 为 0.9，是本模组最初发布的手感。
     时长不是设置项：剩余行程按 2.2 DIP/ms 折算，夹在 160–520 ms 之间，
     菜单越大动画越久，而不是动得越快。用 Windows 键打开菜单跑的是同一套
     从静止起步的动画，所以这个样式同样决定键盘打开菜单的样子。
   - **动画起步速度（占滑动速度的百分比）**（`GestureStartSpeedPercent`，
     默认 100，范围 0–300）决定松手瞬间菜单以朝向目标的速度分量的百分之几
     继续滑动，两种样式相同。末端速度有两条相互独立的检查。新鲜度：最新的
     位置样本比松手时刻早 30 ms 以上时按 0 计，所以手指先移动、随后只发按住
     状态报告、然后抬起时，不会带着按住之前的速度起步。跨度：从该最新位置向
     前回溯，最老的样本取第一个比它早至少 8 ms、至多 30 ms 的点，所以这一对
     跨度为 8–30 ms，报告流在松手前刚停顿时最远可以落在松手前 60 ms。这一段
     的变化量投影到手势轴上，所以只有真正推动面板朝目标走的那部分计入：100%
     使用该分量，而不是松手时的原始速度向量。曲线按自身在
     `T` 处的值（夹在 0.5–1）归一化，会略微调整接缝速度；这个除数取决于
     阻尼和起始斜率：起始斜率为零时 `Windows 11` 为 0.99249、`Windows 10`
     为 0.99589，所以接缝倍率以 `Windows 11` 的 1.007 为例，`Windows 10`
     为 1.004。首个关键帧最多落在 4 ms 之后，面板首段走的是这一段的平均
     速度，而不是解析接缝速度，与手指的衔接是近似的。更大值甩得更快，0 从
     静止起步。手指停住再抬起时 `vel` 为 0.00，
     所以无论设多少都从静止起步；用 Windows 键打开或关闭菜单同样总是从静止
     起步，这个设置不影响它们——影响它们的是上面的样式。

`Progress` is written whenever the finger moves 0.5 DIP, so the drag is
continuous rather than stepping on a progress grid.

The keyboard path runs the same settle: a closed→open transition that no gesture
owns calls `BeginGestureSettle(0, 1, 0)`, the gesture open from rest, so a Win-key
open logs `settle style=... dir=open vel=0.00 dur=...` and then
`win open dur=...`. One curve serves every open and every close, and the travel
plans the keyboard open's duration exactly as it does a gesture's: 520 ms at the
1440 DIP travel of this layout, 409 ms for the 900 DIP panel. `AnimationStyle`
applies to it unchanged; `GestureStartSpeedPercent` has no finger speed to scale,
so the keyboard open always starts from rest. A Win press and an edge pull run
one curve, not two. Every closed→open transition also publishes a fresh
**open generation** on the channel: that number is the identity of this menu,
and a retract request is bound to it. The same open transition writes the
cancelled close kind, so an unconsumed `tapped` record from the previous menu
cannot survive into the next one, and generation 0 is never a menu:
`SwipeIpcReadOpen` reports `ERROR_NOT_READY` for it. A Win-key close is animated
too: `twinui.pcshell.dll` `CStartExperienceManager::Hide` is where the shell takes
the menu away, so the explorer-side bridge asks the Start mod to retract and holds
that call until the retract is over. The request carries no duration — only the
Start side knows the panel's travel — so the mod answers with the settle it just
planned (409 ms for the 900 DIP panel at 2.2 DIP/ms) on the same shared channel,
and the bridge waits that plus 30 ms. What the request does carry is the open
generation the bridge saw and a 700 ms deadline: a request whose generation is
not current, whose deadline has passed, or that the bridge has already marked
done is answered with the no-settle sentinel and starts nothing. Without an
answer it falls back to a fixed 560 ms. Measured with the menu fully open before
the channel moved to shared memory: the request was written at
`23:15:09.588` and the answer at `23:15:09.654` (66 ms, one 50 ms debug poll in
its slow phase), the renderer logged `settle ... dur=409` and
`debug retract from=1.000 dur=409`, the bridge logged
`hide delayed reason=1 retract=409 lead=439`, and the window was cloaked 549 ms
after the key-down, 50 ms after the settle it had answered with ended. On the
channel the answer needs no poll: the bridge measured `retract=520 lead=550`
with the request answered on the same event cycle.

The panel's own travel ends sooner than the settle's clock: its bottom edge went
1351 → 870 → 640 → 448 → 150 px and left the screen 192 ms after the key-down,
100 ms into the settle, because the fitted spring covers 70% of the travel in
105 ms. Frames taken 350 ms after the key-down — before the old 350 ms lead
expired — and at 500 ms are therefore pixel-identical to the closed desktop: at
the old hide instant nothing of the panel was left to cut on this layout. The
lead now follows the settle instead of a constant, which is what a menu whose
panel reaches closer to the screen edge needs.

A gesture close needs no retract command. Its retract starts at the release and
its tap comes at the end of it; the bridge sees an unconsumed `tapped` record
inside its 800 ms window and calls the shell's `Hide` at once, because the panel
is already off screen. The plan that preceded it and the cancelled kind are not
taps, so neither passes a Hide. The tap is what updates the Start toggle, and it
is the only tap: there is no programmatic `Hide` and no 120 ms re-arm tap, so a
press cannot collide with one.

A close of either family runs the same ownership transition: suppression window
opened, the dismiss and reveal sessions ended, a contact still on the glass
muted so its release cannot settle the panel open, the 1 ms timer period
released, and — for a keyboard close — an armed tap dropped, because the Hide it
serves would otherwise toggle the menu back open. The mute is read from the
contact latch and either gesture family before either owner is cleared, so a
keyboard close that lands while the reveal owns the finger mutes that contact
too. Without that state the next report of the same contact dragged the panel
back and its release settled the menu open over a close that was already running.

A close exists once its settle starts. `BeginDismissSettle` reports three
outcomes: not started, started, and started with a lost batch observer
(`close observer lost kind=...`). A lost observer does not put the panel back:
the settle runs and its tap is the close, so the panel never flashes back. Only
a close that provably never started calls `AbandonClose(kind)`
(`close abandoned kind=...`), which restores the panel; that is the whole
operation, because such a close published no plan and armed no tap, so there is
nothing to cancel.

Unload is prerequisite-driven. The one stop event is created before any thread
starts; without it the mod starts no raw-input thread and logs
`hid thread not started (stop event unavailable)`. The input thread waits on
that event beside its messages in 10 ms slices, the module-scan thread waits on
the same event, and the 60 ms Windows tap waits on it too and always releases
the key. Unload stops the module-scan thread (10 s wait), sets the event, waits
10 s for the input thread, revokes every XAML delegate on the UI thread and
waits for that turn, detaches the settle batch's `Completed` handler, and drains
every counted entry into module code (2 s bound), then frees the composition
state and the IPC view. A thread or callback that misses its wait leaves every
resource in place and logs `unload residual=...; process must restart to finish
unloading`; the timed-out thread's handle stays open so a later attempt can join
it, a joined thread's handle is closed, and the hid-thread case also logs
`unload hid thread still running; ipc view leaked` instead of unmapping a
section the running thread still touches. A counted entry that does not drain
logs `unload entries outstanding=%d`. A XAML delegate that could not be revoked
keeps its token and its owner, because that token is the only handle on a
handler that can still fire, and a new registration is refused while one is
outstanding (`xaml size handler left attached; new registration refused`, the
same for `loaded`; the watches log `xaml layout handler left attached; new
registration refused` and `xaml viewchanged handler left attached; new
registration refused`). The root then gets no renderer, the layout watch leaves
that open to its 400 ms deadline, and the scroll pass adds no watcher; the
unload logs `unload residual=xaml handlers; process must restart to finish
unloading` while one survives. The `SizeChanged` and `Loaded` callbacks capture
the setup generation when they are registered and return when it has moved, so a
late callback from the previous root touches nothing. The layout watch mints a
serial and captures its own measurement request by value, so a callback that
outlived a newer watch measures and revokes nothing. The `ViewChanged` handler
captures the `ScrollViewer` it was registered on and may write the cached
offsets only while that element is `g_scrollerCacheViewer` and the cache is
`Scrollable`, both under `g_scrollerMutex`.

That drain is only meaningful because the image cannot go away. The pin and the
instance guard are the two prerequisites of `Wh_ModInit`: either failing returns
failure with `mod init refused: ...; no hook, no channel and no thread
installed`, and `g_initRefused` short-circuits the later lifecycle callbacks. A
successful pin
(`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
GET_MODULE_HANDLE_EX_FLAG_PIN)` on an address in the module, logged
`mod pin image=... pinned=1`) is what keeps the image mapped; a failed pin logs
`mod pin failed err=...` and installs nothing. `void Wh_ModUninit` cannot veto
Windhawk's unload, and the mod held no reference on its own image before this
pin, so returning from that call would let Windhawk free the module while a
thread is still inside it. A pinned image cannot be unmapped: the module stays
in the process until it exits, by design, and that is what makes late-running
thread code and late delegate calls safe. The pin keeps code mapped, not
resources: a residual unload still needs a process restart before anything the
leaked thread or entry touches can be freed. One instance per process:
`Wh_ModInit` takes `Local\WindhawkLegacyStartSwipe.instance.<pid>` before it
creates the stop event, opens the channel or starts a thread. The name carries
the PID, so it excludes another instance in that process, not every process in
the session. A second instance logs
`instance guard refused: another instance is live in this process`, resets
nothing and installs no hook, no thread and no channel; only a clean unload
releases the mutex. A re-initialisation of the same pinned image after a clean
unload resets the lifecycle state (`ResetReinitState`) and then runs one
per-activation reset
(`ResetActivationState`, logged `activation state reset`), which the input
thread's exit runs too: the tracked contact, the contact latch, both sessions,
the close mute, the deferred flag, the pending tap and the pull generation it was
armed under, the pull serial, the coast, both velocity windows, the diagnostic
latches and the `anim` override all start over. Every entry into module code from
the compositor or the dispatcher is counted from before it can run: the batch
completion takes a
`ModuleEntryLease` before `batch.End()`, and every UI turn posted through
`PostCountedUiTurn` is counted. The unload's revocation turn is the exception by
design: it uses `RunAsync` plus an event and waits for it, not the counted post,
because the unload must know the revocation ran before it frees the objects that
turn holds.

A new edge pull during the retract cancels the close on the Start side: the
armed tap is dropped before it is sent (the record becomes the cancelled kind),
no `Hide` runs, the shell's toggle is never touched, and the pull's own follow
keeps driving the panel. A Win press during the retract is a keyboard close: the
plan is current, so the bridge gives that `Hide` its own lead, and the Start side
drops the armed tap when the request arrives, because that tap is what would
toggle the menu back open. The earlier design returned `S_OK` from
the bridge's `Hide` without calling it; measured, one such abort left the
Windows key and the taskbar Start button dead until
`StartMenuExperienceHost.exe` restarted, so every path calls the original.

Release detection: the digitizer keeps reporting while the finger rests
(measured 255 reports in a 1.5 s hold), so an 18 ms window without a report is a
reliable lift. On a device whose reports carry `TipSwitch`, the tracked slot's
lift report ends the contact instead: the release line says `src=report`, and the
18 ms window is only the fallback for a device that goes silent or a contact that
leaves without a lift report (`src=idle`). A report that carries only a contact
count, or no contact field for the tracked slot, says nothing about it, so a
count-only lift no longer ends a tracked contact; the idle expiry does, with
`src=idle`, and once no session owns the contact the same 18 ms latch clears the
contact flag and forgets the tracked identity. A report that names the tracked
contact and asserts it (`TipSwitch` down or `InRange`, or a slot state of
Contact) but carries no point is not silence: it refreshes the liveness clock
and the identity's evidence clock, and moves nothing.
Measured layouts on this
machine: ELAN2513 Col01 touch is five contact collections with `Tip` on each,
Col05 pen carries tip and in-range, SYNA3580 is five with `Tip` each, and the
virtual digitizer is four with `Tip` and `In Range` plus a contact count, report
ID 1. Both halves of the timing matter on this build, and each was measured
separately:

- the idle check needs a **`QueryPerformanceCounter`** silence: `GetTickCount64`
  only advances every 15.6 ms, so an 18 ms window read as 31 ms;
- the idle tick needs a **`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` waitable
  timer**: `WM_TIMER` stayed on the 15.6 ms quantum even while the process held
  a 1 ms `timeBeginPeriod(1)` request. The request is still made, paired through
  `timeEndPeriod`, for the time a contact is down.

Measured release latency (`relms` on the release line): 31 ms before, 19-23 ms
after. `vel=` on the `hid release`, `open follow` and `settle` lines is always
DIP/ms toward that settle's target, so a finger held still reads `vel=0.00` on
all three.

## How the Windows 11 numbers were measured

`tools/native_flyout_frames.ps1` captures the native
Quick Settings flyout (`Win+A`) as 32bpp frames; `tools/native_flyout_track.py`
fits a vertical offset of each frame against the settled frame (Pearson
`r >= 0.999` over the card's interior). At capture time the taskbar was on top, so
`ControlCenterWindow` (768x1704 physical) opens anchored to the **top**-right
corner and its content unfolds **downward**: the leading edge is the panel's
*bottom* edge, not the top one, and `dy_hidden = 1252 - 96 = 1156 px` (578 DIP)
is the whole travel.

Sampling was 9.2 ms per frame (a 200 px wide strip, one `BitBlt` into a reused
DIB), and that capture stamped `t` after the `BitBlt` and its copy, so the times
below are late by the read-back (a few ms); the current tool stamps before the
`BitBlt` and records the copy's cost as `cap=`. `DwmFlush` before each `BitBlt`
costs a whole compositor interval on top of the ~8 ms a read-back already takes,
which dropped the rate to 17 ms; the read-back returns the last presented frame
anyway, so the offset to the presented content is 0-8 ms. `IsWindowVisible` on
`ControlCenterWindow` (not `DWMWA_CLOAKED`, which stays 0) was polled every 1 ms
as the time reference.

Normalised displacement, open, `t` from the window becoming visible:

| t (ms) | d | note |
|---|---|---|
| 0 | 0.00 | window visible, panel still above the window top |
| 10.3 | 0.00 | still absent |
| 29.7 | 0.54 | first visible frame |
| 37.5 | 0.60 | |
| 45.7 | 0.65 | |
| 55.9 | 0.69 | |
| 72.1 | 0.76 | |
| 89.8 | 0.81 | |
| 104.7 | 0.85 | |
| 120.4 | 0.89 | |
| 137.6 | 0.92 | |
| 153.9 | 0.94 | |
| 171.4 | 0.96 | |
| 187.2 | 0.97 | |
| 204.1 | 0.98 | |
| 220.1 | 0.99 | |
| 228.8 | 0.993 | |
| 253.0 | 0.999 | last frame that still moved (1 px) |
| 261.6 | 1.000 | at rest |

Duration from the window becoming visible to rest: 262 ms. No overshoot in
either direction (`dy` decays monotonically to 0). The close is the mirror in
kind but not in timing: the panel accelerates upward and is gone 135 ms after its
first moving frame (0.04 at 25 ms, 0.19 at 75 ms, 0.59 at 122 ms, hidden at
~137 ms); no overshoot.

Fitting the mod's spring with `y0 = 0` to this curve (area between the curves
over 0..306 ms) gives **period 102 ms, damping 1.72**: area 10 ms of
displacement, mean `|y - d|` 0.034, and at most 0.012 after the first 90 ms. The
second open run (17 ms frames) fits 84 ms / 1.98 with the same 247 ms duration,
so the ridge is real: the data fixes the decay time (about 50 ms) and the
duration (about 250 ms), not the pair's exact split. The native curve is faster
than any `y0 = 0` spring in its first 30 ms (0.54 of the travel against 0.38);
the shell starts that animation with its own initial velocity, which a settle
that must continue the finger cannot copy.

Initial slope at a release half open (`Distance` 1440 DIP, finger 0.4 DIP/ms):
the spring (either style) moves the panel at 0.4 DIP/ms, 4.0 DIP in the first
10 ms. The Windows 11 style's earlier fixed 300 ms eased pair had an initial
slope of 9x its average, i.e. 21.6 DIP/ms and 216 DIP in the first 10 ms — that
is the jump a release at half open produced, and the reason that style now runs
the spring.

## Opening follows the finger too

The left-edge swipe is Windows' own gesture: `twinui.pcshell.dll`
`TouchEdgeGestureProcessor` calls `CStartExperienceManager::Show(..., 16)` and
`StartUI_.dll` starts its own reveal animation. ExplorerPatcher is not involved.

The mod cannot suppress that animation: hooking
`StartUI::SplitViewFrame::OnGlobalAnimationRequested` and skipping the `type=0`
request also skips presenting the panel, which leaves a window that DWM reports
as shown but that draws nothing. The hook is installed on every load and passes
every request through: only the debug command `anim=0` drops the open request,
and the switch starts at `anim=1`, so the default never suppresses anything.

Instead the mod takes over after the shell opens the window:

- A touch that starts within 12 DIP of the taskbar's screen edge while the menu
  is closed becomes an edge candidate. Raw Input sees it from the first report,
  before the shell opens anything.
- Once the menu is open and the contact is still down, every report drives
  `Progress` from the **absolute** finger position and the panel's trailing edge:

  ```
  Progress = 1 - (finger - panelTrail) / (direction * distance)
  ```

  The trailing edge is the panel side facing away from the taskbar — the one
  that meets the finger. It is measured on the UI thread from the root's visible
  child (`StartSizingFrame`; ~1053 DIP on a left taskbar, the panel's top edge at
  about 277-289 DIP on this bottom-taskbar layout, 289 measured on one host and
  277 on another), translated by `ClientToScreen` of the host window's client
  origin — not by the work-area origin. Axis, `Distance`,
  the whole panel rectangle, the XAML origin, the work area and the monitor are
  one `GestureGeometry` snapshot behind a short writer lock: the UI thread is the
  only writer and holds an `SRWLOCK` exclusively for one struct assignment; the
  input thread tries a shared acquisition per sample and answers its last whole
  copy on contention, because a seqlock's counters do not legalize reading a
  plain struct while the writer assigns it. The first report of a new contact
  takes the lock blocking when its cached copy has an obsolete layout identity,
  and that read has no time bound if the writer is descheduled. The snapshot
  also carries the root/open generation and a layout identity token, so a cached
  copy from before a resize is rejected for a new gesture instead of starting
  from an old rectangle. The trail is derived from the rect by one function, so
  a rect and its edge cannot disagree, and the hit test, the
  follow, the release and that report's log line all describe one panel. The
  legacy `reveal panelTrail=...` line prints that derived edge with the axis, the
  rect, the work area, the band it was tested against and the generation. A
  measurement is accepted only when the rect lies on the monitor and its
  taskbar-side edge lies inside the band the taskbar itself defines: the bar's
  reserved strip at the screen edge, extended by one bar thickness toward the
  desktop. The band comes from `SHAppBarMessage(ABM_GETTASKBARPOS)` or from the
  `Shell_TrayWnd`/`Shell_SecondaryTrayWnd` rectangle, and from the work-area
  inset only when the probe leaves no band for this monitor; auto-hide makes
  that inset zero by design. Measured on this bottom-taskbar layout, the settled
  panel sits 12 DIP inside the bar edge (`840` against `852`), so the band is
  `804..900` and the open storyboard's rect is rejected whole; a rejection
  publishes nothing and
  logs `reveal panelTrail rejected ... axis=... rect=... work=... mon=...
  band=...` once per open, from the first pass that sees a rejected rect; the
  last whole snapshot stays. The previous rect is
  carried over only when the whole layout identity — axis, travel, client
  origin, work area and monitor — matches (±0.5 DIP on travel). The axis is part
  of that identity, so an axis-only change bumps the token, drops the carried
  rect and republishes the trail. Renderer scalars
  are written first and `g_dismissDistance` advances only when they took the new
  travel, so a failed or skipped write leaves the applied distance where the
  compositor holds it; a scalar write that throws after an earlier one landed
  marks the renderer invalid (`gesture renderer invalid until a successful
  reapply`) and refuses gestures until a full reapply. The apply reservation
  (`LayoutApplyGuard`) and the admission claim (`ClaimPanelForAdmission`) are
  `seq_cst`, so either the layout is deferred whole or that admission is refused
  and retried on the next report. A layout change of any kind — an open, `WM_DISPLAYCHANGE`,
  `SPI_SETWORKAREA` or `SizeChanged` — is deferred whole while a contact owns the
  panel: renderer scalars, panel rect and scroller cache are applied together by
  one settled pass after the release
  (`gesture geometry deferred reason=panel-owned gen=...`), so the edge, the
  axis, the travel and the scroller rect never move under the finger. The first
  measurement of an open lands while the shell is still animating the panel in
  and fails that rule; one request — the
  layout watch's second pass or its 400 ms deadline, whichever runs first —
  carries a request id and an epoch. Its decision and its write are one critical
  section under `g_measureMutex`, the commit lock every epoch advance also takes,
  so a close, a newer request or a resize cannot slip between them; all XAML
  measurement stays outside that section. The panel publish re-checks the id,
  the epoch, the axis and travel, and panel ownership at the moment it writes,
  its scroller commit re-checks the id, the epoch, the layout version and panel
  ownership, and a request that a close or a newer request replaced writes
  nothing. An idle
  `SizeChanged` also re-measures the scroller, not only the panel, so a press
  cannot be judged against the list rect of the old layout. With that anchor the
  panel edge equals the finger: measured within 0.5 DIP residual
  at 150, 450 and 800 DIP on the left taskbar, and within 5 DIP of the finger on
  the bottom.
- A vertical dismiss (top or bottom taskbar) yields to the menu's own
  scrolling. The mod caches the innermost ScrollViewer under the panel
  (StartUI.AllAppsGridListView; `ScrollableHeight` measured 3664 on the earlier
  left-side run and 4077 on the later top-taskbar run, offsets refreshed on
  ViewChanged) plus its screen rect, in three states. Every remeasure publishes
  `Unknown` first, so a pass that fails or stops short cannot leave a previous
  `Nothing` or `Scrollable` speaking for a tree it no longer describes, and the
  cached answer carries the layout version the gate checks beside the open
  generation. Unknown means no
  measurement speaks for this open — nothing measured yet, a failed measurement,
  or only the pass taken while the shell still moved the window — and it defers
  a press inside the panel while a press outside it still passes. Nothing
  scrollable means a settled, complete search found nothing to scroll, so the
  press dismisses; a search that threw or hit its depth limit stays Unknown and
  retries with 100/200/400 ms backoff, three attempts, logging
  `scroller retry why=origin|incomplete|not-allowed|offsets|commit gen=... try=.../...`
  once per open and `scroller retry exhausted gen=...` when the budget is spent;
  a search that ran under a contact stays Unknown and is posted again when that
  contact ends. A completed negative result revokes the old viewer watch, so no
  handler keeps refreshing offsets the gate can never use. Two variables own
  that watch, not one. `g_scrollerWatchOwner` is the element whose
  `ViewChanged` token must still be revoked: a failed revoke keeps it for a
  retry, and a replacement registers no watch while it survives
  (`xaml viewchanged handler left attached; new registration refused`).
  `g_scrollerCacheViewer` is the element the committed rect and offsets were
  measured from. A `ViewChanged` handler may write the cached offsets only
  while the element it was registered on is that cache viewer and the cache is
  `Scrollable`, both under `g_scrollerMutex`. When a replacement cannot be
  watched, the gate keeps the new element's rect and the offsets that same pass
  read — the correct list, possibly with stale end-of-list numbers — instead of
  going `Unknown`, which would defer every press inside the panel for the rest
  of that open. A positive candidate commits even when another branch was
  unreadable, because refusing it would leave the gate Unknown for certain.
  Scrollable carries the rect and offsets, and a downward press
  inside it only starts a dismiss when the list is already at its end
  (`VerticalOffset <= 24` for a bottom taskbar, mirrored for a top one — the
  allowance a fling needs). Only the unknown state defers. The same contact
  starts the dismiss as soon as its scrolling reaches the end.

  The current layout has the Windows 11 taskbar auto-hidden at the bottom:
  `dismiss axis=3`, `Distance` 900, panel rect `236,289..1229,840` on the host
  that logged it, so the trail is the panel's top edge — about 277-289 DIP on
  this layout, 277 on one host and 289 on another — and the band it is tested
  against is `804..900` (`reveal panelTrail=289 axis=3 ... band=804,900
  gen=3`). The vertical path was exercised most fully on the earlier
  top-taskbar layout: `dismiss axis=1`, panel about `236,36..1229,587`, scroller
  about `309,66..569,586` with `vo=0 sh=4077`. There an upward drag inside the
  panel whose list cannot scroll commits and taps. A drag in the long list
  scrolls first (`dismiss deferred to scroll vo=0 sh=4077`), the cache follows
  `ViewChanged` (vo 277/1214/2194/3135), and the same swipe turns into a dismiss
  once the list reaches its end. A work-area broadcast during a held drag defers
  the whole turn (`gesture geometry deferred reason=panel-owned gen=5`), and the
  release keeps the axis and travel it started with. The mod honours only a
  broadcast: `SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE,
  SPI_SETWORKAREA, ...)`, because the `wParam` must be `SPI_SETWORKAREA`
  (`0x002F`; `0x0029` is ignored) and a `PostMessageW` to the mod's hidden
  window fails with error 1159 on this host.
  A burst of work-area changes leaves the scroller gate `Unknown` for that
  open: every remeasure publishes `Unknown` first and a pass whose request a
  newer one replaced commits nothing, so presses inside the panel are deferred
  there rather than dismissed.
- Release is decided symmetrically with the dismiss gesture: `travel` is how far
  the finger moved away from the edge since the touch began (the release value,
  not the peak), and the menu opens fully when `travel > 20%` of the distance
  (10% on a vertical axis) or the session-average speed exceeds 0.45 DIP/ms.
  Otherwise the mod retracts the panel and taps the Windows key at the end of
  that retract, so a short slow pull, or pulling out and pushing back, does not
  open the menu; the bridge passes that Hide at once because the `tapped` record
  says the panel is already gone. The settle starts at the end-window component
  toward the target, not at that average: a short pull that stopped before
  lifting settles from rest.

An earlier version anchored progress to the frame in which the menu was first
seen open and used the root width as the distance; that lagged the finger by
565 DIP and made the commit decision depend on swipe speed. Both are fixed.

## Data sources that do not work

Each of these was measured on this build, not assumed:

| Source | Result |
|---|---|
| XAML pointer events | `PointerDeviceType.Mouse` (0), one `move` per drag, `axisDelta ≈ 0` |
| `CancelDirectManipulations` | returns false |
| UDK `ShellViewGestureAnimationPropertySet` | `AnimationProps.Translation` resolves but is constant 0; `Width`/`Height`/`VisibilityProgress` return `0x80070057` |
| XAML `DManipDataBase` cache (`CUIElement+0x118 → +0x138 → +0x58`) | empty during a drag |
| `DManipDataBase::SetContentOffsetsHelper` hook | installed, **0 calls** across three real drags |
| System cursor (`GetCursorPos`) | `CURSOR_SUPPRESSED` for the whole contact, position frozen |
| Queue (`GetMessageW`/`PeekMessageW`) | `WM_POINTERENTER` + `WM_POINTERDOWN`, then `WM_POINTERCAPTURECHANGED` and silence |
| `GetPointerInfo(id)` / `GetPointerFrameInfoHistory` | valid id, but a frozen down snapshot |
| `RegisterPointerInputTarget` | requires UIAccess, unavailable to an injected mod |
| Composition `IDCompositionVisual` read-back | write-only, no getter |

The short version: the touch never reaches XAML on this UI, and the window queue
is closed after the down, so the only in-process source is the digitizer itself.

## Mods

- **`mods/start-menu-legacy-swipe.wh.cpp`** (`StartMenuExperienceHost.exe`,
  0.29.0) — the shipping mod described above.
- **`mods/msgprobe.wh.cpp`** — diagnostic: logs `WM_POINTER*` and mouse messages
  pulled from the message queue. Proved the queue dead end. Not needed at runtime.
- **`mods/inputprobe.wh.cpp`** — diagnostic: raw HID decoding without the gesture
  logic. **Do not enable it together with the shipping mod**: Raw Input allows one
  sink per usage per process, and inputprobe takes the stream for itself.
- **`mods/start-menu-touch-gesture-bridge.wh.cpp`** (`explorer.exe`, 0.15.0) —
  hooks `CStartExperienceManager::Hide`. Only a Hide on a `tapped` close record
  the bridge has not consumed, inside the record's 800 ms window and not older
  than the newest open tick it saw, runs at once (`hide passed (gesture)`); the
  plan and the cancelled kind never pass one. A fresh stamp is correlation, not
  proof of cause. Any other Hide is a keyboard close: the mod publishes the
  `retract` request —
  bound to the open generation it saw and a 700 ms deadline — and holds the
  original for the settle the Start mod answers with, plus 30 ms (fixed 560 ms
  when no answer or the no-settle sentinel arrives), then marks the request done
  so it cannot start a settle under the next menu. A dropped gesture close
  publishes the cancelled kind, which takes the keyboard path. Publishes the
  taskbar-edge gesture handle/state from `twinui.pcshell.dll` to shared memory,
  which the shipping mod does not read. The in-flight `Hide` count is a
  `HideCallLease` (RAII, exactly one release per frame, taken before the hook
  reads anything) that spans the original call, so a count of zero also means no
  frame of that hook can still need the MinHook trampoline. On unload it keeps
  new Hides out and waits for that count to reach zero before it closes the v2
  channel (`ipc close`); quiescence is the condition, and 500 ms is only the mark
  where a call still live is reported
  (`ipc unload drain over 500 ms hides=...; still waiting for quiescence`). The
  drain's bound is 10 s — above the mod's own hold (660 ms for the shipped pair,
  at most 2.13 s for any answer the channel accepts) and with room for a thread
  the scheduler kept off the CPU — and a Hide that reaches it keeps every handle
  (`ipc unload residual hides=...; the v2 channel stays until explorer exits`).
  The host order was established from the installed engine's upstream tag
  (`Windhawk v2.0.0-alpha.8`, `src/windhawk/engine/mods_manager.cpp`): disable
  hooks, `Wh_ModUninit`, `ThreadsCallStackWaitForRegions(..., 200, 400)`, remove
  hooks, `FreeLibrary`, with that wait's result discarded. "The scan found the
  frame" and "the frame exited before removal" are therefore two different
  claims, and only the first is the host's. The bridge pins its own image as a
  prerequisite, before the channel and the hooks
  (`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
  GET_MODULE_HANDLE_EX_FLAG_PIN)`, `mod pin image=... pinned=1`); a failed pin
  logs `mod pin failed err=...` and returns FALSE from `Wh_ModInit`
  (`mod init refused: the image could not be pinned; no hook, no channel and no
  thread installed`), because a shell-owned `Hide` thread needs valid return code
  as much as a worker would. The pin keeps that module's code and the
  `HideCallLease` destructor mapped for a frame that returns after
  `Wh_ModUninit`; the trampoline under a frame is the host's allocation, and a
  scan blind to the frame is outside what any mod can fix — the hazard is
  reduced, not eliminated. A re-enable re-initialises the same image without the
  loader resetting anything, and refuses while a Hide from the previous
  activation is still inside
  (`mod init refused: a Hide from the previous activation is still inside; ...`);
  otherwise `ResetReinitState()` clears `g_disabled`, `g_afterInit`,
  `g_explorerHooked`, `g_hideDelayEnabled` and `g_startPointTick`, retries a
  failed gesture mapping, leaves `g_hideCalls` alone, and logs
  `activation state reset`; a channel a timed-out unload kept open is reused
  rather than remapped.
  The `Local\WindhawkStartGestureShared` mapping stays for the process lifetime
  on purpose, because a consumer and a late gesture callback may still hold its
  view. **Enable it together with the Start mod**
  for the slide-out; without it the menu still closes (the tap is the close), but
  instantly.
- **`mods/start-menu-touch-gesture-consumer.wh.cpp`** — diagnostic: reads the
  bridge's `Local\WindhawkStartGestureShared` mapping and logs the
  state/handle transitions in `StartMenuExperienceHost.exe`. It changes no UI and
  is how that publication is verified end to end.
- **`mods/start-menu-touch-gesture-probe.wh.cpp`** — Phase 0 diagnostic probe.
- **`mods/start_gesture_shared.h`** — shared-memory contract for the bridge's
  gesture publication, read by the consumer probe; the shipping Start mod does
  not read it.
- **`mods/swipe_ipc_shared.h`** — the contract between the two shipping mods and
  the tools: one session-local section, three events and one command mutex, every
  record a seqlock, every field pinned by an `offsetof` assert (672 bytes).

## Build and install

```powershell
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod install --file .\mods\start-menu-legacy-swipe.wh.cpp
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod install --file .\mods\start-menu-touch-gesture-bridge.wh.cpp
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod enable local@start-menu-touch-gesture-bridge
```

The CLI needs an elevated shell. Windhawk reloads a mod in place, but the
`StartUI::App::PrepareRootElement` hook only fires when a new Start root is
created, so restart `StartMenuExperienceHost.exe` (or log off and on) before
testing; the menu then attaches its renderer on the next open. `mod install`
leaves a previously disabled mod disabled, so the `enable` line is required for
the Windows-key close animation; the bridge installs its explorer hooks when it
is enabled, without an explorer restart on this build.

## Test tools

- `tools/screenshot.ps1` — full-screen PNG in physical pixels. The process is
  switched to per-monitor-v2 DPI awareness first, otherwise a 200% display only
  captures the top-left quarter.
- `tools/start_menu.ps1` — reports or forces the menu's open state. Uses
  `DWMWA_CLOAKED`; `IsWindowVisible` stays TRUE while the menu is hidden.
  `-EnsureOpen` invokes the taskbar's Start button (`winapp ui invoke "开始" -a
  explorer --type Button`) up to three times at 1800 ms; on this auto-hidden
  taskbar all three left the menu cloaked, while a `VK_LWIN` tap opens it and
  the `START hwnd=... cloaked=... open=...` line reports it correctly.
  `-EnsureClosed` is one `VK_LWIN` tap.
- `tools/inject_swipe.ps1` — inject a touch swipe (virtual digitizer) through
  `tools/touch_common.ps1`; `-DryRun` prints the sequence instead of injecting.
- `tools/touch_common.ps1` — the one injection lifecycle every tool dot-sources:
  DOWN, UPDATE, a hold at 5 ms, and one UP. `End-Contact` owns stop, join and UP
  in that order, so no UPDATE can follow the release, and it records the UP
  timestamp immediately before the injection. A join that times out sends no UP;
  a failed UP keeps the contact open and reports it, and `ReleaseMs` stays 0 in
  both cases, so a trace shows that no release happened. `touch_hold.ps1` and
  `follow_hold.ps1` call `End-Contact` alone, so a failed stop cannot skip the UP
  through a separate call.
- `tools/touch_hold.ps1` — drag to a point, hold with the shared 5 ms
  keep-alive, capture, then release with `-ReleaseAtEnd`; `-Lift` is kept as an
  alias of it. The keep-alive runs through the capture, so the frame shows a
  live contact.
- `tools/follow_hold.ps1` — the same capture for the reveal follow from the
  screen edge, also keeping the 5 ms keep-alive running while the screenshot is
  taken: the virtual digitizer stops reporting during a plain hold, unlike the
  real panel. `touch_hold.ps1` starts the same keep-alive, so either tool can
  photograph a live hold.
- `tools/inject_swipe_holds.ps1` — swipe with pauses at waypoints; each pause is
  a real 5 ms hold, not 50 ms of silence.
- `tools/measure_swipe.ps1` — samples the Start window rect while a swipe runs
  and writes `artifacts/start-window.csv`; it asks for 8 ms and this host's
  15.6 ms timer quantum delivers ~16 ms, which the file itself shows.
- `tools/read_shared_gesture.ps1` — reads the bridge's
  `Local\WindhawkStartGestureShared` mapping, optionally while injecting a swipe.
- `tools/probe_cursor_follow.ps1` — polls `GetCursorPos`/`GetCursorInfo` while an
  injected touch drag runs and writes `artifacts/cursor-follow.csv`.
- `tools/native_flyout_frames.ps1` — capture the native Quick Settings flyout
  (`Win+A`) as BMP frames at ~10 ms, with the `ControlCenterWindow` visibility
  poll that anchors t=0. The stamp is taken immediately before each `BitBlt` and
  the copy's own cost is recorded as `cap=`.
- `tools/native_flyout_track.py` — fit each captured frame's vertical offset
  against the settled frame and print `t -> dy`.
- `tools/fit_settle_spring.py` — model the shipped spring against
  `artifacts/native-flyout-open.csv` (slowest-root duration, end-value
  normalisation, the shipped damping by default) and print the residuals;
  `--model legacy` repeats the old free-period fit, and the report states that
  the captured data does not identify the damping. It prints the model's first
  sampled frame and its 10 ms position, and labels the initial tangent
  (`velocity * 10 ms`) a straight-line extrapolation, not a position on the
  curve.
- `tools/settle_track.ps1` — pull the menu half open, hold, release, and track
  the panel's bottom edge for 500 ms; its release time comes from `End-Contact`,
  so `t=0` is the UP and not the end of the preceding capture. Its start point
  and scan band are the top-taskbar geometry.

The raw tables behind the Windows 11 fit are in `artifacts/`:
`native-flyout-open.csv` and `native-flyout-close.csv` are the per-frame
`t -> dy -> d` curves of the native flyout (9 ms frames, `d` normalised to the
panel's 578 DIP travel); both were captured with the earlier loop, which stamped
`t` after the `BitBlt` and its copy, so their times are late by that read-back (a
few ms). `settle-trace.csv` is the shipped mod's settle trace in both styles for
the same half-open hold release; it came from the earlier tracker loop, whose
clock starts when the UP injection returns and which stamps `t` after the
`BitBlt` and before the read-back, so its net bias is the `BitBlt`'s copy time
minus the UP call's own time, with no fixed sign — the current tool stamps before
its `BitBlt` and takes t=0 from the UP timestamp. Then
`cursor-follow.csv`, `start-window.csv` and
`gesture-shared.csv` are the cursor, window-rect and bridge-state dumps behind
the data-sources table. `artifacts/AGENTS.md` lists each file with its bias.

The mod has a debug channel for the renderer: one session-local shared-memory
section, three events and one mutex (`mods/swipe_ipc_shared.h`, v2):
`Local\SwipeFor10Start_v2_State`, `_Cmd` (manual-reset), `_RetractReq` and
`_RetractPlan` (auto-reset) and `_CmdWrite` (a mutex serialising command
writers, which `tools/ipc.ps1` takes before it writes). `pwsh -NoProfile -File
tools\ipc.ps1 p=0.5` (also `t=x,y,z`, `e=x,y`
and `anim=0|1`) drives `Progress`, `UIElement.Translation`, the visual `Offset`
or the open storyboard directly, and `-State` prints the counters. Every record
is a seqlock: the writer raises the counter to odd, writes, raises it to even;
the reader retries while it is odd or changed, and a dead writer's counter is
moved forward so a reader never accepts half-written fields. Init is a
claim/ready handshake: `SwipeIpcOpen` may wait up to the whole 550 ms budget (the
500 ms abandon age plus one 50 ms window), so a young claim is abandoned inside
the call; it tells "not ready" (`ERROR_NOT_READY`) from "broken"
(`ERROR_INVALID_DATA`), and neither side ever recreates a section. An `initState`
outside the four values the header defines is refused at once with
`ERROR_INVALID_DATA` instead of being waited on, and every retry path checks the
deadline, so no state can keep the caller past the 550 ms budget. The command
event wakes the raw input thread, so nothing polls. The bridge's `retract`
request and the Start mod's answer share the same section; the answer echoes the
request's sequence number, and the bridge holds
`CStartExperienceManager::Hide` for that duration (409 ms for the 900 DIP panel,
and 560 ms when the answer says no settle started or never arrives). Both sides
record the sequence numbers they find at attach, so a command or request from
before the process started is ignored instead of replayed.
`SwipeIpcReadOpen` refuses generation 0 with `ERROR_NOT_READY`, because 0 is no
menu at all; the bridge binds a `tapped` stamp to the open tick it saw and
refuses a stamp older than that open. Open and cancellation are separate
records, and equal ticks do not order them, so that binding is correlation, not
causal identity: a Hide caused by the tap is inferred, not proven. A gesture
close never asks for `retract`: it retracts, then taps the Windows key and
publishes the close record.

## Known limits and risks

- The left, top and bottom taskbars are verified on this machine, and the right
  side was exercised earlier through the same path (`axis=2`). The dismiss axis
  and the panel band come from the taskbar itself, never from the Start window's
  rect. `SHAppBarMessage(ABM_GETTASKBARPOS)` is the primary source (`uEdge` and
  its docked rectangle, reported unchanged while the bar is hidden); the
  `Shell_TrayWnd`/`Shell_SecondaryTrayWnd` rectangle is the cross-check (the bar
  must be flush to the edge **and** thin across it, so a full-width bottom bar is
  not mistaken for a side one); the work-area inset names the axis only when it
  truly insets, because auto-hide makes it zero by design. An inconclusive
  probe keeps the last axis and band, logs once per undecided episode with what
  each source said (`dismiss axis=... kept, no source named an edge ...`), and
  retries on the next open or broadcast; it never silently means `Left`.
  Measured on this layout:
  `dismiss axis=3 (0=left,1=top,2=right,3=bottom) src=appbar appbar=3
  rc=0,1704,2880,1800 bar=3 rect=0,1798,2880,1894 work=0,0,2880,1800
  mon=0,0,2880,1800`. The band is the bar's strip extended by one bar thickness
  toward the desktop, the axis is part of the layout identity, and both the
  translation and the reveal anchor are axis-aware.
- The reveal and the dismiss share the 20% (10% vertical) travel and the
  0.45 DIP/ms session-average thresholds, but their visual anchors differ (panel
  edge vs. drag start), so the same physical travel reads as a different
  `progress` in each; the decision uses travel, not progress.
- The panel trailing edge is measured from the root's visible child; if the menu
  layout changes (another panel becomes the first visible child) the anchor needs
  revisiting. The XAML coordinate space starts at `ClientToScreen` of the host
  window's client origin, not at the window rect and not at the work-area origin;
  the window rect alone is 95 px of error here.
- Progress is inserted on a 0.5 DIP movement threshold; a finer threshold costs
  extra property writes on the input thread. At 200% scaling 0.5 DIP is exactly
  1 px (1 DIP = 2 px), so the follow can still step by one pixel: the threshold is
  not sub-pixel.
- A new contact is chosen from a report's real contacts by proximity to the last
  raw position; two fingers moving together can confuse which one starts the
  gesture. Once tracked, another slot or another device cannot move or end it,
  and a count-only lift report does not end it either: the 18 ms idle does.
- Composition translation does not move XAML hit-testing: while the menu is
  dragged, the content still receives touches at its original position.
- After a disable/enable without restarting `StartMenuExperienceHost.exe`, the
  reused XAML root means no new `PrepareRootElement` and no renderer: a new
  contact is tracked but cannot drive the panel until the host restarts. The
  restart in "Build and install" is the workaround, not just advice.
- A gesture close is a real Windows tap at the end of its retract: the shell's key
  handler performs the hide and updates its Start toggle. A Win press during the
  retract closes the menu early — the plan is current, so the bridge gives that
  Hide the keyboard lead — and the Start side drops the armed tap when the
  request arrives. A press just after that hide lands in the shell's own settle
  and is ignored; the toggle is not advanced, and the next press opens the menu.
  A press 50 ms after the menu reports closed opens it and animates the panel in.
- The bridge holds every `CStartExperienceManager::Hide` that is not a gesture
  close for the settle the Start mod answered with, plus 30 ms, and that wait
  runs on explorer's UI thread: a click-away close slides out too, and the
  taskbar is blocked for that time (409 ms for the 900 DIP panel, 560 ms plus up
  to 100 ms of answer wait when the answer never arrives). The `reason` argument
  is logged (`hide delayed reason=1 retract=520 lead=550` on the left taskbar of
  this machine, whose 1440 DIP travel hits the 520 ms clamp); the Windows-key
  close reported 1 on this build. The gesture path does not wait.
- Two limits here are code-path arguments, not measurements. With the CLI as the
  lever, the bridge's drain/residual path cannot be reached: CLI-plus-engine
  latency exceeds the 660 ms maximum hold of the shipped pair (100 ms answer wait
  plus the 560 ms fallback; the shipped settle is clamped to 520 ms), while a
  tap-to-cloaked run measured 574-592 ms. The two handles one pinned image keeps
  (the gesture mapping and its event) are below explorer's handle-count noise
  (±60), and a version change leaves the older image's pair too. Both are reasoned
  from the code paths above, not observed as failures.
- The close record is an 800 ms window. A gesture close publishes the plan at
  its release and the `tapped` record immediately before the tap; a dropped close
  publishes the cancelled kind at once, and the tap's deadline republication is
  only the fallback. A Windows-key close that lands after the `tapped` record is
  a second Hide under the same stamp and takes the keyboard lead; one that lands
  while only the plan is current is a keyboard close too. Only `tapped` passes a
  Hide without a wait.
- A gesture close that the user cancels by pulling the menu open again drops its
  armed tap before the shell is touched, so no `Hide` runs and the toggle is
  never advanced. The panel is already retracted and is driven purely by the
  follow from then on: it reappears at the mapped position under the finger
  (0.269 at the screen edge on this layout) instead of already being open.
- The root hook is gated, not opportunistic: the file version of `StartUI_.dll`
  must be exactly `10.0.22621.3733` **and** the target prologue plus its `E8`
  call target must match. Anything else logs one line
  (`startui root hook skipped build=... expected=10.0.22621.3733`, or
  `... prologue mismatch`) and installs no hook; without the root there is no
  renderer, so no gesture starts. Before either probe reads, it proves the
  inspected range lies in an executable section of the loaded image (DOS and NT
  headers, `SizeOfImage`, the section table); a range that does not logs
  `startui root hook skipped (target not readable)` or
  `startui anim hook skipped (target not readable)`, which is distinct from a
  build or signature mismatch. A different build needs a new RVA and new
  reference bytes.
- The digitizer mapping assumes the touch panel covers the primary monitor.
