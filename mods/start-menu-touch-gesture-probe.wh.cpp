// ==WindhawkMod==
// @id              start-menu-touch-gesture-probe
// @name            Start Menu Touch Gesture Probe
// @description     Phase 0 diagnostic probe for Win10 Start menu touch gestures (ExplorerPatcher classic mode)
// @version         0.1.0
// @author          doubao
// @include         explorer.exe
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Start Menu Touch Gesture Probe

Diagnostic-only mod. It does not change any behavior; it only writes a log of
the shell/Start callbacks that fire while a touch gesture is performed.

Targets:
- explorer.exe / twinui.pcshell.dll: Start gesture plumbing
- StartMenuExperienceHost.exe / StartUI_.dll: legacy Start UI callbacks

The log is written to the mod's storage folder as `probe.log`.
*/
// ==/WindhawkModReadme==

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include <windhawk_utils.h>

namespace {

constexpr uintptr_t kPreferredBase = 0x180000000;

struct Point {
    float x;
    float y;
};

struct GestureAnimationData {
    uint64_t sourceId;
    uint64_t providerId;
};

std::atomic<bool> g_explorerHooked{false};
std::atomic<bool> g_startUIHooked{false};
std::atomic<bool> g_afterInit{false};
std::atomic<bool> g_stopPolling{false};
std::atomic<uint32_t> g_continueCount{0};
HANDLE g_logMutex = nullptr;
WCHAR g_logPath[MAX_PATH] = {};
const char* g_procTag = "?";
bool g_isStartProcess = false;

void InitLog() {
    WCHAR storage[MAX_PATH] = {};
    size_t len = Wh_GetModStoragePath(storage, ARRAYSIZE(storage));
    if (len) {
        swprintf_s(g_logPath, L"%s\\probe.log", storage);
    } else {
        WCHAR temp[MAX_PATH] = {};
        GetTempPathW(ARRAYSIZE(temp), temp);
        swprintf_s(g_logPath, L"%sstart-menu-touch-gesture-probe.log", temp);
    }

    g_logMutex = CreateMutexW(nullptr, FALSE,
                              L"Global\\StartMenuTouchGestureProbeLog");

    WCHAR exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath));
    const WCHAR* exeName = wcsrchr(exePath, L'\\');
    exeName = exeName ? exeName + 1 : exePath;
    if (_wcsicmp(exeName, L"StartMenuExperienceHost.exe") == 0) {
        g_procTag = "start";
        g_isStartProcess = true;
    } else if (_wcsicmp(exeName, L"explorer.exe") == 0) {
        g_procTag = "explorer";
    }
}

void Log(const char* format, ...) {
    if (!g_logPath[0]) {
        return;
    }

    char line[1024];
    int prefix = snprintf(line, sizeof(line), "[%s][%lu:%lu][%llu] ",
                          g_procTag, GetCurrentProcessId(), GetCurrentThreadId(),
                          static_cast<unsigned long long>(GetTickCount64()));
    if (prefix < 0) {
        prefix = 0;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(line + prefix, sizeof(line) - prefix, format, args);
    va_end(args);

    size_t length = strlen(line);
    if (length + 2 < sizeof(line)) {
        line[length++] = '\r';
        line[length++] = '\n';
        line[length] = '\0';
    }

    if (g_logMutex) {
        WaitForSingleObject(g_logMutex, 100);
    }

    HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
        CloseHandle(file);
    }

    if (g_logMutex) {
        ReleaseMutex(g_logMutex);
    }
}

void LogTarget(const char* name, void* target) {
    if (!target) {
        Log("%s target=null", name);
        return;
    }

    unsigned char bytes[8] = {};
    memcpy(bytes, target, sizeof(bytes));

    Log("%s target=%p bytes=%02X %02X %02X %02X %02X %02X %02X %02X", name,
        target, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
        bytes[6], bytes[7]);
}

bool InstallHook(void* target, void* hook, void** original, const char* name) {
    LogTarget(name, target);
    if (!target) {
        return false;
    }

    BOOL ok = Wh_SetFunctionHook(target, hook, original);
    Log("%s Hook=%d original=%p", name, ok, original ? *original : nullptr);
    return ok != FALSE;
}

// ---------------------------------------------------------------------------
// explorer.exe / twinui.pcshell.dll
// ---------------------------------------------------------------------------

using ReportViewDragGesture_t = void (*)(void*, int, const int*,
                                         const GestureAnimationData*, Point);
ReportViewDragGesture_t ReportViewDragGesture_Original = nullptr;

void ReportViewDragGesture_Hook(void* self, int edge, const int* view,
                                const GestureAnimationData* data, Point point) {
    Log("ReportViewDragGesture edge=%d view=%d src=%llu prov=%llu pt=(%.2f,%.2f)",
        edge, view ? *view : -1,
        static_cast<unsigned long long>(data ? data->sourceId : 0),
        static_cast<unsigned long long>(data ? data->providerId : 0), point.x,
        point.y);
    if (ReportViewDragGesture_Original) {
        ReportViewDragGesture_Original(self, edge, view, data, point);
    }
}

using ReportEdgeGesture_t = void (*)(void*, int, const GestureAnimationData*,
                                     Point);
ReportEdgeGesture_t ReportEdgeGesture_Original = nullptr;

void ReportEdgeGesture_Hook(void* self, int edge,
                            const GestureAnimationData* data, Point point) {
    Log("EdgeAnimationRelationshipManager::ReportEdgeGesture edge=%d src=%llu prov=%llu pt=(%.2f,%.2f)",
        edge, static_cast<unsigned long long>(data ? data->sourceId : 0),
        static_cast<unsigned long long>(data ? data->providerId : 0), point.x,
        point.y);
    if (ReportEdgeGesture_Original) {
        ReportEdgeGesture_Original(self, edge, data, point);
    }
}

using SetAnimationData_t = HRESULT (*)(void*, uint64_t, uint64_t, uint64_t);
SetAnimationData_t SetAnimationData_Original = nullptr;

HRESULT SetAnimationData_Hook(void* self, uint64_t a, uint64_t b, uint64_t c) {
    Log("CStartExperienceManager::SetAnimationData a=%llu b=%llu c=%llu",
        static_cast<unsigned long long>(a),
        static_cast<unsigned long long>(b),
        static_cast<unsigned long long>(c));
    if (SetAnimationData_Original) {
        return SetAnimationData_Original(self, a, b, c);
    }
    return S_OK;
}

using Show_t = HRESULT (*)(void*, unsigned char, unsigned char, unsigned int);
Show_t Show_Original = nullptr;

HRESULT Show_Hook(void* self, unsigned char a, unsigned char b, unsigned int c) {
    Log("CStartExperienceManager::Show a=%u b=%u c=%u", a, b, c);
    if (Show_Original) {
        return Show_Original(self, a, b, c);
    }
    return S_OK;
}

using Hide_t = HRESULT (*)(void*, unsigned int);
Hide_t Hide_Original = nullptr;

HRESULT Hide_Hook(void* self, unsigned int a) {
    Log("CStartExperienceManager::Hide a=%u", a);
    if (Hide_Original) {
        return Hide_Original(self, a);
    }
    return S_OK;
}

using StartSwipe_t = void (*)(void*, unsigned int, const Point*);
StartSwipe_t StartSwipe_Original = nullptr;

void StartSwipe_Hook(void* self, unsigned int pointerId, const Point* point) {
    Log("TouchGestureProcessor::StartSwipe pointerId=%u pt=(%.2f,%.2f)",
        pointerId, point ? point->x : 0, point ? point->y : 0);
    if (StartSwipe_Original) {
        StartSwipe_Original(self, pointerId, point);
    }
}

using ContinueSwipe_t = void (*)(void*, const Point*, const Point*);
ContinueSwipe_t ContinueSwipe_Original = nullptr;

void ContinueSwipe_Hook(void* self, const Point* current, const Point* previous) {
    static uint64_t lastTick = 0;
    uint64_t now = GetTickCount64();
    if (now - lastTick >= 4) {
        lastTick = now;
        Log("TouchGestureProcessor::ContinueSwipe #%u cur=(%.2f,%.2f) prev=(%.2f,%.2f)",
            static_cast<unsigned int>(++g_continueCount),
            current ? current->x : 0, current ? current->y : 0,
            previous ? previous->x : 0, previous ? previous->y : 0);
    }
    if (ContinueSwipe_Original) {
        ContinueSwipe_Original(self, current, previous);
    }
}

using FinishSwipe_t = void (*)(void*, const Point*);
FinishSwipe_t FinishSwipe_Original = nullptr;

void FinishSwipe_Hook(void* self, const Point* point) {
    Log("TouchGestureProcessor::FinishSwipe pt=(%.2f,%.2f)",
        point ? point->x : 0, point ? point->y : 0);
    if (FinishSwipe_Original) {
        FinishSwipe_Original(self, point);
    }
}

using GetObject_t = HRESULT(STDMETHODCALLTYPE*)(void*, void**);
using GetU64_t = HRESULT(STDMETHODCALLTYPE*)(void*, uint64_t*);
using GetPoint_t = HRESULT(STDMETHODCALLTYPE*)(void*, Point*);
using GetGestureData_t = HRESULT(STDMETHODCALLTYPE*)(void*,
                                                     GestureAnimationData*);

void LogReportedArgs(void* argsWrapper) {
    if (!argsWrapper) {
        Log("  reportedArgs=null");
        return;
    }

    // C++/WinRT runtime class references hold the ABI pointer as the first
    // field.
    void* abi = *reinterpret_cast<void**>(argsWrapper);
    if (!abi) {
        Log("  reportedArgs=%p abi=null", argsWrapper);
        return;
    }

    void** vtbl = *reinterpret_cast<void***>(abi);
    if (!vtbl) {
        Log("  reportedArgs=%p abi=%p vtbl=null", argsWrapper, abi);
        return;
    }

    void* animationProperties = nullptr;
    auto getAnimationProperties = reinterpret_cast<GetObject_t>(vtbl[6]);
    HRESULT hrProperties = E_FAIL;
    if (getAnimationProperties) {
        hrProperties = getAnimationProperties(abi, &animationProperties);
    }

    Point invocationPoint = {};
    auto getInvocationPoint = reinterpret_cast<GetPoint_t>(vtbl[7]);
    HRESULT hrPoint = E_FAIL;
    if (getInvocationPoint) {
        hrPoint = getInvocationPoint(abi, &invocationPoint);
    }

    Log("  reportedArgs=%p abi=%p propsHr=0x%08X props=%p pointHr=0x%08X pt=(%.2f,%.2f)",
        argsWrapper, abi, hrProperties, animationProperties, hrPoint,
        invocationPoint.x, invocationPoint.y);

    if (!animationProperties) {
        return;
    }

    void** propsVtbl = *reinterpret_cast<void***>(animationProperties);
    if (!propsVtbl) {
        return;
    }

    uint64_t offset = 0;
    auto getOffset = reinterpret_cast<GetU64_t>(propsVtbl[6]);
    HRESULT hrOffset = E_FAIL;
    if (getOffset) {
        hrOffset = getOffset(animationProperties, &offset);
    }

    GestureAnimationData data = {};
    auto getData = reinterpret_cast<GetGestureData_t>(propsVtbl[7]);
    HRESULT hrData = E_FAIL;
    if (getData) {
        hrData = getData(animationProperties, &data);
    }

    Log("  edgeOffset=%llu offsetHr=0x%08X src=%llu prov=%llu dataHr=0x%08X",
        static_cast<unsigned long long>(offset), hrOffset,
        static_cast<unsigned long long>(data.sourceId),
        static_cast<unsigned long long>(data.providerId), hrData);
}

using EdgeReportViewDragGesture_t = void (*)(void*, const int*, const void*);
EdgeReportViewDragGesture_t EdgeReportViewDragGesture_Original = nullptr;

void EdgeReportViewDragGesture_Hook(void* self, const int* view,
                                    const void* args) {
    Log("TouchEdgeGestureProcessor::ReportViewDragGesture view=%d args=%p",
        view ? *view : -1, args);
    LogReportedArgs(const_cast<void*>(args));
    if (EdgeReportViewDragGesture_Original) {
        EdgeReportViewDragGesture_Original(self, view, args);
    }
}

using EdgeGestureOperation_t = void (*)(void*, const void*, const void*);
EdgeGestureOperation_t EdgeGestureOperationStarted_Original = nullptr;
EdgeGestureOperation_t EdgeGestureOperationEnded_Original = nullptr;

void EdgeGestureOperationStarted_Hook(void* self, const void* operation,
                                      const void* args) {
    Log("TouchEdgeGestureProcessor::InputGestureOperationStarted operation=%p args=%p",
        operation, args);
    if (EdgeGestureOperationStarted_Original) {
        EdgeGestureOperationStarted_Original(self, operation, args);
    }
}

void EdgeGestureOperationEnded_Hook(void* self, const void* operation,
                                    const void* args) {
    Log("TouchEdgeGestureProcessor::InputGestureOperationEnded operation=%p args=%p",
        operation, args);
    if (EdgeGestureOperationEnded_Original) {
        EdgeGestureOperationEnded_Original(self, operation, args);
    }
}

void InstallExplorerHooks(HMODULE module) {
    if (g_explorerHooked.exchange(true)) {
        return;
    }

    Log("InstallExplorerHooks module=%p", module);
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);

    InstallHook(reinterpret_cast<void*>(base + (0x1806F6720 - kPreferredBase)),
                reinterpret_cast<void*>(ReportViewDragGesture_Hook),
                reinterpret_cast<void**>(&ReportViewDragGesture_Original),
                "EdgeAnimationRelationshipManager::ReportViewDragGesture");

    InstallHook(reinterpret_cast<void*>(base + (0x1806F63BC - kPreferredBase)),
                reinterpret_cast<void*>(ReportEdgeGesture_Hook),
                reinterpret_cast<void**>(&ReportEdgeGesture_Original),
                "EdgeAnimationRelationshipManager::ReportEdgeGesture");

    InstallHook(reinterpret_cast<void*>(base + (0x1803C7740 - kPreferredBase)),
                reinterpret_cast<void*>(SetAnimationData_Hook),
                reinterpret_cast<void**>(&SetAnimationData_Original),
                "CStartExperienceManager::SetAnimationData");

    InstallHook(reinterpret_cast<void*>(base + (0x180254C90 - kPreferredBase)),
                reinterpret_cast<void*>(Show_Hook),
                reinterpret_cast<void**>(&Show_Original),
                "CStartExperienceManager::Show");

    InstallHook(reinterpret_cast<void*>(base + (0x1803C5960 - kPreferredBase)),
                reinterpret_cast<void*>(Hide_Hook),
                reinterpret_cast<void**>(&Hide_Original),
                "CStartExperienceManager::Hide");

    InstallHook(reinterpret_cast<void*>(base + (0x1805F6E1C - kPreferredBase)),
                reinterpret_cast<void*>(StartSwipe_Hook),
                reinterpret_cast<void**>(&StartSwipe_Original),
                "TouchGestureProcessor::StartSwipe");

    InstallHook(reinterpret_cast<void*>(base + (0x1805F4824 - kPreferredBase)),
                reinterpret_cast<void*>(ContinueSwipe_Hook),
                reinterpret_cast<void**>(&ContinueSwipe_Original),
                "TouchGestureProcessor::ContinueSwipe");

    InstallHook(reinterpret_cast<void*>(base + (0x1805F4A5C - kPreferredBase)),
                reinterpret_cast<void*>(FinishSwipe_Hook),
                reinterpret_cast<void**>(&FinishSwipe_Original),
                "TouchGestureProcessor::FinishSwipe");

    InstallHook(reinterpret_cast<void*>(base + (0x1805F8DAC - kPreferredBase)),
                reinterpret_cast<void*>(EdgeReportViewDragGesture_Hook),
                reinterpret_cast<void**>(&EdgeReportViewDragGesture_Original),
                "TouchEdgeGestureProcessor::ReportViewDragGesture");

    InstallHook(reinterpret_cast<void*>(base + (0x1800CAD80 - kPreferredBase)),
                reinterpret_cast<void*>(EdgeGestureOperationStarted_Hook),
                reinterpret_cast<void**>(&EdgeGestureOperationStarted_Original),
                "TouchEdgeGestureProcessor::InputGestureOperationStarted");

    InstallHook(reinterpret_cast<void*>(base + (0x1805F8840 - kPreferredBase)),
                reinterpret_cast<void*>(EdgeGestureOperationEnded_Hook),
                reinterpret_cast<void**>(&EdgeGestureOperationEnded_Original),
                "TouchEdgeGestureProcessor::InputGestureOperationEnded");
}

// ---------------------------------------------------------------------------
// StartMenuExperienceHost.exe / StartUI_.dll
// ---------------------------------------------------------------------------

using OnGlobalAnimationRequested_t = void (*)(void*, void*, void*);
OnGlobalAnimationRequested_t OnGlobalAnimationRequested_Original = nullptr;

struct VtblPrefix {
    void* queryInterface;
    void* addRef;
    void* release;
    void* getIids;
    void* getRuntimeClassName;
    void* getTrustLevel;
};

using GetInt_t = HRESULT(STDMETHODCALLTYPE*)(void*, int*);

int TryGetIntProperty(void* object, int slot) {
    if (!object) {
        return -1;
    }

    void** vtbl = *reinterpret_cast<void***>(object);
    if (!vtbl || !vtbl[slot]) {
        return -1;
    }

    int value = -1;
    auto getter = reinterpret_cast<GetInt_t>(vtbl[slot]);
    HRESULT hr = getter(object, &value);
    return SUCCEEDED(hr) ? value : -2;
}

void OnGlobalAnimationRequested_Hook(void* self, void* sender, void* args) {
    int animationType = TryGetIntProperty(args, 6);
    int taskbarEdge = TryGetIntProperty(args, 7);
    int transitionType = TryGetIntProperty(args, 8);
    Log("SplitViewFrame::OnGlobalAnimationRequested animationType=%d taskbarEdge=%d transitionType=%d args=%p",
        animationType, taskbarEdge, transitionType, args);
    if (OnGlobalAnimationRequested_Original) {
        OnGlobalAnimationRequested_Original(self, sender, args);
    }
}

using PrepareRootElement_t = void* (*)(void*, void*, bool);
PrepareRootElement_t PrepareRootElement_Original = nullptr;

void* PrepareRootElement_Hook(void* frame, void* perFrameData, bool flag) {
    void* root = nullptr;
    if (PrepareRootElement_Original) {
        root = PrepareRootElement_Original(frame, perFrameData, flag);
    }
    Log("App::PrepareRootElement frame=%p perFrameData=%p flag=%d root=%p", frame,
        perFrameData, flag, root);
    return root;
}

using OnWindowVisibilityChanged_t = void (*)(void*, void*, void*);
OnWindowVisibilityChanged_t OnWindowVisibilityChanged_Original = nullptr;

void OnWindowVisibilityChanged_Hook(void* self, void* sender, void* args) {
    int visible = TryGetIntProperty(args, 6);
    Log("StartSizingFrame::OnWindowVisibilityChanged visible=%d args=%p", visible,
        args);
    if (OnWindowVisibilityChanged_Original) {
        OnWindowVisibilityChanged_Original(self, sender, args);
    }
}

using OnWindowSizeChanged_t = void (*)(void*, void*, void*);
OnWindowSizeChanged_t OnWindowSizeChanged_Original = nullptr;

void OnWindowSizeChanged_Hook(void* self, void* sender, void* args) {
    Log("StartSizingFrame::OnWindowSizeChanged args=%p", args);
    if (OnWindowSizeChanged_Original) {
        OnWindowSizeChanged_Original(self, sender, args);
    }
}

void HookStartUI(HMODULE module) {
    if (g_startUIHooked.exchange(true)) {
        return;
    }

    Log("HookStartUI module=%p", module);
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);

    InstallHook(reinterpret_cast<void*>(base + (0x1804164C0 - kPreferredBase)),
                reinterpret_cast<void*>(OnGlobalAnimationRequested_Hook),
                reinterpret_cast<void**>(&OnGlobalAnimationRequested_Original),
                "StartUI::SplitViewFrame::OnGlobalAnimationRequested");

    InstallHook(reinterpret_cast<void*>(base + (0x1800AD730 - kPreferredBase)),
                reinterpret_cast<void*>(PrepareRootElement_Hook),
                reinterpret_cast<void**>(&PrepareRootElement_Original),
                "StartUI::App::PrepareRootElement");

    InstallHook(reinterpret_cast<void*>(base + (0x1802B0E20 - kPreferredBase)),
                reinterpret_cast<void*>(OnWindowVisibilityChanged_Hook),
                reinterpret_cast<void**>(&OnWindowVisibilityChanged_Original),
                "StartUI::StartSizingFrame::OnWindowVisibilityChanged");

    InstallHook(reinterpret_cast<void*>(base + (0x1800F0C40 - kPreferredBase)),
                reinterpret_cast<void*>(OnWindowSizeChanged_Hook),
                reinterpret_cast<void**>(&OnWindowSizeChanged_Original),
                "StartUI::StartSizingFrame::OnWindowSizeChanged");
}

void ApplyPendingHooks() {
    if (g_afterInit.load()) {
        Wh_ApplyHookOperations();
    }
}

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original = nullptr;

void TryInstallFromLoadedModules(bool apply) {
    bool installed = false;

    if (!g_explorerHooked.load()) {
        HMODULE twinui = GetModuleHandleW(L"twinui.pcshell.dll");
        if (twinui) {
            InstallExplorerHooks(twinui);
            installed = true;
        }
    }

    if (!g_startUIHooked.load()) {
        HMODULE startUI = GetModuleHandleW(L"StartUI_.dll");
        if (startUI) {
            HookStartUI(startUI);
            installed = true;
        }
    }

    if (installed && apply && g_afterInit.load()) {
        Wh_ApplyHookOperations();
    }
}

HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR fileName, HANDLE file,
                                   DWORD flags) {
    HMODULE module = LoadLibraryExW_Original
                         ? LoadLibraryExW_Original(fileName, file, flags)
                         : nullptr;

    if (module) {
        HMODULE twinui = GetModuleHandleW(L"twinui.pcshell.dll");
        HMODULE startUI = GetModuleHandleW(L"StartUI_.dll");
        if (module == twinui || module == startUI) {
            TryInstallFromLoadedModules(true);
        }
    }

    return module;
}

DWORD WINAPI PollModulesThread(LPVOID) {
    for (int i = 0; i < 400 && !g_stopPolling.load(); i++) {
        TryInstallFromLoadedModules(true);
        if (g_isStartProcess ? g_startUIHooked.load()
                             : g_explorerHooked.load()) {
            break;
        }
        Sleep(50);
    }
    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    InitLog();
    Log("Wh_ModInit");

    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibraryExW = reinterpret_cast<LoadLibraryExW_t>(
        GetProcAddress(kernelBase, "LoadLibraryExW"));
    InstallHook(reinterpret_cast<void*>(loadLibraryExW),
                reinterpret_cast<void*>(LoadLibraryExW_Hook),
                reinterpret_cast<void**>(&LoadLibraryExW_Original),
                "LoadLibraryExW");

    TryInstallFromLoadedModules(false);

    CreateThread(nullptr, 0, PollModulesThread, nullptr, 0, nullptr);

    return TRUE;
}

void Wh_ModAfterInit() {
    Log("Wh_ModAfterInit");
    g_afterInit = true;
    TryInstallFromLoadedModules(false);
    Wh_ApplyHookOperations();
}

void Wh_ModUninit() {
    Log("Wh_ModUninit");
    g_stopPolling = true;
    if (g_logMutex) {
        CloseHandle(g_logMutex);
        g_logMutex = nullptr;
    }
}
