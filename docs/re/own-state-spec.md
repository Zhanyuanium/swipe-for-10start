# Own-state spec — taking the Start toggle away from the shell

> Implementation contract for `feat/own-start-state` (from `v0.5.0`, Start mod
> 0.36.0 / bridge 0.17.0). Read-only analysis; nothing here is installed. The
> measured facts are the E1–E7 run in [close-decisions.md](close-decisions.md);
> the open/close primitives below were re-checked against
> `twinui.pcshell.dll` 10.0.26100.9949 on 2026-11-20.

## 0. What ships today, and what "own" adds

`v0.5.0` already decides every **close** at `XamlLauncherState::HideStartAndCortana`
(bridge 0.17.0): a tapped gesture close passes, method 7 is never held, every
other method asks the Start mod for a retract and holds the original for the
answered duration, and the Start mod may answer `cancel` while a reveal owns the
panel. What it does **not** decide is whether a press is an open or a close — the
shell still asks `IsStartVisible` first, and a press inside a committed close is
swallowed at state 9/11 (E1b, E4b).

"Own" means: the bridge decides open vs close from its own boolean, and calls the
shell's own open/close functions at the moment it chooses. The shell keeps doing
the animation, the state transition and the visual; we keep the decision.

## 1. The two decision entries

Both entries are plain member functions in `twinui.pcshell.dll`; neither holds a
lock, both run on explorer's UI thread, and both are called once per press. The
shipped close hook (`HideStartAndCortana`) stays installed for the **execution**
of a close; the new hooks only decide.

### 1.1 Windows key — hook `OnHotkeyInvoke`

| | |
|---|---|
| RVA / VA | `0x14195C` / `0x18014195C` |
| symbol | `?OnHotkeyInvoke@XamlLauncher@@AEAAJW4IMMERSIVELAUNCHERSHOWFLAGS@@@Z` |
| signature | `HRESULT (*)(void* self, uint32_t showFlags)` |
| prologue (20 B) | `48 89 5C 24 10 48 89 7C 24 20 55 48 8B EC 48 83 EC 70 8B FA` |
| body landmark | `mov rcx,[rcx+0x178]` then `vtable+0x68` (`IsStartVisible`), and `lea rcx,[rbx+0x18]` before `Dismiss`/`ShowStartView` |
| `this` | **the outer object; the launcher is `self + 0x18`** — `[self+0x178]` is the state, `[self+0x18+0x160]` is the same pointer |
| callers | `OnShellHookMessage` (0x492BB0, `flags=0`) and the delegate `sub_4917A0` (`flags=8` when its arg is 1) |
| measured | a Win press with the menu open: `dismiss method=1 state=5` 78 ms after key-down |

Hook it, read the truth (section 3), decide, and call either
`XamlLauncher::ShowStartView(launcher, 1, 0)` or `XamlLauncher::Dismiss(launcher, 1)`
— or return `S_OK` without the original to swallow the press. Calling the
original runs the shell's own open/close decision; that is the fallback path.

**Why not `OnShellHookMessage`:** it is only one of two callers. The hotkey is
also delivered through the delegate `sub_4917A0`, so a hook there would miss that
half. `OnHotkeyInvoke` is the single choke point for both.

**Combination hazard (must be gated).** `OnHotkeyInvoke` carries no key
information (measured: only `showFlags`, and `OnShellHookMessage` ignores its
payload). A combination that the shell resolves to "close Start" looks exactly
like a bare Win press here: Win+Shift+S with the menu open measured as
`dismiss method=1` 468 ms after the key. Therefore:

- at entry, if `GetAsyncKeyState(VK_SHIFT/VK_CONTROL/VK_MENU)` reports a held
  modifier, **call the original and return** — the shell keeps the combination;
- E-own-2 below is the admission gate: Win+Shift+S with the menu **closed**, and
  with the menu open, must both keep their own effect. If the modifier heuristic
  fails (the shell delivers the press after the user released the modifier),
  hooking `OnHotkeyInvoke` is not safe and the takeover must fall back to
  decision-at-execution: hook `XamlLauncher::Dismiss` / `ShowStartView` only and
  keep the shell's toggle (i.e. stay at v0.5.0 behaviour plus our own timing).

### 1.2 Start button — hook `OnStartButtonPressed`

| | |
|---|---|
| RVA / VA | `0x492C90` / `0x180492C90` |
| symbol | `?OnStartButtonPressed@XamlLauncher@@UEAAJW4IMMERSIVELAUNCHERSHOWMETHOD@@W4IMMERSIVELAUNCHERDISMISSMETHOD@@@Z` |
| signature | `HRESULT (*)(void* self, uint32_t showMethod, uint32_t dismissMethod)` |
| prologue (20 B) | `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B D9 41 8B` |
| body landmark | `mov rcx,[rcx+0x160]` then `vtable+0x68` (`IsStartVisible`) and `vtable+0x40` (`DismissStartAndCortana`) / `call 0x141AF0` |
| `this` | **the launcher** (`[self+0x160]` is the state) |
| interface | `IImmersiveLauncherInternal`, stored vtable view at 0x77E040, slot `+0x78` |
| measured | `dismiss method=7` (once 6) with state 5, same close as the Win key |

It is a pure toggle: it reads `IsStartVisible` and calls
`DismissStartAndCortana(dismissMethod)` or `ShowStartView(showMethod, 0)`. Swallow
it (return `S_OK`) and call our own choice; call the original to pass it through.
No combination risk: nothing else reaches this entry.

### 1.3 Click away — not a decision entry

Measured (E3): the click-away close does **not** pass `DismissStartAndCortana`
and does not use the `0x3FD` message; it enters
`XamlLauncherState::HideStartAndCortana(7)` directly through the delegate invoker
`sub_4916D0`. The shipped close hook already sees it. It must stay a **close**
in our model (the owner's boolean is "open" when a click-away arrives, so it is a
normal close), and it must keep method 7's "never hold" rule.

## 2. Executing our decision

### 2.1 Close

Call `XamlLauncher::Dismiss(launcher, method)`:

| | |
|---|---|
| RVA / VA | `0x25D670` / `0x18025D670` |
| signature | `HRESULT (*)(void* launcher, uint32_t method)` |
| prologue (20 B) | `48 83 EC 28 48 8B 89 60 01 00 00 48 85 C9 75 07 B8 90 04 07 80` |
| body | `[launcher+0x160]` is the state; calls `state->DismissStartAndCortana(method)` (`vtable+0x40`), returns `S_OK` |

It is a forwarder to the state's `DismissStartAndCortana`, and it is the right one
to call: that function writes the state's `+0x118` (last dismiss method) and runs
the same `ClaimForeground` special case the key path does. Everything after it is
the shipped pipeline: state 5 → 9 → (11 during the animation) → `Hidden` → 0/7.
**We never write the state.**

`method` values to use: `1` for a Windows-key close (matches the shell's own
measurement and the bridge's logs), the button's `dismissMethod` for a button
close. Do not invent values; the Start mod and the logs key on them.

If the launcher pointer or the state is null, `Dismiss` returns `0x80070490`;
log and fall back to calling the original.

`Dismiss` returning success is **not** a promise that the close happened: the
shipped close hook may still answer `cancel` while a reveal owns the panel, and
then no `Hidden` event follows. Do not write `g_open = false` as a fact — either
leave it to the events, or set it optimistically and let the next decision's
`SyncOpen` restore it (that next press then reads "open" and closes, which is the
correct behaviour after a cancelled close).

### 2.2 Open

Call `XamlLauncher::ShowStartView(launcher, 1, 0)`:

| | |
|---|---|
| RVA / VA | `0x141AF0` / `0x180141AF0` |
| signature | `HRESULT (*)(void* launcher, uint32_t showMethod, uint32_t showFlags)` |
| prologue (20 B) | `48 89 5C 24 20 55 56 57 41 56 41 57 48 8D AC 24 70 FF FF FF` |
| body | `[launcher+0x160]` must be non-null; pre-checks context menus / lock Z-band; then the state's `ShowStartView` (`vtable+0x38`) → `IStartExperienceManager::Show` |

`showFlags = 0` is the plain open. The bits only relax pre-checks: 0x8 skips the
foreground/context-menu check, 0x4 tolerates an open context menu (from the
prologue's `test r14b,8` / `test r14b,4`). Use 0 unless a measurement says
otherwise.

After the call the manager presents the window; `OnStartShown` (0x141DF0) sets
`InternalState = 5` and the Start mod runs one settle from rest (`win open
dur=409` at this layout). We do not write the state or animate the open.

### 2.3 Open while a close is in flight (state 9/11) — the owner's case

The static facts: `ShowStartView` has **no `InternalState` gate** (0x26F780,
0x897 bytes, no compare against the state values), so it asks the manager to
present the window whatever the state is. The shell's pending hide is a separate
commitment; the `Hidden` event may still arrive after the `Shown` event. What the
pair does to `InternalState` is the one unknown that needs E-own-1.

Two-step design, so the press never becomes a no-op:

1. **Immediate try.** In the decision hook, when the press is a close request but
   our boolean says open and the state is a close transient (`state != 5 &&
   visible`, i.e. 9/11/12/13/14/16/17), call `ShowStartView(launcher, 1, 0)` and
   log `own reopen during close state=... hr=...`.
2. **Latch.** Set `g_pendingOpen = true` with the tick before the call; clear it
   when `OnStartShown` fires (state 5) or after 1 s. If the state lands on 0/7
   (`OnStartHidden`) while `g_pendingOpen` is set, serve it there: call
   `ShowStartView` again from the settled closed state and log
   `own reopen served after close state=...`. That path is guaranteed to work
   (state 0 → open), at the cost of the rest of the close animation.

Do not call `ShowStartView` twice in a row; the latch is the retry.

If E-own-1 shows that the immediate call leaves a shown window with state 0
(the late `Hidden` mapping 11 → 0), the bridge must not accept "shown + state 0":
read the truth after 100 ms and, if `IsStartWindowVisible()` is true while the
state is 0, call `Dismiss(launcher, 1)` then `ShowStartView` (a clean close/open
pair) or simply latch-and-retry once. That repair belongs to the bridge, not to
the Start mod.

### 2.4 Failure and return codes

- `Dismiss` returns `S_OK` after handing off; a negative HRESULT means the
  launcher was not initialised — log and call the original hook.
- `ShowStartView` returns the manager's HRESULT; `0x80070490` (not found) means
  no state; negative means the shell refused (lock Z-band, context menu). In both
  cases leave the menu closed, keep our boolean at false, and log
  `own open refused hr=...`.
- Never leave `g_open` true after a refused open.

## 3. State ownership

**Truth is the shell's cloak, not our memory.** `IsStartWindowVisible` is the
method the shell itself uses; it calls `IStartExperienceManager::IsWindowFullyVisible`,
which answers the window's cloaked attribute:

| | |
|---|---|
| RVA / VA | `0x1D4350` / `0x1801D4350` |
| symbol | `?IsStartWindowVisible@XamlLauncherState@@UEAA_NXZ` |
| signature | `bool (*)(void* state)` |
| prologue (20 B) | `48 83 EC 28 48 8B 49 38 48 8D 15 61 00 00 00 C6 44 24 30 00` |
| slot | `IXamlLauncherState` vtable (0x77E3C8) `+0x70` |
| fallback | `manager = *(void**)(state+0x38)`; `manager->vtable[0xB0]` = `IsWindowFullyVisible` |

`state = *(void**)(launcher + 0x160)`. Call it at **every decision** (one
cheap virtual call) and adopt the answer:

```
bool shellOpen = state ? IsStartWindowVisible(state) : false;
if (shellOpen != g_open) { log own state resync cached=... shell=...; g_open = shellOpen; }
```

Mirror the events as a fast path: hook `OnStartShown` (0x141DF0) → `g_open = true`
and serve a pending open; hook `OnStartHidden` (0x65E70) → `g_open = false` and
serve a pending open from state 0/7. The per-decision read is the self-heal; the
event hooks only make the boolean fresh between presses. No other caller needs a
hook: a close or an open started by another component still ends in one of those
two events, and the next decision re-reads the truth anyway.

Do **not** read `InternalState` at `+0x28` for the boolean: it is interface-relative,
it is 9/11 during a close — which the shell calls visible — and it does not answer
"is the panel on screen".

## 4. Consistency risks

| Risk | Under takeover | Verification |
|---|---|---|
| Taskbar Start highlight / pressed state | Press feedback is the button's own (`CPearl`, `taskbar.dll`); the highlight follows `PublishLauncherVisibilityState` from the **view** events. Our calls go through the same view, so the highlight follows; it lags by the retract while the shipped hold runs. | E-own-5: pixel means of (0,1704,110,96); open ≈ 62.9, closed ≈ 55.1, no residue after 1.2 s (E7 baseline). |
| Win+Tab | Closes Start with method 5 through `DismissStartAndCortana`, **not** through `OnHotkeyInvoke`; our decision hook is not involved, the shipped close hook holds it as today. | E-own-6: open menu, Win+Tab → `hide reason=5`, Task View opens; with the menu closed, `dismiss 5 state=0` and no `Hide`. |
| Win+Shift+S | The measured close is `method=1` and looks like a bare Win press at the decision. With the menu **open** we may refuse the close and open instead (wrong: the snip overlay should open). | E-own-2: open and closed, the overlay must open and Start must match the modifier rule; if it fails, drop the `OnHotkeyInvoke` takeover (section 1.1). |
| Win+L | `DismissSynchronouslyWithoutTransition` (method 7) → `HideStartAndCortana(7)`; the shipped hook never holds it. Our decision hook is not on that path. | E-own-7: Win+L locks, the menu is closed on unlock, no dead key. Screen locks — run last. |
| Other callers (Cortana, search, docked start controller, tray) | They call `Show`/`Hide`/`Dismiss` directly, bypassing our decision; the shipped close hook still governs their closes, and each transition ends in a Shown/Hidden event that resyncs `g_open`. | E-own-6: open the menu, invoke search/Cortana from the shell, check `own state resync` does not repeat every press. |
| Multi-monitor | `ShowStartView` positions on the monitor the shell chooses; our boolean is global, not per-monitor. A second monitor with its own Start window is not modelled. | E-own-8: move the menu to the second monitor (Win+P / taskbar on both), press Win twice; the menu must open and close where the shell puts it. |
| Lock screen | Same method 7 path; the session lock also destroys the window (shown/hidden events may not fire). `g_open` heals at the next decision. | E-own-7. |
| Our boolean vs the shell | The per-decision read heals any drift; the only window where it matters is between the press and the read, which is one call. | Every E logs `own state resync` when it happens; a clean run has none. |

## 5. Pseudocode

```
// Hook: OnHotkeyInvoke(self, flags) -> HRESULT
OnWinPress(self, flags):
    launcher = self + 0x18
    if AnyModifierDown():                  // Shift/Ctrl/Alt: not ours (E-own-2)
        return OnHotkeyInvoke_Original(self, flags)
    SyncOpen(launcher)                     // section 3, logs a resync
    if g_open:
        if InCloseTransient(launcher):     // state != 5 and visible (9/11/12..17)
            g_pendingOpen = true
            hr = ShowStartView(launcher, 1, 0)
            log "own reopen during close state=%u hr=%08X"
            if FAILED(hr): g_pendingOpen = false      // nothing to serve
            return S_OK
        g_open = false
        hr = Dismiss(launcher, 1)
        log "own close method=1 hr=%08X"
        if FAILED(hr): return OnHotkeyInvoke_Original(self, flags)
        return S_OK
    hr = ShowStartView(launcher, 1, 0)
    log "own open hr=%08X"
    if FAILED(hr):
        g_open = false
        return OnHotkeyInvoke_Original(self, flags)   // let the shell try
    g_open = true
    return S_OK

// Hook: OnStartButtonPressed(self, showMethod, dismissMethod) -> HRESULT
OnStartButton(self, showMethod, dismissMethod):
    launcher = self
    SyncOpen(launcher)
    if g_open:
        if InCloseTransient(launcher):
            g_pendingOpen = true
            ShowStartView(launcher, showMethod, 0)
            return S_OK
        g_open = false
        hr = Dismiss(launcher, dismissMethod)
        if FAILED(hr): return OnStartButtonPressed_Original(self, showMethod, dismissMethod)
        return S_OK
    hr = ShowStartView(launcher, showMethod, 0)
    if FAILED(hr): return OnStartButtonPressed_Original(self, showMethod, dismissMethod)
    g_open = true
    return S_OK

// Hook: OnStartShown(state)                (state's vtable entry, 0x141DF0)
    g_open = true
    g_pendingOpen = false

// Hook: OnStartHidden(state)               (0x65E70)
    g_open = false
    if g_pendingOpen:
        g_pendingOpen = false
        if exists launcher: ShowStartView(launcher, 1, 0)
        log "own reopen served after close"

// every decision
SyncOpen(launcher):
    state = state_of(launcher)
    actual = state ? IsStartWindowVisible(state) : false
    if actual != g_open: log "own state resync cached=%d shell=%d"; g_open = actual
```

`InCloseTransient` reads `[state+0x28]` only to distinguish "settled open (5)"
from "closing", and only when `IsStartVisible` semantics are needed; the
authoritative gate is `IsStartWindowVisible`. If the state cannot be read, treat
it as not in transition and let `Dismiss` decide.

## 6. Interface split and protocol

- **Bridge (explorer):** owns `g_open`, both decision hooks, the Show/Hidden
  mirrors, the pending-open latch, and the shipped close hook (execution). It is
  stateless across restarts: `g_open` is re-read at the next decision.
- **Start mod (host):** unchanged ownership of `Progress`, the settle and the
  reveal. It already reacts to a shell open during a close (`win open during
  close`, settles from the live position) and to a shell close (`win close
  observed`), so the takeover's opens and closes need **no new protocol**.
- **Do not put "is open" on the v2 channel.** The shell is the only writer of
  that fact, the bridge sits in the same process as the shell, and a published
  copy would need its own lease and a divergence rule for no gain. The channel
  keeps only what it has: the open generation, the close record, the retract
  request/answer (duration / none / cancel) and the reveal claim.
- **Crash/restart:** bridge restart → the next decision reads the truth; Start
  mod restart → closes fall back to the shell's immediate close (no answer),
  opens are ours and still work; both processes up → unchanged.

## 7. Acceptance experiments (owner approval; not run)

Each line: start state → action → expected log / state / pixels.

- **E-own-1 (owner's case) — open during a close.** Menu open → Win, then Win
  again 100 ms later. Expected: first `own close`, `hide delayed ...`; second
  `own reopen during close state=9..11`, then the window presents and
  `win open during close from=...` (or, if the immediate call is refused,
  `own reopen served after close` within 1 s and a normal `win open dur=409`).
  End state: menu open, `IsStartWindowVisible` true, no `win open unhealed`.
  This is the experiment that decides whether the immediate path is usable.
- **E-own-2 — combinations (the Win-hook gate).** Menu open and closed →
  Win+Shift+S; then Win+E, Win+D, Win+Tab. Expected: each keeps its own effect,
  Start does not open at the wrong time, and the decision hook logs a pass-through
  for the modifier case. Failure means dropping the `OnHotkeyInvoke` hook.
- **E-own-3 — plain toggle.** Closed → Win → `own open`, `win open dur=409`,
  state 5; Win → `own close`, retract, state 0. Repeat 5×; no `resync` lines.
- **E-own-4 — Start button.** Same as E-own-3 with a click at (55,1752); expects
  `own close method=<dismiss>` (7 or 6).
- **E-own-5 — button visual.** As E7: open ≈ 62.9, closed ≈ 55.1, no residue
  after a refused close and after a close served from the latch.
- **E-own-6 — foreign transitions.** Open the menu, click the desktop (method 7
  close), invoke search from the shell, open with a gesture: after each, one
  press must behave per the truth, and `own state resync` appears at most once.
- **E-own-7 — lock.** Menu closed → Win+L → unlock. Expected: menu closed, next
  Win opens; no `hide reason=7` hold and no dead key.
- **E-own-8 — second monitor.** Repeat E-own-3 with the taskbar/menu on the
  secondary monitor; the menu must open and close on that monitor.

## 8. Not feasible, or high risk

- **Distinguishing a combination at `OnHotkeyInvoke`.** The function carries no
  key data; the only handle is `GetAsyncKeyState` at decision time, and the shell
  may deliver the press after the modifier was released. If E-own-2 fails, the
  Win hook cannot be used and the takeover degenerates to "decide at execution"
  (`Dismiss`/`ShowStartView` hooks), which cannot change open-vs-close.
- **Making the shell's already committed hide disappear.** We can present the
  window again (`ShowStartView`), but we cannot un-commit the hide; the worst
  case is the two events racing and the state landing on 0 while the window is
  shown. The latch-and-retry repair in 2.3 is the fallback; a shell that keeps
  the state at 0 with a shown window leaves `win open unhealed` to the Start mod.
- **Writing `InternalState` ourselves.** Never: it is interface-relative, it has
  no public writer, and every value we could write fights the events that follow.
- **A second monitor's independent Start state.** `g_open` is one boolean; a
  per-monitor Start is not modelled by the shell's own launcher object either.
- **Build drift.** Each new hook is an RVA in `twinui.pcshell.dll` 10.0.26100.9949.
  Install each one only after: the module version is exactly that build; the
  target lies in an executable section (DOS/NT headers, section table,
  `SizeOfImage`); the 20 prologue bytes match; and the body landmark is present
  within the first 0x400 bytes (`[self+0x178]` + `vtable+0x68` for the Win hook;
  `[self+0x160]` + `vtable+0x68` for the button; `[launcher+0x160]` for both
  calls). A failed check logs its own line, `own hook skipped <name> build=...`
  or `own hook skipped <name> prologue`, installs only that hook out, and the
  bridge keeps the v0.5.0 behaviour: the shell decides, the close hook only
  arbitrates. Never fall back to "install anyway".
