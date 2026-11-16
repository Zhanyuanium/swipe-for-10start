// ==WindhawkMod==
// @id              start-menu-legacy-swipe
// @name            Legacy Start Edge Swipe
// @description     Touch/pen edge swipe to open and to dismiss the legacy Start menu
// @version         0.6.4
// @author          doubao
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32 -lhid -Wall -Wextra -I"C:\Users\doubao\source\repos\swipe-for-10start\mods"
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Legacy Start edge swipe (reveal follow and dismiss)

StartMenuExperienceHost-side companion for ExplorerPatcher's legacy Windows 10
Start menu. A one-finger swipe from the taskbar edge opens the menu and makes it
follow the finger; a swipe toward the same edge slides the menu out and
dismisses it. The input layer reads raw HID touch and pen reports; it does not
use XAML pointers, DirectManipulation or the pointer message queue.

What it does:

- Hooks `StartUI::App::PrepareRootElement` (`StartUI_.dll`, RVA `0xAD730`),
  captures the returned root after it loads and enables Composition
  translation.
- Hooks `StartUI::SplitViewFrame::OnGlobalAnimationRequested` (`StartUI_.dll`,
  RVA `0x4164C0`) and can drop the request whose `AnimationType` is 0 when the
  debug line `anim=0` sets the switch; the switch starts at `anim=1`, the
  original behaviour. The close request (`AnimationType` 1) always reaches the
  original unchanged. The animation type is read from the event arguments with
  the same three int getters the original calls, not from a call argument.
- At every setup it gets the Start CoreWindow HWND via `ICoreWindowInterop`,
  compares `GetWindowRect` with the monitor work area and picks the dismiss
  direction: left if the window is anchored to the work-area left, then top,
  then right, then bottom. It logs `dismiss axis=...` once and publishes the
  axis, the window width/height and the window bounds in DIP for the input
  thread.
- Installs a `CompositionPropertySet` with `Progress` (animatable),
  `Distance` (static window width or height), `DirX` and `DirY`, plus the
  expression
  `Vector3((1 - state.Progress) * state.DirX * state.Distance, (1 - state.Progress) * state.DirY * state.Distance, 0)`.
- `Wh_ModAfterInit` starts one input thread. The thread owns a message-only
  window and registers `RAWINPUTDEVICE{0x0D, 0x04}` (touch) and
  `{0x0D, 0x02}` (pen) with `RIDEV_INPUTSINK`, so it still receives reports
  while the Start menu is in front.
- Each device is decoded from its `RIDI_PREPARSEDDATA` blob, never by opening
  the device. Contact collections are the `0x0D/0x05` and `0x0D/0x22` link
  nodes. `contacts` is the top-level Contact Count (`0x0D/0x54`); X and Y come
  from the first contact collection with a non-zero position.
- Raw X/Y is mapped from the HID logical range to physical screen pixels with
  `GetSystemMetrics`, then to DIP with `GetDpiForWindow(...) / 96`.
- The gesture state machine runs on the input thread. It begins at one
  contact inside the Start window (4 DIP margin) while the CoreWindow is not
  DWM-cloaked. While the finger moves it inserts
  `Progress = 1 - delta / Distance`, and only when the change moves the panel
  by at least 0.5 DIP (`fabs(dprogress) * Distance >= 0.5`). The filter is a
  screen distance, not a progress fraction: a fixed 0.004 was 5.8 DIP here,
  which is 11.5 physical px at 200% and read as a staircase.
- The Start CoreWindow HWND outlives the menu and `IsWindowVisible` stays TRUE
  while it is closed, so `DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED)` decides
  whether the menu is open: 0 while shown, 2 while closed. One query runs per
  down, after the settling check.
- A gesture whose perpendicular travel exceeds 120 DIP, is more than 1.5x the
  axis travel and keeps the axis travel below 60 DIP is a scroll, not a
  dismiss: it restores `Progress = 1` without animation.
- Release commits when the axis travel exceeds 20% of `Distance` or the average
  velocity exceeds 0.45 DIP/ms. A commit animates `Progress` to 0 over 220 ms
  on an ease-out curve (`cubic-bezier(0.2, 0.85, 0.3, 1)`) and then taps the
  Windows key; a cancel animates back to 1 over 120 ms on the same curve.
  Both animations place the easing on the keyframe at t=1, and both insert the
  target as the base value before `StartAnimation`.
- A menu opened without keyboard focus keeps the foreground on `SearchHost`, so
  the legacy menu ignores an injected `VK_ESCAPE`. A lone `VK_LWIN` tap (down,
  60 ms, up) closes it, because the shell acts on the key itself.
- A down that arrives while the previous commit is still settling is ignored.
  The menu reuses one XAML root, so a commit sets a flag that makes the next
  down stop the animation and restore `Progress = 1` before the drag starts.
- Two or more contacts cancel the gesture and are ignored until the panel is
  released. A lift report is not required: a session with no sample for 120 ms
  ends on its own.
- Reveal follow: a contact that starts within 12 DIP of the taskbar edge while
  the menu is closed is a candidate. EP opens the menu by itself once the
  contact passes its own edge threshold; the first report that sees the menu
  open takes `Progress` over. The follow works in absolute screen DIP against
  the panel edge that faces the taskbar (`panelTrail`), which the UI thread maps
  from the first visible child of the root (`StartSizingFrame`) and translates
  by the work area origin: `Progress = 1 - (finger - panelTrail) / (dir *
  Distance)`. The panel edge therefore meets the finger at every sample, and the
  panel is exactly off screen at the screen edge (Progress 0.269 for a 1052 DIP
  trail on a 1440 DIP root). That mapping drives the visual only. The release
  decision mirrors the dismiss path: it judges the travel away from the edge at
  release, `(finger - edgeStart) * openingSign` clamped at 0, where
  `openingSign` is +1 for a left or top taskbar and -1 for right or bottom. A
  release past 20% of `Distance`, or one whose average velocity
  `travel / max(elapsed, 1)` exceeds 0.45 DIP/ms, animates `Progress` to 1 over
  220 ms and keeps the menu. Anything else is a cancel: `Progress` animates to 0
  over 140 ms on an ease-in-out curve (`cubic-bezier(0.42, 0, 0.25, 1)`), so the
  panel leaves the finger gently and settles back out of the screen edge, and
  the `VK_LWIN` tap that closes the menu follows the animation 160 ms after the
  release on the input timer. `Progress` stays 0; a later open restores it,
  unless an edge contact is already driving that open. Pulling out and pushing
  back therefore cancels, as it does inside the open menu. The line
  `open follow travel=... max=... progress=... vel=... commit=... panel=... n=...`
  is written once per reveal; the cancel adds `reveal cancel win sent` when the
  deferred tap runs, or `reveal cancel tap dropped` when a new pull takes the
  panel over before it does.
- Every close the mod starts suppresses the reopen restore for 600 ms: the
  dismiss tap, and the reveal cancel from the moment its retract starts. The
  reused root reports itself open until the shell has processed the tap, and
  restoring `Progress = 1` in that gap flashed the fully open panel while the
  close was in flight. For the same 600 ms a contact on the edge counts as a new
  reveal attempt even while the window still reports open: otherwise the down
  was read as a drag of the retracting panel, which stopped the retract and
  restored `Progress = 1` on the first sample.
- The candidate path polls the cloaked attribute at most once per 20 ms; the
  cached answer is dropped on every lift, so each contact starts from a fresh
  query.

What it does not do:

- It does not use the explorer-side bridge or shared memory.
- It does not attach XAML pointer handlers and has no DirectManipulation
  hook.
- It does not remove the shell's open storyboard. Measured with `anim=0`:
  dropping the open request leaves the Start window uncloaked with no pane
  drawn, because the same request is the step that presents the content. The
  switch is therefore off and the open still slides from the shell.

Debug channel (kept in the build):

- `debug.txt` in the mod storage directory
  (`C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\local@start-menu-legacy-swipe\`)
  is polled every 50 ms on the raw input thread and re-read only when its write
  time or size changes. One command per line:
  - `p=0.5` inserts `Progress`, clamped to 0..1.
  - `t=-300,0,0` sets `UIElement.Translation` on the UI thread.
  - `e=-300,0` sets `Properties["Offset"]` and `Visual.Offset` on the visual.
  - `anim=0` suppresses the shell's open storyboard; `anim=1` restores it. The
    default is `anim=1`, because suppression leaves the pane undrawn.
  Empty lines and unknown lines are ignored. The channel compares the rendering
  mechanisms without a gesture; it does nothing before the first
  `attached root=...` line.

Diagnostics:

- `mod init`, `hid init`, `hid reg usage=... ok=... err=...`,
  `hid down x=... y=... axis=... dist=...`,
  `hid release delta=... perp=... vel=... commit=... n=...`,
  `hid dismiss win sent`,
  `reveal cancel win sent`, `reveal cancel tap dropped`,
  `open follow travel=... max=... progress=... vel=... commit=... panel=... n=...`,
  `reveal panelTrail=...` (or `reveal panelTrail=... (fallback)`),
  `hid decodefail dev=... err=...`,
  `attached root=...`, `dismiss axis=...`, `debug p=...`, `debug t=...`,
  `debug e=...`, `debug anim=...`, `startui anim hooked`,
  `startui anim hook skipped` and the first four
  `startui anim type=... edge=... trans=... suppress=...` calls go to
  `renderer.log`.
- Only begin and release are logged, never individual samples. The `edge` line
  stays in the code behind `g_debugEdge`, which is false; it measured the raw
  edge contact and EP's own open threshold.

Known risks:

- Raw input has one sink per usage per process. Another mod that registers
  `0x0D/0x04` (the `inputprobe` diagnostic does) redirects or removes the
  stream; `inputprobe` must stay disabled.
- The digitizer is assumed to cover the primary screen and match the window
  DPI. A panel on another display, a rotated panel or a non-default logical
  range decodes to the wrong DIP point.
- The legacy menu also receives the same touch through its normal XAML input,
  so a drag can scroll or click a tile while the menu slides.
- The reveal takes `Progress` over on the first report that sees the menu open,
  so the menu drops to the computed progress at that instant (0.269 at the
  screen edge on this layout) and then follows. The mapping needs the panel
  trail: it is the first visible child of the root with a width. If that child
  is missing or unlaid when `Setup` and `SizeChanged` run, the trail falls back
  to the root extent and the follow lags the finger again (log
  `reveal panelTrail=... (fallback)`).
- The trail assumes the XAML island sits at the work area origin. Measured on
  this build: the CoreWindow is full screen and `GetWindowRect` returns
  `0,0,2880,1800`, yet the panel's border lands 48 DIP right of the mapped
  transform because the island is inset by the taskbar. A shell that lays the
  island out at the screen origin would read 48 DIP high on a left taskbar.
- `panelTrail` is republished on every root `SizeChanged`. A panel resize during
  a follow moves the mapping, so the panel edge jumps to the new trail under the
  same finger.
- `Distance` is sampled at takeover. A layout change during the follow keeps
  the old extent until the contact lifts.
- Composition calls and the settle batch run from the input thread. The batch
  completion arrives on the compositor's dispatcher; if it never arrives,
  `Progress` stays at 0 and every later down is ignored until that batch
  completes.
- A committed dismiss taps the Windows key globally, so it lands on whichever
  window has the foreground; a lone Windows tap opens Start if the menu is
  already gone.
- The suppression window is time based, not tied to the shell's own signal. If a
  reopen without an edge contact, such as the Windows key, lands inside the
  600 ms after a close, that open is not restored; `Progress` stays 0 and the
  panel is invisible until the next open.
- The deferred cancel tap waits on `WM_TIMER`, which is low priority: a report
  flood on the same thread delays the tap by that much. The panel stays
  retracted until the tap runs.
- `DwmGetWindowAttribute` is resolved at init from `dwmapi.dll`. If the export
  is missing, no gesture starts.
- The `PrepareRootElement` and `OnGlobalAnimationRequested` RVAs are specific to
  `StartUI_.dll` 10.0.22621.3733. The animation hook checks its target prologue
  and the three argument getters before it installs; a different build logs
  `startui anim hook skipped` and keeps the original animation.
*/
// ==/WindhawkModReadme==

/*
 * [INPUT]: depends on StartUI::App::PrepareRootElement and
 *   StartUI::SplitViewFrame::OnGlobalAnimationRequested plus its three argument
 *   getters in StartUI_.dll; on RegisterRawInputDevices, GetRawInputData,
 *   GetRawInputDeviceInfoW,
 *   GetDpiForWindow, GetSystemMetrics, GetWindowRect, MonitorFromWindow,
 *   GetMonitorInfoW and keybd_event from
 *   user32; on DwmGetWindowAttribute, resolved at init from dwmapi.dll; on
 *   HidP_GetCaps, HidP_GetLinkCollectionNodes, HidP_GetValueCaps and
 *   HidP_GetUsageValue from hid.dll; on the CompositionPropertySet, the
 *   expression and the two cubic-bezier easing functions installed in Setup; on
 *   VisualTreeHelper and TransformToVisual
 *   from Windows.UI.Xaml.Media to map the panel trail; on `debug.txt` in the
 *   mod storage directory, read by the debug channel.
 * [OUTPUT]: one message-only window and one worker thread per process; the
 *   Composition `Progress` scalar; the panel trail in absolute screen DIP,
 *   published for the reveal follow; one global Windows-key tap per committed
 *   dismiss or short reveal, the reveal's one deferred to the input timer until
 *   its retract animation has run; a 600 ms window after either tap in which a
 *   closed-to-open report does not restore `Progress` and an edge contact starts
 *   a new reveal rather than a drag of the retracting panel; an optional
 *   suppressed open animation request
 *   (type 0, off by default because dropping it leaves the pane undrawn) while
 *   the close request (type 1) always passes through; four file-driven debug
 *   commands (`p`, `t`, `e`, `anim`) that apply a Progress value, the XAML
 *   translation, the visual offset or the animation mode; no exported symbols.
 * [POS]: input and gesture layer of the legacy Start mod. It replaces the XAML
 *   pointer and DirectManipulation paths with raw HID decoding, owns two
 *   gestures (the dismiss drag inside the open menu and the reveal follow that
 *   takes the menu's own open over from the taskbar edge and pins the panel's
 *   trailing edge to the finger) and carries the switch that can drop the
 *   shell's open storyboard for experiments. The composition helpers below it
 *   are shared with the root hook above it.
 * [PROTOCOL]: Update this header on change, then check AGENTS.md
 */

#include <windhawk_utils.h>
#include <windows.h>
#undef GetCurrentTime  // MinGW winbase.h macro vs. Timeline.GetCurrentTime

#include <hidusage.h>
#include <hidpi.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace {

namespace wuc = winrt::Windows::UI::Composition;
namespace wucore = winrt::Windows::UI::Core;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;

using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;

void FileLog(const wchar_t* format, ...) {
    static SRWLOCK lock = SRWLOCK_INIT;

    WCHAR storage[MAX_PATH] = {};
    if (!Wh_GetModStoragePath(storage, ARRAYSIZE(storage))) {
        return;
    }

    WCHAR path[MAX_PATH] = {};
    swprintf_s(path, L"%s\\renderer.log", storage);

    WCHAR line[1024] = {};
    va_list args;
    va_start(args, format);
    vswprintf_s(line, ARRAYSIZE(line), format, args);
    va_end(args);

    char utf8[2048] = {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8,
                                    sizeof(utf8), nullptr, nullptr);
    if (bytes <= 1) {
        return;
    }

    AcquireSRWLockExclusive(&lock);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
        WriteFile(file, "\r\n", 2, &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&lock);
}

constexpr uintptr_t kPreferredBase = 0x180000000;

// StartUI::App::PrepareRootElement(Frame^, PerFrameData^, bool) -> UIElement^
// StartUI_.dll 10.0.22621.3733 (x86-64).
constexpr uintptr_t kPrepareRootElementVa = 0x1800AD730;

// StartUI::SplitViewFrame::OnGlobalAnimationRequested(sender, args) -> void
// StartUI_.dll 10.0.22621.3733 (x86-64).
//
// The handler forwards the shell's animation request to a delegate stored at
// this+0x3C0. The animation type is NOT an argument: the handler reads three
// ints from the args object through the wrappers below (vtable slots 6, 7 and
// 8). The function never writes eax and has no direct call site, so its return
// value is unused and the hook must return void as well.
constexpr uintptr_t kOnGlobalAnimationRequestedVa = 0x1804164C0;
constexpr uintptr_t kAnimTypeGetterVa = 0x18041A978;
constexpr uintptr_t kAnimEdgeGetterVa = 0x18041AA20;
constexpr uintptr_t kAnimTransitionGetterVa = 0x18041A9CC;

// Bytes 0x00..0x27 of the target. The three E8 calls at +0x27, +0x33 and
// +0x3F read the animation type, the taskbar edge and the transition type; the
// installer resolves their rel32 targets before it trusts this layout.
constexpr uint8_t kAnimPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,  // mov qword ptr [rsp+0x10], rbx
    0x48, 0x89, 0x74, 0x24, 0x20,  // mov qword ptr [rsp+0x20], rsi
    0x57,                          // push rdi
    0x48, 0x83, 0xEC, 0x70,        // sub  rsp, 0x70
    0x48, 0x8B, 0x05,              // mov  rax, [rip+cookie]
    0x00, 0x00, 0x00, 0x00,        //      ... displacement, checked below
    0x48, 0x33, 0xC4,              // xor  rax, rsp
    0x48, 0x89, 0x44, 0x24, 0x60,  // mov  qword ptr [rsp+0x60], rax
    0x49, 0x8B, 0xD8,              // mov  rbx, r8   (args)
    0x48, 0x8B, 0xF9,              // mov  rdi, rcx  (self)
    0x49, 0x8B, 0xC8,              // mov  rcx, r8
};

// The committed dismiss slides the full panel width out of the screen, so it
// gets the longest duration: 220 ms keeps the peak speed low enough that the
// slide reads as a settle, not as the panel flying away. Every animation here
// carries an easing curve; a plain two-keyframe pair is linear and starts at
// its peak velocity on the first frame.
constexpr std::chrono::milliseconds kDismissSettleDuration{220};
// A cancelled reveal retracts a panel that is only partly on screen. 140 ms on
// an ease-in-out curve starts gently and settles at the edge; a shorter linear
// run read as a snap back.
constexpr std::chrono::milliseconds kRevealRetractDuration{140};
constexpr std::chrono::milliseconds kDismissCancelDuration{120};

// A cancelled reveal taps the Windows key only after the panel has slid back
// out of the screen edge, which is the mirror of the committed dismiss. The tap
// waits on the 20 ms input timer, so the deadline stays above the 140 ms
// retract plus one timer tick. A tap inside the animation froze the panel
// mid-retract and hid the window at that position.
constexpr uint64_t kRevealCancelWinTapMs = 160;

// A Windows tap this mod sent closes the menu a moment later. Until then the
// shell still reports the window open, so the reopen poll must not read that as
// a fresh open and restore Progress while the close is in flight.
constexpr uint64_t kRestoreSuppressMs = 600;

// Gesture thresholds. Delta and Distance are DIP, velocity is DIP/ms.
constexpr float kCommitDistanceFraction = 0.20f;
constexpr float kCommitVelocity = 0.45f;
constexpr float kScrollAbortPerp = 120.0f;
constexpr float kScrollAbortRatio = 1.5f;
constexpr float kScrollAbortMaxDelta = 60.0f;
// The insert filter is a screen distance, not a progress fraction: one step of
// the scalar must move the panel by at least this many DIP. A fixed 0.004 was
// 5.8 DIP on the 1440 DIP distance here, which is 11.5 physical px at 200%
// scaling and read as a staircase during a drag. The panel reports at ~170 Hz,
// so writing Progress per report is affordable and buys a continuous follow.
constexpr float kProgressEpsilonDip = 0.5f;
constexpr float kWindowMarginDip = 4.0f;

// Reveal follow (edge swipe to open). A contact that starts inside the edge
// band on the taskbar side can drive the menu's own open. The release decision
// uses the dismiss thresholds on the travel away from the edge, not on the
// mapped progress: the screen edge already maps to Progress 0.269 here, so any
// progress threshold commits every release.
constexpr float kEdgeBandDip = 12.0f;
constexpr uint64_t kIdleReleaseMs = 35;
constexpr UINT_PTR kIdleTimerId = 1;
constexpr UINT kIdleTimerMs = 20;
constexpr UINT_PTR kDebugTimerId = 2;
constexpr UINT kDebugTimerMs = 50;
// The DWM cloaked query is a syscall. The report handler accepts one answer
// per interval and reuses it for every other report.
constexpr uint64_t kOpenCheckIntervalMs = 20;

// Raw input and HID decoding limits.
constexpr int kMaxHidDevices = 4;
constexpr int kMaxContactCollections = 8;
constexpr int kMaxLinkNodes = 64;
constexpr int kHidPathChars = 512;
constexpr wchar_t kHidWindowClass[] =
    L"WindhawkLegacyStartSwipeRawInputWindow";

constexpr USAGE kUsagePageGeneric = 0x01;
constexpr USAGE kUsagePageDigitizer = 0x0D;
constexpr USAGE kUsageTouchScreen = 0x04;
constexpr USAGE kUsagePen = 0x02;
constexpr USAGE kUsageContact = 0x05;
constexpr USAGE kUsageFinger = 0x22;
constexpr USAGE kUsageX = 0x30;
constexpr USAGE kUsageY = 0x31;
constexpr USAGE kUsageInRange = 0x32;
constexpr USAGE kUsageTipSwitch = 0x42;
constexpr USAGE kUsageContactCount = 0x54;

// ICoreWindowInterop (45D64A29-A63E-4CB6-B498-5781D298CB4F). Slot 3 is
// get_WindowHandle; the interface is classic COM, so it is called directly.
constexpr GUID kCoreWindowInteropIid = {
    0x45D64A29, 0xA63E, 0x4CB6,
    {0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F}};
using GetCoreWindowHandleAbi = HRESULT(STDMETHODCALLTYPE*)(void*, HWND*);

// DWMWA_CLOAKED (dwmapi.h value 14). The CoreWindow HWND outlives the menu and
// IsWindowVisible stays TRUE while it is closed, so cloaking is the only
// reliable open/closed signal: 0 while the menu is shown.
constexpr DWORD kDwmwaCloaked = 14;
using DwmGetWindowAttribute_t = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);

enum class DismissAxis : int32_t {
    Left = 0,
    Top = 1,
    Right = 2,
    Bottom = 3,
};

bool IsHorizontalAxis(int32_t axis) {
    return axis == static_cast<int32_t>(DismissAxis::Left) ||
           axis == static_cast<int32_t>(DismissAxis::Right);
}

std::atomic<bool> g_disabled{false};
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_stopDismissCallback{false};

// The open storyboard drops only when the debug line `anim=0` asks for it.
// Measured on this build: skipping the open request leaves the window open
// (DWMWA_CLOAKED 0) with no pane drawn, because that request is also the step
// that presents the SplitViewFrame content. The switch therefore starts at the
// original behaviour.
std::atomic<bool> g_suppressOpenAnim{false};
std::atomic<int32_t> g_animLogCount{0};

// ---------------------------------------------------------------------------
// Composition state
//
// Progress is the only animated scalar; Distance is the static window width
// or height. Both the property set and the visual are agile Composition
// objects, so the input thread and the settle batch callback can use them.
// The mutex protects the projection pointers and the check-then-update
// sequence against teardown; it is never held across a wait.
// ---------------------------------------------------------------------------

std::mutex g_compositionMutex;
wuc::Compositor g_compositor{nullptr};
wuc::CompositionPropertySet g_state{nullptr};
wuc::ExpressionAnimation g_translationExpression{nullptr};
wuc::Visual g_visual{nullptr};
wuc::CompositionScopedBatch g_dismissBatch{nullptr};

// Easing curves, created in Setup and cleared with the property set. Both
// animations read them under g_compositionMutex, so they never outlive the
// compositor that created them.
wuc::CompositionEasingFunction g_settleEasing{nullptr};
wuc::CompositionEasingFunction g_retractEasing{nullptr};

wux::FrameworkElement g_root{nullptr};  // UI thread only
wucore::CoreDispatcher g_dispatcher{nullptr};
std::atomic<DWORD> g_uiThreadId{0};
std::atomic<uint32_t> g_setupGeneration{0};

winrt::event_token g_loadedToken{};
winrt::event_token g_sizeToken{};

std::atomic<bool> g_dismissSettling{false};
std::atomic<uint32_t> g_dismissSerial{0};
// Set whenever this mod starts a close itself: the dismiss tap in the settle
// batch and the reveal cancel tap on the input thread. PollRootVisibility runs
// on the input thread while the dismiss tap runs in a compositor callback, so
// the deadline is atomic. 0 means no close of this mod is pending.
std::atomic<uint64_t> g_suppressRestoreUntil{0};
std::atomic<HWND> g_coreHwnd{nullptr};
// Resolved in Wh_ModInit. A runtime resolve keeps the mod loadable without
// dwmapi.dll in the import table.
std::atomic<DwmGetWindowAttribute_t> g_dwmGetWindowAttribute{nullptr};

// Filled in at setup and on every PrepareRootElement.
int32_t g_dismissAxis = static_cast<int32_t>(DismissAxis::Left);
float g_dismissDistance = 0.0f;

// Published by the UI thread and read by the input thread. No XAML object is
// shared: only these plain values.
//
// g_hasRoot is set by Setup and never cleared: PrepareRootElement does not fire
// again for a reused XAML root, so it only answers "the renderer was installed
// once in this process". Whether the menu is open is decided per down by the
// DWM cloaked attribute, never by a latch.
std::atomic<bool> g_hasRoot{false};
std::atomic<int32_t> g_axisPub{static_cast<int32_t>(DismissAxis::Left)};
std::atomic<float> g_distancePub{0.0f};
std::atomic<int64_t> g_winRectDip[4]{};  // left, top, right, bottom

// The panel edge that faces the taskbar, in absolute screen DIP. The root
// canvas spans the screen (1440 DIP here) while the visible panel is narrower
// (~993 DIP), so the reveal must be measured against this edge, not against the
// root extent. 0 means "not known yet"; the input thread then uses
// g_distancePub, which restores the old root-width travel.
std::atomic<float> g_panelTrailDip{0.0f};

// UI thread only. Setup resets the open log; a failed mapping logs its fallback
// once per process so a missing panel does not flood the log every resize.
bool g_panelTrailLogged = false;
bool g_panelTrailFallbackLogged = false;

HANDLE g_hidThread = nullptr;
std::atomic<HWND> g_hidHwnd{nullptr};

// Defined with the unload helpers; PrepareRootElement uses them to reset on
// every open.
void RevokeHandlers();
void ResetComposition();
void ResetRenderer();
HWND FindStartCoreWindow();

// ---------------------------------------------------------------------------
// Composition helpers
// ---------------------------------------------------------------------------

bool ShowAtRest() {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state) {
        return false;
    }

    try {
        g_state.StopAnimation(L"Progress");
        g_state.InsertScalar(L"Progress", 1.0f);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Composition reset failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Composition reset failed with an unknown exception");
        return false;
    }

    return true;
}

// Stops the cancel animation so a new drag can drive the scalar directly.
bool StopProgressAnimation() {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state) {
        return false;
    }

    try {
        g_state.StopAnimation(L"Progress");
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Stopping Progress failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Stopping Progress failed with an unknown exception");
        return false;
    }

    return true;
}

bool InsertProgress(float progress) {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state) {
        return false;
    }

    try {
        g_state.InsertScalar(L"Progress", progress);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Progress update failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Progress update failed with an unknown exception");
        return false;
    }

    return true;
}

enum class ProgressEasing {
    Settle,   // ease-out: leaves the release point fast, lands slowly
    Retract,  // ease-in-out: leaves slowly and settles at the screen edge
};

// The easing sits on the keyframe at t=1, the one that defines the motion into
// the target. The t=0 frame carries the start value only; a keyframe pair
// without an easing curve is linear and starts at its peak speed.
bool AnimateProgressTo(float from, float to,
                       std::chrono::milliseconds duration,
                       ProgressEasing easingKind) {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state || !g_compositor) {
        return false;
    }

    const wuc::CompositionEasingFunction easing =
        easingKind == ProgressEasing::Retract ? g_retractEasing
                                              : g_settleEasing;
    if (!easing) {
        return false;
    }

    try {
        wuc::ScalarKeyFrameAnimation animation =
            g_compositor.CreateScalarKeyFrameAnimation();
        animation.InsertKeyFrame(0.0f, std::clamp(from, 0.0f, 1.0f));
        animation.InsertKeyFrame(1.0f, std::clamp(to, 0.0f, 1.0f), easing);
        animation.Duration(duration);

        g_state.StopAnimation(L"Progress");
        // Insert the target as the base value *before* the animation starts, so
        // a cancelled or interrupted animation still lands on the intended end
        // state instead of snapping back to the drag position.
        g_state.InsertScalar(L"Progress", std::clamp(to, 0.0f, 1.0f));
        g_state.StartAnimation(L"Progress", animation);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Progress animation failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Progress animation failed with an unknown exception");
        return false;
    }

    return true;
}

// A lone Windows-key tap is acted on by the shell itself, so it closes the menu
// even when the menu was opened without keyboard focus and an injected
// VK_ESCAPE never reaches it. The 60 ms hold is the same one the dismiss path
// always used; both callers live on threads that already sleep in that path.
void TapWindowsKey() {
    keybd_event(VK_LWIN, 0, 0, 0);
    Sleep(60);
    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
}

// Committed dismiss: slide out over 220 ms on the ease-out curve, then tap the
// Windows key so the shell closes the menu through its normal input path.
bool BeginDismissSettle(float from) {
    wuc::CompositionScopedBatch batch{nullptr};
    uint32_t serial = 0;

    try {
        {
            std::lock_guard lock(g_compositionMutex);
            if (!g_state || !g_compositor || !g_settleEasing) {
                return false;
            }

            serial = g_dismissSerial.fetch_add(1) + 1;

            wuc::ScalarKeyFrameAnimation animation =
                g_compositor.CreateScalarKeyFrameAnimation();
            animation.InsertKeyFrame(0.0f, std::clamp(from, 0.0f, 1.0f));
            animation.InsertKeyFrame(1.0f, 0.0f, g_settleEasing);
            animation.Duration(kDismissSettleDuration);

            batch = g_compositor.CreateScopedBatch(
                wuc::CompositionBatchTypes::Animation);
            batch.Completed(
                [serial](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuc::CompositionBatchCompletedEventArgs const&) {
                    try {
                        std::lock_guard lock(g_compositionMutex);

                        // The stop flag and the serial check guard the state
                        // update against unload and a new open.
                        if (g_stopDismissCallback.load(
                                std::memory_order_relaxed) ||
                            serial != g_dismissSerial.load(
                                          std::memory_order_relaxed)) {
                            return;
                        }

                        // A menu opened without keyboard focus keeps the
                        // foreground on SearchHost, so an injected VK_ESCAPE
                        // does not reach it. A lone Windows-key tap is acted
                        // on by the shell itself and closes the menu in that
                        // state. The suppression window starts before the tap:
                        // the keydown already closes the window, and the poll
                        // between the two events must not restore Progress.
                        g_suppressRestoreUntil.store(
                            GetTickCount64() + kRestoreSuppressMs,
                            std::memory_order_relaxed);
                        TapWindowsKey();
                        FileLog(L"hid dismiss win sent");

                        // Leave Progress at 0: resetting it here flashes the
                        // fully open menu for the frames between this reset and
                        // the shell actually hiding the window. The next
                        // session resets it instead.
                        if (g_state) {
                            g_state.StopAnimation(L"Progress");
                        }
                        g_dismissSettling.store(
                            false, std::memory_order_relaxed);
                    } catch (...) {
                        g_dismissSettling.store(
                            false, std::memory_order_relaxed);
                    }
                });

            g_dismissSettling.store(true, std::memory_order_relaxed);
            g_state.StopAnimation(L"Progress");
            // Base value first: inserting after StartAnimation overrides the
            // animation and turns the 220 ms slide-out into an instant jump.
            g_state.InsertScalar(L"Progress", 0.0f);
            g_state.StartAnimation(L"Progress", animation);
        }

        // End the batch outside the lock: its completion handler takes the
        // same mutex.
        batch.End();
        {
            std::lock_guard lock(g_compositionMutex);
            if (serial == g_dismissSerial.load(std::memory_order_relaxed)) {
                g_dismissBatch = batch;
            }
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Dismiss animation failed: %08X %s", e.code(),
               e.message().c_str());
        g_dismissSettling.store(false, std::memory_order_relaxed);
        return false;
    } catch (...) {
        Wh_Log(L"Dismiss animation failed with an unknown exception");
        g_dismissSettling.store(false, std::memory_order_relaxed);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Debug command channel
//
// Kept in the shipping build. It applies one rendering mechanism at a time so
// the candidates can be compared without a gesture. The file is
//   C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\
//   local@start-menu-legacy-swipe\debug.txt
// which is the directory Wh_GetModStoragePath returns. One command per line:
//   p=<progress>        InsertProgress, clamped to 0..1
//   t=<x>,<y>,<z>       UIElement.Translation, applied on the UI thread (DIP)
//   e=<x>[,<y>]         visual Properties["Offset"] and Visual.Offset (DIP)
// The raw input thread polls the file every 50 ms and re-reads it only when
// its write time or size changes. Nothing is logged per poll.
// ---------------------------------------------------------------------------

constexpr wchar_t kDebugFileName[] = L"debug.txt";
constexpr DWORD kDebugFileMaxBytes = 4096;

struct DebugFileStamp {
    bool seen = false;
    DWORD writeLow = 0;
    DWORD writeHigh = 0;
    LONGLONG size = 0;
};

DebugFileStamp g_debugFileStamp;  // input thread only

bool DebugFilePath(WCHAR* path, size_t count) {
    WCHAR storage[MAX_PATH] = {};
    if (!Wh_GetModStoragePath(storage, ARRAYSIZE(storage))) {
        return false;
    }

    return swprintf_s(path, count, L"%s\\%s", storage, kDebugFileName) > 0;
}

// Parses "v[,v...]" up to maxCount values. A missing, partial or trailing
// token rejects the whole line.
bool ParseFloatList(const char* text, int maxCount, float* values,
                    int& count) {
    count = 0;
    const char* cursor = text;

    while (count < maxCount) {
        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }

        char* end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(value)) {
            return false;
        }
        values[count++] = value;
        cursor = end;

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == ',') {
            ++cursor;
            continue;
        }
        break;
    }

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') {
        ++cursor;
    }
    return *cursor == '\0';
}

// The alternative to the expression: write the visual Offset directly. Both
// the property set entry and the visual property are written, because the
// probe must cover both forms the visual may read.
bool SetVisualOffset(float x, float y) {
    std::lock_guard lock(g_compositionMutex);
    if (!g_visual) {
        return false;
    }

    try {
        g_visual.Properties().InsertVector3(L"Offset", float3{x, y, 0.0f});
        g_visual.Offset(float3{x, y, 0.0f});
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Visual offset failed: %08X %s",
               static_cast<unsigned>(e.code()), e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Visual offset failed with an unknown exception");
        return false;
    }

    return true;
}

// The other alternative: the XAML framework translation property, which must be
// set on the UI thread. The call is asynchronous, so the applied line is logged
// from the callback and never claims a change the dispatcher refused.
void RequestTranslation(float x, float y, float z) {
    wucore::CoreDispatcher dispatcher = g_dispatcher;
    if (!dispatcher || g_unloading.load(std::memory_order_relaxed)) {
        return;
    }

    try {
        dispatcher.TryRunAsync(
            wucore::CoreDispatcherPriority::Normal,
            [x, y, z]() {
                try {
                    wux::FrameworkElement root = g_root;
                    if (!root) {
                        return;
                    }
                    wuxh::ElementCompositionPreview::SetIsTranslationEnabled(
                        root, true);
                    root.Translation(float3{x, y, z});
                    FileLog(L"debug t=%.1f,%.1f,%.1f", x, y, z);
                } catch (winrt::hresult_error const& e) {
                    FileLog(L"debug t fail err=%08X",
                            static_cast<unsigned>(e.code()));
                } catch (...) {
                    FileLog(L"debug t fail err=unknown");
                }
            });
    } catch (winrt::hresult_error const& e) {
        FileLog(L"debug t queue err=%08X", static_cast<unsigned>(e.code()));
    } catch (...) {
        FileLog(L"debug t queue err=unknown");
    }
}

void ApplyDebugLine(const char* line) {
    while (*line == ' ' || *line == '\t' || *line == '\r') {
        ++line;
    }
    if (line[0] == '\0') {
        return;
    }

    // anim=0 drops the shell's own open storyboard, anim=1 restores it. The
    // switch is its own command word, so it is matched before the one-letter
    // commands and their `letter=value` shape.
    if (std::strncmp(line, "anim=", 5) == 0) {
        const char* value = line + 5;
        while (*value == ' ' || *value == '\t') {
            ++value;
        }
        if (*value != '0' && *value != '1') {
            return;
        }
        const int anim = *value - '0';
        g_suppressOpenAnim.store(anim == 0, std::memory_order_relaxed);
        FileLog(L"debug anim=%d", anim);
        return;
    }

    if (line[1] != '=') {
        return;
    }

    float values[3] = {};
    int count = 0;

    switch (line[0]) {
        case 'p':
            if (!ParseFloatList(line + 2, 1, values, count) || count != 1) {
                return;
            }
            {
                const float progress = std::clamp(values[0], 0.0f, 1.0f);
                if (InsertProgress(progress)) {
                    FileLog(L"debug p=%.3f", progress);
                }
            }
            return;
        case 't':
            if (!ParseFloatList(line + 2, 3, values, count) || count != 3) {
                return;
            }
            RequestTranslation(values[0], values[1], values[2]);
            return;
        case 'e':
            if (!ParseFloatList(line + 2, 2, values, count) || count < 1) {
                return;
            }
            {
                const float y = count > 1 ? values[1] : 0.0f;
                if (SetVisualOffset(values[0], y)) {
                    FileLog(L"debug e=%.1f,%.1f", values[0], y);
                }
            }
            return;
        default:
            return;
    }
}

void ApplyDebugCommands(char* text) {
    char* cursor = text;
    while (*cursor != '\0') {
        char* newline = std::strchr(cursor, '\n');
        if (newline) {
            *newline = '\0';
        }
        ApplyDebugLine(cursor);
        if (!newline) {
            return;
        }
        cursor = newline + 1;
    }
}

// Reads the command file only when its write time or size changed, and only
// after the renderer exists: before the first Setup neither mechanism has an
// object to act on.
void PollDebugFile() {
    if (g_unloading.load(std::memory_order_relaxed) ||
        !g_hasRoot.load(std::memory_order_relaxed)) {
        return;
    }

    WCHAR path[MAX_PATH] = {};
    if (!DebugFilePath(path, ARRAYSIZE(path))) {
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        // A missing file is not an error. Forget the stamp so a recreated file
        // is read even when it keeps the old size.
        g_debugFileStamp = DebugFileStamp{};
        return;
    }

    const LONGLONG size =
        (static_cast<LONGLONG>(attributes.nFileSizeHigh) << 32) |
        static_cast<LONGLONG>(attributes.nFileSizeLow);

    if (g_debugFileStamp.seen &&
        attributes.ftLastWriteTime.dwLowDateTime ==
            g_debugFileStamp.writeLow &&
        attributes.ftLastWriteTime.dwHighDateTime ==
            g_debugFileStamp.writeHigh &&
        size == g_debugFileStamp.size) {
        return;
    }

    g_debugFileStamp.seen = true;
    g_debugFileStamp.writeLow = attributes.ftLastWriteTime.dwLowDateTime;
    g_debugFileStamp.writeHigh = attributes.ftLastWriteTime.dwHighDateTime;
    g_debugFileStamp.size = size;

    if (size <= 0 || size > kDebugFileMaxBytes) {
        return;
    }

    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    char buffer[kDebugFileMaxBytes + 1] = {};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buffer, kDebugFileMaxBytes, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) {
        return;
    }
    buffer[read] = '\0';

    // An editor may prepend a UTF-8 BOM; skip it so the first line parses.
    char* text = buffer;
    if (read >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text += 3;
    }

    ApplyDebugCommands(text);
}

// ---------------------------------------------------------------------------
// Raw HID input
//
// The legacy Start consumes the pointer stream before XAML sees it, so the
// only reliable live source is raw input. Each digitizer is decoded from its
// RIDI_PREPARSEDDATA blob; the device is never opened.
// ---------------------------------------------------------------------------

struct HidDevice {
    HANDLE handle = nullptr;
    bool ready = false;
    bool decodeFailLogged = false;
    uint64_t lastUsedTick = 0;
    WCHAR path[kHidPathChars] = {};
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    std::vector<HIDP_VALUE_CAPS> valueCaps;
    USHORT contactNodes[kMaxContactCollections] = {};
    int contactNodeCount = 0;
    USHORT contactCountLink = 0;
    bool hasContactCount = false;
};

HidDevice g_hidDevices[kMaxHidDevices];
int g_hidDeviceCount = 0;  // input thread only
bool g_hidCacheFullLogged = false;

void CopyDevicePath(HANDLE handle, WCHAR* out, size_t outCount) {
    out[0] = L'\0';

    UINT chars = 0;
    SetLastError(0);
    if (GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, nullptr, &chars) ==
            static_cast<UINT>(-1) ||
        chars == 0) {
        wcsncpy_s(out, outCount, L"?", _TRUNCATE);
        return;
    }

    WCHAR* name = new (std::nothrow) WCHAR[chars + 1];
    if (!name) {
        wcsncpy_s(out, outCount, L"?", _TRUNCATE);
        return;
    }

    name[0] = L'\0';
    UINT fetched = chars;
    SetLastError(0);
    if (GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, name, &fetched) ==
        static_cast<UINT>(-1)) {
        wcsncpy_s(out, outCount, L"?", _TRUNCATE);
    } else {
        name[chars] = L'\0';
        wcsncpy_s(out, outCount, name, _TRUNCATE);
    }

    delete[] name;
}

void LogDecodeFail(HidDevice& device, unsigned error) {
    if (device.decodeFailLogged) {
        return;
    }

    device.decodeFailLogged = true;
    FileLog(L"hid decodefail dev=%s err=%u", device.path, error);
}

// Reads the path, the preparsed blob, the contact collection indices and the
// value caps once per device. Runs on the input thread.
void InitHidDevice(HidDevice& device) {
    CopyDevicePath(device.handle, device.path, ARRAYSIZE(device.path));

    UINT bytes = 0;
    SetLastError(0);
    if (GetRawInputDeviceInfoW(device.handle, RIDI_PREPARSEDDATA, nullptr,
                               &bytes) == static_cast<UINT>(-1) ||
        bytes == 0 || bytes > 64 * 1024) {
        LogDecodeFail(device, GetLastError());
        return;
    }

    BYTE* blob = new (std::nothrow) BYTE[bytes];
    if (!blob) {
        return;
    }

    UINT fetched = bytes;
    SetLastError(0);
    if (GetRawInputDeviceInfoW(device.handle, RIDI_PREPARSEDDATA, blob,
                               &fetched) == static_cast<UINT>(-1) ||
        fetched == 0) {
        LogDecodeFail(device, GetLastError());
        delete[] blob;
        return;
    }

    device.preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(blob);

    HIDP_CAPS caps{};
    const NTSTATUS capsStatus = HidP_GetCaps(device.preparsed, &caps);
    if (capsStatus != HIDP_STATUS_SUCCESS) {
        device.decodeFailLogged = true;
        FileLog(L"hid decodefail dev=%s err=0x%08X", device.path,
                static_cast<unsigned>(capsStatus));
        return;
    }

    // The logical range of X and Y is needed to map raw values to pixels.
    if (caps.NumberInputValueCaps > 0) {
        device.valueCaps.resize(caps.NumberInputValueCaps);
        USHORT length = caps.NumberInputValueCaps;
        if (HidP_GetValueCaps(HidP_Input, device.valueCaps.data(), &length,
                              device.preparsed) == HIDP_STATUS_SUCCESS) {
            device.valueCaps.resize(length);
        } else {
            device.valueCaps.clear();
        }
    }

    // Contact collections are 0x0D/0x05 on classic digitizers and 0x0D/0x22
    // (Finger) on the ELAN2513. Both carry X/Y per contact.
    HIDP_LINK_COLLECTION_NODE nodes[kMaxLinkNodes]{};
    ULONG nodeCount = kMaxLinkNodes;
    if (HidP_GetLinkCollectionNodes(nodes, &nodeCount, device.preparsed) ==
        HIDP_STATUS_SUCCESS) {
        if (nodeCount > kMaxLinkNodes) {
            nodeCount = kMaxLinkNodes;
        }

        for (ULONG i = 0; i < nodeCount; ++i) {
            if (nodes[i].LinkUsagePage != kUsagePageDigitizer ||
                (nodes[i].LinkUsage != kUsageContact &&
                 nodes[i].LinkUsage != kUsageFinger)) {
                continue;
            }
            if (device.contactNodeCount < kMaxContactCollections) {
                device.contactNodes[device.contactNodeCount++] =
                    static_cast<USHORT>(i);
            }
        }
    }

    // The Contact Count that reports how many contacts are down lives in the
    // top-level digitizer collection, not in collection 0. Reading it with the
    // wrong collection makes a lift report look like "still one contact",
    // which forces the idle timer to guess the release.
    for (const HIDP_VALUE_CAPS& cap : device.valueCaps) {
        if (cap.UsagePage != kUsagePageDigitizer) {
            continue;
        }
        const bool matches =
            cap.IsRange ? (cap.Range.UsageMin <= kUsageContactCount &&
                           kUsageContactCount <= cap.Range.UsageMax)
                        : (cap.NotRange.Usage == kUsageContactCount);
        if (!matches) {
            continue;
        }
        device.contactCountLink = cap.LinkCollection;
        device.hasContactCount = true;
        break;
    }
    FileLog(L"hidcc dev=%s have=%d link=%u", device.path,
            device.hasContactCount ? 1 : 0,
            static_cast<unsigned>(device.contactCountLink));

    device.ready = true;
}

HidDevice* GetHidDevice(HANDLE handle) {
    if (!handle) {
        return nullptr;
    }

    const uint64_t now = GetTickCount64();
    for (int i = 0; i < g_hidDeviceCount; ++i) {
        if (g_hidDevices[i].handle == handle) {
            g_hidDevices[i].lastUsedTick = now;
            return &g_hidDevices[i];
        }
    }

    HidDevice* device = nullptr;
    if (g_hidDeviceCount < kMaxHidDevices) {
        device = &g_hidDevices[g_hidDeviceCount++];
    } else {
        // Injectors create a fresh virtual digitizer handle for every process,
        // so the table must recycle instead of going deaf after four devices.
        device = &g_hidDevices[0];
        for (int i = 1; i < g_hidDeviceCount; ++i) {
            if (g_hidDevices[i].lastUsedTick < device->lastUsedTick) {
                device = &g_hidDevices[i];
            }
        }
        delete[] reinterpret_cast<BYTE*>(device->preparsed);
        if (!g_hidCacheFullLogged) {
            g_hidCacheFullLogged = true;
            FileLog(L"hid device table recycled (was full)");
        }
    }

    *device = HidDevice{};
    device->handle = handle;
    device->lastUsedTick = now;
    InitHidDevice(*device);
    return device;
}

void FreeHidDevices() {
    for (int i = 0; i < g_hidDeviceCount; ++i) {
        delete[] reinterpret_cast<BYTE*>(g_hidDevices[i].preparsed);
        g_hidDevices[i] = HidDevice{};
    }
    g_hidDeviceCount = 0;
}

bool ReadUsageValue(const HidDevice& device, USAGE usagePage,
                    USHORT collection, USAGE usage, const BYTE* report,
                    ULONG reportLength, ULONG* value) {
    if (!device.preparsed) {
        return false;
    }

    ULONG parsed = 0;
    const NTSTATUS status = HidP_GetUsageValue(
        HidP_Input, usagePage, collection, usage, &parsed, device.preparsed,
        reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)), reportLength);
    if (status != HIDP_STATUS_SUCCESS) {
        return false;
    }

    *value = parsed;
    return true;
}

struct HidAxisRange {
    long min = 0;
    long max = 0;
    bool valid = false;
};

// Selects the value cap that describes this report. reportId is the HID
// report ID from RAWINPUTHEADER.wParam; 0 means the device has no report IDs.
HidAxisRange FindAxisRange(const HidDevice& device, USAGE usage,
                           USHORT reportId) {
    HidAxisRange range;

    for (const HIDP_VALUE_CAPS& cap : device.valueCaps) {
        if (cap.UsagePage != kUsagePageGeneric) {
            continue;
        }

        const bool usageMatches =
            cap.IsRange
                ? (usage >= cap.Range.UsageMin && usage <= cap.Range.UsageMax)
                : (cap.NotRange.Usage == usage);
        if (!usageMatches) {
            continue;
        }
        if (cap.ReportID != 0 && reportId != 0 && cap.ReportID != reportId) {
            continue;
        }
        if (cap.LogicalMax <= cap.LogicalMin) {
            continue;
        }

        range.min = cap.LogicalMin;
        range.max = cap.LogicalMax;
        range.valid = true;
        break;
    }

    return range;
}

int32_t ToScreenPixel(long raw, const HidAxisRange& range, int screenSize) {
    if (!range.valid || screenSize <= 1) {
        return static_cast<int32_t>(raw);
    }

    const int64_t span = static_cast<int64_t>(range.max) - range.min;
    if (span <= 0) {
        return static_cast<int32_t>(raw);
    }

    const int64_t value = static_cast<int64_t>(raw) - range.min;
    int64_t pixel = value * (screenSize - 1) / span;
    if (pixel < 0) {
        pixel = 0;
    }
    if (pixel > screenSize - 1) {
        pixel = screenSize - 1;
    }
    return static_cast<int32_t>(pixel);
}

// The HID thread never sees the XAML thread's CoreWindow pointer. The UI
// thread stores it in Setup; if that did not happen, look it up once.
HWND FindStartCoreWindow() {
    DWORD processId = GetCurrentProcessId();
    HWND hwnd = FindWindowW(L"Windows.UI.Core.CoreWindow", nullptr);
    while (hwnd) {
        DWORD owner = 0;
        GetWindowThreadProcessId(hwnd, &owner);
        if (owner == processId) {
            return hwnd;
        }
        hwnd = FindWindowExW(nullptr, hwnd, L"Windows.UI.Core.CoreWindow",
                             nullptr);
    }
    return nullptr;
}

// Cached answer of StartWindowIsOpen() for the report handler. The cloaked
// query is a syscall, so one answer serves every report inside 20 ms; the
// cache is dropped on each lift so a down always starts from a fresh query.
bool g_openCheckValid = false;     // input thread only
bool g_openCheckOpen = false;      // input thread only
uint64_t g_lastOpenCheckTick = 0;  // input thread only

// The menu closes by cloaking its CoreWindow, not by destroying or hiding it.
// Called once per down; nothing is cached here, because the menu opens and
// closes many times on the same HWND.
bool StartWindowIsOpen() {
    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        return false;
    }

    DwmGetWindowAttribute_t dwmGetWindowAttribute =
        g_dwmGetWindowAttribute.load(std::memory_order_relaxed);
    if (!dwmGetWindowAttribute) {
        return false;
    }

    DWORD cloaked = 0;
    const HRESULT hr =
        dwmGetWindowAttribute(hwnd, kDwmwaCloaked, &cloaked, sizeof(cloaked));
    return hr == S_OK && cloaked == 0;
}

// The candidate contact needs the open state on every report, but the cloaked
// query is a syscall. One answer serves all reports inside 20 ms; the cache is
// invalidated when a contact lifts, so a down always starts from a fresh one.
bool StartWindowIsOpenThrottled(uint64_t tickMs) {
    if (g_openCheckValid &&
        tickMs - g_lastOpenCheckTick < kOpenCheckIntervalMs) {
        return g_openCheckOpen;
    }

    g_lastOpenCheckTick = tickMs;
    g_openCheckValid = true;
    g_openCheckOpen = StartWindowIsOpen();
    return g_openCheckOpen;
}

float HidDpiScale() {
    static HWND cachedHwnd = nullptr;
    static float cachedScale = 1.0f;

    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        hwnd = FindStartCoreWindow();
        if (hwnd) {
            g_coreHwnd.store(hwnd, std::memory_order_relaxed);
        }
    }
    if (!hwnd) {
        return 1.0f;
    }

    if (hwnd != cachedHwnd) {
        const UINT dpi = GetDpiForWindow(hwnd);
        if (dpi == 0) {
            // The window may still be initializing; retry on the next report
            // instead of caching a wrong scale for the whole session.
            return 1.0f;
        }
        cachedHwnd = hwnd;
        cachedScale = static_cast<float>(dpi) / 96.0f;
    }

    return cachedScale;
}

struct HidGestureSession {
    bool active = false;
    bool suppress = false;  // ignore samples until all contacts are released
    long lastRawX = 0;
    long lastRawY = 0;
    float startX = 0.0f;
    float startY = 0.0f;
    uint64_t startTick = 0;
    uint64_t lastTick = 0;
    float curDelta = 0.0f;
    float maxDelta = 0.0f;
    float maxPerp = 0.0f;
    float lastProgress = 1.0f;
    int samples = 0;
};

HidGestureSession g_hidSession;  // input thread only

// Reveal follow. The contact that starts on the taskbar edge is remembered
// while the menu is closed. EP opens the menu by itself once the contact
// passes its own edge threshold, and the same contact is still down at that
// moment, so it takes Progress over and the menu follows it from there.
//
// `distance` is the root extent along the axis: Progress 1 leaves the root at
// rest, Progress 0 slides it out by Distance. `panelTrail` is the panel edge in
// absolute screen DIP that the finger meets: the reveal is
// Progress = 1 - (finger - panelTrail) / (dir * distance), so the panel's
// trailing edge sits under the finger at every sample and Progress no longer
// depends on when the menu was seen opening. That mapping is visual only.
// `travel` is the decision quantity: the distance the finger moved away from
// the taskbar edge since the touch began, clamped at 0, so pulling out and back
// reads 0 at release. `maxTravel` is the furthest it got; the log shows it.
// `progress` is the last computed value, `lastInserted` the last one that
// reached the property set, so the 0.5 DIP change filter compares against what
// the renderer actually shows.
struct HidRevealSession {
    bool edgeCandidate = false;
    bool active = false;
    float edgeStartX = 0.0f;  // first report, kept for the edge-band test
    float edgeStartY = 0.0f;
    float distance = 0.0f;
    float panelTrail = 0.0f;
    float travel = 0.0f;
    float maxTravel = 0.0f;
    float progress = 0.0f;
    float lastInserted = 1.0f;
    int samples = 0;
    uint64_t startTick = 0;
    uint64_t lastTick = 0;
};

HidRevealSession g_reveal;  // input thread only

// True from the first report of a contact until it lifts. The edge test runs
// once per contact, never on a finger that slides into the band later.
bool g_contactDown = false;      // input thread only
uint64_t g_contactTick = 0;      // input thread only, last report of a contact

// Screen size in DIP, rewritten by FeedHidReport before every state-machine
// step. The right and bottom edge bands are measured from it.
float g_screenDipWidth = 0.0f;   // input thread only
float g_screenDipHeight = 0.0f;  // input thread only

// Set when a commit hands Progress to the settle animation. The reuse of one
// XAML root means the next drag can still see a translated root or a base
// value of 0, so BeginHidSession restores the resting value first.
bool g_commitNeedsProgressReset = false;  // input thread only
bool g_rootWasOpen = false;              // input thread only
// A cancelled reveal closes the menu only after its retract animation, so the
// Windows tap waits for this deadline on the 20 ms input timer. 0 means none is
// pending. A new follow clears it: that contact is pulling the menu out again.
uint64_t g_pendingWinTapTick = 0;        // input thread only
uint64_t g_edgeLogTick = 0;              // input thread only, diagnostic
// The edge line was a measurement instrument: it is off in the shipping build.
constexpr bool g_debugEdge = false;

float AxisDelta(int32_t axis, const HidGestureSession& session, float x,
                float y) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
            return session.startX - x;
        case DismissAxis::Right:
            return x - session.startX;
        case DismissAxis::Top:
            return session.startY - y;
        case DismissAxis::Bottom:
            return y - session.startY;
    }
    return session.startX - x;
}

float PerpDelta(int32_t axis, const HidGestureSession& session, float x,
                float y) {
    if (IsHorizontalAxis(axis)) {
        return std::fabs(y - session.startY);
    }
    return std::fabs(x - session.startX);
}

bool PointInStartWindow(float x, float y) {
    const int64_t left = g_winRectDip[0].load(std::memory_order_relaxed);
    const int64_t top = g_winRectDip[1].load(std::memory_order_relaxed);
    const int64_t right = g_winRectDip[2].load(std::memory_order_relaxed);
    const int64_t bottom = g_winRectDip[3].load(std::memory_order_relaxed);

    // Without published bounds, do not reject a plausible touch.
    if (right <= left || bottom <= top) {
        return true;
    }

    return x >= static_cast<float>(left) - kWindowMarginDip &&
           x <= static_cast<float>(right) + kWindowMarginDip &&
           y >= static_cast<float>(top) - kWindowMarginDip &&
           y <= static_cast<float>(bottom) + kWindowMarginDip;
}

// The taskbar sits on the side the dismiss axis slides out along, so that axis
// also names the edge band the reveal starts from.
bool PointInEdgeBand(int32_t axis, float x, float y) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
            return x <= kEdgeBandDip;
        case DismissAxis::Top:
            return y <= kEdgeBandDip;
        case DismissAxis::Right:
            return g_screenDipWidth > kEdgeBandDip &&
                   x >= g_screenDipWidth - kEdgeBandDip;
        case DismissAxis::Bottom:
            return g_screenDipHeight > kEdgeBandDip &&
                   y >= g_screenDipHeight - kEdgeBandDip;
    }
    return false;
}

// Finger position on the gesture axis in absolute screen DIP. The reveal
// formula compares it to the panel trail directly, so the same drag yields the
// same progress on every open.
float AxisScreenPosition(int32_t axis, float x, float y) {
    return IsHorizontalAxis(axis) ? x : y;
}

// The direction the menu slides out along, as the DirX/DirY sign: -1 for a
// left or top taskbar, +1 for right or bottom.
float AxisDirection(int32_t axis) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
        case DismissAxis::Top:
            return -1.0f;
        case DismissAxis::Right:
        case DismissAxis::Bottom:
            return 1.0f;
    }
    return -1.0f;
}

// The direction a revealing finger moves away from the taskbar edge: the
// opposite of AxisDirection, so +1 for a left or top taskbar and -1 for right
// or bottom. The reveal's travel and velocity use it; the visual mapping does
// not, because that one already divides by AxisDirection.
float AxisOpeningSign(int32_t axis) {
    return -AxisDirection(axis);
}

// Runs on the first report of a contact. A contact that starts on the taskbar
// edge while the menu is closed can own the menu's own open; anything else is
// left to the dismiss path. A touch on the edge of an *open* menu is a dismiss
// start, not a candidate.
bool BeginEdgeCandidate(float x, float y, uint64_t tickMs) {
    if (g_dismissSettling.load(std::memory_order_relaxed) ||
        !g_hasRoot.load(std::memory_order_relaxed)) {
        return false;
    }

    const float distance = g_distancePub.load(std::memory_order_relaxed);
    if (!(distance > 0.0f)) {
        return false;  // renderer not ready: no distance to follow
    }

    const int32_t axis = g_axisPub.load(std::memory_order_relaxed);
    if (!PointInEdgeBand(axis, x, y)) {
        return false;
    }

    // A close this mod started still reports the window open for a moment. A
    // contact on the edge in that window is a new reveal attempt, not a drag of
    // the retracting panel: the shell opens the menu again under the contact
    // and the follow takes over. Treating it as a dismiss drag froze the
    // retract and restored Progress 1 on the down.
    const bool closeInFlight =
        tickMs < g_suppressRestoreUntil.load(std::memory_order_relaxed);
    if (!closeInFlight && StartWindowIsOpenThrottled(tickMs)) {
        return false;
    }

    g_reveal = HidRevealSession{};
    g_reveal.edgeCandidate = true;
    g_reveal.edgeStartX = x;
    g_reveal.edgeStartY = y;
    g_reveal.startTick = tickMs;
    g_reveal.lastTick = tickMs;
    return true;
}

// One reveal step. While the menu is still closed the contact is only
// remembered; the moment it is open under the contact, the follow takes
// Progress over and every later report drives it.
void UpdateRevealSession(float x, float y, uint64_t tickMs) {
    HidRevealSession& reveal = g_reveal;
    reveal.lastTick = tickMs;

    if (!reveal.active) {
        if (!StartWindowIsOpenThrottled(tickMs)) {
            return;
        }

        const float distance = g_distancePub.load(std::memory_order_relaxed);
        if (!(distance > 0.0f)) {
            // A new root is being installed under the contact, so this contact
            // no longer has a renderer to drive.
            reveal.edgeCandidate = false;
            return;
        }

        reveal.active = true;
        reveal.distance = distance;
        reveal.progress = 0.0f;
        reveal.lastInserted = 1.0f;  // the menu is fully in right now
        reveal.samples = 0;

        // A cancel or dismiss animation from a previous gesture may still own
        // Progress; the follow must drive the scalar from this sample on. The
        // deferred Windows tap of a cancelled reveal is dropped here as well:
        // the menu is being pulled out again, and a tap mid-follow would close
        // it under the finger.
        StopProgressAnimation();
        if (g_pendingWinTapTick != 0) {
            g_pendingWinTapTick = 0;
            FileLog(L"reveal cancel tap dropped");
        }
    }

    const int32_t axis = g_axisPub.load(std::memory_order_relaxed);
    const float finger = AxisScreenPosition(axis, x, y);
    // Decision quantity: how far the finger moved away from the taskbar edge
    // since the touch began. Signed by the opening direction and clamped at 0,
    // so a pull back toward the edge reads 0 and cannot commit.
    reveal.travel = std::max(
        0.0f, (finger - AxisScreenPosition(axis, reveal.edgeStartX,
                                           reveal.edgeStartY)) *
                  AxisOpeningSign(axis));
    reveal.maxTravel = std::max(reveal.maxTravel, reveal.travel);

    // The trail is published by the UI thread before the root opens. A zero
    // value means the renderer is between roots; the root extent keeps the
    // follow monotonic instead of dividing by a stale or missing edge.
    float panelTrail = g_panelTrailDip.load(std::memory_order_relaxed);
    if (!(panelTrail > 0.0f)) {
        panelTrail = reveal.distance;
    }
    reveal.panelTrail = panelTrail;

    // Absolute screen DIP on both sides of the subtraction: the panel's
    // trailing edge meets the finger, so the progress no longer depends on
    // where the finger was when the menu was first seen open.
    const float progress = std::clamp(
        1.0f - (finger - panelTrail) / (AxisDirection(axis) * reveal.distance),
        0.0f, 1.0f);

    reveal.progress = progress;
    reveal.samples++;
    if (std::fabs(progress - reveal.lastInserted) * reveal.distance >=
            kProgressEpsilonDip &&
        InsertProgress(progress)) {
        reveal.lastInserted = progress;
    }
}

// Release of the edge contact. A candidate that never saw the menu open ends
// silently: it did not touch Progress. A reveal that ran logs exactly one line.
//
// The decision mirrors ReleaseHidSession: it judges the travel at the moment of
// release, not the mapped progress. Progress reads 0.269 at the screen edge on
// this layout, so a progress threshold would commit a pull-out that never left
// the edge and a pull out and back alike.
void ReleaseRevealSession() {
    HidRevealSession& reveal = g_reveal;
    if (!reveal.edgeCandidate) {
        return;
    }

    reveal.edgeCandidate = false;
    if (!reveal.active) {
        return;
    }
    reveal.active = false;

    // The release arrives from the idle timer, so elapsed stops at the last
    // observed sample; adding the idle wait would dilute a quick flick.
    const uint64_t elapsed = reveal.lastTick > reveal.startTick
                                 ? reveal.lastTick - reveal.startTick
                                 : 0;
    const float travel = std::max(0.0f, reveal.travel);
    const float velocity =
        travel / static_cast<float>(elapsed > 0 ? elapsed : 1);
    const float distance = reveal.distance;
    const float progress = std::clamp(reveal.progress, 0.0f, 1.0f);
    const bool commit = distance > 0.0f &&
                        (travel > kCommitDistanceFraction * distance ||
                         velocity > kCommitVelocity);

    FileLog(L"open follow travel=%.0f max=%.0f progress=%.2f vel=%.2f "
            L"commit=%d panel=%.0f n=%d",
            travel, reveal.maxTravel, progress, velocity, commit ? 1 : 0,
            reveal.panelTrail, reveal.samples);

    if (commit) {
        // Let go past the commit point: the menu stays open and the follow
        // finishes the slide instead of snapping.
        if (!AnimateProgressTo(progress, 1.0f, kDismissSettleDuration,
                               ProgressEasing::Settle)) {
            ShowAtRest();
        }
        return;
    }

    // Short reveal: the panel slides back out of the screen edge first, over
    // 140 ms on an ease-in-out curve, so the motion leaves the finger position
    // gently and settles at the edge instead of starting at peak speed. The
    // Windows tap that actually closes the menu follows the animation on the
    // input timer; tapping here froze the panel at the finger position and the
    // window then hid without a retract.
    //
    // Progress stays at 0 afterwards and is not restored here: the tap takes a
    // moment to hide the window, and restoring now would flash the fully open
    // menu while the close is in flight. The flag makes the next down restore
    // the resting value once the shell has finished hiding the reused root.
    //
    // The suppression window starts at the release, not at the tap: the close
    // is already running, and a down inside the next 600 ms is a new reveal
    // attempt, not a drag of the retracting panel.
    const uint64_t now = GetTickCount64();
    AnimateProgressTo(progress, 0.0f, kRevealRetractDuration,
                      ProgressEasing::Retract);
    g_suppressRestoreUntil.store(now + kRestoreSuppressMs,
                                 std::memory_order_relaxed);
    g_pendingWinTapTick = now + kRevealCancelWinTapMs;
    g_commitNeedsProgressReset = true;
}

void LogHidRelease(const HidGestureSession& session, float velocity,
                   bool commit, uint64_t releaseTick) {
    const uint64_t releaseDelay =
        releaseTick > session.lastTick ? releaseTick - session.lastTick : 0;
    FileLog(
        L"hid release delta=%.1f final=%.1f perp=%.1f vel=%.2f commit=%d "
        L"n=%d relms=%llu",
        session.maxDelta, session.curDelta, session.maxPerp, velocity,
        commit ? 1 : 0, session.samples,
        static_cast<unsigned long long>(releaseDelay));
}

void BeginHidSession(float x, float y, uint64_t tickMs) {
    // The previous commit is still sliding out; its Windows-key tap has not
    // been sent yet, so this down belongs to the same menu.
    if (g_dismissSettling.load(std::memory_order_relaxed)) {
        return;
    }

    const float distance = g_distancePub.load(std::memory_order_relaxed);
    if (!(distance > 0.0f)) {
        return;
    }

    // The renderer must exist and the menu must be on screen right now. A
    // closed menu keeps its HWND and reports itself visible, so only the DWM
    // cloaked attribute answers this.
    if (!g_hasRoot.load(std::memory_order_relaxed) ||
        !StartWindowIsOpenThrottled(tickMs)) {
        return;
    }

    HidGestureSession& session = g_hidSession;
    session = HidGestureSession{};
    session.active = true;
    session.startX = x;
    session.startY = y;
    session.startTick = tickMs;
    session.lastTick = tickMs;
    session.lastProgress = 1.0f;
    session.samples = 1;

    // A cancel animation from the previous gesture may still be running;
    // stop it so this drag owns Progress. After a commit the base value can
    // still be 0, because the settle callback may not have run yet, so restore
    // the resting value before the first sample drives the scalar. The restore
    // is skipped while a close this mod started is still in flight: inserting 1
    // there flashed the fully open panel until the shell hid the window. The
    // flag stays set so the next open restores the scalar.
    StopProgressAnimation();
    if (g_commitNeedsProgressReset &&
        tickMs >= g_suppressRestoreUntil.load(std::memory_order_relaxed)) {
        g_commitNeedsProgressReset = false;
        InsertProgress(1.0f);
    }

    const int32_t axis = g_axisPub.load(std::memory_order_relaxed);
    FileLog(L"hid down x=%.1f y=%.1f axis=%d dist=%.0f", x, y, axis,
            distance);
}

void ReleaseHidSession(uint64_t tickMs, bool allowCommit) {
    HidGestureSession& session = g_hidSession;
    if (!session.active) {
        return;
    }

    // Measure the gesture up to the last observed sample. The release itself
    // arrives from an idle timer, so using it here would add the whole idle
    // wait to the elapsed time and dilute the velocity of a quick flick.
    const uint64_t lastSample = session.lastTick;
    const uint64_t elapsed =
        lastSample > session.startTick ? lastSample - session.startTick : 0;
    // Judge on where the finger actually let go, not on how far it once went:
    // dragging out and back in must cancel, not dismiss.
    const float finalDelta = std::max(0.0f, session.curDelta);
    const float velocity = finalDelta / static_cast<float>(elapsed > 0 ? elapsed : 1);
    const float distance = g_distancePub.load(std::memory_order_relaxed);
    const bool commit =
        allowCommit && distance > 0.0f &&
        (finalDelta > kCommitDistanceFraction * distance ||
         velocity > kCommitVelocity);

    LogHidRelease(session, velocity, commit, tickMs);

    if (commit) {
        const float from = std::clamp(session.lastProgress, 0.0f, 1.0f);
        if (BeginDismissSettle(from)) {
            g_commitNeedsProgressReset = true;
        } else {
            ShowAtRest();
        }
        session.active = false;
        return;
    }

    session.active = false;
    if (distance > 0.0f) {
        AnimateProgressTo(session.lastProgress, 1.0f,
                          kDismissCancelDuration, ProgressEasing::Settle);
    } else {
        ShowAtRest();
    }
}

// Scroll abort: the menu returns to its resting position at once; no
// animation and no dismissal.
void AbortHidSession(uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;
    if (!session.active) {
        return;
    }

    const uint64_t elapsed =
        tickMs > session.startTick ? tickMs - session.startTick : 0;
    const float velocity =
        session.maxDelta / static_cast<float>(elapsed > 0 ? elapsed : 1);

    LogHidRelease(session, velocity, false, tickMs);
    session.active = false;
    ShowAtRest();
}

void UpdateHidSession(float x, float y, uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;

    const float distance = g_distancePub.load(std::memory_order_relaxed);
    if (!(distance > 0.0f)) {
        AbortHidSession(tickMs);
        return;
    }

    const int32_t axis = g_axisPub.load(std::memory_order_relaxed);
    const float delta = AxisDelta(axis, session, x, y);
    const float perpendicular = PerpDelta(axis, session, x, y);

    session.lastTick = tickMs;
    session.curDelta = delta;
    session.maxDelta = std::max(session.maxDelta, delta);
    session.maxPerp = std::max(session.maxPerp, perpendicular);
    session.samples++;

    // A mostly perpendicular movement with little axis travel is a scroll of
    // the content, not a dismiss.
    if (session.maxPerp > kScrollAbortPerp &&
        session.maxPerp > kScrollAbortRatio * session.maxDelta &&
        session.maxDelta < kScrollAbortMaxDelta) {
        AbortHidSession(tickMs);
        return;
    }

    const float progress =
        std::clamp(1.0f - delta / distance, 0.0f, 1.0f);
    if ((delta > 0.0f || session.lastProgress < 1.0f) &&
        std::fabs(progress - session.lastProgress) * distance >=
            kProgressEpsilonDip) {
        InsertProgress(progress);
        session.lastProgress = progress;
    }
}

// One pure state-machine step, called on the input thread only. It never
// touches a XAML object; composition goes through the mutex-guarded helpers.
void OnHidSample(int contacts, float xDip, float yDip, uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;

    // Extra contacts (palm, ghost slot) are filtered when the report is
    // decoded; by the time a sample reaches here it always means "the tracked
    // finger is present".
    if (contacts <= 0) {
        if (session.active) {
            ReleaseHidSession(tickMs, true);
        }
        session.suppress = false;
        ReleaseRevealSession();
        g_contactDown = false;
        g_openCheckValid = false;
        return;
    }

    if (session.suppress) {
        return;
    }

    if (session.active) {
        UpdateHidSession(xDip, yDip, tickMs);
        return;
    }

    // The edge test belongs to the first report of the contact: a finger that
    // slides into the band after starting elsewhere is not a reveal candidate,
    // and a candidate that already exists owns the contact until it lifts.
    const bool newContact =
        !g_contactDown ||
        (tickMs > g_contactTick && tickMs - g_contactTick >= kIdleReleaseMs);
    g_contactDown = true;
    g_contactTick = tickMs;
    if (newContact) {
        BeginEdgeCandidate(xDip, yDip, tickMs);
    }
    if (g_reveal.edgeCandidate) {
        UpdateRevealSession(xDip, yDip, tickMs);
        return;
    }

    if (!PointInStartWindow(xDip, yDip)) {
        return;
    }
    BeginHidSession(xDip, yDip, tickMs);
}

// A device that leaves the surface without a lift report still ends the
// gesture; WM_TIMER only runs while no report is arriving.
void CheckHidIdleRelease(uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;

    if (session.active && tickMs > session.lastTick &&
        tickMs - session.lastTick >= kIdleReleaseMs) {
        ReleaseHidSession(tickMs, true);
    }

    if (g_reveal.edgeCandidate && tickMs > g_reveal.lastTick &&
        tickMs - g_reveal.lastTick >= kIdleReleaseMs) {
        ReleaseRevealSession();
    }

    // A contact that holds no session is not tracked per report, so the same
    // silence that ends a gesture ends the "this contact already started"
    // latch. Without it a lost lift would swallow the next edge swipe.
    if (!session.active && !g_reveal.edgeCandidate && g_contactDown &&
        tickMs > g_contactTick && tickMs - g_contactTick >= kIdleReleaseMs) {
        g_contactDown = false;
        g_openCheckValid = false;
    }
}

// The deferred tap of a cancelled reveal. It runs on the input thread so the
// tap follows the retract animation, and it opens the same suppression window
// the dismiss tap uses, so the poll cannot restore Progress while the shell
// hides the menu.
void RunPendingWinTap(uint64_t tickMs) {
    if (g_pendingWinTapTick == 0 || tickMs < g_pendingWinTapTick) {
        return;
    }

    g_pendingWinTapTick = 0;
    g_suppressRestoreUntil.store(tickMs + kRestoreSuppressMs,
                                 std::memory_order_relaxed);
    TapWindowsKey();
    FileLog(L"reveal cancel win sent");
}

// The XAML root is reused across menu opens, and PrepareRootElement does not
// run again, so a committed dismiss leaves Progress at 0. Without this the next
// open shows a window that is on screen but slid out of view.
void PollRootVisibility(uint64_t tickMs) {
    static uint64_t lastPollTick = 0;
    if (tickMs - lastPollTick < 100) {
        return;
    }
    lastPollTick = tickMs;

    const bool open = StartWindowIsOpen();
    // A close this mod started reports the window open until the shell has
    // processed the tap. Reading that as a reopen restored Progress 1 while the
    // close was still in flight, so a follow that began in that window flashed
    // the panel fully open and the window then vanished.
    const bool closeInFlight =
        tickMs < g_suppressRestoreUntil.load(std::memory_order_relaxed);
    // An edge contact that is still down owns the open: the follow drives the
    // scalar from the finger, and restoring 1 here would insert a fully open
    // frame between two follow samples.
    const bool gestureOwnsProgress = g_reveal.edgeCandidate;
    if (open && !g_rootWasOpen && g_commitNeedsProgressReset &&
        !closeInFlight && !gestureOwnsProgress) {
        g_commitNeedsProgressReset = false;
        ShowAtRest();
        FileLog(L"hid root restored after reopen");
    }
    g_rootWasOpen = open;
}

void FeedHidReport(HidDevice& device, USHORT reportId, const BYTE* report,
                   ULONG reportLength, uint64_t tickMs) {
    if (!device.ready || !device.preparsed) {
        LogDecodeFail(device, 0);
        return;
    }

    USHORT collections[kMaxContactCollections] = {};
    int collectionCount = 0;
    if (device.contactNodeCount > 0) {
        collectionCount = device.contactNodeCount;
        for (int i = 0; i < collectionCount; ++i) {
            collections[i] = device.contactNodes[i];
        }
    } else {
        collectionCount = 1;
        collections[0] = 0;
    }

    int nonZero = 0;
    int chosen = -1;
    long chosenX = 0;
    long chosenY = 0;
    long bestDistance = 0;

    // A precision touchscreen reports one collection per contact slot. Extra
    // contacts (palm, a ghost slot) must not cancel the gesture, so pick the
    // collection nearest the position the tracked finger last had instead of
    // just the first non-zero one.
    for (int i = 0; i < collectionCount; ++i) {
        ULONG x = 0;
        ULONG y = 0;
        const bool haveX = ReadUsageValue(device, kUsagePageGeneric,
                                          collections[i], kUsageX, report,
                                          reportLength, &x);
        const bool haveY = ReadUsageValue(device, kUsagePageGeneric,
                                          collections[i], kUsageY, report,
                                          reportLength, &y);
        if (!haveX || !haveY) {
            continue;
        }
        if (x == 0 && y == 0) {
            continue;
        }
        nonZero++;

        if (!g_hidSession.active) {
            if (chosen < 0) {
                chosen = i;
                chosenX = static_cast<long>(x);
                chosenY = static_cast<long>(y);
            }
            continue;
        }

        const long dx = static_cast<long>(x) - g_hidSession.lastRawX;
        const long dy = static_cast<long>(y) - g_hidSession.lastRawY;
        const long distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (chosen < 0 || distance < bestDistance) {
            chosen = i;
            chosenX = static_cast<long>(x);
            chosenY = static_cast<long>(y);
            bestDistance = distance;
        }
    }

    const bool primaryValid = chosen >= 0;
    const long primaryX = chosenX;
    const long primaryY = chosenY;

    ULONG topLevelCount = 0;
    bool haveCount = false;
    if (device.hasContactCount) {
        haveCount = ReadUsageValue(device, kUsagePageDigitizer,
                                   device.contactCountLink,
                                   kUsageContactCount, report, reportLength,
                                   &topLevelCount);
    }

    int contacts = 0;
    if (haveCount) {
        contacts = static_cast<int>(topLevelCount);
    } else {
        // No top-level Contact Count: contact buttons are authoritative,
        // otherwise count the collections that carry a position. A readable
        // but all-zero collection 0 is a lift, not a contact.
        ULONG tipSwitch = 0;
        ULONG inRange = 0;
        const bool haveTip =
            ReadUsageValue(device, kUsagePageDigitizer, collections[0],
                           kUsageTipSwitch, report, reportLength, &tipSwitch);
        const bool haveInRange =
            ReadUsageValue(device, kUsagePageDigitizer, collections[0],
                           kUsageInRange, report, reportLength, &inRange);
        if (haveInRange) {
            contacts = inRange ? 1 : 0;
        } else if (haveTip) {
            contacts = tipSwitch ? 1 : 0;
        } else {
            contacts = nonZero > 0 ? nonZero : 0;
        }
    }

    if (haveCount && contacts == 0) {
        // Contact Count is authoritative: on a lift report the position fields
        // often keep their last value, so trusting them would hide the lift and
        // force the idle timer to guess.
        OnHidSample(0, 0.0f, 0.0f, tickMs);
        return;
    }

    if (contacts <= 0) {
        // No Contact Count to trust: a report with no usable collection is a
        // lift, and a count above our tracked contact just means an extra
        // finger, which must not end the gesture.
        if (!primaryValid) {
            OnHidSample(0, 0.0f, 0.0f, tickMs);
            return;
        }
        contacts = 1;
    }

    if (!primaryValid) {
        LogDecodeFail(device, 0);
        return;
    }

    if (g_hidSession.active) {
        g_hidSession.lastRawX = primaryX;
        g_hidSession.lastRawY = primaryY;
    }

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if (screenWidth <= 1 || screenHeight <= 1) {
        LogDecodeFail(device, 0);
        return;
    }

    const HidAxisRange xRange = FindAxisRange(device, kUsageX, reportId);
    const HidAxisRange yRange = FindAxisRange(device, kUsageY, reportId);
    const float dpiScale = HidDpiScale();

    const int32_t pixelX = ToScreenPixel(primaryX, xRange, screenWidth);
    const int32_t pixelY = ToScreenPixel(primaryY, yRange, screenHeight);
    const float xDip = static_cast<float>(pixelX) / dpiScale;
    const float yDip = static_cast<float>(pixelY) / dpiScale;

    // The right and bottom edge bands are measured from the screen, which this
    // thread only knows in pixels; publish it in the same DIP space as xDip.
    g_screenDipWidth = static_cast<float>(screenWidth) / dpiScale;
    g_screenDipHeight = static_cast<float>(screenHeight) / dpiScale;

    // Diagnostic: does the digitizer see a touch that starts on the screen edge
    // before the menu is open? Off in the shipping build; it measured the edge
    // contact and EP's own open threshold.
    if (g_debugEdge && !g_hidSession.active && xDip < 200.0f &&
        tickMs > g_edgeLogTick + 100) {
        g_edgeLogTick = tickMs;
        FileLog(L"edge x=%.0f y=%.0f open=%d", xDip, yDip,
                StartWindowIsOpen() ? 1 : 0);
    }

    OnHidSample(contacts, xDip, yDip, tickMs);
}

void HandleHidInput(const RAWINPUT* raw) {
    const RAWHID& hid = raw->data.hid;
    if (hid.dwSizeHid == 0 || hid.dwCount == 0) {
        return;
    }

    HidDevice* device = GetHidDevice(raw->header.hDevice);
    if (!device) {
        return;
    }

    const uint64_t tickMs = GetTickCount64();
    for (UINT i = 0; i < hid.dwCount; ++i) {
        const BYTE* report =
            hid.bRawData + static_cast<size_t>(i) * hid.dwSizeHid;
        FeedHidReport(*device, static_cast<USHORT>(raw->header.wParam), report,
                      hid.dwSizeHid, tickMs);
    }
}

void HandleRawInput(HRAWINPUT rawHandle) {
    BYTE stackBuffer[512];
    UINT size = sizeof(stackBuffer);
    SetLastError(0);
    UINT read = GetRawInputData(rawHandle, RID_INPUT, stackBuffer, &size,
                                sizeof(RAWINPUTHEADER));
    BYTE* heapBuffer = nullptr;

    if (read == static_cast<UINT>(-1)) {
        if (size <= sizeof(stackBuffer) || size > 64 * 1024) {
            return;
        }
        heapBuffer = new (std::nothrow) BYTE[size];
        if (!heapBuffer) {
            return;
        }
        UINT heapSize = size;
        read = GetRawInputData(rawHandle, RID_INPUT, heapBuffer, &heapSize,
                               sizeof(RAWINPUTHEADER));
        if (read == static_cast<UINT>(-1)) {
            delete[] heapBuffer;
            return;
        }
    }

    const auto* raw = reinterpret_cast<const RAWINPUT*>(
        heapBuffer ? heapBuffer : stackBuffer);
    if (raw->header.dwType == RIM_TYPEHID) {
        HandleHidInput(raw);
    }

    delete[] heapBuffer;
}

LRESULT CALLBACK HidWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                               LPARAM lParam) {
    switch (msg) {
        case WM_INPUT:
            HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
            return 0;
        case WM_TIMER:
            if (wParam == kIdleTimerId) {
                const uint64_t now = GetTickCount64();
                // The tap runs before the poll so its suppression window is
                // already in force for this tick's open/closed answer.
                RunPendingWinTap(now);
                CheckHidIdleRelease(now);
                PollRootVisibility(now);
            } else if (wParam == kDebugTimerId) {
                PollDebugFile();
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void RegisterHidUsage(USAGE usage, HWND hwnd) {
    RAWINPUTDEVICE device{};
    device.usUsagePage = kUsagePageDigitizer;
    device.usUsage = usage;
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = hwnd;

    SetLastError(0);
    const BOOL ok = RegisterRawInputDevices(&device, 1, sizeof(device));
    const DWORD error = ok ? 0 : GetLastError();
    FileLog(L"hid reg usage=%u ok=%d err=%u", static_cast<unsigned>(usage),
            ok ? 1 : 0, static_cast<unsigned>(error));
}

void UnregisterHidUsage(USAGE usage) {
    RAWINPUTDEVICE device{};
    device.usUsagePage = kUsagePageDigitizer;
    device.usUsage = usage;
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = nullptr;
    RegisterRawInputDevices(&device, 1, sizeof(device));
}

DWORD WINAPI HidInputThread(LPVOID) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = HidWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kHidWindowClass;

    if (!RegisterClassExW(&windowClass)) {
        FileLog(L"hid thread fail err=%u",
                static_cast<unsigned>(GetLastError()));
        return 0;
    }

    HWND hwnd = CreateWindowExW(0, kHidWindowClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, windowClass.hInstance,
                                nullptr);
    if (!hwnd) {
        FileLog(L"hid thread fail err=%u",
                static_cast<unsigned>(GetLastError()));
        UnregisterClassW(kHidWindowClass, windowClass.hInstance);
        return 0;
    }

    g_hidHwnd.store(hwnd, std::memory_order_relaxed);

    RegisterHidUsage(kUsageTouchScreen, hwnd);
    RegisterHidUsage(kUsagePen, hwnd);
    SetTimer(hwnd, kIdleTimerId, kIdleTimerMs, nullptr);
    SetTimer(hwnd, kDebugTimerId, kDebugTimerMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd, kIdleTimerId);
    KillTimer(hwnd, kDebugTimerId);
    UnregisterHidUsage(kUsageTouchScreen);
    UnregisterHidUsage(kUsagePen);

    g_hidSession = HidGestureSession{};
    g_reveal = HidRevealSession{};
    g_contactDown = false;
    g_openCheckValid = false;
    g_pendingWinTapTick = 0;
    g_debugFileStamp = DebugFileStamp{};
    FreeHidDevices();

    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    g_hidHwnd.store(nullptr, std::memory_order_relaxed);
    UnregisterClassW(kHidWindowClass, windowClass.hInstance);
    return 0;
}

// ---------------------------------------------------------------------------
// UI-thread state publication
// ---------------------------------------------------------------------------

float DpiScaleForWindow(HWND hwnd) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

// Publishes the plain values the input thread needs. Runs on the UI thread.
void PublishGestureGeometry() {
    g_axisPub.store(g_dismissAxis, std::memory_order_relaxed);
    g_distancePub.store(g_dismissDistance, std::memory_order_relaxed);

    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        return;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return;
    }

    const float scale = DpiScaleForWindow(hwnd);
    g_winRectDip[0].store(
        static_cast<int64_t>(std::llround(rect.left / scale)),
        std::memory_order_relaxed);
    g_winRectDip[1].store(
        static_cast<int64_t>(std::llround(rect.top / scale)),
        std::memory_order_relaxed);
    g_winRectDip[2].store(
        static_cast<int64_t>(std::llround(rect.right / scale)),
        std::memory_order_relaxed);
    g_winRectDip[3].store(
        static_cast<int64_t>(std::llround(rect.bottom / scale)),
        std::memory_order_relaxed);
}

// Maps the panel's trailing edge into absolute screen DIP. The visible panel is
// the first visible child of the root with a width (StartSizingFrame); the rest
// of the root canvas is transparent and must not be part of the reveal travel.
// The edge that faces the taskbar is the right edge on a left taskbar, the
// bottom on top, the left on right and the top on bottom. Runs on the UI thread
// and returns false while that child is not laid out yet.
bool ComputePanelTrailDip(float& trail) {
    wux::FrameworkElement root = g_root;
    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!root || !hwnd) {
        return false;
    }

    float frameLeft = 0.0f;
    float frameTop = 0.0f;
    float frameRight = 0.0f;
    float frameBottom = 0.0f;
    bool measured = false;

    const int32_t childCount =
        wux::Media::VisualTreeHelper::GetChildrenCount(root);
    for (int32_t i = 0; i < childCount; ++i) {
        wux::FrameworkElement child =
            wux::Media::VisualTreeHelper::GetChild(root, i)
                .try_as<wux::FrameworkElement>();
        if (!child) {
            continue;
        }
        const float width = static_cast<float>(child.ActualWidth());
        if (!(width > 0.0f) ||
            child.Visibility() != wux::Visibility::Visible) {
            continue;
        }

        // The transform is relative to the window's XAML root; null asks for
        // that root. A scaling or offset transform stays in the mapping.
        wux::Media::GeneralTransform transform = child.TransformToVisual(nullptr);
        const winrt::Windows::Foundation::Point topLeft =
            transform.TransformPoint({0.0f, 0.0f});
        frameLeft = topLeft.X;
        frameTop = topLeft.Y;
        frameRight = transform.TransformPoint({width, 0.0f}).X;
        frameBottom =
            transform
                .TransformPoint(
                    {0.0f, static_cast<float>(child.ActualHeight())})
                .Y;
        measured = true;
        break;
    }

    if (!measured) {
        return false;
    }

    // The Start CoreWindow spans the whole screen, but its XAML island is laid
    // out in the work area: the mapped frame comes out 48 DIP left of where the
    // pixels are when the taskbar is on the left. GetWindowRect returns the
    // full-screen rect, so the work area origin is the offset the XAML
    // coordinates are measured from. Measured: frame right 1005 DIP + work
    // left 48 DIP = 1053 DIP, the panel's border at 2105 physical px.
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return false;
    }
    const float scale = DpiScaleForWindow(hwnd);
    const float originX = monitorInfo.rcWork.left / scale;
    const float originY = monitorInfo.rcWork.top / scale;

    switch (static_cast<DismissAxis>(g_dismissAxis)) {
        case DismissAxis::Left:
            trail = originX + frameRight;
            break;
        case DismissAxis::Top:
            trail = originY + frameBottom;
            break;
        case DismissAxis::Right:
            trail = originX + frameLeft;
            break;
        case DismissAxis::Bottom:
            trail = originY + frameTop;
            break;
    }

    return true;
}

// Publishes the panel trail for the reveal follow. Falls back to the root
// extent, which reproduces the earlier root-width travel, and says so once.
void PublishPanelTrailDip() {
    const float distance = g_dismissDistance;
    if (!(distance > 0.0f)) {
        return;
    }

    float trail = 0.0f;
    bool computed = false;
    try {
        computed = ComputePanelTrailDip(trail);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Panel trail mapping failed: %08X", e.code());
    } catch (...) {
        // A tree mid-teardown must not break Setup or SizeChanged.
    }

    if (!computed) {
        trail = distance;
    }
    g_panelTrailDip.store(trail, std::memory_order_relaxed);

    if (!computed) {
        if (!g_panelTrailFallbackLogged) {
            g_panelTrailFallbackLogged = true;
            FileLog(L"reveal panelTrail=%.0f (fallback)", trail);
        }
        return;
    }

    if (!g_panelTrailLogged) {
        g_panelTrailLogged = true;
        FileLog(L"reveal panelTrail=%.0f", trail);
    }
}

// The HWND is needed for the DWM cloaked query that decides whether the menu
// is open, and for the window bounds the input thread tests against.
void CaptureCoreWindowHandle() {
    g_coreHwnd.store(nullptr, std::memory_order_relaxed);

    try {
        wucore::CoreWindow coreWindow =
            wucore::CoreWindow::GetForCurrentThread();
        if (!coreWindow) {
            return;
        }

        void* windowAbi = winrt::get_abi(coreWindow);
        if (!windowAbi) {
            return;
        }

        void* interop = nullptr;
        if (FAILED(static_cast<IUnknown*>(windowAbi)->QueryInterface(
                kCoreWindowInteropIid, &interop)) ||
            !interop) {
            return;
        }

        HWND hwnd = nullptr;
        auto getWindowHandle = reinterpret_cast<GetCoreWindowHandleAbi>(
            (*reinterpret_cast<void***>(interop))[3]);
        if (getWindowHandle) {
            getWindowHandle(interop, &hwnd);
        }
        static_cast<IUnknown*>(interop)->Release();
        g_coreHwnd.store(hwnd, std::memory_order_relaxed);
    } catch (...) {
        // No HWND means no cloaked query, so no gesture starts.
    }
}

// Chooses the dismiss direction from the side of the work area the Start
// window is anchored to. Checked on every setup because the window can move
// between displays.
void UpdateDismissAxis() {
    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        return;
    }

    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) {
        return;
    }

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return;
    }

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }

    const RECT& work = info.rcWork;
    DismissAxis axis = DismissAxis::Left;
    if (windowRect.left <= work.left + 4) {
        axis = DismissAxis::Left;
    } else if (windowRect.top <= work.top + 4) {
        axis = DismissAxis::Top;
    } else if (windowRect.right >= work.right - 4) {
        axis = DismissAxis::Right;
    } else if (windowRect.bottom >= work.bottom - 4) {
        axis = DismissAxis::Bottom;
    }

    g_dismissAxis = static_cast<int32_t>(axis);
    FileLog(L"dismiss axis=%d (0=left,1=top,2=right,3=bottom) "
            L"win=%d,%d,%d,%d work=%d,%d,%d,%d",
            g_dismissAxis, static_cast<int>(windowRect.left),
            static_cast<int>(windowRect.top),
            static_cast<int>(windowRect.right),
            static_cast<int>(windowRect.bottom), static_cast<int>(work.left),
            static_cast<int>(work.top), static_cast<int>(work.right),
            static_cast<int>(work.bottom));

    PublishGestureGeometry();
}

// Runs on the UI thread, after the root element is loaded.
bool Setup(wux::FrameworkElement const& root) {
    if (!root) {
        return false;
    }

    {
        std::lock_guard lock(g_compositionMutex);
        if (g_state) {
            return true;
        }
    }

    g_uiThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
    g_root = root;
    g_dispatcher = root.Dispatcher();
    g_panelTrailLogged = false;  // one reveal trail line per open

    CaptureCoreWindowHandle();
    if (!g_coreHwnd.load(std::memory_order_relaxed)) {
        Wh_Log(L"No CoreWindow HWND; gestures stay disabled");
    }
    UpdateDismissAxis();

    wuxh::ElementCompositionPreview::SetIsTranslationEnabled(root, true);

    wuc::Visual visual = wuxh::ElementCompositionPreview::GetElementVisual(root);
    wuc::Compositor compositor = visual.Compositor();

    const float width = static_cast<float>(root.ActualWidth());
    const float height = static_cast<float>(root.ActualHeight());

    float dirX = -1.0f;
    float dirY = 0.0f;
    float distance = width;
    switch (static_cast<DismissAxis>(g_dismissAxis)) {
        case DismissAxis::Left:
            dirX = -1.0f;
            dirY = 0.0f;
            distance = width;
            break;
        case DismissAxis::Top:
            dirX = 0.0f;
            dirY = -1.0f;
            distance = height;
            break;
        case DismissAxis::Right:
            dirX = 1.0f;
            dirY = 0.0f;
            distance = width;
            break;
        case DismissAxis::Bottom:
            dirX = 0.0f;
            dirY = 1.0f;
            distance = height;
            break;
    }
    g_dismissDistance = distance;

    wuc::CompositionPropertySet state = compositor.CreatePropertySet();
    state.InsertScalar(L"Progress", 1.0f);  // Normal Start is visible.
    state.InsertScalar(L"Distance", distance);
    state.InsertScalar(L"DirX", dirX);
    state.InsertScalar(L"DirY", dirY);

    // Easing curves for every Progress animation. Without them a keyframe pair
    // is linear and starts at its peak velocity: a short retract then reads as
    // the panel flying away on the first frame.
    //   settle  (0.2, 0.85, 0.3, 1)   ease-out: committed dismiss and both
    //                                 return-to-rest slides land slowly.
    //   retract (0.42, 0, 0.25, 1)    ease-in-out: cancelled reveal leaves the
    //                                 finger position gently and settles.
    const wuc::CompositionEasingFunction settleEasing =
        compositor.CreateCubicBezierEasingFunction(float2{0.2f, 0.85f},
                                                   float2{0.3f, 1.0f});
    const wuc::CompositionEasingFunction retractEasing =
        compositor.CreateCubicBezierEasingFunction(float2{0.42f, 0.0f},
                                                   float2{0.25f, 1.0f});

    // The translation property must exist before the expression is started.
    visual.Properties().InsertVector3(L"Translation",
                                      float3{0.0f, 0.0f, 0.0f});

    wuc::ExpressionAnimation translation = compositor.CreateExpressionAnimation(
        L"Vector3((1 - state.Progress) * state.DirX * state.Distance, "
        L"(1 - state.Progress) * state.DirY * state.Distance, 0)");
    translation.SetReferenceParameter(L"state", state);
    visual.StartAnimation(L"Translation", translation);

    {
        std::lock_guard lock(g_compositionMutex);
        g_compositor = compositor;
        g_visual = visual;
        g_state = state;
        g_translationExpression = translation;
        g_settleEasing = settleEasing;
        g_retractEasing = retractEasing;
    }

    g_sizeToken = root.SizeChanged(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           wux::SizeChangedEventArgs const&) {
            try {
                wux::FrameworkElement fe =
                    sender.try_as<wux::FrameworkElement>();
                if (!fe) {
                    return;
                }

                const float width = static_cast<float>(fe.ActualWidth());
                const float height = static_cast<float>(fe.ActualHeight());
                const float distance =
                    IsHorizontalAxis(g_dismissAxis) ? width : height;
                g_dismissDistance = distance;

                {
                    std::lock_guard lock(g_compositionMutex);
                    if (g_state) {
                        g_state.InsertScalar(L"Distance", distance);
                    }
                }

                PublishGestureGeometry();
                // The root may have been resized by the panel layout, so the
                // trail is remapped with the new extent in force.
                PublishPanelTrailDip();
            } catch (winrt::hresult_error const& e) {
                Wh_Log(L"SizeChanged update failed: %08X", e.code());
            } catch (...) {
                // Never let an exception escape into XAML.
            }
        });

    // Everything the input thread reads is published before the root opens.
    PublishGestureGeometry();
    PublishPanelTrailDip();
    g_hasRoot.store(true, std::memory_order_release);

    const winrt::hstring rootClassName = winrt::get_class_name(root);
    FileLog(L"attached root=%s size=%.0fx%.0f", rootClassName.c_str(), width,
            height);

    Wh_Log(L"Dismiss input armed; composition translation enabled "
           L"root=%s (distance=%.1f)",
           rootClassName.c_str(), distance);
    return true;
}

// ---------------------------------------------------------------------------
// Hook: StartUI::App::PrepareRootElement
// ---------------------------------------------------------------------------

using PrepareRootElement_t = void* (*)(void*, void*, bool);
PrepareRootElement_t PrepareRootElement_Original = nullptr;

void* PrepareRootElement_Hook(void* frame, void* perFrameData, bool flag) {
    void* result = PrepareRootElement_Original
                       ? PrepareRootElement_Original(frame, perFrameData, flag)
                       : nullptr;
    if (!result || g_unloading.load(std::memory_order_relaxed)) {
        return result;
    }

    try {
        wux::UIElement element{nullptr};
        winrt::copy_from_abi(element, result);  // Adds a reference.

        wux::FrameworkElement root = element.try_as<wux::FrameworkElement>();
        if (!root) {
            Wh_Log(L"PrepareRootElement returned a non-FrameworkElement root");
            return result;
        }

        Wh_Log(L"PrepareRootElement root class=%s name=%s",
               winrt::get_class_name(root).c_str(), root.Name().c_str());

        // A new open: drop the previous root's property set, animation and
        // gesture state before installing the fresh renderer.
        ResetRenderer();

        bool alreadyLoaded = false;
        try {
            alreadyLoaded = root.IsLoaded();
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"IsLoaded check failed: %08X", e.code());
        }

        if (alreadyLoaded) {
            Setup(root);
        } else {
            const winrt::hstring rootClassName = winrt::get_class_name(root);
            FileLog(L"root pending load class=%s", rootClassName.c_str());

            const uint32_t generation =
                g_setupGeneration.load(std::memory_order_relaxed);
            auto weak = winrt::make_weak(root);
            g_loadedToken = root.Loaded(
                [weak, generation](
                    winrt::Windows::Foundation::IInspectable const& sender,
                    wux::RoutedEventArgs const&) {
                    try {
                        if (generation != g_setupGeneration.load(
                                              std::memory_order_relaxed)) {
                            return;
                        }
                        wux::FrameworkElement current =
                            sender.try_as<wux::FrameworkElement>();
                        if (!current) {
                            current = weak.get();
                        }
                        if (current) {
                            Setup(current);
                        }
                    } catch (winrt::hresult_error const& e) {
                        Wh_Log(L"Loaded setup failed: %08X %s", e.code(),
                               e.message().c_str());
                    } catch (...) {
                        Wh_Log(L"Loaded setup failed with an unknown exception");
                    }
                });
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"PrepareRootElement hook failed: %08X %s", e.code(),
               e.message().c_str());
    } catch (...) {
        Wh_Log(L"PrepareRootElement hook failed with an unknown exception");
    }

    return result;
}

// ---------------------------------------------------------------------------
// Hook: StartUI::SplitViewFrame::OnGlobalAnimationRequested
//
// The shell raises this request to start the XAML open and close storyboards.
// Dropping the open request leaves the pane at its final layout position, so
// the composition `Progress` expression is the only remaining motion. The
// close request (type 1) always reaches the original.
// ---------------------------------------------------------------------------

using OnGlobalAnimationRequested_t = void (*)(void*, void*, void*);
OnGlobalAnimationRequested_t OnGlobalAnimationRequested_Original = nullptr;

// The args object exposes the three ints through vtable slots 6, 7 and 8; the
// wrappers at the RVAs above dispatch exactly those slots and return the value
// in eax. Null until the layout check in InstallAnimHook succeeds.
using AnimArgsIntGetter_t = int (*)(void*);
AnimArgsIntGetter_t g_animTypeGetter = nullptr;
AnimArgsIntGetter_t g_animEdgeGetter = nullptr;
AnimArgsIntGetter_t g_animTransitionGetter = nullptr;

void OnGlobalAnimationRequested_Hook(void* self, void* sender, void* args) {
    int type = -1;
    int edge = -1;
    int transition = -1;
    if (args && g_animTypeGetter && g_animEdgeGetter &&
        g_animTransitionGetter) {
        type = g_animTypeGetter(args);
        edge = g_animEdgeGetter(args);
        transition = g_animTransitionGetter(args);
    }

    // Type 0 is the open request. Any other value, and any unreadable type,
    // keeps the shell's own behaviour.
    const bool suppress =
        type == 0 && g_suppressOpenAnim.load(std::memory_order_relaxed);

    if (g_animLogCount.fetch_add(1, std::memory_order_relaxed) < 4) {
        FileLog(L"startui anim type=%d edge=%d trans=%d suppress=%d", type,
                edge, transition, suppress ? 1 : 0);
    }

    if (suppress) {
        return;
    }

    if (OnGlobalAnimationRequested_Original) {
        OnGlobalAnimationRequested_Original(self, sender, args);
    }
}

// ---------------------------------------------------------------------------
// Hook installation
// ---------------------------------------------------------------------------

std::atomic<bool> g_startUIHooked{false};
std::atomic<bool> g_animHookAttempted{false};
std::atomic<bool> g_afterInit{false};
std::atomic<bool> g_stopPolling{false};
HANDLE g_pollThread = nullptr;

void InstallStartUIHooks(HMODULE module) {
    if (!module || g_startUIHooked.exchange(true)) {
        return;
    }
    if (g_disabled.load(std::memory_order_relaxed)) {
        g_startUIHooked.store(false);
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    void* target = reinterpret_cast<void*>(
        base + (kPrepareRootElementVa - kPreferredBase));

    if (!Wh_SetFunctionHook(target,
                            reinterpret_cast<void*>(PrepareRootElement_Hook),
                            reinterpret_cast<void**>(
                                &PrepareRootElement_Original))) {
        Wh_Log(L"Failed to hook StartUI::App::PrepareRootElement at %p",
               target);
        g_startUIHooked.store(false);
        return;
    }

    Wh_Log(L"Hooked StartUI::App::PrepareRootElement at %p", target);
}

// Confirms that the target is the function the hook was written for: the exact
// prologue, the three int getters called in order, and the argument registers
// those calls imply. A different StartUI_ build fails here and the hook is left
// out instead of being installed on a guessed ABI.
bool AnimHookAbiMatches(uintptr_t base) {
    const auto* code = reinterpret_cast<const uint8_t*>(
        base + (kOnGlobalAnimationRequestedVa - kPreferredBase));
    constexpr size_t kCookieDispBegin = 0x12;
    constexpr size_t kCookieDispEnd = 0x16;

    for (size_t i = 0; i < sizeof(kAnimPrologue); ++i) {
        if (i >= kCookieDispBegin && i < kCookieDispEnd) {
            continue;
        }
        if (code[i] != kAnimPrologue[i]) {
            return false;
        }
    }

    const uintptr_t getters[] = {
        base + (kAnimTypeGetterVa - kPreferredBase),
        base + (kAnimEdgeGetterVa - kPreferredBase),
        base + (kAnimTransitionGetterVa - kPreferredBase),
    };
    // The three calls are separated by their result stores, so they are not
    // adjacent: call type, store; call edge, store; call transition, store.
    constexpr size_t kGetterCallOffsets[] = {0x27, 0x33, 0x3F};
    const uintptr_t function = reinterpret_cast<uintptr_t>(code);
    for (size_t i = 0; i < ARRAYSIZE(getters); ++i) {
        const size_t offset = kGetterCallOffsets[i];
        if (code[offset] != 0xE8) {
            return false;
        }
        int32_t displacement = 0;
        std::memcpy(&displacement, code + offset + 1, sizeof(displacement));
        if (function + offset + 5 + displacement != getters[i]) {
            return false;
        }
    }

    return true;
}

bool InstallAnimHook(HMODULE module) {
    if (!module || g_animHookAttempted.exchange(true)) {
        return false;
    }
    if (g_disabled.load(std::memory_order_relaxed)) {
        g_animHookAttempted.store(false);
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    void* target = reinterpret_cast<void*>(
        base + (kOnGlobalAnimationRequestedVa - kPreferredBase));

    if (!AnimHookAbiMatches(base)) {
        Wh_Log(L"StartUI_.dll does not match the expected "
               L"OnGlobalAnimationRequested layout");
        FileLog(L"startui anim hook skipped");
        return false;
    }

    const AnimArgsIntGetter_t typeGetter = reinterpret_cast<AnimArgsIntGetter_t>(
        base + (kAnimTypeGetterVa - kPreferredBase));
    const AnimArgsIntGetter_t edgeGetter = reinterpret_cast<AnimArgsIntGetter_t>(
        base + (kAnimEdgeGetterVa - kPreferredBase));
    const AnimArgsIntGetter_t transitionGetter =
        reinterpret_cast<AnimArgsIntGetter_t>(
            base + (kAnimTransitionGetterVa - kPreferredBase));

    if (!Wh_SetFunctionHook(
            target, reinterpret_cast<void*>(OnGlobalAnimationRequested_Hook),
            reinterpret_cast<void**>(&OnGlobalAnimationRequested_Original))) {
        Wh_Log(L"Failed to hook "
               L"StartUI::SplitViewFrame::OnGlobalAnimationRequested at %p",
               target);
        FileLog(L"startui anim hook skipped");
        return false;
    }

    g_animTypeGetter = typeGetter;
    g_animEdgeGetter = edgeGetter;
    g_animTransitionGetter = transitionGetter;

    Wh_Log(L"Hooked StartUI::SplitViewFrame::OnGlobalAnimationRequested at %p",
           target);
    FileLog(L"startui anim hooked");
    return true;
}

void TryInstallFromLoadedModules(bool apply) {
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }

    bool installed = false;
    HMODULE startUI = GetModuleHandleW(L"StartUI_.dll");
    if (startUI) {
        if (!g_startUIHooked.load(std::memory_order_relaxed)) {
            InstallStartUIHooks(startUI);
            installed = true;
        }
        if (!g_animHookAttempted.load(std::memory_order_relaxed)) {
            if (InstallAnimHook(startUI)) {
                installed = true;
            }
        }
    }

    if (installed && apply && g_afterInit.load(std::memory_order_relaxed)) {
        Wh_ApplyHookOperations();
    }
}

// ---------------------------------------------------------------------------
// Late load of StartUI_.dll
// ---------------------------------------------------------------------------

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original = nullptr;

HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR fileName, HANDLE file,
                                   DWORD flags) {
    HMODULE module = LoadLibraryExW_Original
                         ? LoadLibraryExW_Original(fileName, file, flags)
                         : nullptr;

    if (module && !g_startUIHooked.load(std::memory_order_relaxed)) {
        if (module == GetModuleHandleW(L"StartUI_.dll")) {
            TryInstallFromLoadedModules(true);
        }
    }

    return module;
}

DWORD WINAPI PollModulesThread(LPVOID) {
    for (int i = 0; i < 400 && !g_stopPolling.load(std::memory_order_relaxed);
         i++) {
        TryInstallFromLoadedModules(true);
        if (g_startUIHooked.load(std::memory_order_relaxed)) {
            break;
        }
        Sleep(50);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------

// Runs on the UI thread.
void RevokeHandlers() {
    wux::FrameworkElement root = g_root;
    if (!root) {
        return;
    }

    try {
        if (g_sizeToken.value) {
            root.SizeChanged(g_sizeToken);
            g_sizeToken = {};
        }
        if (g_loadedToken.value) {
            root.Loaded(g_loadedToken);
            g_loadedToken = {};
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Failed to revoke XAML handlers: %08X %s", e.code(),
               e.message().c_str());
    } catch (...) {
        Wh_Log(L"Failed to revoke XAML handlers with an unknown exception");
    }

    // Release the XAML reference on the UI thread.
    g_root = nullptr;
}

void RevokeHandlersOnUiThread() {
    if (!g_root) {
        return;
    }

    if (g_uiThreadId.load(std::memory_order_relaxed) ==
        GetCurrentThreadId()) {
        RevokeHandlers();
        return;
    }

    wucore::CoreDispatcher dispatcher = g_dispatcher;
    if (!dispatcher) {
        Wh_Log(L"No CoreDispatcher captured; XAML handlers left attached");
        return;
    }

    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!done) {
        return;
    }

    try {
        dispatcher.RunAsync(wucore::CoreDispatcherPriority::Normal,
                            [done]() {
                                try {
                                    RevokeHandlers();
                                } catch (...) {
                                    // Never let an exception escape the
                                    // dispatcher callback.
                                }
                                SetEvent(done);
                            });
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Dispatcher.RunAsync failed: %08X %s", e.code(),
               e.message().c_str());
        CloseHandle(done);
        return;
    } catch (...) {
        CloseHandle(done);
        return;
    }

    if (WaitForSingleObject(done, 2000) == WAIT_OBJECT_0) {
        CloseHandle(done);
    } else {
        // The queued callback may still signal the event; leaking one handle
        // is safer than closing a handle it will use later.
        Wh_Log(L"Timed out waiting for the UI thread to revoke XAML handlers");
    }
}

void ResetComposition() {
    std::lock_guard lock(g_compositionMutex);

    // Invalidate a pending dismiss completion before stopping its animation,
    // so an old batch cannot tap the Windows key into a newly opened menu.
    g_dismissSerial.fetch_add(1, std::memory_order_relaxed);
    g_dismissSettling.store(false, std::memory_order_relaxed);

    try {
        if (g_state) {
            g_state.StopAnimation(L"Progress");
            g_state.InsertScalar(L"Progress", 1.0f);
        }
        if (g_visual) {
            g_visual.StopAnimation(L"Translation");
            g_visual.Properties().InsertVector3(
                L"Translation", float3{0.0f, 0.0f, 0.0f});
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Composition cleanup failed: %08X", e.code());
    } catch (...) {
    }

    g_dismissBatch = nullptr;
    g_translationExpression = nullptr;
    g_compositor = nullptr;
    g_state = nullptr;
    g_visual = nullptr;
    g_settleEasing = nullptr;
    g_retractEasing = nullptr;

    g_distancePub.store(0.0f, std::memory_order_relaxed);
    g_panelTrailDip.store(0.0f, std::memory_order_relaxed);

    // Invalidate a stale Loaded handler from the previous root.
    g_setupGeneration.fetch_add(1, std::memory_order_relaxed);
}

// Drops the previous root's renderer before a new open installs its own
// property set, animation and input state. Runs on the UI thread.
void ResetRenderer() {
    RevokeHandlers();
    ResetComposition();
}

}  // namespace

BOOL Wh_ModInit() {
    FileLog(L"mod init");
    Wh_Log(L"Legacy Start Edge Swipe 0.6.4 initializing");

    // The open/closed signal comes from DWM. Resolve it at runtime so a build
    // without the dwmapi import library still links and loads.
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (dwmapi) {
        g_dwmGetWindowAttribute.store(
            reinterpret_cast<DwmGetWindowAttribute_t>(
                GetProcAddress(dwmapi, "DwmGetWindowAttribute")),
            std::memory_order_relaxed);
    }
    if (!g_dwmGetWindowAttribute.load(std::memory_order_relaxed)) {
        Wh_Log(L"DwmGetWindowAttribute not resolved; gestures stay disabled");
    }

    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibraryExW = kernelBase
                              ? reinterpret_cast<LoadLibraryExW_t>(
                                    GetProcAddress(kernelBase,
                                                   "LoadLibraryExW"))
                              : nullptr;
    if (!loadLibraryExW) {
        Wh_Log(L"LoadLibraryExW not found; relying on polling");
    } else if (!Wh_SetFunctionHook(
                   reinterpret_cast<void*>(loadLibraryExW),
                   reinterpret_cast<void*>(LoadLibraryExW_Hook),
                   reinterpret_cast<void**>(&LoadLibraryExW_Original))) {
        Wh_Log(L"Failed to hook LoadLibraryExW; relying on polling");
    }

    TryInstallFromLoadedModules(false);

    g_pollThread =
        CreateThread(nullptr, 0, PollModulesThread, nullptr, 0, nullptr);
    if (!g_pollThread) {
        Wh_Log(L"Failed to create the module polling thread");
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    g_afterInit = true;
    TryInstallFromLoadedModules(false);
    Wh_ApplyHookOperations();

    FileLog(L"hid init");
    g_hidThread =
        CreateThread(nullptr, 0, HidInputThread, nullptr, 0, nullptr);
    if (!g_hidThread) {
        FileLog(L"hid thread fail err=%u",
                static_cast<unsigned>(GetLastError()));
    }
}

void Wh_ModBeforeUninit() {
    // Stop hook registration and code that can run on its own thread before
    // Windhawk removes the hooks (Wh_SetFunctionHook/Wh_ApplyHookOperations
    // must not be called after this function returns).
    g_disabled = true;
    g_unloading = true;
    g_stopPolling = true;

    // Stop the asynchronous dismiss-settle callback from touching state once
    // the module starts to unload.
    g_stopDismissCallback = true;

    if (g_pollThread) {
        WaitForSingleObject(g_pollThread, 2000);
        CloseHandle(g_pollThread);
        g_pollThread = nullptr;
    }

    // The message-only window and its raw input registration belong to the
    // input thread. Ask it to leave the message loop and wait for it before
    // the module's code can be unmapped.
    HWND hwnd = g_hidHwnd.load(std::memory_order_relaxed);
    if (hwnd) {
        PostMessageW(hwnd, WM_QUIT, 0, 0);
    }
    if (g_hidThread) {
        const DWORD waitResult = WaitForSingleObject(g_hidThread, 2000);
        CloseHandle(g_hidThread);
        g_hidThread = nullptr;
        if (waitResult != WAIT_OBJECT_0) {
            Wh_Log(L"Timed out waiting for the raw input thread");
        }
    }
}

void Wh_ModUninit() {
    RevokeHandlersOnUiThread();
    ResetComposition();
    Wh_Log(L"Legacy Start Edge Swipe Dismiss uninitialized");
}
