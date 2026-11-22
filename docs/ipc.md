# IPC — the merged mod's channel

> Mod 0.7.0 (one source, two process halves). One session-local section, four
> named events, every record a seqlock. There is no file, no mutex, no command
> channel and no polling on either side.

## Names and layout

```
Local\SwipeFor10Start_SingleMod2_State      SwipeIpcState (224 bytes)
Local\SwipeFor10Start_SingleMod2_Intent     auto-reset, bridge -> Start
Local\SwipeFor10Start_SingleMod2_IntentAck  auto-reset, Start -> bridge
Local\SwipeFor10Start_SingleMod2_Exec       auto-reset, Start -> bridge
Local\SwipeFor10Start_SingleMod2_ExecAck    auto-reset, bridge -> Start
```

`kSwipeIpcMagic = 0x53323053` (`S02S`). There is no product version word: the
**namespace is the ABI identity**. The `2` in the prefix is the layout identity:
the `intentTaken` word made the section 224 bytes, so the old 216-byte namespace
must never be joined. A future incompatible layout changes the
prefix again, so an old pinned development image can never join this channel and a
same-size change is never reinterpreted across a staggered update. Every field's
offset is pinned by a `static_assert` in `mods/swipe_ipc_shared.h`;
`layoutBytes` publishes `sizeof(SwipeIpcState)` and a reader that sees a
different value refuses with `ERROR_INVALID_DATA` instead of reading a foreign
layout.

```
0    magic
8    openSeq          16   openTick
24   initState        28   layoutBytes      32   initTick
40   intentSeq        48   intentTick       56   intentKind
60   intentTrigger    64   intentMethod     68   intentFlags
72   intentOpen
80   intentAckSeq     88   intentAckFor     96   intentAckState
100  intentAckDuration 104 intentAckEntry   108  intentAckHr
112  execSeq          120  execTick         128  execKind
132  execMethod       136  execFlags        144  execExecuteAt
152  execOpen         160  execTarget
168  execAckSeq       176  execAckFor       184  execAckState
188  execAckHr        192  execAckVisible
196  controllerDirection
200  controllerTouch
204  peerTick         208  peerState        212  peerEpoch
216  intentTaken
size 224
```

The debug command channel is **gone**: its record, mutex, event and parser were
deleted in 0.7.0, and `tools/ipc.ps1` is read-only.

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
generation the bridge read. The Start side's recovery ticket is bound to the open
tick (the tick the open record published), not to the generation; the landing
compares that tick, while an executor record that names a menu takes the
generation.

### Intent — bridge -> Start

```
seq, tick, kind (open|close|external), trigger (win|startbutton|clickaway|touch|override),
method, flags, open
```

Published by `SwipeIpcPublishIntent` on the launcher thread, woken by the
`Intent` event. The Start side ignores a sequence it already handled and answers
with `SwipeIpcAnswerIntent(seq, state, durationMs, entryPercent, hr)` on
`intentAckSeq`; `intentAckFor` echoes the intent. The Start side publishes the
executor command first and then the ack: the ack reports the decision, and the
command does not wait for it.

The ack is also the bridge's press serialisation point. The channel has **one**
intent record, so two presses that decided before the Start side consumed the
first would overwrite each other and both read the same direction. One writer
path publishes it: the Win key, the Start button's two primitives, the
click-away, and the ordinary `HideStartAndCortana` close (entry `hide`, a forced
close kind) all enter the FIFO. The bridge
keeps a four-entry FIFO of admitted presses on the launcher thread; it publishes
the first at once and publishes the next only when the ack for the outstanding
sequence arrives (the worker waits on `IntentAck` and posts a private message to
the executor window) or its 1000 ms bound expires. The next press therefore
decides from the direction the controller published while applying the press
before it. An arriving press on a full queue is dropped with the depth in its
line and is never executed ahead of the queue; a press still unpublished past
1500 ms runs the shell's own original primitive under the executor guard
(`own press fallback ...`), and the reserve poll re-evaluates that bound. The
bound is paused while a normal press is executing: its clock restarts when that
slot is released, so the next press is never aged out ahead of the claimed one.
An `external` intent clears the queue and the latch,
is published at once, and sets the same barrier as a state, not a timer: it
stands even when the watchdog cannot be armed (`own external barrier ... armed`
or `unarmed`) and is cleared only by its ack (`own external ack ... reconciled`),
by a changed epoch (`released reason=epoch-changed`), or by a peer that is
gone/stale (`own external barrier ... released
reason=peer-gone`, checked at the reserve poll and at the next admission). A live
but silent peer keeps it, and the timeout line is `decision=reserved`; the bridge
never guesses a reconciliation, and the reserve poll runs any held press past its
queue bound.

`method` is **evidence about the shell path that produced the trigger**, not an
execution method. A click-away carries the Hide method `7`. The Start side maps
it to the executor's dismiss vocabulary (`6`, the pointer-driven close the Start
button's own dismiss uses) before publishing the command and keeps `7` in the
decision log; the intent record is not rewritten.

A refused intent has **no native replay**. The bridge swallows the trigger before
it publishes, so the shell never saw that press; when the Start side answers
`refused` (no executor, no channel, a show that could not be published) the
press is lost. The refusal is reserved for the case where nothing could run, and
the line names it (`intent open refused entry=... reason=no-executor`,
`intent close refused from=... reason=no-executor`); the bridge does not retry the
original on an answered refusal. An **unanswered** decision is not replayed on a
guess. At the 1000 ms bound the bridge re-reads the ack, `intentTaken` and the
shared intent record. Any claim is `decision=consumed awaiting=ack`: the take
proves the controller has the record, not that the resulting direction is
published, so the next press waits for the ack, its queue-age bound stays paused,
and only the ack, a changed epoch, or the peer's **state** going away releases it.
A stale heartbeat alone does not: the controller may be paused between two
heartbeats with a validated copy. A claim that
lands after the bridge's first read but before its abort is the same case
(`reason=claim-after-abort`): the controller may already hold a validated copy.
Every Start branch publishes the resulting direction before it answers, so the
press after an ack reads the new direction. Otherwise the bridge **aborts** the
record (`SwipeIpcAbortIntent` steps
the counter to its odd state, which every reader rejects) and re-reads the claim.
If the abort succeeded and no claim exists, the original runs once
(`decision=replay reason=revoked`); a record that already moved cannot be aborted
(`decision=superseded reason=record-moved`). A different `peerEpoch` at the
watchdog **or at the next admission** means the activation that was asked is
gone: a taken press is consumed without its ack
(`reason=epoch-changed`), an untaken one is aborted and replayed, and the queue
advances without a timer. A stale tick reconciles an **unclaimed** record at
admission (`reason=stale-unclaimed`) but never a claimed one. That is the
caller's pre-check, not the last read: reconciliation reads the claim again, and
a claim found there on the same epoch with the state alive keeps the slot
(`reason=claimed-at-admission`) until its ack; a claim between that read and the
abort holds as `reason=claim-after-abort`. An **external** record gets the same
evidence at admission: its claim is read, the record is aborted, and the claim is
read again. A claim before or after the abort keeps the barrier reserved
(`decision=reserved`, `reason=claimed-at-admission|claim-after-abort`); a record
that cannot be aborted keeps it too (`reason=record-moved`); only an aborted
record with no claim after the re-read is released (`reason=stale-unclaimed`).
When admission
reconciles, it decides FIFO placement before any snapshot: if the drain published
an older press, the arriving press queues and reads the launcher at its turn. Only
an immediately servable press re-reads the launcher state after the replay or
drain, so an old native open is followed by a close, not a second open. The peer
words are
evidence;
the abort and the claim are the proof. An execution is never duplicated: an
applied press is not replayed, and a press is lost rather than duplicated if the
controller dies after the take. When the bridge's abort lands before Start's
copy, Start answers superseded, which proves no intent ran; the bridge then runs
the original once (`reason=revoked-before-copy`), unless the press is past the
2500 ms absolute bound, in which case it is dropped (`reason=stale`) rather than
toggled late.

A landing reconciles an already-published `external` intent before it may issue a
recovery: the idle timer and the intent event are two wait objects of the same
thread, and the timer has the lower index, so the landing can otherwise run
before the takeover that arrived with it.

### Intent take — Start -> bridge

`intentTaken` is one 64-bit atomic word, written by the Start side with the
sequence it is about to process, **before it copies the record**. It is a claim,
not a lock. The Start side's turn is:

1. read the raw sequence (`SwipeIpcIntentSequence`: 0 when none, an odd value is
   a write in progress or an aborted record);
2. write `intentTaken = seq`;
3. copy the record;
4. re-read the record's sequence; act only when it is still this sequence. An
   abort between the claim and this read makes the counter odd, so the read
   fails. A claimed record that can no longer be applied is answered
   `intent revoked seq=...` (`kSwipeAckSuperseded`), never dropped silently: the
   publisher's timeout is holding the queue slot for exactly this answer.

The publisher's timeout then has one proof: a claim for the sequence means the
controller may still apply it, so no replay follows and the next press waits for
the ack; only a changed epoch or the peer's state going away releases it without
the ack. A stale heartbeat is not that proof.
An untaken record is aborted and re-read; the original runs only when the abort
succeeded and no claim landed. An `external` record has no original: at the
watchdog's stale-heartbeat turn its claim is read, the record is aborted, and the
claim is read again. A claim in between, or a record that cannot be aborted,
keeps the barrier reserved (`decision=reserved
reason=claim-after-abort|record-moved`); only an aborted record with no claim
after the re-read is released (`reason=stale-unclaimed`), and an explicit state
withdrawal still releases (`reason=peer-gone`).

`0` means no claim has been made. The word is not a seqlock: it is a claim, and no
two fields must agree. An aborted record stays odd until the next publisher steps
the counter past it, so its sequence is never reused.

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

**Admission before any launcher call.** The bridge validates the record before it
arms a timer or calls the shell:

| kind | allowed method | flags | other fields |
|---|---|---|---|
| `show` | `1` (keyboard/touch), `11` (Start button), `16` (touch/edge) | only bits `0x8|0x4` | `executeAt=0`, `target=0` |
| `dismiss` | `1` (Win), `6` (Start button) | `0` | `target=0` |
| `cancel` | `0` | `0` | `executeAt=0` |

Anything else is answered `kSwipeAckRefused` with `E_INVALIDARG` and the log
line `exec refused ... reason=invalid`; a numeric range is never clamped into a
legitimate shell action. The show flag mask is the mask `ShowStartView` actually
reads (`0x8` relaxes the foreground/context-menu check, `0x4` tolerates an open
context menu).

The producer validates the same vocabulary before it swallows a press: the Win
key, the Start button and the launcher's `Dismiss`/`ShowStartView` check
`SwipeShowAllowed` for an open and `SwipeCloseIntentAllowed` for a close, because
a close intent carries the observed shell method as evidence; the click-away
uses the same dispatcher with 7 as that evidence. An
unsupported combination goes to the shell's original (`own pass ...
reason=unsupported`) instead of becoming an intent that the executor would
refuse after the press was lost. The executor's check above is therefore defence
in depth, not the only validation. The click-away's evidence method 7 is allowed
as an intent and mapped to 6 by the Start side; the executor never accepts 7.

**Expiry.** The controller's budget is 400 ms: a show is answered within 400 ms
of its publication, a dismiss within 400 ms of the deadline it was given. The
comparison is `>= due + 400` on both sides, so a dispatch exactly at the boundary
is expired for the bridge and for the publisher. A dispatch that starts after
that is stale — the publisher has already cleared its sequence, published a
cancel where it could, and released ownership — so the bridge answers
`kSwipeAckExpired` and does not call the primitive. The publisher does the same
from its side: an unanswered dismiss is cancelled by sequence at its timeout, and
an unanswered show, which has no armed command to revoke, is invalidated by the
executor's age check.

A deadline is **not** a guaranteed response and not a lease on the shell call. A
primitive can enter before the deadline and return after the publisher timed out;
the timeout is a repair, not a cancel, and no scheme can recall a call already
inside the shell. What the bridge can still do is refuse a record whose publisher
activation changed: it reads `peerEpoch` when it accepts the record and again
immediately before the call, and answers `kSwipeAckSuperseded` on a mismatch. A
record that names a menu (`execOpen`: a dismiss carries the open generation from
`SwipeIpcReadOpen` when a menu is known; a show and a cancel carry 0) is also
refused when the section has already moved to a later open generation
(`exec dismiss ... superseded menu=... now=...`).
The Start side publishes a `cancel(target=0)` at attach for the same reason: a record
from the previous activation must not act on the menu the new one opens.

The bridge answers on `execAckSeq` with `(command, state, hr, visible)`, where
`visible` is the visibility probe, three-valued: `kSwipeVisibilityHidden` (0),
`kSwipeVisibilityVisible` (1), and `kSwipeVisibilityUnknown` (-1) when there is no
launcher or `IsVisible` failed. The mailbox holds one answer: a later answer can
replace an earlier one before the Start side reads it, so the reader matches
`command` and never reads one command's state under another's sequence. A dismiss
is answered in two steps: `executed` means the primitive returned; then the
300 ms probe answers `confirmed` (hidden), `unconfirmed` (visible), or `unknown`
(probe or timer failed). A record replaced by a newer one is answered
`superseded`, which is distinct from `expired`. The log prints the probe's real
elapsed time (`after=<n> ms`), not the requested delay. `unconfirmed` and
`unknown` are terminal repairs for the Start side, which releases ownership and
reads the cloak on the next poll; `executed` alone is never taken as a hide.

### Controller direction — Start -> bridge

| Field | Meaning |
|---|---|
| `controllerDirection` | the one decision: `0` unowned, `1` the controller owns an open, `2` a close is in flight |
| `controllerTouch` | 1 while the opening contact owns the panel |

`controllerDirection` becomes `2` in `TakeCloseOwnership`, before the close's
settle starts; it is not deferred to the dismiss. That is what lets the bridge's
one toggle rule see the whole retract as a close while the shell still reports
state 5 and the cloak still reports open. A cancel republishes `1` — a press's
open intent for any close, and the qualified pull for every close whose dismiss
is still a revocable command. The bridge reads
that single word: `2` makes a press a reopen, `1` makes it a close, and `0` falls
back to the shell's cloak and internal state. A hide in flight (9/11) or a
`Dismiss` our own executor has entered still latches the press instead of
publishing into a no-op show; an armed dismiss does not, because the Start side
cancels it.
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
loop and the intent event exist, and the renderer it will drive is usable
(`RendererReady`: the root is attached and its scalar set was not invalidated).
It is not a claim about the digitizer: a refused
raw-input registration is logged by `RegisterHidUsage` and the intent half still
runs; the `ipc peer alive` line prints `touch=`/`pen=` so the difference is
visible. The publication and the terminal withdrawal are serialised in one
critical section, so a startup that is already in flight either lands before the
withdrawal or is refused after it; a flag check followed by an independent store
would let `alive` overwrite `gone`.

Readiness is continuing, not a one-time announcement. When the root is torn down
for replacement, or a failed scalar write invalidates it, the input thread
withdraws the peer (`ipc peer gone reason=renderer-lost terminal=0`) before the
bridge can intercept again; the press then runs the shell's own path exactly
once. The UI thread signals a renderer event and the input thread republishes
(`ipc peer alive reason=renderer`) only once a usable replacement is attached.
The unload's terminal withdrawal still wins: a republish that races it is
refused by the same lock.

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

A reinstall loads a fresh image at a fresh base; the old image is unmapped by its
unload, and the new image opens the same section because the namespace is
unchanged. There is no version word, so a build that predates this namespace
opens its own older section and cannot join this one at all.

## What is not in the channel

No file, no named pipe, no window message between the processes, no command
record and no command mutex. The section is session-local because both processes
run as the same user in the same session. The bridge's hidden executor window is
process-local; its only input is the section.

## Verification

`pwsh -File tools/ipc.ps1 -State` prints one snapshot: the open generation, the
intent and its ack, the executor command and its ack with outcome and visibility
names, the controller direction/touch, and the peer triple. It refuses a section
whose `initState` is not ready or whose magic or `layoutBytes` disagrees.
