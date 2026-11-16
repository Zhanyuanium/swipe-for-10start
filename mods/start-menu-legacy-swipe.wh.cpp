// ==WindhawkMod==
// @id              start-menu-legacy-swipe
// @name            Legacy Start Edge Swipe Dismiss
// @description     Touch/pen swipe toward the anchored edge to dismiss the legacy Start menu
// @version         0.1.0
// @author          doubao
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -I"C:\Users\doubao\source\repos\swipe-for-10start\mods"
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Legacy Start edge swipe to dismiss (touch/pen)

StartMenuExperienceHost-side companion for ExplorerPatcher's legacy Windows 10
Start menu. It adds an in-menu swipe towards the anchored edge to dismiss the
menu; it does not draw the slide-in reveal.

What it does:

- Hooks `StartUI::App::PrepareRootElement` (`StartUI_.dll`, RVA `0xAD730`),
  captures the returned root after it loads, and enables Composition
  translation.
- At every setup it gets the Start CoreWindow HWND via `ICoreWindowInterop`,
  compares `GetWindowRect` with the monitor work area and picks the dismiss
  direction: left if the window is anchored to the work-area left, then top,
  then right, then bottom (a full-height left flyout resolves to left). It
  logs `dismiss axis=...` once.
- Installs a `CompositionPropertySet` with `Progress` (animatable),
  `Distance` (static window width or height), `DirX` and `DirY`, plus the
  expression
  `Vector3((1 - state.Progress) * state.DirX * state.Distance, (1 - state.Progress) * state.DirY * state.Distance, 0)`.
- Attaches `PointerPressed`, `PointerMoved`, `PointerReleased` and
  `PointerCaptureLost` to the root with `handledEventsToo`.
- If the XAML build matches, the first root attach installs a hook on
  `DManipDataBase::SetContentOffsetsHelper` (`Windows.UI.Xaml.dll` RVA
  `0x4DE388`). It runs on the XAML UI thread and sees the menu's own content
  offset updates; it records only while a press session is active. The first
  value is the baseline, then the largest positive and negative deviation
  along the dismiss axis (X for left/right, Y for top/bottom) is tracked.
  No per-call logging, no pointer capture.
- On release it logs `dmhook n=... maxPos=... maxNeg=... base=(...) el=...`
  and uses `m = max(maxPos, -maxNeg)`: 25% of `Distance` or 0.5 DIP/ms
  commits. Commit sets `Progress = clamp(1 - m / Distance, 0, 1)` and runs the
  150 ms animate-to-0 + `VK_ESCAPE` + reset path. Otherwise `Progress` resets
  to 1 without animation and the ScrollViewer snaps back naturally.
- Without the hook (version mismatch or a failed install), the pointer
  fallback remains: a touch/pen press records the start point and innermost
  `ScrollViewer`; the drag takes over after 8 DIP of axis-dominant movement
  when the ScrollViewer cannot scroll further, calls
  `CancelDirectManipulations` on the hit element and root, captures the
  pointer and follows with `Progress`. On release the same 25% / 0.5 rules
  apply; cancel animates back to 1 over 120 ms.
- Every `PrepareRootElement` resets the previous root's handlers and
  composition state, recomputes the axis and installs a fresh driver.

What it does not do:

- It does not drive the slide-in reveal and has no per-frame reveal. The
  ExplorerPatcher Start window appears at its final position; its own flyout
  animation covers the open transition.
- It does not use the explorer-side bridge mod, shared memory, or the
  `InputAnimationTarget` experiments.

Diagnostics:

- `mod init`, `root pending load ...`, `attached root=...`,
  `dismiss axis=...`, `dmhook installed target=...`,
  `dmhook n=... maxPos=... maxNeg=... base=(...) el=...`, and the
  pointer-fallback diagnostics `press type=...` (capped at 20 per process),
  first `move ...` (capped at 20 per process) and `takeover ...` go to
  `renderer.log`; the fallback's dismiss commit/cancel/dragging go to the
  Windhawk log.

Known risks:

- The offset hook relies on a PDB-confirmed signature and a hard-coded RVA:
  `self` in `rcx`, `x` in `xmm1`, `y` in `xmm2`, `changed` in `r9`. Windows
  x64 defines one calling convention, so a Windhawk function hook with the
  same prototype is ABI-compatible; a wrong prototype, a stale RVA or a
  different XAML build corrupts arguments or crashes. The file-version gate is
  the only guard.
- The hook also sees offset changes from other XAML surfaces in the process.
  It accepts all of them while a press session is active, because the process
  hosts only the Start UI; `el=...` shows which element produced them.
- `PointerDeviceType` is Touch/Pen only in the pointer fallback; the offset
  hook itself accepts any device. A precision touchpad or mouse reports Mouse
  and does not reach the fallback.
- `CancelDirectManipulations` is used only by the pointer fallback. It is
  called on the pressed hit element first and then on the root; the API
  cancels manipulations on ScrollViewer *parents*, so the hit-element call is
  the meaningful one.
- Dismiss posts synthetic `WM_KEYDOWN`/`WM_KEYUP` for `VK_ESCAPE` to the
  CoreWindow HWND; it depends on the legacy Start processing posted key input
  as its normal Escape. Not runtime-verified.
- The pointer fallback's scroll gate uses the innermost ScrollViewer's
  offsets. Nested or custom scroll surfaces may not be found, and at an
  overscroll edge the reported offsets may not reflect the visual position.
- Composition translation does not move XAML hit-testing. A drag either
  cancels back to 1 or the shell hides the menu; content is never left offset.
- The RVA is specific to `StartUI_.dll` 10.0.22621.3733.
- The dismiss settle uses an asynchronous `CompositionScopedBatch`
  completion. A stop flag and a serial guard it against unload/re-open, but an
  in-flight callback at module unload remains a teardown risk.
- Pointer handlers only see input that reaches the XAML root; input consumed
  by the shell before XAML will not dismiss.
*/


#include <windhawk_utils.h>
#include <windows.h>
#undef GetCurrentTime  // MinGW winbase.h macro vs. Timeline.GetCurrentTime

#include <winrt/base.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <mutex>

namespace {

namespace wuc = winrt::Windows::UI::Composition;
namespace wucore = winrt::Windows::UI::Core;
namespace wudi = winrt::Windows::Devices::Input;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;
namespace wuxi = winrt::Windows::UI::Xaml::Input;
namespace wuxm = winrt::Windows::UI::Xaml::Media;

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

constexpr std::chrono::milliseconds kDismissSettleDuration{150};
constexpr std::chrono::milliseconds kDismissCancelDuration{120};

constexpr float kDragStartThreshold = 8.0f;       // DIP
constexpr float kDismissDistanceFraction = 0.25f; // of Distance
constexpr float kDismissVelocity = 0.5f;          // DIP per ms

// ICoreWindowInterop (45D64A29-A63E-4CB6-B498-5781D298CB4F). Slot 3 is
// get_WindowHandle; the interface is classic COM, so it is called directly.
constexpr GUID kCoreWindowInteropIid = {
    0x45D64A29, 0xA63E, 0x4CB6,
    {0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F}};
using GetCoreWindowHandleAbi = HRESULT(STDMETHODCALLTYPE*)(void*, HWND*);

// One-shot XAML object reconnaissance, gated on the XAML build.
constexpr unsigned kXamlVersion[4] = {10, 0, 26100, 9949};

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

// ---------------------------------------------------------------------------
// Composition state
//
// Progress is the only animated scalar; Distance is the static window width
// or height, depending on the dismiss axis.
// Both the property set and the visual are agile Composition objects, so the
// pointer handlers and the settle batch callback can use them. The mutex
// protects the projection pointers and the check-then-update sequence against
// teardown; it is never held across a wait.
// ---------------------------------------------------------------------------

std::mutex g_compositionMutex;
wuc::Compositor g_compositor{nullptr};
wuc::CompositionPropertySet g_state{nullptr};
wuc::ExpressionAnimation g_translationExpression{nullptr};
wuc::Visual g_visual{nullptr};
wuc::CompositionScopedBatch g_dismissBatch{nullptr};

wux::FrameworkElement g_root{nullptr};  // UI thread only
wucore::CoreDispatcher g_dispatcher{nullptr};
std::atomic<DWORD> g_uiThreadId{0};
std::atomic<uint32_t> g_setupGeneration{0};

winrt::event_token g_loadedToken{};
winrt::event_token g_sizeToken{};

// XAML keeps only a weak reference to a routed-event handler, so the delegate
// objects must stay alive for as long as the handlers are attached.
wuxi::PointerEventHandler g_pointerPressedHandler{nullptr};
wuxi::PointerEventHandler g_pointerMovedHandler{nullptr};
wuxi::PointerEventHandler g_pointerReleasedHandler{nullptr};
wuxi::PointerEventHandler g_pointerCaptureLostHandler{nullptr};

// AddHandler takes an IReference<delegate>, so the boxed handlers are kept
// too; the same object is passed to RemoveHandler.
winrt::Windows::Foundation::IInspectable g_pointerPressedSink{nullptr};
winrt::Windows::Foundation::IInspectable g_pointerMovedSink{nullptr};
winrt::Windows::Foundation::IInspectable g_pointerReleasedSink{nullptr};
winrt::Windows::Foundation::IInspectable g_pointerCaptureLostSink{nullptr};

// Swipe dismiss state. UI thread only unless marked atomic.
bool g_dismissPressed = false;
bool g_dismissDragging = false;
bool g_moveLogged = false;
float g_dismissStartX = 0.0f;
float g_dismissStartY = 0.0f;
float g_dismissLastAxisDelta = 0.0f;
uint64_t g_dismissStartTick = 0;
uint32_t g_dismissPointerId = 0;
wux::UIElement g_pressHitElement{nullptr};
wuxc::ScrollViewer g_pressScrollViewer{nullptr};
double g_pressVerticalOffset = 0.0;
double g_pressScrollableHeight = 0.0;
double g_pressHorizontalOffset = 0.0;
double g_pressScrollableWidth = 0.0;
std::atomic<bool> g_dismissSettling{false};
std::atomic<uint32_t> g_dismissSerial{0};
std::atomic<uint32_t> g_pressLogCount{0};
std::atomic<uint32_t> g_moveLogCount{0};
HWND g_coreHwnd = nullptr;

// Filled in at setup and on every PrepareRootElement.
int32_t g_dismissAxis = static_cast<int32_t>(DismissAxis::Left);
float g_dismissDistance = 0.0f;

// DManip hook state. The hook and the pointer handlers both run on the XAML UI
// thread, so the session state needs no atomics and no locks.
bool g_dmHookChecked = false;
bool g_dmHookInstalled = false;
bool g_dmSessionActive = false;
bool g_dmHaveBaseline = false;
float g_dmBaseX = 0.0f;
float g_dmBaseY = 0.0f;
float g_dmMaxPos = 0.0f;
float g_dmMaxNeg = 0.0f;
int g_dmSamples = 0;
void* g_dmElement = nullptr;
uint64_t g_dmPressStartTick = 0;

// Defined with the unload helpers; PrepareRootElement uses them to reset on
// every open.
void RevokeHandlers();
void ResetComposition();
void ResetRenderer();
bool EnsureDmHook();
void EndDmSession();

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

// Stops the current Progress animation so a drag can drive the scalar
// directly.
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

bool AnimateProgressTo(float from, float to,
                       std::chrono::milliseconds duration) {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state || !g_compositor) {
        return false;
    }

    try {
        wuc::ScalarKeyFrameAnimation animation =
            g_compositor.CreateScalarKeyFrameAnimation();
        animation.InsertKeyFrame(0.0f, std::clamp(from, 0.0f, 1.0f));
        animation.InsertKeyFrame(1.0f, std::clamp(to, 0.0f, 1.0f));
        animation.Duration(duration);

        g_state.StopAnimation(L"Progress");
        g_state.StartAnimation(L"Progress", animation);
        // Keep the base value in sync so a later StopAnimation does not snap
        // back to the drag position.
        g_state.InsertScalar(L"Progress", to);
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

double AverageDismissVelocity(float axisDelta, uint64_t startTick) {
    const uint64_t now = GetTickCount64();
    uint64_t elapsed = now > startTick ? now - startTick : 0;
    if (elapsed == 0) {
        elapsed = 1;
    }
    return static_cast<double>(axisDelta) / static_cast<double>(elapsed);
}

// Finds the innermost ScrollViewer between the pressed element and the root.
wuxc::ScrollViewer FindInnermostScrollViewer(
    winrt::Windows::Foundation::IInspectable const& source,
    wux::FrameworkElement const& root) {
    wux::DependencyObject node = source.try_as<wux::DependencyObject>();
    while (node) {
        wuxc::ScrollViewer viewer = node.try_as<wuxc::ScrollViewer>();
        if (viewer) {
            return viewer;
        }
        if (winrt::get_abi(node) == winrt::get_abi(root)) {
            break;
        }
        node = wuxm::VisualTreeHelper::GetParent(node);
    }
    return nullptr;
}

bool RecordedCanScrollInDismissDirection() {
    switch (static_cast<DismissAxis>(g_dismissAxis)) {
        case DismissAxis::Left:
            return g_pressHorizontalOffset > 0.0;
        case DismissAxis::Right:
            return g_pressHorizontalOffset < g_pressScrollableWidth - 1.0;
        case DismissAxis::Top:
            return g_pressVerticalOffset > 0.0;
        case DismissAxis::Bottom:
            return g_pressVerticalOffset < g_pressScrollableHeight - 1.0;
    }
    return false;
}

// True while the ScrollViewer under the press can still scroll in the
// dismiss direction; until it reaches that end, the swipe belongs to the list.
bool CanScrollInDismissDirection() {
    wuxc::ScrollViewer viewer = g_pressScrollViewer;
    if (!viewer) {
        return false;
    }

    try {
        switch (static_cast<DismissAxis>(g_dismissAxis)) {
            case DismissAxis::Left:
                return viewer.HorizontalOffset() > 0.0;
            case DismissAxis::Right:
                return viewer.HorizontalOffset() <
                       viewer.ScrollableWidth() - 1.0;
            case DismissAxis::Top:
                return viewer.VerticalOffset() > 0.0;
            case DismissAxis::Bottom:
                return viewer.VerticalOffset() <
                       viewer.ScrollableHeight() - 1.0;
        }
    } catch (...) {
        return RecordedCanScrollInDismissDirection();
    }

    return false;
}

float ComputeAxisDelta(float x, float y) {
    switch (static_cast<DismissAxis>(g_dismissAxis)) {
        case DismissAxis::Left:
            return g_dismissStartX - x;
        case DismissAxis::Top:
            return g_dismissStartY - y;
        case DismissAxis::Right:
            return x - g_dismissStartX;
        case DismissAxis::Bottom:
            return y - g_dismissStartY;
    }
    return g_dismissStartX - x;
}

float ComputePerpendicular(float x, float y) {
    if (IsHorizontalAxis(g_dismissAxis)) {
        return std::fabs(y - g_dismissStartY);
    }
    return std::fabs(x - g_dismissStartX);
}

void ClearDismissPress() {
    g_dismissPressed = false;
    g_dismissDragging = false;
    g_moveLogged = false;
    g_dismissLastAxisDelta = 0.0f;
    g_dismissStartX = 0.0f;
    g_dismissStartY = 0.0f;
    g_dismissStartTick = 0;
    g_dismissPointerId = 0;
    g_pressHitElement = nullptr;
    g_pressScrollViewer = nullptr;
    g_pressVerticalOffset = 0.0;
    g_pressScrollableHeight = 0.0;
    g_pressHorizontalOffset = 0.0;
    g_pressScrollableWidth = 0.0;
}

void ResetDismissDrag() {
    ClearDismissPress();
    if (g_root) {
        try {
            g_root.ReleasePointerCaptures();
        } catch (...) {
        }
    }
}

// Committed dismiss: slide out over 150 ms, then post Escape so the shell
// closes the menu through its normal input path.
bool BeginDismissSettle(float from) {
    wuc::CompositionScopedBatch batch{nullptr};
    uint32_t serial = 0;

    try {
        {
            std::lock_guard lock(g_compositionMutex);
            if (!g_state || !g_compositor) {
                return false;
            }

            serial = g_dismissSerial.fetch_add(1) + 1;
            HWND coreHwnd = g_coreHwnd;
            if (!coreHwnd) {
                // The pointer handler runs on the UI thread; the Start window
                // is normally the active window of this thread.
                coreHwnd = GetActiveWindow();
            }

            wuc::ScalarKeyFrameAnimation animation =
                g_compositor.CreateScalarKeyFrameAnimation();
            animation.InsertKeyFrame(0.0f, std::clamp(from, 0.0f, 1.0f));
            animation.InsertKeyFrame(1.0f, 0.0f);
            animation.Duration(kDismissSettleDuration);

            batch = g_compositor.CreateScopedBatch(
                wuc::CompositionBatchTypes::Animation);
            batch.Completed(
                [serial, coreHwnd](
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

                        if (coreHwnd) {
                            PostMessageW(coreHwnd, WM_KEYDOWN, VK_ESCAPE, 0);
                            PostMessageW(coreHwnd, WM_KEYUP, VK_ESCAPE, 0);
                        }

                        if (g_state) {
                            g_state.StopAnimation(L"Progress");
                            g_state.InsertScalar(L"Progress", 1.0f);
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
            g_state.StartAnimation(L"Progress", animation);
            g_state.InsertScalar(L"Progress", 0.0f);
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

void OnDismissPointerPressed(wuxi::PointerRoutedEventArgs const& args) {
    try {
        if (g_dismissDragging ||
            g_dismissSettling.load(std::memory_order_relaxed)) {
            return;
        }

        // A session whose release never arrived is finished before the new one
        // starts recording.
        if (g_dmSessionActive) {
            EndDmSession();
        }

        if (EnsureDmHook()) {
            g_dmHaveBaseline = false;
            g_dmBaseX = 0.0f;
            g_dmBaseY = 0.0f;
            g_dmMaxPos = 0.0f;
            g_dmMaxNeg = 0.0f;
            g_dmSamples = 0;
            g_dmElement = nullptr;
            g_dmPressStartTick = GetTickCount64();
            g_dmSessionActive = true;

            // The offset hook is the distance source; the pointer is never
            // captured or consumed.
            ClearDismissPress();
            return;
        }

        const wudi::PointerDeviceType deviceType =
            args.Pointer().PointerDeviceType();
        if (deviceType != wudi::PointerDeviceType::Touch &&
            deviceType != wudi::PointerDeviceType::Pen) {
            return;
        }

        // A new touch/pen press supersedes a pending press that never became
        // a drag, so a lost release cannot block later gestures.
        ClearDismissPress();

        wux::FrameworkElement root = g_root;
        if (!root) {
            return;
        }

        const auto point = args.GetCurrentPoint(root);
        g_dismissStartX = point.Position().X;
        g_dismissStartY = point.Position().Y;
        g_dismissLastAxisDelta = 0.0f;
        g_dismissStartTick = GetTickCount64();
        g_dismissPointerId = args.Pointer().PointerId();
        g_dismissPressed = true;
        g_pressHitElement = args.OriginalSource().try_as<wux::UIElement>();

        const uint32_t count =
            g_pressLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 20) {
            FileLog(L"press type=%d", static_cast<int>(deviceType));
        }

        // The whole legacy Start content can live inside one ScrollViewer.
        // Remember the innermost one; the drag only takes over once it cannot
        // scroll further in the dismiss direction.
        g_pressScrollViewer =
            FindInnermostScrollViewer(args.OriginalSource(), root);
        if (g_pressScrollViewer) {
            try {
                g_pressVerticalOffset =
                    g_pressScrollViewer.VerticalOffset();
                g_pressScrollableHeight =
                    g_pressScrollViewer.ScrollableHeight();
                g_pressHorizontalOffset =
                    g_pressScrollViewer.HorizontalOffset();
                g_pressScrollableWidth =
                    g_pressScrollViewer.ScrollableWidth();
            } catch (...) {
            }
        }
    } catch (...) {
        ClearDismissPress();
        // Never let an exception escape into XAML.
    }
}

void OnDismissPointerMoved(wuxi::PointerRoutedEventArgs const& args) {
    try {
        if (g_dmSessionActive) {
            // The offset hook is the distance source; never take the pointer
            // or drive Progress during the gesture.
            return;
        }

        if (!g_dismissPressed ||
            args.Pointer().PointerId() != g_dismissPointerId) {
            return;
        }

        wux::FrameworkElement root = g_root;
        if (!root) {
            return;
        }

        const auto point = args.GetCurrentPoint(root);
        const float x = point.Position().X;
        const float y = point.Position().Y;
        const float axisDelta = ComputeAxisDelta(x, y);
        const float perpendicular = ComputePerpendicular(x, y);
        g_dismissLastAxisDelta = axisDelta;

        if (!g_moveLogged) {
            g_moveLogged = true;
            const bool canScroll = CanScrollInDismissDirection();
            const uint32_t count =
                g_moveLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 20) {
                FileLog(L"move axisDelta=%.1f perp=%.1f scrollable=%d "
                        L"canScroll=%d",
                        axisDelta, perpendicular,
                        g_pressScrollViewer ? 1 : 0, canScroll ? 1 : 0);
            }
        }

        if (!g_dismissDragging) {
            if (axisDelta <= kDragStartThreshold ||
                perpendicular >= axisDelta) {
                return;
            }
            if (CanScrollInDismissDirection()) {
                return;
            }
            if (!(g_dismissDistance > 0.0f)) {
                ClearDismissPress();
                return;
            }

            // CancelDirectManipulations affects ScrollViewer ancestors of the
            // element it is called on, so try the pressed hit element first;
            // the root call is a cheap fallback. Either may throw if the
            // element has left the tree; the capture is still attempted.
            bool cancelHit = false;
            bool cancelRoot = false;
            try {
                if (g_pressHitElement) {
                    cancelHit = g_pressHitElement.CancelDirectManipulations();
                }
            } catch (...) {
            }
            try {
                cancelRoot = root.CancelDirectManipulations();
            } catch (...) {
            }
            FileLog(L"takeover cancelHit=%d cancelRoot=%d",
                    cancelHit ? 1 : 0, cancelRoot ? 1 : 0);

            if (!root.CapturePointer(args.Pointer())) {
                // Never follow without capture: moves and the release could
                // be lost, leaving the panel half-offset.
                ClearDismissPress();
                return;
            }

            g_dismissDragging = true;
            StopProgressAnimation();
            Wh_Log(L"dismissPressed");
            Wh_Log(L"dismissDragging axisDelta=%.1f vel=%.2f", axisDelta,
                   AverageDismissVelocity(axisDelta, g_dismissStartTick));
        }

        if (!(g_dismissDistance > 0.0f)) {
            return;
        }
        InsertProgress(
            std::clamp(1.0f - axisDelta / g_dismissDistance, 0.0f, 1.0f));
        args.Handled(true);
    } catch (...) {
        // Never let an exception escape into XAML.
    }
}

void FinishDismiss(wuxi::PointerRoutedEventArgs const* args, bool captureLost) {
    if (!g_dismissPressed) {
        return;
    }
    if (args && args->Pointer().PointerId() != g_dismissPointerId) {
        return;
    }

    wux::FrameworkElement root = g_root;
    float axisDelta = g_dismissLastAxisDelta;
    if (args && root) {
        try {
            const auto point = args->GetCurrentPoint(root);
            axisDelta =
                ComputeAxisDelta(point.Position().X, point.Position().Y);
        } catch (...) {
        }
    }

    const bool wasDragging = g_dismissDragging;
    const float distance = g_dismissDistance;
    const double velocity =
        AverageDismissVelocity(axisDelta, g_dismissStartTick);

    ClearDismissPress();

    if (!wasDragging) {
        // A pending press that never dragged has no capture, no animation and
        // must not log anything.
        return;
    }

    if (root && !captureLost && args) {
        try {
            root.ReleasePointerCapture(args->Pointer());
        } catch (...) {
        }
    }

    if (!(distance > 0.0f)) {
        ShowAtRest();
        return;
    }

    const float from = std::clamp(1.0f - axisDelta / distance, 0.0f, 1.0f);
    if (axisDelta > kDismissDistanceFraction * distance ||
        velocity > kDismissVelocity) {
        Wh_Log(L"dismissCommit");
        if (!BeginDismissSettle(from)) {
            ShowAtRest();
        }
    } else {
        Wh_Log(L"dismissCancel");
        if (!AnimateProgressTo(from, 1.0f, kDismissCancelDuration)) {
            ShowAtRest();
        }
    }
}

void OnDismissPointerReleased(wuxi::PointerRoutedEventArgs const& args) {
    try {
        if (g_dmSessionActive) {
            EndDmSession();
        } else {
            FinishDismiss(&args, false);
        }
    } catch (...) {
        // Never let an exception escape into XAML.
    }
}

void OnDismissPointerCaptureLost(wuxi::PointerRoutedEventArgs const& args) {
    try {
        if (g_dmSessionActive) {
            EndDmSession();
        } else {
            FinishDismiss(&args, true);
        }
    } catch (...) {
        // Never let an exception escape into XAML.
    }
}

using PointerHandlerFn = void (*)(wuxi::PointerRoutedEventArgs const&);

wuxi::PointerEventHandler MakePointerHandler(PointerHandlerFn fn) {
    return wuxi::PointerEventHandler(
        [fn](winrt::Windows::Foundation::IInspectable const&,
             wuxi::PointerRoutedEventArgs const& args) { fn(args); });
}

// The HWND is needed to post Escape after a committed dismiss.
void CaptureCoreWindowHandle() {
    g_coreHwnd = nullptr;

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
        g_coreHwnd = hwnd;
    } catch (...) {
        // A missing CoreWindow handle only disables the Escape fallback.
    }
}

// Chooses the dismiss direction from the side of the work area the Start
// window is anchored to. Checked on every setup because the window can move
// between displays.
void UpdateDismissAxis() {
    if (!g_coreHwnd) {
        return;
    }

    RECT windowRect{};
    if (!GetWindowRect(g_coreHwnd, &windowRect)) {
        return;
    }

    HMONITOR monitor = MonitorFromWindow(g_coreHwnd, MONITOR_DEFAULTTONEAREST);
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
}

// ---------------------------------------------------------------------------
// Diagnostic: one-shot XAML object reconnaissance
//
// Dumps the captured root's field pointers and the first qwords of plausible
// heap candidates once per process. Read-only; no behaviour change.
// ---------------------------------------------------------------------------

using GetFileVersionInfoSizeW_t = decltype(&GetFileVersionInfoSizeW);
using GetFileVersionInfoW_t = decltype(&GetFileVersionInfoW);
using VerQueryValueW_t = decltype(&VerQueryValueW);

struct VersionApi {
    HMODULE module = nullptr;
    bool ownsReference = false;
    GetFileVersionInfoSizeW_t size = nullptr;
    GetFileVersionInfoW_t info = nullptr;
    VerQueryValueW_t query = nullptr;
};

void UnloadVersionApi(VersionApi& api) {
    if (api.ownsReference && api.module) {
        FreeLibrary(api.module);
    }
    api = {};
}

bool LoadVersionApi(VersionApi* api) {
    HMODULE module = GetModuleHandleW(L"version.dll");
    if (!module) {
        module = LoadLibraryW(L"version.dll");
        if (!module) {
            return false;
        }
        api->ownsReference = true;
    }
    api->module = module;

    api->size = reinterpret_cast<GetFileVersionInfoSizeW_t>(
        GetProcAddress(module, "GetFileVersionInfoSizeW"));
    api->info = reinterpret_cast<GetFileVersionInfoW_t>(
        GetProcAddress(module, "GetFileVersionInfoW"));
    api->query = reinterpret_cast<VerQueryValueW_t>(
        GetProcAddress(module, "VerQueryValueW"));

    if (!api->size || !api->info || !api->query) {
        UnloadVersionApi(*api);
        return false;
    }
    return true;
}

bool QueryFileVersionString(const VersionApi& api, const wchar_t* path,
                            wchar_t* out, size_t outCount) {
    DWORD handle = 0;
    const DWORD size = api.size(path, &handle);
    if (size == 0 || size > 8192) {
        return false;
    }

    BYTE buffer[8192];
    if (!api.info(path, 0, size, buffer)) {
        return false;
    }

    struct Translation {
        WORD language;
        WORD codePage;
    };

    Translation* translations = nullptr;
    UINT translationBytes = 0;
    if (!api.query(buffer, L"\\VarFileInfo\\Translation",
                   reinterpret_cast<void**>(&translations),
                   &translationBytes) ||
        !translations) {
        return false;
    }

    const size_t translationCount = translationBytes / sizeof(Translation);
    for (size_t i = 0; i < translationCount; i++) {
        wchar_t key[64] = {};
        swprintf_s(key, ARRAYSIZE(key),
                   L"\\StringFileInfo\\%04x%04x\\FileVersion",
                   translations[i].language, translations[i].codePage);

        wchar_t* value = nullptr;
        UINT valueChars = 0;
        if (!api.query(buffer, key, reinterpret_cast<void**>(&value),
                       &valueChars) ||
            !value || valueChars == 0) {
            continue;
        }

        size_t length = 0;
        while (length + 1 < outCount && value[length] != L'\0') {
            out[length] = value[length];
            length++;
        }
        out[length] = L'\0';
        return length != 0;
    }

    return false;
}

bool ParseVersion(const wchar_t* text, unsigned* parts) {
    const wchar_t* cursor = text;
    for (int i = 0; i < 4; i++) {
        while (*cursor == L' ' || *cursor == L'\t') {
            cursor++;
        }
        if (*cursor < L'0' || *cursor > L'9') {
            return false;
        }

        unsigned value = 0;
        while (*cursor >= L'0' && *cursor <= L'9') {
            value = value * 10 + static_cast<unsigned>(*cursor - L'0');
            if (value > 0xFFFF) {
                return false;
            }
            cursor++;
        }
        parts[i] = value;

        if (i < 3) {
            if (*cursor != L'.') {
                return false;
            }
            cursor++;
        }
    }
    return true;
}

bool XamlBuildMatches(HMODULE module) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        FileLog(L"xaml build mismatch unknown; DManip hook disabled");
        return false;
    }

    wchar_t rawVersion[64] = {};
    wchar_t displayVersion[64] = L"unknown";
    unsigned parts[4] = {};
    bool versionMatches = false;

    VersionApi api;
    if (LoadVersionApi(&api)) {
        if (QueryFileVersionString(api, path, rawVersion,
                                   ARRAYSIZE(rawVersion)) &&
            ParseVersion(rawVersion, parts)) {
            swprintf_s(displayVersion, ARRAYSIZE(displayVersion),
                       L"%u.%u.%u.%u", parts[0], parts[1], parts[2],
                       parts[3]);
            versionMatches = true;
            for (int i = 0; i < 4; i++) {
                if (parts[i] != kXamlVersion[i]) {
                    versionMatches = false;
                    break;
                }
            }
        }
        UnloadVersionApi(api);
    }

    if (!versionMatches) {
        FileLog(L"xaml build mismatch %s; DManip hook disabled",
                displayVersion);
        return false;
    }

    return true;
}

// DManipDataBase::SetContentOffsetsHelper, Windows.UI.Xaml.dll 10.0.26100.9949.
// HRESULT (DManipDataBase* self, float x, float y, bool* changed): self in
// rcx, x in xmm1, y in xmm2, changed in r9.
using SetContentOffsetsHelper_t = HRESULT (*)(void*, float, float, bool*);

SetContentOffsetsHelper_t g_setContentOffsetsHelperOriginal = nullptr;

// Runs on the XAML UI thread, inside the offset update. No logging here.
void RecordDmOffset(void* self, float x, float y) {
    const bool horizontal = IsHorizontalAxis(g_dismissAxis);
    const float value = horizontal ? x : y;
    if (!std::isfinite(value)) {
        return;
    }

    if (!g_dmHaveBaseline) {
        g_dmHaveBaseline = true;
        g_dmBaseX = x;
        g_dmBaseY = y;
        if (self) {
            g_dmElement = *reinterpret_cast<void**>(
                static_cast<BYTE*>(self) + 0x40);
        }
    }

    const float delta = value - (horizontal ? g_dmBaseX : g_dmBaseY);
    if (delta > g_dmMaxPos) {
        g_dmMaxPos = delta;
    }
    if (-delta > g_dmMaxNeg) {
        g_dmMaxNeg = -delta;
    }
    g_dmSamples++;
}

HRESULT SetContentOffsetsHelper_Hook(void* self, float x, float y,
                                     bool* changed) {
    if (g_dmSessionActive && self && g_setContentOffsetsHelperOriginal) {
        RecordDmOffset(self, x, y);
    }
    return g_setContentOffsetsHelperOriginal(self, x, y, changed);
}

// Installs the offset hook once, after the first root attach. The file version
// is the only guard for the hard-coded RVA.
bool EnsureDmHook() {
    if (g_dmHookInstalled) {
        return true;
    }
    if (g_dmHookChecked) {
        return false;
    }
    g_dmHookChecked = true;

    HMODULE module = GetModuleHandleW(L"Windows.UI.Xaml.dll");
    if (!module) {
        FileLog(L"xaml build mismatch unknown; DManip hook disabled");
        return false;
    }
    if (!XamlBuildMatches(module)) {
        return false;
    }

    void* target = reinterpret_cast<BYTE*>(module) + 0x4DE388;
    if (!Wh_SetFunctionHook(
            target, reinterpret_cast<void*>(SetContentOffsetsHelper_Hook),
            reinterpret_cast<void**>(&g_setContentOffsetsHelperOriginal))) {
        FileLog(L"dmhook install failed; pointer fallback only");
        return false;
    }

    // A hook registered after Wh_ModInit needs an explicit apply.
    Wh_ApplyHookOperations();

    g_dmHookInstalled = true;
    FileLog(L"dmhook installed target=%p", target);
    return true;
}

void EndDmSession() {
    if (!g_dmSessionActive) {
        return;
    }
    g_dmSessionActive = false;

    const uint64_t now = GetTickCount64();
    const uint64_t elapsed =
        now > g_dmPressStartTick ? now - g_dmPressStartTick : 0;
    const float m = std::max(g_dmMaxPos, g_dmMaxNeg);

    FileLog(L"dmhook n=%d maxPos=%.1f maxNeg=%.1f base=(%.1f,%.1f) el=%p",
            g_dmSamples, g_dmMaxPos, g_dmMaxNeg, g_dmBaseX, g_dmBaseY,
            g_dmElement);

    const float distance = g_dismissDistance;
    if (!(distance > 0.0f)) {
        ShowAtRest();
        return;
    }

    const double speed = static_cast<double>(m) /
                         static_cast<double>(elapsed > 0 ? elapsed : 1);
    if (m > kDismissDistanceFraction * distance ||
        speed > kDismissVelocity) {
        const float from = std::clamp(1.0f - m / distance, 0.0f, 1.0f);
        InsertProgress(from);
        if (!BeginDismissSettle(from)) {
            ShowAtRest();
        }
    } else {
        // Let the native ScrollViewer snap back; reset without animation.
        ShowAtRest();
    }
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

    CaptureCoreWindowHandle();
    if (!g_coreHwnd) {
        Wh_Log(L"No CoreWindow HWND; dismiss will fall back to GetActiveWindow");
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

                std::lock_guard lock(g_compositionMutex);
                if (g_state) {
                    g_state.InsertScalar(L"Distance", distance);
                }
            } catch (winrt::hresult_error const& e) {
                Wh_Log(L"SizeChanged update failed: %08X", e.code());
            } catch (...) {
                // Never let an exception escape into XAML.
            }
        });

    g_pointerPressedHandler = MakePointerHandler(&OnDismissPointerPressed);
    g_pointerMovedHandler = MakePointerHandler(&OnDismissPointerMoved);
    g_pointerReleasedHandler = MakePointerHandler(&OnDismissPointerReleased);
    g_pointerCaptureLostHandler =
        MakePointerHandler(&OnDismissPointerCaptureLost);

    g_pointerPressedSink = winrt::box_value(g_pointerPressedHandler);
    g_pointerMovedSink = winrt::box_value(g_pointerMovedHandler);
    g_pointerReleasedSink = winrt::box_value(g_pointerReleasedHandler);
    g_pointerCaptureLostSink = winrt::box_value(g_pointerCaptureLostHandler);

    root.AddHandler(wux::UIElement::PointerPressedEvent(),
                    g_pointerPressedSink, true);
    root.AddHandler(wux::UIElement::PointerMovedEvent(), g_pointerMovedSink,
                    true);
    root.AddHandler(wux::UIElement::PointerReleasedEvent(),
                    g_pointerReleasedSink, true);
    root.AddHandler(wux::UIElement::PointerCaptureLostEvent(),
                    g_pointerCaptureLostSink, true);

    // The XAML module is loaded by the time the root exists; install the
    // offset hook once so the first press can use it.
    EnsureDmHook();

    const winrt::hstring rootClassName = winrt::get_class_name(root);
    FileLog(L"attached root=%s size=%.0fx%.0f", rootClassName.c_str(), width,
            height);

    Wh_Log(L"Dismiss handlers attached; composition translation enabled "
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

        // A new open: drop the previous root's handlers, property set,
        // animation and drag state before installing the fresh renderer.
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
// Hook installation
// ---------------------------------------------------------------------------

std::atomic<bool> g_startUIHooked{false};
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

void TryInstallFromLoadedModules(bool apply) {
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }

    bool installed = false;
    if (!g_startUIHooked.load(std::memory_order_relaxed)) {
        HMODULE startUI = GetModuleHandleW(L"StartUI_.dll");
        if (startUI) {
            InstallStartUIHooks(startUI);
            installed = true;
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
    // No reference is held here; just stop recording offsets.
    g_dmSessionActive = false;
    g_dmHaveBaseline = false;
    g_dmElement = nullptr;

    // The press cache holds a XAML reference; release it on the UI thread.
    g_pressHitElement = nullptr;

    wux::FrameworkElement root = g_root;
    if (!root) {
        return;
    }

    try {
        if (g_pointerPressedSink) {
            root.RemoveHandler(wux::UIElement::PointerPressedEvent(),
                               g_pointerPressedSink);
            g_pointerPressedSink = nullptr;
            g_pointerPressedHandler = nullptr;
        }
        if (g_pointerMovedSink) {
            root.RemoveHandler(wux::UIElement::PointerMovedEvent(),
                               g_pointerMovedSink);
            g_pointerMovedSink = nullptr;
            g_pointerMovedHandler = nullptr;
        }
        if (g_pointerReleasedSink) {
            root.RemoveHandler(wux::UIElement::PointerReleasedEvent(),
                               g_pointerReleasedSink);
            g_pointerReleasedSink = nullptr;
            g_pointerReleasedHandler = nullptr;
        }
        if (g_pointerCaptureLostSink) {
            root.RemoveHandler(wux::UIElement::PointerCaptureLostEvent(),
                               g_pointerCaptureLostSink);
            g_pointerCaptureLostSink = nullptr;
            g_pointerCaptureLostHandler = nullptr;
        }
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
    // so an old batch cannot post Escape into a newly opened menu.
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

    // Invalidate a stale Loaded handler from the previous root.
    g_setupGeneration.fetch_add(1, std::memory_order_relaxed);
}

// Drops the previous root's renderer before a new open installs its own
// property set, animation and handlers. Runs on the UI thread.
void ResetRenderer() {
    ResetDismissDrag();
    RevokeHandlers();
    ResetComposition();
}

}  // namespace

BOOL Wh_ModInit() {
    FileLog(L"mod init");
    Wh_Log(L"Legacy Start Edge Swipe Dismiss 0.1.0 initializing");

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

    // The offset hook must stop recording before the module goes away.
    g_dmSessionActive = false;
}

void Wh_ModUninit() {
    RevokeHandlersOnUiThread();
    ResetComposition();
    Wh_Log(L"Legacy Start Edge Swipe Dismiss uninitialized");
}
