# Gesture and state machines

> M3. The executor's thread, launcher read and timing are in
> [architecture.md](architecture.md); this page is the Start side's own state.

## Session types

- **dismiss** — the contact started on the open panel. A vertical drag toward
  the taskbar follows the finger; a release past the commit threshold settles to
  0 and closes. A press on a scrolling region that can still scroll is refused
  (`dismiss deferred to scroll ...`); a press on a region at its end closes.
- **reveal** — the contact started in the taskbar band (or the band-tail strip)
  while the menu is closed. The first report requests the bridge show; the same
  contact then owns `Progress` through the open.
- **candidate** — a band contact measured but not yet active. It becomes active
  at the open's live takeover, or is released and carried in the late-release
  record if it lifts before the open.

One contact owns the panel: `g_hidSession.active` (dismiss) or
`g_reveal.edgeCandidate` (reveal). The other session is refused until the
contact lifts.

## The reveal

1. **Band admission** — the first positioned report inside the band while the
   window is closed. `StartRevealFollow` records the down point, starts the
   timing, and requests `exec show` **once per open** (`g_intentOpenActive`).
   It sets `controllerOwning` (phase 1) and `controllerTouch`.
2. **Shell open** — the bridge's executor calls `ShowStartView`; the shell
   uncloaks (245–268 ms worst case; the docked keyboard open showed its first
   pixel at +190 ms from the key). The Start side starts the content settle at
   the entry (`RevealEntryProgress`; docked warm 0.348, cold 0.000), not at 0.
3. **Live takeover** — at the open, `PromoteRevealContact` adopts the contact
   that began in the band if it is still down and has travelled
   `kAdoptTravelDip` (24 DIP). The takeover starts at the entry, holds it for the
   first presentation (`kRevealEntryHoldMs`, 30 ms), then every report moves the
   visible position toward the absolute mapped position at
   `kSettleSpeedDipPerMs` (2.2 DIP/ms), never past it. The finger's own motion is
   the mapping; only the written position is rate-limited.
4. **Follow** — once caught up, `behind=0.000`: the panel is glued (docked
   `progress=0.96` at a held 300 DIP finger, the panel edge 2 px from it). A
   release
   commits on travel (half the travel) or on the session average, and the release
   speed is measured over the last 8–30 ms window (freshness 30 ms).
5. **Late release** — a pull that lifts before the open keeps its travel, mapped
   position and end speed in one `RevealLateRelease` record for 300 ms. The open
   reads it and settles from the panel's live position with that end speed. A
   live takeover, an admitted newer pull, the consumption and the window
   invalidate it; a new down does not.
6. **Show/close in flight** — while `controllerTouch` or `g_intentOpenActive` is
   set, and while a close is in flight with a reveal candidate, the 18 ms idle
   release is skipped: the digitizer may go quiet during the shell's uncloak and
   during the key/hide, and that silence is not a lift. A real lift is a report
   and still ends the contact.

The 18 ms idle still ends a contact that is truly silent, with `src=idle`.

## The open entry

`RevealEntryProgress` is the smallest `Progress` whose translated trail edge sits
on the work area's edge — the first progress at which any of the panel is on
screen. The trail is the measured panel edge (docked `reveal panelTrail=265` at
`axis=3`; auto-hidden `167`). The fallback is the far screen edge. A warm
docked measurement gives 0.348 on the 900 DIP panel (auto-hidden 0.186); a cold
first open (no measurement yet, the request deferred because the panel is owned)
gives 0.000, and the settle starts there honestly. The first visible frame is
always the entry.

## The close

One close path for the Win key, the Start button and the click-away:

```
intent close -> TakeCloseOwnership -> BeginMirrorCloseSettle(TakeProgressNow())
             -> exec dismiss at settle + 30 ms -> bridge Dismiss -> hidden
```

`TakeCloseOwnership` sets `g_suppressRestoreUntil` (the visibility poll must not
heal the retracted panel), mutes a contact still on the glass, clears the reveal
and clears `controllerTouch`. The mirrored settle is the open curve played
backwards, full travel (409 ms at 900 DIP docked; 387 at 852 in the historical
auto-hidden geometry), no seam speed, from the live position. The dismiss is scheduled, not immediate, so the panel is never
hidden mid-slide.

A press while the close runs is never lost. If the close is still settling
(phase 2, no dismiss armed), `IntentShouldClose` answers open and the Start side
takes the panel from its live position; if the dismiss is already armed and due,
or the shell is in the 9/11 hide, the bridge latches the press (one slot, tick)
and serves it as the same open intent when the hide lands (`own press served
wait=...`, 31–110 ms measured). A press while the open runs is a close: phase 1
makes it a close, and the settle starts from the live position (`from=0.542`
measured 172 ms into an open).

A pull that is still down when the close lands (or that lifted inside the 300 ms
late window) takes the menu back: after `win close observed` the controller
issues one fresh show (`reveal reopen after close pull=1 late=0 entry=...`), and
the open adopts the live contact or consumes the release record. The idle is
held through the close so the digitizer's pause during the key/hide does not
drop the pull. Measured: held pull at 350 DIP, Win close, landing reopened with
`reveal takeover from=0.348 offset=0.558`, 221 reports, `progress=0.91
behind=0.000 commit=1`, menu open.

The gesture close (a touch drag with enough travel/velocity) uses its own release
spring through `BeginGestureSettle` and schedules the same dismiss at the end of
that settle. There is no Windows-key tap anywhere.

## Gates

- **Vertical gate** — the scroll survey keeps up to six deepest vertically
  scrollable regions; a press inside one that can still scroll is refused, a
  press outside all of them dismisses. The survey publishes `Unknown` first, so
  a failed pass cannot leave a previous answer speaking; retries back off
  100/200/400 ms.
- **Band tail** — a contact that starts in the band of an open menu and moves
  away from the taskbar is the opening pull's tail: it settles nothing
  (`dismiss skipped band tail travel=...`, once per 500 ms). A press that starts
  on the panel takes the ordinary dismiss path.
- **Layout** — the apply reservation and the admission claim are the two
  `seq_cst` sides of one handshake; a layout change under a live contact is
  deferred whole and applied once after the release.
- **Unhealed window** — a shown window with the panel off it (a close whose hide
  landed, or a reopen whose settle never ran) is healed once the close's
  suppression has ended and no animation/follow owns the panel; a pull on that
  window is admitted as the reveal (`reason=unhealed`).

## Settle ownership

One damped spring per outcome; `AnimationStyle` picks the damping (`Windows 11`
1.72, fitted to the native Quick Settings flyout; `Windows 10` 0.9). The duration
is planned by the travel (`clamp(remaining / 2.2 DIP/ms, 160, 520)`). A settle
is the only owner of `Progress`; the coast (a 200 ms linear continuation after
8 ms without a report) is the only other animation and the next report pins it.
`TakeProgressNow()` is the only reader that turns the animation's recorded base
into a position, so every interruption starts where the panel is.
