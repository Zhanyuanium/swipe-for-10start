// ==WindhawkMod==
// @id              start-menu-touch-gesture-consumer
// @name            Start Menu Touch Gesture Consumer (Probe)
// @description     Reads the explorer gesture bridge and logs the live handle/state (no UI changes)
// @version         0.1.0
// @author          Zhanyuanium
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -I"C:\Users\doubao\source\repos\swipe-for-10start\mods"
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Start Menu Touch Gesture Consumer (Probe)

Phase 1 diagnostic consumer. It reads `Local\WindhawkStartGestureShared` written
by `start-menu-touch-gesture-bridge.wh.cpp` and logs state/handle transitions.
It does not touch the Start UI yet.

This is used to verify the explorer -> Start bridge end-to-end before binding
the native `InputAnimationTarget` animation.
*/
// ==/WindhawkModReadme==

#include <windows.h>

#include <atomic>
#include <cstdint>

#include <windhawk_utils.h>

#include "start_gesture_shared.h"

namespace {

std::atomic<bool> g_stop{false};
HANDLE g_thread = nullptr;
HANDLE g_mapping = nullptr;
HANDLE g_event = nullptr;
StartGestureShared* g_shared = nullptr;

struct Snapshot {
    uint32_t state;
    int32_t edge;
    int64_t sourceId;
    int64_t providerId;
    float startX;
    float startY;
    uint64_t gestureStartTick;
    uint64_t lastUpdateTick;
};

bool ReadSnapshot(Snapshot* snapshot) {
    if (!g_shared) {
        return false;
    }

    for (int attempt = 0; attempt < 8; attempt++) {
        const uint32_t before = g_shared->sequence;
        if (before & 1u) {
            continue;
        }

        Snapshot value;
        value.state = g_shared->state;
        value.edge = g_shared->edge;
        value.sourceId = g_shared->sourceId;
        value.providerId = g_shared->providerId;
        value.startX = g_shared->startX;
        value.startY = g_shared->startY;
        value.gestureStartTick = g_shared->gestureStartTick;
        value.lastUpdateTick = g_shared->lastUpdateTick;

        const uint32_t after = g_shared->sequence;
        if (before == after && (after & 1u) == 0) {
            *snapshot = value;
            return true;
        }
    }

    return false;
}

void LogSnapshot(const Snapshot& snapshot) {
    Wh_Log(L"state=%u edge=%d src=%lld prov=%lld startTick=%llu lastTick=%llu pt=(%.1f,%.1f)",
           snapshot.state, snapshot.edge,
           static_cast<long long>(snapshot.sourceId),
           static_cast<long long>(snapshot.providerId),
           static_cast<unsigned long long>(snapshot.gestureStartTick),
           static_cast<unsigned long long>(snapshot.lastUpdateTick),
           snapshot.startX, snapshot.startY);
}

DWORD WINAPI ReaderThread(LPVOID) {
    Snapshot last = {};
    bool haveLast = false;

    while (!g_stop.load(std::memory_order_relaxed)) {
        if (g_event) {
            WaitForSingleObject(g_event, 50);
        } else {
            Sleep(50);
        }

        Snapshot current;
        if (!ReadSnapshot(&current)) {
            continue;
        }

        if (!haveLast || current.state != last.state ||
            current.sourceId != last.sourceId ||
            current.providerId != last.providerId ||
            current.edge != last.edge) {
            LogSnapshot(current);
            last = current;
            haveLast = true;
        }
    }

    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
                                 kWindhawkStartGestureMappingName);
    if (!g_mapping) {
        Wh_Log(L"OpenFileMappingW failed (%u); explorer bridge not running?",
               GetLastError());
        return TRUE;
    }

    g_shared = static_cast<StartGestureShared*>(
        MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0, sizeof(StartGestureShared)));
    if (!g_shared) {
        Wh_Log(L"MapViewOfFile failed (%u)", GetLastError());
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return TRUE;
    }

    if (g_shared->magic != kWindhawkStartGestureMagic) {
        Wh_Log(L"Shared memory magic mismatch (0x%08X)", g_shared->magic);
    }

    g_event = OpenEventW(SYNCHRONIZE, FALSE, kWindhawkStartGestureEventName);
    if (!g_event) {
        Wh_Log(L"OpenEventW failed (%u); polling", GetLastError());
    }

    g_thread = CreateThread(nullptr, 0, ReaderThread, nullptr, 0, nullptr);
    if (!g_thread) {
        Wh_Log(L"CreateThread failed (%u)", GetLastError());
    }

    Wh_Log(L"Consumer initialized");
    return TRUE;
}

void Wh_ModUninit() {
    g_stop = true;
    // Wake before joining: the reader waits on the shared event with a 50 ms
    // timeout, and the event is the one wake that does not depend on the timeout.
    if (g_event) {
        SetEvent(g_event);
    }
    if (g_thread) {
        const DWORD waited = WaitForSingleObject(g_thread, 1000);
        if (waited != WAIT_OBJECT_0) {
            // A timeout means the reader may still be inside ReadSnapshot or
            // LogSnapshot. Every resource it can touch stays mapped, and the
            // handle stays open; releasing them here is the use-after-unmap this
            // path exists to avoid.
            Wh_Log(L"Consumer uninit residual: reader thread still running; "
                   L"mapping kept");
            return;
        }
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_event) {
        CloseHandle(g_event);
        g_event = nullptr;
    }
    if (g_shared) {
        UnmapViewOfFile(g_shared);
        g_shared = nullptr;
    }
    if (g_mapping) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
    }
    Wh_Log(L"Consumer uninitialized");
}
