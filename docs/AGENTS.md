# docs/
> L2 | Parent: ../AGENTS.md

The project's memory: what was measured, what was falsified, and why each
decision was taken. The user-facing quick start is `../README.md` /
`../README.zh-CN.md`.

architecture.md: M3.3, the shipping design. The launcher read (`s_pLauncher`
outer object, `+0x18` IXamlLauncher, `[+0x160]` state, readiness `> 0x10000`
checked again at the post-call resync),
the executor thread (the `OnHotkeyInvoke` thread, not the taskbar thread), the
window lifetime (worker joined, the one window destroyed on its owner thread,
the fixed class unregistered under `&__ImageBase`), one
decision/one executor per trigger, the controller-direction toggle rule (closing
published at the close's start), the close cancel/recovery for a press during a
retract (the ticket only for a published command, bounded by its deadline), the
bounded press latch for a hide in flight, the click-away guards of an
opening window, the qualified pull-reopen at a close's landing, the command-expiry rule
(one `>=` comparison, no lease on the call), the executed/confirmed outcome split, the
auto-hidden timing/interruption chains (the bottom docked numbers are named as
historical; the live left docked layout is a separate set), the
accepted cloak cap, the 216-byte channel, the intent-consumer readiness/interception
capability precondition, and the deleted M2 mechanisms.

re/: reverse-engineering records, read-only analysis of this build's shell, kept
because every RVA and prologue check in the mods comes from here. close-decisions.md
(the launcher state machine, call chains and the refutable decision point),
own-state-spec.md (the decision-entry contract), onset-args.md (why the open
presentation cannot be parametrised), own-execution.md (launcher singleton,
executor thread affinity, the cloak ticket). Each carries the evidence it was
derived from; a Windows update invalidates them and the mods' own probes are what
notices.

input.md: why Raw Input; the report decode (report IDs, `TipSwitch`/`InRange`,
contact identity and its evidence clock, the coordinate-only fallback), the
liveness and idle rules, the release deadlines, and what wakes the input thread
(events, the one one-shot deadline, the cloak event and its fallback poll, the
peer heartbeat) with the measured idle cost.

gesture.md: the `Progress` model, the dismiss/reveal sessions, the band admission
and its 24 DIP qualified open claim (a tap requests nothing), the reveal entry
and the 2.2 DIP/ms chase (the live takeover of an interrupted retract continues
from the panel's position), the one close path (`TakeProgressNow` → mirrored
settle → scheduled `exec dismiss`), the qualified pull-cancel of any close whose
dismiss is still revocable, the qualified pull-reopen on a closing panel,
the gates and settle ownership.

animation.md: the damped spring (ζ 1.72 / 0.9), the travel-planned duration, the
start-speed seam, the mirrored close, the coast, and the measured lines.

geometry.md: the `GestureGeometry` snapshot and its lock, the panel-trail
measurement and band, the taskbar axis probe, the deferred measurement, the
scroll survey, and the measured layouts.

ipc.md: the merged mod's channel — names, 216-byte layout, seqlocks, open
generation, intent/ack, executor command/ack (admission vocabulary, expiry and
the dismiss confirmation), controller direction, peer liveness/identity (epoch,
tick, state, the 600 ms detection budget), init handshake.

lifecycle.md: quiescing both roles before the image is unmapped — the Start
join/UI-teardown/entry-drain order, the bridge worker join/window destruction/
class unregistration order, the no-safe-success rule, and the host's unload
boundary.

testing.md: the probes/injectors and the captured measurements in `artifacts/`;
the smoke's `DiagnosticTrace` precondition and the tap tests.

limits.md: the measured limits — the shell cap, the launcher readiness, the
launcher thread, the 24 DIP band threshold with the native-gesture trade-off,
the touch-show idle suppression, the cold entry, the taskbar auto-hide state,
the engine's symbol cache layout and four-hour error cache, and the hardware
still owed.

README.md: the map and the one shipping mod.

store.md: how the mod reaches `ramensoftware/windhawk-mods` — the one-file-per-PR
rule that forces `tools/make_store_mods.ps1` to inline the shared headers, what
the `store` and `local` profiles each change (no `-I` in either; the store also
drops `-Wall`/`-Wextra` and adds `@github`/`@license`/`@homepage`), the two
constraints the collection's scripts impose on the source (a hook name must be a
literal in its entry; the array variable must name a module the file-name index
knows, or carry a module comment), the local install flow that consumes `local`,
why it is one PR, and the five checks to run before opening it.

## Laws

Every claim names its probe or its log line. A mechanism the code no longer
contains does not get a page; the deletion list lives in architecture.md.

[PROTOCOL]: Update this header on change, then check AGENTS.md
