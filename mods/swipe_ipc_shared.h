#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <windows.h>


// Shared-memory contract between the StartMenuExperienceHost-side controller and
// the explorer-side bridge.
//
// Two processes, one session-local section, four auto-reset events, one
// manual-reset command event and one mutex. There is no file and no polling:
// every record carries its own sequence number, and a reader accepts a record
// only when that number did not move while the record was being copied.
//
//   Local\SwipeFor10Start_v6_State        SwipeIpcState
//   Local\SwipeFor10Start_v6_Cmd          manual-reset, tools -> Start
//   Local\SwipeFor10Start_v6_CmdWrite     mutex,         one command writer
//   Local\SwipeFor10Start_v6_Intent       auto-reset,   bridge -> Start
//   Local\SwipeFor10Start_v6_IntentAck    auto-reset,   Start -> bridge
//   Local\SwipeFor10Start_v6_Exec         auto-reset,   Start -> bridge
//   Local\SwipeFor10Start_v6_ExecAck      auto-reset,   bridge -> Start
//
// The names are session-local because both processes run as the same user in the
// same session. The version is part of every name, so a layout change is a new
// channel instead of a silent mismatch.
//
// ---------------------------------------------------------------------------
// Publishing: one seqlock per record
// ---------------------------------------------------------------------------
//
// Every record a reader copies more than one word from has its own counter. The
// writer bumps the counter to an odd value, writes the fields, and bumps it back
// to even. The reader reads the counter, copies the fields, reads the counter
// again, and accepts the copy only when both reads are equal and even. A torn
// copy is therefore impossible to accept.
//
// One counter for the whole section would not work: the records are written by
// different processes (the tools the command, the Start side the open generation
// and the executor command, the bridge the intent), and a shared counter would
// invalidate a record another side is reading.
//
// A counter left odd means its writer died between the two bumps. The next
// writer takes the record at the next odd value, so no reader can accept the
// fields the dead writer left behind; a reader treats an odd counter as "no
// stable record" after a bounded number of attempts and falls back.
//
// ---------------------------------------------------------------------------
// Who initialises the section
// ---------------------------------------------------------------------------
//
// The section exists as soon as the first of the two processes calls
// CreateFileMappingW, and the other can map it before a single byte was written.
// Creation is therefore not initialisation:
//
//   1. A process claims the section by an interlocked exchange of initState from
//      blank (0) or abandoned (3) to initialising (1). It writes initTick first.
//   2. The claimer writes magic, version and layoutBytes, then publishes
//      initState = ready (2).
//   3. A process that finds initState = initialising waits and re-checks, in one
//      millisecond steps, for kSwipeIpcOpenBudgetMs: kSwipeIpcInitAbandonMs plus
//      one kSwipeIpcInitWaitMs window, so a dead claim becomes abandonable while
//      this call is still waiting for it.
//   4. A claim older than kSwipeIpcInitAbandonMs is marked abandoned, and the
//      next attempt claims it. A process that died inside step 2 therefore does
//      not brick the channel for the rest of the session.
//   5. Any other initState value is refused with ERROR_INVALID_DATA at once.
//
// SwipeIpcOpen may sleep for its whole budget. Call it from mod init and never
// from a hook.
//
// A reader tells the two failures apart by the error code:
//   ERROR_NOT_READY    no section was handed over: the section is not ready yet
//                      or the budget expired. Nothing was changed.
//   ERROR_INVALID_DATA initState is not one of the four values, or it says ready
//                      while magic, version or layoutBytes disagrees.
//
// Neither side ever destroys a section the other side may hold: SwipeIpcClose
// closes this process's handles only.
//
// ---------------------------------------------------------------------------
// The records
// ---------------------------------------------------------------------------
//
// Command (tools -> Start): one line in the syntax `docs/ipc.md` documents. The
// command event is manual-reset. Only one writer may publish at a time:
// `tools/ipc.ps1` takes kSwipeIpcCommandMutexName.
//
// Open generation (Start -> bridge): a counter the Start side publishes when it
// sees a fresh menu open. The even value is the identity of that menu. A counter
// of 0 is not a generation; SwipeIpcReadOpen reports ERROR_NOT_READY.
//
// Intent (bridge -> Start): the one decision record. kind is open/close/external,
// trigger names the adapter (win, startbutton, clickaway, touch, override), and
// method/flags are the shell call's own. `open` is the generation the bridge saw.
// The Start side answers with intentAck (state, duration, entry, hr), echoing the
// intent's sequence.
//
// Executor command (Start -> bridge): show, dismiss or cancel. dismiss carries
// the GetTickCount64 deadline at which the bridge must call the launcher; cancel
// names the dismiss sequence it supersedes. The bridge answers with execAck
// (state, hr, visible), where visible is the IsVisible result.
//
// Controller state (Start -> bridge): one direction word and one touch flag.
// controllerDirection is the whole decision: the bridge reads that single word,
// never a pair of flags, so it cannot see new ownership beside a stale phase.
// Values: kSwipeDirectionUnowned / Open / Closing.
//
// Peer liveness (Start -> bridge): peerState and peerTick. The bridge creates
// the section, so a mapped section says nothing about whether the Start side
// is running; this pair is the answer the bridge's interception gate needs.
// peerState is alive after a successful init and gone when the Start mod
// unloads; peerTick is the low 32 bits of GetTickCount64, refreshed from the
// Start side's input thread at most once per kSwipePeerTickIntervalMs. The
// bridge intercepts only while the state is alive and the tick is younger than
// kSwipePeerStaleMs: a clean disable is immediate through peerState, and a
// crashed host is caught by the tick going stale.

constexpr wchar_t kSwipeIpcStateName[] =
    L"Local\\SwipeFor10Start_v6_State";
constexpr wchar_t kSwipeIpcCommandEventName[] =
    L"Local\\SwipeFor10Start_v6_Cmd";
constexpr wchar_t kSwipeIpcCommandMutexName[] =
    L"Local\\SwipeFor10Start_v6_CmdWrite";
constexpr wchar_t kSwipeIpcIntentEventName[] =
    L"Local\\SwipeFor10Start_v6_Intent";
constexpr wchar_t kSwipeIpcIntentAckEventName[] =
    L"Local\\SwipeFor10Start_v6_IntentAck";
constexpr wchar_t kSwipeIpcExecEventName[] =
    L"Local\\SwipeFor10Start_v6_Exec";
constexpr wchar_t kSwipeIpcExecAckEventName[] =
    L"Local\\SwipeFor10Start_v6_ExecAck";

constexpr uint32_t kSwipeIpcMagic = 0x53313053;  // 'S' '0' '1' 'S', LE
// v6 adds the Start side's liveness pair (`peerTick`, `peerState`) at the tail.
// v5 replaced v4's `controllerOwning` + `controllerPhase` pair with one
// direction word and dropped `controllerOpen`, which no reader used. Each change
// moves the section size, so the version is bumped deliberately and stays part
// of every object name: a v5 section left by an older build is a different
// channel, and `layoutBytes` refuses it if one is ever handed over anyway.
constexpr uint32_t kSwipeIpcVersion = 6;

// One command per line, in the same syntax `docs/ipc.md` documents.
constexpr size_t kSwipeIpcCommandMaxBytes = 512;

// M3 intents, executor commands and acknowledgements.
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

// initState values; see "Who initialises the section" above.
constexpr LONG kSwipeIpcBlank = 0;
constexpr LONG kSwipeIpcInitialising = 1;
constexpr LONG kSwipeIpcReady = 2;
constexpr LONG kSwipeIpcAbandoned = 3;

// One wait step for a live claim, the age at which a claim is dead, and the
// whole budget of one SwipeIpcOpen call: the budget must outlast the abandon age,
// or a caller that arrives while a claim is still young would leave without a
// channel. See "Who initialises the section" above.
constexpr ULONGLONG kSwipeIpcInitWaitMs = 50;
constexpr ULONGLONG kSwipeIpcInitAbandonMs = 500;
constexpr ULONGLONG kSwipeIpcOpenBudgetMs =
    kSwipeIpcInitAbandonMs + kSwipeIpcInitWaitMs;

// How many times a reader retries a record whose counter moved. A record is a
// handful of word-sized writes, so a live writer leaves the odd state within
// nanoseconds; a writer that is preempted mid-record fails these attempts and
// the caller falls back instead of spinning.
constexpr int kSwipeIpcReadAttempts = 8;

// Peer liveness. The publisher refreshes peerTick at most once per interval; the
// reader declares the peer gone after the window without a refresh. The window
// is six intervals: five consecutive refreshes may be missed without closing the
// gate, so an ordinary stall of the input thread (a settle, a busy message loop)
// does not flap it, while a killed Start host stops every interception within
// 0.6 s.
constexpr uint32_t kSwipePeerTickIntervalMs = 100;
constexpr uint32_t kSwipePeerStaleMs = 600;

// peerState values. A fresh section reads 0, so `gone` is also what the bridge
// finds before the Start side ever attaches.
constexpr int32_t kSwipePeerGone = 0;
constexpr int32_t kSwipePeerAlive = 1;

// The layout is written once, here, and both sides include this header.
// Windhawk compiles each mod from stdin against the repository include path, and
// a copy in each source would drift. The asserts below pin every field.
struct SwipeIpcState {
    uint32_t magic;
    uint32_t version;

    // Tools -> Start. cmdSeq is the record's seqlock counter.
    volatile LONG64 cmdSeq;
    char cmdText[kSwipeIpcCommandMaxBytes];
    volatile LONG64 cmdTick;

    // Start -> bridge. openSeq is the record's counter and its even value is
    // the open generation.
    volatile LONG64 openSeq;
    volatile LONG64 openTick;

    // Initialisation handshake; see "Who initialises the section".
    volatile LONG initState;
    volatile LONG layoutBytes;
    volatile LONG64 initTick;

    // M3: bridge -> Start intent (seqlock). `intentOpen` is the open generation
    // the bridge saw; `intentKind` is open/close/external and `intentTrigger`
    // names the adapter.
    volatile LONG64 intentSeq;
    volatile LONG64 intentTick;
    volatile LONG intentKind;
    volatile LONG intentTrigger;
    volatile LONG intentMethod;
    volatile LONG intentFlags;
    volatile LONG64 intentOpen;

    // M3: Start -> bridge answer to an intent (seqlock). `intentAckFor` echoes
    // the intent sequence; durationMs is the close settle the Start planned;
    // entryPercent is the open entry in tenths of a percent.
    volatile LONG64 intentAckSeq;
    volatile LONG64 intentAckFor;
    volatile LONG intentAckState;     // kSwipeAck*
    volatile LONG intentAckDuration;
    volatile LONG intentAckEntry;
    volatile LONG intentAckHr;

    // M3: Start -> bridge executor command (seqlock). Show and cancel run now;
    // dismiss is due at execExecuteAt (a GetTickCount64 deadline).
    volatile LONG64 execSeq;
    volatile LONG64 execTick;
    volatile LONG execKind;           // kSwipeExec*
    volatile LONG execMethod;
    volatile LONG execFlags;
    volatile LONG64 execExecuteAt;
    volatile LONG64 execOpen;
    volatile LONG64 execTarget;       // the dismiss sequence a cancel names

    // M3: bridge -> Start executor answer (seqlock). state is the outcome; hr is
    // the launcher's HRESULT and visible is IsVisible afterwards.
    volatile LONG64 execAckSeq;
    volatile LONG64 execAckFor;
    volatile LONG execAckState;
    volatile LONG execAckHr;
    volatile LONG execAckVisible;

    // M3/v5: the Start-side session. One authoritative direction word, so the
    // bridge's toggle rule reads a single atomic value and can never see new
    // ownership beside the previous phase. The v4 section published owning and
    // phase as separate words; a reader could see owning=1 with the old phase,
    // and the bridge read the still-open window as a close during a retract.
    volatile LONG controllerDirection;  // kSwipeDirection*
    // 1 while a touch contact owns the opening panel. The bridge swallows the
    // shell's click-away from that same opening contact; it is the finger that
    // pulled the menu out, not a press outside it.
    volatile LONG controllerTouch;
    // v6: the Start side's liveness. peerTick is the low 32 bits of
    // GetTickCount64, so the reader compares by unsigned difference and a wrap
    // after 49.7 days is not a discontinuity. peerState is kSwipePeer*.
    volatile LONG peerTick;
    volatile LONG peerState;
};

static_assert(offsetof(SwipeIpcState, magic) == 0, "layout");
static_assert(offsetof(SwipeIpcState, version) == 4, "layout");
static_assert(offsetof(SwipeIpcState, cmdSeq) == 8, "layout");
static_assert(offsetof(SwipeIpcState, cmdText) == 16, "layout");
static_assert(offsetof(SwipeIpcState, cmdTick) == 528, "layout");
static_assert(offsetof(SwipeIpcState, openSeq) == 536, "layout");
static_assert(offsetof(SwipeIpcState, openTick) == 544, "layout");
static_assert(offsetof(SwipeIpcState, initState) == 552, "layout");
static_assert(offsetof(SwipeIpcState, layoutBytes) == 556, "layout");
static_assert(offsetof(SwipeIpcState, initTick) == 560, "layout");
static_assert(offsetof(SwipeIpcState, intentSeq) == 568, "layout");
static_assert(offsetof(SwipeIpcState, intentTick) == 576, "layout");
static_assert(offsetof(SwipeIpcState, intentKind) == 584, "layout");
static_assert(offsetof(SwipeIpcState, intentTrigger) == 588, "layout");
static_assert(offsetof(SwipeIpcState, intentMethod) == 592, "layout");
static_assert(offsetof(SwipeIpcState, intentFlags) == 596, "layout");
static_assert(offsetof(SwipeIpcState, intentOpen) == 600, "layout");
static_assert(offsetof(SwipeIpcState, intentAckSeq) == 608, "layout");
static_assert(offsetof(SwipeIpcState, intentAckFor) == 616, "layout");
static_assert(offsetof(SwipeIpcState, intentAckState) == 624, "layout");
static_assert(offsetof(SwipeIpcState, intentAckDuration) == 628, "layout");
static_assert(offsetof(SwipeIpcState, intentAckEntry) == 632, "layout");
static_assert(offsetof(SwipeIpcState, intentAckHr) == 636, "layout");
static_assert(offsetof(SwipeIpcState, execSeq) == 640, "layout");
static_assert(offsetof(SwipeIpcState, execTick) == 648, "layout");
static_assert(offsetof(SwipeIpcState, execKind) == 656, "layout");
static_assert(offsetof(SwipeIpcState, execMethod) == 660, "layout");
static_assert(offsetof(SwipeIpcState, execFlags) == 664, "layout");
static_assert(offsetof(SwipeIpcState, execExecuteAt) == 672, "layout");
static_assert(offsetof(SwipeIpcState, execOpen) == 680, "layout");
static_assert(offsetof(SwipeIpcState, execTarget) == 688, "layout");
static_assert(offsetof(SwipeIpcState, execAckSeq) == 696, "layout");
static_assert(offsetof(SwipeIpcState, execAckFor) == 704, "layout");
static_assert(offsetof(SwipeIpcState, execAckState) == 712, "layout");
static_assert(offsetof(SwipeIpcState, execAckHr) == 716, "layout");
static_assert(offsetof(SwipeIpcState, execAckVisible) == 720, "layout");
static_assert(offsetof(SwipeIpcState, controllerDirection) == 724, "layout");
static_assert(offsetof(SwipeIpcState, controllerTouch) == 728, "layout");
static_assert(offsetof(SwipeIpcState, peerTick) == 732, "layout");
static_assert(offsetof(SwipeIpcState, peerState) == 736, "layout");
static_assert(sizeof(SwipeIpcState) == 744, "layout");

struct SwipeIpcChannel {
    SwipeIpcState* state;
    HANDLE mapping;
    HANDLE commandEvent;
    HANDLE commandMutex;
    HANDLE intentEvent;
    HANDLE intentAckEvent;
    HANDLE execEvent;
    HANDLE execAckEvent;
};

// Reads a 64-bit field that the other process may write at any time.
inline LONG64 SwipeIpcLoad64(volatile LONG64* value) {
    return InterlockedCompareExchange64(value, 0, 0);
}

// Takes a record for writing: the returned value is odd. A counter left odd by
// a writer that died is stepped forward to the next odd value, never back to the
// even one it left behind: that even value would be accepted by a reader
// together with the dead writer's half-written fields, and only the odd state
// keeps every reader out until this write is complete.
inline LONG64 SwipeIpcBeginWrite(volatile LONG64* sequence) {
    const LONG64 current = SwipeIpcLoad64(sequence);
    if ((current & 1) != 0) {
        return InterlockedExchangeAdd64(sequence, 2) + 2;
    }
    return InterlockedIncrement64(sequence);
}

// Releases a record: the returned value is even and is the published sequence.
inline LONG64 SwipeIpcEndWrite(volatile LONG64* sequence) {
    return InterlockedIncrement64(sequence);
}

// Copies one record and returns the sequence it was copied under. `copy` writes
// the fields into the caller's snapshot; it runs at least once and at most
// kSwipeIpcReadAttempts times. False means no stable record was copied.
template <typename Copier>
inline bool SwipeIpcSnapshot(volatile LONG64* sequence, LONG64* outSequence,
                             Copier&& copy) {
    for (int attempt = 0; attempt < kSwipeIpcReadAttempts; ++attempt) {
        const LONG64 before = SwipeIpcLoad64(sequence);
        if ((before & 1) != 0) {
            MemoryBarrier();
            continue;  // a write is in progress
        }
        copy();
        MemoryBarrier();
        if (SwipeIpcLoad64(sequence) != before) {
            continue;  // the record moved under the copy
        }
        *outSequence = before;
        return true;
    }
    return false;
}

// Creates the channel when the other side has not, initialises it when neither
// side has, then validates it. `created` reports that this call performed the
// initialisation. Every member is optional except the section itself: a missing
// event disables only the direction it carries, a missing mutex serialises
// nothing. On failure `*error` is ERROR_NOT_READY or ERROR_INVALID_DATA as
// described above, or the failing Win32 code.
//
// This call may sleep for kSwipeIpcOpenBudgetMs while another process finishes
// its claim, so it belongs at mod init. A hook must not call it: a hook that
// finds no channel takes its own fixed lead.
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

    // Claim, wait, or discover ready. A section that another process created is
    // initialised by whichever of the two gets here first, never by "the
    // creator": the creator may still be between its CreateFileMappingW and its
    // first write. The deadline outlasts the abandon age on purpose, so a claim
    // that is alive but stuck becomes this caller's instead of ending the wait
    // with no channel.
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
            // initTick goes first: a claim must carry its own age before it can
            // be judged dead by anyone.
            state->initTick = static_cast<LONG64>(GetTickCount64());
            if (InterlockedCompareExchange(&state->initState,
                                           kSwipeIpcInitialising,
                                           observed) == observed) {
                owner = true;
                break;
            }
            // Another process claimed it first. The next pass sees its state;
            // the budget still bounds this loop.
            if (GetTickCount64() >= deadline) {
                return fail(ERROR_NOT_READY);
            }
            continue;
        }
        if (observed != kSwipeIpcInitialising) {
            // Not a state this header publishes. Waiting cannot turn it into a
            // claim, and a loop that waited anyway would outlast the documented
            // budget. The section belongs to another build or is corrupt; it is
            // left untouched for its owner.
            return fail(ERROR_INVALID_DATA);
        }

        // Initialising. Either a live claim or one whose owner died inside it.
        // A claim older than the abandon age is taken over, which is what the
        // caller's whole budget is for; only a claim with no readable age can
        // outlast the budget.
        const ULONGLONG claimed =
            static_cast<ULONGLONG>(SwipeIpcLoad64(&state->initTick));
        const ULONGLONG now = GetTickCount64();
        if (claimed != 0 && now > claimed &&
            now - claimed > kSwipeIpcInitAbandonMs) {
            InterlockedCompareExchange(&state->initState, kSwipeIpcAbandoned,
                                       kSwipeIpcInitialising);
            // Re-observe whether this process or another one marked it; the
            // deadline check covers a takeover that another caller won first.
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
        state->version = kSwipeIpcVersion;
        state->layoutBytes = static_cast<LONG>(sizeof(SwipeIpcState));
        MemoryBarrier();
        InterlockedExchange(&state->initState, kSwipeIpcReady);
        *created = true;
    }

    if (state->initState != kSwipeIpcReady ||
        state->magic != kSwipeIpcMagic || state->version != kSwipeIpcVersion ||
        state->layoutBytes != static_cast<LONG>(sizeof(SwipeIpcState))) {
        // Broken, or written by another layout. Nothing here is repaired: the
        // other side may be live on it.
        *error = ERROR_INVALID_DATA;
        UnmapViewOfFile(state);
        CloseHandle(mapping);
        return false;
    }

    // Auto-reset except for the command event: a command that arrived while the
    // reader was busy must not be forgotten by a second reset.
    HANDLE commandEvent =
        CreateEventW(nullptr, TRUE, FALSE, kSwipeIpcCommandEventName);
    HANDLE commandMutex =
        CreateMutexW(nullptr, FALSE, kSwipeIpcCommandMutexName);
    HANDLE intentEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcIntentEventName);
    HANDLE intentAckEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcIntentAckEventName);
    HANDLE execEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcExecEventName);
    HANDLE execAckEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcExecAckEventName);

    *error = 0;
    channel->state = state;
    channel->mapping = mapping;
    channel->commandEvent = commandEvent;
    channel->commandMutex = commandMutex;
    channel->intentEvent = intentEvent;
    channel->intentAckEvent = intentAckEvent;
    channel->execEvent = execEvent;
    channel->execAckEvent = execAckEvent;
    return true;
}

// Drops this process's handles. The section, its events and its mutex stay alive
// while the other side holds them, so a restarted side reopens the same channel
// and finds the other side's records where it left them.
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
    if (channel->commandMutex) {
        CloseHandle(channel->commandMutex);
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
    *channel = SwipeIpcChannel{};
}

// Copies the newest command when it is newer than `consumed`, and advances it.
// A torn share of the record is retried inside SwipeIpcSnapshot; a record that
// never settles inside the attempt budget is reported as "no command" and is
// taken by the next event, or replaced by the next command.
inline bool SwipeIpcTakeCommand(SwipeIpcChannel* channel, char* buffer,
                                size_t count, LONG64* consumed) {
    if (!channel->state || count < 2) {
        return false;
    }

    char text[kSwipeIpcCommandMaxBytes] = {};
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->cmdSeq, &sequence, [&]() {
            std::memcpy(text, channel->state->cmdText, sizeof(text));
        })) {
        return false;
    }
    if (sequence == 0 || sequence == *consumed) {
        return false;
    }

    std::strncpy(buffer, text, count - 1);
    buffer[count - 1] = '\0';
    *consumed = sequence;
    return true;
}

// Publishes a fresh menu open and returns its generation (the even value of the
// record's counter, unique for every open). Called by the Start side when it
// sees the window uncloak, which is also where the open settle starts. This is
// where the Start side must cancel the close stamp it left; the bridge refuses a
// stamp older than this tick whether or not it does.
inline LONG64 SwipeIpcPublishOpen(SwipeIpcChannel* channel, uint64_t tickMs) {
    if (!channel->state) {
        return 0;
    }

    SwipeIpcBeginWrite(&channel->state->openSeq);
    InterlockedExchange64(&channel->state->openTick,
                          static_cast<LONG64>(tickMs));
    return SwipeIpcEndWrite(&channel->state->openSeq);
}

// Copies the open generation and its tick. A counter of 0 is not a generation:
// no open has been published yet, so the call fails with ERROR_NOT_READY and
// leaves the outputs untouched. A record that never settled fails with
// ERROR_RETRY. `error` may be null. No caller may treat 0 as a generation.
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

// ---------------------------------------------------------------------------
// M3: intents, executor commands and acks
// ---------------------------------------------------------------------------

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

// The controller's published direction, one atomic word. The three values are
// the whole vocabulary the bridge needs: it is a close when the controller owns
// an open, a reopen while the controller is closing, and the shell's cloak
// otherwise. `controllerTouch` stays a separate flag because it answers a
// different question (which contact owns the opening panel) and no reader
// combines it with the direction.
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

// ---------------------------------------------------------------------------
// Peer liveness (Start -> bridge)
// ---------------------------------------------------------------------------

// The Start side's input thread refreshes this from its idle tick and from every
// positioned report, throttled by kSwipePeerTickIntervalMs. The bridge never
// writes either word.
inline void SwipeIpcPublishPeerTick(SwipeIpcChannel* channel, uint64_t tickMs) {
    if (channel->state) {
        InterlockedExchange(
            &channel->state->peerTick,
            static_cast<LONG>(static_cast<uint32_t>(tickMs)));
    }
}

// `alive` must follow a fresh tick. The reader pairs an alive state with the
// tick it reads after it, so the previous activation's tick is never accepted as
// this one's.
inline void SwipeIpcSetPeerState(SwipeIpcChannel* channel, int32_t state) {
    if (channel->state) {
        InterlockedExchange(&channel->state->peerState, state);
    }
}

// True while the Start side is attached and its tick is younger than `staleMs`.
// The state is read on both sides of the tick: the publisher writes the tick
// before it publishes alive, so a tick read after an alive state is this
// activation's, and a `gone` that lands during the read is not missed.
inline bool SwipeIpcPeerAlive(SwipeIpcChannel* channel, uint64_t nowMs,
                              uint32_t staleMs) {
    if (!channel->state) {
        return false;
    }
    if (InterlockedCompareExchange(&channel->state->peerState, 0, 0) !=
        kSwipePeerAlive) {
        return false;
    }
    const uint32_t tick = static_cast<uint32_t>(
        InterlockedCompareExchange(&channel->state->peerTick, 0, 0));
    if (InterlockedCompareExchange(&channel->state->peerState, 0, 0) !=
        kSwipePeerAlive) {
        return false;
    }
    const uint32_t now = static_cast<uint32_t>(nowMs);
    return now - tick < staleMs;
}

