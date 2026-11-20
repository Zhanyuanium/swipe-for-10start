# IPC — the v6 channel

> Start 0.42.0 / bridge 0.23.0. One session-local section, seven named objects,
> every record a seqlock. There is no file and no polling on either side.

## Names and layout

```
Local\SwipeFor10Start_v6_State        SwipeIpcState (744 bytes, v6)
Local\SwipeFor10Start_v6_Cmd          manual-reset, tools -> Start
Local\SwipeFor10Start_v6_CmdWrite     mutex, one command writer
Local\SwipeFor10Start_v6_Intent       auto-reset, bridge -> Start
Local\SwipeFor10Start_v6_IntentAck    auto-reset, Start -> bridge
Local\SwipeFor10Start_v6_Exec         auto-reset, Start -> bridge
Local\SwipeFor10Start_v6_ExecAck      auto-reset, bridge -> Start
```

`kSwipeIpcMagic = 0x53313053` (`S01S`), `kSwipeIpcVersion = 6`. The names carry
the version, so a layout change is a new channel, never a silent mismatch. Every
field's offset is pinned by a `static_assert` in `mods/swipe_ipc_shared.h`;
`layoutBytes` publishes `sizeof(SwipeIpcState)` and a reader that sees a different
value refuses with `ERROR_INVALID_DATA` instead of reading a foreign layout.

**v6 adds the Start side's liveness pair.** `peerTick` and `peerState` extend the
tail (736 -> 744). The bridge creates the section, so a mapped channel is not
evidence that the Start side runs; this pair is the precondition every
interception reads. It is not a seqlock: two independent words, written by the
Start side only, read as one liveness answer (see "Peer liveness" below).

**v5 replaced v4's controller words.** v4 published `controllerOwning` and
`controllerPhase` as separate atomics, so a bridge reader could see new ownership
with the previous phase and read a close in flight as an open. v5 published one
`controllerDirection` word (`0` unowned, `1` open, `2` closing) that both sides
read and write, and dropped `controllerOpen`, which no reader used. That changed
the tail's meaning and the section size (744 -> 736), so the version was bumped
deliberately. The M2 close/retract slots are still **gone** (v3 reserved them;
v4 removed them). The layout is:

```
0    magic           4    version
8    cmdSeq          16   cmdText[512]     528  cmdTick
536  openSeq         544  openTick
552  initState       556  layoutBytes      560  initTick
568  intentSeq       576  intentTick       584  intentKind
588  intentTrigger   592  intentMethod     596  intentFlags
600  intentOpen
608  intentAckSeq    616  intentAckFor     624  intentAckState
628  intentAckDuration 632 intentAckEntry   636 intentAckHr
640  execSeq         648  execTick         656  execKind
660  execMethod      664  execFlags        672  execExecuteAt
680  execOpen        688  execTarget
696  execAckSeq      704  execAckFor       712  execAckState
716  execAckHr       720  execAckVisible
724  controllerDirection
728  controllerTouch
732  peerTick         736  peerState
size 744
```

## Seqlocks

A writer calls `SwipeIpcBeginWrite(&seq)` (the counter goes odd), writes the
payload, then `SwipeIpcEndWrite(&seq)` (the counter goes even and is the
published sequence). A reader calls `SwipeIpcSnapshot`: read the counter, refuse
an odd value, copy, then require the same even value. The sequence number is
never an authority by itself; only a complete, stable copy is.
`kSwipeIpcReadAttempts` is 8, so a writer that dies mid-record fails the reader
instead of spinning it.

## Records

### Open generation — Start -> bridge

`openSeq` (seqlock), `openTick` (uint64). Published when the Start side first
sees the window shown. The even `openSeq` is the generation; generation 0 is not
a menu and `SwipeIpcReadOpen` returns `ERROR_NOT_READY`. Every intent carries the
generation the bridge read.

### Intent — bridge -> Start

```
seq, tick, kind (open|close|external), trigger (win|startbutton|clickaway|touch|override),
method, flags, open
```

Published by `SwipeIpcPublishIntent` on the launcher thread, woken by the
`Intent` event. The Start side ignores a sequence it already handled and answers
with `SwipeIpcAnswerIntent(seq, state, durationMs, entryPercent, hr)` on
`intentAckSeq`; `intentAckFor` echoes the intent. The Start side publishes the
executor command first and then the ack: the ack reports the decision, it is not
a barrier the bridge waits for before the command.

### Executor command — Start -> bridge

```
seq, tick, kind (show|dismiss|cancel), method, flags,
executeAt (GetTickCount64 deadline), open, target (the dismiss a cancel names)
```

Published by `SwipeIpcPublishExec`; `execEvent` wakes the bridge worker. `show`
and `cancel` run at once; `dismiss` is armed for `executeAt` and a later command
supersedes it (the launcher-thread sequence watermark ignores the same record
delivered twice). The bridge answers on `execAckSeq` with
`(command, state, hr, visible)`, where `visible` is the `IsVisible` result: 0
right after Show, 1 at the 300 ms probe, 0 after Dismiss.

### Controller direction — Start -> bridge

| Field | Meaning |
|---|---|
| `controllerDirection` | the one decision: `0` unowned, `1` the controller owns an open, `2` a close is in flight |
| `controllerTouch` | 1 while the opening contact owns the panel |

`controllerDirection` becomes `2` in `TakeCloseOwnership`, before the close's
settle starts; it is not deferred to the dismiss. That is what lets the bridge's
one toggle rule see the whole retract as a close while the shell still reports
state 5 and the cloak still reports open. A pull that cancels a gesture close
before its dismiss is requested republishes `1` at the cancel. The bridge reads
that single word: `2` makes a press a reopen, `1` makes it a close, and `0` falls
back to the shell's cloak and internal state. A hide in flight (9/11) or an armed
executor dismiss still latches the press instead of publishing into a no-op show.
`controllerTouch` is the only trigger-level reason a `HideStartAndCortana(7)`
is not published as a close; the peer gate sits before every close intent, so a
call with no live controller passes to the shell instead.

### Peer liveness — Start -> bridge

| Field | Meaning |
|---|---|
| `peerTick` | low 32 bits of the Start side's `GetTickCount64`, refreshed from its input thread |
| `peerState` | `1` alive after a successful init, `0` gone when the Start mod unloads |

The bridge creates the section, so the channel stays mapped and valid while the
bridge runs even with the Start mod disabled. `peerState` is the controller's
answer to "am I here"; `peerTick` is the freshness that survives a host kill.
The bridge intercepts a trigger only while `peerState == 1` and
`(uint32_t)now - peerTick < kSwipePeerStaleMs` (600 ms); otherwise it logs one
`own pass trigger=... reason=no-peer` line and calls the shell's own path. The
close intent at `HideStartAndCortana` logs `exec pass hide method=... reason=no-peer`
and passes the same way.

The Start side's `OpenIpcChannel` writes a fresh tick and then `alive`, so a
reader that sees `alive` never pairs it with the previous activation's tick. The
input thread refreshes the tick from its 5 ms idle tick and from every positioned
report, at most once per `kSwipePeerTickIntervalMs` (100 ms). `Wh_ModUninit`
writes `gone` before it releases the view, and it does so before any residual
early return: a clean disable is immediate, and a residual unload also stops
every interception. A killed host leaves `alive` behind; the tick stops, and the
gate closes 600 ms later.

The window is six refresh intervals: five consecutive refreshes may be missed
without closing the gate, so a stalled input thread (a settle, a busy message
loop) does not flap it, and a kill still restores the shell's own toggle within
0.6 s.

## Initialisation

`SwipeIpcOpen` creates the section with `CreateFileMappingW(INVALID_HANDLE_VALUE,
PAGE_READWRITE, sizeof(SwipeIpcState), name)`; an existing section is opened and
validated. The `initState` handshake is a four-value CAS state machine
(`Blank -> Initialising -> Ready`, `Abandoned` after a dead claim); the whole
call has a 550 ms budget, so a young claim is abandoned inside the call rather
than waited on. A missing event disables only the direction it carries. Both
mods call it at `Wh_ModInit`, never in a hook.

A reinstall maps a new image at a new base while the old one stays pinned. The
new image opens the same section; the old image's handles are closed by its
unload. The names carry the version, so a v6 build opens its own section and
leaves a v5 (736 bytes) one to the old pair. If a section of another layout is
handed over anyway, `SwipeIpcOpen` refuses it with `ipc open failed err=13`
(`ERROR_INVALID_DATA`): `version` must be 6 and `layoutBytes` 744. A v4 section
was also 744 bytes, so the version word, not the size, separates the two.
Measured on the v5 upgrade: install Start first, then the bridge; a bridge that
still held the old section was one Start reload behind.

## The debug command

`Local\SwipeFor10Start_v6_Cmd` is the only command path: `tools/ipc.ps1` takes
the mutex, writes one line, sets the event. The Start side's input thread takes
it (`SwipeIpcTakeCommand`) and applies the documented debug lines. Nothing
polls.

## What is not in the channel

No file, no named pipe, no window message between the processes. The section is
session-local because both processes run as the same user in the same session.
The bridge's hidden executor window is process-local; its only input is the
section.
