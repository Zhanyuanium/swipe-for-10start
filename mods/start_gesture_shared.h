#pragma once

#include <cstdint>
#include <windows.h>

// Shared-memory contract between the explorer-side gesture bridge and the
// StartMenuExperienceHost-side renderer.
//
// Explorer writes the live gesture handle and state; Start reads it and binds a
// Composition animation to the legacy Start UI's root visual.
//
// The name is local to the user session. Both processes run as the same user.

constexpr uint32_t kWindhawkStartGestureMagic = 0x53475042;  // 'SGPB'
constexpr uint32_t kWindhawkStartGestureVersion = 1;

constexpr wchar_t kWindhawkStartGestureMappingName[] =
    L"Local\\WindhawkStartGestureShared";
constexpr wchar_t kWindhawkStartGestureEventName[] =
    L"Local\\WindhawkStartGestureEvent";

enum class StartGestureState : uint32_t {
    None = 0,
    Active = 1,
    Committed = 2,
    Cancelled = 3,
};

enum class StartGestureEdge : int32_t {
    Left = 0,
    Top = 1,
    Right = 2,
    Bottom = 3,
};

struct StartGestureShared {
    uint32_t magic;
    uint32_t version;

    // Incremented around every write so the reader can detect a torn update.
    volatile uint32_t sequence;

    volatile uint32_t state;  // StartGestureState
    volatile int32_t edge;    // StartGestureEdge

    // Live input gesture handle from GestureAnimationData.
    volatile int64_t sourceId;
    volatile int64_t providerId;

    // Physical screen coordinates of the invocation/current point. These are
    // only used by the fallback per-frame path.
    volatile float startX;
    volatile float startY;
    volatile float currentX;
    volatile float currentY;

    volatile uint64_t gestureStartTick;
    volatile uint64_t lastUpdateTick;
};

static_assert(sizeof(StartGestureShared) <= 128,
              "StartGestureShared should stay cache-line sized");
