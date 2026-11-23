# Testing — build gate, smoke, probes, evidence

> Part of [docs/README.md](README.md). The script roster and its limits are in
> [../tools/AGENTS.md](../tools/AGENTS.md); the captures are listed in
> [../artifacts/AGENTS.md](../artifacts/AGENTS.md).

## Build gate

`tools/make_store_mods.ps1` produces the single-file source that
`tools/install_mods.ps1` installs (`-Profile local`) and the store submission
(`-Profile store`). Both profiles inline `swipe_ipc_shared.h` and replace the
`==WindhawkModReadme==` body with `README.md`, `---` and `README.zh-CN.md`; the
build fails if either guide or marker is missing. The store profile also runs
`windhawk-cli source meta --strict` and fails on any rejection.

## The one-pass smoke

`tools/smoke.ps1` drives the four triggers through their real paths and judges
each step by `start_menu.ps1`'s state plus the lines the two halves wrote in that
step:

| Row | Passes when |
|---|---|
| win open / close | `kind=open` + `exec show`; `kind=close` + `exec dismiss` |
| touch open | `reveal pull qualified` and the executor's ack for that show sequence |
| touch close | the producer line and the executed dismiss for the sequence it requested |
| click-away | `trigger=clickaway ... kind=close`, the method-6 dismiss and `win close observed` |
| three reversal rows | Win close + click, Start-button close + Win, gesture close + click: the reopen decision, the cancel that retired the close, `close ended ... reason=open-cancel`, no `win close observed`, and a cancellation `from=` below 1 |
| last step | no error-shaped line in either log (non-zero `hr=`, `timeout`, `refused`, a lost press or a dropped latch) |

The reversal rows read the final cloak only after that close's own dismiss
deadline (`executeAt`, or the gesture settle's end + 30 ms), so a command that was
not actually cancelled fails the row. The touch row names its producer
(`producer=hid` or `producer=injected-pointer`); an injected-pointer row is never
quoted as HID evidence.

The smoke requires `DiagnosticTrace`; after the first trigger it checks that
`renderer.log` or `bridge.log` was written since the run started, and otherwise
prints `windhawk-cli mod settings set local@swipe-for-10start DiagnosticTrace=true`
and exits non-zero (an earlier session's file is stale and rejected the same way).
It warms a cold executor with one native open and its close before the first row,
reads the layout from the mod's own `reveal panelTrail`, `gesture geometry` and
`dismiss` lines after the run mark, and prints `skipped: ...` for `-SkipClickAway`
rows. It keeps the monitor scale fractional, because rounding puts every injected
point off the band or off the panel.

## Manual checks the smoke cannot express

- **A tap must not open.** Tap the taskbar background, the clock, the tray's empty
  area and the Start button with the menu closed. The first three log
  `reveal admit ... ok=1` with no `reveal pull qualified`, no `reveal show request`
  and no `exec show`, and the menu stays cloaked; the Start button logs one shell
  open (`exec show ... method=11`) and no band claim.
- **The threshold.** A 23 DIP pull requests nothing from this mod (the shell's own
  gesture may open it); a 25 DIP pull requests one show.
- **A hold through a close.** A contact held in the band never reaches 24 DIP: it
  is admitted as a grab, and the landing logs `win close observed muted=0` with no
  `exec show`. Past 24 DIP it opens, or is refused and consumed.
- **A down on the panel surface** that reaches 24 DIP at or below the entry logs
  `grab=refused reason=offsurface` and opens nothing.

## Checks that need a harness or an external probe

Each must show its named line; all of them are outside the tree by design.

- **Unload quiescence:** enable, disable and re-enable in the same PIDs; both
  hosts keep running and the old DLL leaves both module lists.
- **Activation identity:** a disabled Start half makes the next press log
  `reason=no-peer`; the next `ipc peer alive` carries a new `epoch` and its
  `touch=`/`pen=` words.
- **Detection budget:** `own pass ... reason=no-peer` at the first press, and
  `own press latch dropped reason=peer-gone` when a latched press outlives its
  peer.
- **Capability:** a click-away close decides only after the executor exists;
  with the worker refused it passes with `no-executor`/`no-window`/`no-primitive`.
- **Expiry and confirmation:** a dismiss at or past `executeAt + 400 ms` logs
  `expired` and no `hr=`; every successful close is followed by
  `exec dismiss confirmed` (or `unconfirmed`/`unknown`) with the real `after=`
  time. A superseded dismiss produces no confirmation.
- **Latch count:** two presses during a hide are latched, the second appended
  (`owed=2`), the serve publishes the first open and the remainder in order.
- **Landing order:** Win+Tab right after a Win press logs
  `intent reconcile ... (before landing recovery)` and no `intent open recovered`.
- **Readiness floor:** a post-call resync of a null or `0x100` state logs
  `own state resync skipped` and leaves the cached direction.
- **Ticket identity:** a local-only cancel logs `ticket=0` and a later hide of the
  same menu produces no `intent open recovered`; a published command logs
  `ticket=1 ticket_seq=... ticket_expire=...`.
- **Publisher epoch:** a host restart supersedes the old dismiss instead of calling
  the launcher, and the attach logs `exec cancel request target=0 reason=attach`.
- **Claim windows:** a pause before the claim replays once (`reason=revoked`); a
  pause after it logs `decision=consumed awaiting=ack` and serves nothing until
  the ack, a changed epoch, or the peer's state going away.
- **Tray ancestry:** clicks on the tray and clock during a close read
  `kind=close ... ptr=0` and leave the menu closed.
- **Replay bound:** a Start held 4.3 s between its sequence read and its claim ends
  with `own press dropped ... reason=stale`, no `revoked-before-copy`, menu closed.
- **Touch tap:** a virtual-digitizer tap on the desktop during a Win close logs
  `kind=close ... ptr=0`, not a reopen. A real finger or pen tap is still owed.

## Producers

A press that dismisses the open panel reaches the shell as a pointer, so the
injected drag is served by the shell's click-away while a real finger is served by
the mod's own HID dismiss. The two end in the same `exec dismiss`, so the producer
is the line before it: `close accepted ... kind=dismiss` plus
`close dismiss kind=...` for the digitizer, `own toggle decide
trigger=clickaway` plus the method-7 intent for the pointer. A four-trigger smoke
that only checks "closed and dismissed" cannot tell them apart.

## Trailing-edge evidence

`tools/panel_edge_track.ps1` reads the panel's trailing edge from pixels for runs
whose close must be judged by more than "the window hid". On the 2026-11-21 build
(left docked) the fully-open edge is 2106 px and every run measured
2105 px; the edge first leaves it 79–84 ms after the close tap, and the visible
series ends at 98–175 px (progress 0.279–0.306) because the window is cloaked
before the last 48 DIP reach the bar. Those samples are `edge=-1`, never progress
0.

| run | 2000 px at | last visible | hidden at |
|---|---|---|---|
| plain Win close | 261–274 ms | 98–127 px @ 564–567 | 573–575 ms |
| pull at 150 ms | 264–275 ms | 175 px @ 556–561 | 564–567 ms |

The pull rows are the pre-change baseline: the retract landmarks run within 14 ms
of the plain runs and no sample reverses, because that build could only cancel a
local gesture slot. The delayed reopen it left behind is gone: a grab the close
outruns is now refused and consumed, and the landing adopts no finger. The
interruption runs measured a retract floor of 1996 px (progress 0.962) at
`tc` 265–313 and a reversal at `tc` 463 for a pull, and a floor of 2045 px
(0.979) at `tc` 208 with a reversal at 363 for a Win press; both ended open.

`panel_edge_track.ps1` cannot read the bottom bar (`axis=3`), so the bottom-layout
boundary was read from the mod's own lines: a pull qualifying 150 ms after the tap
took at `progress=0.934`; one qualifying after the settle ended had nothing on
screen and the close landed.

## Artifacts

`artifacts/` holds the raw tables: `native-flyout-open.csv` and
`native-flyout-close.csv` (per-frame `t -> dy -> d`, ~9 ms frames, normalised to
the 578 DIP travel), `settle-trace.csv` (both styles of the same half-open hold
release), `cursor-follow.csv`, `start-window.csv`, `gesture-shared.csv`, and the
`panel-edge-*.csv` trailing-edge runs. Their capture clocks differ by tool and by
generation; `artifacts/AGENTS.md` names each file's bias, and a rerun does not
reproduce an older column.
