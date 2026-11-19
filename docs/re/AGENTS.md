# re/

> L2 | Parent: ../AGENTS.md

Reverse-engineering notes. Static analysis of shipped Microsoft binaries, kept
out of the behavioural docs because every claim here carries an RVA and a build.

close-decisions.md: who decides to close the Start menu, for the three triggers
(Windows key, taskbar Start button, click outside), and whether a close can be
refused without desynchronising the shell's state. Maps `XamlLauncher` and
`XamlLauncherState` in `twinui.pcshell.dll` 10.0.26100.9949, the single
`InternalState` dword and its two-phase close, the `DismissStartAndCortana` ->
`HideStartAndCortana` -> `CStartExperienceManager::Hide` call chain, the visible
transient (9, then 11) that makes toggles no-ops during a retract, the
`ShowStartView` path with no state gate, the identification bytes for each
candidate hook, the two design options — refuse the close at the decision (A) or
own the close and execute through the shell's own state entry (B), with B
recommended — and the measured E1–E7 run of 2026-11-19: the click-away close
bypasses `DismissStartAndCortana` and enters `HideStartAndCortana(7)` directly,
the refusal point that covers all three triggers is therefore
`HideStartAndCortana`, and B's 400 ms hold keeps `InternalState` at 5 but does
not stop the shell swallowing a press that lands inside the committed hide. It
states its own limits: the delegate's event source is unnamed, and Win+L was left
unrun because it locks the session.

own-state-spec.md: the takeover contract for `feat/own-start-state` — the two
decision hooks (`OnHotkeyInvoke` with the launcher at `self+0x18`,
`OnStartButtonPressed` with the launcher at `self`), the exact call contracts for
`Dismiss` and `ShowStartView`, the open-during-close two-step (immediate try plus
a latch served at `OnStartHidden`), the cloak truth via `IsStartWindowVisible`
and its per-decision resync, the consistency risk table (taskbar highlight,
Win+Tab, Win+Shift+S's method-1 close, Win+L, foreign callers, multi-monitor,
lock), the pseudocode, why "is open" stays out of the v2 channel, the acceptance
experiments (E-own-1 is the owner's open-during-close case), and the build-drift
`own hook skipped ...` rules.

onset-args.md: whether
`StartExperienceAnimationRequestedEventArgs` can make the presentation
animation-free (it cannot): the six read-only getters and their offsets/widths,
the 32-byte `START_GLOBAL_ANIMATION_DATA` WNF payload behind them, the args being
in-proc in the host while the presentation is a window-level DWM storyboard on
the shell side (`IDWMStoryboardListener`, `SetAnimationData`), why no field
controls the presentation, why `Progress` can only move content, and the
experiments that would falsify each claim.

own-execution.md: the three prerequisites for executing open/close from the mod
instead of letting the shell do it — `XamlLauncher`'s constructor stores `this`
into the singleton `twinui+0x948E80` and nothing clears it (the activation
factory has no caller inside the image, so the shell builds it at logon), yet a
captured launcher is not callable until `[launcher+0x160]` holds the
`IXamlLauncherState` (`Dismiss` / `IsVisible` return `0x80070490` on zero, and the
constructor leaves the sentinel `0x100` there); why neither entry point carries a
thread test and the safe shape is a posted message to a hidden window on the
shell UI thread, confirmed with `IsVisible`; and the cloak, which is DWM attribute
13 written by `CPrivilegedPresentationOperations::SetShellCloak` under an
`EnableIAMAccess` ticket (`AcquireIAMKey`), not a shell-private path — ownership is
the right the attribute needs, so the `E_ACCESSDENIED` is a missing ticket, and
explorer can take the same ticket. It lists its own gaps (the activation call
site, the `+0x160` writer, the `[experience+0x50]` wrapper, DWM's server-side
rule) and the six experiments that close them.

[PROTOCOL]: Update this header on change, then check AGENTS.md
