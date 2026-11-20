# IPC — the v4 channel

> Start 0.40.0 / bridge 0.21.0. One session-local section, seven named objects,
> every record a seqlock. There is no file and no polling on either side.

## Names and layout

```
Local\SwipeFor10Start_v4_State        SwipeIpcState (744 bytes, v4)
Local\SwipeFor10Start_v4_Cmd          manual-reset, tools -> Start
Local\SwipeFor10Start_v4_CmdWrite     mutex, one command writer
Local\SwipeFor10Start_v4_Intent       auto-reset, bridge -> Start
Local\SwipeFor10Start_v4_IntentAck    auto-reset, Start -> bridge
Local\SwipeFor10Start_v4_Exec         auto-reset, Start -> bridge
Local\SwipeFor10Start_v4_ExecAck      auto-reset, bridge -> Start
```

`kSwipeIpcMagic = 0x53313053` (`S01S`), `kSwipeIpcVersion = 4`. The names carry
the version, so a layout change is a new channel, never a silent mismatch.
Every field's offset is pinned by a `static_assert` in
`mods/swipe_ipc_shared.h`; `layoutBytes` publishes `sizeof(SwipeIpcState)` and a
reader that sees a different value refuses with `ERROR_INVALID_DATA` instead of
reading a foreign layout.

The v4 section contains only the records below. The M2 close/retract slots are
**gone** (v3 reserved them; v4 removes them). The layout is:

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
724  controllerOwning 728 controllerOpen   736 controllerPhase
740  controllerTouch
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
`intentAckSeq`; `intentAckFor` echoes the intent.

### Executor command — Start -> bridge

```
seq, tick, kind (show|dismiss|cancel), method, flags,
executeAt (GetTickCount64 deadline), open, target (the dismiss a cancel names)
```

Published by `SwipeIpcPublishExec`; `execEvent` wakes the bridge worker. `show`
and `cancel` run at once; `dismiss` is armed for `executeAt` and a later command
supersedes it (the same sequence posted twice is ignored). The bridge answers on
`execAckSeq` with `(command, state, hr, visible)`, where `visible` is the
`IsVisible` result: 0 right after Show, 1 at the 300 ms probe, 0 after Dismiss.

### Controller flags — Start -> bridge

Plain interlocked fields, read on every decision:

| Field | Meaning |
|---|---|
| `controllerOwning` | 1 from an intent/touch show until the close's hide is observed |
| `controllerOpen` | the open generation the controller owns |
| `controllerPhase` | 1 while the controller owns an open, 2 from the close's first frame, 0 idle |
| `controllerTouch` | 1 while the opening contact owns the panel |

`controllerPhase` becomes 2 in `TakeCloseOwnership`, before the close's settle
starts; it is not deferred to the tap. That is what lets the bridge's one toggle
rule see the whole retract as a close while the shell still reports state 5 and
the cloak still reports open. A pull that cancels a gesture close before its tap
republishes phase 1 at the cancel. The bridge reads `controllerOwning` first:
while it is set, phase 2 makes a press a reopen and any owned phase makes it a
close. The shell's cloak and internal state are only the resync for a panel the
controller does not own. A hide in flight (9/11) or an armed executor dismiss
still latches the press instead of publishing into a no-op show.
`controllerTouch` is the only reason a `HideStartAndCortana(7)` is not published
as a close.

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
unload. The section's `layoutBytes` is the guard: a new 744-byte build refuses a
v3 (840) or v2 (664) section an old build may still hold, logs
`ipc open failed err=13` (`ERROR_INVALID_DATA`), and attaches on the next
reload once the old handles are gone. Measured: install Start first, then the
bridge; if the bridge still holds the old section, one reload of the Start mod
attaches it.

## The debug command

`Local\SwipeFor10Start_v4_Cmd` is the only command path: `tools/ipc.ps1` takes
the mutex, writes one line, sets the event. The Start side's input thread takes
it (`SwipeIpcTakeCommand`) and applies the documented debug lines. Nothing
polls.

## What is not in the channel

No file, no named pipe, no window message between the processes. The section is
session-local because both processes run as the same user in the same session.
The bridge's hidden executor window is process-local; its only input is the
section.
