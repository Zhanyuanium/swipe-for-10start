// ==WindhawkMod==
// @id              start-menu-touch-gesture-bridge
// @name            Start Menu Touch Gesture Bridge
// @description     Publishes the live Win10 Start edge-gesture handle and state from explorer for the StartMenuExperienceHost side
// @version         0.4.0
// @author          doubao
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -I"C:\Users\doubao\source\repos\swipe-for-10start\mods"
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Start Menu Touch Gesture Bridge (Phase 1, explorer side)

Explorer-side half of the touch-swipe bridge for the ExplorerPatcher legacy
Windows 10 Start menu.

The mod observes the taskbar-edge gesture path in `twinui.pcshell.dll`, then
publishes the live `GestureAnimationData` handle (source/provider id), the
edge, the start point and the gesture state to shared memory:

- mapping: `Local\WindhawkStartGestureShared` (`StartGestureShared`)
- event:   `Local\WindhawkStartGestureEvent` (auto-reset, signalled on write)

A StartMenuExperienceHost-side consumer could read the mapping and bind a
Composition animation with `InputAnimationTarget::GetInputAnimationObject`; the
shipping Start mod does not read it. Its live coupling is the `retract` debug
line described below.

Every hooked function is forwarded unchanged except for one deliberate delay:
`Hide` waits 350 ms so the Start side can retract the panel first (see below).
The mod creates no threads and makes no COM or process-enumeration calls.

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
- A Windows-key close is animated together with the Start side. `Hide` is
  delayed by 350 ms, and before the delay the mod writes `retract=1` to the
  Start mod's `debug.txt`; that mod retracts its composition `Progress` to 0 in
  260 ms, so the panel slides out instead of vanishing. One `hide delayed`
  line per delayed hide. The delay runs only after the `Hide` hook was
  installed, and it never exceeds 350 ms. When the write fails, the hide runs
  immediately: a delay without a retract would only slow the close down.
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
 *   on the shared-memory contract in start_gesture_shared.h.
 * [OUTPUT]: the `Local\WindhawkStartGestureShared` mapping and its auto-reset
 *   event, holding the edge, the gesture handle and the state under a seqlock;
 *   a `bridge.log` line per state transition; and one `retract=1` line in the
 *   Start mod's `debug.txt` followed by a 350 ms delay in
 *   CStartExperienceManager::Hide, so a Windows-key close slides the panel out
 *   before the window is hidden. No exported symbols.
 * [POS]: explorer-side half of the legacy Start gesture work. It is the only
 *   code that observes the shell's own hide decision; it changes one shell
 *   behaviour (the hide is 350 ms late) and otherwise forwards every hooked
 *   call unchanged. The shipping Start mod does not read the shared memory; the
 *   debug line is the only coupling that is live today.
 * [PROTOCOL]: Update this header on change, then check AGENTS.md
 */

#include <windows.h>
#include <winver.h>

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
// 50 ms poll ticks. The value is also the hard ceiling: Hide never blocks
// longer than this.
constexpr DWORD kHideLeadMs = 350;

// The Start mod's debug channel. The bridge runs in explorer, so
// Wh_GetModStoragePath would resolve to its own storage directory: the Start
// mod's path is fixed by its id.
constexpr wchar_t kStartDebugFilePath[] =
    L"C:\\ProgramData\\Windhawk\\Engine\\ModsWritable\\mod-storage\\"
    L"local@start-menu-legacy-swipe\\debug.txt";
constexpr char kRetractCommand[] = "retract=1\r\n";

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

HRESULT Show_Hook(void* self, uint8_t a, uint8_t b, uint32_t c) {
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

// CStartExperienceManager::Hide(void* self, uint32_t reason) -> HRESULT
// twinui.pcshell.dll 26100.9949, RVA 0x3C5960, vtable slot after Show.
//
// The prologue stores rcx (this) and edx (reason), zeroes dil and compares
// `edx` with 7; r8 and r9 are written before they are read, so the function
// takes two arguments. Both return paths set eax: 0 on success, the negative
// HRESULT of a failing inner call otherwise, so the return type is HRESULT.
HRESULT Hide_Hook(void* self, uint32_t reason) {
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
    LogStateChange(update, L"Hide");
    g_startPointTick.store(0, std::memory_order_relaxed);

    // The Windows-key close is decided here, while the Start panel is still on
    // screen. The Start side owns the visual, so it retracts first: the debug
    // line starts its 260 ms slide and the sleep covers the poll that reads the
    // file (measured 13-67 ms, a 50 ms WM_TIMER quantised to 62 ms) with room
    // for the whole animation.
    if (g_hideDelayEnabled.load(std::memory_order_relaxed) &&
        !g_disabled.load(std::memory_order_relaxed) &&
        WriteStartRetractCommand()) {
        FileLog(L"hide delayed reason=%u", reason);
        Sleep(kHideLeadMs);
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
    Wh_Log(L"Start Menu Touch Gesture Bridge 0.4.0 initializing");

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
}

void Wh_ModBeforeUninit() {
    // Stop hook registration and writes before Windhawk removes the hooks.
    // Wh_SetFunctionHook must not be called after this function returns.
    // The mod creates no threads, so there is nothing to join.
    g_disabled = true;
    g_startPointTick.store(0, std::memory_order_relaxed);
    PublishInactiveOnUnload();
}

void Wh_ModUninit() {
    // g_sharedMapping, its view and g_sharedEvent are intentionally kept for
    // the rest of the process lifetime: an in-flight hook may still be using
    // the view, and unmapping it here could crash explorer. Process exit
    // reclaims the handles.
}
