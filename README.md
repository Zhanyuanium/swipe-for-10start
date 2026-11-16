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
| Reveal (edge swipe to open) following the finger | not achievable, see below |

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
   the release speed exceeded 0.45 DIP/ms; a commit animates `Progress` to 0 over
   150 ms and then taps `VK_LWIN`, which is the only dismissal that works when
   the menu was opened without keyboard focus. Otherwise `Progress` animates back
   to 1.

Release detection: the digitizer keeps reporting while the finger rests
(measured 255 reports in a 1.5 s hold), so a 35 ms window without a report is a
reliable lift. Observed release latency is ~47 ms.

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
- **`mods/start-menu-touch-gesture-bridge.wh.cpp`** (`explorer.exe`) — publishes
  the taskbar-edge gesture handle/state from `twinui.pcshell.dll` to shared
  memory. Not used by the shipping mod; kept for future work.
- **`mods/start-menu-touch-gesture-probe.wh.cpp`** — Phase 0 diagnostic probe.
- **`mods/start_gesture_shared.h`** — shared-memory contract for the bridge.

## Build and install

```powershell
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod install --file .\mods\start-menu-legacy-swipe.wh.cpp
```

The CLI needs an elevated shell. Windhawk reloads the mod in place, but the
`StartUI::App::PrepareRootElement` hook only fires when a new Start root is
created, so restart `StartMenuExperienceHost.exe` (or log off and on) before
testing; the menu then attaches its renderer on the next open.

## Test tools

- `tools/screenshot.ps1` — full-screen PNG in physical pixels. The process is
  switched to per-monitor-v2 DPI awareness first, otherwise a 200% display only
  captures the top-left quarter.
- `tools/start_menu.ps1` — reports or forces the menu's open state. Uses
  `DWMWA_CLOAKED`; `IsWindowVisible` stays TRUE while the menu is hidden.
- `tools/inject_swipe.ps1` — inject a touch swipe (virtual digitizer).
- `tools/touch_hold.ps1` — drag to a point, hold, capture, then release.
- `tools/inject_swipe_holds.ps1` — swipe with pauses at waypoints.
- `tools/measure_swipe.ps1`, `tools/read_shared_gesture.ps1` — earlier
  investigation helpers for the edge-gesture bridge.

The mod has a file-driven debug channel for the renderer:
write `p=0.5`, `t=x,y,z` or `e=x,y` to
`C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\local@start-menu-legacy-swipe\debug.txt`
to drive `Progress`, `UIElement.Translation` or the visual `Offset` directly.

## Known limits and risks

- Only the left-taskbar case is verified. `UpdateDismissAxis` maps the taskbar
  edge to a dismiss direction and the translation is axis-aware, but the other
  three sides are untested.
- The contact collection is chosen by proximity to the previous raw position; two
  fingers moving together can confuse which one is tracked.
- Composition translation does not move XAML hit-testing: while the menu is
  dragged, the content still receives touches at its original position.
- A commit taps `VK_LWIN`. If the shell is mid-transition that tap can bounce and
  reopen the menu.
- `PrepareRootElement` is hooked through a hardcoded RVA for `StartUI_.dll`
  22621.3733; a different build needs a new RVA or a pattern scan.
- The digitizer mapping assumes the touch panel covers the primary monitor.
