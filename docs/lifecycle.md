# Lifetime — pin, instance guard, counted entries, quiescence

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). The close-hook drain this depends on is in [ipc.md](ipc.md).

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
10 ms slices, and the module-scan thread waits on the same event.

Every entry into module code from the compositor or the dispatcher is counted
from before it can run: the batch completion takes a `ModuleEntryLease` before
`batch.End()`, and every UI turn posted through `PostCountedUiTurn` is counted.
The unload's revocation turn is the exception by design: it uses `RunAsync`
plus an event and waits for it, not the counted post, because the unload must
know the revocation ran before it frees the objects that turn holds.

## Unload order and residuals

Unload withdraws availability first, then stops the module-scan thread (10 s
wait), sets the event, waits 10 s for the input thread, revokes every XAML
delegate on the UI thread and waits for that turn, detaches the settle batch's
`Completed` handler, and drains every counted entry into module code (2 s bound),
then frees the composition state and the IPC view. The withdrawal is
`PublishPeerGone`: `peerState=0` plus `controllerDirection=0` plus
`controllerTouch=0`, written **before** the input thread is told to stop and
therefore before any residual can return. A few presses during the stop are served
by the shell, which is the intended trade: the alternative is an `alive` word over
a consumer that is already leaving. A thread or callback that misses its wait
leaves every resource in place and logs `unload residual=...; process must restart
to finish unloading`; the timed-out thread's handle stays open so a later attempt
can join it, a joined thread's handle is closed, and the hid-thread case also logs
`unload hid thread still running; ipc view leaked` instead of unmapping a
section the running thread still touches. A counted entry that does not drain
logs `unload entries outstanding=%d`. The input thread's own exit withdraws
availability again (`reason=consumer-exit`), so an exit that no `Wh_ModBeforeUninit`
preceded — a failed wait, a `WM_QUIT` — cannot leave the direction of a stopped
consumer readable by the bridge.

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
the close mute, the deferred flag, the pending dismiss and the pull generation it
was armed under, the pull serial, the coast, both velocity windows, the diagnostic
latches, the `anim` override, the executor's pending show and dismiss sequences
with their deadlines, the pending open entry, the last consumed intent sequence
and the recovery ticket all start over. The attach then withdraws the peer's
`alive` word, resets the shared direction and touch, seeds the intent boundary
from the section and claims a fresh `peerEpoch`; `alive` is published only when
the input thread is ready to serve.

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
...`) or while the previous activation's executor teardown is still outstanding
(`... the previous activation's executor has not finished stopping; ...`).
"Outstanding" is exact: the worker has not been joined, the identified window has
not been destroyed, or a creator is still inside `CreateWindowExW`. A worker that
has exited since is joined and its handles closed when the enable asks, and a
window destroyed since clears the stop target then, so the refusal is not
permanent. Otherwise `ResetReinitState()` clears `g_disabled`, `g_afterInit`,
`g_explorerHooked`, `g_closeHookEnabled`, the executor's
pending command, press latch, confirmation sequence, accepted sequence, thread
ids and stop target, reopens window admission,
leaves `g_closeCalls` alone, and logs `activation state reset`; a drained unload
closes the channel before it returns, so a re-enable opens a fresh one.
`Wh_ModInit` still reuses a channel `g_ipc` already holds instead of mapping a
second one over it — and that reuse is exactly why the refusal above exists: a
surviving worker still waits on that channel's `Exec` event.

**The invariant and its one narrow hole.** The intended invariant is: while a
re-enable runs, no window from the previous activation exists. The stop enforces
it by closing admission, draining the creator reservation, joining the worker and
destroying the identified window; the re-enable then *proves* the same three
facts before `ResetReinitState` clears anything. The hole the earlier version had
was a two-moment read: it snapshotted the published HWND, then read the creator
reservation, so a creator that published and released between those two reads
looked like "no window, no creator". The enable then cleared the HWND and the
window survived with this image's procedure and the new activation's globals. The
fix is an order, not a flag: admission stays closed across the unload, the
reservation is drained **before** the HWND snapshot (the publish precedes the
release, so a null HWND read after the drain is durable), and the reservation is
never cleared to manufacture quiescence. A creator that does not leave inside
500 ms makes the enable refuse; a refused enable is acceptable, a second window is
not. Two further guards make a survivor harmless even if a future path reopens
admission early: the procedure refuses every dispatch for an HWND other than the
one currently published (a stale window's armed dismiss cannot fire into a new
menu), and the shutdown is a private `WM_APP` teardown answered only when the
window equals the recorded stop target, not a generic `WM_CLOSE`. The target is
still only an HWND, so `IsWindow` alone is not identity; the answer is the
target check inside the procedure.

**Staging the creator/reaper interleaving on a test build.** The interleaving is
not reachable on the shipping build by timing alone. A test build inserts a hold
of ~1.5 s between `CreateWindowExW` returning and `g_execWindow.store(...)` in
`EnsureExecutorWindow` (and a second hold before the `CreatorDone` destructor).
Then: enable the mod, make one Start press so the creator enters the hold,
disable during the hold (the stop drains 500 ms, times out, defers and keeps every
resource), wait for the hold to expire, and enable again. With the fix the re-enable
either drains the now-published window and logs `exec teardown completed late` or
refuses with `exec creator still inside (reinit)`; without it, `activation state
reset` appears while the old HWND is still alive. The observable is one
`exec window hwnd=` per activation, a `Get-Process`/Spy++ check that the old HWND
no longer exists, and no `exec dismiss`/`exec visible` line from the old
activation after the new one starts. The same hold stages the variant where the
creator publishes and releases before the re-enable: the log must show
`exec teardown found a surviving window` and then `exec teardown completed late`.

The unload order on the bridge is the executor first, then the drain: admission
closes, readiness clears, a creator is drained, the worker is joined, and the
identified window and its four timers are destroyed — directly
when the unload already runs on its owner thread, otherwise through the private
`kExecTeardownMessage` the owner answers from `WM_NCDESTROY`, where only that
HWND signals. Readiness is published by the worker itself, after its exit guard
is installed, and the gate also requires admission, so a creator that stores
`true` after a stop cleared it cannot be read as a service.
**Acknowledged destruction is conditional.** A join timeout, a teardown-post
failure, an unacknowledged teardown or a creator that does not leave in 500 ms
leaves the window, the worker or both alive; the unload then keeps the handles
**and the channel** and logs `ipc close deferred: ...` instead of pretending to
have closed it. That is the path a re-enable must refuse, and the unacknowledged
window is why the acknowledgement is bound to one HWND: a second window's
destruction must not stand in for the first one's. Every executor dispatch holds
a `CloseCallLease`, so the count that gates the channel close covers commands as
well as close hooks.

## The host's unload boundary

The host order was established from the installed engine's upstream tag
(`Windhawk v2.0.0-alpha.8`, `src/windhawk/engine/mods_manager.cpp`): disable
hooks, `Wh_ModUninit`, `ThreadsCallStackWaitForRegions(..., 200, 400)`, remove
hooks, `FreeLibrary`, with that wait's result discarded. "The scan found the
frame" and "the frame exited before removal" are therefore two different
claims, and only the first is the host's. The bridge's counted close-hook and
executor-dispatch drain is the mod-side half of that boundary; the executor's
thread rule and its unload acknowledgement are in
[architecture.md](architecture.md#the-executor-thread).
