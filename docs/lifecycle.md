# Lifetime — pin, instance guard, counted entries, quiescence

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). The bridge's `Hide` drain this depends on is in [ipc.md](ipc.md).

## The two prerequisites of `Wh_ModInit`

The pin and the instance guard are the two prerequisites: either failing
returns failure with `mod init refused: ...; no hook, no channel and no thread
installed`, and `g_initRefused` short-circuits the later lifecycle callbacks.

A successful pin (`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
GET_MODULE_HANDLE_EX_FLAG_PIN)` on an address in the module, logged `mod pin
image=... pinned=1`) is what keeps the image mapped; a failed pin logs `mod pin
failed err=...` and installs nothing. `void Wh_ModUninit` cannot veto
Windhawk's unload, and the mod held no reference on its own image before this
pin, so returning from that call would let Windhawk free the module while a
thread is still inside it. A pinned image cannot be unmapped: the module stays
in the process until it exits, by design, and that is what makes late-running
thread code and late delegate calls safe. The pin keeps code mapped, not
resources: a residual unload still needs a process restart before anything the
leaked thread or entry touches can be freed.

One instance per process: `Wh_ModInit` takes
`Local\WindhawkLegacyStartSwipe.instance.<pid>` before it creates the stop
event, opens the channel or starts a thread. The name carries the PID, so it
excludes another instance in that process, not every process in the session. A
second instance logs `instance guard refused: another instance is live in this
process`, resets nothing and installs no hook, no thread and no channel; only a
clean unload releases the mutex.

## The stop event and the counted entries

The one stop event is created before any thread starts; without it the mod
starts no raw-input thread and logs `hid thread not started (stop event
unavailable)`. The input thread waits on that event beside its messages in
10 ms slices, the module-scan thread waits on the same event, and the 60 ms
Windows tap waits on it too and always releases the key.

Every entry into module code from the compositor or the dispatcher is counted
from before it can run: the batch completion takes a `ModuleEntryLease` before
`batch.End()`, and every UI turn posted through `PostCountedUiTurn` is counted.
The unload's revocation turn is the exception by design: it uses `RunAsync`
plus an event and waits for it, not the counted post, because the unload must
know the revocation ran before it frees the objects that turn holds.

## Unload order and residuals

Unload stops the module-scan thread (10 s wait), sets the event, waits 10 s for
the input thread, revokes every XAML delegate on the UI thread and waits for
that turn, detaches the settle batch's `Completed` handler, and drains every
counted entry into module code (2 s bound), then frees the composition state
and the IPC view. A thread or callback that misses its wait leaves every
resource in place and logs `unload residual=...; process must restart to finish
unloading`; the timed-out thread's handle stays open so a later attempt can
join it, a joined thread's handle is closed, and the hid-thread case also logs
`unload hid thread still running; ipc view leaked` instead of unmapping a
section the running thread still touches. A counted entry that does not drain
logs `unload entries outstanding=%d`.

A XAML delegate that could not be revoked keeps its token and its owner,
because that token is the only handle on a handler that can still fire, and a
new registration is refused while one is outstanding (`xaml size handler left
attached; new registration refused`, the same for `loaded`; the watches log
`xaml layout handler left attached; new registration refused` and `xaml
viewchanged handler left attached; new registration refused`). The root then
gets no renderer, the layout watch leaves that open to its 400 ms deadline, and
the scroll pass adds no watcher; the unload logs `unload residual=xaml
handlers; process must restart to finish unloading` while one survives.

The `SizeChanged` and `Loaded` callbacks capture the setup generation when they
are registered and return when it has moved, so a late callback from the
previous root touches nothing. The layout watch mints a serial and captures its
own measurement request by value, so a callback that outlived a newer watch
measures and revokes nothing.

## Re-initialisation

A re-initialisation of the same pinned image after a clean unload resets the
lifecycle state (`ResetReinitState`) and then runs one per-activation reset
(`ResetActivationState`, logged `activation state reset`), which the input
thread's exit runs too: the tracked contact, the contact latch, both sessions,
the close mute, the deferred flag, the pending tap and the pull generation it
was armed under, the pull serial, the coast, both velocity windows, the
diagnostic latches and the `anim` override all start over.

## The bridge's side

The bridge pins its own image as a prerequisite, before the channel and the
hooks (`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
GET_MODULE_HANDLE_EX_FLAG_PIN)`, `mod pin image=... pinned=1`); a failed pin
logs `mod pin failed err=...` and returns FALSE from `Wh_ModInit` (`mod init
refused: the image could not be pinned; no hook, no channel and no thread
installed`), because a shell-owned close thread needs valid return code as
much as a worker would. The pin keeps that module's code and the
`CloseCallLease` destructor mapped for a frame that returns after
`Wh_ModUninit`; the trampoline under a frame is the host's allocation, and a
scan blind to the frame is outside what any mod can fix — the hazard is
reduced, not eliminated.

A re-enable re-initialises the same image without the loader resetting
anything, and refuses while a close from the previous activation is still inside
(`mod init refused: a close from the previous activation is still inside;
...`); otherwise `ResetReinitState()` clears `g_disabled`, `g_afterInit`,
`g_explorerHooked`, `g_arbiterEnabled` and `g_startPointTick`, retries a
failed gesture mapping, leaves `g_closeCalls` alone, and logs `activation state
reset`; a drained unload closes the channel before it returns, so a re-enable
opens a fresh one. `Wh_ModInit` still reuses a channel `g_ipc` already holds
instead of mapping a second one over it.

## The host's unload boundary

The host order was established from the installed engine's upstream tag
(`Windhawk v2.0.0-alpha.8`, `src/windhawk/engine/mods_manager.cpp`): disable
hooks, `Wh_ModUninit`, `ThreadsCallStackWaitForRegions(..., 200, 400)`, remove
hooks, `FreeLibrary`, with that wait's result discarded. "The scan found the
frame" and "the frame exited before removal" are therefore two different
claims, and only the first is the host's. The bridge's counted-`Hide` drain is
the mod-side half of that boundary; its measured behaviour is in
[ipc.md](ipc.md#the-bridges-unload-drain).
