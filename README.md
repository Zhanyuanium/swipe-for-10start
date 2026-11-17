# swipe-for-10start

Windhawk mods for the ExplorerPatcher Windows 10 Start menu on Windows 11.

## What this is

ExplorerPatcher replaces Windows 11's Start UI with the legacy `StartUI_.dll`,
which has no consumer for the shell's touch gesture. This repository is the
investigation of where that gesture data actually lives, plus a working
touch/pen swipe-to-dismiss that makes the menu follow the finger.

Verified on: Windows 11 build 26300.9974, ExplorerPatcher 26100.9711
(`Start_ShowClassicMode=1`), Windhawk 2.0 alpha, taskbar on the left,
2880x1800 physical at 200% scaling (1440x900 DIP), ELAN2513 touchscreen.

## Status

| Feature | State |
|---|---|
| Find a per-frame finger source inside `StartMenuExperienceHost.exe` | done, raw HID |
| Menu follows the finger while a contact is down | working |
| Swipe dismiss with commit/cancel thresholds | working, left taskbar |
| Dismiss animation (slide out, spring back) | working |
| Pen | registered, not yet exercised by hand |
| Taskbar on top / right / bottom | axis logic present, only left verified |
| Mouse drag to dismiss | deliberately not enabled |
| Reveal (edge swipe to open) following the finger | working, anchored to the panel edge |
| Windows-key close | animated slide-out, 350 ms hide lead from the explorer-side bridge |
| Gesture close | retract, then one Windows tap at its end; the bridge skips its keyboard lead for the stamped Hide |
| Reopen during a close | a new edge pull drops the armed tap, so the shell never sees that close |

## How the working path works

`mods/start-menu-legacy-swipe.wh.cpp` runs inside `StartMenuExperienceHost.exe`:

1. A worker thread registers Raw Input for the digitizer
   (`usUsagePage 0x0D / usUsage 0x04` for touch, `0x0D / 0x02` for pen,
   `RIDEV_INPUTSINK`) on a message-only window.
2. Each report is decoded with `GetRawInputDeviceInfoW(RIDI_PREPARSEDDATA)` +
   `HidP_GetUsageValue`: contact collections `0x0D/0x22` (Finger) or `0x0D/0x05`
   carry `0x01/0x30` (X) and `0x01/0x31` (Y); the top-level `0x0D/0x54` is the
   Contact Count. Raw values are mapped to pixels through the value caps' logical
   range and then to DIP through the window DPI.
3. The contact nearest the previous raw position is tracked, so palm or ghost
   contacts do not cancel the gesture.
4. A state machine on that thread drives a `Progress` scalar in a
   `CompositionPropertySet`; an expression translates the root visual by
   `(1 - Progress) * Direction * Distance`.
5. On release it commits when the release position passed 20% of the distance or
   the release speed exceeded 0.45 DIP/ms; a commit animates `Progress` to 0 and
   arms a `VK_LWIN` tap (down 60 ms, up) for the end of that retract. That tap is
   the shell's own Start toggle: its key handler performs
   `CStartExperienceManager::Hide`, so the next Windows press opens the menu and
   no re-arm tap exists. Every close — dismiss or cancelled reveal — runs the
   same planner:

   ```
   flick    = max(0, velocity - 0.45 DIP/ms)
   duration = 260 ms / (1 + 0.35 * flick), never below 45% (117 ms)
   ```

   The duration comes from the flick; the curve is built per close so the panel
   leaves at the speed the finger had. Its initial slope is
   `1.75 x finger speed / average speed`, expressed as
   `s = clamp(gain * velocity / avg, 0, 3)` with `y1 = clamp(s * 0.35, 0.02, 1)`
   on `cubic-bezier(0.35, y1, 0.55, 1)`. A release below the commit threshold is
   not a flick, so it keeps the plain 260 ms; only a real flick shortens the
   slide. At the release the mod writes `gesture=0` to `close-mode.txt`, so the
   bridge knows the Hide its tap causes needs no retract lead.

   The gain is a mod setting: **Close animation start speed (% of finger
   speed)** (`RetractStartSpeedPercent`, default 175, clamped 0-300). 100 leaves
   at exactly the finger's speed, 0 gives the old flat-start curve.

`Progress` is written whenever the finger moves 0.5 DIP, so the drag is
continuous rather than stepping on a progress grid.

The keyboard path uses the same machinery: a closed→open transition that no
gesture owns sets `Progress = 0` and animates it to 1 over 260 ms, so a Win-key
open slides in the way a gesture open does. A Win-key close is animated too:
`twinui.pcshell.dll` `CStartExperienceManager::Hide` is where the shell takes
the menu away, so the explorer-side bridge delays that call by 350 ms and writes
`retract=1` to the mod's debug file first; the mod retracts `Progress` with the
shared 260 ms curve while the shell waits. Measured with the menu open (one
run, frames at ~123 ms and ~189 ms after the key-down): the panel's right edge
moved 0 → 119 → 344 DIP left and was gone by ~260 ms, instead of vanishing on
the first frame.

A gesture close needs no retract command. Its retract starts at the release and
its tap comes at the end of it; the bridge sees the fresh `close-mode.txt` stamp
and calls the shell's `Hide` at once, because the panel is already off screen.
The tap is what updates the Start toggle, and it is the only tap: there is no
programmatic `Hide` and no 120 ms re-arm tap, so a press cannot collide with
one.

A new edge pull during the retract cancels the close on the Start side: the
armed tap is dropped before it is sent (the stamp becomes `cancelled`), no
`Hide` runs, the shell's toggle is never touched, and the pull's own follow keeps
driving the panel. A Win press during the retract is a keyboard close: the fresh
stamp makes the bridge run that `Hide` at once, and the armed tap is skipped
because the window is already closed. The earlier design returned `S_OK` from
the bridge's `Hide` without calling it; measured, one such abort left the
Windows key and the taskbar Start button dead until
`StartMenuExperienceHost.exe` restarted, so every path calls the original.

Release detection: the digitizer keeps reporting while the finger rests
(measured 255 reports in a 1.5 s hold), so an 18 ms window without a report is a
reliable lift. Both halves of the timing matter on this build, and each was
measured separately:

- the idle check needs a **`QueryPerformanceCounter`** silence: `GetTickCount64`
  only advances every 15.6 ms, so an 18 ms window read as 31 ms;
- the idle tick needs a **`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` waitable
  timer**: `WM_TIMER` stayed on the 15.6 ms quantum even while the process held
  a 1 ms `timeBeginPeriod(1)` request. The request is still made, paired through
  `timeEndPeriod`, for the time a contact is down.

Measured release latency (`relms` on the release line): 31 ms before, 19-23 ms
after.

## Opening follows the finger too

The left-edge swipe is Windows' own gesture: `twinui.pcshell.dll`
`TouchEdgeGestureProcessor` calls `CStartExperienceManager::Show(..., 16)` and
`StartUI_.dll` starts its own reveal animation. ExplorerPatcher is not involved.

The mod cannot suppress that animation: hooking
`StartUI::SplitViewFrame::OnGlobalAnimationRequested` and skipping the `type=0`
request also skips presenting the panel, which leaves a window that DWM reports
as shown but that draws nothing. That hook exists in the mod as an experiment
(`anim=0` in the debug file) and is off by default.

Instead the mod takes over after the shell opens the window:

- A touch that starts within 12 DIP of the taskbar's screen edge while the menu
  is closed becomes an edge candidate. Raw Input sees it from the first report
  (`edge x=0 ...`), before the shell opens anything.
- Once the menu is open and the contact is still down, every report drives
  `Progress` from the **absolute** finger position and the panel's trailing edge:

  ```
  Progress = 1 - (finger - panelTrail) / (direction * distance)
  ```

  The trailing edge is measured on the UI thread from the root's visible child
  (`StartSizingFrame`; ~1053 DIP on a left taskbar, ~200 DIP from the bottom) and
  published to the input thread. The first measurement lands while the shell is
  still animating the panel in, so the value is re-measured 400 ms after the
  menu settles and rejected if it falls outside the work area. With that anchor
  the panel edge equals the finger: measured within 0.5 DIP residual at 150, 450
  and 800 DIP on the left taskbar, and within 5 DIP of the finger on the bottom.
- Release is decided symmetrically with the dismiss gesture: `travel` is how far
  the finger moved away from the edge since the touch began (the release value,
  not the peak), and the menu opens fully when `travel > 20%` of the distance or
  the release speed exceeds 0.45 DIP/ms. Otherwise the mod retracts the panel and
  taps the Windows key, so a short slow pull, or pulling out and pushing back,
  does not open the menu; the bridge holds the hide for the retract.

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

- **`mods/start-menu-legacy-swipe.wh.cpp`** (`StartMenuExperienceHost.exe`) — the
  shipping mod described above.
- **`mods/msgprobe.wh.cpp`** — diagnostic: logs `WM_POINTER*` and mouse messages
  pulled from the message queue. Proved the queue dead end. Not needed at runtime.
- **`mods/inputprobe.wh.cpp`** — diagnostic: raw HID decoding without the gesture
  logic. **Do not enable it together with the shipping mod**: Raw Input allows one
  sink per usage per process, and inputprobe takes the stream for itself.
- **`mods/start-menu-touch-gesture-bridge.wh.cpp`** (`explorer.exe`) — hooks
  `CStartExperienceManager::Hide`. A keyboard close gets the 350 ms retract lead
  after `retract=1`; a gesture close is tapped only after its retract, and its
  `gesture=0` stamp makes the Hide run at once (`hide passed (gesture)`). A
  dropped gesture close stamps `cancelled`, which takes the keyboard path.
  Publishes the taskbar-edge gesture handle/state from `twinui.pcshell.dll` to
  shared memory, which the shipping mod does not read. **Enable it together with
  the Start mod** for the slide-out; without it the menu still closes (the tap is
  the close), but instantly.
- **`mods/start-menu-touch-gesture-probe.wh.cpp`** — Phase 0 diagnostic probe.
- **`mods/start_gesture_shared.h`** — shared-memory contract for the bridge.

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
- `tools/inject_swipe.ps1` — inject a touch swipe (virtual digitizer).
- `tools/touch_hold.ps1` — drag to a point, hold, capture, then release.
- `tools/follow_hold.ps1` — same, but keeps sending updates while the screenshot
  is taken: the virtual digitizer stops reporting during a plain hold, unlike the
  real panel, so this is the only way to photograph a mid-gesture state.
- `tools/inject_swipe_holds.ps1` — swipe with pauses at waypoints.
- `tools/measure_swipe.ps1`, `tools/read_shared_gesture.ps1` — earlier
  investigation helpers for the edge-gesture bridge.

The mod has a file-driven debug channel for the renderer:
write `p=0.5`, `t=x,y,z` or `e=x,y` to
`C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\local@start-menu-legacy-swipe\debug.txt`
to drive `Progress`, `UIElement.Translation` or the visual `Offset` directly.
`retract` plays the committed-dismiss retract from the current `Progress`; the
explorer-side bridge writes it 350 ms before it lets `CStartExperienceManager::Hide`
run, which is what animates a Windows-key close. It applies only to a file
written in the last second, so the leftover command is not replayed by the next
Start process. A gesture close never asks for `retract`: it retracts, then taps
the Windows key and stamps `gesture=0` in `close-mode.txt`.

## Known limits and risks

- The left and bottom taskbars are verified. `UpdateDismissAxis` maps the taskbar
  edge to a dismiss direction and both the translation and the reveal anchor are
  axis-aware, but the other three sides are untested.
- The reveal and the dismiss share the 20% / 0.45 DIP/ms thresholds, but their
  visual anchors differ (panel edge vs. drag start), so the same physical travel
  reads as a different `progress` in each; the decision uses travel, not progress.
- The panel trailing edge is measured from the root's visible child; if the menu
  layout changes (another panel becomes the first visible child) the anchor needs
  revisiting. The XAML island is inset to the monitor work area, not to the
  window rect, which is 95 px of error if the window rect is used instead.
- Progress is inserted on a 0.5 DIP movement threshold; a finer threshold costs
  extra property writes on the input thread, but the current one is already
  sub-pixel at 200% scaling.
- The contact collection is chosen by proximity to the previous raw position; two
  fingers moving together can confuse which one is tracked.
- Composition translation does not move XAML hit-testing: while the menu is
  dragged, the content still receives touches at its original position.
- A gesture close is a real Windows tap at the end of its retract: the shell's key
  handler performs the hide and updates its Start toggle. A Win press during the
  retract closes the menu early (the fresh stamp makes the bridge run the Hide at
  once) and the armed tap is skipped. A press just after that hide lands in the
  shell's own settle and is ignored; the toggle is not advanced, and the next
  press opens the menu. A press 50 ms after the menu reports closed opens it and
  animates the panel in.
- The bridge delays every `CStartExperienceManager::Hide` that is not a gesture
  close by 350 ms, and that wait runs on explorer's UI thread: a click-away close
  slides out too, and the taskbar is blocked for those 350 ms. The `reason`
  argument is logged (`hide delayed reason=...`); the Windows-key close reported
  1 on this build. The gesture path does not wait.
- `close-mode.txt` is an 800 ms window. A gesture close stamps `gesture=0` at its
  release; a dropped close stamps `cancelled`, so a keyboard close inside the
  window still takes the 350 ms lead. A Windows-key close that lands right after
  a gesture close but before its stamp is replaced is read as a gesture close and
  hides at once.
- A gesture close that the user cancels by pulling the menu open again drops its
  armed tap before the shell is touched, so no `Hide` runs and the toggle is
  never advanced. The panel is already retracted and is driven purely by the
  follow from then on: it reappears at the mapped position under the finger
  (0.269 at the screen edge on this layout) instead of already being open.
- `PrepareRootElement` is hooked through a hardcoded RVA for `StartUI_.dll`
  22621.3733; a different build needs a new RVA or a pattern scan.
- The digitizer mapping assumes the touch panel covers the primary monitor.
