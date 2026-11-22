# docs/
> L2 | Parent: ../AGENTS.md

The project's memory: what was measured, what was falsified, and why each
decision was taken. The user-facing quick start is `../README.md` /
`../README.zh-CN.md`.

architecture.md: M3.4, the shipping design. The launcher read (`s_pLauncher`
outer object, `+0x18` IXamlLauncher, `[+0x160]` state, readiness `> 0x10000`
checked again at the post-call resync),
the executor thread (the `OnHotkeyInvoke` thread, not the taskbar thread), the
window lifetime (worker joined, the one window destroyed on its owner thread,
the fixed class unregistered under `&__ImageBase`), one
decision/one executor per trigger, the controller-direction toggle rule (closing
published at the close's start), the close cancel/recovery for an explicit open
request during a retract (the ticket only for a published command, bounded by its
deadline), the bounded press latch for a hide in flight, the click-away guards of an
opening window, the grab's one transfer attempt (a band pull at or below the entry
takes the press's cancel body and follows from the entry) and the explicit
request's obligation a
landing still serves, the command-expiry rule
(one `>=` comparison, no lease on the call), the executed/confirmed outcome split, the
auto-hidden timing/interruption chains (the bottom docked numbers are named as
historical; the live left docked layout is a separate set), the
accepted cloak cap, the 232-byte channel, the intent-consumer readiness/interception
capability precondition (including the executor capability word), and the deleted
M2 mechanisms.

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
and its 24 DIP qualified open claim (a tap requests nothing), the down's author
(only a band down or a grab may be adopted or promoted), the reveal entry
and the 2.2 DIP/ms chase (the live takeover of an interrupted retract continues
from the panel's position), the one close path (`TakeProgressNow` → shell-close
settle → scheduled `exec dismiss`), the close record (serial, acceptance tick, the
menu, a phase, the 600 ms repair bound) and its endings including the logged
repair, the grab's down hit-test (the trail along the axis **and** the panel
rect's cross range) and its one
transfer attempt (a taken transfer follows from the live position, a band down
whose retract is already past the entry shares the press's cancel body but follows
from the entry, a band down whose close has already ended asks for the open like
any pull on a closed menu, any
refusal consumes the contact, the landing retires no unspent grant and adopts no
finger, and silence never releases a consumed contact), the gates and settle
ownership.

animation.md: the damped spring (ζ 1.72 / 0.9), the travel-planned duration, the
start-speed seam, the shell-close settle, the coast, and the measured lines.

geometry.md: the `GestureGeometry` snapshot and its lock, the panel-trail
measurement and band, the axial and cross-axis halves of a point's hit test, the
taskbar axis probe, the deferred measurement, the
scroll survey, and the measured layouts.

ipc.md: the merged mod's channel — names, 232-byte layout, seqlocks, open
generation, intent/ack and the intent take, executor command/ack (admission
vocabulary, expiry and
the dismiss confirmation), controller direction, peer liveness/identity (epoch,
tick, state, the 600 ms detection budget), the bridge's `execCapable` promise and
its `Capability` event, init handshake.

lifecycle.md: quiescing both roles before the image is unmapped — the Start
join/UI-teardown/entry-drain order, the bridge worker join/window destruction/
class unregistration order, the no-safe-success rule, and the host's unload
boundary.

development.md: the generated local build, the elevated install, the store
build with `source meta --strict`, and the repository-side compile check.

testing.md: the build gate (`make_store_mods.ps1` and its README block), the
probes/injectors and the captured measurements in `artifacts/`;
the smoke's `DiagnosticTrace` precondition and the tap tests.

limits.md: the measured limits — the shell cap, the launcher readiness, the
launcher thread, the 24 DIP band threshold with the native-gesture trade-off,
the close's 600 ms repair bound (and why a late hide costs latency, not the
pull), the touch-show idle suppression, the cold entry, the taskbar auto-hide
state,
the engine's symbol cache layout and four-hour error cache, and the hardware
still owed.

README.md: the map and the one shipping mod.

store.md: how the mod reaches `ramensoftware/windhawk-mods` — the one-file-per-PR
rule that forces `tools/make_store_mods.ps1` to inline the shared headers and
replace the readme body with the two user guides, what
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
