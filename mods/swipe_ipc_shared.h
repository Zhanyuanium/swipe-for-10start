#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <windows.h>

// Shared-memory contract between the StartMenuExperienceHost-side renderer and
// the explorer-side bridge.
//
// The two mods run in two processes, so everything they say to each other goes
// through one session-local section and three events. No file, no polling, no
// timestamp race: the writer publishes a sequence number last, and the reader
// accepts an answer only when it echoes the request's number.
//
// The names are session-local because both processes run as the same user in
// the same session. The version is part of every name, so a layout change is a
// new channel instead of a silent mismatch.
//
//   Local\SwipeFor10Start_v1_State        SwipeIpcState
//   Local\SwipeFor10Start_v1_Cmd          manual-reset, tools -> Start
//   Local\SwipeFor10Start_v1_RetractReq   auto-reset,   bridge -> Start
//   Local\SwipeFor10Start_v1_RetractPlan  auto-reset,   Start -> bridge
//
// Ownership:
//   - The command channel carries the debug commands (`p=`, `t=`, `e=`,
//     `anim=`) from a tool to the Start mod. It is a live signal, never state:
//     a Start mod that attaches later skips everything published before it.
//   - The close stamp is the Start side saying "a close I started is about to
//     tap the Windows key, and no wait is needed for its Hide". One stamp is
//     consumed by exactly one Hide: the tap's own.
//   - The retract channel is the bridge asking how long the retract it just
//     triggered lasts, and the Start side answering with the duration it
//     planned. Only that side knows the panel's travel.

constexpr wchar_t kSwipeIpcStateName[] =
    L"Local\\SwipeFor10Start_v1_State";
constexpr wchar_t kSwipeIpcCommandEventName[] =
    L"Local\\SwipeFor10Start_v1_Cmd";
constexpr wchar_t kSwipeIpcRetractRequestEventName[] =
    L"Local\\SwipeFor10Start_v1_RetractReq";
constexpr wchar_t kSwipeIpcRetractPlanEventName[] =
    L"Local\\SwipeFor10Start_v1_RetractPlan";

constexpr uint32_t kSwipeIpcMagic = 0x53313053;  // 'S' '0' '1' 'S', LE
constexpr uint32_t kSwipeIpcVersion = 1;

// One command per line, in the same syntax the README documents.
constexpr size_t kSwipeIpcCommandMaxBytes = 512;

// closeKind values. Only a gesture close passes its Hide without a wait.
constexpr int32_t kSwipeCloseKindCancelled = 0;
constexpr int32_t kSwipeCloseKindGesture = 1;

// retractPlanMs when the Start side could not start a settle.
constexpr int32_t kSwipeRetractPlanNone = 1;

// The layout is written twice by design: Windhawk compiles each mod from stdin
// against the repository include path, and a copy in each source would drift.
// The asserts below pin every field, and both sides include this one header.
struct SwipeIpcState {
    uint32_t magic;
    uint32_t version;

    // Tools -> Start. cmdText is published before cmdSeq.
    volatile LONG64 cmdSeq;
    char cmdText[kSwipeIpcCommandMaxBytes];
    volatile LONG64 cmdTick;

    // Start -> bridge. A close is published with closeSeq last.
    volatile LONG64 closeSeq;
    volatile LONG64 closeTick;
    volatile LONG closeKind;      // kSwipeCloseKind*
    volatile LONG closeDelayMs;

    // Bridge -> Start, then the answer back. The answer's seq echoes the
    // request's, and retractPlanMs is published before retractPlanSeq.
    volatile LONG64 retractRequestSeq;
    volatile LONG64 retractRequestTick;
    volatile LONG64 retractPlanSeq;
    volatile LONG retractPlanMs;  // kSwipeRetractPlanNone, or planned ms

    volatile LONG64 reserved[10];
};

static_assert(offsetof(SwipeIpcState, cmdSeq) == 8, "layout");
static_assert(offsetof(SwipeIpcState, cmdText) == 16, "layout");
static_assert(offsetof(SwipeIpcState, cmdTick) == 528, "layout");
static_assert(offsetof(SwipeIpcState, closeSeq) == 536, "layout");
static_assert(offsetof(SwipeIpcState, closeTick) == 544, "layout");
static_assert(offsetof(SwipeIpcState, closeKind) == 552, "layout");
static_assert(offsetof(SwipeIpcState, closeDelayMs) == 556, "layout");
static_assert(offsetof(SwipeIpcState, retractRequestSeq) == 560, "layout");
static_assert(offsetof(SwipeIpcState, retractRequestTick) == 568, "layout");
static_assert(offsetof(SwipeIpcState, retractPlanSeq) == 576, "layout");
static_assert(offsetof(SwipeIpcState, retractPlanMs) == 584, "layout");
static_assert(sizeof(SwipeIpcState) == 672, "layout");

struct SwipeIpcChannel {
    SwipeIpcState* state;
    HANDLE mapping;
    HANDLE commandEvent;
    HANDLE retractRequestEvent;
    HANDLE retractPlanEvent;
};

// Reads a 64-bit field that the other process may write at any time.
inline LONG64 SwipeIpcLoad64(volatile LONG64* value) {
    return InterlockedCompareExchange64(value, 0, 0);
}

// Creates the channel when the other side has not, then validates it. `created`
// reports which side initialized the section. Every member is optional except
// the section itself: a missing event disables only the direction it carries.
inline bool SwipeIpcOpen(SwipeIpcChannel* channel, bool* created, DWORD* error) {
    *created = false;

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0,
                                        sizeof(SwipeIpcState),
                                        kSwipeIpcStateName);
    if (!mapping) {
        *error = GetLastError();
        return false;
    }

    const bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
    auto* state = static_cast<SwipeIpcState*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                      sizeof(SwipeIpcState)));
    if (!state) {
        *error = GetLastError();
        CloseHandle(mapping);
        return false;
    }

    // Whoever creates the section also initializes it; the other side only
    // reads these two words, and both are written before any sequence number.
    if (fresh) {
        state->magic = kSwipeIpcMagic;
        state->version = kSwipeIpcVersion;
    }
    if (state->magic != kSwipeIpcMagic || state->version != kSwipeIpcVersion) {
        *error = ERROR_INVALID_DATA;
        UnmapViewOfFile(state);
        CloseHandle(mapping);
        return false;
    }

    // Auto-reset except for the command event: a command that arrived while the
    // reader was busy must not be forgotten by a second reset.
    HANDLE commandEvent =
        CreateEventW(nullptr, TRUE, FALSE, kSwipeIpcCommandEventName);
    HANDLE retractRequestEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcRetractRequestEventName);
    HANDLE retractPlanEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcRetractPlanEventName);

    *created = fresh;
    *error = 0;
    channel->state = state;
    channel->mapping = mapping;
    channel->commandEvent = commandEvent;
    channel->retractRequestEvent = retractRequestEvent;
    channel->retractPlanEvent = retractPlanEvent;
    return true;
}

inline void SwipeIpcClose(SwipeIpcChannel* channel) {
    if (channel->state) {
        UnmapViewOfFile(channel->state);
    }
    if (channel->mapping) {
        CloseHandle(channel->mapping);
    }
    if (channel->commandEvent) {
        CloseHandle(channel->commandEvent);
    }
    if (channel->retractRequestEvent) {
        CloseHandle(channel->retractRequestEvent);
    }
    if (channel->retractPlanEvent) {
        CloseHandle(channel->retractPlanEvent);
    }
    *channel = SwipeIpcChannel{};
}

// Copies `text` into the section and publishes it. Returns the sequence number
// it was published under, or 0 on failure.
inline LONG64 SwipeIpcSendCommand(SwipeIpcChannel* channel, const char* text) {
    if (!channel->state || !channel->commandEvent) {
        return 0;
    }

    char line[kSwipeIpcCommandMaxBytes] = {};
    std::strncpy(line, text, sizeof(line) - 1);
    // Strip the trailing newline an editor or a shell adds; the reader splits
    // on lines and an empty last line would be one more command.
    size_t length = std::strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
    if (length == 0) {
        return 0;
    }

    std::memcpy(channel->state->cmdText, line, length + 1);
    InterlockedExchange64(&channel->state->cmdTick,
                          static_cast<LONG64>(GetTickCount64()));
    const LONG64 sequence = InterlockedIncrement64(&channel->state->cmdSeq);
    if (!SetEvent(channel->commandEvent)) {
        return 0;
    }
    return sequence;
}

// Copies the newest command when it is newer than `consumed`, and advances it.
// A torn read is retried: the reader only acts on a text whose sequence number
// did not move while it was being copied.
inline bool SwipeIpcTakeCommand(SwipeIpcChannel* channel, char* buffer,
                                size_t count, LONG64* consumed) {
    if (!channel->state || count < 2) {
        return false;
    }

    for (int attempt = 0; attempt < 4; ++attempt) {
        const LONG64 sequence = SwipeIpcLoad64(&channel->state->cmdSeq);
        if (sequence == *consumed || sequence == 0) {
            return false;
        }

        char text[kSwipeIpcCommandMaxBytes] = {};
        std::memcpy(text, channel->state->cmdText, sizeof(text) - 1);
        MemoryBarrier();
        if (SwipeIpcLoad64(&channel->state->cmdSeq) != sequence) {
            continue;
        }

        std::strncpy(buffer, text, count - 1);
        buffer[count - 1] = '\0';
        *consumed = sequence;
        return true;
    }
    return false;
}

// Publishes a close stamp. `delayMs` is the wait the bridge still owes this
// Hide: 0 when the tap comes after the retract, as it does in this mod.
inline bool SwipeIpcStampClose(SwipeIpcChannel* channel, int32_t kind,
                               uint32_t delayMs) {
    if (!channel->state) {
        return false;
    }

    InterlockedExchange(&channel->state->closeKind, kind);
    InterlockedExchange(&channel->state->closeDelayMs,
                        static_cast<LONG>(delayMs));
    InterlockedExchange64(&channel->state->closeTick,
                          static_cast<LONG64>(GetTickCount64()));
    InterlockedIncrement64(&channel->state->closeSeq);
    return true;
}

// Publishes the retract request and returns the sequence number the answer must
// echo, or 0 when the channel is unavailable.
inline LONG64 SwipeIpcRequestRetract(SwipeIpcChannel* channel) {
    if (!channel->state || !channel->retractRequestEvent) {
        return 0;
    }

    InterlockedExchange64(&channel->state->retractRequestTick,
                          static_cast<LONG64>(GetTickCount64()));
    const LONG64 sequence = InterlockedIncrement64(
        &channel->state->retractRequestSeq);
    if (!SetEvent(channel->retractRequestEvent)) {
        return 0;
    }
    return sequence;
}

// Publishes the duration of the settle that just started, or
// kSwipeRetractPlanNone when none did. The sequence number is the release.
inline bool SwipeIpcAnswerRetract(SwipeIpcChannel* channel,
                                  LONG64 requestSequence, int32_t settleMs) {
    if (!channel->state || requestSequence <= 0) {
        return false;
    }

    InterlockedExchange(&channel->state->retractPlanMs, settleMs);
    InterlockedExchange64(&channel->state->retractPlanSeq, requestSequence);
    return channel->retractPlanEvent &&
           SetEvent(channel->retractPlanEvent) != FALSE;
}
