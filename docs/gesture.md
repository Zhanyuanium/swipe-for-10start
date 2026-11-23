# Gesture and state machines

> The executor's thread, launcher read and timing are in
> [architecture.md](architecture.md); this page is the Start side's own state.

## Sessions

One contact owns the panel (`g_hidSession.active` or `g_reveal.edgeCandidate`);
the other session is refused until it lifts.

- **dismiss** — started on the open panel. A drag follows the finger; a release
  past `CommitDistancePercent` of the travel (default 10, one value per axis)
  settles to 0. A press in a region that can still scroll is refused
  (`dismiss deferred to scroll`).
- **reveal** — started in the band while the menu is closed; the show is
  requested at 24 DIP of inward travel. A band contact measured but not yet
  active is a **candidate**: it activates at the live takeover or is carried in
  the late-release record.
- **grab** — lands while a close owns the panel; writes no `Progress` and gets
  one transfer attempt at 24 DIP.

## The reveal

- **Admission** — the first positioned report in the band with the window closed
  records the contact, the down point and the clocks, and nothing else. A tap is
  admitted and released without a claim. The down's **author**
  (`Band` or `Grab`) is the only thing that may later adopt or promote it, so a
  finger that landed on the desktop or inside the panel is never bound to a
  keyboard open.
- **Open claim** — one per contact, on the first report whose signed inward
  travel reaches **24 DIP**, marked even when refused so it is never retried. It
  starts the pull generation, drops the previous release record, cancels a
  close's armed schedules and publishes the open (`controllerDirection=1`,
  `controllerTouch`, the entry, `exec show`). Measured: 31 ms to the crossing on
  a ~1 DIP/ms pull, +15 ms to `ShowStartView`.
- **Entry** — the settle starts at `RevealEntryProgress`: **0.255** bottom
  auto-hidden (trail 265, Distance 852), **0.348** bottom docked, **0.000** cold.
- **Takeover and follow** — adopted only if still down, 24 DIP travelled and
  authored. A hidden panel is met at the entry, held 30 ms, then chased at
  **2.2 DIP/ms**, never past the mapped finger; once caught up, `behind=0.000`
  (a held 300 DIP finger measured 1.8 px away bottom auto-hidden, 2 px bottom
  docked; 350 DIP, 0.4 px). A panel a cancelled close left on screen is met
  where the transfer pinned it and keeps the finger-to-panel offset (`live=1`).
  Commit is on travel or session average, with the release speed from the last
  8–30 ms; a live takeover commits at or above the position it took over.
- **Late release** — a pull that lifts before the open keeps its travel, mapped
  position and end speed in one record for 300 ms; the open settles from it. A
  grab never writes a record.
- **Held idle** — while `controllerTouch`/`g_intentOpenActive` is set, or the
  close record owns the panel with a reveal candidate, the idle release is
  skipped: silence during the uncloak or the key/hide is not a lift. A lift
  report still ends the contact.

## The close

```
intent close -> TakeCloseOwnership (the record, accepted) -> BeginShellCloseSettle(TakeProgressNow())
             -> exec dismiss at settle + 30 ms -> bridge Dismiss -> hidden
```

A gesture close is the same pipeline with its own release settle and its dismiss
at that settle's end. Both accept the close **before** the settle's first frame.

### The close record

A close is an identity, not an age: one record, written and read only by the
input thread.

| field | meaning |
|---|---|
| `serial`, `acceptedTick` | monotonic id, and when ownership was taken |
| `openTick`, `openGeneration` | the menu it was accepted against (diagnostics) |
| `phase` | `Settling`, `AwaitingHide`, `Done` |
| `repairAt` | `acceptedTick + 800 ms` |

`CloseInFlight()` reads the phase; the tick fields are data. `CloseHideMayLand`
answers the second question (a published dismiss may still land). The record is
retired by the landing, a pull cancel, a press or band-pull cancel, an external
takeover, a shell open during the close, a terminal refusal/repair, and the
repair bound — and it retires only the record, because the dismiss command and
its generation are the landing's identity.

`TakeCloseOwnership` publishes `closing` before the settle's first frame,
consumes every contact on the glass and accepts the record. The settle is the
open spring in the closing direction, full travel (387 ms at 852 DIP, 409 at
900), from the live position; the dismiss is scheduled, never immediate. At
`repairAt` a close that never landed ends as a logged repair
(`reason=repair-deadline`), releasing the direction while the command and ticket
still name the hide that may land.

### A grab during the close

A down is a grab only in the band or on the surface the retract still shows: its
mapped progress at or below the panel's live one **and** the point inside the
panel rectangle's cross range (`PanelCrossContains`, the axis the gesture does not
slide along). Anything else is `reason=offsurface`, muted for its contact.

- At its 24 DIP report the grab spends its one opportunity
  (`CancelCloseForQualifiedPull`): both schedules revoked, the record ended, the
  open direction republished, the recovery ticket armed, the retract taken over
  from its live position (`grab=taken`, `live=1`).
- A refusal consumes the contact: the dismiss already inside the shell, a
  published dismiss at or past its deadline (`reason=dispatch`), no distance,
  nothing left to cancel, a retract at or below the entry, or a close that
  already ended (`reason=no-close`).
- A down **in the band** at that point is the press's request (`grab=open`): the
  same cancel body, no settle. The grant is spent and the follow starts at the
  entry; a window the retract already hid gets the ordinary show
  (`window=hidden`) and binds there.
- A landing retires no grant; a contact still on its way to its 24 DIP report
  keeps its open right.

### A press during the close

A close still settling publishes open at once: the local dismiss slot drops, a
pending command is cancelled, the record ends, and the panel settles back from
its live position (`intent open cancel from=...`).

- A dismiss already inside `Dismiss` cannot be recalled: the hide lands, and the
  landing reissues that open once (`intent open recovered entry=...`), after
  reconciling an `external` intent that arrived with it.
- Only a close out of the controller's reach latches the press (the shell is
  hiding, or our own `Dismiss` is live); the bridge serves the slot after the
  hidden event. An armed dismiss is revocable and is cancelled instead, and a
  local-only cancel arms no ticket.
- A close consumes every contact on the glass; a landing adopts no finger and
  serves no touch open of its own. A press the bridge swallowed and then lost
  (peer gone, epoch changed) is dropped, never passed on.

### A landing without a transition

When the 300 ms probe reads hidden and the cloak carried no hide, the same
landing finishes the close (`reason=confirmed-without-transition`). When the
cloak reads open or cannot answer, the command and its open generation are
retained (`exec dismiss retained`) and land once through the transition; a
command from a menu a later open replaced is stale, and a fresh open cancels a
still-queued dismiss. The transition is never synthesized.

## Gates

- **Vertical** — up to six deepest vertically scrollable regions; a press inside
  one that can still scroll is refused, elsewhere it dismisses. Until the settled
  survey lands, a press inside the panel waits; the gate never fails open inside
  the panel.
- **Band tail** — a band contact of an open menu that moves away from the
  taskbar is the opening pull's tail and settles nothing (once per 500 ms).
- **Layout** — the apply reservation and the admission claim are the two
  `seq_cst` sides of one handshake; a change under a live contact is deferred
  whole and applied after the release.
- **Unhealed window** — a shown window with the panel off it is healed once the
  close record ended, no dismiss may still land and no animation owns the panel;
  a pull on it is admitted as the reveal (`reason=unhealed`).

## Settle ownership

One damped spring per outcome; `AnimationStyle` picks the damping (`Windows 11`
1.72, fitted to the native Quick Settings flyout; `Windows 10` 0.9). The duration
is planned by travel: `clamp(remaining / 2.2 DIP/ms, 160, 520)`. A settle is the
only owner of `Progress`; the coast (200 ms linear after 8 ms without a report)
is the only other animation. `TakeProgressNow()` is the only reader that turns the
recorded base into a position.
