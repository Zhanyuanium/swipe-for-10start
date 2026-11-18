// ==WindhawkMod==
// @id              inputprobe
// @name            Start Menu Input Probe
// @description     Subclasses the CoreMessaging input windows and decodes raw touch HID reports
// @version         0.2.0
// @author          Zhanyuanium
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -luser32 -lhid -Wall -Wextra
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Start Menu Input Probe

Diagnostic-only mod for the legacy Start menu on `StartMenuExperienceHost.exe`.
It observes the two input paths that a `PeekMessageW`/`GetMessageW` hook cannot
see.

## Probe A: window subclassing

Every 250 ms for 120 s the mod enumerates all top-level windows, all child
windows and all message-only windows of the process. It logs each distinct
`hwnd=%p class=%s` (40 lines) and subclasses every
`SystemUserAdapterWindowClass` / `Windows.UI.Core.CoreWindow` window (8 slots).
The subclass logs only `WM_POINTERDOWN` and `WM_POINTERCAPTURECHANGED`, one
line per message, with the `GetPointerInfo` position.

## Probe B: raw input and HID decoding

A dedicated thread owns a message-only window and registers
`RAWINPUTDEVICE{ page 0x0D, usage 0x04 }` with `RIDEV_INPUTSINK`. It then
enumerates the raw input devices, keeps the touch digitizers (page 0x0D,
usage 0x04), and reads each preparsed data blob with `RIDI_PREPARSEDDATA`.
No device is opened.

Once per device the mod logs
`hidcaps dev=... page=... usage=... inlen=... nvalues=... ncollections=...`
and `hidcoll dev=... n=... first=...`, where `n` counts the 0x0D/0x05
(Contact) and 0x0D/0x22 (Finger) contact collections. Up to four devices and
five contact collections per device are cached.

Every input report is decoded with `HidP_GetUsageValue` and logged as
`hid dev=<path> id=<report id> x=<X> y=<Y> contacts=<contact count>`
(400 lines). Each contact collection adds
`hidc dev=... coll=... x=... y=... inrange=...` (200 lines). `inrange` is
In Range (0x32) when present, otherwise Tip Switch (0x42), otherwise a live
Contact Identifier (0x51), Width/Height (0x48/0x49) or non-zero X/Y.
`contacts` uses the top-level Contact Count (0x54) when the device declares
it. A device without contact collections decodes collection 0 and logs
`hidc dev=... coll=0 x=... y=...`. Every 100 reports the mod writes
`hidcount reports=... devs=...`.

The raw input listener stops after 900 s (15 min) or on `Wh_ModBeforeUninit`.
The log is truncated on `Wh_ModInit`, so every run starts clean.

The log is `...\mod-storage\local@inputprobe\inputprobe.log`.
*/
// ==/WindhawkModReadme==

/*
 * [INPUT]: depends on EnumWindows, EnumChildWindows, FindWindowExW,
 *   SetWindowLongPtrW, GetPointerInfo, RegisterRawInputDevices,
 *   GetRawInputData, GetRawInputDeviceList and GetRawInputDeviceInfoW from
 *   user32; on HidP_GetCaps, HidP_GetLinkCollectionNodes and
 *   HidP_GetUsageValue from hid.dll.
 * [OUTPUT]: writes inputprobe.log; exports no symbols.
 * [POS]: isolated diagnostic probe for StartMenuExperienceHost.exe. It is a
 *   sibling of msgprobe (message queue) and start-menu-touch-gesture-probe
 *   (callbacks): it observes the window procedure and the raw HID stream,
 *   the two paths that a queue hook cannot see.
 * [PROTOCOL]: Update this header on change, then check AGENTS.md
 */

#include <windows.h>
#include <windowsx.h>

#include <hidusage.h>
#include <hidpi.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------

constexpr wchar_t kLogDir[] =
    L"C:\\ProgramData\\Windhawk\\Engine\\ModsWritable\\mod-storage\\"
    L"local@inputprobe";
constexpr wchar_t kLogPath[] =
    L"C:\\ProgramData\\Windhawk\\Engine\\ModsWritable\\mod-storage\\"
    L"local@inputprobe\\inputprobe.log";
constexpr int kMaxTotalLines = 12000;

SRWLOCK g_logLock = SRWLOCK_INIT;
HANDLE g_logFile = INVALID_HANDLE_VALUE;  // closed after every write
int g_logLineCount = 0;
bool g_capLogged = false;
thread_local bool t_logging = false;

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

    char line[1200];
    int length = snprintf(line, sizeof(line), "[inputprobe][%lu:%lu][%llu] %s",
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

    WCHAR wide[1200];
    int wideLength = MultiByteToWideChar(CP_ACP, 0, line, length, wide,
                                         ARRAYSIZE(wide) - 1);
    if (wideLength > 0) {
        wide[wideLength] = L'\0';
        Wh_Log(L"%s", wide);
    }

    AcquireSRWLockExclusive(&g_logLock);
    if (g_logLineCount >= kMaxTotalLines) {
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

    const char newline[2] = {'\r', '\n'};
    AppendToFileLocked(newline, 2);
    ReleaseSRWLockExclusive(&g_logLock);

    t_logging = false;
}

void LogFormat(const char* format, ...) {
    char body[1000];
    va_list args;
    va_start(args, format);
    vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    LogLine(body);
}

void CopyWideToNarrow(const WCHAR* source, char* target, int targetSize) {
    if (targetSize <= 0) {
        return;
    }
    target[0] = '\0';
    WideCharToMultiByte(CP_ACP, 0, source, -1, target, targetSize, nullptr,
                        nullptr);
}

// Takes one line from a per-category budget in a racy-safe way.
bool TakeLine(std::atomic<int>& used, int cap) {
    int current = used.load(std::memory_order_relaxed);
    while (current < cap) {
        if (used.compare_exchange_weak(current, current + 1,
                                       std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

std::atomic<bool> g_stopping{false};

// ---------------------------------------------------------------------------
// Probe A: window enumeration and subclassing
//
// The touch stream of the legacy Start is not visible in the message queue
// after WM_POINTERDOWN. Two delivery paths remain: posted pointer messages
// dispatched to a window procedure, and SendMessage calls from the
// CoreMessaging input window (SystemUserAdapterWindowClass) to the CoreWindow.
// A window procedure subclass sees both.
// ---------------------------------------------------------------------------

constexpr UINT kPointerDown = 0x0246;           // WM_POINTERDOWN
constexpr UINT kPointerCaptureChanged = 0x024C; // WM_POINTERCAPTURECHANGED
constexpr int kMaxSubclass = 8;
constexpr int kMaxScannedWindows = 96;
constexpr int kMaxLoggedWindows = 64;
constexpr int kEnumLogCap = 40;
constexpr int kScanLogCap = 12;

struct SubclassEntry {
    HWND hwnd;
    WNDPROC original;
};

SubclassEntry g_subclass[kMaxSubclass];
std::atomic<int> g_subclassCount{0};
SRWLOCK g_subclassLock = SRWLOCK_INIT;
std::atomic<bool> g_subclassFullLogged{false};

std::atomic<int> g_enumLogLines{0};
std::atomic<int> g_scanLogLines{0};

HWND g_loggedWindows[kMaxLoggedWindows];
int g_loggedWindowCount = 0;

HANDLE g_scanThread = nullptr;

LRESULT CALLBACK WindowSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam);

bool IsInputWindowClass(const WCHAR* className) {
    return wcscmp(className, L"SystemUserAdapterWindowClass") == 0 ||
           wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0;
}

bool IsWindowLogged(HWND hwnd) {
    for (int i = 0; i < g_loggedWindowCount; ++i) {
        if (g_loggedWindows[i] == hwnd) {
            return true;
        }
    }
    return false;
}

void MarkWindowLogged(HWND hwnd) {
    if (g_loggedWindowCount < kMaxLoggedWindows) {
        g_loggedWindows[g_loggedWindowCount++] = hwnd;
    }
}

// Installs the subclass once per hwnd. Holds the exclusive lock across
// SetWindowLongPtrW so WindowSubclassProc can never run before its entry is
// published.
void SubclassInputWindow(HWND hwnd, const WCHAR* className) {
    AcquireSRWLockExclusive(&g_subclassLock);

    const int count = g_subclassCount.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        if (g_subclass[i].hwnd == hwnd) {
            ReleaseSRWLockExclusive(&g_subclassLock);
            return;
        }
    }

    int slot = -1;
    if (count < kMaxSubclass) {
        slot = count;
    } else {
        for (int i = 0; i < count; ++i) {
            if (!IsWindow(g_subclass[i].hwnd)) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        ReleaseSRWLockExclusive(&g_subclassLock);
        if (!g_subclassFullLogged.exchange(true)) {
            char classNameNarrow[128];
            CopyWideToNarrow(className, classNameNarrow,
                             sizeof(classNameNarrow));
            LogFormat("subclass full hwnd=%p class=%s", static_cast<void*>(hwnd),
                      classNameNarrow);
        }
        return;
    }

    SetLastError(0);
    const LONG_PTR previous = SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(WindowSubclassProc));
    const DWORD error = GetLastError();

    if (previous == 0 && error != 0) {
        ReleaseSRWLockExclusive(&g_subclassLock);
        char classNameNarrow[128];
        CopyWideToNarrow(className, classNameNarrow, sizeof(classNameNarrow));
        LogFormat("subclass fail hwnd=%p class=%s err=%u",
                  static_cast<void*>(hwnd), classNameNarrow,
                  static_cast<unsigned>(error));
        return;
    }

    g_subclass[slot].hwnd = hwnd;
    g_subclass[slot].original = reinterpret_cast<WNDPROC>(previous);
    if (slot == count) {
        g_subclassCount.store(count + 1, std::memory_order_release);
    }

    ReleaseSRWLockExclusive(&g_subclassLock);

    char classNameNarrow[128];
    CopyWideToNarrow(className, classNameNarrow, sizeof(classNameNarrow));
    LogFormat("subclass hwnd=%p class=%s old=%p slot=%d",
              static_cast<void*>(hwnd), classNameNarrow,
              reinterpret_cast<void*>(previous), slot);
}

LRESULT CALLBACK WindowSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
    WNDPROC original = nullptr;
    AcquireSRWLockShared(&g_subclassLock);
    const int count = g_subclassCount.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        if (g_subclass[i].hwnd == hwnd) {
            original = g_subclass[i].original;
            break;
        }
    }
    ReleaseSRWLockShared(&g_subclassLock);

    // Only the two lifecycle messages matter here. Logging every pointer
    // update would bury the HID stream under hundreds of lines.
    if (msg == kPointerDown || msg == kPointerCaptureChanged) {
        const UINT32 id = GET_POINTERID_WPARAM(wParam);
        POINTER_INFO info{};
        if (GetPointerInfo(id, &info)) {
            LogFormat(
                "sub hwnd=%p msg=0x%04X id=%u x=%d y=%d flags=0x%08X",
                static_cast<void*>(hwnd), msg, id,
                static_cast<int>(info.ptPixelLocation.x),
                static_cast<int>(info.ptPixelLocation.y),
                static_cast<unsigned>(info.pointerFlags));
        } else {
            LogFormat("sub hwnd=%p msg=0x%04X id=%u err=%u",
                      static_cast<void*>(hwnd), msg, id,
                      static_cast<unsigned>(GetLastError()));
        }
    }

    if (original) {
        return CallWindowProcW(original, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct ScanState {
    DWORD pid;
    int count;
    HWND windows[kMaxScannedWindows];
    WCHAR classNames[kMaxScannedWindows][96];
};

void AddScannedWindow(ScanState* state, HWND hwnd) {
    for (int i = 0; i < state->count; ++i) {
        if (state->windows[i] == hwnd) {
            return;
        }
    }
    if (state->count >= kMaxScannedWindows) {
        return;
    }

    const int index = state->count++;
    state->windows[index] = hwnd;
    state->classNames[index][0] = L'\0';
    if (!GetClassNameW(hwnd, state->classNames[index],
                       ARRAYSIZE(state->classNames[index]))) {
        wcscpy(state->classNames[index], L"?");
    }
}

BOOL CALLBACK EnumChildWindowProc(HWND hwnd, LPARAM param) {
    auto* state = reinterpret_cast<ScanState*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == state->pid) {
        AddScannedWindow(state, hwnd);
    }
    return TRUE;
}

BOOL CALLBACK EnumTopWindowProc(HWND hwnd, LPARAM param) {
    auto* state = reinterpret_cast<ScanState*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == state->pid) {
        AddScannedWindow(state, hwnd);
        // EnumChildWindows walks the whole descendant tree, not only the
        // direct children.
        EnumChildWindows(hwnd, EnumChildWindowProc, param);
    }
    return TRUE;
}

// Message-only windows are not returned by EnumWindows; they are children of
// HWND_MESSAGE. SystemUserAdapterWindowClass may live there.
void AddMessageOnlyWindows(ScanState* state) {
    HWND hwnd = nullptr;
    for (;;) {
        hwnd = FindWindowExW(HWND_MESSAGE, hwnd, nullptr, nullptr);
        if (!hwnd) {
            break;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == state->pid) {
            AddScannedWindow(state, hwnd);
        }
    }
}

void LogWindowOnce(HWND hwnd, const WCHAR* className) {
    if (IsWindowLogged(hwnd)) {
        return;
    }
    MarkWindowLogged(hwnd);
    if (!TakeLine(g_enumLogLines, kEnumLogCap)) {
        return;
    }
    char classNameNarrow[128];
    CopyWideToNarrow(className, classNameNarrow, sizeof(classNameNarrow));
    LogFormat("hwnd=%p class=%s", static_cast<void*>(hwnd), classNameNarrow);
}

void ScanProcessWindows(int scan, DWORD pid) {
    ScanState state{};
    state.pid = pid;

    EnumWindows(EnumTopWindowProc, reinterpret_cast<LPARAM>(&state));
    AddMessageOnlyWindows(&state);

    // Target windows first: they must survive the 40-line enumeration cap.
    int targetCount = 0;
    for (int i = 0; i < state.count; ++i) {
        if (IsInputWindowClass(state.classNames[i])) {
            ++targetCount;
            LogWindowOnce(state.windows[i], state.classNames[i]);
            SubclassInputWindow(state.windows[i], state.classNames[i]);
        }
    }
    for (int i = 0; i < state.count; ++i) {
        if (!IsInputWindowClass(state.classNames[i])) {
            LogWindowOnce(state.windows[i], state.classNames[i]);
        }
    }

    if (scan % 40 == 0 && TakeLine(g_scanLogLines, kScanLogCap)) {
        LogFormat("scan n=%d windows=%d targets=%d", scan, state.count,
                  targetCount);
    }
}

DWORD WINAPI WindowScanThread(LPVOID) {
    const DWORD pid = GetCurrentProcessId();
    LogFormat("scan start pid=%u", static_cast<unsigned>(pid));

    // 480 scans * 250 ms = 120 s. The Start menu is created after
    // Wh_ModAfterInit, so a one-shot scan would miss every window.
    int scans = 0;
    for (; scans < 480 && !g_stopping.load(std::memory_order_relaxed);
         ++scans) {
        ScanProcessWindows(scans, pid);
        Sleep(250);
    }

    LogFormat("scan stop scans=%d", scans);
    return 0;
}

// ---------------------------------------------------------------------------
// Probe B: raw input from the touch digitizer
//
// Raw input is delivered per process and independent of the pointer message
// queue. The preparsed data comes from RIDI_PREPARSEDDATA, so a virtual
// digitizer that cannot be opened is still decodable. HidP_GetUsageValue
// resolves X/Y and In Range inside each 0x0D/0x05 contact collection; no byte
// offset is hard-coded.
// ---------------------------------------------------------------------------

constexpr wchar_t kHidWindowClass[] = L"WindhawkInputProbeWindow";
constexpr int kHidDecodeLogCap = 400;
constexpr int kHidContactLogCap = 200;
constexpr int kHidMaxDevices = 4;
constexpr int kHidMaxContactCollections = 5;
constexpr int kHidMaxLinkNodes = 32;
constexpr int kHidPathChars = 512;
constexpr unsigned kHidListenSeconds = 900;  // 15 min
constexpr ULONGLONG kHidListenMs =
    static_cast<ULONGLONG>(kHidListenSeconds) * 1000;

constexpr USAGE kUsagePageGeneric = 0x01;
constexpr USAGE kUsagePageDigitizer = 0x0D;
constexpr USAGE kUsageTouchScreen = 0x04;
constexpr USAGE kUsageContact = 0x05;
constexpr USAGE kUsageGenericX = 0x30;
constexpr USAGE kUsageGenericY = 0x31;
constexpr USAGE kUsageInRange = 0x32;
constexpr USAGE kUsageTipSwitch = 0x42;
constexpr USAGE kUsageFinger = 0x22;
constexpr USAGE kUsageWidth = 0x48;
constexpr USAGE kUsageHeight = 0x49;
constexpr USAGE kUsageContactId = 0x51;
constexpr USAGE kUsageContactCount = 0x54;

HANDLE g_hidStopEvent = nullptr;
HANDLE g_hidThread = nullptr;
std::atomic<HWND> g_hidHwnd{nullptr};
std::atomic<DWORD> g_hidThreadId{0};
std::atomic<uint32_t> g_hidReportCount{0};
std::atomic<int> g_hidDecodeLines{0};
std::atomic<int> g_hidContactLines{0};
std::atomic<bool> g_hidCacheFullLogged{false};
int g_hidRegisteredUsage = -1;  // owned by the raw input thread

struct HidDevice {
    HANDLE handle;
    bool ready;
    char path[kHidPathChars];
    PHIDP_PREPARSED_DATA preparsed;
    HIDP_CAPS caps;
    USHORT contactNodes[kHidMaxContactCollections];
    int contactNodeCount;       // every 0x0D/0x05 collection in the descriptor
    int decodedContactCount;    // min(contactNodeCount, 5)
};

HidDevice g_hidDevices[kHidMaxDevices] = {};
int g_hidDeviceCount = 0;  // owned by the raw input thread

// Reads the device path and the preparsed blob without opening the device.
// Logs the caps and the contact collections once.
void ReadHidDeviceInfo(HidDevice* device) {
    UINT chars = 0;
    SetLastError(0);
    GetRawInputDeviceInfoW(device->handle, RIDI_DEVICENAME, nullptr, &chars);
    if (chars > 0) {
        WCHAR* name = new (std::nothrow) WCHAR[chars + 1];
        if (name) {
            name[0] = L'\0';
            UINT fetched = chars;
            SetLastError(0);
            if (GetRawInputDeviceInfoW(device->handle, RIDI_DEVICENAME, name,
                                       &fetched) != static_cast<UINT>(-1)) {
                name[chars] = L'\0';
                CopyWideToNarrow(name, device->path, kHidPathChars);
            }
            delete[] name;
        }
    }
    if (device->path[0] == '\0') {
        snprintf(device->path, sizeof(device->path), "?");
    }

    UINT bytes = 0;
    SetLastError(0);
    GetRawInputDeviceInfoW(device->handle, RIDI_PREPARSEDDATA, nullptr,
                           &bytes);
    if (bytes == 0 || bytes > 64 * 1024) {
        LogFormat("hid prep fail dev=%s size=%u err=%u", device->path,
                  static_cast<unsigned>(bytes),
                  static_cast<unsigned>(GetLastError()));
        return;
    }

    BYTE* blob = new (std::nothrow) BYTE[bytes];
    if (!blob) {
        return;
    }

    UINT fetched = bytes;
    SetLastError(0);
    const UINT result = GetRawInputDeviceInfoW(
        device->handle, RIDI_PREPARSEDDATA, blob, &fetched);
    if (result == static_cast<UINT>(-1) || fetched == 0) {
        LogFormat("hid prep fail dev=%s size=%u err=%u", device->path,
                  static_cast<unsigned>(bytes),
                  static_cast<unsigned>(GetLastError()));
        delete[] blob;
        return;
    }

    device->preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(blob);

    HIDP_CAPS caps{};
    const NTSTATUS capsStatus = HidP_GetCaps(device->preparsed, &caps);
    if (capsStatus != HIDP_STATUS_SUCCESS) {
        LogFormat("hid caps fail dev=%s st=0x%08X", device->path,
                  static_cast<unsigned>(capsStatus));
        delete[] blob;
        device->preparsed = nullptr;
        return;
    }

    device->caps = caps;
    device->ready = true;
    LogFormat(
        "hidcaps dev=%s page=0x%04X usage=0x%04X inlen=%u nvalues=%u "
        "ncollections=%u",
        device->path, static_cast<unsigned>(caps.UsagePage),
        static_cast<unsigned>(caps.Usage),
        static_cast<unsigned>(caps.InputReportByteLength),
        static_cast<unsigned>(caps.NumberInputValueCaps),
        static_cast<unsigned>(caps.NumberLinkCollectionNodes));

    HIDP_LINK_COLLECTION_NODE nodes[kHidMaxLinkNodes]{};
    ULONG nodeCount = kHidMaxLinkNodes;
    const NTSTATUS nodeStatus =
        HidP_GetLinkCollectionNodes(nodes, &nodeCount, device->preparsed);

    int total = 0;
    USHORT first = 0;
    if (nodeStatus == HIDP_STATUS_SUCCESS) {
        if (nodeCount > static_cast<ULONG>(kHidMaxLinkNodes)) {
            nodeCount = kHidMaxLinkNodes;
        }
        for (ULONG i = 0; i < nodeCount; ++i) {
            // Standard touch digitizers declare contact collections as
            // 0x0D/0x05; the ELAN2513 and the virtual digitizer declare them
            // as 0x0D/0x22 (Finger). Both carry X/Y per contact.
            if (nodes[i].LinkUsagePage != kUsagePageDigitizer ||
                (nodes[i].LinkUsage != kUsageContact &&
                 nodes[i].LinkUsage != kUsageFinger)) {
                continue;
            }
            if (total == 0) {
                first = static_cast<USHORT>(i);
            }
            if (device->decodedContactCount < kHidMaxContactCollections) {
                device->contactNodes[device->decodedContactCount++] =
                    static_cast<USHORT>(i);
            }
            ++total;
        }
    }
    device->contactNodeCount = total;
    LogFormat("hidcoll dev=%s n=%u first=%u", device->path,
              static_cast<unsigned>(total), static_cast<unsigned>(first));
}

// One cache entry per RAWINPUT hDevice. Only the raw input thread calls this.
HidDevice* GetHidDevice(HANDLE handle) {
    for (int i = 0; i < g_hidDeviceCount; ++i) {
        if (g_hidDevices[i].handle == handle) {
            return &g_hidDevices[i];
        }
    }
    if (!handle) {
        return nullptr;
    }
    if (g_hidDeviceCount >= kHidMaxDevices) {
        if (!g_hidCacheFullLogged.exchange(true)) {
            LogFormat("hid cache full dev=%p", static_cast<void*>(handle));
        }
        return nullptr;
    }

    HidDevice* device = &g_hidDevices[g_hidDeviceCount++];
    *device = HidDevice{};
    device->handle = handle;
    ReadHidDeviceInfo(device);
    return device;
}

bool ReadUsageValue(const HidDevice* device, USAGE usagePage,
                    USHORT collection, USAGE usage, const BYTE* report,
                    ULONG reportLength, ULONG* value) {
    if (!device || !device->preparsed) {
        return false;
    }
    ULONG parsed = 0;
    const NTSTATUS status = HidP_GetUsageValue(
        HidP_Input, usagePage, collection, usage, &parsed,
        device->preparsed,
        reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)), reportLength);
    if (status != HIDP_STATUS_SUCCESS) {
        return false;
    }
    *value = parsed;
    return true;
}

struct HidContactFrame {
    int x;
    int y;
    int inRange;
    bool valid;
    bool active;
    bool haveContactId;
    ULONG contactId;
    bool hasExtent;
};

void DecodeReport(HidDevice* device, WPARAM reportId, const BYTE* report,
                  ULONG reportLength) {
    HidContactFrame frames[kHidMaxContactCollections]{};
    USHORT collections[kHidMaxContactCollections]{};
    int collectionCount = 0;

    if (device && device->preparsed) {
        if (device->decodedContactCount > 0) {
            collectionCount = device->decodedContactCount;
            for (int i = 0; i < collectionCount; ++i) {
                collections[i] = device->contactNodes[i];
            }
        } else {
            // No contact collection: a single-contact device keeps X/Y on
            // the top-level collection 0.
            collectionCount = 1;
            collections[0] = 0;
        }

        for (int i = 0; i < collectionCount; ++i) {
            ULONG x = 0;
            ULONG y = 0;
            ULONG inRange = 0;
            ULONG tipSwitch = 0;
            ULONG contactId = 0;
            ULONG width = 0;
            ULONG height = 0;
            const bool haveX = ReadUsageValue(device, kUsagePageGeneric,
                                              collections[i], kUsageGenericX,
                                              report, reportLength, &x);
            const bool haveY = ReadUsageValue(device, kUsagePageGeneric,
                                              collections[i], kUsageGenericY,
                                              report, reportLength, &y);
            const bool haveInRange =
                ReadUsageValue(device, kUsagePageDigitizer, collections[i],
                               kUsageInRange, report, reportLength, &inRange);
            const bool haveTipSwitch =
                ReadUsageValue(device, kUsagePageDigitizer, collections[i],
                               kUsageTipSwitch, report, reportLength,
                               &tipSwitch);
            const bool haveContactId =
                ReadUsageValue(device, kUsagePageDigitizer, collections[i],
                               kUsageContactId, report, reportLength,
                               &contactId);
            const bool haveWidth =
                ReadUsageValue(device, kUsagePageDigitizer, collections[i],
                               kUsageWidth, report, reportLength, &width);
            const bool haveHeight =
                ReadUsageValue(device, kUsagePageDigitizer, collections[i],
                               kUsageHeight, report, reportLength, &height);

            frames[i].valid = haveX && haveY;
            frames[i].x = haveX ? static_cast<int>(x) : -1;
            frames[i].y = haveY ? static_cast<int>(y) : -1;
            frames[i].haveContactId = haveContactId;
            frames[i].contactId = contactId;
            frames[i].hasExtent = (haveWidth && width != 0) ||
                                  (haveHeight && height != 0);

            // Neither the ELAN2513 nor the virtual digitizer exposes In
            // Range. Tip Switch is authoritative next; without it, a live
            // Contact Identifier, Width/Height or non-zero X/Y means the
            // contact is down. The virtual digitizer reports ID 0 for an
            // active contact, so ID alone is not enough.
            bool active = false;
            if (haveInRange) {
                active = inRange != 0;
            } else if (haveTipSwitch) {
                active = tipSwitch != 0;
            } else {
                active = (haveContactId && contactId != 0) ||
                         frames[i].hasExtent ||
                         (frames[i].valid &&
                          (frames[i].x != 0 || frames[i].y != 0));
            }
            frames[i].active = active;
            frames[i].inRange = active ? 1 : 0;
        }
    }

    int activeContacts = 0;
    int primary = -1;
    for (int i = 0; i < collectionCount; ++i) {
        if (frames[i].active) {
            ++activeContacts;
        }
    }

    // Prefer a live Contact Identifier, then a non-zero extent, then a
    // non-zero position. This keeps the first decoded contact on the finger
    // when the device leaves unused collections at zero.
    for (int i = 0; i < collectionCount && primary < 0; ++i) {
        if (frames[i].valid && frames[i].haveContactId &&
            frames[i].contactId != 0) {
            primary = i;
        }
    }
    for (int i = 0; i < collectionCount && primary < 0; ++i) {
        if (frames[i].valid && frames[i].hasExtent) {
            primary = i;
        }
    }
    for (int i = 0; i < collectionCount && primary < 0; ++i) {
        if (frames[i].valid && (frames[i].x != 0 || frames[i].y != 0)) {
            primary = i;
        }
    }
    for (int i = 0; i < collectionCount && primary < 0; ++i) {
        if (frames[i].valid) {
            primary = i;
        }
    }

    // A top-level Contact Count usage is authoritative for multi-touch
    // reports. Without it, the active contact collections are the count.
    int contacts = activeContacts;
    ULONG topLevelCount = 0;
    if (device && device->preparsed &&
        ReadUsageValue(device, kUsagePageDigitizer, 0, kUsageContactCount,
                       report, reportLength, &topLevelCount)) {
        contacts = static_cast<int>(topLevelCount);
    }

    const char* path = device ? device->path : "?";

    if (TakeLine(g_hidDecodeLines, kHidDecodeLogCap)) {
        if (primary >= 0) {
            LogFormat("hid dev=%s id=%u x=%d y=%d contacts=%d", path,
                      static_cast<unsigned>(reportId), frames[primary].x,
                      frames[primary].y, contacts);
        } else {
            LogFormat("hid dev=%s id=%u x=-1 y=-1 contacts=%d", path,
                      static_cast<unsigned>(reportId), contacts);
        }
    }

    if (!device || !device->preparsed) {
        return;
    }

    if (device->contactNodeCount == 0) {
        if (TakeLine(g_hidContactLines, kHidContactLogCap)) {
            LogFormat("hidc dev=%s coll=0 x=%d y=%d", path, frames[0].x,
                      frames[0].y);
        }
        return;
    }

    for (int i = 0; i < collectionCount; ++i) {
        if (!TakeLine(g_hidContactLines, kHidContactLogCap)) {
            break;
        }
        LogFormat("hidc dev=%s coll=%u x=%d y=%d inrange=%d", path,
                  static_cast<unsigned>(collections[i]), frames[i].x,
                  frames[i].y, frames[i].inRange);
    }
}

void HandleHidInput(const RAWINPUT* raw) {
    const RAWHID& hid = raw->data.hid;
    if (hid.dwSizeHid == 0) {
        return;
    }

    HidDevice* device = GetHidDevice(raw->header.hDevice);
    for (UINT i = 0; i < hid.dwCount; ++i) {
        const uint32_t index =
            g_hidReportCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (device) {
            const BYTE* report =
                hid.bRawData + static_cast<size_t>(i) * hid.dwSizeHid;
            DecodeReport(device, raw->header.wParam, report, hid.dwSizeHid);
        }
        if (index % 100 == 0) {
            LogFormat("hidcount reports=%u devs=%d",
                      static_cast<unsigned>(index), g_hidDeviceCount);
        }
    }
}

void HandleRawInput(HRAWINPUT rawHandle) {
    BYTE stackBuffer[1024];
    UINT size = sizeof(stackBuffer);
    SetLastError(0);
    UINT read = GetRawInputData(rawHandle, RID_INPUT, stackBuffer, &size,
                                sizeof(RAWINPUTHEADER));
    BYTE* heapBuffer = nullptr;

    if (read == static_cast<UINT>(-1)) {
        if (size <= sizeof(stackBuffer) || size > 64 * 1024) {
            LogFormat("hid read fail size=%u err=%u",
                      static_cast<unsigned>(size),
                      static_cast<unsigned>(GetLastError()));
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
            LogFormat("hid read fail size=%u err=%u",
                      static_cast<unsigned>(size),
                      static_cast<unsigned>(GetLastError()));
            return;
        }
        size = heapSize;
    }

    const auto* raw =
        reinterpret_cast<const RAWINPUT*>(heapBuffer ? heapBuffer : stackBuffer);
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
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Enumerates the touch digitizers before the first report, so their caps and
// contact collections are visible without touching the panel. The virtual
// digitizer and the real ELAN touchscreen are both cached here.
void EnumerateDigitizers() {
    UINT count = 0;
    SetLastError(0);
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 ||
        count == 0 || count > 256) {
        LogFormat("hid enum fail n=%u err=%u", static_cast<unsigned>(count),
                  static_cast<unsigned>(GetLastError()));
        return;
    }

    auto* devices =
        new (std::nothrow) RAWINPUTDEVICELIST[count];
    if (!devices) {
        return;
    }

    UINT fetched = count;
    SetLastError(0);
    if (GetRawInputDeviceList(devices, &fetched,
                              sizeof(RAWINPUTDEVICELIST)) ==
        static_cast<UINT>(-1)) {
        LogFormat("hid enum fail n=%u err=%u", static_cast<unsigned>(count),
                  static_cast<unsigned>(GetLastError()));
        delete[] devices;
        return;
    }

    for (UINT i = 0; i < fetched; ++i) {
        if (devices[i].dwType != RIM_TYPEHID) {
            continue;
        }
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT infoSize = sizeof(info);
        if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICEINFO, &info,
                                   &infoSize) == static_cast<UINT>(-1)) {
            continue;
        }
        if (info.dwType != RIM_TYPEHID ||
            info.hid.usUsagePage != kUsagePageDigitizer ||
            info.hid.usUsage != kUsageTouchScreen) {
            continue;
        }
        GetHidDevice(devices[i].hDevice);
    }

    delete[] devices;
}

void RegisterDigitizer(HWND hwnd) {
    // 0x04 = touch screen, 0x05 = touch pad, 0x02 = pen. RegisterRawInputDevices
    // accepts a usage even without a matching device, so the first success ends
    // the fallback chain.
    const USAGE attempts[] = {0x04, 0x05, 0x02};
    for (const USAGE usage : attempts) {
        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x0D;
        device.usUsage = usage;
        device.dwFlags = RIDEV_INPUTSINK;
        device.hwndTarget = hwnd;

        SetLastError(0);
        const BOOL ok = RegisterRawInputDevices(&device, 1, sizeof(device));
        const DWORD error = ok ? 0 : GetLastError();
        LogFormat("hid reg usage=%u ok=%d err=%u",
                  static_cast<unsigned>(usage), ok ? 1 : 0,
                  static_cast<unsigned>(error));
        if (ok) {
            g_hidRegisteredUsage = usage;
            return;
        }
    }
}

void UnregisterDigitizer() {
    if (g_hidRegisteredUsage < 0) {
        return;
    }
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x0D;
    device.usUsage = static_cast<USAGE>(g_hidRegisteredUsage);
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = nullptr;
    RegisterRawInputDevices(&device, 1, sizeof(device));
    g_hidRegisteredUsage = -1;
}

void FreeHidDeviceCache() {
    for (int i = 0; i < g_hidDeviceCount; ++i) {
        delete[] reinterpret_cast<BYTE*>(g_hidDevices[i].preparsed);
        g_hidDevices[i] = HidDevice{};
    }
    g_hidDeviceCount = 0;
}

DWORD WINAPI RawInputThread(LPVOID) {
    g_hidThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = HidWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kHidWindowClass;

    if (!RegisterClassExW(&windowClass)) {
        LogFormat("hid class fail err=%u",
                  static_cast<unsigned>(GetLastError()));
        return 0;
    }

    HWND hwnd = CreateWindowExW(0, kHidWindowClass, L"inputprobe", 0, 0, 0, 0,
                                0, HWND_MESSAGE, nullptr, windowClass.hInstance,
                                nullptr);
    if (!hwnd) {
        LogFormat("hid window fail err=%u",
                  static_cast<unsigned>(GetLastError()));
        UnregisterClassW(kHidWindowClass, windowClass.hInstance);
        return 0;
    }
    g_hidHwnd.store(hwnd, std::memory_order_relaxed);

    RegisterDigitizer(hwnd);
    EnumerateDigitizers();
    LogFormat("hid listen start tid=%u window=%us",
              static_cast<unsigned>(GetCurrentThreadId()), kHidListenSeconds);

    const ULONGLONG deadline = GetTickCount64() + kHidListenMs;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining =
            now >= deadline ? 0 : static_cast<DWORD>(deadline - now);

        const DWORD waitResult =
            MsgWaitForMultipleObjects(1, &g_hidStopEvent, FALSE, remaining,
                                      QS_ALLINPUT);
        if (waitResult == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!IsWindow(hwnd)) {
                break;
            }
        } else if (waitResult == WAIT_TIMEOUT) {
            LogFormat("hid stop reason=timeout reports=%u",
                      g_hidReportCount.load(std::memory_order_relaxed));
            break;
        } else {
            LogFormat("hid stop reason=wait result=%u err=%u",
                      static_cast<unsigned>(waitResult),
                      static_cast<unsigned>(GetLastError()));
            break;
        }
    }

    UnregisterDigitizer();
    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    g_hidHwnd.store(nullptr, std::memory_order_relaxed);
    UnregisterClassW(kHidWindowClass, windowClass.hInstance);
    LogFormat("hid thread exit reports=%u",
              g_hidReportCount.load(std::memory_order_relaxed));
    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    CreateDirectoryW(kLogDir, nullptr);

    // Truncate once per process so a stale run never mixes with this one.
    HANDLE file = CreateFileW(kLogPath, GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    } else {
        LogFormat("log truncate fail err=%u",
                  static_cast<unsigned>(GetLastError()));
    }

    LogFormat("inputprobe init pid=%u",
              static_cast<unsigned>(GetCurrentProcessId()));
    return TRUE;
}

void Wh_ModAfterInit() {
    LogFormat("inputprobe afterinit pid=%u",
              static_cast<unsigned>(GetCurrentProcessId()));

    g_stopping.store(false, std::memory_order_relaxed);

    g_hidStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_hidStopEvent) {
        g_hidThread = CreateThread(nullptr, 0, RawInputThread, nullptr, 0,
                                   nullptr);
        if (!g_hidThread) {
            LogFormat("hid thread fail err=%u",
                      static_cast<unsigned>(GetLastError()));
        }
    } else {
        LogFormat("hid event fail err=%u",
                  static_cast<unsigned>(GetLastError()));
    }

    g_scanThread = CreateThread(nullptr, 0, WindowScanThread, nullptr, 0,
                                nullptr);
    if (!g_scanThread) {
        LogFormat("scan thread fail err=%u",
                  static_cast<unsigned>(GetLastError()));
    }
}

void Wh_ModBeforeUninit() {
    g_stopping.store(true, std::memory_order_relaxed);
    if (g_hidStopEvent) {
        SetEvent(g_hidStopEvent);
    }
}

void Wh_ModUninit() {
    g_stopping.store(true, std::memory_order_relaxed);
    if (g_hidStopEvent) {
        SetEvent(g_hidStopEvent);
    }

    if (g_scanThread) {
        WaitForSingleObject(g_scanThread, 3000);
        CloseHandle(g_scanThread);
        g_scanThread = nullptr;
    }

    // Restore every window procedure before the DLL can be unloaded. A new
    // subclass installed by another mod after ours is left untouched.
    AcquireSRWLockExclusive(&g_subclassLock);
    const int count = g_subclassCount.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        HWND hwnd = g_subclass[i].hwnd;
        WNDPROC original = g_subclass[i].original;
        if (hwnd && original && IsWindow(hwnd) &&
            GetWindowLongPtrW(hwnd, GWLP_WNDPROC) ==
                reinterpret_cast<LONG_PTR>(WindowSubclassProc)) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(original));
        }
        g_subclass[i].hwnd = nullptr;
        g_subclass[i].original = nullptr;
    }
    g_subclassCount.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_subclassLock);

    if (g_hidThread) {
        const DWORD waitResult = WaitForSingleObject(g_hidThread, 3000);
        CloseHandle(g_hidThread);
        g_hidThread = nullptr;
        // The cache belongs to the raw input thread; free it only after that
        // thread has actually exited.
        if (waitResult == WAIT_OBJECT_0) {
            FreeHidDeviceCache();
        }
    }
    if (g_hidStopEvent) {
        CloseHandle(g_hidStopEvent);
        g_hidStopEvent = nullptr;
    }

    LogFormat("inputprobe uninit pid=%u",
              static_cast<unsigned>(GetCurrentProcessId()));
}
