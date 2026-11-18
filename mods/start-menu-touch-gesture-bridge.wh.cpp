// ==WindhawkMod==
// @id              start-menu-touch-gesture-bridge
// @name            Start Menu Touch Gesture Bridge
// @description     Publishes the live Win10 Start edge-gesture handle and state from explorer for the StartMenuExperienceHost side
// @version         0.12.0
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

The mod calls no hide of its own and presses no key. The Start side owns both
closes: a keyboard close is the user's Windows tap, a gesture close is a Windows
tap the Start mod sends at the end of its retract. This mod only decides how
long the shell's key handler must wait for the panel.

- A gesture close publishes a plan at its release, and a fresh record marked
  `tapped` immediately before it sends the Windows key. Only the tapped record
  names the Hide that needs no wait, because only it proves the tap is already
  on its way. When that Hide arrives on a tapped record this hook has not
  consumed, the panel is already gone; the hook logs
  `hide passed (gesture)` and calls the original at once. A Hide that arrives
  while only the plan is current is the user's own Windows press during the
  settle, a keyboard close, and gets the lead below.
- A gesture close that a new edge pull cancels before its tap publishes the
  cancelled kind at once, so no Hide in between is ever mistaken for the tap's.
- The close record names no menu, so a tapped record is accepted only when it is
  newer than the sequence this process seeded at attach and was stamped at or
  after the newest open generation it has seen. That alone keeps a tap whose Hide
  never came from passing an unrelated Hide under the next menu; the channel also
  asks the Start side to cancel its stamp when it publishes a new open. Both
  ticks come from one clock, so only a tap and an open inside the same ~16 ms
  tick cannot be ordered: an unconsumed fresh stamp is evidence of correlation,
  not proof of cause, and the plan-versus-tapped distinction is what carries the
  correctness.
- Any other Hide is a keyboard close: the mod publishes the `retract` request on
  the channel and waits for the Start mod's answer with the settle it
  just planned, then holds the shell for exactly that duration plus a 30 ms
  margin. The request carries the menu-open generation the hook saw and a
  deadline, so a request that reaches the Start side late cannot start a settle
  under the menu the user opened next; the hook marks the request done when the
  Hide it held completes. A menu with no published generation sends 0, which is
  never a valid generation and which the Start side refuses. A Start mod that
  cannot answer costs a 100 ms wait and then a fixed 560 ms lead. A request that
  cannot be published skips the wait, because a delay without a retract would
  only slow the close down.

Measured on this build, `Hide` is not the whole close: the shell keeps its Start
toggle in the key handler. Both closes now go through that handler, so the
toggle is always in step and the next Windows press after the close opens the
menu. The old programmatic hide plus a 120 ms re-arm tap is gone; that tap raced
a user press inside its window, which is the bug this protocol removes.

The mod also observes the taskbar-edge gesture path in `twinui.pcshell.dll`,
then publishes the live `GestureAnimationData` handle (source/provider id), the
edge, the start point and the gesture state to shared memory:

- mapping: `Local\WindhawkStartGestureShared` (`StartGestureShared`)
- event:   `Local\WindhawkStartGestureEvent` (auto-reset, signalled on write)

A StartMenuExperienceHost-side consumer could read the mapping and bind a
Composition animation with `InputAnimationTarget::GetInputAnimationObject`; the
shipping Start mod does not read it. Its live couplings are the close stamp and
the retract request/answer pair described below, both in
`Local\SwipeFor10Start_v2_*` (see `swipe_ipc_shared.h`).

Hooked functions are forwarded unchanged except for the keyboard wait described
below. The mod creates no windows, threads or COM objects.

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
  it changed between two reads. The IPC channel uses the same rule per record; a
  single section-wide counter would not work because the two sides write
  different records.
- `edge` is written verbatim from the shell. In the Phase 0 log, a swipe that
  started at (45, 900) on a 1440x900 screen (taskbar on the left) reported
  edge 0, matching `StartGestureEdge::Left`.
- The point is published once per gesture from `ReportEdgeGesture`, in the same
  write as the edge and the handle. There is no per-frame position sampling.
- Each gesture state transition appends one line to `bridge.log` in the mod
  storage folder: `state=... edge=... src=... prov=... <reason>`. The channel to
  the Start mod logs one `ipc open created=... close=...` at init, and one
  `hide open generation unavailable err=...` for a Hide that found no published
  open generation (0 is never a generation).
- The two channels have two lifetimes. The v2 channel (the section mapping and
  view, the mutex and the three events) is closed when the mod is disabled, but
  only after the disable flag keeps new Hides out and the in-flight Hide count
  has drained;
  a Hide that outlives the drain keeps every handle and logs
  `ipc unload residual hides=...; the v2 channel stays until explorer exits`.
  The gesture mapping and its event stay for the rest of the explorer process
  lifetime instead: a consumer maps them independently, and an in-flight gesture
  callback may still hold the view, so unmapping them is unsafe. A reload opens
  a fresh pair and leaves the previous pair to process exit, so those two
  handles per reload are deliberate; process exit reclaims them.

The delayed Hide:

- Both closes are Windows taps, so every Hide this hook sees was decided by the
  shell's own key handler. No manager pointer is cached, no worker exists, and no
  call is made on the shell's behalf. Every Show and Hide logs one line
  (`show a=... b=... c=...`, `hide reason=...`) before the delay decision.
- A gesture close publishes its plan at its release and a tapped record the
  moment its Windows tap goes down; only the tapped one names the tap's own Hide,
  so the hook logs `hide passed (gesture)` and calls the original at once for it.
  A tapped Hide never waits: no publisher in this repository writes a nonzero
  close delay, and the lead that remains is the keyboard close's alone.
- A plan or a cancel never passes a Hide: the tap is the record that carries
  `tapped`, and the plan stays current for the whole settle. A user's Windows
  press during that settle is therefore a keyboard close and gets its own lead,
  and a close a new pull cancels publishes the cancelled kind at once, not at the
  tap's deadline.
- A keyboard close publishes the `retract` request on the same channel and wakes
  the Start mod through its request event. The request names the menu-open
  generation this hook read and a deadline; that mod plans its settle from the
  panel's current `Progress` only while that generation is current and the
  deadline has not passed, then answers with the duration (or the no-settle
  sentinel) under the request's sequence number. When this hook's Hide completes
  it marks the request done, so a request whose close is over cannot start a
  settle under the next menu. The hook waits for that answer, logs
  `hide delayed reason=... retract=... lead=...` and
  holds the original for the answered duration plus 30 ms, so the shell hides
  after the slide has landed instead of cutting it. The wait for the answer is
  bounded at 100 ms; a missing answer or the sentinel falls back to the fixed
  560 ms lead. Both waits step in 10 ms so a mod being disabled ends them at
  once. When the request cannot be published the hide runs immediately: a delay
  without a retract would only slow the close down.
- The original always runs. An earlier version returned `S_OK` without it;
  measured, the shell's Start toggle then stayed one close ahead and the Windows
  key and the taskbar Start button stopped working until
  `StartMenuExperienceHost.exe` restarted.
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
 *   on the shared-memory contract in start_gesture_shared.h; on the channel in
 *   swipe_ipc_shared.h, whose close record carries a tapped gesture stamp (the
 *   tap's own Hide) and whose retract request/answer pair carries the settle
 *   duration a keyboard close needs, bound to the menu-open generation the
 *   request was written for. A generation of 0 means the Start side published no
 *   open, so no request may name it and no close stamp can be bound; both then
 *   fall back to the fixed lead.
 * [OUTPUT]: the `Local\WindhawkStartGestureShared` mapping and its auto-reset
 *   event, holding the edge, the gesture handle and the state under a seqlock;
 *   a `bridge.log` line per state transition; one `retract` request on the Start
 *   mod's channel, marked done when its Hide completes, and a wait in
 *   CStartExperienceManager::Hide for the settle duration the Start mod answers
 *   with plus a 30 ms margin, so a Windows-key close slides the panel out before
 *   the window is hidden; a `hide delayed reason=... retract=... lead=...` line
 *   per keyboard close; a `hide passed (gesture)` line and the original
 *   call at once for a Hide on a tapped stamp that is newer than the attach
 *   watermark and not older than the newest open generation, because the Start
 *   side taps before the shell's key handler hides. The v2 channel is closed on
 *   disable once no Hide is inside it; a Hide that outlives the drain keeps it,
 *   and logs the residual. The gesture mapping keeps the process-lifetime
 *   behaviour below. Every path calls the original Hide; nothing is pressed and
 *   no hide is requested. No exported symbols.
 * [POS]: explorer-side half of the legacy Start gesture work. It observes the
 *   shell's own hide decision and decides whether that Hide still needs the
 *   retract lead. Both closes are Windows taps performed by the Start side, and
 *   a gesture close is tapped only after its retract; the shell's Start toggle is
 *   always updated by its key handler, so this mod needs no manager pointer,
 *   worker thread, timer or re-arm tap. The shipping Start mod does not read the
 *   shared gesture memory; the close record, the open generation and the
 *   retract request/answer pair in the swipe_ipc_shared.h channel are the
 *   couplings that are live today. That channel's handles are released when the
 *   mod can prove no Hide is inside it; the older gesture mapping is kept for
 *   the process lifetime because its consumer maps it independently. An
 *   unconsumed fresh stamp is evidence of correlation, not proof of cause: the
 *   plan-versus-tapped distinction is what makes the gesture path correct, and
 *   the binding only keeps a stamp of a menu that is gone from passing a later
 *   Hide.
 * [PROTOCOL]: Update this header on change, then check AGENTS.md
 */

#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "start_gesture_shared.h"
#include "swipe_ipc_shared.h"

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

// A keyboard close is a Windows tap by the user. Hide is where the shell takes
// the Start window away; the panel needs time to retract, so the hide waits for
// it. The wait is normally the duration the Start side answers with; this fixed
// lead is what remains when no answer arrives: a Start mod that is not running,
// a broken channel, a settle that did not start. A full 900+ DIP panel needs
// 409 ms at the 2.2 DIP/ms the Start mod plans with.
constexpr uint64_t kHideLeadMs = 560;

// How long the answer may take. The request wakes the Start mod's input thread
// through an event, so an answer normally arrives in a few milliseconds; this
// bound only covers a Start side that is busy or gone.
constexpr uint64_t kRetractPlanWaitMs = 100;
// Added to the answered duration: the answer is published a moment after the
// animation starts, and the wait's 10 ms slicing can add one step.
constexpr uint64_t kHideLeadMarginMs = 30;
// The Start side's "no settle started" answer, and the corrupt-value cap.
constexpr int32_t kRetractPlanNoSettleMs = 1;
constexpr uint64_t kRetractPlanMaxMs = 2000;

// How long a published retract request stays valid for the Start side. The
// bridge's own hold after publishing is at most kRetractPlanWaitMs plus the
// fixed lead (650 ms), and it marks the request done when that Hide completes,
// so this deadline is the backstop for a lost marker, not the usual end: a
// request can never reach a settle after the close it was written for is over.
constexpr uint64_t kRetractRequestWindowMs = 700;

// A tapped close is consumed by exactly one Hide. The Start side publishes the
// plan at the release, marks the same record `tapped` with a fresh sequence
// immediately before it sends the Windows key, so the Hide that follows is the
// tap's own and needs no wait. A tapped record older than this window belongs to
// a close that is over, and a tap whose Hide never came leaves its record valid
// for this window and no longer; the plan alone never passes a Hide.
constexpr uint64_t kCloseStampFreshMs = 800;

// The hide waits in 10 ms steps, so the wait can end early when the mod is being
// disabled under it.
constexpr DWORD kHidePollMs = 10;

// How long Wh_ModUninit lets an in-flight Hide leave the v2 channel before it
// keeps the handles instead of closing them. Every wait in the hook ends within
// one kHidePollMs step of the disable flag, so this only covers a hook that the
// system preempted; it is bounded because explorer's unload thread must not be
// held by one call.
constexpr DWORD kIpcUnloadDrainMs = 500;

// The cross-process channel (swipe_ipc_shared.h). Opened once at init. Its
// handles are closed by Wh_ModUninit once no Hide can still be inside them; see
// the two lifetimes there. The gesture mapping and its event, unlike this
// channel, stay for the whole process lifetime.
SwipeIpcChannel g_ipc;
// The highest tapped close this hook has consumed. Seeded from the channel at
// attach, so a tap written before this process belongs to someone else's close.
LONG64 g_handledCloseSeq = 0;

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
// Hides currently inside Hide_Hook, entry to exit, incremented before the hook
// reads anything else. Wh_ModUninit closes the v2 channel only when this is
// zero: g_disabled is set in Wh_ModBeforeUninit first, so a Hide that enters
// after that point skips the channel, and the count names the ones that were
// already inside when the flag went up.
std::atomic<int> g_hideCalls{0};

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
    FileLog(L"show a=%u b=%u c=%u", static_cast<unsigned int>(a),
            static_cast<unsigned int>(b), static_cast<unsigned int>(c));

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

// Clears the published gesture record when the shell hides the Start window.
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

// Gives the Start side the time its retract needs. The steps stay at 10 ms so a
// mod that is being disabled mid-wait ends the delay at once instead of holding
// the hide for a retract nothing will draw.
void WaitForRetractLead(uint64_t leadMs) {
    const ULONGLONG deadline = GetTickCount64() + leadMs;
    while (!g_disabled.load(std::memory_order_relaxed)) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            break;
        }
        Sleep(static_cast<DWORD>(
            std::min<ULONGLONG>(kHidePollMs, deadline - now)));
    }
}

// Consumes the Start side's tapped record when it belongs to this Hide. The
// sequence number is the whole protocol: a tapped record this hook has not seen
// is a gesture close whose Windows tap is going down right now, so this Hide is
// that tap's and needs no wait; a tapped record it has seen is a later keyboard
// close. The plan alone never passes: it is published at the release, up to a
// full settle before the tap exists, so a Hide during that settle is the user's
// own Windows press and gets its own lead. A cancelled record never taps at all.
//
// The record names no menu, so two more tests bind it to one. It must be newer
// than the sequence this process seeded at attach, and it must not be older than
// the newest open generation this hook has seen: a stamp written before that
// open belongs to a menu that is gone, and would otherwise pass an unrelated
// Hide under the menu the user has since opened. The channel also asks the Start
// side to cancel its stamp when it publishes a new open; the test above does not
// depend on it. With no known open generation nothing can be bound, so the stamp
// is refused and the Hide takes the keyboard path.
bool ConsumeGestureStamp(bool openKnown, uint64_t openTick) {
    if (!g_ipc.state || !openKnown) {
        return false;
    }

    // One snapshot, so the kind, the tapped flag and the tick can never come
    // from two different publishes.
    SwipeIpcCloseSnapshot close{};
    if (!SwipeIpcReadClose(&g_ipc, &close)) {
        return false;
    }
    if (close.seq == 0 || close.seq == g_handledCloseSeq) {
        return false;
    }
    if (close.tapped == 0 || close.kind != kSwipeCloseKindGesture) {
        return false;
    }

    const uint64_t now = GetTickCount64();
    if (now > close.tick && now - close.tick > kCloseStampFreshMs) {
        // The tap that wrote this is long over; the Hide in hand is a keyboard
        // close and gets its own lead.
        return false;
    }
    if (close.tick < openTick) {
        // Stamped before the menu now on screen was published: a tap whose Hide
        // never came. The current menu's own tap is always newer than its open.
        return false;
    }

    g_handledCloseSeq = close.seq;
    return true;
}

// Waits for the Start side's answer to this request. Only an answer whose
// sequence number echoes the request counts, so an answer to an earlier close
// can never satisfy this one, and no timestamp is involved. Returns false when
// no answer arrived inside kRetractPlanWaitMs; `settleMs` is left at the
// no-settle sentinel when the Start side answered that nothing started.
bool WaitForRetractPlan(LONG64 request, int32_t* settleMs) {
    if (!g_ipc.state || !g_ipc.retractPlanEvent) {
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() + kRetractPlanWaitMs;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            return false;
        }

        // Slices keep the disabled check: a mod that is being unloaded must not
        // hold the shell's UI thread.
        const DWORD wait = WaitForSingleObject(
            g_ipc.retractPlanEvent,
            static_cast<DWORD>(
                std::min<ULONGLONG>(kHidePollMs, deadline - now)));
        if (wait == WAIT_OBJECT_0) {
            // The plan is copied as one seqlock record and accepted only when it
            // echoes this request, so an answer to an earlier close can never
            // satisfy this one and no half-written plan is read.
            SwipeIpcRetractPlan plan{};
            if (!SwipeIpcReadRetractPlan(&g_ipc, &plan)) {
                continue;
            }
            if (plan.request != request) {
                continue;  // an answer to an earlier request
            }
            *settleMs =
                plan.settleMs > 0 &&
                        static_cast<uint64_t>(plan.settleMs) <= kRetractPlanMaxMs
                    ? plan.settleMs
                    : kRetractPlanNoSettleMs;
            return true;
        }
        if (g_disabled.load(std::memory_order_relaxed)) {
            return false;
        }
        if (wait != WAIT_TIMEOUT) {
            return false;
        }
    }
}
// ---------------------------------------------------------------------------
// Delayed Hide
//
// A gesture close taps the Windows key at the end of its retract, so the Hide
// that tap causes needs no wait: the panel is already gone. The tapped record
// names that case, and only the tap publishes it. A keyboard close publishes the
// `retract` request and then waits for the Start mod's answer: that mod plans the
// settle from the panel's travel and publishes its duration, so the shell is held
// for exactly as long as the slide takes. Without an answer the fixed lead
// applies. No worker, manager pointer, timer or re-arm tap is involved: every
// Hide is the shell's own call, on its own thread.
// ---------------------------------------------------------------------------

// CStartExperienceManager::Hide(void* self, uint32_t reason) -> HRESULT
// twinui.pcshell.dll 26100.9949, RVA 0x3C5960, vtable slot after Show.
//
// The prologue stores rcx (this) and edx (reason), zeroes dil and compares
// `edx` with 7; r8 and r9 are written before they are read, so the function
// takes two arguments. Both return paths set eax: 0 on success, the negative
// HRESULT of a failing inner call otherwise, so the return type is HRESULT.
HRESULT Hide_Hook(void* self, uint32_t reason) {
    // The only module state this hook releases on the way out is g_ipc, so the
    // count covers the whole call: an unload that sees zero knows no Hide can
    // reach the channel or the code that reads it any more.
    g_hideCalls.fetch_add(1, std::memory_order_acq_rel);
    FileLog(L"hide reason=%u", reason);
    ClearSharedGestureState(L"Hide");

    // The shell's key handler decided this hide from a Windows tap. A gesture
    // close publishes its plan at the release and marks the same record tapped
    // immediately before its own Windows tap, so the Hide that tap causes is
    // already too late to cut the animation and runs at once. A keyboard close
    // (no record, a plan or a cancel, a record this hook already consumed, one
    // older than the window, one stamped before the menu now open) requests the
    // retract through the channel and waits for the Start mod's answer. Only that
    // mod knows the panel's travel, so the wait is its planned settle plus one
    // margin; no answer at all falls back to the fixed lead, which covers the
    // full 409 ms slide.
    //
    // This hook always calls the original. An earlier version returned S_OK to
    // cancel a hide when the user pulled the menu open again; measured, the
    // shell's Start toggle then stayed one close ahead and the Windows key and
    // the taskbar Start button stopped working until the host restarted. A close
    // is cancelled on the Start side instead, by not sending its tap.
    if (g_hideDelayEnabled.load(std::memory_order_relaxed) &&
        !g_disabled.load(std::memory_order_relaxed)) {
        // The newest menu open this hook has seen, read once and used by both
        // paths: the stamped Hide is bound to it, and the keyboard path sends it
        // with its request. No published generation (ERROR_NOT_READY), no stable
        // copy, or no channel at all leaves both at 0: the request then carries
        // 0, which the Start side refuses, and no close stamp can pass either,
        // so this Hide takes the fixed lead.
        uint64_t openGeneration = 0;
        uint64_t openTick = 0;
        DWORD openError = 0;
        const bool openKnown =
            SwipeIpcReadOpen(&g_ipc, &openGeneration, &openTick, &openError);
        if (!openKnown) {
            openGeneration = 0;
            openTick = 0;
            if (g_ipc.state) {
                // Only a channel that exists can have a record problem worth
                // naming; without one, `hide retract request failed` already
                // says why this Hide takes the fixed lead.
                FileLog(L"hide open generation unavailable err=%u",
                        static_cast<unsigned int>(openError));
            }
        }

        if (ConsumeGestureStamp(openKnown, openTick)) {
            // The Start side tapped after its retract, so the panel is already
            // gone: no wait belongs to this Hide.
            FileLog(L"hide passed (gesture)");
        } else {
            const LONG64 request = SwipeIpcRequestRetract(
                &g_ipc, openGeneration, kRetractRequestWindowMs);
            if (request != 0) {
                int32_t settleMs = kRetractPlanNoSettleMs;
                const bool answered = WaitForRetractPlan(request, &settleMs);
                const bool planned = answered &&
                                     settleMs > kRetractPlanNoSettleMs;
                const uint64_t lead =
                    planned ? static_cast<uint64_t>(settleMs) +
                                  kHideLeadMarginMs
                            : kHideLeadMs;

                if (planned) {
                    FileLog(L"hide delayed reason=%u retract=%d lead=%llu",
                            reason, settleMs,
                            static_cast<unsigned long long>(lead));
                } else {
                    FileLog(L"hide delayed reason=%u retract=%s lead=%llu",
                            reason, answered ? L"none" : L"unanswered",
                            static_cast<unsigned long long>(lead));
                }
                WaitForRetractLead(lead);
                // This Hide is over. A request the Start side reads from now on
                // belongs to no close of this menu and is marked done here, so it
                // cannot start a settle later.
                SwipeIpcClearRetractRequest(&g_ipc, request);
            } else {
                FileLog(L"hide retract request failed");
            }
        }
    }

    const HRESULT result = Hide_Original ? Hide_Original(self, reason) : S_OK;
    g_hideCalls.fetch_sub(1, std::memory_order_acq_rel);
    return result;
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
    Wh_Log(L"Start Menu Touch Gesture Bridge 0.12.0 initializing");

    // Create the objects up front so the first gesture never pays for it.
    EnsureShared();

    // The channel to the Start side. Opening it here means the section and its
    // events exist before the first Hide, and a Start mod that attaches later
    // joins the same ones. This is the only call: SwipeIpcOpen may sleep for its
    // init budget while the other side finishes a claim, so it must not run on
    // a hook. A failure here means every Hide takes the fixed lead for the life
    // of this attachment, which the budget makes reachable only for a section
    // that cannot be initialised at all.
    bool created = false;
    DWORD ipcError = 0;
    if (!SwipeIpcOpen(&g_ipc, &created, &ipcError)) {
        Wh_Log(L"Swipe IPC channel unavailable (%u); a keyboard close takes the "
               L"fixed lead",
               static_cast<unsigned>(ipcError));
    } else {
        // A tap published before this process attached belongs to a close this
        // hook never saw: seed the watermark so only a later tap can pass.
        SwipeIpcCloseSnapshot close{};
        g_handledCloseSeq = SwipeIpcReadClose(&g_ipc, &close)
                                ? close.seq
                                : SwipeIpcLoad64(&g_ipc.state->closeSeq);
        FileLog(L"ipc open created=%d close=%lld", created ? 1 : 0,
                static_cast<long long>(g_handledCloseSeq));
    }

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
    // This must be set before Windhawk removes the hooks. The Hide hook reads it
    // in its wait loop, so a hide that is being held ends its wait at once. The
    // v2 channel is closed in Wh_ModUninit, after this flag has kept new Hides
    // out of it and g_hideCalls has been drained.
    g_disabled = true;
    g_startPointTick.store(0, std::memory_order_relaxed);

    PublishInactiveOnUnload();
}

void Wh_ModUninit() {
    // Two channels, two lifetimes, for two reasons.
    //
    // The gesture mapping (Local\WindhawkStartGestureShared) and its event are
    // kept for the rest of the explorer process lifetime, on purpose. A consumer
    // maps the section independently and may poll it at any time, and an
    // in-flight gesture callback can still hold the view pointer that
    // EnsureShared cached; unmapping it here would turn either into an access to
    // a freed view. A reload opens a fresh pair for its own module instance and
    // leaves the previous instance's pair to process exit: that is two handles
    // per reload, the price of the safety above. The reset published by
    // PublishInactiveOnUnload is what tells a reader that this instance is gone,
    // not the handle lifetime. Process exit reclaims every pair.
    //
    // The v2 channel is closed, because its objects are named and its records
    // belong to the other process: g_ipc is unmapped and its mapping, mutex and
    // three events are closed once no Hide hook can still be inside them.
    // g_disabled was set in Wh_ModBeforeUninit, so a Hide that enters after it
    // never reaches the channel; g_hideCalls names the Hides that entered
    // before. Their waits end within one kHidePollMs step of that flag, so the
    // bounded drain below normally returns at once. If it does not, the handles
    // stay and the log says so: freeing them while a Hide is inside the channel
    // would be a use-after-close, and the process must exit to reclaim them.
    const ULONGLONG drainDeadline = GetTickCount64() + kIpcUnloadDrainMs;
    while (g_hideCalls.load(std::memory_order_acquire) != 0 &&
           GetTickCount64() < drainDeadline) {
        Sleep(kHidePollMs);
    }

    const int residualHides = g_hideCalls.load(std::memory_order_acquire);
    if (residualHides == 0) {
        if (g_ipc.state) {
            SwipeIpcClose(&g_ipc);
            FileLog(L"ipc close");
        }
    } else {
        FileLog(L"ipc unload residual hides=%d; the v2 channel stays until "
                L"explorer exits",
                residualHides);
        Wh_Log(L"Unload left the v2 channel open: %d Hide hook(s) are still "
               L"inside it",
               residualHides);
    }
}
