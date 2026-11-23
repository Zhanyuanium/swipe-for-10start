# swipe-for-10start - touch swipe for ExplorerPatcher's legacy Win10 Start

C++ / Windhawk 2.0 mods + PowerShell 7 test tools
<directory>
mods/ - Windhawk mod sources (4 sources + 1 shared header: swipe-for-10start, touch-gesture-probe, msgprobe, inputprobe; swipe_ipc_shared.h)
tools/ - probes, injectors, animation harnesses, the panel-edge tracker, the regression smoke, the store packager and the local installer (18 scripts + AGENTS.md)
artifacts/ - captured measurements (20 CSVs + AGENTS.md; `inputprobe.dll` is an untracked local build output of `mods/inputprobe.wh.cpp`)
docs/ - technical documentation (architecture, input, gesture, animation, geometry, ipc, lifecycle, testing, development, limits, store, README + AGENTS.md; `docs/re/` is evidence, not documentation)
</directory>
<config>
README.md - user-facing quick start (English; README.zh-CN.md is the Chinese page)
CHANGELOG.md - release history: one row per tag with the channel size it shipped (CHANGELOG.zh-CN.md is the Chinese page)
.gitignore - Windhawk/MSVC build outputs and local logs
</config>

Laws: measure before designing · one data source per claim · docs and code move together

## What ships (0.8.5, one mod)

One mod, two processes. `mods/swipe-for-10start.wh.cpp` is installed once and
loaded into `StartMenuExperienceHost.exe` (namespace `start`) and into
`explorer.exe` (namespace `bridge`). `Wh_ModInit` picks one role table from the
host executable name before either half runs; the other half's hooks, threads
and handles are never created there. The bridge elects itself: only the Explorer
that owns `Shell_TrayWnd` opens the channel, publishes `execPid` and starts the
worker; any other `explorer.exe` stays inert.

The controller decides; the executor performs. The controller (`start`) owns the
Raw Input gesture, the panel's `Progress`, every settle, the close record and the
gates. The executor (`bridge`) owns the launcher call and the non-touch triggers:
the Win key (`XamlLauncher::OnHotkeyInvoke`), the Start button (`ShowStartView`
11, `Dismiss` 6) and the click-away (`HideStartAndCortana` 7).

- Those triggers reach one toggle decision and publish one
  `intent(seq, kind, trigger, method, flags, open)`; the Start side applies it —
  `exec show`, or the one shell-close settle and then `exec dismiss` — and acks.
- Method 7 is click-away evidence, mapped to the executor's 6. A real left
  button down reverses a close in flight; a z-band change with no button
  (Alt+Tab, a foreground switch, a taskbar activation) closes. External
  takeovers (Win+Tab, lock, display change, a forwarded modified Win press)
  publish `intent=external` and are never held.

An enable or update attaches to a running host: `Wh_ModAfterInit` posts a
registered message, a targeted `WH_GETMESSAGE` hook runs on its UI thread, and
`Window::Current().Content()` takes the same attach path as `PrepareRootElement`.

Interception needs three things, and no one implies another: a **live peer**
(`peerState=alive`, `peerTick` younger than 600 ms), a **usable executor**
(`execCapable` with a live `execPid` handle: worker, window on the recorded
launcher thread, verified layout, primitives) and a **proven launcher thread** (a
launcher entry proved it, and a close finds the callable `Dismiss`). A Hide never
adopts; `ShowStartView`/`Dismiss` never adopt and never prove.

Launcher symbols are resolved once per module per install, on the executor
worker, never in a hook. `HookSymbols` blocks on Windhawk's PDB fetch (measured
up to 571 s); a false result is terminal (`attempts=1`) and every trigger stays
native. The fixed `twinui.pcshell.dll` version is checked before that attempt,
so an unsupported build never downloads a PDB.

A press is recovered, not guaranteed. The bridge reads the controller's
published direction: `closing` makes the press an open, `open` makes it a close.
A close still settling publishes open at once; a hide in flight latches the
press; presses wait in a four-entry FIFO, one per ack, and an open arms the
generation gate. A press the bridge already swallowed is dropped, not passed on.
The bounds: 1000 ms ack, 1500 ms unpublished (shell's original), 2000 ms claim
deadline, 2500 ms absolute, 900 ms generation gate. A claim past its deadline is
spent without replay, so a frozen host cannot block the next press.

The open runs from the band contact only after 24 DIP of inward travel; a tap in
the band stays a native press, and the shell's own edge gesture (from about
20 DIP) is the fallback. An intent close is `TakeProgressNow()` + the shell-close
settle + `exec dismiss` at +30 ms; a gesture close asks for the same dismiss at
its settle's end. The close record — serial, acceptance tick, the menu, a phase,
the 800 ms repair bound — answers "is a close in flight?" by its phase, never by
an age. The close is judged by behaviour: the hidden event, the executor's
`confirmed` at 300 ms, and the next open working; a failed cloak read is
unknown, never hidden.

## Channel

`mods/swipe_ipc_shared.h`: one session-local section (`SwipeIpcState`, 240 bytes)
and five named events (`Intent/IntentAck/Exec/ExecAck/Capability`; six objects),
every record a seqlock with its own counter and an `offsetof` assert, plus the
single-word claims `controllerDirection`, `controllerTouch`, `intentTaken` and
the bridge's `execCapable`. Names are `Local\SwipeFor10Start_SingleMod6_*`: the
namespace is the ABI identity, and there is no product version word.

- `peerTick` 204 / `peerState` 208 / `peerEpoch` 212; `peerPid` 216 /
  `peerUiThread` 220; `intentTaken` 224; `execCapable` 232; `execPid` 236.
- `SwipeIpcOpen` may wait the whole 550 ms init budget; generation 0 and an
  unknown `initState` are refused. Each role opens the channel at its `RoleInit`,
  never in a hook. `tools/ipc.ps1 -State` reads the 240 bytes read-only.

## Machine

Windows 11 26300.10075, `twinui.pcshell.dll` fixed 10.0.26100.10068 (display
string 9949), ExplorerPatcher legacy Start (`Start_ShowClassicMode=1`),
2880x1800 physical at 200 % (1440x900 DIP).

**The bar is not pinned to an edge.** The controller derives the axis, band,
panel rectangle, `Distance` and scale from the live geometry on every read, and
the tools read the same numbers from the mod's own lines (`reveal panelTrail`,
`gesture geometry`, the `dismiss` bar thickness). Measured on this machine, each
on the layout it names:

| layout | axis | work | band | panel rect | trail | Distance | measured |
|---|---|---|---|---|---|---|---|
| left, docked | 0 | `48,0,1440,900` | `0..96` | `60,151..1053,726` | 1053 | 1392 | 2026-11-21 |
| bottom, docked | 3 | `0,0,1440,852` | `804..900` | `236,265..1229,840` | 265 | 852 | 2026-11-20 |
| bottom, auto-hidden | 3 | `0,0,1440,900` | `804..900` | `236,279..1229,840` | 279 | 900 | 2026-11-22 |

Top (`axis=1`) and right (`axis=2`) use the same `TrailEdge`/`AxisDirection`
mapping; all four taskbar sides are checked by hand, and the table lists the
sessions with captured numbers.

## Rules

- One data source per claim: the cloak decides the shell state, the close record
  decides whether a close is in flight, the published direction decides the
  toggle, the geometry snapshot decides the mapping.
- No period. One high-resolution one-shot for the soonest deadline, disarmed when
  served; the open/close state is a WinEvent, not a poll, with a bounded 100 ms
  fallback while a transition is outstanding; the only idle wake is the 100 ms
  `peerTick` heartbeat while the peer is advertised. Three state-owned exceptions
  stop with their state: the open-generation gate (20 ms, at most 900 ms), a
  standing slot or barrier (100 ms reserve poll), a channel without a
  `Capability` event (100 ms read). A periodic tick or state poll is a
  regression.
- A mapped channel is not a live peer. Interception needs `peerState=alive` and
  a `peerTick` younger than 600 ms; otherwise the shell's own path runs.
  Availability is published only by the ready consumer, only while
  `execCapable=1` with the bridge process alive, and withdrawn before it is stopped or when either drops.
- A live peer is not a usable executor. The gate also proves the recorded
  launcher thread, a launcher entry that proved it, a window this activation
  created on it, a running worker, the verified layout and the primitive.
- One mechanism, one log shape. A decision line and an executor line per
  trigger; no per-trigger branch in either mod.
- A wrong-thread launcher call is not a clean error. Never call from a hook
  frame; post to the executor window, and never create it off the recorded
  launcher thread. A `Show` on an unproven thread only proposes it.
- No cross-process call from a XAML handler. The taskbar's appbar record is
  cached on the input thread and refreshed on `WM_SETTINGCHANGE` /
  `WM_DISPLAYCHANGE`; `ProbeTaskbar` reads the cache.
- The shell owns every visual transition. The mods read and request, never write
  the launcher's state.
- The trace is a setting. Every decision line calls `Wh_Log`; the file is
  written only while `DiagnosticTrace` is on, one file per role, capped at
  20,000 lines. The smoke requires it and prints the command that enables it.
- The mod image is not pinned. Unloading joins the two owned threads, sends the UI
  teardown to the module's own hidden window on the XAML UI thread, then requires
  the UI-turn count, the registration count and one acknowledged empty turn. A
  frame on the stack defers the teardown until it returns; a proof that cannot be
  obtained blocks the unload.
- Docs and code move together. Update this header, the L2 map and the L3 header
  of the file you changed.

[PROTOCOL]: Update this header on change, then check AGENTS.md
