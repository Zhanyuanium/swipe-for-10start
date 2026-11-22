# Changelog

A tag is the release. Up to `v0.6.2` it named a pair of mods, each with its own
Windhawk `@version`; from `v0.7.0` it names one mod whose `@version` is the tag
without the `v`. The Start/Bridge columns below are historical: they describe the
pair that shipped before the merge.

| Tag | Start | Bridge | Channel | What it is |
|---|---:|---:|---:|---|
| `v0.8.3` | 0.8.3 | — | 232 B | one drag-intent setting for every side, a mod page composed from both guides with localized names and settings, and two full-screen demos |
| `v0.8.2` | 0.8.2 | — | 232 B | an unload proved on the owner threads: one UI rendezvous window, counted proofs on every path, the manager's `Show` as the one adoption bootstrap, an open during a close handed to the controller, the version gate before the PDB fetch |
| `v0.8.1` | 0.8.1 | — | 232 B | the executor proves itself before either half intercepts: one `execCapable` word, one symbol attempt, an unowned show adopted, a close that moves from its first frame |
| `v0.8.0` | 0.8.0 | — | 216 B | a close that can be interrupted: one ownership record, one grab per contact, a press's open for a band pull, a landing that adopts no finger |
| `v0.7.0` | 0.7.0 | — | 216 B | one mod, two processes: the collection's review answered (symbol hooks, no pin, deadline-driven wakeups, tap rejection, lean channel) |
| `v0.6.2` | 0.44.0 | 0.25.0 | v7 | M3.3: one controller, one executor |
| `v0.6.1` | 0.40.0 | 0.21.0 | v4 | intent direction from the controller state; no press lost after a gesture close |
| `v0.6.0` | 0.39.0 | 0.20.0 | v4 | `PanelController` owns the toggle; the bridge executes `Show`/`Dismiss`; presses latched through a hide |
| `v0.5.0` | 0.36.0 | 0.17.0 | v2 | close arbitration at `XamlLauncherState::HideStartAndCortana`, reveal claim, cancel answer |
| `v0.4.2` | 0.35.0 | 0.16.0 | v2 | close/open ownership parity, deferred pulls, taskbar-click limit |
| `v0.4.1` | 0.34.0 | 0.16.0 | v2 | entry plus chase reveal, one open animation per pull, no window-shown dead state |
| `v0.4.0` | 0.33.0 | 0.16.0 | v2 | shared-memory bridge, taskbar-derived axis, reveal seam and late-release open |
| `v0.3.0` | 0.15.0 | 0.8.0 | — | speed-planned settle, exact landing, panel-rect dismiss guard |
| `v0.2.0` | 0.12.0 | 0.7.0 | — | dismiss and edge-reveal follow the finger on all four taskbar edges, scroll-aware vertical dismisses, one settle planner for both directions |
| `v0.1.0` | 0.6.5 | 0.3.0 | — | legacy Start touch gestures: swipe-to-dismiss and edge-swipe-to-open follow the finger |

`—` means the release predates the shared-memory channel.

## v0.8.3 — the drag distance is yours

Windhawk `@version` 0.8.3. The channel and the two-process design are unchanged;
this release makes one gesture threshold adjustable and brings the Windhawk page
in line with the guides.

- **One drag-intent distance.** `CommitDistancePercent` (default 10, range 1-50)
  is the share of the panel's travel a release must cover toward the target to
  commit. A shorter pull springs back closed; a shorter close drag reverses back
  open. The same value applies on every taskbar side, and a quick flick still
  commits regardless. Before this release the code used 10 % on top/bottom and
  20 % on left/right.
- **The mod page is built from the guides.** `tools/make_store_mods.ps1`
  composes `==WindhawkModReadme==` from `README.md`, a separator and
  `README.zh-CN.md`, dropping the repository's language-switch line and
  pointing the demo images at the repository. The name, description and all
  four settings carry `:zh-CN` fields, so the Windhawk page shows the same
  Chinese text as the guides.
- **Two full-screen demos.** `docs/images/demo-bottom.gif` and
  `docs/images/demo-left.gif` record the same gesture on a bottom and a left
  taskbar: the pull follows the finger, a close is reversed, then a plain close.
- **Docs that describe the current build.** All four taskbar sides are listed
  without a right-side caveat; the idle-wake paragraph and the
  `exec launcher symbols pending` line, which the code no longer writes, are
  gone.

Still owed, and named in `docs/limits.md`: rotation is not supported — the raw
digitizer coordinates are mapped without the display orientation, so a rotated
panel puts the edge band and travel in the wrong place — a pen by hand, a second
display, and a finger or pen tap that cannot reverse a close.

## v0.8.2 — an unload that can be proved

Windhawk `@version` 0.8.2. The channel is unchanged (232 bytes, `SingleMod3`);
this release is the lifecycle half of the same design.

- **The unload runs on the owner threads, synchronously.** The Start side creates
  its own message-only window on the UI thread before `Loaded` is registered; the
  unload sends teardown, then one empty drain, then destroy, and each
  `SendMessage` returns only after the handler and its delegate thunks have
  returned. The executor window is destroyed the same way with its own private
  message and handler. The dispatcher post, the completion events, the
  window-gone event and every retry loop are gone; the entry wait is a count
  predicate that a stale auto-reset signal cannot end.
- **Every path proves the same two counts.** Entry count zero and XAML
  registrations zero are required whether the teardown ran inline, ran through
  the window, or could not run at all. A destroyed window is not read as a dead
  UI thread. A proof that cannot be obtained blocks the unload once, with one
  line, instead of a poll.
- **The manager's `Show` is the one launcher-thread bootstrap.** It records the
  thread the shell called it on and creates the executor window there. The
  Win/Start-button hooks only serve an existing capability/window; they cannot
  create the first one (`OnStartButtonPressed` is not called on this build).
  `Dismiss`/`ShowStartView` never adopt: a non-pumping COM or taskbar caller
  passes natively, and if its internal manager `Show` then runs on that thread,
  that `Show` is the adopter.
- **An open during a close is handed to the controller.** When the shell shows
  the menu while a close is retracting it, both close schedules (local and
  executor) are cancelled, the recovery ticket dropped, and the controller
  publishes the open direction, so the next press closes instead of opening
  again.
- **The version check precedes the symbol work.** The launcher's fixed version is
  read from the mapped image's `RT_VERSION`, not from the file path; a build
  other than the reference is refused in one line and never starts the 33 MB PDB
  fetch. The Start side's `StartUI_.dll` root symbol gets the same one-attempt
  rule, after a bounded wait for the module.

Still owed, and named in `docs/limits.md`: a pen by hand, a second display and
rotation (untestable here), a finger or pen tap that cannot reverse a close
(mouse only), and a consumed finger that is silent through a landing and then
moves.

## v0.8.1 — the executor proves itself

Windhawk `@version` 0.8.1. The channel grows to 232 bytes, and the two halves
agree on one capability word before either takes a press in hand.

- **A capability, not a mapping.** The bridge publishes one `execCapable` word
  only while its worker, the executor window on the recorded launcher thread, the
  verified launcher layout and the show/dismiss primitives all exist. The Start
  side advertises, animates and commands only while that word is 1; a drop
  withdraws the peer, releases the panel and restores its rest. Launcher symbols
  are resolved once per module per install, with a terminal `unresolved` line,
  instead of a retry loop that could stall every press. The reference image is
  `twinui.pcshell.dll` fixed 10.0.26100.10068.
- **An unowned show is adopted, not replayed.** A show the shell performed while
  the press was passed keeps its own storyboard: the Start side inserts the
  resting value and writes `win open adopted reason=unowned` instead of settling
  from 0, which used to rewind an already presented menu and flash. Only a show
  this activation requested by intent, touch claim or landing keeps an entry
  settle.
- **The shell close moves from its first frame.** The Win key, the Start button
  and the click-away now apply the keyboard open's spring in the closing
  direction (`start * (1 - openY(i))`) instead of playing its samples backwards.
  The old shape covered 1.5 % of the travel in the first 380 ms and then snapped
  into the bar; the close now starts at once and decelerates into the edge, with
  the same duration, style and dismiss lead.
- **The channel is 232 bytes.** `SingleMod3`, magic `0x53333053`, `intentTaken`
  at 216 and `execCapable` at 224. The 0.8.0 layout was 216 bytes; the 224-byte
  development image between the two is not a tag. The namespace is the ABI
  identity, so an old image cannot join.

Still owed, and named in `docs/limits.md`: a pen by hand, a second display and
rotation (untestable here), a finger or pen tap that cannot reverse a close
(mouse only), and a consumed finger that is silent through a landing and then
moves.

## v0.8.0 — a close you can interrupt

Windhawk `@version` 0.8.0. The Win key, the Start button and the touch gesture
now interrupt the same transition the same way, and a close owns the panel from
its first frame to its hard landing instead of being one more animation a finger
can fight.

- **One ownership record.** A close is a serial, the menu it was accepted
  against, a phase (`settling`, `awaiting-hide`, `done`) and a 600 ms repair
  bound. Fifteen readers ask it instead of a clock. The bound's ending is a
  repair: it releases the published direction and leaves the dismiss command and
  the recovery ticket naming the hide that may still land, which is what the
  anti-heal rule waits on, so a slow hide (measured 562–593 ms) cannot land on a
  panel that was just restored under it.
- **A grab, once.** A down during a retract is admitted only when it lands in the
  band or on the surface the retract still shows — both halves of the panel
  rectangle, from the run's own geometry. It spends one transfer at 24 DIP: a
  take that reverses the retract from its live position in one take, with the
  finger's offset kept; a band pull whose surface is gone is the same open
  request a press makes, and follows the finger from the entry; anything else
  consumes the contact with no open right until it lifts.
- **A landing adopts no finger.** The landing serves only an explicit open
  request that lost its race to an entered `Dismiss`. A hand resting through a
  retract no longer resurrects the menu afterwards, and a consumed contact ends
  only on its lift or a report that proves another contact — never on the
  digitizer's quiet through a hide.
- **Two diagnostic lines.** `exec dismiss enter ... at=` dates the launcher call
  from before it was entered, and `motion sample` records the position and slope
  a takeover read. Both are written only with `DiagnosticTrace`.
- **A tracker for the evidence.** `tools/panel_edge_track.ps1` samples the
  panel's trailing edge for any of the four docks, which is how every claim above
  was judged in pixels rather than in logs.

Still owed, and named in `docs/limits.md`: a pen by hand, and a consumed finger
that is silent through a landing and then moves (the virtual digitizer cannot
deliver that report).

## v0.7.0 — one mod, two processes

Windhawk `@version` 0.7.0. The pair is replaced by one mod, installed once and
loaded into both `explorer.exe` and `StartMenuExperienceHost.exe`; the role is
picked at init from the process image name, so the halves can never be installed
or updated separately.

Answers to the collection's review of the two submissions:

- **Symbols instead of addresses.** `StartUI::App::PrepareRootElement` and the
  thirteen `twinui.pcshell.dll` functions are resolved by public symbol on the
  mod's own threads, so the PDB fetch never blocks a host. When the private
  launcher layout does not match the reference image, the offset-dependent
  capability stays off and presses run natively.
- **No image pin.** Unloading joins every owned thread, runs the UI teardown turn,
  destroys the launcher and raw-input windows, and unregisters both fixed classes
  before the channel closes; there is no "residual" success.
- **No periodic tick.** Deadlines arm a single one-shot and the open/close state is
  a WinEvent with a bounded fallback. Idle with the menu closed measures about 9
  wakes/s and no DWM queries, down from about 190 wakes/s and 32 queries/s.
- **A tap does not open Start.** An in-band contact only records a candidate; the
  show is requested after 24 DIP of inward travel. The shell's own edge gesture
  remains the fallback.
- **A lean channel.** Five named objects and a 216-byte record; the version word is
  gone (the namespace is the ABI identity) and so is the debug command channel.
- **A user-facing store page.** The README block describes the gesture, the two
  settings and the limits; the design notes stay in this repository.
- **Close identity.** A landing is bound to its dismiss command and open
  generation; a confirmation that disagrees with the cloak sample is retained for
  the transition within a bounded wait, and a fresh open retires it.

The trace file is no longer written by default: every decision line goes to
`Wh_Log`, and the per-role file is written only while the `DiagnosticTrace`
setting is on (capped at 20,000 lines).

## v0.6.2 — M3.3

Start 0.44.0, bridge 0.25.0, channel v7 (744 bytes). Signed off after six review
rounds and a real-machine pass.

One `PanelController` owns progress, velocity, target and phase; every trigger is
an adapter over the same `Begin/Move/End/Commit`, so a key press is the
degenerate drag. The bridge runs the requested action on the launcher thread and
never writes the launcher's state.

- Cancelling an armed dismiss leaves its cancellation result available to Start,
  so a later unrelated hide is not mistaken for the close that press caused.
- A successful `Dismiss` call is not a confirmed hide: the executor owes a 300 ms
  visibility confirmation and reports `hidden`, `visible` or `unknown`.
- A record dispatched past its deadline is answered without calling the launcher.
- Interception requires a live Start peer and a proven executor: the recorded
  launcher thread, a window this activation created on it, a running worker and
  the callable primitive the decision needs. Otherwise the press passes to
  Windows, which then toggles exactly once.
- Ships with `tools/smoke.ps1`, the one-pass regression smoke over the four
  triggers; two of the review defects were caught by it and by nothing else.

Limits that are part of the contract: Windows owns when the first panel frame
appears (measured worst case 245–268 ms after `ShowStartView`, after which the
panel catches up to the finger); the 600 ms peer-loss detection budget is not a
delivery guarantee, so a press already intercepted can be lost if Start exits;
the panel's contents still receive touches at their original positions while it
is dragged. The real-machine pass covers one finger end to end, two fingers in
both lift orders and the auto-hide reveal after a fresh logon; a pen by hand
remains a manual check. See [docs/limits.md](docs/limits.md).

