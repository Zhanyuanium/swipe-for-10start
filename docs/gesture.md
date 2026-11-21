# Gesture and state machines

> M3. The executor's thread, launcher read and timing are in
> [architecture.md](architecture.md); this page is the Start side's own state.

## Session types

- **dismiss** — the contact started on the open panel. A vertical drag toward
  the taskbar follows the finger; a release past the commit threshold settles to
  0 and closes. A press on a scrolling region that can still scroll is refused
  (`dismiss deferred to scroll ...`); a press on a region at its end closes.
- **reveal** — the contact started in the taskbar band (or the band-tail strip)
  while the menu is closed. Admission records it; the bridge show is requested on
  the first report that reaches 24 DIP of inward travel, and the same contact then
  owns `Progress` through the open.
- **candidate** — a band contact measured but not yet active. It becomes active
  at the open's live takeover, or is released and carried in the late-release
  record if it lifts before the open.

One contact owns the panel: `g_hidSession.active` (dismiss) or
`g_reveal.edgeCandidate` (reveal). The other session is refused until the
contact lifts.

## The reveal

1. **Band admission** — the first positioned report inside the band while the
   window is closed. `StartRevealFollow` records the contact identity, its down
   point and the clocks, and nothing else: no show request, no pull generation,
   no dismiss cancel. A tap anywhere in the band is admitted and then released
   without a claim.
2. **Qualified pull** — on the first positioned report of that same contact whose
   signed inward travel (`RevealTravelDip`) reaches `kAdoptTravelDip` (24 DIP),
   `TryClaimRevealOpen` attempts the one open claim. It marks the attempt per
   contact even when it fails, so a refused claim is never retried report by
   report; an open the shell performs on its own is adopted instead. A claim that
   can run starts the pull generation, drops the previous release record, cancels
   a close the shell still has scheduled, and calls `PublishOpenClaim`
   (`controllerDirection = 1`, `controllerTouch`, the entry, `exec show`). The
   check runs before `UpdateRevealSession`: a takeover expects a window that is
   already open and would never request one (`reveal pull qualified ...`,
   `reveal show request ...`; a tap logs neither). Measured on the bottom docked
   layout: 31 ms from the first positioned report to the 24 DIP crossing on a
   ~1 DIP/ms pull, and +15 ms from the crossing to the executor's
   `ShowStartView`.
3. **Shell open** — the bridge's executor calls `ShowStartView`; the shell
   uncloaks (245–268 ms worst case; the bottom auto-hidden Win open showed its
   first panel pixel at +82–187 ms from the key, median +115 over three runs; the
   bottom docked run was +190 ms). The Start side starts the content settle at
   the entry (`RevealEntryProgress`; bottom auto-hidden warm 0.255, bottom docked
   0.348, cold 0.000), not at 0.
4. **Live takeover** — at the open, `PromoteRevealContact` adopts the contact
   that began in the band if it is still down and has travelled
   `kAdoptTravelDip` (24 DIP). The same test gates the candidate branch of
   `UpdateRevealSession`: a contact that landed on a shown but off-rest panel
   (the unhealed case) does not take `Progress` over without the 24 DIP, or
   without having qualified earlier (`openClaimAttempted`). A stationary finger
   on a settled open never acquires the panel. The takeover starts at the entry,
   holds it for the
   first presentation (`kRevealEntryHoldMs`, 30 ms), then every report moves the
   visible position toward the absolute mapped position at
   `kSettleSpeedDipPerMs` (2.2 DIP/ms), never past it. The finger's own motion is
   the mapping; only the written position is rate-limited.
5. **Follow** — once caught up, `behind=0.000`: the panel is glued (bottom
   auto-hidden `progress=0.96` at a held 300 DIP finger, the panel edge 1.8 px
   from it; `progress=0.90` at 350 DIP, 0.4 px; the bottom docked 300 DIP hold
   measured 2 px). A release commits on travel (half the travel) or on the
   session average, and the release speed is measured over the last 8–30 ms
   window (freshness 30 ms).
6. **Late release** — a pull that lifts before the open keeps its travel, mapped
   position and end speed in one `RevealLateRelease` record. The window it may be
   consumed in is 300 ms measured from its readiness: the release time when the
   menu was closed, or `readyQpc` (the landing) when the pull waited for a close.
   The idle-deadline expiry uses that same clock and skips a record that is still
   deferred. The open
   reads it and settles from the panel's live position with that end speed. A
   live takeover, an admitted newer pull, the consumption and the window
   invalidate it; a new down does not.
7. **Show/close in flight** — while `controllerTouch` or `g_intentOpenActive` is
   set, and while a close is in flight with a reveal candidate, the idle
   release is skipped: the digitizer may go quiet during the shell's uncloak and
   during the key/hide, and that silence is not a lift. A real lift is a report
   and still ends the contact.

The 18 ms idle still ends a dismiss session that is truly silent, with
`src=idle`. That window is armed as a one-shot deadline at the last report, so
the release lands 18-19 ms later instead of on the next periodic tick. A band
candidate is held for `kCandidateIdleHoldMs` (120 ms)
instead, so a sparse report stream (20–30 ms steps) can still deliver its 25 DIP
flick; the watchdog that releases it is the missing-lift case and also clears
the contact latch, the origin, the open-check cache and the tracked identity. A
report gap alone is not a new down: the next report continues the same contact
from its original admission point.

A tap must not open the menu. Measured on the bottom docked layout, with the
trace on: a tap on the taskbar's empty background, on the clock and on the
tray's empty area each logged `reveal admit ... ok=1 reason=admitted` twice (the
injector reports the same contact twice), no `reveal pull qualified`, no
`reveal show request`, no `exec show`, and the menu stayed cloaked. A tap on the
Start button logged no claim either; the shell's own Start-button intent produced
one open (`exec show ... method=11`) and the band candidate was not promoted.
The shell's own edge gesture still opens from about 20 DIP, which is the
documented fallback: a 23 DIP drag requested nothing from this mod (the native
gesture opened the menu), and a 25 DIP drag requested one show
(`reveal pull qualified travel=24`, one `exec show`). The landing applies the
same threshold (see The close).

## The open entry

`RevealEntryProgress` is the smallest `Progress` whose translated trail edge sits
on the work area's edge — the first progress at which any of the panel is on
screen. The trail is the measured panel edge (bottom auto-hidden
`reveal panelTrail=265` at `axis=3`; the earlier bottom auto-hidden variant
measured 167, the bottom docked 265). The fallback is the far screen edge. A warm
bottom auto-hidden measurement gives 0.255 on the 852 DIP panel (bottom docked
0.348); a cold first open (no measurement yet, the request deferred because the
panel is owned)
gives 0.000, and the settle starts there honestly. A warm open's first visible
frame is the entry; a cold one starts below the screen edge and reaches it on the
first frames.

## The close

One close path for the Win key, the Start button and the click-away:

```
intent close -> TakeCloseOwnership -> BeginMirrorCloseSettle(TakeProgressNow())
             -> exec dismiss at settle + 30 ms -> bridge Dismiss -> hidden
```

A gesture close runs the same pipeline with its own release settle and `exec
dismiss` at that settle's end; it has no 30 ms lead.

`TakeCloseOwnership` sets `g_suppressRestoreUntil` (the visibility service must
not heal the retracted panel), publishes the `closing` direction **before the
settle's first frame**, mutes a contact still on the glass, and clears the reveal
and `controllerTouch`. That one word is the whole toggle answer while it is set;
the shell's cloak and state cannot answer it, because they still say open for the
whole retract. The mirrored settle is the open curve played backwards, full
travel (387 ms at the bottom auto-hidden 852 DIP; 409 at the bottom docked 900),
no seam speed,
from the live position. The dismiss
is scheduled, not immediate, so the panel is never hidden mid-slide.

A touch contact that lands during a gesture close does not cancel it on the
down. When the same contact crosses 24 DIP inward it becomes a qualified pull:
`CancelCloseForQualifiedPull` drops the armed local dismiss, clears the
suppression, republishes the open direction and settles the panel back from its
live position. A contact that never travels leaves the close to run; a tap that
never enters the band has no candidate and cannot cancel anything.

A press while the close runs is recovered, not guaranteed. `IntentShouldClose`
reads the controller's phase, so a press during the retract is an open:
- a gesture close still settling publishes open at once; the Start side drops
  the local dismiss slot, cancels the pending executor command, clears the
  suppression and settles the panel back from its live position
  (`intent open cancel from=...`). If the
  dismiss already entered the shell's `Dismiss` call, the hide lands and the
  landing reissues the open (`intent open recovered entry=...`), provided the
  landing is for the same menu the cancel named and no external takeover
  arrived with it (`intent reconcile ... (before landing recovery)`);
- an intent close has its dismiss armed for a future deadline, so the bridge
  latches the press (one slot, tick, the epoch the admission was decided under); the hidden event arms a
  50 ms timer and the timer serves the open after the event's stack has unwound
  (`own press served wait=...`). A show at the hidden event's own tick was
  swallowed (state stuck at 3), which is why the serve is deferred. A shell hide
  in flight (state 9/11) is latched the same way.

The recoveries are bounded obligations, not a theorem: a newer press supersedes
the slot, a dismissed command the shell refused or that expired invalidates it, a
serve whose peer is gone or replaced by a new activation is dropped, and a latch
older than 1200 ms expires. The windows this cannot cover are the ones where
there is no consumer left — a press after the 600 ms liveness gate closed, or a
latch armed before a kill and served after it. There the shell's own toggle
serves the press, which is the intended fallback: one interception may be lost, a
wedged menu never is.

That fallback is **not** available for a press the bridge already swallowed: the
shell never saw it, so "the shell's own toggle serves it" would be false. Such a
press is decided by the paths above — the cancel, the latch, or the landing
recovery — and a press that loses all three (peer gone between the gate and the
latch's serve, epoch changed) is lost, not passed to the shell. A local-only
cancel (no executor command was pending) arms no recovery ticket, so an unrelated
hide of the same menu cannot be mistaken for the close that press cancelled.

A press while the open runs is a close: the `open` direction makes it a close, and
the settle starts from the live position (`from=0.542` measured 172 ms into an
open).

A pull that is still down and qualified when the close lands (or a qualified
release inside its readiness window — the release on a closed menu, the landing
for a deferred record) takes the menu back: after `win close
observed` the controller issues one fresh show (`reveal reopen after close
pull=1 late=0 entry=...`), and the open adopts the live contact or consumes the
release record. "Qualified" is the same signed inward travel, 24 DIP, measured
from the contact's own down point at its last positioned report
(`RevealPullQualified`). Ownership is not the test: an active follow that pulled
out and came back toward the edge fails the current-travel check at the landing
too. The late-release record only exists for a pull that travelled 24 DIP. A
finger merely resting in the band during the retract is admitted deferred, logs
`reveal defer released candidate=1 down=1` at the landing, and is not reopened; a
qualified release and an independently owed Win/button recovery are unaffected.
Touch ownership follows the finger: a release record reopens as `pull=0 late=1`
with `controllerTouch=0`, so the next real click away from the menu closes it;
only a contact still down at the landing keeps the click-away protection.

A close can also land without a transition: when the executor's confirmation
probe reads hidden and no cloak event carried the hide (the window was already
hidden when `Dismiss` ran, a native hide won the race, or the transition was
lost), the same landing body finishes the close and logs
`win close observed reason=confirmed-without-transition`. When the cloak still
reads open, or cannot answer, the confirmation is **retained** instead
(`exec dismiss retained command=... gen=...`): the command and its open
generation wait for the transition, which lands them once
(`exec dismiss retained served`) within a bounded wait. The command that owns the
pending dismiss is the identity, never the suppression window, and a
confirmation from a menu a later open replaced is stale: it cannot land the new
menu's hide. A generation-zero stamp is valid only while no open has been
published; once one is, it is as stale as any other mismatch, and a fresh open
cancels a still-queued dismiss at the bridge and drops the confirmation. A held
pull is released by that landing, and a still-down qualified
contact reopens exactly as it does after any transition. The transition itself is
never synthesized.

The idle is held through the close so the digitizer's pause during the key/hide
does not drop the pull. Measured on the bottom docked layout: held pull at 350
DIP, Win close, landing reopened with `reveal takeover from=0.348 offset=0.558`,
221 reports, `progress=0.91 behind=0.000 commit=1`, menu open. A contact resting
at
898 DIP through the same Win close produced `reveal defer released candidate=1
down=1` and no `reveal reopen after close`, and the menu stayed closed. The
bottom auto-hidden entry is 0.255.

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
