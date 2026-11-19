# The animation args — is there a "no animation" field?

> Read-only answer for M2. Artefacts: `windows.internal.shell.broker.dll`
> 10.0.26100.9949 (public PDB `Windows.Internal.Shell.Broker.pdb`, 7 934
> symbols) and `StartUI_.dll` 10.0.22621.3733, on this machine. Nothing was
> installed, patched or restarted. The measured facts referenced here are the
> implementation lane's (empty root visual, zero `StartAnimation` calls, a
> 268–279 ms first frame then ~7 DIP/ms for ~100 ms).

**One-line answer:** no. The args carry six read-only values that *describe* an
animation already decided elsewhere; none of them is a flag, a "none" mode or a
duration. The presentation is a **window-level DWM storyboard** attached to the
Start window on the shell side, so there is no host-side point — not this object,
not the handler, not the XAML content — that can turn it into "no animation".

## 1. The type and its complete member table

The runtime type is
`Windows.Internal.Shell.StartUI.StartExperienceAnimationRequestedEventArgs`;
the implementation class is
`StartExperienceAnimationRequestedEventArgs@StartUI@Shell@Internal@Windows`,
implemented in `windows.internal.shell.broker.dll` (the same DLL the sender
`StartExperience@StartUI` is implemented in).

Primary vtable `??_7StartExperienceAnimationRequestedEventArgs@@6B@` at RVA
`0xA2C00`:

| slot | getter | RVA | field it reads | width | observed (open) |
|---|---|---|---|---|---|
| +0x00..0x28 | `QueryInterface` / `AddRef` / `Release` / `GetIids` / `GetRuntimeClassName` / `GetTrustLevel` | — | IInspectable | — | — |
| +0x30 | `get_GlobalAnimationType` | `0x27460` | `[this+0x40]` | u16, widened to u32 | **0** |
| +0x38 | `get_TransitionType` | `0x4C170` | `[this+0x42]` | u16 | **3** |
| +0x40 | `get_TaskbarEdge` | `0x4C160` | `[this+0x44]` | u16 | **1** (bottom taskbar) |
| +0x48 | `get_EdgeGestureOffset` | `0x259C0` | `[this+0x48]` | u64 | not measured |
| +0x50 | `get_AnimationSourceId` | `0x27C30` | `[this+0x50]` | u64 | not measured |
| +0x58 | `get_AnimationProviderId` | `0x27C50` | `[this+0x58]` | u64 | not measured |

Every getter is the same shape:

```
movzx eax, word ptr [rcx+0x40]   ; GlobalAnimationType
mov   dword ptr [rdx], eax
xor   eax, eax                   ; S_OK
ret
```

There is **no `put_`** method in the class's public symbols, no boolean, no
flags, no duration, no "suppress"/"immediate"/"none" property. The object is
read-only over its ABI. Its constructor (`0x13FAC`) zeroes +0x38..+0x5F and
installs `FtmBase` in the implements chain, i.e. it is a free-threaded
marshalable object, not a struct.

The values are not authored by this object: `MakeAndInitialize<...>` (`0x26400`)
allocates it and copies a **32-byte `START_GLOBAL_ANIMATION_DATA`**:

```
struct START_GLOBAL_ANIMATION_DATA {   // StartExperienceEvents
    uint16 GlobalAnimationType;        // +0x00
    uint16 TransitionType;             // +0x02
    uint16 TaskbarEdge;                // +0x04
    uint16 pad;                        // +0x06
    uint64 EdgeGestureOffset;          // +0x08
    uint64 AnimationSourceId;          // +0x10
    uint64 AnimationProviderId;        // +0x18
};                                     // sizeof == 0x20
```

The copy is literal: `xmm0 = data[0..0xF]`, `xmm1 = data[0x10..0x1F]`, then
`cx → +0x40`, `(q >> 16) & 0xFFFF → +0x42`, `(q >> 32) & 0xFFFF → +0x44`,
`bytes 8..15 → +0x48`, `bytes 16..23 → +0x50`, `bytes 24..31 → +0x58`.

The domain of `GlobalAnimationType` is the enum
`StartGlobalAnimationType@StartExperienceEvents` (the same namespace as
`StartFocusNavigateDirection` and `StartThemeUpdateType`); the content uses it to
pick an animation (`PlayGlobalAnimation@AllAppsPane` `0x2A5EDA`,
`AppendGlobalAnimationForElement` `0x2B22EE`). Public symbols do not carry enum
values, so only the observed `0` can be stated as fact; `3` for the transition
and `1` for the edge likewise. Nothing in the class reserves a sentinel: the
constructor's zeroed defaults are the only "empty" state, and a producer that
sent type 0 is what today's open is.

**The args never reach the presentation.** `SplitViewFrame::OnGlobalAnimationRequested`
(`0x4164C0`) reads exactly the three u16 properties and hands them to the content
animation path (`0x176414` → `0x17589C`); it never touches the window, the view
host or DWM.

## 2. In-proc or proxy — and where the presentation runs

- `windows.internal.shell.broker.dll` **is loaded in `StartMenuExperienceHost.exe`**
  (read-only module list, base `0x7FFB…`), next to `StartUI_.dll`. Both the sender
  class `StartExperience` and the args class are implemented there.
- The args are created **in the host process** by `MakeAndInitialize` from a
  **WNF payload**. The subscription is a `wil::wnf_subscription_state<START_GLOBAL_ANIMATION_DATA>`
  object (vtable `0xA36A0`) created at `0x20830`; its callback
  `InternalCallback<START_GLOBAL_ANIMATION_DATA>` (`0x222AC`) **rejects any payload
  whose size is not exactly 0x20** (`cmp r8d,0x20`) and then calls a
  `wistd::function` that ends in the raiser `0x4A394`, which builds the args and
  fires `GlobalAnimationRequested`.
- So the object is in-proc, with a cross-process *data* source: the host is a
  **subscriber**. The publisher is not this module's code path from the host; it
  is whatever writes that WNF state (most plausibly the shell's view manager in
  `explorer.exe`, which is also the side that attaches the storyboard — see
  below). Pinning it needs the experiment in §6.
- The presentation is **not** in the host: `CStartExperienceManager` (twinui,
  explorer) implements `IDWMStoryboardListener` and receives
  `OnStoryboardCompleted` (`0x3C67E0`); it stores a three-qword animation
  identity with `SetAnimationData(a, b, c)` (`0x3C7740`, at
  `+0x298/+0x2A0/+0x2A8`), which has the same three 64-bit fields the args expose
  — whether the values are the same needs E-args-1 — and the view itself is taken
  away by `CSingleViewShellExperience::Hide`/`Show`. In other words: the shell
  attaches a **DWM storyboard to the Start window**, tells the content which
  animation it is (the WNF payload → these args), and DWM runs the window
  transition. The lane's own measurements fit: the content root is empty,
  no `StartAnimation` is called on it, and the window's first frame arrives after
  a fixed 268–279 ms, then slides ~100 ms at ~7 DIP/ms.

## 3. If the field existed… and why it does not

There is no field to rewrite into "no animation". The six properties are
read-only and none is a control:

- type / transition / edge choose a **content** animation, not the presentation;
- offset / source / provider identify the animation for the content, and the
  manager receives the same three values on the shell side, where the storyboard
  is already committed;
- there is no duration, no "animate = false", no "instant" value, and no setter.

Writing the in-proc object's fields is technically possible (they are plain
dwords/qwords at +0x40..+0x58), but static analysis says the only reader is the
host's own handler, so a rewrite can at most change which content animation
plays. The window was already attached and started on the shell side before the
event is raised.

**Conclusion: the presentation cannot be parameterized from the host side.**

## 4. Window or content?

Window-level. The args describe an animation whose implementation is a DWM
storyboard on the Start window (`IDWMStoryboardListener` on the manager,
`SetAnimationData`, the view host's show/hide). The content is carried by the
window, and the host's `Progress` (a composition translation on the root visual)
moves content **inside** the window. Therefore:

- `Progress` cannot cancel, shorten or retime the presentation; it can only move
  what is inside once the window exists.
- This is consistent with the lane's finding that the presentation survives a
  dropped type-0 request: the request is a description for the content, not the
  presenter.
- Taking over after the presentation (the reveal's entry/chase) is still valid:
  the mod drives the content once the window is uncloaked, which is exactly the
  measured 268 ms boundary.

## 5. What this means for M2

- There is no host-side switch to make Start present instantly. The only levers
  are shell-side: the window's storyboard/cloak (the view host), or the values
  the publisher sends.
- The host-side reachable points are: the WNF subscription (read-only), the args
  object (rewritable in-proc, content-only effect), and the handler (skippable,
  content-only effect). None changes the window.
- If "no animation" is a requirement, it has to be pursued where the storyboard
  is attached (the shell's view manager / DWM), which is outside M2's host scope.

## 6. Experiments needing owner approval

- **E-args-1 — data identity.** Hook the WNF callback `0x222AC` (or the raiser
  `0x4A394`) and log the 32-byte payload next to the three getters. Expected: the
  payload equals what the args expose, at the same tick, and no host-side write
  precedes it.
- **E-args-2 — rewrite the args.** In `OnGlobalAnimationRequested`, before calling
  the original, write `+0x40/+0x42/+0x44` to different values. Expected: the
  content animation changes (or stops), the window's 268–279 ms and the 100 ms
  slide are unchanged. This is the falsifier for "the args could drive the
  presentation".
- **E-args-3 — the publisher.** Break on `NtUpdateWnfStateData` with a 0x20-byte
  payload for that state during a Win open; capture the stack. Expected: an
  explorer-side caller (view manager), not the host.
- **E-args-4 — window versus content.** Sample the Start window rect and the
  cloaked attribute during the 268–279 ms and the 100 ms slide with `Progress`
  pinned. Expected: the window rect (or its composition offset) moves while the
  content translation stays, i.e. window-level.

## 7. Limits

- Enum domains (`StartGlobalAnimationType`, the transition/edge enums) are not in
  the public PDB; only the observed values are stated. A full domain needs a
  producer-side dump (E-args-1/3).
- The WNF state name at the subscription site (`mov rdx,[rip+…]`, RVA global
  `0xAC798`) is not reported: the data symbol recovered there is
  `WNF_TMCN_ISTABLETMODE`, which does not fit a 0x20-byte animation payload, so
  either the public data symbol is misattributed or the load is a pointer to the
  name. The name is not needed for the conclusion; E-args-3 would confirm it.
- The publisher is inferred, not observed; the host's role as a subscriber is
  observed (subscription object + 0x20 size check + raiser).
