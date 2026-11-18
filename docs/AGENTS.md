# docs/

> L2 | Parent: ../AGENTS.md

The project's technical memory, moved out of the user-facing README. English only; the user guide is bilingual at ../README.md and ../README.zh-CN.md.

README.md: the index — topic map, the verified-environment block, the shipping-mod roster.
input.md: why Raw HID — the falsified data-source table, the report decode (report IDs, TipSwitch/InRange, contact identity, liveness vs position vs evidence clocks, the coordinate-only fallback), release detection and its timers.
gesture.md: the Progress model and expression, the dismiss and reveal state machines, the taskbar band and the adoption of a late open, the commit thresholds (20%/10%, 0.45 DIP/ms), the close ownership transition.
animation.md: the damped-spring settle — underdamped/overdamped/critical forms, T = clamp(remaining / 2.2 DIP/ms, 160, 520), slowest-root omega, end-value normalisation, TakeProgressNow ownership, the 1.72/0.9 styles, the start-speed freshness/span checks, the window replay at a reveal takeover and the away-from-target clamp, the seam factors, the native flyout measurement and fit.
geometry.md: the GestureGeometry snapshot and SRWLOCK discipline, panel-trail measurement and band, the taskbar axis probe (appbar/tray rect/work-area inset, auto-hide), the id+epoch measurement request under g_measureMutex, the six-region scroll survey and its three states, the work-area broadcast rules, the measured layouts.
ipc.md: the v2 channel (section, events, command mutex, seqlocks, claim/ready 550 ms budget, attach watermarks), the open generation, the plan/tapped/cancelled close record, the retract request/answer, the bridge Hide policy and unload drain, the gesture-state publication.
lifecycle.md: the image pin, the instance guard, counted entries (ModuleEntryLease/PostCountedUiTurn), the unload order and residuals, re-initialisation resets, the bridge's side, the Windhawk host's unload boundary.
testing.md: the tools in ../tools and the measurements in ../artifacts.
limits.md: known limits and risks; the hardware checks still owed (one physical finger end to end, two fingers with both lift orders, a pen by hand, a right-side taskbar by hand).

[PROTOCOL]: Update this header on change, then check AGENTS.md
