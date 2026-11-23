#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>


// Shared-memory contract between the Start-side controller and the explorer-side
// bridge: one session-local section and five auto-reset events, every record a
// seqlock. The names are Local\SwipeFor10Start_SingleMod5_*; that namespace is
// the ABI identity, and there is no product version word.

constexpr wchar_t kSwipeIpcStateName[] =
    L"Local\\SwipeFor10Start_SingleMod5_State";
constexpr wchar_t kSwipeIpcIntentEventName[] =
    L"Local\\SwipeFor10Start_SingleMod5_Intent";
constexpr wchar_t kSwipeIpcIntentAckEventName[] =
    L"Local\\SwipeFor10Start_SingleMod5_IntentAck";
constexpr wchar_t kSwipeIpcExecEventName[] =
    L"Local\\SwipeFor10Start_SingleMod5_Exec";
constexpr wchar_t kSwipeIpcExecAckEventName[] =
    L"Local\\SwipeFor10Start_SingleMod5_ExecAck";
constexpr wchar_t kSwipeIpcCapabilityEventName[] =
    L"Local\\SwipeFor10Start_SingleMod5_Capability";

constexpr uint32_t kSwipeIpcMagic = 0x53333053;

constexpr int32_t kSwipeIntentOpen = 0;
constexpr int32_t kSwipeIntentClose = 1;
constexpr int32_t kSwipeIntentExternal = 2;

constexpr int32_t kSwipeTriggerWin = 0;
constexpr int32_t kSwipeTriggerStartButton = 1;
constexpr int32_t kSwipeTriggerClickAway = 2;
constexpr int32_t kSwipeTriggerTouch = 3;
constexpr int32_t kSwipeTriggerOverride = 4;

constexpr int32_t kSwipeExecShow = 0;
constexpr int32_t kSwipeExecDismiss = 1;
constexpr int32_t kSwipeExecCancel = 2;

constexpr int32_t kSwipeAckPending = 0;
constexpr int32_t kSwipeAckExecuted = 1;
constexpr int32_t kSwipeAckRefused = 2;
constexpr int32_t kSwipeAckExpired = 3;

constexpr int32_t kSwipeAckConfirmed = 4;
constexpr int32_t kSwipeAckUnconfirmed = 5;
constexpr int32_t kSwipeAckUnknown = 6;

constexpr int32_t kSwipeAckSuperseded = 7;

constexpr int32_t kSwipeVisibilityUnknown = -1;
constexpr int32_t kSwipeVisibilityHidden = 0;
constexpr int32_t kSwipeVisibilityVisible = 1;

constexpr LONG kSwipeIpcBlank = 0;
constexpr LONG kSwipeIpcInitialising = 1;
constexpr LONG kSwipeIpcReady = 2;
constexpr LONG kSwipeIpcAbandoned = 3;

constexpr ULONGLONG kSwipeIpcInitWaitMs = 50;
constexpr ULONGLONG kSwipeIpcInitAbandonMs = 500;
constexpr ULONGLONG kSwipeIpcOpenBudgetMs =
    kSwipeIpcInitAbandonMs + kSwipeIpcInitWaitMs;

constexpr int kSwipeIpcReadAttempts = 8;

constexpr uint32_t kSwipePeerTickIntervalMs = 100;
constexpr uint32_t kSwipePeerStaleMs = 600;

constexpr int32_t kSwipePeerGone = 0;
constexpr int32_t kSwipePeerAlive = 1;

struct SwipeIpcState {
    uint32_t magic;

    volatile LONG64 openSeq;
    volatile LONG64 openTick;

    volatile LONG initState;
    volatile LONG layoutBytes;
    volatile LONG64 initTick;

    volatile LONG64 intentSeq;
    volatile LONG64 intentTick;
    volatile LONG intentKind;
    volatile LONG intentTrigger;
    volatile LONG intentMethod;
    volatile LONG intentFlags;
    volatile LONG64 intentOpen;

    volatile LONG64 intentAckSeq;
    volatile LONG64 intentAckFor;
    volatile LONG intentAckState;
    volatile LONG intentAckDuration;
    volatile LONG intentAckEntry;
    volatile LONG intentAckHr;

    volatile LONG64 execSeq;
    volatile LONG64 execTick;
    volatile LONG execKind;
    volatile LONG execMethod;
    volatile LONG execFlags;
    volatile LONG64 execExecuteAt;
    volatile LONG64 execOpen;
    volatile LONG64 execTarget;

    volatile LONG64 execAckSeq;
    volatile LONG64 execAckFor;
    volatile LONG execAckState;
    volatile LONG execAckHr;
    volatile LONG execAckVisible;

    volatile LONG controllerDirection;

    volatile LONG controllerTouch;

    volatile LONG peerTick;
    volatile LONG peerState;
    volatile LONG peerEpoch;

    volatile LONG peerPid;

    volatile LONG peerUiThread;

    volatile LONG64 intentTaken;

    volatile LONG execCapable;
};

static_assert(offsetof(SwipeIpcState, magic) == 0, "layout");
static_assert(offsetof(SwipeIpcState, openSeq) == 8, "layout");
static_assert(offsetof(SwipeIpcState, openTick) == 16, "layout");
static_assert(offsetof(SwipeIpcState, initState) == 24, "layout");
static_assert(offsetof(SwipeIpcState, layoutBytes) == 28, "layout");
static_assert(offsetof(SwipeIpcState, initTick) == 32, "layout");
static_assert(offsetof(SwipeIpcState, intentSeq) == 40, "layout");
static_assert(offsetof(SwipeIpcState, intentTick) == 48, "layout");
static_assert(offsetof(SwipeIpcState, intentKind) == 56, "layout");
static_assert(offsetof(SwipeIpcState, intentTrigger) == 60, "layout");
static_assert(offsetof(SwipeIpcState, intentMethod) == 64, "layout");
static_assert(offsetof(SwipeIpcState, intentFlags) == 68, "layout");
static_assert(offsetof(SwipeIpcState, intentOpen) == 72, "layout");
static_assert(offsetof(SwipeIpcState, intentAckSeq) == 80, "layout");
static_assert(offsetof(SwipeIpcState, intentAckFor) == 88, "layout");
static_assert(offsetof(SwipeIpcState, intentAckState) == 96, "layout");
static_assert(offsetof(SwipeIpcState, intentAckDuration) == 100, "layout");
static_assert(offsetof(SwipeIpcState, intentAckEntry) == 104, "layout");
static_assert(offsetof(SwipeIpcState, intentAckHr) == 108, "layout");
static_assert(offsetof(SwipeIpcState, execSeq) == 112, "layout");
static_assert(offsetof(SwipeIpcState, execTick) == 120, "layout");
static_assert(offsetof(SwipeIpcState, execKind) == 128, "layout");
static_assert(offsetof(SwipeIpcState, execMethod) == 132, "layout");
static_assert(offsetof(SwipeIpcState, execFlags) == 136, "layout");
static_assert(offsetof(SwipeIpcState, execExecuteAt) == 144, "layout");
static_assert(offsetof(SwipeIpcState, execOpen) == 152, "layout");
static_assert(offsetof(SwipeIpcState, execTarget) == 160, "layout");
static_assert(offsetof(SwipeIpcState, execAckSeq) == 168, "layout");
static_assert(offsetof(SwipeIpcState, execAckFor) == 176, "layout");
static_assert(offsetof(SwipeIpcState, execAckState) == 184, "layout");
static_assert(offsetof(SwipeIpcState, execAckHr) == 188, "layout");
static_assert(offsetof(SwipeIpcState, execAckVisible) == 192, "layout");
static_assert(offsetof(SwipeIpcState, controllerDirection) == 196, "layout");
static_assert(offsetof(SwipeIpcState, controllerTouch) == 200, "layout");
static_assert(offsetof(SwipeIpcState, peerTick) == 204, "layout");
static_assert(offsetof(SwipeIpcState, peerState) == 208, "layout");
static_assert(offsetof(SwipeIpcState, peerEpoch) == 212, "layout");
static_assert(offsetof(SwipeIpcState, peerPid) == 216, "layout");
static_assert(offsetof(SwipeIpcState, peerUiThread) == 220, "layout");
static_assert(offsetof(SwipeIpcState, intentTaken) == 224, "layout");
static_assert(offsetof(SwipeIpcState, execCapable) == 232, "layout");
static_assert(sizeof(SwipeIpcState) == 240, "layout");

struct SwipeIpcChannel {
    SwipeIpcState* state;
    HANDLE mapping;
    HANDLE intentEvent;
    HANDLE intentAckEvent;
    HANDLE execEvent;
    HANDLE execAckEvent;
    HANDLE capabilityEvent;
};

inline LONG64 SwipeIpcLoad64(volatile LONG64* value) {
    return InterlockedCompareExchange64(value, 0, 0);
}

inline LONG64 SwipeIpcBeginWrite(volatile LONG64* sequence) {
    const LONG64 current = SwipeIpcLoad64(sequence);
    if ((current & 1) != 0) {
        return InterlockedExchangeAdd64(sequence, 2) + 2;
    }
    return InterlockedIncrement64(sequence);
}

inline LONG64 SwipeIpcEndWrite(volatile LONG64* sequence) {
    return InterlockedIncrement64(sequence);
}

template <typename Copier>
inline bool SwipeIpcSnapshot(volatile LONG64* sequence, LONG64* outSequence,
                             Copier&& copy) {
    for (int attempt = 0; attempt < kSwipeIpcReadAttempts; ++attempt) {
        const LONG64 before = SwipeIpcLoad64(sequence);
        if ((before & 1) != 0) {
            MemoryBarrier();
            continue;
        }
        copy();
        MemoryBarrier();
        if (SwipeIpcLoad64(sequence) != before) {
            continue;
        }
        *outSequence = before;
        return true;
    }
    return false;
}

inline bool SwipeIpcOpen(SwipeIpcChannel* channel, bool* created, DWORD* error) {
    *created = false;
    *error = 0;

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0,
                                        sizeof(SwipeIpcState),
                                        kSwipeIpcStateName);
    if (!mapping) {
        *error = GetLastError();
        return false;
    }

    auto* state = static_cast<SwipeIpcState*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                      sizeof(SwipeIpcState)));
    if (!state) {
        *error = GetLastError();
        CloseHandle(mapping);
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() + kSwipeIpcOpenBudgetMs;
    const auto fail = [&](DWORD code) -> bool {
        *error = code;
        UnmapViewOfFile(state);
        CloseHandle(mapping);
        return false;
    };

    bool owner = false;
    for (;;) {
        const LONG observed = InterlockedCompareExchange(&state->initState, 0, 0);
        if (observed == kSwipeIpcReady) {
            break;
        }
        if (observed == kSwipeIpcBlank || observed == kSwipeIpcAbandoned) {

            state->initTick = static_cast<LONG64>(GetTickCount64());
            if (InterlockedCompareExchange(&state->initState,
                                           kSwipeIpcInitialising,
                                           observed) == observed) {
                owner = true;
                break;
            }

            if (GetTickCount64() >= deadline) {
                return fail(ERROR_NOT_READY);
            }
            continue;
        }
        if (observed != kSwipeIpcInitialising) {

            return fail(ERROR_INVALID_DATA);
        }

        const ULONGLONG claimed =
            static_cast<ULONGLONG>(SwipeIpcLoad64(&state->initTick));
        const ULONGLONG now = GetTickCount64();
        if (claimed != 0 && now > claimed &&
            now - claimed > kSwipeIpcInitAbandonMs) {
            InterlockedCompareExchange(&state->initState, kSwipeIpcAbandoned,
                                       kSwipeIpcInitialising);

            if (GetTickCount64() >= deadline) {
                return fail(ERROR_NOT_READY);
            }
            continue;
        }
        if (now >= deadline) {
            return fail(ERROR_NOT_READY);
        }
        Sleep(1);
    }

    if (owner) {
        state->magic = kSwipeIpcMagic;
        state->layoutBytes = static_cast<LONG>(sizeof(SwipeIpcState));
        MemoryBarrier();
        InterlockedExchange(&state->initState, kSwipeIpcReady);
        *created = true;
    }

    if (state->initState != kSwipeIpcReady ||
        state->magic != kSwipeIpcMagic ||
        state->layoutBytes != static_cast<LONG>(sizeof(SwipeIpcState))) {

        *error = ERROR_INVALID_DATA;
        UnmapViewOfFile(state);
        CloseHandle(mapping);
        return false;
    }

    HANDLE intentEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcIntentEventName);
    HANDLE intentAckEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcIntentAckEventName);
    HANDLE execEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcExecEventName);
    HANDLE execAckEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcExecAckEventName);
    HANDLE capabilityEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcCapabilityEventName);

    *error = 0;
    channel->state = state;
    channel->mapping = mapping;
    channel->intentEvent = intentEvent;
    channel->intentAckEvent = intentAckEvent;
    channel->execEvent = execEvent;
    channel->execAckEvent = execAckEvent;
    channel->capabilityEvent = capabilityEvent;
    return true;
}

inline void SwipeIpcClose(SwipeIpcChannel* channel) {
    if (channel->state) {
        UnmapViewOfFile(channel->state);
    }
    if (channel->mapping) {
        CloseHandle(channel->mapping);
    }
    if (channel->intentEvent) {
        CloseHandle(channel->intentEvent);
    }
    if (channel->intentAckEvent) {
        CloseHandle(channel->intentAckEvent);
    }
    if (channel->execEvent) {
        CloseHandle(channel->execEvent);
    }
    if (channel->execAckEvent) {
        CloseHandle(channel->execAckEvent);
    }
    if (channel->capabilityEvent) {
        CloseHandle(channel->capabilityEvent);
    }
    *channel = SwipeIpcChannel{};
}

inline LONG64 SwipeIpcPublishOpen(SwipeIpcChannel* channel, uint64_t tickMs) {
    if (!channel->state) {
        return 0;
    }

    SwipeIpcBeginWrite(&channel->state->openSeq);
    InterlockedExchange64(&channel->state->openTick,
                          static_cast<LONG64>(tickMs));
    return SwipeIpcEndWrite(&channel->state->openSeq);
}

inline bool SwipeIpcReadOpen(SwipeIpcChannel* channel, uint64_t* generation,
                             uint64_t* tickMs, DWORD* error = nullptr) {
    if (error) {
        *error = 0;
    }
    if (!channel->state) {
        if (error) {
            *error = ERROR_INVALID_HANDLE;
        }
        return false;
    }

    uint64_t tick = 0;
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->openSeq, &sequence, [&]() {
            tick = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->openTick));
        })) {
        if (error) {
            *error = ERROR_RETRY;
        }
        return false;
    }
    if (sequence == 0) {
        if (error) {
            *error = ERROR_NOT_READY;
        }
        return false;
    }

    *generation = static_cast<uint64_t>(sequence);
    *tickMs = tick;
    return true;
}

struct SwipeIpcIntent {
    LONG64 seq = 0;
    uint64_t tick = 0;
    int32_t kind = kSwipeIntentOpen;
    int32_t trigger = kSwipeTriggerWin;
    int32_t method = 0;
    int32_t flags = 0;
    uint64_t open = 0;
};

struct SwipeIpcIntentAck {
    LONG64 seq = 0;
    LONG64 intent = 0;
    int32_t state = kSwipeAckPending;
    int32_t durationMs = 0;
    int32_t entryPercent = 0;
    int32_t hr = 0;
};

struct SwipeIpcExecCommand {
    LONG64 seq = 0;
    uint64_t tick = 0;
    int32_t kind = kSwipeExecShow;
    int32_t method = 0;
    int32_t flags = 0;
    uint64_t executeAt = 0;
    uint64_t open = 0;
    int64_t target = 0;
};

struct SwipeIpcExecAck {
    LONG64 seq = 0;
    LONG64 command = 0;
    int32_t state = kSwipeAckPending;
    int32_t hr = 0;
    int32_t visible = 0;
};

inline LONG64 SwipeIpcPublishIntent(SwipeIpcChannel* channel, int32_t kind,
                                    int32_t trigger, int32_t method,
                                    int32_t flags, uint64_t open) {
    if (!channel->state) {
        return 0;
    }
    SwipeIpcBeginWrite(&channel->state->intentSeq);
    InterlockedExchange64(&channel->state->intentTick,
                          static_cast<LONG64>(GetTickCount64()));
    InterlockedExchange(&channel->state->intentKind, kind);
    InterlockedExchange(&channel->state->intentTrigger, trigger);
    InterlockedExchange(&channel->state->intentMethod, method);
    InterlockedExchange(&channel->state->intentFlags, flags);
    InterlockedExchange64(&channel->state->intentOpen,
                          static_cast<LONG64>(open));
    const LONG64 sequence = SwipeIpcEndWrite(&channel->state->intentSeq);
    if (channel->intentEvent) {
        SetEvent(channel->intentEvent);
    }
    return sequence;
}

inline bool SwipeIpcReadIntent(SwipeIpcChannel* channel, SwipeIpcIntent* out) {
    if (!channel->state) {
        return false;
    }
    SwipeIpcIntent record{};
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->intentSeq, &sequence, [&]() {
            record.tick = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->intentTick));
            record.kind =
                InterlockedCompareExchange(&channel->state->intentKind, 0, 0);
            record.trigger = InterlockedCompareExchange(
                &channel->state->intentTrigger, 0, 0);
            record.method = InterlockedCompareExchange(
                &channel->state->intentMethod, 0, 0);
            record.flags = InterlockedCompareExchange(
                &channel->state->intentFlags, 0, 0);
            record.open = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->intentOpen));
        })) {
        return false;
    }
    record.seq = sequence;
    *out = record;
    return sequence != 0;
}

inline void SwipeIpcTakeIntent(SwipeIpcChannel* channel, LONG64 seq) {
    if (channel->state && seq > 0) {
        InterlockedExchange64(&channel->state->intentTaken, seq);
    }
}

inline LONG64 SwipeIpcTakenIntent(SwipeIpcChannel* channel) {
    return channel->state ? SwipeIpcLoad64(&channel->state->intentTaken) : 0;
}

inline LONG64 SwipeIpcIntentSequence(SwipeIpcChannel* channel) {
    if (!channel->state) {
        return 0;
    }
    const LONG64 sequence = SwipeIpcLoad64(&channel->state->intentSeq);
    return (sequence & 1) != 0 ? 0 : sequence;
}

inline bool SwipeIpcAbortIntent(SwipeIpcChannel* channel, LONG64 seq) {
    if (!channel->state || seq <= 0 || (seq & 1) != 0) {
        return false;
    }
    return InterlockedCompareExchange64(&channel->state->intentSeq, seq + 1,
                                        seq) == seq;
}

inline bool SwipeIpcAnswerIntent(SwipeIpcChannel* channel, LONG64 intentSeq,
                                 int32_t state, int32_t durationMs,
                                 int32_t entryPercent, int32_t hr) {
    if (!channel->state || intentSeq <= 0) {
        return false;
    }
    SwipeIpcBeginWrite(&channel->state->intentAckSeq);
    InterlockedExchange64(&channel->state->intentAckFor, intentSeq);
    InterlockedExchange(&channel->state->intentAckState, state);
    InterlockedExchange(&channel->state->intentAckDuration, durationMs);
    InterlockedExchange(&channel->state->intentAckEntry, entryPercent);
    InterlockedExchange(&channel->state->intentAckHr, hr);
    SwipeIpcEndWrite(&channel->state->intentAckSeq);
    return channel->intentAckEvent &&
           SetEvent(channel->intentAckEvent) != FALSE;
}

inline bool SwipeIpcReadIntentAck(SwipeIpcChannel* channel,
                                  SwipeIpcIntentAck* out) {
    if (!channel->state) {
        return false;
    }
    SwipeIpcIntentAck record{};
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->intentAckSeq, &sequence, [&]() {
            record.intent = SwipeIpcLoad64(
                &channel->state->intentAckFor);
            record.state = InterlockedCompareExchange(
                &channel->state->intentAckState, 0, 0);
            record.durationMs = InterlockedCompareExchange(
                &channel->state->intentAckDuration, 0, 0);
            record.entryPercent = InterlockedCompareExchange(
                &channel->state->intentAckEntry, 0, 0);
            record.hr = InterlockedCompareExchange(
                &channel->state->intentAckHr, 0, 0);
        })) {
        return false;
    }
    record.seq = sequence;
    *out = record;
    return sequence != 0;
}

inline LONG64 SwipeIpcPublishExec(SwipeIpcChannel* channel, int32_t kind,
                                  int32_t method, int32_t flags,
                                  uint64_t executeAt, uint64_t open,
                                  int64_t target = 0) {
    if (!channel->state) {
        return 0;
    }
    SwipeIpcBeginWrite(&channel->state->execSeq);
    InterlockedExchange64(&channel->state->execTick,
                          static_cast<LONG64>(GetTickCount64()));
    InterlockedExchange(&channel->state->execKind, kind);
    InterlockedExchange(&channel->state->execMethod, method);
    InterlockedExchange(&channel->state->execFlags, flags);
    InterlockedExchange64(&channel->state->execExecuteAt,
                          static_cast<LONG64>(executeAt));
    InterlockedExchange64(&channel->state->execOpen,
                          static_cast<LONG64>(open));
    InterlockedExchange64(&channel->state->execTarget, target);
    const LONG64 sequence = SwipeIpcEndWrite(&channel->state->execSeq);
    if (channel->execEvent) {
        SetEvent(channel->execEvent);
    }
    return sequence;
}

inline bool SwipeIpcReadExec(SwipeIpcChannel* channel,
                             SwipeIpcExecCommand* out) {
    if (!channel->state) {
        return false;
    }
    SwipeIpcExecCommand record{};
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->execSeq, &sequence, [&]() {
            record.tick = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->execTick));
            record.kind = InterlockedCompareExchange(
                &channel->state->execKind, 0, 0);
            record.method = InterlockedCompareExchange(
                &channel->state->execMethod, 0, 0);
            record.flags = InterlockedCompareExchange(
                &channel->state->execFlags, 0, 0);
            record.executeAt = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->execExecuteAt));
            record.open = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->execOpen));
            record.target = static_cast<int64_t>(
                SwipeIpcLoad64(&channel->state->execTarget));
        })) {
        return false;
    }
    record.seq = sequence;
    *out = record;
    return sequence != 0;
}

inline bool SwipeIpcAnswerExec(SwipeIpcChannel* channel, LONG64 execSeq,
                               int32_t state, int32_t hr, int32_t visible) {
    if (!channel->state || execSeq <= 0) {
        return false;
    }
    SwipeIpcBeginWrite(&channel->state->execAckSeq);
    InterlockedExchange64(&channel->state->execAckFor, execSeq);
    InterlockedExchange(&channel->state->execAckState, state);
    InterlockedExchange(&channel->state->execAckHr, hr);
    InterlockedExchange(&channel->state->execAckVisible, visible);
    SwipeIpcEndWrite(&channel->state->execAckSeq);
    return channel->execAckEvent &&
           SetEvent(channel->execAckEvent) != FALSE;
}

inline bool SwipeIpcReadExecAck(SwipeIpcChannel* channel,
                                SwipeIpcExecAck* out) {
    if (!channel->state) {
        return false;
    }
    SwipeIpcExecAck record{};
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->execAckSeq, &sequence, [&]() {
            record.command = SwipeIpcLoad64(
                &channel->state->execAckFor);
            record.state = InterlockedCompareExchange(
                &channel->state->execAckState, 0, 0);
            record.hr = InterlockedCompareExchange(
                &channel->state->execAckHr, 0, 0);
            record.visible = InterlockedCompareExchange(
                &channel->state->execAckVisible, 0, 0);
        })) {
        return false;
    }
    record.seq = sequence;
    *out = record;
    return sequence != 0;
}

constexpr int32_t kSwipeDirectionUnowned = 0;
constexpr int32_t kSwipeDirectionOpen = 1;
constexpr int32_t kSwipeDirectionClosing = 2;

inline void SwipeIpcSetControllerDirection(SwipeIpcChannel* channel,
                                           int32_t direction) {
    if (channel->state) {
        InterlockedExchange(&channel->state->controllerDirection, direction);
    }
}

inline int32_t SwipeIpcControllerDirection(SwipeIpcChannel* channel) {
    return channel->state
               ? InterlockedCompareExchange(
                     &channel->state->controllerDirection, 0, 0)
               : kSwipeDirectionUnowned;
}

inline void SwipeIpcSetControllerTouch(SwipeIpcChannel* channel, bool touch) {
    if (channel->state) {
        InterlockedExchange(&channel->state->controllerTouch, touch ? 1 : 0);
    }
}

inline bool SwipeIpcControllerTouch(SwipeIpcChannel* channel) {
    return channel->state &&
           InterlockedCompareExchange(&channel->state->controllerTouch, 0,
                                      0) != 0;
}

inline void SwipeIpcPublishPeerTick(SwipeIpcChannel* channel, uint64_t tickMs) {
    if (channel->state) {
        InterlockedExchange(
            &channel->state->peerTick,
            static_cast<LONG>(static_cast<uint32_t>(tickMs)));
    }
}

inline void SwipeIpcSetPeerState(SwipeIpcChannel* channel, int32_t state) {
    if (channel->state) {
        InterlockedExchange(&channel->state->peerState, state);
    }
}

inline uint32_t SwipeIpcBeginPeerActivation(SwipeIpcChannel* channel) {
    if (!channel->state) {
        return 0;
    }
    return static_cast<uint32_t>(
        InterlockedIncrement(&channel->state->peerEpoch));
}

inline void SwipeIpcSetPeerPid(SwipeIpcChannel* channel, uint32_t pid) {
    if (channel->state) {
        InterlockedExchange(&channel->state->peerPid,
                            static_cast<LONG>(pid));
    }
}

inline uint32_t SwipeIpcPeerPid(SwipeIpcChannel* channel) {
    return channel->state
               ? static_cast<uint32_t>(
                     InterlockedCompareExchange(&channel->state->peerPid, 0, 0))
               : 0;
}

inline uint32_t SwipeIpcPeerEpoch(SwipeIpcChannel* channel) {
    return channel->state
               ? static_cast<uint32_t>(
                     InterlockedCompareExchange(&channel->state->peerEpoch, 0,
                                                0))
               : 0;
}

inline void SwipeIpcSetPeerUiThread(SwipeIpcChannel* channel, uint32_t tid) {
    if (channel->state) {
        InterlockedExchange(&channel->state->peerUiThread,
                            static_cast<LONG>(tid));
    }
}

inline uint32_t SwipeIpcPeerUiThread(SwipeIpcChannel* channel) {
    return channel->state
               ? static_cast<uint32_t>(InterlockedCompareExchange(
                     &channel->state->peerUiThread, 0, 0))
               : 0;
}

inline int32_t SwipeIpcPeerState(SwipeIpcChannel* channel) {
    return channel->state
               ? InterlockedCompareExchange(&channel->state->peerState, 0, 0)
               : kSwipePeerGone;
}

inline uint32_t SwipeIpcPeerTick(SwipeIpcChannel* channel) {
    return channel->state
               ? static_cast<uint32_t>(InterlockedCompareExchange(
                     &channel->state->peerTick, 0, 0))
               : 0;
}

inline bool SwipeIpcPeerAlive(SwipeIpcChannel* channel, uint32_t staleMs) {
    if (!channel->state) {
        return false;
    }
    if (InterlockedCompareExchange(&channel->state->peerState, 0, 0) !=
        kSwipePeerAlive) {
        return false;
    }
    const uint32_t tick = static_cast<uint32_t>(
        InterlockedCompareExchange(&channel->state->peerTick, 0, 0));
    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    if (InterlockedCompareExchange(&channel->state->peerState, 0, 0) !=
        kSwipePeerAlive) {
        return false;
    }
    return now - tick < staleMs;
}

inline void SwipeIpcPublishExecCapable(SwipeIpcChannel* channel, bool capable) {
    if (!channel->state) {
        return;
    }
    InterlockedExchange(&channel->state->execCapable, capable ? 1 : 0);
    if (channel->capabilityEvent) {
        SetEvent(channel->capabilityEvent);
    }
}

inline bool SwipeIpcExecCapable(SwipeIpcChannel* channel) {
    return channel->state != nullptr &&
           InterlockedCompareExchange(&channel->state->execCapable, 0, 0) == 1;
}
