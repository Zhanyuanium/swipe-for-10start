# Architecture — one controller, one executor

> Mod 0.8.5: one source, two process halves. Channel 240 B, no product version
> word (`SingleMod6` namespace). Measured on the machine in `../AGENTS.md`.

## What ships

One Windhawk mod, installed once and loaded into both hosts. `Wh_ModInit` picks
one role table from the host executable name; the other half's hooks, threads and
handles never exist there. The bridge elects itself: only the Explorer that owns
`Shell_TrayWnd` opens the channel and starts the worker.

- **`start`** (`StartMenuExperienceHost.exe`) — the controller: Raw Input, the
  panel's `Progress`, the settle planner, the close record, the gates, the peer
  liveness. It consumes executor intents and publishes executor commands.
- **`bridge`** (`explorer.exe`) — the executor: the launcher read, the one toggle
  dispatch, the non-touch triggers, and the shell's duplicate show/close
  suppression while the controller owns the panel.

The shell performs every visual transition; the mod writes no launcher state.
An enable or update attaches to a running host through `Wh_ModAfterInit`, a
registered message and a targeted `WH_GETMESSAGE` hook that takes the same
`Window::Current().Content()` attach path as `PrepareRootElement`.

## The launcher read and the pin

`twinui.pcshell.dll + 0x95F0E8` holds `XamlLauncher::s_pLauncher`, the outer
object. The `IXamlLauncher` subobject is `outer + 0x18`; its `[+0x160]` is the
live `IXamlLauncherState`, ready when `> 0x10000` (constructor sentinel `0x100`;
zero means no state). Using the outer pointer returns `0x80070490`
(`ERROR_NOT_FOUND`), which was the observed failed first show.

Reference RVAs on the pinned image: `ShowStartView` `+0xF6EC0`, `Dismiss`
`+0x25A060`, `IsVisible` `+0x1F6790`; state vtable `+0x38` Show, `+0x40`
`DismissStartAndCortana`, `+0x68` `IsVisible`, `+0x70` `IsStartWindowVisible`.
A read that is not ready refuses the command and leaves the cached direction
alone, including the resync after a forwarded `Show`/`Dismiss`.

The pin is one identity: fixed file version **10.0.26100.10068** read from the
mapped image's `RT_VERSION` (not its path), and all 13 named symbols at the
reference RVAs. Another image is refused before `HookSymbols`, so no PDB is
fetched: `exec launcher layout unverified ... native=1 reason=version|rva`. The
verified image logs `exec launcher layout verified ... native=0`.

## The executor thread

The launcher's state machine is apartment-affine. The **launcher thread** runs
`OnHotkeyInvoke` and the shell's `ShowStartView`; the **taskbar thread** owns
`Shell_TrayWnd` and is not interchangeable. A `ShowStartView` on the taskbar
thread fail-fasts `explorer.exe` (`0xC0000409` at `twinui+0x1BB010`).

- The executor is a hidden message-only window, class
  `WindhawkSwipeFor10Start.Executor`, created only on that thread. The manager's
  `Show` proposes its thread and a targeted `WH_GETMESSAGE` hook commits it;
  capability stays 0 until a Win entry, a Start-button entry or the shell's own
  click-away Hide proves the thread. `Dismiss`/`ShowStartView` never adopt.
- The worker waits on the channel and posts `WM_APP+0x5A`; the WndProc calls the
  launcher with an empty stack. Never call from a hook frame.
- Shutdown is an order: worker joined `INFINITE`, the one published window
  destroyed on its owner thread with `SendMessage(kExecTeardownMessage)` (no
  window-gone event exists), then the class unregistered under `&__ImageBase`
  and the channel closed last. `ERROR_CLASS_ALREADY_EXISTS` is a failure.

## One decision, one executor

| Trigger | Hook | Decision |
|---|---|---|
| Windows key | `XamlLauncher::OnHotkeyInvoke` | `own toggle decide trigger=win …` |
| Start button open | `ShowStartView` method 11 | `own toggle decide trigger=startbutton …` |
| Start button close | `Dismiss` method 6 | `own toggle decide trigger=startbutton …` |
| click-away | `HideStartAndCortana` method 7 | `own toggle decide trigger=clickaway …` |
| touch | the qualified band pull (24 DIP) | `reveal show request` |

`IntentShouldClose` is the only toggle rule: the controller's one direction word
decides while it owns the panel (`2` close in flight → the press is an open; `1`
→ a close). The shell's cloak and `InternalState` are the resync for a panel the
controller does not own. The controller publishes `2` in `TakeCloseOwnership`
before the settle's first frame, because the shell still reports state 5 and the
cloak still reports open for the whole retract.

- **Precondition.** Peer (`peerState=alive`, tick younger than 600 ms, clock read
  after the tick), then an open channel, a started worker, the recorded launcher
  thread, a window on it, `execCapable=1` with a live `execPid` handle. Otherwise the trigger passes
  (`own pass trigger=... reason=no-peer|wrong-thread|no-capability|...`).
- **One intent at a time.** A four-entry FIFO on the launcher thread, one press
  published per ack (1000 ms bound). `intentTaken` is the claim written before the
  copy; the ack is the release. A claim is released only by its ack, a changed
  `peerEpoch`, or a peer whose state is gone. An untaken record is aborted and
  replayed once; a record that moved is superseded.
- **Latch.** A hide in flight (`InternalState` 9/11) or a `Dismiss` our executor
  entered latches the press; the landing serves the first open and the remainder
  in order. An armed dismiss is revocable, so it is cancelled instead.
- **Generation gate.** Every published open captures the generation it was
  published against; the next close waits for a different open generation, at
  most 900 ms, on a 20 ms one-shot.
- **Click-away guards.** A Hide(7) in state 1/3 from the `ActiveZBand` caller, or
  the Hide(7) inside our own `ShowStartView` with an unattributed caller, is the
  pointer-away check and is swallowed (`reason=opening|own-show`). A lock, an
  unattributed Hide elsewhere, or any other caller takes the external path and
  passes.

## Command outcomes

- A command dispatched at or past `due + 400 ms` is answered `expired` and never
  calls the launcher; the publisher uses the same `>=` comparison.
- `hr=0` is `executed`; a successful dismiss also owes the 300 ms `IsVisible`
  probe. `confirmed` (hidden) is a hide; `unconfirmed`/`unknown` is a terminal
  repair. A show is confirmed by the same 300 ms probe.
- A record whose publisher epoch changed is `superseded`; a record naming a menu
  the section has left is `superseded menu=...`; a show and a cancel carry
  generation 0.
- An unanswered show or dismiss is repaired at 400 ms and published as an
  `exec cancel` naming the sequence, so a stale record cannot run.

## The open and the close (measured)

The shell owns the cloak: the first visible frame is **245–268 ms** after a band
show in the worst case. The Win open on the bottom auto-hidden layout reached
`exec show` at **+31–32 ms**, the shown event at **+46–141 ms** and the first
panel pixel at **+82–187 ms** (median +115); the bottom docked capture measured
**+190 ms**. Decision and command latency is the mod's gain; the cloak is not.

- **Open claim.** One per contact, on the first positioned report whose signed
  inward travel reaches **24 DIP**. Measured on the bottom docked layout: 31 ms
  from the first positioned report to the crossing on a ~1 DIP/ms pull, +15 ms to
  `ShowStartView`, 125 ms to uncloak, 141 ms to the first panel frame. A 23 DIP
  drag requested nothing (the shell's own gesture, from about 20 DIP, opened it);
  a 25 DIP drag requested one show. A tap requests nothing.
- **Entry.** `RevealEntryProgress` is the first progress whose trail edge is on
  the work-area edge: **0.255** bottom auto-hidden (trail 265, Distance 852),
  **0.348** bottom docked, **0.000** cold. The takeover chases at **2.2 DIP/ms**
  and then follows (`behind=0.000`): a held 300 DIP finger measured 1.8 px away
  (bottom auto-hidden) and 2 px (bottom docked), a held 350 DIP finger 0.4 px.
- **Close.** `TakeProgressNow()` → shell-close settle (full travel: **387 ms** at
  852 DIP, **409 ms** at 900) → `exec dismiss` at settle + 30 ms; a gesture close
  asks for it at its own settle's end. The panel is never hidden mid-slide.
- **Close record.** Serial, acceptance tick, the menu, a phase (`Settling`,
  `AwaitingHide`, `Done`) and the **800 ms** repair bound. `CloseInFlight` reads
  the phase, never a tick. Takeover → `win close observed` measured **562–610 ms**
  over ten maximum-duration closes (mean 578), so the bound is a repair, not the
  answer to "is a close in flight".
- **Landing.** A confirmed dismiss on a cloak that already reads hidden finishes
  through the same landing (`reason=confirmed-without-transition`). A landing
  adopts no finger, serves no touch open of its own, and retires no unspent grab
  grant; the only open it owes is the explicit request's recovery ticket.
- **Interruption.** A press during the retract is an open: the panel settles back
  from its live position (`intent open cancel from=...`); a dismiss already
  inside `Dismiss` lands, and the landing reissues the open once
  (`intent open recovered`). A Win close 172 ms into an open started at
  `from=0.542`; a Win press during a held touch started at `progress=1.000` and
  hid at +437 ms. 40 randomised presses per kind: **0 lost**.

## Channel

One session-local section of **240 bytes** and five events
(`Intent/IntentAck/Exec/ExecAck/Capability`), every record a seqlock. The records
are the open generation, the intent and its ack and `intentTaken`, the executor
command and its ack, one controller direction word plus the touch flag, the peer
identity, `execCapable` and its `execPid`. The prefix is `Local\SwipeFor10Start_SingleMod6_*`;
the namespace is the ABI identity, and there is no product version word.
`tools/ipc.ps1` is read-only. See [ipc.md](ipc.md).
