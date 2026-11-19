# RE — where the Start menu decides to close, and what may refuse it

> Part of [docs/README.md](../README.md). The command layer this sits above is in
> [ipc.md](../ipc.md); the gesture that wants to arbitrate is in
> [gesture.md](../gesture.md). This page is analysis only: no product code, no
> patch, no process attach.

## Scope and method

The question: for the three ways a Start menu closes — the Windows key, the
taskbar Start button, and a click outside — find the code that *decides*, not the
`Show`/`Hide` command the bridge already hooks, and say whether a close can be
refused there without leaving the shell's own "open/closed" state wrong.

Everything below is static, from artefacts on this machine:

| Artefact | Identity |
|---|---|
| `C:\Windows\System32\twinui.pcshell.dll` | 10.0.26100.9949, image base `0x180000000`, `SizeOfImage 0x9D3000`, `TimeDateStamp 0x59290A19`, SHA-256 `0D44E428FE81F68522678F0958DB8B039E1E7DC3175B0D6A641918C3B125A2AE` |
| `...\Microsoft.Windows.StartMenuExperienceHost_cw5n1h2txyewy\StartUI_.dll` | 10.0.22621.3733 (the build the shipping mod gates on) |
| `C:\Windows\explorer.exe` | 10.0.26100.9949 |
| `C:\Windows\System32\taskbar.dll` | 3 293 776 bytes; the legacy taskbar hosting the Start button (`CPearl`); public PDB `Taskbar.pdb` |
| Public PDBs | `twinui.pcshell.pdb`, `StartUI.pdb`, `explorer.pdb`, `taskbar.pdb`, fetched from `msdl.microsoft.com` by the debug-directory GUID; symbol names below are the PDB public symbols, RVAs are image-relative |

Method: PE parse, public-symbol recovery from the PDB streams, a `.pdata`-bounded
Capstone disassembly of `.text` (40 791 functions in `twinui.pcshell.dll`), a
direct-call graph (172 542 edges), and indirect dispatch sites recovered from the
`mov rax,[rax+N]; call <fothk thunk 0x76E010>` pattern (16 798 sites) — this build
routes every virtual call through the Control Flow Guard thunk in the `fothk`
section, and `call qword ptr [reg+disp]` does not appear in the disassembly at
all. Nothing was attached to, patched in, or injected into a running process; no
mod was installed, enabled, disabled or reloaded; `modlane.lock` was not taken.

Two limits are stated up front:

- the click-away path is traced from the manager inward to
  `HideStartAndCortana`; the *event source* that starts it (a delegate whose
  invoker is `sub_4916D0`) is not identified; see
  [the open link](#the-one-open-link-the-delegates-event-source).
- decompilation was not used (the Ghidra import of a 10 MB shell DLL did not
  finish inside the 330 s startup budget, and the budget was not raised while
  another lane was using the machine). The evidence is symbol-named disassembly,
  which is enough to fix the call order and the state writes, but local variable
  names are the analyst's reading, not the compiler's.

## The two layers

`CStartExperienceManager` (twinui.pcshell.dll, `explorer.exe`) is the **command
layer**. `Hide(uint32_t reason)` at RVA `0x3C5960` does one thing: it calls
`CSingleViewShellExperience::Hide` (0x165F98) and, when the view was not fully
visible, posts the completion work at `sub_3C3330`. It writes no launcher state.
Its interface is `IStartExperienceManager` at RVA `0x79B930`, where `Show` is
slot `+0x38`, `Hide` is `+0x40`, and `IsWindowFullyVisible` is `+0xB0`.

Above it sits the **decision layer**, two classes in the same module:

- `XamlLauncher` (0x140F80..0x494D1C) — the shell-side launcher object. It
  implements `IImmersiveLauncher`, `IImmersiveLauncherInternal`,
  `IImmersiveLauncherTrayNotify`, `IImmersiveShellHookNotification`,
  `IImmersiveLauncherCortana` and `IXamlLauncherStateCallback`.
- `XamlLauncherState` (0x48E53C..0x49C9C0) — the object that holds the state. It
  implements `IXamlLauncherState`, and `XamlLauncher` keeps it at `+0x160` and
  `+0x178` (two interface pointers to the same object).

`XamlLauncherState::Initialize` (0x25BA70) stores what the state machine talks to:

```
mov [r12+0x38], rbx     ; IStartExperienceManager*   (rdx)
mov [r12+0x40], rdi     ; ICortanaExperienceManager* (r8)
mov [r12+0x48], rsi     ; IRealmActivationHistory*   (r9)
mov [r12+0x68], ...     ; IXamlLauncherStateCallback* (stack)
```

So `[state+0x38]` is the manager, and every close decision is one
`[state+0x38]->+0x40` away from the `Hide` the bridge already sees.

## The state that decides

There is one dword — `XamlLauncherState::InternalState`. Its initial value is
written by the constructor (`mov dword ptr [rdi+0x40], esi`, `esi = 0`), and it is
read by `IsStartVisible` (0x1B6050):

```
mov edx, [rcx+0x28]      ; the field, from this interface's `this`
sub edx, 5   ; 5  -> visible
sub edx, 4   ; 9  -> visible
sub edx, 2   ; 11 -> visible
sub edx, 1   ; 12
sub edx, 1   ; 13
sub edx, 1   ; 14
sub edx, 2   ; 16
sub edx, 1   ; 17
cmp edx, 5   ; 22 -> visible
```

`IsStartVisible` is true for **{5, 9, 11, 12, 13, 14, 16, 17, 22}**. One of those
values is a *transient closing* state, and that single fact explains most of the
behaviour the owner reported (below).

Values seen being written, and by whom:

| Value | Written by | Meaning (inferred from the writes) |
|---|---|---|
| `0` | ctor; `HideStartAndCortana` (0x24BF47, 0x24BFC7); `OnStartHidden` for 17 and 11 | at rest / closed |
| `5` | `OnStartShown` (`mov [rsi],5`, 0x141F74) | open and settled |
| `6` | `Show` (0x49C0EC) | open, search view |
| `7` | `OnStartHidden` for 14 (0x65F0E) | open on the Cortana box |
| `8` | `XamlLauncherState::Hide` (0x499F49); `Toggle` (0x49C763) | hide started, **not** in the visible set |
| `9` | `HideStartAndCortana` path for state 5 (0x24BEBF) | dismiss started, **still** in the visible set |
| `0xA` | `OnStartHidden` for 9 (0x65F16) | dismiss finished, not visible |
| `0x11` | `HideStartAndCortana` (0x24BE95); `Show` (0x49BFC8) | Cortana transition |
| `0x10` | `OnStartShown`, Cortana-transition branch (0x142152) | shown, Cortana exit in flight |

The state is a two-phase close: a decision writes a transient value, calls the
manager, and the manager's `Hidden` event finishes the transition. The event path
is confirmed:

```
CStartExperienceManager::OnViewHidden (0x1D2870)
  -> InvokeAll on the Hidden event source  [rbx+0x20]   (0x1D28DC)
     -> XamlLauncherState::OnStartHidden (0x65E70)
        state 0x11 -> 0, state 9 -> 0xA, state 0xB -> 0, state 0xE -> 7
```

`IsStartVisible` including `9` is not an oversight: while a dismiss is in flight
the launcher still reports itself open, so a second toggle is a *dismiss*, not an
open.

## Trigger 1 — the Windows key

Synchronous, entirely in `explorer.exe` / `twinui.pcshell.dll`.

### What the launcher is told about the key

`OnHotkeyInvoke` receives a single `IMMERSIVELAUNCHERSHOWFLAGS` value, not a key
event, and neither caller carries a virtual-key code, a scan code or a modifier
mask:

- `OnShellHookMessage` (0x492BB0) ignores both of its message arguments. It runs
  one feature check, reads a byte at `+0x139`, and calls
  `OnHotkeyInvoke(flags = 0)` at 0x492BE8.
- The second caller, the delegate `sub_4917A0`, chooses `flags = 8` when its
  event argument is 1 and `flags = 0` otherwise (`cmp r8d,1; mov edx,8;
  cmovne edx,eax`), and forwards to the same function at 0x4917B6.

There is no comparison against `VK_LWIN` (0x5B), `VK_RWIN` (0x5C), or any
modifier state anywhere in the launcher, and no key message identity is passed in.
So the plain-Win versus Win-plus-combination decision is **upstream** of this
function: by the time the launcher is called, the shell has already resolved the
input to "open or close Start". That is also consistent with the machine's
behaviour (Win+E opens Explorer and never opens Start).

Consequence for the design: a hook on `OnHotkeyInvoke` cannot tell a combination
from a bare Win because there is nothing left to tell — but it also cannot break
combinations *for opens*, because combinations never reach it. The one place the
question becomes live is a design that owns the **open** path (see option B
below); owning only the **close** path never sees a combination that was not
already resolved to "close Start".

```
shell hotkey / shell hook
  -> XamlLauncher::OnShellHookMessage  (0x492BB0, IImmersiveShellHookNotification)
       -> XamlLauncher::OnHotkeyInvoke (0x14195C, flags=0)      site 0x492BE8
            if [state+0x178]->IsStartVisible()                  slot +0x68
                XamlLauncher::Dismiss(1)                        (0x25D670)
                  -> [state+0x160]->DismissStartAndCortana(1)   slot +0x40
                else
                  XamlLauncher::ShowStartView(1, flags)         (0x141AF0)
```

`OnHotkeyInvoke` in full, minus logging:

```
141973  mov  rcx,[rcx+0x178]        ; the state object
141989  mov  rax,[rcx]
14198C  mov  rax,[rax+0x68]         ; IXamlLauncherState::IsStartVisible
141990  call <cfg thunk>
141995  test al,al
141997  je   1419d0                 ; not visible -> show
141999  lea  rcx,[rbx+0x18]
14199D  mov  edx,1
1419A2  call 0x25D670               ; XamlLauncher::Dismiss(1)
...
141ABD  lea  rcx,[rbx+0x18]
141AC1  mov  r8d,edi                ; flags
141AC4  mov  edx,1                  ; show method
141AC9  call 0x141AF0               ; XamlLauncher::ShowStartView
```

`XamlLauncher::Dismiss` is a three-instruction forwarder to the state's
`DismissStartAndCortana`; `XamlLauncher::Hide` is the same shape for `Hide`. There
is no separate "is it open" decision in the command layer.

**Where the state moves.** `DismissStartAndCortana` (0x4991C0) writes only
`[state+0x118] = method` (the last dismiss method, telemetry). It calls
`HideStartAndCortana(method)` at 0x499252 when the state is visible, or when
Cortana's find-in-start view is visible. `HideStartAndCortana` (0x24BCB0) is where
the state moves and where `Hide` is called, for the visible case (state 5):

```
24BEBF  mov  dword ptr [rcx+0x28], 9     ; transient "dismissing"
24BEC6  mov  rcx,[rcx+0x40]              ; ICortanaExperienceManager
24BED1  call [rax+0x60]                  ; Cortana-side hide
24BED6  mov  rcx,[rdi+0x38]              ; IStartExperienceManager
24BEDD  mov  edx,esi                     ; the dismiss method, i.e. 1 here
24BEDF  mov  rax,[rax+0x40]              ; -> CStartExperienceManager::Hide
24BEE3  call <cfg thunk>
```

`edx = 1` is exactly the `reason=1` the bridge logs for a Windows-key close
([ipc.md](../ipc.md)), which ties the measured log to this call site.

**Refusable — minimal point and compensation.** Yes. Hook `XamlLauncherState::HideStartAndCortana`
(0x24BCB0) at entry and return without calling the original. Compensation:
**none**. The hook reads and writes no state, the manager is never called, and
the rest of the frame (the Cortana-side hide, the manager `Hide`, the telemetry
teardown) never runs because the whole frame is skipped. What
`DismissStartAndCortana` already wrote (`[+0x118]`, and `ClaimForeground` for
method 9) is harmless and needs no undo.

## Trigger 2 — the taskbar Start button

Synchronous, in `explorer.exe` / `twinui.pcshell.dll`.

```
taskbar Start button
  -> IImmersiveLauncherInternal::OnStartButtonPressed(show, dismiss)   0x492C90
       (the stored vtable view at 0x77E040 carries it at +0x78)
```

Which module makes that call is now fixed to the legacy taskbar, `taskbar.dll`
(loaded in `explorer.exe`), not the XAML taskbar: `Taskbar.View.dll` (68 718
public symbols recovered) contains no reference to `IImmersiveLauncher` or
`OnStartButtonPressed`, while `taskbar.dll` carries `ComPtr<IImmersiveLauncher>`,
the Start button itself as `CPearl` (`CPearl::v_WndProc` 0x46770,
`CPearl::_CreateWindow` 0xB1E40), and the visibility plumbing
`HandleImmersiveLauncherVisibilityChange` on `CTaskBand` (0x654E0), `CTaskListWnd`
(0x654C0) and `CSecondaryTray` (0x669F0), plus `TrayUI::OnLauncherVisibilityChange`
(0x8E830). The exact click-to-call site inside that path is not pinned yet; the
refusal point does not depend on it.

That same visibility plumbing answers what carries the button's highlight: the
launcher publishes its visibility when the *view* shows or hides
(`PublishLauncherVisibilityState`, 0x2B20FC, from `OnLauncherShow`/`OnLauncherHide`),
and the tray reacts to that notification. So the pressed animation is the button's
own, the highlight follows the view event, and neither is written by the close
decision.

`OnStartButtonPressed` is a real toggle, not an explicit show/hide:

```
492CA5  mov  rcx,[rcx+0x160]       ; the state object
492CBA  mov  rax,[rcx]
492CBD  mov  rax,[rax+0x68]        ; IsStartVisible
492CC6  test al,al
492CC8  je   492CF2                ; not visible -> show
492CCA  mov  rcx,[rbx+0x160]
492CD1  mov  edx,esi               ; the caller's dismiss method
492CD6  mov  rax,[rax+0x40]        ; DismissStartAndCortana
492CDA  call <cfg thunk>
...
492CF2  xor  r8d,r8d
492CF5  mov  edx,edi               ; the caller's show method
492CF7  mov  rcx,rbx
492CFA  call 0x141AF0              ; ShowStartView
```

**Where the state moves.** Nowhere new: the dismiss branch calls
`DismissStartAndCortana` on the same state object, which calls the same
`HideStartAndCortana`. The button does not keep its own open/closed bit. The
visual state of the button is fed from the launcher's WNF publication,
`XamlLauncher::PublishLauncherVisibilityState` (0x2B20FC), called from
`OnLauncherShow`/`OnLauncherHide` (0x26B670 / 0x1EC560), and consumed in
`explorer.exe` by `ImmersiveLauncherStateWnfCallback`. Because that publication
follows the *view* events and not the decision, refusing the decision leaves it
consistent too.

**Refusable — minimal point and compensation.** Yes, at the same
`XamlLauncherState::HideStartAndCortana` entry, and the same answer: return
without calling the original, **no compensation**. A refusal is indistinguishable
from "the button's dismiss did nothing": the button reads `IsStartVisible` next
time and still sees 5, and the WNF publication was never touched because the view
never hid. The caller's dismiss method is the only unknown; it belongs to the
legacy taskbar (`taskbar.dll`), which is where the button lives, and it does not
affect the refusal point.

## Trigger 3 — click away / focus loss

Measured: synchronous inside `explorer.exe`, through a delegate; it does not
cross the process boundary and it does not use the `0x3FD` message.

```
click outside the Start window
  -> (a delegate inside twinui; its event source is not identified)
  -> XamlLauncherState::HideStartAndCortana(7)     0x24BCB0, entered directly
       (measured: no DismissStartAndCortana, no DismissSynchronouslyWithoutTransition,
        no 0x3FD message)
  -> ICortanaExperienceManager + IStartExperienceManager::Hide -> view hide
```

The static event path below exists and is real, but the measured click-away on this
build does not use it. A delegate invoker at `sub_4916D0` (its vtable entry is the
qword at 0x7A9938) takes the launcher from `[this+0x10]`, reads the state from
`[launcher+0xa8]`, and dispatches `+0x48` — `HideStartAndCortana` — with no
argument setup, which is why the close still carries method 7.

The 0x3FD event path, for the record: `OnMessage` (0x25B150) raises
`DismissRequested` on message `0x3FD`, and `LauncherDismissRequested` (0x491DCC)
resolves to `XamlLauncher::Dismiss`. Not one 0x3FD message reached the manager
during a measured click-away.

`OnMessage` is the only place that raises `DismissRequested` in the module:

```
25B163  sub edx,0x1a      ; WM_SETTINGCHANGE   -> StartThemeChanged
25B16C  sub edx,0x304     ; 0x31A WM_THEMECHANGED
25B178  sub edx,0xe3      ; 0x3FD -> DismissRequested (this is the one)
25B184  sub edx,1         ; 0x3FE -> Shown/Hidden InvokeAll
25B189  cmp edx,1         ; 0x3FF -> Cloak
...
25B23C  mov [rsp+0x68], eax          ; lParam low word = dismiss method
25B265  call <EventSource<...,int>::DoInvoke>   ; the only caller of this instantiation
```

`LauncherDismissRequested` allocates the coroutine `sub_491978` and returns
`fire_and_forget`; the coroutine resolves to `XamlLauncher::Dismiss`. That path is
a second, event-driven way into the same close; the measured click-away used the
direct `HideStartAndCortana` entry above instead.

**Where the state moves.** Same function, same field, same two phases: the
measured click-away wrote 5 → (close) → 0 inside `HideStartAndCortana`, exactly
as the Windows-key and taskbar closes do.

**Refusable, with two caveats — minimal point and compensation.** Yes, at
`XamlLauncherState::HideStartAndCortana` again, with **no compensation** on the
shell side: the state is untouched and the manager is never called, so the next
`IsStartVisible` still answers 5. Two caveats belong to the design, not to the
hook:

- the click that caused it *has already moved focus*. Refusing the close leaves
  the Start window uncloaked, visible, and not foreground. The next click may
  then produce a second dismiss request; the state is still 5, so that one is
  again a dismiss. This is a real user-visible oddity, not a state corruption.
- because the request is asynchronous, it can land after the panel owner has
  changed. The refusal test must therefore read a fresh ownership record, not a
  cached one.

### The one open link: the delegate's event source

The click-away close enters through `sub_4916D0`, a delegate invoker whose vtable
entry is the qword at 0x7A9938. Its registration (which event source calls it) is
not identified; the invoker itself is pinned, and so is the call it makes. The
`0x3FD` message path was checked and did not carry the measured click-away: no
`diag onmessage msg=0x3FD` line and no `PostMessageW(0x3FD)` appeared while a
desktop click closed the menu, while `HideStartAndCortana(7)` did.

What this costs: the name of the event source. What it does not cost: the
receiver and the state path are pinned, and a refusal at `HideStartAndCortana`
covers this trigger because the measured close enters exactly there.

## Why the three triggers do not behave the same

Two mechanisms, both visible in the code above.

1. **The visible-transient swallow.** A Windows-key or taskbar close moves the
   state to 9 before calling `Hide`; while the close runs it moves on to 11
   (measured: a press that landed during the retract read state 11, not 9). Both
   9 and 11 are in `IsStartVisible`'s visible set. A second Windows press or Start
   click during the slide-out therefore takes the *dismiss* branch, not the show
   branch, and `HideStartAndCortana` does nothing for that state for any method
   except 7 (and 6 with find-in-start visible):

   ```
   24BE3F  sub edx,6 ; 9 -> 3
   24BE44  sub edx,1 ; 2
   24BE49  sub edx,1 ; 1 -> falls into 24BE4C
   24BE4C  cmp esi,7 ; method 1 -> no
   24BF97  cmp esi,6 ; method 1 -> no
   24BFCB  teardown, no Hide, no state write
   ```

   That is the owner's "during the retract, Win/Start do nothing, you must wait".
   It is not a dead hook and not a lost message: the shell deliberately treats the
   in-flight close as still visible and swallows the toggle.

2. **`ShowStartView` has no state gate.** Its gates are session idle
   (`IsSessionIdle`), a full-screen experience, and a readiness/`0x425` flag; the
   function then calls the manager's `Show` through `[state+0x38]->+0x38`
   (site 0x26FF71). The body is 0x897 bytes and its only memory compares are two
   feature-flag reads (`cmp dword ptr [reg],1`) and one stack check; it never
   compares `InternalState` against any of the values in the table above. So a
   show request that arrives while a hide is committed still asks the manager to
   present the window. Inferred from that and from the owner's capture: the shell
   then plays its open transition on a window whose hide is still in flight — the
   flash, and the replayed open animation seen when a reveal pull arrived during
   a Windows-key retract. The click-away path differs only in when that request
   lands, because it is queued through a window message instead of being
   synchronous with the key.

Neither mechanism is a "different decision layer" — all three decisions are the
same `XamlLauncherState` object. The differences are (a) which state value the
decision sees, and (b) whether the request is synchronous with the input.

## Where `Hide` sits, and what dropping it really breaks

The repository states that a refused `Hide` breaks the toggle because "the
toggling state lives in the function that was skipped". The evidence corrects the
mechanism while keeping the rule:

- `CStartExperienceManager::Hide` (0x3C5960) writes no launcher state at all.
  It hides the view and, when the view was not fully visible, posts
  `sub_3C3330`. The toggle state is not there.
- The state is moved by the *caller*, `HideStartAndCortana`, and the transition
  is *completed by the view's `Hidden` event* (`OnViewHidden` -> `OnStartHidden`).
- Refusing only the command therefore strands the state at 9 (the transient it
  was set to just before `Hide`) with the `Hidden` event never coming. The shell
  then believes the menu is open-but-closing, every toggle is swallowed by the
  `9` rule above, and the Windows key and the Start button look dead until
  `StartMenuExperienceHost.exe` restarts. This is the observed failure, and it is
  a stranded transient, not an inverted toggle.

So the existing rule — never drop `Hide` at the bridge — remains right. The
refusal, if any, must happen one frame up, where skipping also skips the state
write.

## Refusal points

All addresses are RVAs in `twinui.pcshell.dll` 10.0.26100.9949; VAs are
`0x180000000 + RVA`. Prologues are the first bytes on disk, for the build gate
this repository already uses (executable section check, then prologue, then the
call target where the site is a call).

| Function | RVA | Signature (mangled) | Prologue | Covers |
|---|---|---|---|---|
| `XamlLauncherState::HideStartAndCortana` | `0x24BCB0` | `?HideStartAndCortana@XamlLauncherState@@UEAAXW4IMMERSIVELAUNCHERDISMISSMETHOD@@@Z` | `48 89 5C 24 18 48 89 74 24 20 55 57 41 56 48 8D AC 24 00 FE FF FF 48 81` | all three triggers, and `DismissSynchronouslyWithoutTransition` |
| `XamlLauncherState::DismissStartAndCortana` | `0x4991C0` | `?DismissStartAndCortana@XamlLauncherState@@UEAAXW4IMMERSIVELAUNCHERDISMISSMETHOD@@@Z` | `48 89 5C 24 08 57 48 83 EC 20 83 64 24 38 00 8B FA 48 8B D9 89 91 18 01` | outer gate for the win key and the taskbar button only; the measured click-away bypasses it |
| `XamlLauncher::OnHotkeyInvoke` | `0x14195C` | `?OnHotkeyInvoke@XamlLauncher@@AEAAJW4IMMERSIVELAUNCHERSHOWFLAGS@@@Z` | `48 89 5C 24 10 48 89 7C 24 20 55 48 8B EC 48 83 EC 70 8B FA 48 8B D9 48` | win key only; refuses the dismiss *and* the show, so it is not the arbiter |
| `XamlLauncher::OnStartButtonPressed` | `0x492C90` | `?OnStartButtonPressed@XamlLauncher@@UEAAJW4IMMERSIVELAUNCHERSHOWMETHOD@@W4IMMERSIVELAUNCHERDISMISSMETHOD@@@Z` | `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B D9 41 8B F0 48 8B 89` | taskbar only |
| `XamlLauncher::LauncherDismissRequested` | `0x491DCC` | `?LauncherDismissRequested@XamlLauncher@@AEAA?AUfire_and_forget@winrt@@UDismissShellViewRequestedEventArgs@Shell@UI@WindowsUdk@3@@Z` | `48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 48 81 EC 20 01 00 00 49` | click-away entering the shell |
| `CStartExperienceManager::Hide` (existing bridge hook) | `0x3C5960` | `?Hide@CStartExperienceManager@@UEAAJI@Z` | `48 89 5C 24 18 48 89 74 24 20 55 57 41 55 41 56 41 57 48 8B EC 48 81 EC` | command layer only; dropping it strands state `9` |

Read-only anchors, useful for a log line rather than a hook:

| Function | RVA | Why |
|---|---|---|
| `XamlLauncherState::IsStartVisible` | `0x1B6050` | the visible set {5,9,11,12,13,14,16,17,22} |
| `XamlLauncherState::OnStartHidden` | `0x65E70` | the completing transition (9→0xA, 17→0, 0xB→0, 0xE→7) |
| `XamlLauncherState::OnStartShown` | `0x141DF0` | state → 5 |
| `CStartExperienceManager::OnMessage` | `0x25B150` | the only `DismissRequested` raise (message 0x3FD) |
| `CStartExperienceManager::OnViewHidden` | `0x1D2870` | the `Hidden` event that completes every close |
| `XamlLauncherState::ShowStartView` | `0x26F780` | no `InternalState` gate; explains the replay |
| `XamlLauncher::Dismiss` | `0x25D670` | the shared tail of all three triggers |
| `XamlLauncher::DismissSynchronouslyWithoutTransition` | `0x48F7C0` | method 7 straight to `HideStartAndCortana`; B leaves it alone (display change, lock) |
| Win-key key data | — | `OnHotkeyInvoke` carries only show flags; `OnShellHookMessage` ignores its payload; the delegate passes a boolean. No VK code or modifier mask exists in the launcher, so the plain-versus-combination decision is upstream |

Identification recipe, matching the repository's rule that every hook proves its
range first:

1. module version is exactly `10.0.26100.9949` for `twinui.pcshell.dll`, or the
   file's `TimeDateStamp`/`SizeOfImage` pair above;
2. the target RVA lies inside an executable section (DOS/NT headers, section
   table, `SizeOfImage`), the same probe the Start mod already uses;
3. the first 24 bytes equal the prologue in the table;
4. for a refusal hook, additionally require that the call site
   `[this+0x38]->+0x40` (manager `Hide`) is present in the function body, so a
   build whose state machine moved but whose prologue survived is still refused.

## Design proposal A — refuse the close (the reveal as arbiter)

Goal: while a reveal pull owns the panel, a close request from the Windows key,
the taskbar button or a click outside must not start the shell's close. It must
not be *dropped at the command layer*, and it must not be "accepted and undone".

### Layer

Refuse at the decision, in `explorer.exe`, on
`XamlLauncherState::HideStartAndCortana` (0x24BCB0). That is the one entry the
measured click-away reaches directly, and the win key and the taskbar button reach
through `DismissStartAndCortana`; a refusal on the outer function only would miss
the click-away. Return without calling the original.

Why this layer and not the existing one:

- it is the only point where skipping also skips the `InternalState` write, so
  "refused" means "as if the user had not asked", not "closing, but the window
  stayed";
- it is upstream of the manager's cloak, so the window is never cloaked and the
  reveal keeps a real, visible panel to drive;
- it is the single function all three triggers reach, so the arbitration is one
  hook, one log line, one test;
- the WNF visibility publication follows the view events, not the decision, so
  the taskbar's Start button state stays correct without a second write.

The cost is version coupling: this is a `twinui.pcshell.dll` RVA, a module the
project does not currently hook. The bridge already pins itself and logs its
build; the same gate applies.

### Ownership record (protocol change)

The bridge cannot see the panel, and the Start mod cannot see the decision, so the
answer has to travel on the v2 channel. Add one record (a seqlock like the
others), not a command:

```
RevealOwnership {
  uint32 seq;            // seqlock, as every record
  uint32 openGeneration; // the generation the mod had when it took ownership
  uint32 owns;           // 1 while a reveal contact drives the panel
  uint64 sinceTick;      // QPC when `owns` last changed
  uint32 leaseMs;        // 250 by default
}
```

Rules:

- the Start mod writes `owns=1` when a reveal takes the panel (the same condition
  as `gestureOwnsProgress`) and `owns=0` on release, on commit, on a dismiss, and
  on unload;
- the bridge refuses a close only when `owns=1`, `openGeneration` is the current
  open generation, and `now - sinceTick <= leaseMs`; a stale record is "not
  owned", so a crashed or unloaded mod cannot wedge the shell's close;
- the bridge still passes the mod's own tap: an unconsumed `tapped` close record
  makes the `Hide` a gesture close and is never refused (the mod is the one
  closing);
- the refusal is logged once per close with the dismiss method, the state value
  read from the record, and the generation, e.g.
  `close refused reason=reveal method=1 gen=7 age=42ms`.

The `Hide(reason)` argument doubles as the trigger discriminator (`1` for the
Windows key on this build, `7` for the synchronous dismiss, the taskbar's and the
click-away's own methods for the others), so the log can tell the three apart
without another hook.

### What the mods own

- **Start mod (host):** publishes ownership; keeps driving `Progress` while the
  ownership is held; releases it *before* it arms the dismiss tap, so its own
  close is never refused; on unload writes `owns=0` and stops refreshing
  `sinceTick`.
- **Bridge (explorer):** hooks the decision, reads the record, refuses or calls
  the original, logs. It keeps its existing `Hide` hook for the gesture-tap
  passthrough and the unload drain; the `Hide` hook no longer needs a "hold" for
  keyboard closes it refuses — a refused close never reaches it.
- **No third mod.** The click-away path is covered because it converges on
  `Dismiss`.

### What this does not change

- The `9` swallow. A close that has already passed the decision is committed;
  during it, toggles are still swallowed by the shell. The reveal can still take
  the panel visually (the mod's follow), but the window may be cloaked under it.
  Fixing the swallow itself would need a different shell state transition, not a
  hook.
- The show-during-hide replay. A refuse-at-decision design avoids it by never
  starting the hide; it cannot remove it for a hide that already started.

## Design proposal B — own the decision, execute through the shell's state entry

Owner's direction: do not arbitrate the shell's close; take the three triggers
over, let the mod decide when — and whether — the close happens, and let the
shell's own state machine and visuals advance together.

**Feasibility, stated precisely: yes, but only if the deferred call goes to the
decision function, not to the command.** The transition (5 → 9 → `Hidden` →
0xA) lives in `DismissStartAndCortana`/`HideStartAndCortana` and in the view's
`Hidden` event; `CStartExperienceManager::Hide` writes nothing. So "swallow the
decision and later call `Hide`" is exactly the broken variant measured before: a
hidden window whose launcher still says 5, and a next toggle that is a Dismiss
with nothing to dismiss. "Swallow the decision and later call the decision" is
correct: the state moves when the shell actually closes, not before.

### Shape

- **Swallow point:** `XamlLauncherState::HideStartAndCortana` (0x24BCB0), not
  `DismissStartAndCortana`. Measured: the click-away close enters
  `HideStartAndCortana(7)` directly, so a hook on the outer
  `DismissStartAndCortana` would miss it; the Windows key and the taskbar button
  reach the same function through `DismissStartAndCortana` (methods 1 and 6/7).
  Return without calling the original, ask the mod, then either call the original
  or drop the request.
- **Leave alone:** `DismissSynchronouslyWithoutTransition` (0x48F7C0, method 7,
  called on display changes and lock) and every show path. B owns closes only;
  opens keep the shell's `ShowStartView` and the mod's existing adopt/takeover.
- **What the deferral does not fix (measured).** With a 400 ms hold at
  `HideStartAndCortana`, `InternalState` did stay 5 for the whole hold, and the
  close then ran normally. A second Windows press queued during the hold was
  delivered *inside* the original call, at state 11, and became a dismiss that
  `HideStartAndCortana(11, method 1)` discards in 0 ms — the shell's hide pumps
  its message queue, so the press is not held back until the close is over. B
  therefore does not by itself make "Win during the retract" reopen the menu; it
  removes the stranded-state class and gives the mod the decision *before* the
  close is committed.
- **Deferred execution:** the same blocking hold the bridge already performs on
  `Hide` — publish the retract request on the v2 channel, wait for the plan, hold
  the answered duration, then call the original `HideStartAndCortana(this,
  method)`. This needs no new marshalling: all three triggers decide on explorer's
  UI apartment thread (window messages for the Win key and the click-away, the STA
  for the taskbar call), and the hold is the same one the bridge already performs
  one frame lower. The measured run confirmed the thread and the hold: state 5 for
  the whole 400 ms.
- **Refusal:** the mod answers `cancel` instead of a duration (the reveal owns the
  panel); the bridge returns without calling the original. State stays 5.
- **Protocol:** no standing ownership record is needed under B. Every close is a
  request on the existing retract pair, and the answer carries the decision — a
  duration to retract and then close, or `cancel`. A's lease rule survives only as
  the no-answer fallback: no plan within the timeout means the mod is not there,
  so the bridge calls the original at once instead of holding a close the mod
  cannot animate.

### What B buys over A

- the state is 5, not 9 or 11, while the mod's retract runs, so a refused or
  delayed close never leaves the launcher in a transient the shell then swallows,
  and the next press sees an open menu, not a "closing" one;
- the refusal is a special answer on the existing request, not a second code path;
- the click-away close is covered by the same hook (measured: it enters
  `HideStartAndCortana` directly), which the earlier `Dismiss`-level plan would
  have missed;
- combinations never enter the close path: `HideStartAndCortana` is only reached
  for a request the shell already resolved to "close Start" (measured: Win+E and
  Win+D never reach it; Win+Tab and Win+Shift+S reach the decision with their own
  methods and close the menu as they always did).

### What B costs, and what it does not fix

- `HideStartAndCortana` is also the target of `DismissSynchronouslyWithoutTransition`
  (method 7, display changes and lock); the hook must pass method 7 through
  without a hold, or a lock screen would wait on a retract;
- the hold blocks explorer's UI thread one frame earlier than today, for the same
  duration the bridge already blocks it — the documented cost, with a consistent
  state instead of a transient;
- it still does not make an *already started* shell hide interruptible, and it
  does not stop the shell swallowing a press that lands inside that hide: only
  closes that B intercepts are timed by the mod.

### A or B — the choice

| | A (refuse) | B (own and execute) |
|---|---|---|
| hook | `HideStartAndCortana`, refuse when owned | `HideStartAndCortana`, hold then call when the mod is done |
| state during the mod's animation | 5 (nothing started) | 5 (measured over a 400 ms hold) |
| press during the mod's retract | the request is refused again, menu stays | the press is queued; it lands inside the shell's hide and is swallowed |
| stranded state after a skipped close | none (the decision never runs) | none (same) |
| click-away | covered by the same hook | covered by the same hook |
| protocol | one ownership record | the existing retract request/answer plus an outcome field (`retract`/`cancel`) |
| risk | small, surgical | one frame higher, same blocking cost |

**Recommendation: B, with A's lease rule as the fallback.** If the mod is absent,
the ownership record is stale, or the plan does not arrive, the bridge calls the
original immediately — today's behaviour. A's refusal then becomes the mod's
`cancel` answer on the same request, so there is one mechanism, not two. Do not
install both hooks; they would race on the same state.

## Not feasible, or high risk

- **Refusing at `CStartExperienceManager::Hide` and repairing the state.**
  Requires writing the private `InternalState` dword, racing the `Hidden` event,
  and guessing the state to restore (5 vs 9 vs 0xA), all from the bridge, for a
  value it cannot name. Rejected.
- **Cancelling the manager's hide after it started** (uncloaking the window,
  reversing `CSingleViewShellExperience::Hide`). No public path; the cloak is the
  shell's, and the hide is already committed at the animation layer.
- **Making Win/Start reopen during a retract by calling `ShowStartView`.** It has
  no state gate, which is exactly why the replay happens: calling it during a
  hide asks the manager for a second presentation on a window whose hide is in
  flight. It is the current bug, not a fix.
- **A host-side veto in `StartUI_.dll` for click-away.** It would cover only the
  asynchronous trigger, needs its own build gate on `10.0.22621.3733`, and the
  host does not know whether the reveal owns the panel; it would have to read the
  same record through the same channel.
- **Hooking the taskbar's XAML module.** Not needed: the first explorer-side
  decision is already `OnStartButtonPressed`, which forwards to the same
  `DismissStartAndCortana`; a hook there would add a second module to gate for no
  extra coverage.
- **Cost of the accepted risk:** one more RVA in a module that Windows updates on
  its own schedule. A build change disables the arbiter (the decision hook does
  not install), and the behaviour falls back to today's. That is the intended
  failure mode, but it must be logged loudly, not silently.

## Runtime experiments — results (2026-11-19)

Run on the merged tree (Start mod 0.35.0 `931F5625…`, bridge 0.16.0) with a
diagnostic bridge build in this worktree: read-only hooks on
`DismissStartAndCortana`, `HideStartAndCortana`, `DismissSynchronouslyWithoutTransition`,
`CStartExperienceManager::OnMessage` and `PostMessageW`, each logging
`t=<GetTickCount64>` with method and state; refusal and a 400 ms hold were driven
by two flag files and a 250 ms lease. `twinui.pcshell.dll` 10.0.26100.9949. The
lock was taken for the whole run; the experiment build was replaced by the
shipping bridge at the end, the Start mod source still hashes to
`931F5625D361D768B7356161C3BDBB113EFFD81CA75141BE4CD28A4F20A972B7`, the host was
restarted, the menu left closed, the wallpaper is still
`…\IrisService\17626005499944826467\134395157138003260.jpg` with
`WallpaperStyle 10`, and the lock and flag files were removed.

- **E1 — Win key.** Key at t=144298609. `dismiss method=1 state=5 visible=1` at
  +78 ms, `hidestart enter method=1 state=5` in the same tick, `hide reason=1`,
  `hide delayed reason=1 retract=387 lead=417`, `hidestart exit state=0` at
  +469 ms; the window was cloaked (closed) 2.5 s later. The 387 vs 409 is the
  panel travel (852 vs 900 DIP).
- **E1b — press during the retract.** Second tap at +282 ms after the first key:
  queued for 250 ms (the bridge's 417 ms hold blocks explorer's UI thread) and
  delivered at t=143916375 with `dismiss method=1 state=11 visible=1`, then
  `hidestart enter state=11` → `exit state=11` in **0 ms**, no `Hide`, no state
  write. The first close completed (`exit state=0`). One press is lost; the next
  press after the close opens the menu (`show`, `win open dur=387`).
- **E2 — taskbar button.** Click at (55,1752) with the menu open: `dismiss
  method=7 state=5` at +78 ms, `hidestart 7`, `hide reason=7`, retract 387,
  lead 417, exit state 0. A later identical click measured **method=6** (same
  close); the taskbar's method is therefore 6 or 7 depending on the click path,
  never 1.
- **E3 — click-away. The planned chain was wrong.** A desktop click with the menu
  open produced `hidestart enter method=7 state=5` at +78 ms **without** any
  `dismiss` line, without `dismisssync`, and without `onmessage msg=0x3FD` or
  `postmessage msg=0x3FD`. The close goes straight to `HideStartAndCortana(7)`
  (delegate invoker `sub_4916D0`), so a refusal or hold at `DismissStartAndCortana`
  would have missed it. The `0x3FD` path exists but was not used.
- **E4a — refusal is state-safe, all three triggers.** With a fresh lease and
  refusal on: Win → `dismiss 1 state=5` → `hidestart 1 refused`; button →
  `dismiss 7 state=5` → `hidestart 7 refused`; desktop click → `hidestart 7
  refused`. No `hide` line for any of them, and `start_menu.ps1` reported
  `cloaked=0 open=True` after each. A press repeated 1.2 s later still read
  `state=5`, i.e. the refused closes left the toggle untouched. With the lease
  lapsed (400 ms after the refresher stopped) the next press closed normally
  (`dismiss 1 state=5`, `hide reason=1`).
- **E4b — the B shape: a 400 ms hold at `HideStartAndCortana`.** The decision
  entered at t=144403859 with state 5 and was released at t=144404265 (406 ms)
  **still at state 5**; the original then ran. A second Win tap queued during the
  hold was delivered inside the original call, at t=144404265 with `state=11`, was
  held again for 400 ms, and its `hidestart` entered and exited at state 11 in
  0 ms — no close, no reopen. The nested hold also delayed the first close by
  ~400 ms. So B keeps the state consistent during the hold but does **not** stop
  the shell swallowing a press that lands inside the committed hide.
- **E5 — host death.** Killing `StartMenuExperienceHost.exe` while the lease was
  fresh (host 23780 → 4476): Windows relaunched the host on demand, the Start mod
  re-initialised (`mod pin`, `ipc open`), and a press 687 ms later found the menu
  closed and opened it. A later close was normal (`dismiss 1 state=5`, `hide
  reason=1`, retract 409, closed); no dead key. The within-lease press could not
  be captured: the harness's own start-up is ~600 ms, longer than the 250 ms
  lease, so the flag had already aged. The lease *boundary* itself is measured in
  E4a (refused while fresh, passes after).
- **E6 — combinations.** Menu open: Win+E produced no decision and left the menu
  open; Win+D likewise; Win+Tab produced `dismiss method=5 state=5` and closed the
  menu (Task View, as before); Win+Shift+S produced `dismiss method=1 state=5` and
  closed it (the shell treated it as a bare Win). Menu closed: Win+E nothing;
  Win+Tab produced `dismiss method=5 state=0 visible=0` and **no** `hidestart`.
  So a `HideStartAndCortana` hook is not reached by Win+E/Win+D at all, and
  Win+Tab/Win+Shift+S reach it only when they genuinely close Start; they must be
  passed through, not held, or a system close waits on a retract. Win+L was not
  run: it locks the session and would have blocked the restore; it is the one
  remaining check.
- **E7 — Start button visual.** Mean brightness of the button rect (0,1704,110,96):
  closed 55.1, open 62.9. Baseline close: 80 ms 58.7, 250 ms 59.4, 600 ms 62.9,
  1200/2200 ms 55.1. With a 400 ms hold: 80 ms 56.2, 250 ms 54.8, 600 ms 58.3,
  1200/2200 ms 55.1. The press feedback is the button's own and decays; the
  "open" highlight clears only when the shell actually hides (600 ms in the hold
  run, still 58.3), and both runs settle on the closed baseline. No stuck pressed
  or highlighted state.

Corrections the runs forced: the click-away chain is `HideStartAndCortana` direct,
not `Dismiss`/`0x3FD`; the refusal point must be `HideStartAndCortana` for all
three triggers; and the "press during the retract reopens under B" expectation is
false — the press is still swallowed inside the shell's committed hide.

## Evidence index

| Evidence | Where |
|---|---|
| `CStartExperienceManager::Hide` body, no state write | 0x3C5960, disassembly above |
| manager interface layout (Show/Hide/IsWindowFullyVisible) | vtable at 0x79B930; `Initialize` stores it at `[state+0x38]` |
| `IsStartVisible` visible set | 0x1B6050 |
| state writes 5/9/0x11/8/0 | 0x141F74, 0x24BEBF, 0x24BE95, 0x499F49, 0x24BF47 |
| `Hidden` event completion | 0x1D2870 -> 0x65E70 |
| Win-key chain | 0x492BB0 -> 0x14195C -> 0x25D670 -> 0x4991C0 -> 0x24BCB0 -> `Hide` |
| taskbar chain | vtable 0x77E040 slot +0x78 -> 0x492C90 -> 0x4991C0 |
| click-away chain | measured: delegate `sub_4916D0` -> `HideStartAndCortana(7)` direct; the `OnMessage` 0x3FD -> `LauncherDismissRequested` -> `Dismiss` path exists but was not used |
| E1-E7 | results section above; the click-away correction and the B-hold measurements are the two findings that change the design |
| the decision has two entrances | a scan of all 16 798 dispatch sites: the state's `+0x40` is reached only from `XamlLauncher::Dismiss` (0x25D670) and `XamlLauncher::OnStartButtonPressed` (0x492C90); the only other close entrance is `DismissSynchronouslyWithoutTransition` (0x48F7C0), which calls `+0x48` directly. No timer, policy or display path calls the decision on its own |
| no state gate in the show path | 0x26F780 compares only idle/fullscreen/readiness; manager `Show` at site 0x26FF71 |
| WNF visibility publication | 0x2B20FC, called from 0x26B670 / 0x1EC560 |

[PROTOCOL]: Update this header on change, then check AGENTS.md
