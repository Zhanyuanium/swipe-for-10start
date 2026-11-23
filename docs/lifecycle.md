# Lifetime — quiescing both roles before the image is unmapped

> Part of [docs/README.md](README.md). The channel contract is in [ipc.md](ipc.md).

## The rule

The image is **not pinned** and there is no instance guard: every role must leave
no thread and no reachable callback behind before `Wh_ModUninit` returns. A fresh
DLL starts clean and a re-enable loads a fresh image. A bounded wait followed by
"success" is not a proof: a thread, a delegate or an owner-window teardown that
cannot be proved gone **blocks the unload**.

## Start (StartMenuExperienceHost.exe)

1. `Wh_ModBeforeUninit` takes the symbol-install lock, sets `g_disabled` and
   `g_unloading`, removes the enable-time attach hook, stops the async dismiss
   callback and withdraws the peer (`peerState=0`, `controllerDirection=0`,
   `controllerTouch=0`) before the consumer stops. It signals the stop, posts
   `WM_QUIT` as a second signal, and joins the input thread `INFINITE` — never on
   that thread.
2. The input thread unhooks the cloak WinEvent hook, clears the cloak handle,
   closes its event and the deadline timer, unregisters raw input, releases the
   1 ms timer period, cancels the pending measurement, destroys its window,
   unregisters its fixed class under `&__ImageBase` and exits. A failed unregister
   other than `ERROR_CLASS_DOES_NOT_EXIST` blocks the unload.
3. `Wh_ModUninit` sends one synchronous teardown to the module's own hidden
   message-only window on the XAML UI thread, or runs it inline when the unload
   already is that thread. `PrepareRootElement` is admitted by
   `g_rootAdmissionLock` (shared on entry, re-checked before the first
   registration); `RoleUninit` takes the same lock exclusive before choosing the
   teardown and holds it through the turn.
4. A XAML handler on the stack defers the turn: the last UI-depth guard posts
   `kUiDeferredTeardownMessage`, and the loop serves it only after that handler and
   its C++/WinRT thunk returned. The unload waits for the acknowledgement, and
   every dispatcher turn carries the same guard.
5. The turn revokes the `Loaded`, `SizeChanged`, `LayoutUpdated` and scroller
   `ViewChanged` handlers, detaches the settle batch, stops the animations,
   restores resting `Progress` and the neutral translation, resets the scroller
   watches and drops the UI-owned references.
6. Then the UI-turn count and the registration count must be zero and one empty
   drain must acknowledge: a missed drain blocks (`ui drain not acknowledged`),
   because zero alone does not prove the last lease destructor returned. A
   windowless root after a lease was ever used blocks too. The rendezvous window is
   destroyed, its class unregistered, and only then are the IPC view and handles
   closed.

`[[clang::no_destroy]]` protects process termination when uninit is skipped; a
normal unload still clears every one of those objects.

## Explorer (explorer.exe)

1. `Wh_ModBeforeUninit` takes the symbol-install lock, sets `g_disabled`, clears
   `g_execReady`, recomputes `execCapable` and signals the stop. Hooks and the
   WndProc read `g_disabled` and pass; hook application sits inside the same lock
   as resolution, so a stop waits for a complete install or makes it refuse.
2. `Wh_ModUninit` joins the posting worker `INFINITE`. The join stops posting, not
   creation: the window lives on the launcher thread, created by the targeted
   `WH_GETMESSAGE` hook or a Win/Start-button entry, never by the worker.
3. The published HWND is destroyed synchronously on its owner thread, directly or
   with `SendMessage(kExecTeardownMessage)`; a handler that lands on a WndProc or
   `RunPressOriginal` frame defers to that frame's outermost unwind, and the unload
   waits for the acknowledgement. The class is unregistered under the same
   `&__ImageBase` only after the window is gone, then the twinui loader reference,
   the handles and the channel close.

## The host's boundary

The installed engine disables hooks, calls `Wh_ModUninit`, waits on thread call
stacks, removes hooks and frees the library. It does not promise that a hung
original returned and does not revoke a WndProc, so a permanently hung launcher or
UI thread blocks a provably safe unload. Acceptance: both hosts keep running, no
mod-owned callback is reachable, and the old DLL is gone from both module lists.

**Known blocking case.** Both symbol installs call `HookSymbols`, which blocks on
the PDB fetch. They run on the role's own thread, so the shell keeps pumping, but
a cold fetch delays the `INFINITE` join at unload until it returns.
