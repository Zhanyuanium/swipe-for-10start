# Gesture and state machines

> M3. The executor's thread, launcher read and timing are in
> [architecture.md](architecture.md); this page is the Start side's own state.

## Session types

- **dismiss** — the contact started on the open panel. A vertical drag toward
  the taskbar follows the finger; a release past the drag-intent distance
  settles to 0 and closes. The distance is `CommitDistancePercent` of the
  panel's travel, default 10%, the same value on every taskbar side. A press on
  a scrolling region that can still scroll is refused
  (`dismiss deferred to scroll ...`); a press on a region at its end closes.
- **reveal** — the contact started in the taskbar band (or the band-tail strip)
  while the menu is closed. Admission records it; the bridge show is requested on
  the first report that reaches 24 DIP of inward travel, and the same contact then
  owns `Progress` through the open.
- **candidate** — a band contact measured but not yet active. It becomes active
  at the open's live takeover, or is released and carried in the late-release
  record if it lifts before the open.
- **grab** — a contact that lands while a close owns the panel. Its down is
  hit-tested against the panel surface that is still on screen (or the band); a
  point in the area the retract has already left, or off the panel's cross range,
  is refused
  (`reveal admit ... reason=offsurface`) and muted for the rest of its contact.
  A grab is measured but writes no `Progress` and gets exactly one transfer
  attempt at 24 DIP. A refused transfer ends the session through the same mute a
  refused down gets. A band down whose transfer finds nothing left to take — the
  retract at or below the entry, or a close that has already ended — is the
  explicit open request a press makes, but the pull keeps the finger: it is
  `grab=open`, the follow starts at the reveal entry and chases the finger, where
  a press would settle. A landing retires no grant: a contact still on its way to
  its 24 DIP report keeps its one attempt.

One contact owns the panel: `g_hidSession.active` (dismiss) or
`g_reveal.edgeCandidate` (reveal or grab). The other session is refused until the
contact lifts.

## The reveal

1. **Band admission** — the first positioned report inside the band while the
   window is closed. `StartRevealFollow` records the contact identity, its down
   point and the clocks, and nothing else: no show request, no pull generation,
   no dismiss cancel. A tap anywhere in the band is admitted and then released
   without a claim. The same pass records the down's **author**
   (`g_contactOrigin.author`): `Band` for a down in the band — and it stays that
   author even when the pass could not admit it for a renderer, layout or
   distance reason, because the shell's own gesture opens from that band — and
   `Grab` for a down a close granted. During a close the same call is the grab's
   admission instead:
   the down must be in the band or on the panel surface the retract still shows —
   its mapped progress at or below the panel's live one, which is the same thing
   as the panel's trail edge being at or beyond the point, **and** the point
   inside the panel rectangle's cross range, both from the run's own
   geometry (`RevealProgressDip`, `PeekProgressNow`, `PanelCrossContains`).
   Anything else is `reason=offsurface` and muted; a granted grab logs
   `reason=grab`.
2. **Qualified pull** — on the first positioned report of that same contact whose
   signed inward travel (`RevealTravelDip`) reaches `kAdoptTravelDip` (24 DIP),
   `TryClaimRevealOpen` attempts the one open claim. It marks the attempt per
   contact even when it fails, so a refused claim is never retried report by
   report; an open the shell performs on its own is adopted instead. A claim that
   can run starts the pull generation, drops the previous release record, cancels
   the close's armed schedules (or takes the retract over from its live position,
   see The close), and calls `PublishOpenClaim`
   (`controllerDirection = 1`, `controllerTouch`, the entry, `exec show`). The
   check runs before `UpdateRevealSession`: a takeover expects a window that is
   already open and would never request one (`reveal pull qualified ...
   grab=none`, `reveal show request ...`; a tap logs neither). A grab spends the
   same crossing on the transfer of its retract instead
   (`grab=taken|open|refused`). A taken transfer hands the panel to the follow; a
   band down that finds nothing left to take — the retract already past the
   entry, or a close that has already ended (`reason=offsurface|no-close`, named
   on the same line) — shares the press's cancel body when there is still a close
   to cancel, but not its settle: the grant is spent and the follow starts at
   the entry (`grab=open`, `window=open`), or, when the window has already hidden,
   the ordinary show is requested and the follow binds at that entry
   (`window=hidden`); every refusal consumes the
   contact. Measured on the
   bottom docked layout: 31 ms from the first positioned report to the 24 DIP
   crossing on a ~1 DIP/ms pull, and +15 ms from the crossing to the executor's
   `ShowStartView`.
3. **Shell open** — the bridge's executor calls `ShowStartView`; the shell
   uncloaks (245–268 ms worst case; the bottom auto-hidden Win open showed its
   first panel pixel at +82–187 ms from the key, median +115 over three runs; the
   bottom docked run was +190 ms). The Start side starts the content settle at
   the entry (`RevealEntryProgress`; bottom auto-hidden warm 0.255, bottom docked
   0.348, cold 0.000), not at 0.
4. **Live takeover** — at the open, `PromoteRevealContact` adopts the contact
   that began in the band if it is still down and has travelled
   `kAdoptTravelDip` (24 DIP). **Adoption needs an author**: only a contact whose
   own down was admitted for that open — the band's open claim, or a grab — is
   adopted (`AdoptRevealContact` reads `g_contactOrigin.author`). A finger that
   landed on the desktop or inside the panel while the menu was closed has no
   author, so a keyboard open under it never binds it: the keyboard's open is the
   keyboard's. A down on the open panel is the dismiss and is never promoted,
   refused twice by its author and by `startedOpen`. A grab that still holds its
   grant is never adopted
   here, or anywhere else: the flag keeps every takeover out while the grant is
   unspent, so an open another path performs before the crossing is not the
   grab's. The transfer that spends the grant — a take, or the band pull that
   becomes the open request — clears it, so the same finger is promoted when the
   open it asked for arrives. The same test gates the candidate branch of
   `UpdateRevealSession`: a contact that landed on a shown but off-rest panel
   (the unhealed case) does not take `Progress` over without the 24 DIP, or
   without having qualified earlier (`claimAttempted`). A stationary finger
   on a settled open never acquires the panel. A takeover of a hidden panel
   starts at the entry, holds it for the
   first presentation (`kRevealEntryHoldMs`, 30 ms), then every report moves the
   visible position toward the absolute mapped position at
   `kSettleSpeedDipPerMs` (2.2 DIP/ms), never past it. A takeover of a panel that
   is already presented — the pull cancelled a close — binds to the position the
   transfer already took (it does not take again, and holds nothing), and chases
   `mapped - liveOffset`, the offset the finger-to-panel pair had when the pull
   took the retract over. A band pull the retract carried below the entry is the
   hidden-panel start even though its window still shows: the panel is off
   screen, so the follow starts at the entry, holds it, and chases the finger,
   and the same report that cancelled the close activates it — no settle stands
   in between. The finger's own motion is the mapping; only the
   written position is rate-limited.
5. **Follow** — once caught up, `behind=0.000`: the panel is glued (bottom
   auto-hidden `progress=0.96` at a held 300 DIP finger, the panel edge 1.8 px
   from it; `progress=0.90` at 350 DIP, 0.4 px; the bottom docked 300 DIP hold
   measured 2 px). A release commits on travel (`CommitDistancePercent` of the
   panel's `distance`, default 10%, one value on every axis) or on the session
   average, and the release speed is measured over the last 8–30 ms
   window (freshness 30 ms). The one exception is a live takeover: a pull that
   came in to interrupt a close commits when the panel is at or above the
   position it took over, whatever its own travel (the release line prints
   `live=1`).
6. **Late release** — a pull that lifts before the open keeps its travel, mapped
   position and end speed in one `RevealLateRelease` record. The window it may be
   consumed in is 300 ms measured from its release (the release on a closed
   menu): a grab never writes a record, and no landing consumes one, because the
   close a grab could not take is the only menu it was ever granted. The open
   reads the record and settles from the panel's live position with that end
   speed. A live takeover, an admitted newer pull and the consumption invalidate
   it; a new down does not.
7. **Show/close in flight** — while `controllerTouch` or `g_intentOpenActive` is
   set, and while the close record owns the panel with a reveal candidate, the
   idle
   release is skipped: the digitizer may go quiet during the shell's uncloak and
   during the key/hide, and that silence is not a lift. A real lift is a report
   and still ends the contact. The same rule covers a contact a close consumed:
   silence through the hide never ends its latch or its mute. It ends on a lift
   report (`contacts <= 0`), or on a report that proves a new contact — another
   device, a different asserted Contact Identifier, or a different asserted node
   on a device that does not name contacts.
   A Contact Identifier also keeps the mute through silence, so only evidence
   ends it; its residual is a reused identifier after a lost lift, read as a
   continuation until that finger lifts. A node index and the coordinate-only
   path can prove a different contact in a report but cannot survive silence (a
   node can be handed on), so once the close has ended they keep the idle
   fallbacks (the candidate's 120 ms watchdog, then the 18 ms latch) as the
   missing-lift guard; its risk is a consumed finger that stays silent past the
   landing and then resumes, read as a new down.

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
(`reveal pull qualified travel=24`, one `exec show`).

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
intent close -> TakeCloseOwnership (the record, accepted) -> BeginShellCloseSettle(TakeProgressNow())
             -> exec dismiss at settle + 30 ms -> bridge Dismiss -> hidden
```

A gesture close runs the same pipeline with its own release settle and `exec
dismiss` at that settle's end; it has no 30 ms lead. Both families accept the
close **before** the settle's first frame, so no frame and no report can see a
panel leaving with no close behind it.

### The close record

A close is an identity, not an age. One record, written and read only by the
input thread, answers every "is a close in flight?" question:

| field | meaning |
|---|---|
| `serial` | monotonic per accepted close |
| `acceptedTick` | when the ownership was taken |
| `openTick`, `openGeneration` | the menu it was accepted against, read from the open record at that instant (diagnostics — a landing is identified by its dismiss command and its open generation) |
| `phase` | `Settling` (the retract runs, this side has not published its dismiss), `AwaitingHide` (the dismiss is published, the shell's hide is ahead), `Done` |
| `repairAt` | `acceptedTick + kRestoreSuppressMs` (600 ms): the bound after which a close that never landed is repaired |

The tick fields are data; `phase` is the answer. `CloseInFlight()` reads the
phase, and the record's own tick fields never decide anything: a duration is not
a state. Acceptance logs `close accepted serial=... kind=... open=... gen=...
repair=...`; an ending logs `close ended serial=... kind=... phase=... reason=...
age=...ms`.

`TakeCloseOwnership` publishes the `closing` direction **before the settle's
first frame**, consumes every contact already on the glass (the dismiss mute, and
the reveal flags cleared), clears `controllerTouch` and accepts the record. That
one word is the whole toggle answer while the record owns the panel; the shell's
cloak and state cannot answer it, because they still say open for the whole
retract. The close settle is the open spring in the closing direction, full travel
(387 ms at the bottom auto-hidden 852 DIP; 409 at the bottom docked 900), no seam
speed, from the live position: it moves at once and decelerates into the edge.
The dismiss is scheduled, not immediate, so the panel is never hidden mid-slide.

**Endings.** The record is retired by exactly these, and each one knows what it
owes beyond the record: the landing (`landing`, or `external` when the hide was
not this mod's — the landing body already clears the slots), a pull cancel
(`pull-cancel`, the open direction was published and the follow owns the panel), a
press or band-pull cancel (`open-cancel`), an external takeover (`external`), a
shell open that arrived during the close (`open`), a terminal refusal or repair
(`no-settle`, `no-executor`, `window-closed`, `repair`), and the repair bound.
The ending retires the record only: the dismiss command and its open generation
stay identified, because that command is the landing's identity.

**The repair bound.** At `repairAt` a close that never landed ends as a repair
(`close ended ... reason=repair-deadline`): the published direction is released
(the bridge's next toggle reads the cloak and the internal state again, and the
unhealed rule may bring the panel back), and the one line says so. It is not a
silent expiry — the command and the open-cancel ticket still name the hide that
may land, and the unhealed heal waits for that command to answer
(`CloseHideMayLand`), so the panel is not restored under a hide that is still
coming.

A touch contact that lands during a close is decided on its down. It is a grab
only when the point is in the band or on the panel surface the retract still
shows: the point must be on the panel's side of its trailing edge along the
gesture (its mapped progress at or below the panel's own live progress) **and
inside the panel rectangle's cross range** (the axis the gesture does not slide
along, `PanelCrossContains`). Both failures are one refusal: a point in the area
the panel already left, and a point perpendicular to and entirely outside the
panel, are `reveal admit ... reason=offsurface` and muted for the rest of their
contact, so neither can ever open the menu. A grab is measured but writes no
`Progress` and leaves the close's dismiss armed. When it crosses 24 DIP inward it
spends its one
opportunity: `CancelCloseForQualifiedPull` drops both close schedules — the armed
local dismiss and the armed `exec dismiss` the bridge is holding for the end of
the settle — ends the close record, republishes the open direction, arms the
same recovery ticket an explicit open request arms, and takes the retract over
from the panel's
live position (`grab=taken`). That take is the transfer's only one: the position
is pinned as the frame the compositor shows, and the follow binds to it on the
same report (`live=1`) instead of settling or taking again. Every close this mod
owns is cancellable that way while its dismiss is still ahead — the Win key and
the two Start-button primitives included: their dismiss is published at once but
the bridge keeps it in an armed timer until its deadline. A published dismiss at
or past that deadline may already be inside the shell's `Dismiss` call, so the
transfer is refused there too (`reason=dispatch`): the grab lost the race and the
close finishes.

A refusal — the dismiss is already inside the shell, its deadline has passed,
nothing is left to cancel, or a down on the retract's surface finds no panel on
screen (its retract already at or below the entry, or a close that has already
ended) — consumes the contact
(`grab=refused reason=...`): it keeps no open right until it lifts, every later
report is inert, and its release leaves no late-open record. The confirmation
that the hide may still arrive and a layout with no distance consume it wherever
the close stands. The exception is the band down: that finger
asked for the same menu the Win key asks for, so the attempt becomes the explicit
open request instead of a refusal (`grab=open`). With a close still in flight it
shares the press's cancel body
— both schedules dropped, the pending command cancelled, the close record ended,
the open direction published, the recovery ticket armed — and only that body: a
press has no finger and settles the panel back (`intent open cancel from=...`),
while the pull keeps its finger, spends the grant, and the follow starts the panel
at the reveal entry and chases the mapped position at 2.2 DIP/ms. The window is
still shown, but its panel is below the entry, so there is nothing on screen to
take over and nothing settles. If the window has already hidden by the time the
request runs, there is no panel to follow either: the ordinary show is requested,
the entry is held, and the follow binds there when the open promotes the contact
(`grab=open window=hidden`). A close that has already ended has nothing to cancel:
the same band down is the ordinary pull on a closed menu — the open direction is
published, the show is requested when the window is hidden (or the follow binds
at the live position when it still shows), and the reason on the line names that
end (`reason=no-close`). A down on the surface the retract had shown, or in
the area it has already left, is not a band down and stays refused. A contact
that never travels leaves the close to run; a tap in the band was admitted as a
grab and never gets there. A taken transfer starts no settle either: the follow
continues from the pinned position.

A press while the close runs is recovered, not guaranteed. `IntentShouldClose`
reads the controller's phase, so a press during the retract is an open:
- a close still settling publishes open at once; the Start side drops
  the local dismiss slot, cancels the pending executor command, ends the close
  record and settles the panel back from its live position
  (`intent open cancel from=...`). If the
  dismiss already entered the shell's `Dismiss` call, the hide lands and the
  landing reissues the open (`intent open recovered entry=...`), provided the
  landing is for the same menu the cancel named and no external takeover
  arrived with it (`intent reconcile ... (before landing recovery)`);
- only a close that is already out of the controller's reach is latched: the
  shell is hiding (state 9/11), or our own executor's `Dismiss` call is live on
  the launcher thread. The bridge latches such a press (one slot, tick, the epoch
  the admission was decided under); the hidden event arms a 50 ms timer and the
  timer serves the open after the event's stack has unwound
  (`own press served wait=...`). A show at the hidden event's own tick was
  swallowed (state stuck at 3), which is why the serve is deferred. An executor
  dismiss that is only armed is still revocable, so a press then publishes the
  open and the Start side cancels the command.

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

A close consumes every contact that was already on the glass when it started:
that contact cannot interrupt the close and cannot reopen the menu. A landing
therefore adopts no finger. After `win close observed` the controller issues no
touch show; no hover, carried travel or release record turns the landed close
into a `Show`; and a contact the close consumed is inert until it lifts
(`win close observed muted=1`). A grab is not one of those: a grant the contact
has not spent yet is not retired by the landing, so the finger that landed in the
band keeps it, and its 24 DIP report asks for the open the ordinary way, with the
close it was granted against already over (`grab=open ... reason=no-close`).
Silence through the hide does not end it: the
latch and the mute stay until a lift report or a report that proves a new
contact — another device, a different asserted Contact Identifier, or a
different asserted node on a device that does not name contacts. A Contact
Identifier also keeps the mute through silence; a node index and the
coordinate-only path cannot survive silence, so
once the close has ended they keep the idle fallbacks (the candidate's 120 ms
watchdog, then the 18 ms latch) as the missing-lift guard.
Its risk is a consumed finger that stays silent past the landing and then
resumes, read as a new down. The only open a landing can still serve is the
explicit request's obligation: a press, or a band pull whose cancel armed the
ticket, that cancelled a close whose
dismiss ran anyway
(`intent open recovered entry=...`). A menu closed by a touch is opened again by
a new pull, never by the finger whose grab was refused.

A close can also land without a transition: when the executor's confirmation
probe reads hidden and no cloak event carried the hide (the window was already
hidden when `Dismiss` ran, a native hide won the race, or the transition was
lost), the same landing body finishes the close and logs
`win close observed reason=confirmed-without-transition`. When the cloak still
reads open, or cannot answer, the confirmation is **retained** instead
(`exec dismiss retained command=... gen=...`): the command and its open
generation wait for the transition, which lands them once
(`exec dismiss retained served`) within a bounded wait. The command that owns the
pending dismiss is the identity — never the close record, which ends with the
landing — and a
confirmation from a menu a later open replaced is stale: it cannot land the new
menu's hide. A generation-zero stamp is valid only while no open has been
published; once one is, it is as stale as any other mismatch, and a fresh open
cancels a still-queued dismiss at the bridge and drops the confirmation. That
landing serves the explicit request's obligation only, and a still-down contact
is consumed
exactly as after any transition. The transition itself is never synthesized.

The idle is held through the close so the digitizer's pause during the key/hide
does not drop a contact that is mid-grab. The hold ends on the close's ending —
its landing, its cancel, its repair — or the contact's own lift, and a real lift
is still a report. The
earlier build released a held pull *at* the landing and replayed the open from
the panel's position (`reveal takeover ... live=0`, measured on the bottom docked
layout with a 350 DIP pull held through a Win close); that adoption is gone. A
contact already down when the close starts is consumed by it. A grab that
outlives its retract is not: its grant stays unspent until its own crossing, and
while the close is still cancellable that crossing takes the retract over (or, for
a band down, becomes the open request); once the close has ended the same
crossing is the ordinary open of a closed menu for a band down, and a refusal
otherwise. Silence after the landing does not release a consumed contact, and the
menu it belongs to stays closed.
A band down that crosses while the retract is still in flight cancels it and opens
the menu instead (`grab=open`), following the finger from the entry rather than
settling it open, so the asymmetry the owner rejected — a finger
swallowed where the key works — no longer exists in that window, and neither does
the switch-open of the first parity. The
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
  landed, or a reopen whose settle never ran) is healed once the close record has
  ended, no dismiss of ours may still land (`CloseHideMayLand`) and no
  animation/follow owns the panel; a pull on that
  window is admitted as the reveal (`reason=unhealed`).

## Settle ownership

One damped spring per outcome; `AnimationStyle` picks the damping (`Windows 11`
1.72, fitted to the native Quick Settings flyout; `Windows 10` 0.9). The duration
is planned by the travel (`clamp(remaining / 2.2 DIP/ms, 160, 520)`). A settle
is the only owner of `Progress`; the coast (a 200 ms linear continuation after
8 ms without a report) is the only other animation and the next report pins it.
`TakeProgressNow()` is the only reader that turns the animation's recorded base
into a position, so every interruption starts where the panel is.
