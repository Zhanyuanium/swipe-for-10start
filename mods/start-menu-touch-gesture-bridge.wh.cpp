// ==WindhawkMod==
// @id              start-menu-touch-gesture-bridge
// @name            Start Menu Touch Gesture Bridge
// @description     Publishes the live Win10 Start edge-gesture handle and state from explorer for the StartMenuExperienceHost side
// @version         0.6.0
// @author          doubao
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -I"C:\Users\doubao\source\repos\swipe-for-10start\mods" -Wall -Wextra -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Start Menu Touch Gesture Bridge (explorer side)

Explorer-side half of the touch-swipe bridge for the ExplorerPatcher legacy
Windows 10 Start menu.

The mod owns one close for both paths. A Windows-key close is the shell's own
`CStartExperienceManager::Hide`, delayed by up to 350 ms so the Start side can
retract the panel first. A gesture close retracts the panel in the Start mod,
writes `hide-now.txt` there, and this mod calls `Hide(manager, 1)` itself, so a
gesture close and a keyboard close end in the same shell call.

Measured on this build, `Hide` is not the whole close: the shell keeps its Start
toggle in the key handler, so after a programmatic Hide the next Windows press
still calls Hide instead of Show and is spent on an already hidden menu. The mod
therefore sends one Windows tap 120 ms after the hide it performed. That tap is
invisible (in that state the handler's answer is the same Hide) and re-arms the
toggle, which the first press after a gesture close needs.

The mod also observes the taskbar-edge gesture path in `twinui.pcshell.dll`,
then publishes the live `GestureAnimationData` handle (source/provider id), the
edge, the start point and the gesture state to shared memory:

- mapping: `Local\WindhawkStartGestureShared` (`StartGestureShared`)
- event:   `Local\WindhawkStartGestureEvent` (auto-reset, signalled on write)

A StartMenuExperienceHost-side consumer could read the mapping and bind a
Composition animation with `InputAnimationTarget::GetInputAnimationObject`; the
shipping Start mod does not read it. Its live couplings are `hide-now.txt` and
the `retract` debug line described below.

Hooked functions are forwarded unchanged except for the delay described below.
The mod creates one worker thread, one message-only window on the shell's UI
thread and no COM objects.

Notes:
- Windhawk compiles the mod from stdin, so the repository `mods` folder is
  passed via `@compilerOptions -I...` to resolve `start_gesture_shared.h`.
  Update that path if the repository is moved.
- RVAs are specific to `twinui.pcshell.dll` 26100.9949 (x86-64). Before any
  hook is installed, the loaded module must report that exact file version
  (and, when readable, the PE timestamp of the loaded image must match the file
  on disk). Otherwise the mod logs one mismatch line and installs nothing.
- Hooks are installed only after `twinui.pcshell.dll` is loaded, via a
  `LoadLibraryExW` hook plus the module check in `Wh_ModAfterInit`.
- The sequence field is a seqlock: a reader must retry when it is odd or when
  it changed between two reads.
- `edge` is written verbatim from the shell. In the Phase 0 log, a swipe that
  started at (45, 900) on a 1440x900 screen (taskbar on the left) reported
  edge 0, matching `StartGestureEdge::Left`.
- The point is published once per gesture from `ReportEdgeGesture`, in the same
  write as the edge and the handle. There is no per-frame position sampling.
- Each gesture state transition appends one line to `bridge.log` in the mod
  storage folder: `state=... edge=... src=... prov=... <reason>`.

The unified close:

- `Show` and `Hide` both cache the `CStartExperienceManager` pointer. Every call
  refreshes it, so a hide is never sent to an instance the shell has replaced.
- One worker thread polls the Start mod's `hide-now.txt` every 20 ms. A
  last-write time newer than the one already handled is a request; the content
  is the constant `1` and is not read. The stamp that exists when the worker
  starts is recorded first, so a request left over from an earlier session is
  not replayed.
- The request runs on the shell's UI thread: the first `Show` or `Hide` on that
  thread creates a message-only window there, and the worker only posts to it.
  The window proc calls the original `Hide(manager, 1)` and logs
  `hide requested reason=1 thread=ui hr=...`. When the window cannot be created,
  the worker calls the original itself and logs `thread=worker`. A request that
  arrives before the first `Show`/`Hide` logs
  `hide request skipped (no manager)` and is dropped.
- The call bypasses this hook, so a gesture close never asks for a second
  retract: the Start side already animated it.
- 120 ms later the worker taps the Windows key once and logs
  `hide toggle rearmed`. The shell's toggle lives in the key handler, so this
  press is what makes the next Windows press open the menu instead of hiding it
  again; it is invisible because the handler's answer in that state is the Hide
  that already ran. The tap is skipped with `hide toggle rearm skipped
  (reopened)` when a Show arrived in between (the user opened the menu again),
  and with `hide toggle rearm skipped (hide failed)` when the original Hide
  failed.

The Windows-key close:

- A Windows-key close is the shell's own `Hide`. The `Hide` hook delays it by
  up to 350 ms; before the wait the mod writes `retract=1` to the Start mod's
  `debug.txt`, and that mod retracts its composition `Progress` to 0 in 260 ms,
  so the panel slides out instead of vanishing. One `hide delayed` line per
  delayed hide. The delay runs only after the `Hide` hook was installed, and it
  never exceeds 350 ms plus one 10 ms poll. When the write fails, the hide runs
  immediately: a delay without a retract would only slow the close down.
- A hide that follows a gesture close is not delayed and does not ask for a
  retract. The Start mod stamps `close-mode.txt` when a gesture close starts,
  and that close already retracted the panel. A stamp younger than 800 ms
  therefore means "the animation has run": the hook logs
  `hide passed (gesture)` and calls the original at once. This is a guard for a
  shell hide that races a gesture close; the normal gesture path never reaches
  this hook. The stamp is a last-write time only; its content is not read.
- The hook never returns without the original. An earlier version cancelled a
  delayed hide by returning `S_OK`; measured, the shell's Start toggle then
  stayed one close ahead and the Windows key and the taskbar Start button stopped
  working until `StartMenuExperienceHost.exe` restarted. A close is cancelled on
  the Start side instead, by not writing its request.
- `Hide` is `HRESULT (void* this, uint32_t reason)` on
  twinui.pcshell.dll 26100.9949. The prologue reads `rcx` (this) and `edx`
  (reason), compares `edx` with 7 and returns 0 on success or the negative
  HRESULT of the failing inner call. It never reads `r8` or `r9`.
*/
// ==/WindhawkModReadme==

/*
 * [INPUT]: depends on twinui.pcshell.dll 10.0.26100.9949 in explorer.exe for
 *   TouchEdgeGestureProcessor::InputGestureOperationStarted/Ended,
 *   EdgeAnimationRelationshipManager::ReportEdgeGesture,
 *   CStartExperienceManager::SetAnimationData/Show/Hide; on version.dll for the
 *   build gate; on LoadLibraryExW from kernelbase.dll for the late module load;
 *   on RegisterClassExW, CreateWindowExW and PostMessageW from user32; on the
 *   shared-memory contract in start_gesture_shared.h; on the Start mod's
 *   `close-mode.txt` last-write time to tell a gesture close from a Windows-key
 *   close; on the Start mod's `hide-now.txt` last-write time, the request that
 *   turns a gesture close into a Hide call.
 * [OUTPUT]: the `Local\WindhawkStartGestureShared` mapping and its auto-reset
 *   event, holding the edge, the gesture handle and the state under a seqlock;
 *   a `bridge.log` line per state transition; one `retract=1` line in the Start
 *   mod's `debug.txt` followed by a 350 ms wait in
 *   CStartExperienceManager::Hide, so a Windows-key close slides the panel out
 *   before the window is hidden; a `hide requested reason=1 thread=ui|worker
 *   hr=...` line and a call of the original Hide for every `hide-now.txt` write,
 *   so a gesture close ends in the shell's own Hide, plus one Windows tap 120 ms
 *   later (`hide toggle rearmed`) that re-arms the shell's Start toggle because
 *   Hide alone does not; a `hide passed (gesture)` line and an immediate hide
 *   when the Start mod's close-mode stamp is younger than 800 ms. Every path
 *   calls the original Hide. No exported symbols.
 * [POS]: explorer-side half of the legacy Start gesture work. It is the only
 *   code that calls or observes the shell's own hide decision. A gesture close
 *   asks for it through `hide-now.txt` and a Windows-key close is delayed for
 *   the Start side's retract, so both closes end in the same Hide call. The
 *   keyboard toggle that Hide does not update is the one thing this mod has to
 *   press a key for. The shipping Start mod does not read the shared memory; the
 *   hide request, the debug line and the close-mode stamp are the only couplings
 *   that are live today.
 * [PROTOCOL]: Update this header on change, then check AGENTS.md
 */

#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdint>

#include "start_gesture_shared.h"

namespace {

constexpr uintptr_t kTwinuiPreferredBase = 0x180000000;

// twinui.pcshell.dll 26100.9949 (x86-64), verified by the Phase 0 probe.
constexpr uintptr_t kTouchEdgeGestureOperationStartedVa = 0x1800CAD80;
constexpr uintptr_t kTouchEdgeGestureOperationEndedVa = 0x1805F8840;
constexpr uintptr_t kReportEdgeGestureVa = 0x1806F63BC;
constexpr uintptr_t kSetAnimationDataVa = 0x1803C7740;
constexpr uintptr_t kShowVa = 0x180254C90;
constexpr uintptr_t kHideVa = 0x1803C5960;

constexpr wchar_t kTwinuiModuleName[] = L"twinui.pcshell.dll";

// The build the RVAs above were derived from.
constexpr unsigned kTwinuiVersion[4] = {10, 0, 26100, 9949};

// The Windows-key close is animated from the Start side. Hide is where the
// shell takes the Start window away; the panel needs that long to retract, so
// the hide waits for it. 350 ms covers the Start mod's 260 ms retract plus two
// 50 ms poll ticks.
constexpr DWORD kHideLeadMs = 350;

// The Start mod's debug channel. The bridge runs in explorer, so
// Wh_GetModStoragePath would resolve to its own storage directory: the Start
// mod's path is fixed by its id.
constexpr wchar_t kStartDebugFilePath[] =
    L"C:\\ProgramData\\Windhawk\\Engine\\ModsWritable\\mod-storage\\"
    L"local@start-menu-legacy-swipe\\debug.txt";
constexpr char kRetractCommand[] = "retract=1\r\n";

// The Start mod's close-mode stamp. A gesture close writes this file when its
// retract starts, so a fresh stamp means the panel has already left the screen:
// a hide that lands right after it is the tail of that animation, not a keyboard
// close, and must run at once instead of waiting for a retract that already
// happened. Only the last-write time is read.
constexpr wchar_t kStartCloseModeFilePath[] =
    L"C:\\ProgramData\\Windhawk\\Engine\\ModsWritable\\mod-storage\\"
    L"local@start-menu-legacy-swipe\\close-mode.txt";

// The Start mod's hide request. A gesture close retracts the panel in the Start
// process and writes this file when the retract ends; this mod answers with the
// same Hide a Windows-key close performs, so both closes end in one shell call
// and no Windows key is tapped. The content is a constant marker; only the
// last-write time is read.
constexpr wchar_t kStartHideNowFilePath[] =
    L"C:\\ProgramData\\Windhawk\\Engine\\ModsWritable\\mod-storage\\"
    L"local@start-menu-legacy-swipe\\hide-now.txt";

// The worker polls this file. 20 ms is below one compositor frame at 120 Hz, so
// the window is hidden within a frame or two of the retract ending, and it costs
// one file-time query per tick.
constexpr DWORD kHideRequestPollMs = 20;

// The reason a gesture close passes to Hide. Measured on this build: the
// Windows-key close passes 1, a click-away close passes 7. The reason does not
// decide whether the shell's Start toggle stays in step, so a gesture close
// passes the keyboard close's 1.
constexpr uint32_t kHideReasonGesture = 1;

// A gesture close writes the stamp when its retract starts, 290 ms before its
// request and this hide. 800 ms covers the request, the worker poll, the shell's
// own dispatch of the close and one Windows-timer tick, and stays short enough
// that a Windows-key close a second later is animated again.
constexpr DWORD kCloseModeFreshMs = 800;

// The hide waits in 10 ms steps, so the wait can end early when the mod is being
// disabled under it.
constexpr DWORD kHidePollMs = 10;

// The hide request runs on the thread that owns the manager, in a message-only
// window created there on the first Show/Hide. WM_APP+0x51 is above the range
// the shell's own windows use for control messages.
constexpr wchar_t kHideDispatchClass[] =
    L"WindhawkStartGestureHideDispatch";
constexpr UINT kHideDispatchMessage = WM_APP + 0x51;

struct Point {
    float x;
    float y;
};

struct GestureAnimationData {
    uint64_t sourceId;
    uint64_t providerId;
};

static_assert(sizeof(GestureAnimationData) == 16,
              "GestureAnimationData layout must match Phase 0");

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

std::atomic<bool> g_explorerHooked{false};
std::atomic<bool> g_afterInit{false};
std::atomic<bool> g_disabled{false};
// Set only after Wh_SetFunctionHook accepted the Hide hook, so a hide is never
// delayed on a build where the hook was refused.
std::atomic<bool> g_hideDelayEnabled{false};

// The cached CStartExperienceManager pointer, refreshed by every Show and Hide.
// The hider is the message-only window on the shell's UI thread; it is created
// by the first Show/Hide and only receives a post from the worker.
std::atomic<void*> g_managerThis{nullptr};
std::atomic<HWND> g_dispatchHwnd{nullptr};
std::atomic<bool> g_dispatchCreateAttempted{false};
HANDLE g_hideWorker = nullptr;
HANDLE g_hideWorkerStop = nullptr;

// Bumped by every Show. A hide request records it, so the toggle re-arm can tell
// that the menu was opened again in the meantime and leave it alone.
std::atomic<uint32_t> g_showSerial{0};

enum : int {
    kSharedNotStarted = 0,
    kSharedInitializing = 1,
    kSharedReady = 2,
    kSharedFailed = 3,
};

std::atomic<int> g_sharedInitState{kSharedNotStarted};
StartGestureShared* g_shared = nullptr;  // Published before kSharedReady
HANDLE g_sharedMapping = nullptr;
HANDLE g_sharedEvent = nullptr;
std::atomic<bool> g_sharedErrorLogged{false};

// The start point belongs to the gesture identified by gestureStartTick, so a
// later ReportEdgeGesture call for the same gesture never replaces it.
std::atomic<uint64_t> g_startPointTick{0};

// ---------------------------------------------------------------------------
// Shared memory
// ---------------------------------------------------------------------------

void LogSharedErrorOnce(const wchar_t* what, DWORD error) {
    if (!g_sharedErrorLogged.exchange(true)) {
        Wh_Log(L"%s failed (error %u); gesture data may be incomplete", what,
               static_cast<unsigned int>(error));
    }
}

void ResetSharedFields(StartGestureShared* shared) {
    // Unpublish first and publish magic last, so a consumer that mapped the
    // section concurrently never sees a half-initialized struct.
    shared->magic = 0;
    shared->version = 0;
    shared->sequence = 0;
    shared->state = static_cast<uint32_t>(StartGestureState::None);
    shared->edge = -1;
    shared->sourceId = 0;
    shared->providerId = 0;
    // Negative coordinates are not valid screen positions; the consumer can
    // use them to detect "no point reported yet".
    shared->startX = -1.0f;
    shared->startY = -1.0f;
    shared->currentX = -1.0f;
    shared->currentY = -1.0f;
    shared->gestureStartTick = 0;
    shared->lastUpdateTick = 0;
    shared->version = kWindhawkStartGestureVersion;
    shared->magic = kWindhawkStartGestureMagic;
}

StartGestureShared* CreateOrOpenShared() {
    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(sizeof(StartGestureShared)),
        kWindhawkStartGestureMappingName);
    if (!mapping) {
        LogSharedErrorOnce(L"CreateFileMappingW", GetLastError());
        return nullptr;
    }

    const bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;

    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                               sizeof(StartGestureShared));
    if (!view) {
        LogSharedErrorOnce(L"MapViewOfFile", GetLastError());
        CloseHandle(mapping);
        return nullptr;
    }
    g_sharedMapping = mapping;

    auto* shared = static_cast<StartGestureShared*>(view);
    const bool valid = !fresh &&
                       shared->magic == kWindhawkStartGestureMagic &&
                       shared->version == kWindhawkStartGestureVersion;
    // An odd sequence on an otherwise valid section means a previous mod
    // instance was unloaded in the middle of a write; reset it too.
    if (!valid || (shared->sequence & 1u) != 0u) {
        ResetSharedFields(shared);
    }

    g_sharedEvent =
        CreateEventW(nullptr, FALSE, FALSE, kWindhawkStartGestureEventName);
    if (!g_sharedEvent) {
        // Publishing still works; the consumer can poll the sequence.
        LogSharedErrorOnce(L"CreateEventW", GetLastError());
    }

    return shared;
}

StartGestureShared* EnsureShared() {
    const int state = g_sharedInitState.load(std::memory_order_acquire);
    if (state == kSharedReady) {
        return g_shared;
    }
    if (state == kSharedFailed) {
        return nullptr;
    }

    int expected = kSharedNotStarted;
    if (g_sharedInitState.compare_exchange_strong(expected,
                                                  kSharedInitializing,
                                                  std::memory_order_acq_rel)) {
        StartGestureShared* shared = CreateOrOpenShared();
        g_shared = shared;
        g_sharedInitState.store(shared ? kSharedReady : kSharedFailed,
                                std::memory_order_release);
        return shared;
    }

    // Another thread is initializing. Don't wait inside a hook; this write is
    // skipped and the next one will see the ready state.
    return g_sharedInitState.load(std::memory_order_acquire) == kSharedReady
               ? g_shared
               : nullptr;
}

struct SharedUpdate {
    bool written;
    uint32_t previousState;
    uint32_t newState;
    int32_t edge;
    int64_t sourceId;
    int64_t providerId;
};

// Serializes the seqlock writers. The gesture callbacks run on the explorer UI
// thread, but the unload path publishes from the Windhawk thread.
SRWLOCK g_sharedWriteLock = SRWLOCK_INIT;

// Bumps the seqlock around the write, updates the timestamp and signals the
// consumer.
template <typename Writer>
SharedUpdate UpdateShared(Writer&& writer) {
    SharedUpdate result{};

    if (g_disabled.load(std::memory_order_relaxed)) {
        return result;
    }

    StartGestureShared* shared = EnsureShared();
    if (!shared) {
        return result;
    }

    AcquireSRWLockExclusive(&g_sharedWriteLock);
    InterlockedIncrement(
        reinterpret_cast<volatile LONG*>(&shared->sequence));
    result.previousState = shared->state;
    writer(shared);
    shared->lastUpdateTick = GetTickCount64();
    result.newState = shared->state;
    result.edge = shared->edge;
    result.sourceId = shared->sourceId;
    result.providerId = shared->providerId;
    InterlockedIncrement(
        reinterpret_cast<volatile LONG*>(&shared->sequence));

    if (g_sharedEvent) {
        SetEvent(g_sharedEvent);
    }

    ReleaseSRWLockExclusive(&g_sharedWriteLock);

    result.written = true;
    return result;
}

// Used while the mod is being disabled, when normal writes are already
// blocked. Hooks are no longer applied after Wh_ModBeforeUninit returns.
void PublishInactiveOnUnload() {
    if (g_sharedInitState.load(std::memory_order_acquire) != kSharedReady) {
        return;
    }
    StartGestureShared* shared = g_shared;
    if (!shared) {
        return;
    }

    AcquireSRWLockExclusive(&g_sharedWriteLock);
    InterlockedIncrement(
        reinterpret_cast<volatile LONG*>(&shared->sequence));
    shared->state = static_cast<uint32_t>(StartGestureState::None);
    shared->edge = -1;
    shared->sourceId = 0;
    shared->providerId = 0;
    shared->startX = -1.0f;
    shared->startY = -1.0f;
    shared->currentX = -1.0f;
    shared->currentY = -1.0f;
    shared->gestureStartTick = 0;
    shared->lastUpdateTick = GetTickCount64();
    InterlockedIncrement(
        reinterpret_cast<volatile LONG*>(&shared->sequence));
    ReleaseSRWLockExclusive(&g_sharedWriteLock);

    if (g_sharedEvent) {
        SetEvent(g_sharedEvent);
    }
}

// ---------------------------------------------------------------------------
// Logging (state transitions only, never per frame)
// ---------------------------------------------------------------------------

// Appends to <mod storage>\bridge.log. Called once per gesture state
// transition.
void FileLog(const wchar_t* format, ...) {
    static SRWLOCK lock = SRWLOCK_INIT;

    WCHAR storage[MAX_PATH] = {};
    if (!Wh_GetModStoragePath(storage, ARRAYSIZE(storage))) {
        return;
    }

    WCHAR path[MAX_PATH] = {};
    swprintf_s(path, L"%s\\bridge.log", storage);

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

void LogStateChange(const SharedUpdate& update, const wchar_t* reason) {
    if (!update.written || update.previousState == update.newState) {
        return;
    }
    FileLog(L"state=%u edge=%d src=%lld prov=%lld %s",
            static_cast<unsigned int>(update.newState),
            static_cast<int>(update.edge),
            static_cast<long long>(update.sourceId),
            static_cast<long long>(update.providerId), reason);
}

// ---------------------------------------------------------------------------
// Build gate
//
// The hardcoded RVAs are only valid for the build they were taken from. The
// gate checks the file version of the loaded module and, when the file can be
// read, that its PE timestamp is the one in memory. A mismatch installs
// nothing.
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

// Reads FileVersion from the first StringFileInfo translation. The fixed
// version resource can lag the string on incremental Windows updates, so the
// string is what the gate compares.
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

// Parses the leading "a.b.c.d" of a file version string; a suffix such as
// " (WinBuild...)" is ignored.
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

bool ReadLoadedPeTimestamp(HMODULE module, DWORD* timestamp) {
    const auto* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x1000) {
        return false;
    }

    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    *timestamp = nt->FileHeader.TimeDateStamp;
    return true;
}

bool ReadFilePeTimestamp(const wchar_t* path, DWORD* timestamp) {
    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = false;
    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    if (ReadFile(file, &dos, sizeof(dos), &read, nullptr) &&
        read == static_cast<DWORD>(sizeof(dos)) &&
        dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        dos.e_lfanew <= 0x1000) {
        LARGE_INTEGER offset{};
        offset.QuadPart = dos.e_lfanew;
        if (SetFilePointerEx(file, offset, nullptr, FILE_BEGIN)) {
            IMAGE_NT_HEADERS nt{};
            if (ReadFile(file, &nt, sizeof(nt), &read, nullptr) &&
                read == static_cast<DWORD>(sizeof(nt)) &&
                nt.Signature == IMAGE_NT_SIGNATURE) {
                *timestamp = nt.FileHeader.TimeDateStamp;
                ok = true;
            }
        }
    }

    CloseHandle(file);
    return ok;
}

bool TwinuiBuildMatches(HMODULE module) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        Wh_Log(L"twinui build mismatch unknown; refusing to install hooks");
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
                if (parts[i] != kTwinuiVersion[i]) {
                    versionMatches = false;
                    break;
                }
            }
        }
        UnloadVersionApi(api);
    }

    if (!versionMatches) {
        Wh_Log(L"twinui build mismatch %s; refusing to install hooks",
               displayVersion);
        return false;
    }

    // A version match is enough to proceed; the timestamp check is an extra
    // guard against a file that was replaced while the module is loaded.
    DWORD loadedTimestamp = 0;
    DWORD fileTimestamp = 0;
    if (ReadLoadedPeTimestamp(module, &loadedTimestamp) &&
        ReadFilePeTimestamp(path, &fileTimestamp) &&
        loadedTimestamp != fileTimestamp) {
        Wh_Log(L"twinui build mismatch %s (timestamp 0x%08X != 0x%08X); "
               L"refusing to install hooks",
               displayVersion, loadedTimestamp, fileTimestamp);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// twinui.pcshell.dll hooks
// ---------------------------------------------------------------------------

using InputGestureOperationStarted_t = void (*)(void*, const void*, const void*);
InputGestureOperationStarted_t InputGestureOperationStarted_Original = nullptr;

void InputGestureOperationStarted_Hook(void* self, const void* operation,
                                       const void* args) {
    const uint64_t now = GetTickCount64();
    const SharedUpdate update = UpdateShared([now](StartGestureShared* shared) {
        // The complete start record: no field of the previous gesture stays
        // visible next to the new state and timestamp.
        shared->state = static_cast<uint32_t>(StartGestureState::Active);
        shared->edge = -1;
        shared->sourceId = 0;
        shared->providerId = 0;
        shared->startX = -1.0f;
        shared->startY = -1.0f;
        shared->currentX = -1.0f;
        shared->currentY = -1.0f;
        shared->gestureStartTick = now;
    });
    LogStateChange(update, L"InputGestureOperationStarted");

    if (InputGestureOperationStarted_Original) {
        InputGestureOperationStarted_Original(self, operation, args);
    }
}

using InputGestureOperationEnded_t = void (*)(void*, const void*, const void*);
InputGestureOperationEnded_t InputGestureOperationEnded_Original = nullptr;

void InputGestureOperationEnded_Hook(void* self, const void* operation,
                                     const void* args) {
    // Committed means only "the gesture finished while its handle was live",
    // which is the open signal the Start side consumes. It is not a claim
    // about what the shell did with the window.
    const SharedUpdate update = UpdateShared([](StartGestureShared* shared) {
        if (shared->state ==
                static_cast<uint32_t>(StartGestureState::Active) &&
            (shared->sourceId != 0 || shared->providerId != 0)) {
            shared->state =
                static_cast<uint32_t>(StartGestureState::Committed);
        }
    });
    LogStateChange(update,
                   L"InputGestureOperationEnded (ended with a live handle)");

    // The handle is deliberately kept: the subsequent Hide clears it.
    if (InputGestureOperationEnded_Original) {
        InputGestureOperationEnded_Original(self, operation, args);
    }
}

using ReportEdgeGesture_t = void (*)(void*, int, const GestureAnimationData*,
                                     Point);
ReportEdgeGesture_t ReportEdgeGesture_Original = nullptr;

void ReportEdgeGesture_Hook(void* self, int edge,
                            const GestureAnimationData* data, Point point) {
    // Called once per gesture right after InputGestureOperationStarted. This
    // is the first call that carries the edge, the handle and a real point, so
    // it republishes the whole start record in one seqlock write.
    const SharedUpdate update =
        UpdateShared([edge, data, point](StartGestureShared* shared) {
            const uint64_t now = GetTickCount64();
            shared->state = static_cast<uint32_t>(StartGestureState::Active);
            shared->edge = edge;

            uint64_t startTick = shared->gestureStartTick;
            if (startTick == 0) {
                startTick = now;
            }
            // Keep the first point of this gesture if the shell reports the
            // edge more than once.
            if (g_startPointTick.load(std::memory_order_relaxed) !=
                startTick) {
                shared->startX = point.x;
                shared->startY = point.y;
                g_startPointTick.store(startTick, std::memory_order_relaxed);
            }
            shared->currentX = point.x;
            shared->currentY = point.y;
            shared->gestureStartTick = startTick;

            if (data) {
                shared->sourceId = static_cast<int64_t>(data->sourceId);
                shared->providerId = static_cast<int64_t>(data->providerId);
            }
        });
    LogStateChange(update, L"ReportEdgeGesture");

    if (ReportEdgeGesture_Original) {
        ReportEdgeGesture_Original(self, edge, data, point);
    }
}

using SetAnimationData_t = HRESULT (*)(void*, uint64_t, uint64_t, uint64_t);
SetAnimationData_t SetAnimationData_Original = nullptr;

HRESULT SetAnimationData_Hook(void* self, uint64_t a, uint64_t b, uint64_t c) {
    // Phase 0: a=0, b=SourceId, c=ProviderId at gesture start.
    const uint64_t now = GetTickCount64();
    const SharedUpdate update =
        UpdateShared([b, c, now](StartGestureShared* shared) {
            if (shared->state !=
                static_cast<uint32_t>(StartGestureState::Active)) {
                // A new gesture outside the Started path starts with a clean
                // record rather than the previous gesture's edge/point.
                shared->gestureStartTick = now;
                shared->edge = -1;
                shared->startX = -1.0f;
                shared->startY = -1.0f;
                shared->currentX = -1.0f;
                shared->currentY = -1.0f;
            }
            shared->sourceId = static_cast<int64_t>(b);
            shared->providerId = static_cast<int64_t>(c);
            shared->state = static_cast<uint32_t>(StartGestureState::Active);
        });
    LogStateChange(update, L"SetAnimationData");

    return SetAnimationData_Original ? SetAnimationData_Original(self, a, b, c)
                                     : S_OK;
}

using Show_t = HRESULT (*)(void*, uint8_t, uint8_t, uint32_t);
Show_t Show_Original = nullptr;

// Defined with the hide dispatch below. The dispatch window belongs to the
// thread that owns the manager, so it is created from here, on that thread.
void EnsureHideDispatchWindow();

HRESULT Show_Hook(void* self, uint8_t a, uint8_t b, uint32_t c) {
    // Every Show refreshes the pointer a gesture close will hide with.
    g_managerThis.store(self, std::memory_order_release);
    g_showSerial.fetch_add(1, std::memory_order_relaxed);
    EnsureHideDispatchWindow();

    // Only a Show that follows a real gesture handle publication is a
    // gesture-driven open. A normal mouse/keyboard open must not look like a
    // gesture to the Start side.
    StartGestureShared* sharedState = EnsureShared();
    if (sharedState &&
        (sharedState->sourceId != 0 || sharedState->providerId != 0)) {
        const SharedUpdate update = UpdateShared([](StartGestureShared* shared) {
            if (shared->sourceId != 0 || shared->providerId != 0) {
                shared->state =
                    static_cast<uint32_t>(StartGestureState::Active);
            }
        });
        LogStateChange(update, L"Show (live handle)");
    }

    return Show_Original ? Show_Original(self, a, b, c) : S_OK;
}

using Hide_t = HRESULT (*)(void*, uint32_t);
Hide_t Hide_Original = nullptr;

// Clears the published gesture record. Called by the Hide hook for a hide the
// shell decided, and by the request path for a hide this mod performs, so both
// closes leave the same state behind.
void ClearSharedGestureState(const wchar_t* reason) {
    const SharedUpdate update = UpdateShared([](StartGestureShared* shared) {
        shared->state = static_cast<uint32_t>(StartGestureState::None);
        // Drop the handle together with the Start window, so a later
        // non-gesture Show cannot expose an expired handle.
        shared->sourceId = 0;
        shared->providerId = 0;
        shared->gestureStartTick = 0;
        shared->edge = -1;
        shared->startX = -1.0f;
        shared->startY = -1.0f;
        shared->currentX = -1.0f;
        shared->currentY = -1.0f;
    });
    LogStateChange(update, reason);
    g_startPointTick.store(0, std::memory_order_relaxed);
}

// Writes the one command line the Start mod polls for. The write is what makes
// the delay meaningful: when it fails, the panel will not retract and the hide
// runs at once instead of being 350 ms late for nothing.
bool WriteStartRetractCommand() {
    HANDLE file = CreateFileW(kStartDebugFilePath, GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const DWORD length = sizeof(kRetractCommand) - 1;
    const BOOL ok = WriteFile(file, kRetractCommand, length, &written, nullptr);
    CloseHandle(file);
    return ok && written == length;
}

// Last-write time of a file, false when it is missing or a directory. Both
// couplings are timestamp-only: the content is a constant marker, so reading it
// would add a race that the file time already answers.
bool ReadFileWriteTime(const wchar_t* path, FILETIME* writeTime) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    *writeTime = attributes.ftLastWriteTime;
    return true;
}

// True when the Start mod stamped a gesture close less than kCloseModeFreshMs
// ago. A stamp ahead of the local clock counts as fresh: it is the same write,
// read before the clock caught up.
bool GestureCloseStampFresh() {
    FILETIME written{};
    if (!ReadFileWriteTime(kStartCloseModeFilePath, &written)) {
        return false;
    }

    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);

    ULARGE_INTEGER now{};
    now.LowPart = nowFileTime.dwLowDateTime;
    now.HighPart = nowFileTime.dwHighDateTime;
    ULARGE_INTEGER stamp{};
    stamp.LowPart = written.dwLowDateTime;
    stamp.HighPart = written.dwHighDateTime;

    const ULONGLONG age =
        now.QuadPart > stamp.QuadPart ? now.QuadPart - stamp.QuadPart : 0;
    return age <= static_cast<ULONGLONG>(kCloseModeFreshMs) * 10000;
}

// Gives the Start side the time its 260 ms retract needs. The steps stay at
// 10 ms so a mod that is being disabled mid-wait ends the delay at once instead
// of holding the hide for a retract nothing will draw.
void WaitForRetractLead() {
    const ULONGLONG deadline = GetTickCount64() + kHideLeadMs;
    while (!g_disabled.load(std::memory_order_relaxed)) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            break;
        }
        Sleep(static_cast<DWORD>(
            std::min<ULONGLONG>(kHidePollMs, deadline - now)));
    }
}

// ---------------------------------------------------------------------------
// A gesture close asks for the shell's own Hide
//
// The Start mod retracts the panel and writes hide-now.txt. This mod answers
// with the call a Windows-key close makes, on the thread that owns the manager.
// The manager object is shell state, so the call must not run on the worker: the
// first Show/Hide on the shell's UI thread creates a message-only window there
// and the worker only posts to it. When that window cannot be created the worker
// calls Hide itself and says so in the log.
// ---------------------------------------------------------------------------

// Hands the shell's Start toggle back in step with the window.
//
// Measured: the shell keeps that toggle in the key handler, not in Hide. After a
// programmatic Hide the window is cloaked, but the next Windows press still
// calls Hide instead of Show, so the first press after a gesture close is spent
// on an already hidden menu. One Windows tap afterwards re-arms the toggle and
// is invisible, because in that state the handler's answer is exactly the Hide
// this mod just performed. A keyboard close needs none of this: its own key did
// it.
//
// The tap runs from the worker, after the window is gone, so the shell's UI
// thread is never held for it.
constexpr DWORD kToggleRearmDelayMs = 120;

std::atomic<bool> g_lastHideOk{false};

void TapWindowsKey() {
    keybd_event(VK_LWIN, 0, 0, 0);
    Sleep(60);
    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
}

void RearmStartToggle(uint32_t showSerial) {
    Sleep(kToggleRearmDelayMs);
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }
    // A Show in the meantime is a menu the user opened again; its own Show put
    // the toggle in step, and a tap here would close it.
    if (g_showSerial.load(std::memory_order_relaxed) != showSerial) {
        FileLog(L"hide toggle rearm skipped (reopened)");
        return;
    }
    // A failed Hide left the menu on screen: the tap would close it the way the
    // user expects, but it would also hide a menu this close never took away.
    if (!g_lastHideOk.load(std::memory_order_relaxed)) {
        FileLog(L"hide toggle rearm skipped (hide failed)");
        return;
    }

    TapWindowsKey();
    FileLog(L"hide toggle rearmed");
}

// Calls the shell's Hide for the cached manager. `thread` is the log's label for
// where the call ran, so a worker-thread hide is visible in the log.
bool CallShellHide(uint32_t reason, const wchar_t* thread) {
    if (g_disabled.load(std::memory_order_relaxed)) {
        return false;
    }

    void* manager = g_managerThis.load(std::memory_order_acquire);
    if (!manager) {
        FileLog(L"hide request skipped (no manager)");
        return false;
    }

    const Hide_t original = Hide_Original;
    if (!original) {
        FileLog(L"hide request skipped (no original)");
        return false;
    }

    ClearSharedGestureState(L"Hide (request)");
    const HRESULT hr = original(manager, reason);
    g_lastHideOk.store(SUCCEEDED(hr), std::memory_order_relaxed);
    FileLog(L"hide requested reason=%u thread=%s hr=%08X",
            static_cast<unsigned int>(reason), thread,
            static_cast<unsigned int>(hr));
    return true;
}

LRESULT CALLBACK HideDispatchProc(HWND hwnd, UINT message, WPARAM wParam,
                                  LPARAM lParam) {
    switch (message) {
        case kHideDispatchMessage:
            CallShellHide(static_cast<uint32_t>(wParam), L"ui");
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

// Runs on the thread that calls Show/Hide: the shell's UI thread, which owns the
// manager. One attempt per process; a failure leaves the worker as the caller.
void EnsureHideDispatchWindow() {
    if (g_dispatchHwnd.load(std::memory_order_acquire) ||
        g_dispatchCreateAttempted.exchange(true)) {
        return;
    }

    HMODULE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = HideDispatchProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kHideDispatchClass;
    if (!RegisterClassExW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        FileLog(L"hide dispatch class failed err=%u",
                static_cast<unsigned int>(GetLastError()));
        return;
    }

    HWND hwnd = CreateWindowExW(0, kHideDispatchClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd) {
        FileLog(L"hide dispatch window failed err=%u",
                static_cast<unsigned int>(GetLastError()));
        return;
    }

    g_dispatchHwnd.store(hwnd, std::memory_order_release);
    FileLog(L"hide dispatch thread=%u",
            static_cast<unsigned int>(GetCurrentThreadId()));
}

// Runs on the worker. A posted request is logged and executed by the window proc;
// the toggle re-arm follows either path.
void RequestHideNow() {
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }

    if (!g_managerThis.load(std::memory_order_acquire)) {
        FileLog(L"hide request skipped (no manager)");
        return;
    }

    g_lastHideOk.store(false, std::memory_order_relaxed);
    // Recorded before the post: a Show after this point means the user opened
    // the menu again, and the re-arm tap must not close it.
    const uint32_t showSerial = g_showSerial.load(std::memory_order_relaxed);

    const HWND hwnd = g_dispatchHwnd.load(std::memory_order_acquire);
    if (!hwnd ||
        !PostMessageW(hwnd, kHideDispatchMessage, kHideReasonGesture, 0)) {
        CallShellHide(kHideReasonGesture, L"worker");
    }

    RearmStartToggle(showSerial);
}

// The hide-now.txt poll. The file keeps its last content, so a newer last-write
// time is the whole request; the stamp that exists when the worker starts is
// recorded first, so an earlier session's request is not replayed.
DWORD WINAPI HideRequestWorker(LPVOID) {
    FILETIME lastWrite{};
    bool haveLast = ReadFileWriteTime(kStartHideNowFilePath, &lastWrite);

    for (;;) {
        if (WaitForSingleObject(g_hideWorkerStop, kHideRequestPollMs) !=
            WAIT_TIMEOUT) {
            break;
        }

        FILETIME written{};
        if (!ReadFileWriteTime(kStartHideNowFilePath, &written)) {
            continue;
        }
        if (haveLast && CompareFileTime(&written, &lastWrite) <= 0) {
            continue;
        }

        lastWrite = written;
        haveLast = true;
        RequestHideNow();
    }

    return 0;
}

void StartHideRequestWorker() {
    if (g_hideWorker) {
        return;
    }

    g_hideWorkerStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hideWorkerStop) {
        FileLog(L"hide worker event failed err=%u",
                static_cast<unsigned int>(GetLastError()));
        return;
    }

    g_hideWorker =
        CreateThread(nullptr, 0, HideRequestWorker, nullptr, 0, nullptr);
    if (!g_hideWorker) {
        FileLog(L"hide worker thread failed err=%u",
                static_cast<unsigned int>(GetLastError()));
        CloseHandle(g_hideWorkerStop);
        g_hideWorkerStop = nullptr;
        return;
    }

    FileLog(L"hide worker started");
}

// CStartExperienceManager::Hide(void* self, uint32_t reason) -> HRESULT
// twinui.pcshell.dll 26100.9949, RVA 0x3C5960, vtable slot after Show.
//
// The prologue stores rcx (this) and edx (reason), zeroes dil and compares
// `edx` with 7; r8 and r9 are written before they are read, so the function
// takes two arguments. Both return paths set eax: 0 on success, the negative
// HRESULT of a failing inner call otherwise, so the return type is HRESULT.
HRESULT Hide_Hook(void* self, uint32_t reason) {
    // Every Hide refreshes the pointer a later gesture close will use.
    g_managerThis.store(self, std::memory_order_release);
    EnsureHideDispatchWindow();

    ClearSharedGestureState(L"Hide");

    // The Windows-key close is decided here, while the Start panel is still on
    // screen. The Start side owns the visual. A hide that follows a gesture
    // close is already animated: the close-mode stamp names it, so the hide runs
    // at once. Anything else is a Windows-key close: the debug line starts the
    // panel's 260 ms slide and the wait covers the poll that reads the file
    // (measured 13-67 ms, a 50 ms WM_TIMER quantised to 62 ms) with room for the
    // whole animation.
    //
    // This hook always calls the original. An earlier version returned S_OK to
    // cancel a hide when the user pulled the menu open again; measured, the
    // shell's Start toggle then stayed one close ahead and the Windows key and
    // the taskbar Start button stopped working until the host restarted. The
    // cancel belongs on the Start side, where a close is a request that is
    // simply not written.
    if (g_hideDelayEnabled.load(std::memory_order_relaxed) &&
        !g_disabled.load(std::memory_order_relaxed)) {
        if (GestureCloseStampFresh()) {
            FileLog(L"hide passed (gesture)");
        } else if (WriteStartRetractCommand()) {
            FileLog(L"hide delayed reason=%u", reason);
            WaitForRetractLead();
        }
    }

    return Hide_Original ? Hide_Original(self, reason) : S_OK;
}

// ---------------------------------------------------------------------------
// Hook installation
// ---------------------------------------------------------------------------

bool InstallHook(void* target, void* hook, void** original,
                 const wchar_t* name) {
    if (!target) {
        Wh_Log(L"Cannot hook %s: null target", name);
        return false;
    }
    if (!Wh_SetFunctionHook(target, hook, original)) {
        Wh_Log(L"Failed to hook %s", name);
        return false;
    }
    Wh_Log(L"Hooked %s", name);
    return true;
}

void InstallExplorerHooks(HMODULE module) {
    if (!module) {
        return;
    }
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }
    // One attempt per process: a mismatch is not retried on every module load.
    if (g_explorerHooked.exchange(true)) {
        return;
    }
    if (!TwinuiBuildMatches(module)) {
        // The gate logged the mismatch; no hook is installed at all.
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const auto at = [base](uintptr_t preferredVa) -> void* {
        return reinterpret_cast<void*>(base +
                                       (preferredVa - kTwinuiPreferredBase));
    };

    InstallHook(
        at(kTouchEdgeGestureOperationStartedVa),
        reinterpret_cast<void*>(InputGestureOperationStarted_Hook),
        reinterpret_cast<void**>(&InputGestureOperationStarted_Original),
        L"TouchEdgeGestureProcessor::InputGestureOperationStarted");

    InstallHook(
        at(kTouchEdgeGestureOperationEndedVa),
        reinterpret_cast<void*>(InputGestureOperationEnded_Hook),
        reinterpret_cast<void**>(&InputGestureOperationEnded_Original),
        L"TouchEdgeGestureProcessor::InputGestureOperationEnded");

    InstallHook(
        at(kReportEdgeGestureVa),
        reinterpret_cast<void*>(ReportEdgeGesture_Hook),
        reinterpret_cast<void**>(&ReportEdgeGesture_Original),
        L"EdgeAnimationRelationshipManager::ReportEdgeGesture");

    InstallHook(at(kSetAnimationDataVa),
                reinterpret_cast<void*>(SetAnimationData_Hook),
                reinterpret_cast<void**>(&SetAnimationData_Original),
                L"CStartExperienceManager::SetAnimationData");

    InstallHook(at(kShowVa), reinterpret_cast<void*>(Show_Hook),
                reinterpret_cast<void**>(&Show_Original),
                L"CStartExperienceManager::Show");

    if (InstallHook(at(kHideVa), reinterpret_cast<void*>(Hide_Hook),
                    reinterpret_cast<void**>(&Hide_Original),
                    L"CStartExperienceManager::Hide")) {
        g_hideDelayEnabled.store(true, std::memory_order_relaxed);
    }
}

void TryInstallFromLoadedModules(bool apply) {
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }

    bool installed = false;
    if (!g_explorerHooked.load(std::memory_order_relaxed)) {
        HMODULE twinui = GetModuleHandleW(kTwinuiModuleName);
        if (twinui) {
            InstallExplorerHooks(twinui);
            installed = true;
        }
    }

    if (installed && apply && g_afterInit.load(std::memory_order_relaxed)) {
        Wh_ApplyHookOperations();
    }
}

// ---------------------------------------------------------------------------
// Late load of twinui.pcshell.dll
// ---------------------------------------------------------------------------

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original = nullptr;

HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR fileName, HANDLE file,
                                   DWORD flags) {
    HMODULE module = LoadLibraryExW_Original
                         ? LoadLibraryExW_Original(fileName, file, flags)
                         : nullptr;

    if (module && !g_explorerHooked.load(std::memory_order_relaxed)) {
        if (module == GetModuleHandleW(kTwinuiModuleName)) {
            TryInstallFromLoadedModules(true);
        }
    }

    return module;
}

}  // namespace

BOOL Wh_ModInit() {
    Wh_Log(L"Start Menu Touch Gesture Bridge 0.6.0 initializing");

    // Create the objects up front so the first gesture never pays for it.
    EnsureShared();

    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibraryExW = kernelBase
                              ? reinterpret_cast<LoadLibraryExW_t>(
                                    GetProcAddress(kernelBase,
                                                   "LoadLibraryExW"))
                              : nullptr;
    if (!loadLibraryExW) {
        Wh_Log(L"LoadLibraryExW not found; twinui.pcshell.dll hooks may not "
               L"be installed");
    } else if (!Wh_SetFunctionHook(
                   reinterpret_cast<void*>(loadLibraryExW),
                   reinterpret_cast<void*>(LoadLibraryExW_Hook),
                   reinterpret_cast<void**>(&LoadLibraryExW_Original))) {
        Wh_Log(L"Failed to hook LoadLibraryExW; twinui.pcshell.dll hooks may "
               L"not be installed");
    } else {
        Wh_Log(L"Hooked LoadLibraryExW");
    }

    TryInstallFromLoadedModules(false);

    return TRUE;
}

void Wh_ModAfterInit() {
    g_afterInit = true;
    TryInstallFromLoadedModules(false);
    Wh_ApplyHookOperations();
    StartHideRequestWorker();
}

void Wh_ModBeforeUninit() {
    // Stop hook registration and writes before Windhawk removes the hooks.
    // Wh_SetFunctionHook must not be called after this function returns.
    g_disabled = true;
    g_startPointTick.store(0, std::memory_order_relaxed);

    // The worker is the only code that can still call into the mod on its own.
    if (g_hideWorkerStop) {
        SetEvent(g_hideWorkerStop);
    }
    if (g_hideWorker) {
        WaitForSingleObject(g_hideWorker, 2000);
        CloseHandle(g_hideWorker);
        g_hideWorker = nullptr;
    }
    if (g_hideWorkerStop) {
        CloseHandle(g_hideWorkerStop);
        g_hideWorkerStop = nullptr;
    }

    // The dispatch window belongs to the shell's UI thread.
    const HWND hwnd = g_dispatchHwnd.exchange(nullptr);
    if (hwnd) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    PublishInactiveOnUnload();
}

void Wh_ModUninit() {
    // g_sharedMapping, its view and g_sharedEvent are intentionally kept for
    // the rest of the process lifetime: an in-flight hook may still be using
    // the view, and unmapping it here could crash explorer. Process exit
    // reclaims the handles.
}
