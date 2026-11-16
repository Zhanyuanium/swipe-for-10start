# swipe-for-10start

Windhawk mods for the ExplorerPatcher Windows 10 Start menu on Windows 11.

## What this is

Windows 11's modern Start follows your finger during a taskbar edge swipe because
explorer hands the live gesture (`GestureAnimationData`) to a Start UI that knows
how to consume it. ExplorerPatcher replaces that UI with the legacy
`StartUI_.dll`, which has no such consumer.

This repository documents that investigation and ships the one part that turned
out to be achievable: **touch/pen swipe-down to dismiss**, with the menu
following the finger.

## Status

| Feature | State |
|---|---|
| Discover where the edge gesture lives in explorer | done |
| Per-frame reveal following | **not achievable** (see findings below) |
| Touch/pen swipe-down dismiss with follow | implemented, needs real-finger check |
| Reveal animation | left to ExplorerPatcher's own flyout animation |
| Four edges / DPI polish | not done |
| EP PR: opt out of the classic flyout animation | not started |

Environment used: Windows 11 build 26300, ExplorerPatcher 26100.9711
(`Start_ShowClassicMode=1`), Windhawk 2.0 alpha, taskbar on the left,
2880x1800 physical / 1440x900 DIP.

## Mods

- **`mods/start-menu-legacy-swipe.wh.cpp`** (`StartMenuExperienceHost.exe`)
  The shipping mod. Hooks `StartUI::App::PrepareRootElement`, attaches pointer
  handlers to the legacy Start root, and translates it downward while a touch or
  pen drags inside the menu. On release it either dismisses (25% of the height or
  0.5 DIP/ms) or springs back.
- **`mods/start-menu-touch-gesture-bridge.wh.cpp`** (`explorer.exe`)
  Publishes the taskbar-edge gesture handle/state from `twinui.pcshell.dll` to
  shared memory. Not required by the shipping mod; kept for future work.
- **`mods/start-menu-touch-gesture-probe.wh.cpp`**
  Diagnostic probe (log only) used during the investigation.
- **`mods/start_gesture_shared.h`**
  Shared-memory contract between the bridge and its consumer.

## Why the reveal cannot follow the finger

All of the following were measured on the builds above, not assumed:

- `InputGestureOperation::Position` (vtable slot 11) returns the invocation point
  for the whole gesture and only jumps to the real end position in
  `InputGestureOperationEnded`. Polling it at 8 ms gives a constant.
- `TouchEdgeGestureProcessor::ReportViewDragGesture` never fires for an edge
  swipe; it is driven by the view reporting, which the legacy UI never does.
- twinui contains no DirectManipulation (`ContentUpdated`,
  `ViewportStatusChanged`) to hook.
- The legacy Start root receives no touch stream during the edge swipe.
- The UDK path used by the modern Start
  (`InputAnimationTarget::GetForVisual` +
  `ShellViewGestureAnimationPropertySet::GetForInputAnimationTarget`) creates
  successfully in our process, but only `AnimationProps.Translation` resolves on
  the result, and it is constant zero. `Width`, `Height` and
  `VisibilityProgress` fail with `0x80070057`.
- Explorer's `ApproveProcessesForAnimation` (real ABI:
  `(self, uint32_t count, const uint32_t* values)`) does succeed for the Start
  process, but approval alone does not unlock those properties. The consumer is
  missing an in-process `InputGestureOperation`, which only exists for a
  registered shell-view (Bamo) client.

Reaching per-frame following therefore requires a full `ShellViewCoordinator`
client inside `StartMenuExperienceHost`, which the legacy UI does not have and
which would compete with ExplorerPatcher's own registration.

## Build and install

The Windhawk CLI needs an elevated shell:

```powershell
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod install --file .\mods\start-menu-legacy-swipe.wh.cpp
```

Restart `StartMenuExperienceHost.exe` after installing. The mods use hardcoded
RVAs for `StartUI_.dll` 22621.3733 and `twinui.pcshell.dll` 26100.9949; the
explorer bridge refuses to install hooks when the module build does not match.

## Test tools

- `tools/inject_swipe.ps1` — inject a touch swipe.
- `tools/inject_swipe_holds.ps1` — swipe with pauses at waypoints.
- `tools/measure_swipe.ps1` — sample the Start window rect during a swipe.
- `tools/read_shared_gesture.ps1` — sample the bridge shared memory.

Note: injected touch is reported to XAML as `PointerDeviceType.Mouse`, which the
dismiss mod deliberately ignores (mouse drags must not dismiss). The dismiss
path therefore has to be verified with a real finger or pen.

## Known risks

- The dismiss relies on `PointerDeviceType` being `Touch`/`Pen`; a precision
  touchpad reports `Mouse` and will not dismiss.
- Closing the menu posts `VK_ESCAPE` to the Start CoreWindow after the settle
  animation; if that path fails the menu stays translated.
- Composition translation does not move XAML hit-testing, so a half-dragged menu
  is only resolved once the release decision is made.
- RVAs are build-specific.
