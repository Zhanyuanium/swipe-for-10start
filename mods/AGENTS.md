# mods/
> L2 | Parent: ../AGENTS.md

## Members

swipe-for-10start.wh.cpp (0.8.6): the shipping mod. One image, installed once, loaded into `StartMenuExperienceHost.exe` (namespace `start`) and `explorer.exe` (namespace `bridge`); `Wh_ModInit` picks one role table from the host executable name, and the other half's hooks, threads and handles never exist there. Settings: `AnimationStyle` (`Windows 11` / `Windows 10` damping), `GestureStartSpeedPercent` (0-300), `CommitDistancePercent` (1-50, default 10, one value on every axis), `DiagnosticTrace` (default false), all read into atomics at init and on change.
swipe_ipc_shared.h: the channel. One section of 240 bytes and five events, every record a seqlock with its own counter, every offset a `static_assert`, magic `0x53333053`.
start-menu-touch-gesture-probe.wh.cpp: the early gesture-source experiment; refuses new hooks from `BeforeUninit` and keeps its handles when the scan thread does not join.
msgprobe.wh.cpp: pointer messages and `GetPointerInfo` ownership; writes `msgprobe.log` under `Wh_GetModStoragePath` and joins its poller.
inputprobe.wh.cpp: the Raw Input dump into `inputprobe.log`; never enable it beside the shipping mod (one sink per usage per process). Its subclasses detach on their owner threads, and a chain with a later subclass is reported, not restored over.

## Contracts

- **The split.** The controller (`start`) owns the raw HID decode, the panel's
  `Progress`, every settle, the close record, the gates and the peer liveness.
  The executor (`bridge`) owns the launcher read, the one toggle dispatch and the
  non-touch triggers. The shell owns every visual transition; neither half writes
  launcher state.
- **Launcher read.** `s_pLauncher` = `twinui.pcshell.dll + 0x95F0E8` (outer
  object); `+0x18` is the `IXamlLauncher`; `[+0x160]` is the
  `IXamlLauncherState`, ready when `> 0x10000` (constructor sentinel `0x100`).
  Reference RVAs on 10.0.26100.10068: `ShowStartView` `+0xF6EC0`, `Dismiss`
  `+0x25A060`, `IsVisible` `+0x1F6790`; state vtable `+0x26CD00` (Show),
  `+0x499A10` (`DismissStartAndCortana`), `+0x1B0B80` (`IsVisible`),
  `+0x1D1830` (`IsStartWindowVisible`). Readiness is re-checked at every read,
  including the post-call resync.
- **The pin.** The launcher layout is enabled only for the fixed file version
  10.0.26100.10068 read from the mapped image's `RT_VERSION`, with all 13
  symbols at the reference RVAs. Any other image: capability 0, shell's path,
  `layout unverified ... native=1`, no PDB fetch.
- **One symbol attempt.** `HookSymbols` runs on the executor worker, never in a
  hook, once per module per install; a false result is terminal
  (`attempts=1`). The Start root hook (`StartUI::App::PrepareRootElement`) is
  resolved the same way after a wait of up to 120 s for `StartUI_.dll`.
- **The executor thread.** A hidden message-only window, class
  `WindhawkSwipeFor10Start.Executor`, on the recorded launcher thread — the
  thread that runs `OnHotkeyInvoke` and the shell's `ShowStartView`, never the
  taskbar thread (a `ShowStartView` there fail-fasts `0xC0000409` at
  `twinui+0x1BB010`). Never call the launcher from a hook frame; post to that
  window. `ERROR_CLASS_ALREADY_EXISTS` is a failure.
- **The gate.** Peer (`alive`, tick younger than 600 ms), recorded thread, a
  thread a launcher entry proved, a window this activation created there, a
  started worker, the verified layout and the primitive. A Hide never adopts a
  thread; `ShowStartView`/`Dismiss` never adopt and never prove. A `Show`
  proposes its thread through a targeted `WH_GETMESSAGE` hook, provisionally.
- **Press admission.** A four-entry FIFO on the launcher thread; one published
  press per ack (1000 ms bound). `intentTaken` is the consumer's claim, written
  before the copy; the ack is the release. A claim is released only by its ack, a
  changed `peerEpoch`, or a peer whose state is gone; a stale heartbeat releases
  an unclaimed record only, under the abort-and-replay rule.
- **Press latch and gate.** A hide in flight, or a `Dismiss` our executor
  entered, latches the press; the landing serves it. Every published open arms
  the open-generation gate, which holds the next close for at most 900 ms.
- **Command outcomes.** A dispatch at or past `due + 400 ms` is `expired` and
  never calls the launcher. `hr=0` is `executed`; only the 300 ms probe's
  `confirmed` (hidden) is a hide, and `unconfirmed`/`unknown` is a repair.
- **The close record.** `TakeCloseOwnership` accepts it before the settle's
  first frame and publishes `closing`; `CloseInFlight` reads its phase, never a
  tick; the 800 ms repair bound releases the direction and leaves the command
  naming a hide that may still land.
- **Caller classification.** A thread-local set by a wrapper around each proven
  caller, not a return-address match: `DismissStartAndCortana` ordinary,
  `DismissSynchronouslyWithoutTransition` lock, `ActiveZBandChangedForMonitor`
  click-away. A ClickAway scope with any other method is external.
- **Trace.** One `TraceLog` wrapper calls `Wh_Log` and appends the same line to
  `<mod storage>\renderer.log` or `\bridge.log` only while `DiagnosticTrace` is
  on. Cap 20,000 lines; the cut uses its own `FILE_WRITE_DATA` handle and the
  line goes through a `FILE_APPEND_DATA` handle (both bits writes at offset 0).
  One `WriteFile` per line; formatting truncates (`_TRUNCATE`).
- **Unload proofs.** No pin and no instance guard. `BeforeUninit` withdraws the
  peer, signals the stop and joins each owned thread `INFINITE`. `Uninit`
  destroys the one published window on its owner thread (directly, or
  `SendMessage(kExecTeardownMessage)`; no window-gone event exists), unregisters
  the fixed class under `&__ImageBase`, then closes the channel. The Start side
  requires the UI-turn count, the registration count and one acknowledged empty
  turn; a frame on the stack defers the teardown. A proof that cannot be
  obtained blocks the unload.
- **Channel validation.** The executor accepts show 1/11/16 with flags under
  `0x8|0x4`, dismiss 1/6 with no flags, cancel 0/0; anything else is refused,
  never clamped. The producer checks the same vocabulary and passes an
  unsupported combination to the shell.

## Laws

Complete membership · one line per file · parent link · technical terms first.
A behaviour sentence names its probe or its log line.

[PROTOCOL]: Update this header on change, then check AGENTS.md
