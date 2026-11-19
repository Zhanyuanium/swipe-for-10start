# mods/
> L2 | Parent: ../AGENTS.md

## Shipping pair

### start-menu-legacy-swipe.wh.cpp (0.39.0, StartMenuExperienceHost.exe)
The controller and the touch gesture. It reads the digitizer through Raw Input
(the XAML/DirectManipulation sources are empty on this UI), owns the panel's
`Progress`, plans every settle, and decides the toggle from bridge intents.

**What it owns.** The raw HID decode (report IDs, `TipSwitch`/`InRange`,
contact identity and its evidence clock, the coordinate-only fallback); the
dismiss and reveal sessions; the band admission; the reveal entry and the
2.2 DIP/ms chase; the commit thresholds; the settle spring and the coast; the
scroll survey and its gate; the unhealed-window rule; the geometry snapshot and
its lock; the close suppression.

**M3 paths.**
- `ApplyBridgeIntent` — the only intent consumer. `open` holds the entry
  (`RevealEntryProgress`), sets `controllerOwning` phase 1 and asks for
  `exec show`; `close` calls `TakeCloseOwnership`, runs
  `BeginMirrorCloseSettle(TakeProgressNow())` and asks for `exec dismiss` at
  `settle + 30 ms`; `external` drops the touch state and resyncs from the cloak.
- `ApplyExecAck` / `CheckExecAckTimeout` — one ack per command; a refused show
  or a 400 ms silence releases `controllerOwning`/`controllerTouch` and the shell
  path serves the gesture.
- `StartRevealFollow` — the band show request, once per open
  (`g_intentOpenActive`), sets `controllerTouch` so the 18 ms idle does not end
  the contact while the shell uncloaks.
- `ActivateRevealFollow` — the live takeover at `RevealEntryProgress`; the
  chase is `visible += clamp(mapped - visible, -rate*dt, +rate*dt)`.
- `RunPendingCloseTap` — now sends `exec dismiss`; it is the schedule point, not
  a Windows-key tap.
- `win close observed` — after the landing, a pull still down (or a release
  inside the 300 ms window) issues one fresh show and the open adopts it:
  `reveal reopen after close pull=1 late=0 entry=...`. The pull that was under
  the close is the pull that takes the menu back.
- The 18 ms idle is held through a touch show and through a close in flight, so
  the digitizer's pause during the key/hide cannot drop the contact.

**Deleted here.** The `VK_LWIN` tap, the close record publishes, the retract
request handler/answer, the reveal claim, the deferred candidate's close
machinery. The remaining close scheduler is the one dismiss path.

**Lifecycle.** Pinned image (`GetModuleHandleExW(...PIN)`), per-process instance
mutex, counted module entries, stop-event-driven input thread, XAML revokes with
the residual rules, and the per-activation reset. A disable/enable without a host
restart reuses the XAML root and has no renderer until the host restarts (a
measured limit, not a bug to paper over).

### start-menu-touch-gesture-bridge.wh.cpp (0.20.0, explorer.exe)
The launcher executor and the non-touch triggers.

**Launcher read.** `s_pLauncher` at `twinui.pcshell.dll + 0x948E80` is the
outer object; the execution interface is `outer + 0x18`; `[+0x160]` is the
`IXamlLauncherState` and must be `> 0x10000`. `ShowStartView` is
`twinui+0x141AF0`, `Dismiss` `+0x25D670`, `IsVisible` `+0x1F9B40`.

**Executor.** A hidden message-only window, class
`WindhawkSwipeExecutor_<WndProc>`, created on the launcher thread by a one-shot
`WH_GETMESSAGE` hook (or directly by the first decision hook). A worker waits on
`Exec` and posts `WM_APP+0x5A`; the WndProc reads the command and calls the
launcher with an empty stack. `show`/`cancel` run now; `dismiss` arms
`kExecDismissTimer` for `executeAt`. The 300 ms probe logs `exec visible`;
Dismiss is confirmed by `visible=0`. The window class name is unique per image,
so a reinstall does not inherit the old WndProc.

**Triggers.**
- `OnHotkeyInvoke_Hook` (0x14195C): Shift/Ctrl/Alt is a hard gate; the shell's
  own open flag passes; otherwise `IntentShouldClose` publishes open/close. It
  creates the executor window on this thread.
- `ShowStartView_Hook`: method 11 is the Start button's open trigger; any other
  show is swallowed only while `controllerOwning` (the duplicate edge gesture).
- `Dismiss_Hook`: method 6 is the Start button's close trigger; any other
  dismiss is swallowed only while `controllerOwning`.
- `HideStartAndCortana_Hook` (0x24BCB0): lock/unknown/Win+Tab and a forwarded
  modified press publish `intent=external` and pass; method 7 is a close intent
  unless `controllerTouch` is set (the opening contact's shell click-away is
  swallowed); every other Hide is a close intent.
- `IntentShouldClose`: phase 1 → close, phase 2 → reopen; otherwise the cloak
  and the internal state (1/3/5 open or opening, 7/9/11 closing) decide.
- **Press latch.** One dispatch: a close still settling publishes open now
  (cancellable); an `InternalState` 9/11 hide, or a dismiss already armed and
  due, latches the press (one slot, tick, trigger) and serves it as the same
  open intent at `OnStartHidden` (or the closed cloak). A second press replaces
  the slot (`own press latch replaced ...`); the serve logs `own press served
  wait=...`. A click-away in state 1/3, or a Hide(7) inside our own
  `ShowStartView` in state 1/3, is the pointer-away check and is swallowed
  (`reason=opening`/`reason=own-show`); otherwise the served open is hidden the
  instant it appears.

**Deleted here.** The retract request/plan wait, the arbitration hold, the
close-record read/seed, `OwnDecideFrom` and its open/close/reopen helpers, the
reopen serve timer, the tapped-close watermark.

**Lifecycle.** Pinned image, the counted close-hook frames for the v2 channel,
the drain with `drained`/`stalled` reports, and the re-enable refusal while a
close frame is inside.

### swipe_ipc_shared.h (v4, 744 bytes)
The channel contract. See `docs/ipc.md`. The M2 close/retract slots are retired
but kept for layout continuity; no shipping code touches them. The records that
ship are the open generation, the intent and its ack, the executor command and
its ack, and the controller flags. Every offset is asserted; the magic is
`0x53313053` and the version is 3.

### start_gesture_shared.h
The older gesture mapping (`Local\WindhawkStartGestureShared`), kept for the
process lifetime on purpose: a consumer and a late gesture callback may still
hold its view. The bridge publishes the shell's `SetAnimationData` ids and the
release position there for the probe mods.

## Diagnosis (not needed at runtime)

- `start-menu-touch-gesture-probe.wh.cpp` / `start-menu-touch-gesture-consumer.wh.cpp`
  — the early gesture-source experiments.
- `msgprobe.wh.cpp` — pointer messages and `GetPointerInfo` ownership.
- `inputprobe.wh.cpp` — the Raw Input dump. It must not be enabled beside the
  shipping mod: Raw Input allows one sink per usage per process and the probe
  takes the stream.

## Laws

Complete membership · one line per file · parent link · technical terms first.
Every behaviour sentence is measured; a claim that cannot name its probe and its
log line does not belong here.

[PROTOCOL]: Update this header on change, then check AGENTS.md
