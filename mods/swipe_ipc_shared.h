#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <windows.h>

// Shared-memory contract between the StartMenuExperienceHost-side renderer and
// the explorer-side bridge.
//
// Two processes, one session-local section, three events and one mutex. There
// is no file and no polling: every record carries its own sequence number, and
// a reader accepts a record only when that number did not move while the record
// was being copied. The sequence number is never an authority by itself; only a
// complete, stable copy is.
//
// The names are session-local because both processes run as the same user in
// the same session. The version is part of every name, so a layout change is a
// new channel instead of a silent mismatch.
//
//   Local\SwipeFor10Start_v2_State        SwipeIpcState
//   Local\SwipeFor10Start_v2_Cmd          manual-reset, tools -> Start
//   Local\SwipeFor10Start_v2_CmdWrite     mutex,         one command writer
//   Local\SwipeFor10Start_v2_RetractReq   auto-reset,   bridge -> Start
//   Local\SwipeFor10Start_v2_RetractPlan  auto-reset,   Start -> bridge
//
// ---------------------------------------------------------------------------
// Publishing: one seqlock per record
// ---------------------------------------------------------------------------
//
// Every record that a reader copies more than one word from has its own
// counter. The writer bumps the counter to an odd value, writes the fields, and
// bumps it back to even. The reader reads the counter, copies the fields, reads
// the counter again, and accepts the copy only when both reads are equal and
// even. A torn copy is therefore impossible to accept: the writer's two bumps
// bracket every field of the record.
//
// One counter for the whole section would not work. The records are written by
// different processes: the tools write the command, the Start side the close
// record, the open generation and the retract plan, and the bridge the retract
// request. A shared counter would make one side's write invalidate a record
// another side is reading, so a reader could not tell its own record's write in
// progress from a foreign one, and the counter's odd/even ownership would have
// to be shared across two processes that never synchronise.
//
// A counter left odd means its writer died between the two bumps. The next
// writer of that record takes the record at the next odd value, so no reader can
// accept the fields the dead writer left behind; a reader treats an odd counter
// as "no stable record" after a bounded number of attempts and falls back. A
// reader never accepts a value it did not copy between two equal even reads.
//
// ---------------------------------------------------------------------------
// Who initialises the section
// ---------------------------------------------------------------------------
//
// The section exists as soon as the first of the two processes calls
// CreateFileMappingW, and the other can map it before a single byte was
// written. Creation is therefore not initialisation:
//
//   1. A process claims the section by an interlocked exchange of initState
//      from blank (0) or abandoned (3) to initialising (1). It writes initTick
//      first, so a stuck claim can be told from a live one.
//   2. The claimer writes magic, version and layoutBytes, then publishes
//      initState = ready (2). Those are the only writes of that block and they
//      are the same constants in both processes, so a claim taken over from a
//      dead instance rewrites the same bytes.
//   3. A process that finds initState = initialising waits and re-checks, in
//      one-millisecond steps, for kSwipeIpcOpenBudgetMs: kSwipeIpcInitAbandonMs
//      plus one kSwipeIpcInitWaitMs window, so a dead claim becomes abandonable
//      while this call is still waiting for it. Waiting past the abandon age is
//      the point of the budget. Both sides call SwipeIpcOpen exactly once, at
//      mod init, so a caller that gave up inside that window would hold no
//      channel for the rest of its attachment.
//   4. A claim older than kSwipeIpcInitAbandonMs is marked abandoned, and the
//      next attempt claims it. A process that died inside step 2 therefore does
//      not brick the channel for the rest of the session.
//
// SwipeIpcOpen may sleep for its whole budget. Call it from mod init and never
// from a hook: a hook that finds no channel takes its own fixed lead instead of
// sleeping on the shell's UI thread.
//
// A reader tells the two failures apart by the error code:
//   ERROR_NOT_READY    no section was handed over: either the section is not
//                      initialised yet (a claim with no readable age outlived
//                      the budget, or claims keep replacing each other). Nothing
//                      was changed; the caller runs without the channel.
//   ERROR_INVALID_DATA initState says ready, but magic, version or layoutBytes
//                      disagrees with this header. The section is broken or
//                      belongs to another build. It is left untouched.
//
// Neither side ever destroys or re-creates a section the other side may hold:
// SwipeIpcClose closes this process's handles only, and a section object stays
// alive while any process still has a handle open to it.
//
// ---------------------------------------------------------------------------
// The records
// ---------------------------------------------------------------------------
//
// Command (tools -> Start): one line in the syntax the README documents. The
// command event is manual-reset, so a command published while the reader is
// busy is not lost. Only one writer may publish at a time: a publisher holds
// kSwipeIpcCommandMutexName around its record. tools/ipc.ps1 is the only
// publisher in this repository, and it refuses to write when that mutex is
// missing. Two publishers that both skip it can interleave their text, and the
// seqlock then rejects the result instead of running half of each command. This
// header publishes no command of its own.
//
// Close (Start -> bridge), three publishes of one record:
//   - plan    at the release, when the close arms its Windows tap:
//             kind = gesture, tapped = 0, delayMs = 0, pull = the pull
//             generation that owns the close;
//   - tap     with a fresh sequence immediately before keybd_event:
//             kind = gesture, tapped = 1;
//   - cancel  the moment a new pull drops the close, and again at the tap's
//             deadline when that path was missed:
//             kind = cancelled, tapped = 0.
// Only tapped = 1 with kind = gesture names a Hide that needs no wait, and one
// such record is consumed by exactly one Hide. A plan and a cancel never cause
// a Hide of their own: a Hide that arrives while the plan is current is a
// keyboard close (the user pressed the key during the settle) and gets its own
// lead. delayMs is part of the layout for a wait no publisher writes: both mods
// publish 0, and a tapped Hide passes at once whatever the field holds.
//
// The close record carries no open generation, so the two sides bind it to a
// menu with three rules: the bridge refuses a tapped record at or below the
// sequence it seeded at attach, the bridge refuses one whose tick is older than
// the newest open generation it has seen, and the Start side cancels the stamp
// it left when it publishes a new open. The first two are enforced by the
// bridge; the third is a duty on the Start side, so the binding does not depend
// on it. Together they keep a stamp whose tap never produced a Hide from passing
// an unrelated Hide under the next menu. One window remains: GetTickCount64
// advances in about 16 ms steps, so a tap and the next open inside one tick
// cannot be ordered, and an unconsumed fresh stamp is evidence of correlation,
// not proof of cause. The gesture path is correct because of the
// plan-versus-tapped distinction, not because of that binding.
//
// Open generation (Start -> bridge): a counter the Start side publishes when it
// sees a fresh menu open. The even value is the identity of that menu, and the
// close and the retract request are bound to it. A counter of 0 is not a
// generation: it means no open has been published yet, so SwipeIpcReadOpen
// reports ERROR_NOT_READY instead of handing out a snapshot, and no request may
// name it. A request that reaches the Start side with generation 0 is refused
// exactly like one from a menu that is gone.
//
// Retract request (bridge -> Start) and plan (Start -> bridge): the request
// carries the open generation the bridge saw, its own tick and a deadline; the
// plan echoes the request's sequence number and carries the settle the Start
// side planned, or kSwipeRetractPlanNone. The Start side answers a request only
// when it is newer than everything it handled, bound to the current open
// generation, still inside its deadline, and not marked done. The bridge marks
// its request done when the Hide it was holding completes, so a request whose
// close is over cannot start a settle under the menu the user opens next.

constexpr wchar_t kSwipeIpcStateName[] =
    L"Local\\SwipeFor10Start_v2_State";
constexpr wchar_t kSwipeIpcCommandEventName[] =
    L"Local\\SwipeFor10Start_v2_Cmd";
constexpr wchar_t kSwipeIpcCommandMutexName[] =
    L"Local\\SwipeFor10Start_v2_CmdWrite";
constexpr wchar_t kSwipeIpcRetractRequestEventName[] =
    L"Local\\SwipeFor10Start_v2_RetractReq";
constexpr wchar_t kSwipeIpcRetractPlanEventName[] =
    L"Local\\SwipeFor10Start_v2_RetractPlan";

constexpr uint32_t kSwipeIpcMagic = 0x53313053;  // 'S' '0' '1' 'S', LE
constexpr uint32_t kSwipeIpcVersion = 2;

// One command per line, in the same syntax the README documents.
constexpr size_t kSwipeIpcCommandMaxBytes = 512;

// closeKind values. Only a tapped gesture close passes its Hide without a wait.
constexpr int32_t kSwipeCloseKindCancelled = 0;
constexpr int32_t kSwipeCloseKindGesture = 1;

// retractPlanMs when the Start side could not start a settle.
constexpr int32_t kSwipeRetractPlanNone = 1;

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

    // Start -> bridge. closeSeq is the record's seqlock counter.
    volatile LONG64 closeSeq;
    volatile LONG64 closeTick;
    volatile LONG closeKind;      // kSwipeCloseKind*
    volatile LONG closeDelayMs;
    volatile LONG closeTapped;    // 0 = plan, 1 = the tap was sent
    volatile LONG closePull;      // pull generation that owns the close

    // Bridge -> Start, then the answer back. Both are seqlock records.
    volatile LONG64 retractRequestSeq;
    volatile LONG64 retractRequestTick;
    volatile LONG retractPlanMs;      // kSwipeRetractPlanNone, or planned ms
    volatile LONG reserved0;
    volatile LONG64 retractRequestOpen;      // open generation the bridge saw
    volatile LONG64 retractRequestDeadline;  // tick after which it is void
    volatile LONG64 retractPlanSeq;          // the plan's seqlock counter
    volatile LONG64 retractPlanRequest;      // echoes the request's sequence
    volatile LONG64 retractCancelSeq;        // highest request the bridge ended

    // Start -> bridge. openSeq is the record's counter and its even value is
    // the open generation.
    volatile LONG64 openSeq;
    volatile LONG64 openTick;

    // Initialisation handshake; see "Who initialises the section".
    volatile LONG initState;
    volatile LONG layoutBytes;
    volatile LONG64 initTick;

    volatile LONG64 reserved[1];
};

static_assert(offsetof(SwipeIpcState, magic) == 0, "layout");
static_assert(offsetof(SwipeIpcState, version) == 4, "layout");
static_assert(offsetof(SwipeIpcState, cmdSeq) == 8, "layout");
static_assert(offsetof(SwipeIpcState, cmdText) == 16, "layout");
static_assert(offsetof(SwipeIpcState, cmdTick) == 528, "layout");
static_assert(offsetof(SwipeIpcState, closeSeq) == 536, "layout");
static_assert(offsetof(SwipeIpcState, closeTick) == 544, "layout");
static_assert(offsetof(SwipeIpcState, closeKind) == 552, "layout");
static_assert(offsetof(SwipeIpcState, closeDelayMs) == 556, "layout");
static_assert(offsetof(SwipeIpcState, closeTapped) == 560, "layout");
static_assert(offsetof(SwipeIpcState, closePull) == 564, "layout");
static_assert(offsetof(SwipeIpcState, retractRequestSeq) == 568, "layout");
static_assert(offsetof(SwipeIpcState, retractRequestTick) == 576, "layout");
static_assert(offsetof(SwipeIpcState, retractPlanMs) == 584, "layout");
static_assert(offsetof(SwipeIpcState, retractRequestOpen) == 592, "layout");
static_assert(offsetof(SwipeIpcState, retractRequestDeadline) == 600,
              "layout");
static_assert(offsetof(SwipeIpcState, retractPlanSeq) == 608, "layout");
static_assert(offsetof(SwipeIpcState, retractPlanRequest) == 616, "layout");
static_assert(offsetof(SwipeIpcState, retractCancelSeq) == 624, "layout");
static_assert(offsetof(SwipeIpcState, openSeq) == 632, "layout");
static_assert(offsetof(SwipeIpcState, openTick) == 640, "layout");
static_assert(offsetof(SwipeIpcState, initState) == 648, "layout");
static_assert(offsetof(SwipeIpcState, layoutBytes) == 652, "layout");
static_assert(offsetof(SwipeIpcState, initTick) == 656, "layout");
static_assert(sizeof(SwipeIpcState) == 672, "layout");

struct SwipeIpcChannel {
    SwipeIpcState* state;
    HANDLE mapping;
    HANDLE commandEvent;
    HANDLE commandMutex;
    HANDLE retractRequestEvent;
    HANDLE retractPlanEvent;
};

struct SwipeIpcCloseSnapshot {
    LONG64 seq;
    uint64_t tick;
    int32_t kind;
    int32_t delayMs;
    int32_t tapped;
    uint32_t pull;
};

struct SwipeIpcRetractRequest {
    LONG64 seq;
    uint64_t tick;
    uint64_t open;
    uint64_t deadline;
};

struct SwipeIpcRetractPlan {
    LONG64 seq;
    LONG64 request;
    int32_t settleMs;
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
            continue;  // another process claimed it first
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
            continue;
        }
        if (now >= deadline) {
            *error = ERROR_NOT_READY;
            UnmapViewOfFile(state);
            CloseHandle(mapping);
            return false;
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
    HANDLE retractRequestEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcRetractRequestEventName);
    HANDLE retractPlanEvent =
        CreateEventW(nullptr, FALSE, FALSE, kSwipeIpcRetractPlanEventName);

    *error = 0;
    channel->state = state;
    channel->mapping = mapping;
    channel->commandEvent = commandEvent;
    channel->commandMutex = commandMutex;
    channel->retractRequestEvent = retractRequestEvent;
    channel->retractPlanEvent = retractPlanEvent;
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
    if (channel->retractRequestEvent) {
        CloseHandle(channel->retractRequestEvent);
    }
    if (channel->retractPlanEvent) {
        CloseHandle(channel->retractPlanEvent);
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

// Publishes the close record. `tapped` is 0 for the plan and the cancel, 1 for
// the moment the Windows key goes down; `pull` is the generation that owns the
// close. The sequence number is fresh every time, so the bridge sees the tap as
// a new record even when the plan it follows is only milliseconds old.
inline bool SwipeIpcPublishClose(SwipeIpcChannel* channel, int32_t kind,
                                 uint32_t delayMs, int32_t tapped,
                                 uint32_t pull) {
    if (!channel->state) {
        return false;
    }

    SwipeIpcBeginWrite(&channel->state->closeSeq);
    InterlockedExchange64(&channel->state->closeTick,
                          static_cast<LONG64>(GetTickCount64()));
    InterlockedExchange(&channel->state->closeKind, kind);
    InterlockedExchange(&channel->state->closeDelayMs,
                        static_cast<LONG>(delayMs));
    InterlockedExchange(&channel->state->closeTapped, tapped != 0 ? 1 : 0);
    InterlockedExchange(&channel->state->closePull, static_cast<LONG>(pull));
    SwipeIpcEndWrite(&channel->state->closeSeq);
    return true;
}

// Copies the close record as one snapshot. False means no stable record exists
// (the section is uninitialised, or a writer died inside it); the caller then
// treats the Hide as a keyboard close.
inline bool SwipeIpcReadClose(SwipeIpcChannel* channel,
                              SwipeIpcCloseSnapshot* out) {
    if (!channel->state) {
        return false;
    }

    SwipeIpcCloseSnapshot record{};
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->closeSeq, &sequence, [&]() {
            record.tick = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->closeTick));
            record.kind =
                InterlockedCompareExchange(&channel->state->closeKind, 0, 0);
            record.delayMs = InterlockedCompareExchange(
                &channel->state->closeDelayMs, 0, 0);
            record.tapped = InterlockedCompareExchange(
                &channel->state->closeTapped, 0, 0);
            record.pull = static_cast<uint32_t>(InterlockedCompareExchange(
                &channel->state->closePull, 0, 0));
        })) {
        return false;
    }

    record.seq = sequence;
    *out = record;
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
// ERROR_RETRY. `error` may be null. No caller may treat 0 as a generation: the
// retract request then carries 0, which the Start side refuses.
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

// Publishes the retract request and returns the sequence number the answer must
// echo, or 0 when the channel is unavailable. `openGeneration` and `windowMs`
// bind the request to the menu it belongs to and to the close it serves: the
// Start side refuses a request whose generation is no longer current or whose
// deadline has passed.
inline LONG64 SwipeIpcRequestRetract(SwipeIpcChannel* channel,
                                     uint64_t openGeneration,
                                     uint64_t windowMs) {
    if (!channel->state || !channel->retractRequestEvent) {
        return 0;
    }

    const ULONGLONG now = GetTickCount64();
    SwipeIpcBeginWrite(&channel->state->retractRequestSeq);
    InterlockedExchange64(&channel->state->retractRequestTick,
                          static_cast<LONG64>(now));
    InterlockedExchange64(&channel->state->retractRequestOpen,
                          static_cast<LONG64>(openGeneration));
    InterlockedExchange64(&channel->state->retractRequestDeadline,
                          static_cast<LONG64>(now + windowMs));
    const LONG64 sequence = SwipeIpcEndWrite(&channel->state->retractRequestSeq);

    if (!SetEvent(channel->retractRequestEvent)) {
        return 0;
    }
    return sequence;
}

// Copies the request as one snapshot.
inline bool SwipeIpcReadRetractRequest(SwipeIpcChannel* channel,
                                       SwipeIpcRetractRequest* out) {
    if (!channel->state) {
        return false;
    }

    SwipeIpcRetractRequest record{};
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->retractRequestSeq, &sequence, [&]() {
            record.tick = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->retractRequestTick));
            record.open = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->retractRequestOpen));
            record.deadline = static_cast<uint64_t>(
                SwipeIpcLoad64(&channel->state->retractRequestDeadline));
        })) {
        return false;
    }

    record.seq = sequence;
    *out = record;
    return true;
}

// Marks a request done. The bridge calls this when the Hide it published the
// request for has completed, so a request whose close is over can never start a
// settle later. Only the bridge writes this field and it only grows.
inline void SwipeIpcClearRetractRequest(SwipeIpcChannel* channel,
                                        LONG64 requestSequence) {
    if (!channel->state || requestSequence <= 0) {
        return;
    }
    InterlockedExchange64(&channel->state->retractCancelSeq, requestSequence);
}

// The highest request the bridge has ended; a Start side that reads a request at
// or below this value ignores it.
inline LONG64 SwipeIpcReadRetractCancel(SwipeIpcChannel* channel) {
    return channel->state ? SwipeIpcLoad64(&channel->state->retractCancelSeq)
                          : 0;
}

// Publishes the duration of the settle that just started, or
// kSwipeRetractPlanNone when none did. `requestSequence` is the release: the
// bridge accepts only an answer that echoes its own request.
inline bool SwipeIpcAnswerRetract(SwipeIpcChannel* channel,
                                  LONG64 requestSequence, int32_t settleMs) {
    if (!channel->state || requestSequence <= 0) {
        return false;
    }

    SwipeIpcBeginWrite(&channel->state->retractPlanSeq);
    InterlockedExchange(&channel->state->retractPlanMs, settleMs);
    InterlockedExchange64(&channel->state->retractPlanRequest, requestSequence);
    SwipeIpcEndWrite(&channel->state->retractPlanSeq);

    return channel->retractPlanEvent &&
           SetEvent(channel->retractPlanEvent) != FALSE;
}

// Copies the plan as one snapshot; `request` is the sequence it answers.
inline bool SwipeIpcReadRetractPlan(SwipeIpcChannel* channel,
                                    SwipeIpcRetractPlan* out) {
    if (!channel->state) {
        return false;
    }

    SwipeIpcRetractPlan record{};
    LONG64 sequence = 0;
    if (!SwipeIpcSnapshot(&channel->state->retractPlanSeq, &sequence, [&]() {
            record.request = SwipeIpcLoad64(
                &channel->state->retractPlanRequest);
            record.settleMs = InterlockedCompareExchange(
                &channel->state->retractPlanMs, 0, 0);
        })) {
        return false;
    }

    record.seq = sequence;
    *out = record;
    return true;
}
