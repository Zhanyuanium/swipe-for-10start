# IPC — the merged mod's channel

> One session-local section, five named events, every record a seqlock. No file,
> no mutex, no command channel, no polling on either side.

## Names and layout

```
Local\SwipeFor10Start_SingleMod6_State      SwipeIpcState (240 bytes)
Local\SwipeFor10Start_SingleMod6_Intent     auto-reset, bridge -> Start
Local\SwipeFor10Start_SingleMod6_IntentAck  auto-reset, Start -> bridge
Local\SwipeFor10Start_SingleMod6_Exec       auto-reset, Start -> bridge
Local\SwipeFor10Start_SingleMod6_ExecAck    auto-reset, bridge -> Start
Local\SwipeFor10Start_SingleMod6_Capability auto-reset, bridge -> Start
```

`kSwipeIpcMagic = 0x53333053`. There is no product version word: the
**namespace is the ABI identity**, and the `5` marks the 240-byte layout. A future
layout changes the prefix, so an older image cannot join and a same-size change is
never reinterpreted across a staggered update. Every offset is a `static_assert`
in `mods/swipe_ipc_shared.h`; `layoutBytes` publishes `sizeof(SwipeIpcState)` and
a reader that sees another value refuses with `ERROR_INVALID_DATA`.

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
216  peerPid          220  peerUiThread
224  intentTaken
232  execCapable
236  execPid
size 240
```

The channel is trusted by name, not authenticated: `Local\` scopes the objects to
the logon session, and any process in it can read every record and publish any
record. `peerState`, `peerTick`, `peerEpoch`, `peerPid` and `peerUiThread` prove
that a Start half is alive and which activation it is, not that the writer is the
Start host. A hostile same-session process is outside the threat model.

## Seqlocks

A writer calls `SwipeIpcBeginWrite(&seq)` (counter odd), writes the payload, then
`SwipeIpcEndWrite(&seq)` (counter even and published). A reader calls
`SwipeIpcSnapshot`: read the counter, refuse odd, copy, require the same even
value. The sequence is never an authority by itself, only a complete stable copy
is. `kSwipeIpcReadAttempts` is 8, so a writer that dies mid-record fails the
reader instead of spinning it.

## Records

- **Open generation** (Start → bridge): `openSeq`, `openTick`. Published when the
  Start side first sees the window shown. Generation 0 is not a menu
  (`ERROR_NOT_READY`). Every intent carries the generation the bridge read. The
  recovery ticket is bound to the open *tick*; an executor record that names a
  menu takes the *generation*.
- **Intent** (bridge → Start): `seq, tick, kind (open|close|external), trigger
  (win|startbutton|clickaway|touch|override), method, flags, open`. Published on
  the launcher thread; the Start side ignores a handled sequence and answers
  `SwipeIpcAnswerIntent(seq, state, durationMs, entryPercent, hr)`. The Start
  side publishes the executor command first, then the ack.
- **Intent take** (Start → bridge): `intentTaken` is one 64-bit atomic word,
  written with the sequence it is about to process **before** it copies. It is a
  claim, not a lock. The turn is: read the raw sequence, write the claim, copy,
  re-read the sequence, act only when it is still this one. An abort in between
  makes the counter odd, so the copy fails; a claimed record that cannot be
  applied is answered `intent revoked seq=...` (superseded), never dropped
  silently.
- **Executor command** (Start → bridge): `seq, tick, kind (show|dismiss|cancel),
  method, flags, executeAt (GetTickCount64), open, target`. `show` and `cancel`
  run at once; `dismiss` is armed for `executeAt` and a later command supersedes
  it. A launcher-thread watermark accepts each record once.
- **Controller direction** (Start → bridge): `controllerDirection` 0 unowned, 1
  the controller owns an open, 2 a close is in flight; `controllerTouch` is 1
  while the opening contact owns the panel. `2` is set in `TakeCloseOwnership`,
  before the settle. A cancel republishes `1`. The attach resets both to 0 before
  claiming the epoch, so a kill while `closing` cannot be read as the next
  activation's decision.
- **Peer liveness** (Start → bridge): `peerEpoch` (once per attach), `peerTick`
  (low 32 bits of `GetTickCount64`, refreshed from the input thread at most once
  per 100 ms), `peerState` (1 while that consumer runs). Publication order is
  epoch, tick, alive. The bridge intercepts only while `alive` and
  `GetTickCount64() - peerTick < 600 ms`, with the comparison clock read **after**
  the tick. A killed host leaves `alive` behind; the gate closes 600 ms later.
  600 ms is a detection budget, not a delivery guarantee.
- **Executor capability** (bridge → Start): `execCapable` is 1 only while the
  worker runs, the hidden window exists on the recorded launcher thread, the
  loaded `twinui.pcshell.dll` is the verified layout, and the show/dismiss
  primitives and visibility probe are resolved, and its `execPid` is published.
  The Start side holds a process handle to that pid: a signalled handle makes the
  word unusable even before a replacement bridge clears it. Any loss clears it and signals
  `Capability`. The Start side withdraws the peer on a 1 → 0 transition
  (`reason=capability-lost`), clears the shared direction/touch, ends the gesture
  or close it was driving and restores `Progress` at the rest the cloak shows. The
  word is a flag, not a seqlock record; without the event the Start side reads it
  every 100 ms while the peer is unadvertised.

## Admission and expiry

The bridge validates every command before it arms a timer or calls the shell:

| kind | allowed method | flags | other fields |
|---|---|---|---|
| `show` | `1` (keyboard/touch), `11` (Start button), `16` (touch/edge) | only bits `0x8\|0x4` | `executeAt=0`, `target=0` |
| `dismiss` | `1` (Win), `6` (Start button) | `0` | `target=0` |
| `cancel` | `0` | `0` | `executeAt=0` |

Anything else is refused (`E_INVALIDARG`), never clamped. The show flag mask is
the one `ShowStartView` reads (`0x8` relaxes the foreground check, `0x4` tolerates
an open context menu). The producer checks the same vocabulary before swallowing
a press and passes an unsupported combination to the shell
(`own pass ... reason=unsupported`), so the executor's refusal is defence in
depth. A click-away intent keeps method 7 as evidence and the Start side maps it
to 6; the executor never accepts 7.

- The controller's budget is 400 ms: a show within 400 ms of publication, a
  dismiss within 400 ms of its deadline. The comparison is `>= due + 400` on both
  sides; a later dispatch is `expired` and the primitive is not called.
- A deadline is not a lease on the call: a primitive can enter before it and
  return after the publisher timed out, and nothing recalls a call already inside
  the shell.
- `peerEpoch` is captured when a record is accepted and checked immediately
  before the call (`superseded` on a mismatch); a record that names a menu the
  section has left is `superseded menu=... now=...`. The attach publishes
  `cancel(target=0)` so a record the previous activation left queued cannot act.
- The ack is `(command, state, hr, visible)`, one answer per command; the reader
  matches `command` and never reads one command's state under another's sequence.
  A dismiss is answered `executed` (the primitive returned), then `confirmed`
  (hidden), `unconfirmed` (visible) or `unknown` (probe or timer failed) at the
  300 ms probe. `unconfirmed`/`unknown` is a terminal repair; `executed` alone is
  never a hide.

## Initialisation

`SwipeIpcOpen` creates the section with
`CreateFileMappingW(INVALID_HANDLE_VALUE, PAGE_READWRITE, sizeof(SwipeIpcState),
name)`; an existing section is opened and validated. `initState` is a four-value
CAS state machine (`Blank -> Initialising -> Ready`, `Abandoned` after a dead
claim) with a 550 ms budget, so a young claim is abandoned inside the call rather
than waited on. A missing event disables only the direction it carries; a missing
`Capability` event leaves the 100 ms fallback read, and a missing bridge `Intent`
event leaves capability 0 because a takeover could not be delivered. The Start
side opens it at
`Wh_ModInit`; the bridge opens it on its shell-election thread, never in a hook.

A reinstall loads a fresh image at a fresh base; the old image is unmapped by its
unload and the new one opens the same section because the namespace is unchanged.
There is no version word, so a build with another namespace cannot join at all.

## What is not in the channel

No file, no named pipe, no window message between the processes, no command
record and no command mutex. The bridge's executor window is process-local; its
only input is the section. `pwsh -File tools/ipc.ps1 -State` prints one snapshot
and refuses a section whose `initState`, magic or `layoutBytes` disagrees.
