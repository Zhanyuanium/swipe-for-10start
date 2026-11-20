# IPC — the v7 channel

> Start 0.44.0 / bridge 0.25.0. One session-local section, seven named objects,
> every record a seqlock. There is no file and no polling on either side.

## Names and layout

```
Local\SwipeFor10Start_v7_State        SwipeIpcState (744 bytes, v7)
Local\SwipeFor10Start_v7_Cmd          manual-reset, tools -> Start
Local\SwipeFor10Start_v7_CmdWrite     mutex, one command writer
Local\SwipeFor10Start_v7_Intent       auto-reset, bridge -> Start
Local\SwipeFor10Start_v7_IntentAck    auto-reset, Start -> bridge
Local\SwipeFor10Start_v7_Exec         auto-reset, Start -> bridge
Local\SwipeFor10Start_v7_ExecAck      auto-reset, bridge -> Start
```

`kSwipeIpcMagic = 0x53313053` (`S01S`), `kSwipeIpcVersion = 7`. The names carry
the version, so a layout change is a new channel, never a silent mismatch. Every
field's offset is pinned by a `static_assert` in `mods/swipe_ipc_shared.h`;
`layoutBytes` publishes `sizeof(SwipeIpcState)` and a reader that sees a different
value refuses with `ERROR_INVALID_DATA` instead of reading a foreign layout.

**v7 adds the Start side's activation identity.** `peerEpoch` fills the four
bytes after `peerState` (740), because the struct's 8-byte alignment had left
them, so the section stays 744 bytes. The size alone therefore no longer names the
layout: the version word, in the object names and in the section, separates v6
from v7. The field answers one question: *which Start activation is answering
now*. The Start side increments it once per attach, before any tick or `alive`;
the bridge reads it beside the press it latched, and a serve against a different
epoch is a serve into a controller that never saw that press, so the latch is
invalidated instead of published.

**v6 added the Start side's liveness pair.** `peerTick` and `peerState` extended
the tail (736 -> 744). The bridge creates the section, so a mapped channel is not
evidence that the Start side runs; this pair is the precondition every
interception reads. It is not a seqlock: independent words, written by the Start
side only, read as one liveness answer (see "Peer liveness" below).

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
732  peerTick        736  peerState        740  peerEpoch
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
generation the bridge read, and the Start side's recovery ticket is bound to the
same identity, so a landing can only serve an open for the menu it belongs to.

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

A landing reconciles an already-published `external` intent before it may issue a
recovery: the idle timer and the intent event are two wait objects of the same
thread, and the timer has the lower index, so the landing can otherwise run
before the takeover that arrived with it.

### Executor command — Start -> bridge

```
seq, tick, kind (show|dismiss|cancel), method, flags,
executeAt (GetTickCount64 deadline), open, target (the dismiss a cancel names)
```

Published by `SwipeIpcPublishExec`; `execEvent` wakes the bridge worker. `show`
and `cancel` run at once; `dismiss` is armed for `executeAt` and a later command
supersedes it (the launcher-thread sequence watermark ignores the same record
delivered twice). `execTick` is the publication tick and is the deadline of a
show and a cancel.

**Expiry.** The controller's budget is 400 ms: a show is answered within 400 ms
of its publication, a dismiss within 400 ms of the deadline it was given. The
comparison is `>= due + 400` on both sides, so a dispatch exactly at the boundary
is expired for the bridge and for the publisher. A
dispatch that starts after that is stale — the publisher has already cleared its
sequence, published a cancel where it could, and released ownership — so the
bridge answers `kSwipeAckExpired` and does not call the primitive. The publisher
does the same from its side: an unanswered dismiss is cancelled by sequence at its
timeout, and an unanswered show, which has no armed command to revoke, is
invalidated by the executor's age check.

A deadline is **not** a guaranteed response and not a lease on the shell call. A
primitive can enter before the deadline and return after the publisher timed out;
the timeout is a repair, not a cancel, and no scheme can recall a call already
inside the shell. What the bridge can still do is refuse a record whose publisher
activation changed: it reads `peerEpoch` when it accepts the record and again
immediately before the call, and answers `kSwipeAckSuperseded` on a mismatch. A
record that names a menu (`execOpen`; a dismiss carries the menu's open tick when
it is known, a show has no menu yet) is also refused when the section has already
moved to a later open generation (`exec dismiss ... superseded menu=... now=...`).
The Start side publishes a `cancel(target=0)` at attach for the same reason: a record
from the previous activation must not act on the menu the new one opens.

The bridge answers on `execAckSeq` with `(command, state, hr, visible)`, where
`visible` is the visibility probe, now three-valued: `kSwipeVisibilityHidden` (0),
`kSwipeVisibilityVisible` (1), and `kSwipeVisibilityUnknown` (-1) when there is no
launcher or `IsVisible` failed. A dismiss is answered in two steps: `executed`
means the primitive returned; then the 300 ms probe answers `confirmed` (hidden),
`unconfirmed` (visible), or `unknown` (probe or timer failed). A record replaced by
a newer one is answered `superseded`, which is distinct from `expired`. The log
prints the probe's real elapsed time (`after=<n> ms`), not the requested delay.
`unconfirmed` and `unknown` are terminal repairs for the Start side, which releases
ownership and reads the cloak on the next poll; `executed` alone is never taken
as a hide.

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

Both words belong to the activation that publishes them. The attach withdraws
availability, resets both to `0`, and only then claims the epoch; a kill while
`closing` (or with `controllerTouch` set) therefore cannot leave the previous
activation's decision readable as the next one's.

### Peer liveness and identity — Start -> bridge

| Field | Meaning |
|---|---|
| `peerEpoch` | incremented once per attach: the activation that answers now |
| `peerTick` | low 32 bits of the Start side's `GetTickCount64`, refreshed from its input thread |
| `peerState` | `1` while that consumer runs, `0` before it is ready and after it stops |

The bridge creates the section, so the channel stays mapped and valid while the
bridge runs even with the Start mod disabled. `peerState` is the controller's
answer to "am I here"; `peerTick` is the freshness that survives a host kill. The
bridge intercepts a trigger only while `peerState == 1` and
`(uint32_t)GetTickCount64() - peerTick < kSwipePeerStaleMs` (600 ms); otherwise it
logs one `own pass trigger=... reason=no-peer` line and calls the shell's own
path. The close intent at `HideStartAndCortana` logs
`exec pass hide method=... reason=no-peer` and passes the same way.

The reader takes the comparison clock **after** it reads `peerTick`, never from
its own earlier read: a heartbeat published in between would otherwise subtract
to a huge age and close the gate on a peer that had just proved it runs.

Publication order is `peerEpoch`, then `peerTick`, then `peerState=alive`. The
Start side refreshes the tick from its input thread at most once per
`kSwipePeerTickIntervalMs` (100 ms). A killed host leaves `alive` behind; the tick
stops, and the gate closes 600 ms later. A few presses during a start or a stop
are therefore served natively by the shell, by design.

`alive` means **intent-consumer readiness**: that thread's window, its message
loop and the intent event exist. It is not a claim about the digitizer: a refused
raw-input registration is logged by `RegisterHidUsage` and the intent half still
runs; the `ipc peer alive` line prints `touch=`/`pen=` so the difference is
visible. The publication and the terminal withdrawal are serialised in one
critical section, so a startup that is already in flight either lands before the
withdrawal or is refused after it; a flag check followed by an independent store
would let `alive` overwrite `gone`.


The window is six refresh intervals: five consecutive refreshes may be missed
without closing the gate, so a stalled input thread (a settle, a busy message
loop) does not flap it, and a kill still restores the shell's own toggle within
0.6 s. **600 ms is a detection budget, not a delivery guarantee.** A decision
taken inside the window can still be published after the peer died, and a press
latched inside the window can still be served after it. The residual windows are:
the gate check to the `SetEvent` of a published intent (sub-millisecond, but not
atomic against a kill), and, for the latch, the whole wait it is held for, which
is why the serve re-reads the peer and the epoch instead of trusting the press.

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
unload. The names carry the version, so a v7 build opens its own section and
leaves a v6 one of the same 744 bytes to the old pair; the version word is what
refuses it if it is handed over anyway (`ipc open failed err=13`,
`ERROR_INVALID_DATA`). Measured on the v5 upgrade: install Start first, then the
bridge; a bridge that still held the old section was one Start reload behind.

## The debug command

`Local\SwipeFor10Start_v7_Cmd` is the only command path: `tools/ipc.ps1` takes
the mutex, writes one line, sets the event. The Start side's input thread takes
it (`SwipeIpcTakeCommand`) and applies the documented debug lines. Nothing
polls. `tools/ipc.ps1 -State` prints the executor ack with its outcome name
(`executed|confirmed|unconfirmed|unknown|superseded|refused|expired`) and the
visibility word as `hidden|visible|unknown` beside the raw numbers, so the three
outcomes are told apart without decoding the constants by hand.

## What is not in the channel

No file, no named pipe, no window message between the processes. The section is
session-local because both processes run as the same user in the same session.
The bridge's hidden executor window is process-local; its only input is the
section.
