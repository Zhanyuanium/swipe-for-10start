# Executing the launcher — three prerequisites for M3

> Read-only answer. Artefacts: `twinui.pcshell.dll` 10.0.26100.9949 (public PDB,
> private symbols), `user32.dll` 10.0.26100.9949, `win32u.dll`, and the module
> list of the running `StartMenuExperienceHost.exe`. Nothing was installed,
> patched, hooked or restarted. The behavioural facts referenced here are the
> implementation lane's (the in-hook call deadlocks, `DWMWA_CLOAK` from explorer
> returns `E_ACCESSDENIED`, the first visible frame at +245..268 ms).

**One-line answers**

1. **Launcher before the first call** — read the singleton `twinui+0x948E80`
   (`XamlLauncher::s_pLauncher`). The constructor stores `this` there
   (`0x48E4E3`) and no code in the image ever clears it; hooking the constructor
   `0x48E1C0` is only the same fact with a callback.
2. **Thread and shape** — neither `ShowStartView` nor `Dismiss` contains a
   thread test; the affinity is implicit (they drive the shell's own state
   object, which captures its apartment and is fed by the shell's dispatcher).
   Call them on explorer's shell UI thread, from a **posted message to a hidden
   window created on that thread**, never inside a hook frame.
3. **Cloak** — the shell writes DWM attribute 13 with `DwmSetWindowAttribute`
   bracketed by `EnableIAMAccess(key, TRUE/FALSE)`; the ticket is why a bare call
   fails. It is not "shell only": code already inside explorer can take the same
   ticket, or call the shell's own uncloak. The owner is not required.

---

## 1. Getting `XamlLauncher` before the shell calls anything

### 1.1 The object and who builds it

`XamlLauncher` is a WRL runtime class
(`RuntimeClass<RuntimeClassFlags<1>, CWRLObjectWithSite, Implements<
IImmersiveLauncher, IImmersiveLauncherTrayNotify, IImmersiveShellHookNotification,
IServiceProvider, IImmersiveShellComponent, IImmersiveLauncherCortana,
IXamlLauncherStateCallback, IPrivilegedOperationsConsumer,
IImmersiveLauncherInternal>>`), `sizeof == 0x1F0`
(`MakeAndInitialize` allocates `0x1F0`).

| RVA | Symbol | Role |
|---|---|---|
| `0x48E1C0` | `??0XamlLauncher@@QEAA@XZ` | constructor, no arguments |
| `0x24CB90` | `MakeAndInitialize<XamlLauncher, XamlLauncher, XamlLauncherState*>` | allocates, calls the ctor, wires the state |
| `0x495E70` | `XamlLauncher_CreateInstance` | allocates a fresh `XamlLauncherState` (`0x490`, ctor `0x48E53C`), then the launcher |
| `0x48F6A0` | `XamlLauncherFactory::CreateInstance` | the WRL class factory; **no caller inside the image** |

That last line is the structural point: nothing in `twinui.pcshell.dll` calls the
factory. Every launcher is built by activation from outside the DLL's own code —
`CoCreateInstance` / an activation-factory call made by the shell against the
`Windows.Internal.Shell.XamlLauncher` class. So the first construction is the
shell's own startup path, not a user gesture.

### 1.2 The singleton, and the fact that it never goes away

`?s_pLauncher@XamlLauncher@@0PEAV1@EA` — RVA `0x948E80`. A full rip-relative
scan of `.text` finds **five** references in the whole image:

| RVA | Instruction | Function | Meaning |
|---|---|---|---|
| `0x48E4E3` | `mov qword ptr [rip+0x4BA996], rbx` | ctor `0x48E1C0` | **the only store**: `s_pLauncher = this`, right before `PrecreateStartMenuConfigRegKey` and `ret rbx` |
| `0x492C2E` | `mov rax, [s_pLauncher]` | `OnShellSessionLogonComplete` `0x492C20` | no null check; writes `[rax+0x166] = 1` |
| `0x492C4C` | `mov rax, [s_pLauncher]` | same | reads `[rax+0x165]` |
| `0x492C6A` | `mov rax, [s_pLauncher]` | same | writes `[rax+0x165] = 1` |
| `0x495DA3` | `mov rcx, [s_pLauncher]` | `s_LauncherDesktopSwitchEventProc` `0x495D20` (static WinEvent callback) | writes `[rcx+0x165] = 1` |

Three consequences, all directly usable:

- **The pointer exists as soon as the first launcher is constructed**, before any
  show / key / button call — the store is unconditional and is the last thing the
  constructor does.
- **Nothing ever writes zero.** A launcher that is destroyed leaves the global
  pointing at freed memory until the next constructor replaces it. The instance
  is a *last-known* handle, not an owned one: the constructor stores the raw
  `this` without `AddRef`, so the reference count belongs to whoever activated it.
- **The shell itself trusts it.** `OnShellSessionLogonComplete` dereferences it
  with no null check, so by logon-complete a launcher must already exist. That is
  the shell's own statement that this pointer is a session-lifetime singleton.

### 1.3 When it is created — and on which thread

The public surface says the launcher is prelaunched rather than created lazily:
`?Prelaunch@XamlLauncher@@UEAAJXZ` (`0x493220`) gates on
`?IsStartMenuPrelaunchEnabled` (`0x49189C`), and the constructor itself runs the
`tip_StartMenuAutomaticInvokeDuringLogonTest` experiment
(`0x48E4B5`..`0x48E4DB`), i.e. the object is expected to exist at logon and to be
able to invoke Start by itself.

The creating thread is the thread that performs the activation: the shell UI
thread. Two independent traces say so:

- the constructor captures a `winrt::impl::resume_apartment_context` in place at
  `+0x1E0` (`0x48E3B1`), i.e. the object records the apartment it was born in and
  resumes continuations there;
- the launcher and its state are registered as shell services
  (`IImmersiveShellHookNotification`, `IImmersiveWindowMessageNotification`,
  `IImmersiveSessionIdleNotification`), which the shell feeds from its own
  dispatch loop.

`StartMenuExperienceHost.exe` does **not** load `twinui.pcshell.dll` (module list:
`StartUI_.dll`, `Windows.UI.Xaml.dll`, `Windows.UI.Xaml.Controls.dll`,
`Microsoft.UI.Xaml.dll`, `windows.internal.shell.broker.dll`,
`CoreMessaging.dll`). The launcher is an explorer object; the window it commands
lives in the other process.

### 1.4 A captured launcher is not yet a callable launcher

The three execution entry points all take their target from `[launcher+0x160]`:

| RVA | Function | Gate | Effect |
|---|---|---|---|
| `0x141AF0` | `ShowStartView(method, flags)` | `cmp qword [rcx+0x160], 0` -> `0x80070490` | `[state]->vtable+0x38` |
| `0x25D670` | `Dismiss(method)` | `mov rcx,[rcx+0x160]; test; jne` -> `0x80070490` | `[state]->vtable+0x40` |
| `0x1F9B40` | `IsVisible(BOOL*)` | same | `[state]->vtable+0x68`, returns false when the pointer is zero |

`+0x160` is the launcher's `IXamlLauncherState*`: the object it points at has
`?ShowStartView@XamlLauncherState@@` (`0x26F780`) at `IXamlLauncherState`
vtable `+0x38`.

**Caution, and the reason "capture at ctor" is not enough.** The constructor
leaves the sentinel `0x0000000000000100` at `+0x160`
(`0x48E321: mov dword ptr [rbx+0x160], 0x100`, then `[+0x164] = 0`,
`[+0x166] = 0`). The state is attached later, by the shell's initialisation. So
"the pointer is non-zero" is not a readiness test: a launcher caught at
construction time passes the null check and then calls through `0x100`.
A guard that works is `*(uint64_t*)(launcher + 0x160) > 0x10000` (any real
pointer) plus one `IsVisible` probe before the first real call.

### 1.5 Recommendation

1. **Read `twinui+0x948E80` at the moment of need.** No hook, no lifetime
   bookkeeping, available before the first shell show / key / button call. This
   is the whole answer to the "first-open exception".
2. **Hook `0x48E1C0` only for observability** (log every construction, and the
   `s_pLauncher` write at `0x48E4E3` as a confirmation), and hook the destructor
   to know when the value went stale. Do **not** `AddRef` the launcher and hold
   it: the shell's own state would still be re-registered behind your back on a
   recreate, and you would then drive a dead state object.
3. **Never call in the same instruction stream that created the launcher.** See
   §2: the call is only safe after the shell's initialisation ran, on the shell's
   own thread.
4. **Treat "no launcher" as a normal state**, not an error: with the pointer zero
   or still holding the `0x100` sentinel, fall back to the shell's own path (let
   the gesture be the shell's own open) instead of inventing one.

### 1.6 What is not proven here

- The activation call site is outside the image (that is what "no caller inside"
  means). The claim "constructed at logon on the shell UI thread" is inferred
  from `Prelaunch` / the automatic-invoke experiment / `OnShellSessionLogonComplete`
  and from the captured apartment context, not observed. E1 below closes it.
- No getter exists. `s_pLauncher` is the only cross-call handle in the image, and
  it is written once and read as a raw pointer.

---

## 2. Thread affinity and a safe call shape

### 2.1 What the two entry points actually are

They are thin. `Dismiss` is 43 bytes: read `[+0x160]`, call `vtable+0x40`, return
`S_OK`. `ShowStartView` is the long one (0x141AF0..0x141DB9) and its body is
policy, not presentation: `DismissPopupMenus`, a lock-Z-band check,
`IsStartMenuSuppressedByShellConfigRegistry`, a telemetry activity, the
`IsHolographicHandleHotKey` special case, `AdjustMonitorConnectedIfNeeded`, and
finally `[state]->vtable+0x38`.

So the work happens inside `XamlLauncherState`, and behind it in
`CStartExperienceManager` (`Show` `0x254C90`, `Hide` `0x3C5960`,
`Uncloak` `0x2F13B0`) and `CSingleViewShellExperience` (`Show` `0x60314`,
`Cloak` `0x1AF134`). That chain owns shell services, XAML host objects and the
cross-process view wrapper — all of them created on, and pumped by, the shell UI
thread.

### 2.2 There is no explicit thread test in either entry point

- No call to `GetCurrentThreadId`, `CoIsCurrentThreadSTA`, or the C++/CX
  wrong-thread raiser exists anywhere in `0x141AF0..0x141DB9` or in `0x25D670`.
  (The image has exactly **one** reference to
  `?__abi_WinRTraiseWrongThreadException@@YAXXZ`, at `0x1CF5BD`, inside
  `?__abi_WinRTraiseException@@YAXJ@Z` — a generic raiser, not a launcher path
  check.)
- Only the launcher's own `FailFastIfNotOnCreationThread` neighbours use a
  creation-thread assertion: `XamlExplorerHostWindow@implementation` calls
  `?FailFastIfNotOnCreationThread@...` (`0x4FF8C`) at the head of every entry
  point (`put_IsCloaked` `0x4FF1C`, `put_IsVisible` `0x4F95D`,
  `put_Position` `0x4FE9D`, `XamlContent`, `InsertAfter`, `~`).

That combination is worse than an affinity check: a wrong-thread call does not
fail cleanly, it corrupts or deadlocks. The affinity is implicit in what the code
touches (SRW locks owned by the UI thread, `ComPtr`s mutated without
synchronisation, the service objects registered on the dispatcher, and — in the
host-window neighbour — a deliberate fail-fast).

### 2.3 Why the in-hook call deadlocks, and why "just move it a little later" does not help

The hook point (`XamlLauncherState::HideStartAndCortana` `0x24BCB0`, and the
launcher-side decision methods) runs **inside** the shell's own state machine,
with the state's lock held and the close already partly committed. Calling
`ShowStartView` there re-enters that state machine on the same thread: the outer
frame is waiting for a transition that the inner call has just replaced. That is
re-entrancy on a single-threaded object, not a missing apartment — which is why
moving the call "a few frames later" still hangs.

### 2.4 The safe shape

**Deliver the call as a posted message to a hidden window created on the shell UI
thread, and let the message loop run it with an empty stack.**

```
first time the bridge runs on the shell UI thread (the hook frame is fine for this part):
    RegisterClassExW(hidden class, WndProc)
    hwnd = CreateWindowExW(..., class, ..., HWND_MESSAGE or a hidden top-level)
    remember hwnd + the owning thread id

when the mod decides to open/close:
    PostMessageW(hwnd, WM_APP + kExec, (WPARAM)verb, 0)     // returns immediately
    return from the hook

WndProc, on the shell UI thread, outside any shell frame:
    launcher = *(void**)(twinui + 0x948E80)
    if (!launcher || *(uint64_t*)(launcher + 0x160) <= 0x10000) -> drop, log
    hr = verb == open ? ShowStartView(method, flags) : Dismiss(method)
    confirm: IsVisible(&visible) / the state's own visibility, log hr and visible
```

Why a window and not another mechanism:

- The thread is already pumping (it is explorer's UI thread); a message posted to
  a window it owns is the one delivery route that is guaranteed to run **after**
  the current callback returns, i.e. outside the frame that deadlocks.
- It is the same shape the shipping Start mod already uses in the host process
  (hidden top-level input window), so there is one pattern in the codebase, not
  two.
- `PostMessage` gives you a hand-off with no shared COM state, no marshalling and
  no waiting. The `WndProc` is the only place that touches the launcher.

`CoMarshalInterface` / `RoGetActivationFactory` are *not* the answer here: the
launcher is a WRL object with no proxy/stub registration, and the object is not
agile (`RuntimeClassFlags<1>` with no `FtmBase` in its implements chain, unlike
the free-threaded broker args class which lists `FtmBase` explicitly). A
cross-apartment call would be marshalled to the owning apartment anyway — which
is exactly the shell UI thread, only with more failure modes.

**Confirmation of execution.** `IsVisible` (`0x1F9B40`) is the cheapest honest
answer: it reads the same `[+0x160]` and asks the state (`vtable+0x68`) whether
the window is on screen. For a close, `Dismiss` returning `S_OK` is not proof —
confirm with `IsVisible` false. Log `hr` and the observed visible flag together;
`0x80070490` (`ERROR_NOT_FOUND`) means "no state attached", never "nothing
happened".

**If the post cannot be made** (window creation failed, wrong thread, the thread
is not pumping because the session is locked): do nothing. A refused gesture that
leaves the shell's own state untouched is recoverable; a call from the wrong
thread is not. The existing fallback rule still applies — let the shell handle
the gesture itself.

### 2.5 What happens off-thread

Static expectation, in order of likelihood: (a) a hang, because the state's SRW
lock or a cross-process view call waits for the UI thread that is now blocked in
your caller; (b) silent corruption of the launcher's `ComPtr`s (no
synchronisation in the class), which surfaces later as a crash in an unrelated
transition; (c) in the host-window neighbours, an explicit fail-fast. There is no
path that returns a clean error.

---

## 3. The cloak: who can clear it

### 3.1 The shell's actual mechanism

The Start window's cloak is decided on the shell side, not in the host. The chain
is short and fully localised:

```
CStartExperienceManager::OnMessage            0x25B150
  -> CSingleViewShellExperience::Cloak        0x1AF134  (site 0x25B1C9)
       [this+0x28] must be 1, else E_INVALIDARG
       [this+0x50] -> vtable+0x30  ...          the view/frame wrapper
  -> FrameWrapper::SetShellCloak              0x166480  (IApplicationFrameWrapper vtable +0x28)
       -> CPrivilegedPresentationOperations::SetShellCloak   0x166650
            EnableIAMAccess(key, TRUE)         0x1666CA
            DwmSetWindowAttribute(hwnd, 13, &value, 4)   0x1666F2
            EnableIAMAccess(key, FALSE)        0x166706

CStartExperienceManager::Uncloak              0x2F13B0
  -> CSingleViewShellExperience::Uncloak      0x607A8     (same object at +0x118)
```

So the answer to "which API" is: **`DwmSetWindowAttribute` with attribute
`DWMWA_CLOAK` (13)**, exactly the call the bridge already tried. What the bridge
did not have is the bracket around it:

- `CPrivilegedPresentationOperations` holds an `IamAccess` object at `+0x30`; the
  `key` used above is that object's `+0x28`.
- `IamAccess` is created by `MakeAndInitialize<IamAccess>` (`0x12EA3C`), which
  calls `AcquireIAMKey` (`0x12EAA4`, user32 delay-import ordinal 2509) and stores
  the returned key at `+0x28`.
- `TakeIamAccess` (`0x1B9F4C`) takes the object's SRW lock and calls
  `EnableIAMAccess(key, TRUE)` (`0x1B9F95`); the `SyncIamAccess` destructor
  (`0xAED50`) calls `EnableIAMAccess(key, FALSE)`.
- `EnableIAMAccess` is user32 delay-import ordinal 2510, forwarding to
  `win32u!NtUserEnableIAMAccess`. It takes the **key**, not the window: the
  windows-api thunk saves `rcx` and passes `&rcx`, so the protected argument is
  the key.

In other words: **IAM access is a per-session ticket that authorises the caller to
act on windows it does not own.** The shell takes it around every privileged
window operation (`SetActivationFilter`, `AllowSetForegroundWindow`,
`InsertAfterWindow`, `SetCancelRotationDelayHintWindow`, `SetShellCloak`, ...),
not just the cloak — the same `EnableIAMAccess` call appears in
`CPrivilegedDesktopOperations`, `CPrivilegedArrangementOperations`,
`CPrivilegedNotificationOperations` and `CPrivilegedSizeConstraintsOperations`.

### 3.2 Own window versus foreign window

The same attribute, written on a window the process owns, needs no ticket:
`XamlExplorerHostWindow::put_IsCloaked` (`0x4FEF0`) calls
`DwmSetWindowAttribute([rbx+0x28], 13, &value, 4)` (`0x4FF38`) with no
`EnableIAMAccess` anywhere in the function, and asserts the creation thread with
`FailFastIfNotOnCreationThread` first. So:

- **ownership is a sufficient right** for attribute 13;
- a ticket is needed only because the Start window belongs to
  `StartMenuExperienceHost.exe`, and explorer is not its owner.

That also explains the observed `E_ACCESSDENIED` precisely: the bridge ran the
right call as the wrong principal. It is not "the shell has a private path"; it
is "the shell holds a ticket it acquired for itself".

### 3.3 Can the owner clear it?

Static answer: **nothing on the client side stops it, and ownership is the right
the attribute needs** (§3.2). `StartMenuExperienceHost.exe` and `StartUI_.dll`
import no DWM cloak call of their own (no `DwmSetWindowAttribute` in either), so
today the host never writes the attribute — but the rule that lets the shell
write it on its own windows is the rule that would let the owner write it on its
own window.

What cannot be settled from these binaries is the DWM-side rule for the case
"another party set it, now the owner clears it": whether DWM keys the cloak to
the setting principal or only to the window. That is inside `dwm.exe` / the DWM
core in win32k, not in the modules read here. It is E3 below.

### 3.4 What to do instead of guessing

The practical route does not need the owner and does not need DWM guessing:
**clear the cloak from explorer with the shell's own ticket.**

```
// once, lazily, in explorer
MakeAndInitialize<IamAccess>(&iam);          // 0x12EA3C -> AcquireIAMKey (user32 ord 2509)
// around the write
EnableIAMAccess(iam->key, TRUE);             // user32 ord 2510
DwmSetWindowAttribute(startHwnd, 13, &zero, 4);
EnableIAMAccess(iam->key, FALSE);
```

or, better, drive the shell's own object: `CSingleViewShellExperience::Uncloak`
(`0x607A8`) on the Start experience (`CStartExperienceManager+0x118`, the object
`CStartExperienceManager::Uncloak` `0x2F13B0` already wraps), which is by
construction the same operation the shell performs at the end of the open.

Three cautions, all from the same analysis:

1. **The cloak is not the animation.** Attribute 13 hides the window; the
   245..268 ms before the first visible frame is the shell's presentation
   (DWM storyboard) plus content readiness, which is a different mechanism. An
   early uncloak can therefore reveal a window whose content is not painted yet —
   the measurable outcome is "earlier, possibly blank" rather than "faster open".
2. **The shell will still uncloak on its own schedule.** Removing the cloak does
   not cancel the shell's uncloak, so the operation must be idempotent and must
   not fight the shell's transition.
3. **Ticket acquisition is the open question.** `AcquireIAMKey` is a plain user32
   ordinal; the win32k side decides who may have a key. Code inside explorer
   inherits the shell's standing, but whether any other process could obtain one
   is not established here (E4).

---

## 4. Limits of this answer

- The launcher's activation call site is outside the image; "created at logon, on
  the shell UI thread" is inferred from `Prelaunch`, the automatic-invoke
  experiment and `OnShellSessionLogonComplete`, not observed.
- `[launcher+0x160]` is proven to hold `IXamlLauncherState` (the state's
  `ShowStartView` is at `IXamlLauncherState` vtable `+0x38`). The writer that
  replaces the constructor's `0x100` sentinel was not located; the offset is
  therefore described as a gate, not as a lifecycle.
- The `[this+0x50]` object inside `CSingleViewShellExperience` reached by `Cloak`
  was not named; only the next hop (`FrameWrapper::SetShellCloak`, and the
  identical primitive in `WindowWrapperBase::SetShellCloak` `0x46FEA0`) is
  identified. The primitive is what matters for the answer, and it is the same
  in both wrappers.
- `dwm.exe`'s server-side rule for attribute 13 is not in these binaries.

---

## 5. Experiments needing owner approval

Each is read-only in the sense of "no install, no patch, no restart"; E1–E4 need
debugger breakpoints in a live session, which is why they are listed here.

- **E1 — construction.** Break on `twinui+0x48E1C0` in explorer at logon.
  Expected: one hit, on the shell UI thread (`GetCurrentThreadId` equals the
  thread that owns `Shell_TrayWnd`), before the first `ShowStartView`; `[twinui+0x948E80]`
  equals the `rcx` of that call from then on.
- **E2 — readiness.** At the moment the bridge wants its first own open, read
  `[twinui+0x948E80]` and `[launcher+0x160]`. Expected: a heap pointer, not
  `0x100`. A run that reads `0x100` proves the "capture at ctor is too early"
  hazard.
- **E3 — owner clears.** From a probe in `StartMenuExperienceHost.exe`, call
  `DwmSetWindowAttribute(ownStartHwnd, 13, &zero, 4)` while the menu is cloaked.
  Expected (if the ownership rule holds): `S_OK` and the window appears; a
  refusal would mean DWM keys the cloak to the setting principal.
- **E4 — ticket from a non-shell process.** In a probe outside explorer, call
  `AcquireIAMKey` (user32 ord 2509) and then `EnableIAMAccess(key, TRUE)`.
  Expected: failure outside the shell's principal; success would mean the ticket
  is not a privilege boundary at all.
- **E5 — posted-message execution.** With the hidden window in place, `PostMessage`
  an open from inside the `HideStartAndCortana` hook. Expected: the hook returns
  immediately, the `WndProc` runs after it, `ShowStartView` returns `S_OK`,
  `IsVisible` true, and no hang — the falsifier for "any call from that stack
  deadlocks".
- **E6 — early uncloak.** With E5 working, add the ticket + attribute write at the
  first frame. Expected: the window becomes visible earlier; measure whether the
  content is painted. This is the only experiment that answers "can the +245 ms
  be moved".
