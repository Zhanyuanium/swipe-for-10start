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
   `PrepareRootElement` is admitted by `g_rootAdmissionLock`, not by the entry
   count: the hook takes it shared as its first act, checks the unload flag, runs
   the original, records/creates, checks the flag again immediately before its
   first registration, and releases on every return. `RoleUninit`, after the flag
   is set, takes the same lock exclusive before the window/teardown decision and
   holds it through `TeardownUiOnUiThread`; an in-flight hook finishes first, and
   a hook arriving after the flag sees it and registers nothing. This also lets
   the teardown revoke a gesture close's settle batch instead of waiting for a
   lease only that teardown can release. A top-level handler runs the turn
   immediately. A XAML handler on the stack defers it with
   `g_uiTeardownPending`; the last `UiDepthGuard` unwinds, runs the turn and sets
   `g_uiTeardownAck`, and the unload waits for that event instead of returning
   into the handler. The turn revokes the `Loaded`,
   `SizeChanged`, `LayoutUpdated` and scroller
   `ViewChanged` handlers, detaches the settle batch's `Completed`, stops the
   animations, restores resting `Progress` and the neutral translation, resets
   the scroller watches and drops the UI-owned composition/root/dispatcher
   references. The message returns only after the turn and its delegate thunks
   have returned, or after a deferred turn acknowledged.
4. **After the teardown the UI-turn count and the registration count must be
   zero, and the drain must acknowledge.** The admission lock is not part of this
   count. No UI-turn entry and no registered XAML handler may remain. Off the
   owner, one empty drain message follows; its handler sets a caller-owned
   acknowledgement only for an accepted message from this activation's published
   window. A drain
   that did not run blocks (`ui drain not acknowledged`), because zero alone does
   not prove the last lease destructor returned. A window that is gone after a
   UI-turn lease was ever used (dispatcher turn or settle batch) blocks
   (`no window to drain module entries`). A hook
   that failed to create the rendezvous and registered nothing still completes:
   it holds only the admission lock, leaves no window, no registration and no
   UI-turn flag. A never-entered
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
2. **`Wh_ModUninit`** joins the posting worker **INFINITE**. The worker posts
   commands. A targeted `WH_GETMESSAGE` hook creates the executor window on its
   first message on the launcher thread — the thread the manager's `Show`
   proposed, or one a Win/Start-button entry recorded — not on the worker thread.
   The hook commits a proposed thread only after that message proves the loop
   pumps; a caller that never pumps leaves it uncommitted and capability 0. The
   join stops posting but says nothing about creation or
   destruction. The window destruction recomputes the capability (the
   line appears only on a change), and the class teardown writes the last
   recomputation before the section is closed.
3. The one published HWND is destroyed synchronously on its owner thread:
   directly when the unload already runs there, otherwise with
   `SendMessage(kExecTeardownMessage)`. A top-level handler kills the
   dismiss/probe/latch timers and calls `DestroyWindow`; a handler that lands
   while a WndProc or `RunPressOriginal` frame is on the stack marks the
   teardown pending, and that frame's outermost unwind completes it. The unload
   waits on the acknowledgement event, so a nested send cannot return into an
   outer frame that can still call the shell. The window publication is cleared
   at `WM_NCDESTROY`. A
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

The manager's `Show` is the bootstrap: it proposes an unknown launcher thread
with no live peer and no worker, and a targeted `WH_GETMESSAGE` hook fires on
that thread, commits it and creates the window. A `Dismiss`/`ShowStartView`
called from a COM/taskbar thread that does not pump passes natively; the `Show`
running there never fires the hook, so capability stays 0. The Win and
Start-button decision entries adopt the thread directly and create the window,
but only after a live peer and a started worker, so they never serve a service
that is not answering (`OnStartButtonPressed` is not
called on this build). While capability is 0 the Win hook passes its press
natively. The shell's own `ShowStartView` call reaches
`CStartExperienceManager::Show`, whose hook proposes the launcher thread; the
next press is then the controller's.

The old creator reservation, admission flag, stop target, reaper, creator drain
and `CloseCallLease` count are **gone**. The worker is joined before the window is
destroyed, and the window is destroyed before the class is unregistered. The join
stops posting, not creation: a creation that already passed its flag check holds
the admission shared while it registers the class and creates the window, and
the unload's exclusive waits for it. Once the unload holds the exclusive, every
later creation path's shared try fails and returns, so nothing can be created
under the teardown. The launcher-thread `WH_GETMESSAGE` hook also uses the
try-shared side, so the teardown `SendMessage` cannot deadlock against the
writer; the WndProc takes no admission lock. The fixed class ownership also
keeps a second activation from starting while the first still owns the class.

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
