// ==WindhawkMod==
// @id              msgprobe
// @name            Start Menu Pointer Message Probe
// @description     Logs pointer messages and polls live pointer state for the legacy Start menu
// @version         0.2.1
// @author          doubao
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Start Menu Pointer Message Probe

Diagnostic-only. Hooks `PeekMessageW` and `GetMessageW` inside
`StartMenuExperienceHost.exe` and logs every touch/pen pointer message
(`WM_POINTERUPDATE` 0x0245 .. 0x024F) together with the position reported by
`GetPointerInfo`. Mouse-in-pointer updates are counted but not logged per
message, so a moving physical mouse cannot exhaust the line cap. Mouse moves
are counted, left button down/up positions are logged once per message.

The pointer id is `GET_POINTERID_WPARAM(wParam)` (LOWORD). Passing the whole
`wParam` to `GetPointerInfo` fails with `ERROR_INVALID_PARAMETER`.

One gesture summary per process is written 1.2 s after the first touch/pen
message: `gesture summary ptr=... touchptr=... mouseptr=... mousemove=...`.

On `WM_POINTERDOWN` for a touch contact, the mod starts a poller thread. Every
~8 ms it calls `GetPointerInfo` for that pointer id, so the probe measures
whether the input stack keeps the contact's position alive even when the
message queue receives no further pointer messages. Samples stop when
`POINTER_FLAG_INCONTACT` clears, after 8 s, or when a new touch down arrives.
On stop it writes `pollsum` and then dumps the pointer frame history with
`GetPointerFrameInfoHistory` (falling back to `GetPointerFrameTouchInfo`).

`GetPointerInfo` is documented as returning state for the most recent pointer
message retrieved by the calling thread, so the poller thread itself always
fails with `ERROR_INVALID_PARAMETER`. The down therefore also starts
owning-thread samples at the top of `PeekMessageW`/`GetMessageW`, before the
next message is retrieved. Those lines use the `uipoll`, `uipollsum` and
`uihist` prefixes.

The log is appended to
`C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\local@msgprobe\msgprobe.log`
and mirrored to the Windhawk log. Writing stops after 3000 lines.
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr wchar_t kLogDir[] =
    L"C:\\ProgramData\\Windhawk\\Engine\\ModsWritable\\mod-storage\\"
    L"local@msgprobe";
constexpr wchar_t kLogPath[] =
    L"C:\\ProgramData\\Windhawk\\Engine\\ModsWritable\\mod-storage\\"
    L"local@msgprobe\\msgprobe.log";
constexpr int kMaxLines = 3000;

// Pointer message range: WM_POINTERUPDATE .. WM_POINTERHWHEEL.
constexpr UINT kPointerFirst = 0x0245;
constexpr UINT kPointerLast = 0x024F;

SRWLOCK g_logLock = SRWLOCK_INIT;
HANDLE g_logFile = INVALID_HANDLE_VALUE;  // closed after every write
int g_logLineCount = 0;
bool g_capLogged = false;
thread_local bool t_logging = false;

std::atomic<uint32_t> g_pointerCount{0};
std::atomic<uint32_t> g_touchPointerCount{0};
std::atomic<uint32_t> g_mousePointerCount{0};
std::atomic<uint32_t> g_mouseMoveCount{0};
std::atomic<uint32_t> g_otherCount{0};
std::atomic<uint32_t> g_messageCount{0};
std::atomic<bool> g_firstMessageLogged{false};
std::atomic<bool> g_gestureSummaryStarted{false};
HANDLE g_summaryThread = nullptr;

// Caller holds g_logLock.
void AppendToFileLocked(const char* data, int length) {
    if (g_logFile == INVALID_HANDLE_VALUE) {
        g_logFile = CreateFileW(kLogPath, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    if (g_logFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_logFile, data, static_cast<DWORD>(length), &written,
                  nullptr);
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}

void LogLine(const char* body) {
    if (t_logging) {
        return;
    }
    t_logging = true;

    char line[1024];
    int length = snprintf(line, sizeof(line), "[msgprobe][%lu:%lu][%llu] %s",
                          GetCurrentProcessId(), GetCurrentThreadId(),
                          static_cast<unsigned long long>(GetTickCount64()),
                          body);
    if (length < 0) {
        t_logging = false;
        return;
    }
    if (length >= static_cast<int>(sizeof(line))) {
        length = sizeof(line) - 1;
    }

    WCHAR wide[1024];
    int wideLength = MultiByteToWideChar(CP_ACP, 0, line, length, wide,
                                         ARRAYSIZE(wide) - 1);
    if (wideLength > 0) {
        wide[wideLength] = L'\0';
        Wh_Log(L"%s", wide);
    }

    AcquireSRWLockExclusive(&g_logLock);
    if (g_logLineCount >= kMaxLines) {
        if (!g_capLogged) {
            g_capLogged = true;
            const char cap[] = "cap reached";
            Wh_Log(L"cap reached");
            AppendToFileLocked(cap, static_cast<int>(strlen(cap)));
        }
        ReleaseSRWLockExclusive(&g_logLock);
        t_logging = false;
        return;
    }
    ++g_logLineCount;
    AppendToFileLocked(line, length);

    char newline[2] = {'\r', '\n'};
    AppendToFileLocked(newline, 2);
    ReleaseSRWLockExclusive(&g_logLock);

    t_logging = false;
}

void LogFormat(const char* format, ...) {
    char body[768];
    va_list args;
    va_start(args, format);
    vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    LogLine(body);
}

// ---------------------------------------------------------------------------
// Pointer state poller
//
// The queue consumed WM_POINTERDOWN and never delivered UPDATE/UP. Polling
// GetPointerInfo from a separate thread answers whether the input stack still
// holds a live contact with changing coordinates while the finger is down.
// ---------------------------------------------------------------------------

constexpr int kPollMaxSamples = 600;
constexpr int kPollMaxLogLines = 200;
constexpr DWORD kPollIntervalMs = 8;
constexpr ULONGLONG kPollTimeoutMs = 8000;
constexpr UINT32 kPointerFlagInContact = 0x00000004;
constexpr int kHistoryMaxEntries = 64;
constexpr UINT kPointerDownMessage = 0x0246;

struct PollSample {
    int x;
    int y;
    UINT32 flags;
    UINT32 t;
};

struct PollRequest {
    UINT32 id;
    UINT32 generation;
    ULONGLONG startTick;
};

std::atomic<uint32_t> g_pollId{0};
std::atomic<uint32_t> g_pollGeneration{0};
std::atomic<bool> g_pollStop{false};
std::atomic<bool> g_shuttingDown{false};
HANDLE g_pollThread = nullptr;
SRWLOCK g_pollLock = SRWLOCK_INIT;

bool IsValidPointerId(UINT32 id) {
    return id != 0 && id != 0xFFFFFFFFu;
}

template <typename T>
T ResolvePointerApi(const char* name) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        user32 = LoadLibraryW(L"user32.dll");
    }
    T proc = user32 ? reinterpret_cast<T>(GetProcAddress(user32, name))
                    : nullptr;
    if (!proc) {
        HMODULE apiSet = GetModuleHandleW(
            L"api-ms-win-rtcore-ntuser-wmpointer-l1-1-0.dll");
        if (!apiSet) {
            apiSet = LoadLibraryW(
                L"api-ms-win-rtcore-ntuser-wmpointer-l1-1-0.dll");
        }
        if (apiSet) {
            proc = reinterpret_cast<T>(GetProcAddress(apiSet, name));
        }
    }
    return proc;
}

void DumpPointerHistory(UINT32 id, const char* tag, const char* touchTag) {
    if (!IsValidPointerId(id)) {
        return;
    }

    using GetPointerFrameInfoHistory_t =
        BOOL(WINAPI*)(UINT32, UINT32*, UINT32*, POINTER_INFO*);
    auto history = ResolvePointerApi<GetPointerFrameInfoHistory_t>(
        "GetPointerFrameInfoHistory");
    if (history) {
        POINTER_INFO frames[kHistoryMaxEntries]{};
        UINT32 entries = kHistoryMaxEntries;
        UINT32 pointerCount = 0;
        SetLastError(0);
        if (history(id, &entries, &pointerCount, frames)) {
            if (entries > kHistoryMaxEntries) {
                entries = kHistoryMaxEntries;
            }
            LogFormat("%s count=%u entries=%u", tag,
                      static_cast<unsigned>(pointerCount),
                      static_cast<unsigned>(entries));
            for (UINT32 i = 0; i < entries; ++i) {
                LogFormat("%s %u x=%d y=%d pf=0x%08X", tag,
                          static_cast<unsigned>(i),
                          static_cast<int>(frames[i].ptPixelLocation.x),
                          static_cast<int>(frames[i].ptPixelLocation.y),
                          static_cast<unsigned>(frames[i].pointerFlags));
            }
            return;
        }
        LogFormat("%s fail err=%u", tag,
                  static_cast<unsigned>(GetLastError()));
    } else {
        LogFormat("%s fail err=0", tag);
    }

    // Older builds may expose only the touch variant. Its first member is
    // POINTER_INFO, so the log lines use the same fields.
    using GetPointerFrameTouchInfo_t =
        BOOL(WINAPI*)(UINT32, UINT32*, POINTER_TOUCH_INFO*);
    auto touch = ResolvePointerApi<GetPointerFrameTouchInfo_t>(
        "GetPointerFrameTouchInfo");
    if (touch) {
        POINTER_TOUCH_INFO frames[kHistoryMaxEntries]{};
        UINT32 count = kHistoryMaxEntries;
        SetLastError(0);
        if (touch(id, &count, frames)) {
            if (count > kHistoryMaxEntries) {
                count = kHistoryMaxEntries;
            }
            LogFormat("%s count=%u", touchTag, static_cast<unsigned>(count));
            for (UINT32 i = 0; i < count; ++i) {
                LogFormat("%s %u x=%d y=%d pf=0x%08X", touchTag,
                          static_cast<unsigned>(i),
                          static_cast<int>(
                              frames[i].pointerInfo.ptPixelLocation.x),
                          static_cast<int>(
                              frames[i].pointerInfo.ptPixelLocation.y),
                          static_cast<unsigned>(
                              frames[i].pointerInfo.pointerFlags));
            }
        } else {
            LogFormat("%s fail err=%u", touchTag,
                      static_cast<unsigned>(GetLastError()));
        }
    } else {
        LogFormat("%s fail err=0", touchTag);
    }
}

DWORD WINAPI PollerThread(LPVOID param) {
    PollRequest* request = static_cast<PollRequest*>(param);
    const UINT32 id = request->id;
    const UINT32 generation = request->generation;
    const ULONGLONG startTick = request->startTick;
    delete request;

    // Only one poller runs at a time: a new down waits for the previous thread
    // to exit before creating the next one.
    static PollSample samples[kPollMaxSamples];

    int sampleCount = 0;
    int pollCount = 0;
    int okCount = 0;
    int logCount = 0;
    bool haveSample = false;
    bool haveMoveRef = false;
    int firstX = 0;
    int firstY = 0;
    int lastX = 0;
    int lastY = 0;
    int minX = 0;
    int maxX = 0;
    int lastMoveX = 0;
    int lastMoveY = 0;

    for (;;) {
        if (g_shuttingDown.load(std::memory_order_relaxed) ||
            g_pollStop.load(std::memory_order_relaxed) ||
            g_pollGeneration.load(std::memory_order_relaxed) != generation ||
            g_pollId.load(std::memory_order_relaxed) != id) {
            break;
        }

        const ULONGLONG now = GetTickCount64();
        const UINT32 elapsed = static_cast<UINT32>(now - startTick);
        if (elapsed > kPollTimeoutMs) {
            break;
        }

        // Defensive guard: never ask the input stack about a null pointer id.
        if (!IsValidPointerId(id)) {
            LogFormat("poll invalid id=%u", static_cast<unsigned>(id));
            break;
        }

        POINTER_INFO info{};
        if (!GetPointerInfo(id, &info)) {
            LogFormat("pollfail t=%u err=%u", static_cast<unsigned>(elapsed),
                      static_cast<unsigned>(GetLastError()));
            break;
        }

        ++okCount;
        ++pollCount;
        const int x = static_cast<int>(info.ptPixelLocation.x);
        const int y = static_cast<int>(info.ptPixelLocation.y);
        const UINT32 flags = static_cast<UINT32>(info.pointerFlags);

        if (sampleCount < kPollMaxSamples) {
            samples[sampleCount].x = x;
            samples[sampleCount].y = y;
            samples[sampleCount].flags = flags;
            samples[sampleCount].t = elapsed;
            ++sampleCount;
        }

        if (!haveSample) {
            haveSample = true;
            firstX = x;
            firstY = y;
            minX = x;
            maxX = x;
        }
        lastX = x;
        lastY = y;
        if (x < minX) {
            minX = x;
        }
        if (x > maxX) {
            maxX = x;
        }

        bool moved = !haveMoveRef;
        if (haveMoveRef) {
            const int dx = x - lastMoveX;
            const int dy = y - lastMoveY;
            moved = dx * dx + dy * dy >= 4;
        }
        if (moved) {
            lastMoveX = x;
            lastMoveY = y;
            haveMoveRef = true;
        }

        if (logCount < kPollMaxLogLines &&
            (moved || pollCount % 25 == 0)) {
            LogFormat("poll t=%u x=%d y=%d pf=0x%08X",
                      static_cast<unsigned>(elapsed), x, y,
                      static_cast<unsigned>(flags));
            ++logCount;
        }

        if ((flags & kPointerFlagInContact) == 0) {
            break;
        }

        Sleep(kPollIntervalMs);
    }

    const int span = haveSample ? maxX - minX : 0;
    LogFormat(
        "pollsum n=%d ok=%d first=(%d,%d) last=(%d,%d) minx=%d maxx=%d "
        "span=%d",
        sampleCount, okCount, firstX, firstY, lastX, lastY, minX, maxX,
        span);

    DumpPointerHistory(id, "hist", "histtouch");
    return 0;
}

// ---------------------------------------------------------------------------
// Owning-thread pointer sampling
//
// GetPointerInfo returns the state associated with the most recent pointer
// message retrieved by the calling thread. A separate thread therefore fails
// with ERROR_INVALID_PARAMETER even when the contact is alive. These samples
// run at the top of the message hooks, on the thread that retrieved
// WM_POINTERDOWN, before the next message is retrieved.
// ---------------------------------------------------------------------------

bool g_uiPollActive = false;
DWORD g_uiPollThreadId = 0;
UINT32 g_uiPollId = 0;
ULONGLONG g_uiPollStartTick = 0;
int g_uiPollSampleCount = 0;
int g_uiPollOkCount = 0;
int g_uiPollLogCount = 0;
bool g_uiPollHaveSample = false;
int g_uiPollFirstX = 0;
int g_uiPollFirstY = 0;
int g_uiPollLastX = 0;
int g_uiPollLastY = 0;
int g_uiPollMinX = 0;
int g_uiPollMaxX = 0;
int g_uiPollLastSampleX = 0;
int g_uiPollLastSampleY = 0;

void StopUiPointerSession(bool dumpHistory) {
    if (!g_uiPollActive) {
        return;
    }
    g_uiPollActive = false;

    const int span = g_uiPollHaveSample ? g_uiPollMaxX - g_uiPollMinX : 0;
    LogFormat(
        "uipollsum n=%d ok=%d first=(%d,%d) last=(%d,%d) minx=%d maxx=%d "
        "span=%d",
        g_uiPollSampleCount, g_uiPollOkCount, g_uiPollFirstX, g_uiPollFirstY,
        g_uiPollLastX, g_uiPollLastY, g_uiPollMinX, g_uiPollMaxX, span);

    if (dumpHistory && GetCurrentThreadId() == g_uiPollThreadId) {
        DumpPointerHistory(g_uiPollId, "uihist", "uihisttouch");
    }
}

void StartUiPointerSession(UINT32 id) {
    StopUiPointerSession(false);

    g_uiPollId = id;
    g_uiPollThreadId = GetCurrentThreadId();
    g_uiPollStartTick = GetTickCount64();
    g_uiPollSampleCount = 0;
    g_uiPollOkCount = 0;
    g_uiPollLogCount = 0;
    g_uiPollHaveSample = false;
    g_uiPollFirstX = 0;
    g_uiPollFirstY = 0;
    g_uiPollLastX = 0;
    g_uiPollLastY = 0;
    g_uiPollMinX = 0;
    g_uiPollMaxX = 0;
    g_uiPollLastSampleX = 0;
    g_uiPollLastSampleY = 0;
    g_uiPollActive = true;
}

// Runs before every PeekMessageW/GetMessageW call. The previous retrieved
// message's pointer context is still current here, so the down's id can be
// queried even when the queue delivers nothing else.
void SampleUiPointer() {
    if (!g_uiPollActive) {
        return;
    }
    // Only the thread that retrieved the down owns the pointer message
    // context; other message loops in the process would fail with error 87.
    if (GetCurrentThreadId() != g_uiPollThreadId) {
        return;
    }
    if (g_shuttingDown.load(std::memory_order_relaxed)) {
        StopUiPointerSession(false);
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const UINT32 elapsed = static_cast<UINT32>(now - g_uiPollStartTick);
    if (elapsed > kPollTimeoutMs) {
        StopUiPointerSession(true);
        return;
    }

    if (!IsValidPointerId(g_uiPollId)) {
        LogFormat("uipollfail t=%u err=invalid",
                  static_cast<unsigned>(elapsed));
        StopUiPointerSession(false);
        return;
    }

    POINTER_INFO info{};
    if (!GetPointerInfo(g_uiPollId, &info)) {
        LogFormat("uipollfail t=%u err=%u", static_cast<unsigned>(elapsed),
                  static_cast<unsigned>(GetLastError()));
        StopUiPointerSession(true);
        return;
    }

    ++g_uiPollOkCount;
    ++g_uiPollSampleCount;

    const int x = static_cast<int>(info.ptPixelLocation.x);
    const int y = static_cast<int>(info.ptPixelLocation.y);
    const UINT32 flags = static_cast<UINT32>(info.pointerFlags);

    bool moved = !g_uiPollHaveSample;
    if (g_uiPollHaveSample) {
        const int dx = x - g_uiPollLastSampleX;
        const int dy = y - g_uiPollLastSampleY;
        moved = dx * dx + dy * dy >= 4;
    }

    if (!g_uiPollHaveSample) {
        g_uiPollHaveSample = true;
        g_uiPollFirstX = x;
        g_uiPollFirstY = y;
        g_uiPollMinX = x;
        g_uiPollMaxX = x;
    }
    g_uiPollLastX = x;
    g_uiPollLastY = y;
    g_uiPollLastSampleX = x;
    g_uiPollLastSampleY = y;
    if (x < g_uiPollMinX) {
        g_uiPollMinX = x;
    }
    if (x > g_uiPollMaxX) {
        g_uiPollMaxX = x;
    }

    if (g_uiPollLogCount < kPollMaxLogLines &&
        (moved || g_uiPollSampleCount % 25 == 0)) {
        LogFormat("uipoll t=%u x=%d y=%d pf=0x%08X",
                  static_cast<unsigned>(elapsed), x, y,
                  static_cast<unsigned>(flags));
        ++g_uiPollLogCount;
    }

    if ((flags & kPointerFlagInContact) == 0) {
        StopUiPointerSession(true);
    }
}

void StopActivePoller() {
    g_pollStop.store(true, std::memory_order_relaxed);
    g_pollGeneration.fetch_add(1, std::memory_order_relaxed);

    HANDLE thread = nullptr;
    AcquireSRWLockExclusive(&g_pollLock);
    thread = g_pollThread;
    g_pollThread = nullptr;
    ReleaseSRWLockExclusive(&g_pollLock);

    if (thread) {
        WaitForSingleObject(thread, 2000);
        CloseHandle(thread);
    }
}

void StartPoller(UINT32 id, int x, int y, UINT32 flags) {
    LogFormat("polldown id=%u x=%d y=%d flags=0x%08X",
              static_cast<unsigned>(id), x, y, static_cast<unsigned>(flags));

    if (!IsValidPointerId(id)) {
        LogFormat("poll invalid id=%u", static_cast<unsigned>(id));
        return;
    }
    if (g_shuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    // Start the owning-thread samples first: they must run before the other
    // poller's stop wait can block this thread.
    StartUiPointerSession(id);

    // A new down replaces the previous session. The old poller notices the
    // generation change, writes its summary and history, and exits.
    StopActivePoller();
    if (g_shuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    g_pollId.store(id, std::memory_order_relaxed);
    g_pollStop.store(false, std::memory_order_relaxed);
    const UINT32 generation =
        g_pollGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

    auto* request = new PollRequest{id, generation, GetTickCount64()};
    HANDLE thread = CreateThread(nullptr, 0, PollerThread, request, 0, nullptr);
    if (!thread) {
        const DWORD error = GetLastError();
        delete request;
        LogFormat("poll thread start failed err=%u",
                  static_cast<unsigned>(error));
        return;
    }

    AcquireSRWLockExclusive(&g_pollLock);
    g_pollThread = thread;
    ReleaseSRWLockExclusive(&g_pollLock);

    // Wh_ModBeforeUninit may have run between the check above and the handle
    // assignment; stop the fresh thread instead of leaking it.
    if (g_shuttingDown.load(std::memory_order_relaxed)) {
        StopActivePoller();
    }
}


struct CounterSnapshot {
    uint32_t pointer;
    uint32_t touchPointer;
    uint32_t mousePointer;
    uint32_t mouseMove;
    uint32_t other;
};

uint32_t CounterValue(const std::atomic<uint32_t>& counter) {
    return counter.load(std::memory_order_relaxed);
}

DWORD WINAPI GestureSummaryThread(LPVOID param) {
    const CounterSnapshot base = *static_cast<CounterSnapshot*>(param);
    delete static_cast<CounterSnapshot*>(param);

    Sleep(1200);

    LogFormat(
        "gesture summary ptr=%d touchptr=%d mouseptr=%d mousemove=%d "
        "other=%d",
        static_cast<int>(CounterValue(g_pointerCount) - base.pointer),
        static_cast<int>(CounterValue(g_touchPointerCount) - base.touchPointer),
        static_cast<int>(CounterValue(g_mousePointerCount) - base.mousePointer),
        static_cast<int>(CounterValue(g_mouseMoveCount) - base.mouseMove),
        static_cast<int>(CounterValue(g_otherCount) - base.other));
    return 0;
}

// One summary per process, 1.2 s after the first touch/pen pointer message.
void StartGestureSummary() {
    if (g_gestureSummaryStarted.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    auto* base = new CounterSnapshot{
        CounterValue(g_pointerCount),
        CounterValue(g_touchPointerCount),
        CounterValue(g_mousePointerCount),
        CounterValue(g_mouseMoveCount),
        CounterValue(g_otherCount),
    };

    // Keep the handle open: Wh_ModUninit waits for the thread before the DLL
    // can be unloaded.
    g_summaryThread = CreateThread(nullptr, 0, GestureSummaryThread, base, 0,
                                   nullptr);
    if (!g_summaryThread) {
        delete base;
    }
}

void LogFirstMessage(HWND hwnd) {
    if (g_firstMessageLogged.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    WCHAR className[256] = L"?";
    if (hwnd) {
        GetClassNameW(hwnd, className, ARRAYSIZE(className));
    }

    char classNameNarrow[256] = "?";
    WideCharToMultiByte(CP_ACP, 0, className, -1, classNameNarrow,
                        sizeof(classNameNarrow), nullptr, nullptr);

    LogFormat("firsthwnd=%p class=%s", static_cast<void*>(hwnd),
              classNameNarrow);
}

void HandleMessage(const MSG* msg) {
    if (!msg) {
        return;
    }

    const UINT message = msg->message;
    if (message < kPointerFirst || message > kPointerLast) {
        if (message == WM_MOUSEMOVE) {
            g_mouseMoveCount.fetch_add(1, std::memory_order_relaxed);
        } else if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
            LogFormat("mouse msg=0x%04X x=%d y=%d hwnd=%p", message,
                      GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam),
                      static_cast<void*>(msg->hwnd));
        } else {
            g_otherCount.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        g_pointerCount.fetch_add(1, std::memory_order_relaxed);

        // WM_POINTER wParam: LOWORD is the pointer id, HIWORD is the message
        // flags. Passing the whole wParam to GetPointerInfo fails with
        // ERROR_INVALID_PARAMETER.
        const UINT32 id = GET_POINTERID_WPARAM(msg->wParam);
        const int msgX = GET_X_LPARAM(msg->lParam);
        const int msgY = GET_Y_LPARAM(msg->lParam);
        POINTER_INFO info{};
        if (GetPointerInfo(id, &info)) {
            if (info.pointerType == PT_MOUSE) {
                // Mouse-in-pointer updates are continuous while the real
                // mouse moves; count them but keep them out of the log so the
                // touch messages stay within the line cap.
                g_mousePointerCount.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_touchPointerCount.fetch_add(1, std::memory_order_relaxed);
                StartGestureSummary();
                LogFormat(
                    "ptr msg=0x%04X id=%u type=%d x=%d y=%d hwnd=%p src=%d "
                    "flags=0x%08X msgxy=%d,%d wflags=0x%04X",
                    message, id, static_cast<int>(info.pointerType),
                    static_cast<int>(info.ptPixelLocation.x),
                    static_cast<int>(info.ptPixelLocation.y),
                    static_cast<void*>(msg->hwnd),
                    static_cast<int>(
                        reinterpret_cast<intptr_t>(info.sourceDevice)),
                    static_cast<unsigned>(info.pointerFlags), msgX, msgY,
                    static_cast<unsigned>(HIWORD(msg->wParam)));
                if (message == kPointerDownMessage &&
                    info.pointerType == PT_TOUCH) {
                    StartPoller(
                        id, static_cast<int>(info.ptPixelLocation.x),
                        static_cast<int>(info.ptPixelLocation.y),
                        static_cast<UINT32>(info.pointerFlags));
                }
            }
        } else {
            LogFormat(
                "ptr msg=0x%04X id=%u getinfo=0x%08X hwnd=%p msgxy=%d,%d "
                "wflags=0x%04X",
                message, id, static_cast<unsigned>(GetLastError()),
                static_cast<void*>(msg->hwnd), msgX, msgY,
                static_cast<unsigned>(HIWORD(msg->wParam)));
        }
    }

    LogFirstMessage(msg->hwnd);

    const uint32_t total =
        g_messageCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (total % 200 == 0) {
        LogFormat("counters pointer=%d touchptr=%d mouseptr=%d mousemove=%d "
                  "other=%d",
                  static_cast<int>(CounterValue(g_pointerCount)),
                  static_cast<int>(CounterValue(g_touchPointerCount)),
                  static_cast<int>(CounterValue(g_mousePointerCount)),
                  static_cast<int>(CounterValue(g_mouseMoveCount)),
                  static_cast<int>(CounterValue(g_otherCount)));
    }
}

using PeekMessageW_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
PeekMessageW_t PeekMessageW_Original = nullptr;

BOOL WINAPI PeekMessageW_Hook(LPMSG msg, HWND hwnd, UINT filterMin,
                              UINT filterMax, UINT removeMsg) {
    SampleUiPointer();
    const BOOL result =
        PeekMessageW_Original
            ? PeekMessageW_Original(msg, hwnd, filterMin, filterMax, removeMsg)
            : FALSE;
    if (result && msg && msg->message) {
        HandleMessage(msg);
    }
    return result;
}

using GetMessageW_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
GetMessageW_t GetMessageW_Original = nullptr;

BOOL WINAPI GetMessageW_Hook(LPMSG msg, HWND hwnd, UINT filterMin,
                             UINT filterMax) {
    SampleUiPointer();
    const BOOL result = GetMessageW_Original
                            ? GetMessageW_Original(msg, hwnd, filterMin,
                                                   filterMax)
                            : FALSE;
    if (result && result != static_cast<BOOL>(-1) && msg && msg->message) {
        HandleMessage(msg);
    }
    return result;
}

}  // namespace

BOOL Wh_ModInit() {
    CreateDirectoryW(kLogDir, nullptr);
    LogFormat("msgprobe init pid=%u",
              static_cast<unsigned>(GetCurrentProcessId()));

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        user32 = LoadLibraryW(L"user32.dll");
    }

    void* peekMessage = user32
                            ? reinterpret_cast<void*>(
                                  GetProcAddress(user32, "PeekMessageW"))
                            : nullptr;
    if (peekMessage) {
        const BOOL ok = Wh_SetFunctionHook(
            peekMessage, reinterpret_cast<void*>(PeekMessageW_Hook),
            reinterpret_cast<void**>(&PeekMessageW_Original));
        if (!ok) {
            LogFormat("hook PeekMessageW failed error=%u",
                      static_cast<unsigned>(GetLastError()));
        }
    } else {
        LogFormat("PeekMessageW not found");
    }

    void* getMessage = user32
                           ? reinterpret_cast<void*>(
                                 GetProcAddress(user32, "GetMessageW"))
                           : nullptr;
    if (getMessage) {
        const BOOL ok = Wh_SetFunctionHook(
            getMessage, reinterpret_cast<void*>(GetMessageW_Hook),
            reinterpret_cast<void**>(&GetMessageW_Original));
        if (!ok) {
            LogFormat("hook GetMessageW failed error=%u",
                      static_cast<unsigned>(GetLastError()));
        }
    } else {
        LogFormat("GetMessageW not found");
    }

    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_shuttingDown.store(true, std::memory_order_relaxed);
    StopActivePoller();
}

void Wh_ModUninit() {
    // Idempotent: Wh_ModBeforeUninit normally did this already.
    StopActivePoller();

    if (g_summaryThread) {
        WaitForSingleObject(g_summaryThread, 3000);
        CloseHandle(g_summaryThread);
        g_summaryThread = nullptr;
    }
}
