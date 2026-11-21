# Lifetime — quiescing both roles before the image is unmapped

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). The channel contract this depends on is in
> [ipc.md](ipc.md).

## The rule

The image is **not pinned** and there is no instance guard. Windhawk disables the
hooks, calls `Wh_ModBeforeUninit`/`Wh_ModUninit` and frees the module, so every
role must leave no thread and no reachable callback behind before `Wh_ModUninit`
returns. A fresh DLL starts clean; a re-enable loads a fresh image with fresh
globals, so there is no `ResetReinitState` and no residual path.

A bounded wait followed by "success" is not a lifetime proof. Bounded waits stay
for channel startup, gesture/executor expiry, confirmation probes and diagnostic
reporting. A thread, a delegate or an owner-window teardown that cannot be proved
gone **blocks the unload**; it never returns success with a mod-owned callback
reachable.

## Start (StartMenuExperienceHost.exe)

1. **`Wh_ModBeforeUninit`** takes the symbol-install lock and sets `g_disabled`,
   sets `g_unloading`, stops the async dismiss callback, and withdraws peer
   availability (`PublishPeerGone`: `peerState=0` plus `controllerDirection=0`
   plus `controllerTouch=0`) before the consumer is stopped. It signals the input
   thread's stop event and posts `WM_QUIT` as a second signal for a queued
   message, then joins the thread **INFINITE**, never on that thread.
2. The **input thread** unregisters raw input, releases the 1 ms timer period
   (`EndContactTiming`), runs the per-activation reset, cancels the pending
   measurement, destroys its window, unregisters its fixed class under
   `&__ImageBase`, and exits. Its own exit withdraws availability again
   (`reason=consumer-exit`). The unregister is checked and retried: only success
   or `ERROR_CLASS_DOES_NOT_EXIST` lets the thread end, because the class still
   holds this image's procedure.
3. **`Wh_ModUninit`** posts one UI teardown turn to the captured dispatcher (or
   runs it inline when the unload already is that thread). The turn revokes the
   `Loaded`, `SizeChanged`, `LayoutUpdated` and scroller `ViewChanged` handlers,
   detaches the settle batch's `Completed`, stops the animations, restores
   resting `Progress` and the neutral translation, resets the scroller watches and
   drops the UI-owned composition/root/dispatcher references. The turn signals
   completion; the unload waits on that signal, not on a registration count.
   Turns posted before it run first, and the input thread is already joined, so no
   new turn can be posted.
4. The counted **module entries** are drained through their event: a batch
   completion the compositor already dispatched keeps the count up until it
   returns. A XAML revoke that failed leaves a reachable delegate and therefore
   keeps the unload waiting (`unload waiting for xaml revokes=...`).
5. Only then are the IPC view and the thread/stop/entry handles closed.

`[[clang::no_destroy]]` is on `g_root`, `g_loadedRoot`, `g_sizeRoot`,
`g_layoutRoot`, `g_dispatcher`, `g_compositor`, `g_state`, `g_visual`,
`g_translationExpression`, `g_dismissBatch` and the optional
`g_scrollerWatches` array. It protects process termination when uninit is
skipped; it is not a leak policy for a normal unload, which clears every one of
them explicitly on the UI thread.

## Explorer (explorer.exe)

1. **`Wh_ModBeforeUninit`** takes the symbol-install lock, sets `g_disabled`,
   clears `g_execReady` and signals `g_execStopEvent`. Every hook and the
   executor's WndProc reads `g_disabled` and passes; the worker's wait sees the
   stop before the `Exec` event. The launcher hooks are applied inside the same
   lock as their resolution, so a stop either waits for the complete install or
   makes the install refuse; the worker never applies hooks after this point.
2. **`Wh_ModUninit`** joins the posting worker **INFINITE**. The worker never
   calls the launcher synchronously and never waits for an owner callback, so
   this join cannot be held up by the launcher thread.
3. The one published HWND is destroyed on its owner thread: directly when the
   unload already runs there, otherwise through the private
   `kExecTeardownMessage`, which kills the dismiss/probe/latch timers and calls
   `DestroyWindow`; `WM_NCDESTROY` signals `g_execWindowGone`. The unload waits on
   that event **INFINITE**.
4. The fixed window class is unregistered under the same instance that registered
   it (`&__ImageBase`) only after the window is gone. Registration treats
   `ERROR_CLASS_ALREADY_EXISTS` as failure: a surviving class from an earlier
   image is never adopted.
5. Worker/stop/window-gone handles are closed, then the IPC view and its events.

The old creator reservation, admission flag, stop target, reaper, creator drain
and `CloseCallLease` count are **gone**. Two effects replace them: the worker is
joined before the window is destroyed, and the window is destroyed before the
class is unregistered. A creation in flight is covered by the join (the worker is
the only creator) and by the fixed class ownership (a second activation cannot
start while the first still owns the class).

## The host's boundary

The host order was established from the installed engine's upstream tag
(`Windhawk v2.0.0-alpha.8`, `src/windhawk/engine/mods_manager.cpp`): disable
hooks, `Wh_ModUninit`, `ThreadsCallStackWaitForRegions(..., 200, 400)`, remove
hooks, `FreeLibrary`, with that wait's result discarded. The mod's own proofs are
the ones above: joined threads, a completed UI teardown, a drained entry count, a
destroyed window, an unregistered class. The engine does not promise that an
arbitrarily hung original returned, and it does not revoke a WndProc. A
permanently hung launcher or UI thread therefore blocks a provably safe unload;
neither a bounded wait followed by success nor `TerminateThread` fixes that. The
normal unload acceptance criterion is: both hosts keep running, no mod-owned
callback is reachable, and the old DLL is gone from both module lists.

**Known blocking case.** The Start root and the launcher symbol installs call
`HookSymbols`, which blocks on Windhawk's PDB fetch. Both run on the role's own
thread (input thread / executor worker), so the shell keeps pumping, but a cold
fetch also delays the INFINITE join at unload until it returns. With a warm cache
the join is immediate. This is recorded in [limits.md](limits.md).
