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
   plus `controllerTouch=0`) before the consumer is stopped. The root install
   applies its queued hook under that same lock, so a stop here either waits for
   a complete install or makes the next install refuse before `HookSymbols`. The
   input thread also withdraws non-terminally (`renderer-lost`) whenever
   `RendererReady()` goes false and republishes when a usable replacement is
   attached; that withdrawal is before any interception can happen again. A
   capability drop is the same kind of withdrawal: `capability-lost`, the shared
   direction/touch cleared, the driven gesture/close released, and `Progress`
   restored at the cloak's rest. It
   signals the input
   thread's stop event and posts `WM_QUIT` as a second signal for a queued
   message, then joins the thread **INFINITE**, never on that thread.
2. The **input thread** unhooks its cloak WinEvent hook, clears the published
   cloak handle, closes the cloak event and the deadline timer, unregisters raw
   input, releases the 1 ms timer period
   (`EndContactTiming`), runs the per-activation reset, cancels the pending
   measurement, destroys its window, unregisters its fixed class under
   `&__ImageBase`, and exits. Its own exit withdraws availability again
   (`reason=consumer-exit`). The unregister is one checked call: failure other
   than `ERROR_CLASS_DOES_NOT_EXIST` blocks the unload, because the class still
   holds this image's procedure. The hook goes before the event handle it
   signals: a callback that ran after the close would set a recycled handle, and
   both belong to this thread.
3. **The unload waits for an admitted root before choosing its teardown.**
   `Wh_ModUninit` sends one synchronous teardown message to this module's own
   hidden message-only window on the XAML UI thread (created by
   `PrepareRootElement` before `Loaded`; the CoreWindow is only read for the cloak
   and bounds), or runs the turn inline when the unload already is that thread.
   `PrepareRootElement` is itself an admitted, counted module entry from its
   first instruction: it takes a lease, re-reads the unload flag before touching
   module state and again immediately before its first registration, and
   registers nothing if either check sees a stop or the rendezvous window could
   not be created. Off the owner, `RoleUninit` first waits on the module-entry
   count alone, because that hook may have published its owner and lease before
   its window; it re-reads the owner and window after the wait, then runs the
   teardown. The turn revokes the `Loaded`, `SizeChanged`, `LayoutUpdated` and
   scroller
   `ViewChanged` handlers, detaches the settle batch's `Completed`, stops the
   animations, restores resting `Progress` and the neutral translation, resets
   the scroller watches and drops the UI-owned composition/root/dispatcher
   references. The message returns only after the turn and its delegate thunks
   have returned.
4. **After the teardown the proof is both counts and an acknowledged drain.**
   No counted entry and no registered XAML handler may remain. Off the owner, one
   empty drain message follows; its handler sets a caller-owned acknowledgement
   only for an accepted message from this activation's published window. A drain
   that did not run blocks (`ui drain not acknowledged`), because zero alone does
   not prove the last lease destructor returned. A window that is gone after a
   UI-turn lease was ever used (dispatcher turn or settle batch; the root hook's
   admission lease does not count) blocks
   (`no window to drain module entries`). That admission lease is why a hook
   that failed to create the rendezvous and registered nothing still completes:
   it leaves no window, no registration and no UI-turn flag. A never-entered
   process has neither and completes too. An unload already on the owner thread never
   waits: a non-zero count blocks (`unload on ui thread with pending entries`).
   A genuine XAML revoke failure blocks once
   (`unload blocked reason=xaml revokes remain`); it is not polled.
5. The rendezvous window is destroyed with its own message, its class is
   unregistered under `&__ImageBase`, and only then are the IPC view and the
   thread/stop/entry handles closed.

`[[clang::no_destroy]]` is on `g_root`, `g_loadedRoot`, `g_sizeRoot`,
`g_layoutRoot`, `g_dispatcher`, `g_compositor`, `g_state`, `g_visual`,
`g_translationExpression`, `g_dismissBatch` and the optional
`g_scrollerWatches` array. It protects process termination when uninit is
skipped; it is not a leak policy for a normal unload, which clears every one of
them explicitly on the UI thread.

## Explorer (explorer.exe)

1. **`Wh_ModBeforeUninit`** takes the symbol-install lock, sets `g_disabled`,
   clears `g_execReady`, recomputes the published `execCapable` word and signals
   `g_execStopEvent`. The `exec capable=0` line appears only when the word
   actually changes. Every hook and the
   executor's WndProc reads `g_disabled` and passes; the worker's wait sees the
   stop before the `Exec` event. The launcher hooks are applied inside the same
   lock as their resolution, so a stop either waits for the complete install or
   makes the install refuse; the worker never applies hooks after this point.
2. **`Wh_ModUninit`** joins the posting worker **INFINITE**. The worker only
   posts commands; it does not create the executor window. The join stops new
   posts but says nothing about creation, which happens on the launcher thread
   from a launcher entry. The window destruction recomputes the capability (the
   line appears only on a change), and the class teardown writes the last
   recomputation before the section is closed.
3. The one published HWND is destroyed synchronously on its owner thread:
   directly when the unload already runs there, otherwise with
   `SendMessage(kExecTeardownMessage)`, which kills the dismiss/probe/latch
   timers and calls `DestroyWindow`; the call returns after `WM_NCDESTROY` and
   the window publication is cleared. No completion event or wait is used. A
   destroyed window means this activation's window is gone, not that the
   launcher thread is gone; a window that does not answer blocks.
4. The fixed window class is unregistered under the same instance that registered
   it (`&__ImageBase`) only after the window is gone. Registration treats
   `ERROR_CLASS_ALREADY_EXISTS` as failure: a surviving class from an earlier
   image is never adopted. A failure other than `ERROR_CLASS_DOES_NOT_EXIST`
   blocks once; it is not retried.
5. The twinui loader reference taken for the symbol attempt is released here,
   after Windhawk has disabled the hooks and `Wh_ModUninit` has stopped using
   the mapping. Windhawk removes the hooks after `Wh_ModUninit`, so this is a
   release after disabling, not after removal. Worker/stop handles are closed,
   then the IPC view and its events.

Only `OnHotkeyInvoke`, `OnStartButtonPressed` and the manager's `Show` may adopt
an unknown launcher thread. `Dismiss`/`ShowStartView` called from a COM/taskbar
thread that does not pump pass natively instead of creating an executor window
there. While capability is 0 the Win hook passes its press natively too, so a
Win entry alone cannot bootstrap adoption: the shell's own `ShowStartView` call
reaches `CStartExperienceManager::Show`, whose hook records the launcher thread
and creates the window; the next press is then the controller's.

The old creator reservation, admission flag, stop target, reaper, creator drain
and `CloseCallLease` count are **gone**. The worker is joined before the window is
destroyed, and the window is destroyed before the class is unregistered. The join
stops posting, not creation; a creation that races the teardown leaves its window
owned by the class, and the class unregister then blocks instead of freeing under
it. The fixed class ownership also keeps a second activation from starting while
the first still owns the class.

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
