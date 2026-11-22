# Test tools and measurement artifacts

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). Member lists with more detail: [../tools/AGENTS.md](../tools/AGENTS.md) and [../artifacts/AGENTS.md](../artifacts/AGENTS.md).

## Tools

- `tools/screenshot.ps1` — full-screen PNG in physical pixels. The process is
  switched to per-monitor-v2 DPI awareness first, otherwise a 200% display only
  captures the top-left quarter.
- `tools/start_menu.ps1` — reports or forces the menu's open state. Uses
  `DWMWA_CLOAKED`; `IsWindowVisible` stays TRUE while the menu is hidden.
  `-EnsureOpen` invokes the taskbar's Start button (`winapp ui invoke "开始" -a
  explorer --type Button`) up to three times at 1800 ms; on this auto-hidden
  taskbar all three left the menu cloaked, while a `VK_LWIN` tap opens it and
  the `START hwnd=... cloaked=... open=...` line reports it correctly.
  `-EnsureClosed` is one `VK_LWIN` tap.
- `tools/inject_swipe.ps1` — inject a touch swipe (virtual digitizer) through
  `tools/touch_common.ps1`; `-DryRun` prints the sequence instead of injecting.
- `tools/touch_common.ps1` — the one injection lifecycle every tool dot-sources:
  DOWN, UPDATE, a hold at 5 ms, and one UP. `End-Contact` owns stop, join and UP
  in that order, so no UPDATE can follow the release, and it records the UP
  timestamp immediately before the injection. A join that times out sends no UP;
  a failed UP keeps the contact open and reports it, and `ReleaseMs` stays 0 in
  both cases, so a trace shows that no release happened. `touch_hold.ps1` and
  `follow_hold.ps1` call `End-Contact` alone, so a failed stop cannot skip the UP
  through a separate call.
- `tools/touch_hold.ps1` — drag to a point, hold with the shared 5 ms
  keep-alive, capture, then release with `-ReleaseAtEnd`; `-Lift` is kept as an
  alias of it. The keep-alive runs through the capture, so the frame shows a
  live contact.
- `tools/follow_hold.ps1` — the same capture for the reveal follow from the
  screen edge, also keeping the 5 ms keep-alive running while the screenshot is
  taken: the virtual digitizer stops reporting during a plain hold, unlike the
  real panel. `touch_hold.ps1` starts the same keep-alive, so either tool can
  photograph a live hold.
- `tools/inject_swipe_holds.ps1` — swipe with pauses at waypoints; each pause is
  a real 5 ms hold, not 50 ms of silence.
- `tools/measure_swipe.ps1` — samples the Start window rect while a swipe runs
  and writes `artifacts/start-window.csv`; it asks for 8 ms and this host's
  15.6 ms timer quantum delivers ~16 ms, which the file itself shows.
- `tools/probe_cursor_follow.ps1` — polls `GetCursorPos`/`GetCursorInfo` while an
  injected touch drag runs and writes `artifacts/cursor-follow.csv`.
- `tools/native_flyout_frames.ps1` — capture the native Quick Settings flyout
  (`Win+A`) as BMP frames at ~10 ms, with the `ControlCenterWindow` visibility
  poll that anchors t=0. The stamp is taken immediately before each `BitBlt` and
  the copy's own cost is recorded as `cap=`.
- `tools/native_flyout_track.py` — fit each captured frame's vertical offset
  against the settled frame and print `t -> dy`.
- `tools/fit_settle_spring.py` — model the shipped spring against
  `artifacts/native-flyout-open.csv` (slowest-root duration, end-value
  normalisation, the shipped damping by default) and print the residuals;
  `--model legacy` repeats the old free-period fit, and the report states that
  the captured data does not identify the damping. It prints the model's first
  sampled frame and its 10 ms position, and labels the initial tangent
  (`velocity * 10 ms`) a straight-line extrapolation, not a position on the
  curve.
- `tools/settle_track.ps1` — pull the menu half open, hold, release, and track
  the panel's bottom edge for 500 ms; its release time comes from `End-Contact`,
  so `t=0` is the UP and not the end of the preceding capture. Its start point
  and scan band are the top-taskbar geometry.
- `tools/panel_edge_track.ps1` — sample the panel's trailing edge (the side away
  from the taskbar) in physical pixels while the menu opens or closes: one 96 px
  band inside the panel, one reused DIB, every frame diffed against a
  closed-menu baseline, each `t` stamped before its `BitBlt`. The axis, panel
  rectangle, band and work area come from the run's own `reveal panelTrail`
  line, `Distance` from `gesture geometry axis=... dist=`, and the scale from the
  `dismiss` bar thickness over half the band; a missing field refuses the run.
  Progress uses the mod's own `1 - (edge - trail) / (dir * distance)`. `-WinTap`
  opens and closes with the Win key, `-Observe` leaves the trigger to the caller,
  and `-TriggerAtMs`/`-Trigger` fire a qualified 25 DIP band pull or another tap
  at an offset (from the close tap in win-tap mode). On the live left bar it reads
  2105 px with the menu fully open (trail 1053 DIP × 2) and follows the retract
  down to the entry edge (~98-175 px) before the window hides.
- `tools/ipc.ps1` — the read-only view of the channel; see
  [ipc.md](ipc.md) for the protocol and the records. `-State` prints `peerEpoch`
  beside `peerTick`/`peerState`, which is how a re-enable is told from a
  still-running activation. There is no command publisher any more.

## Touch producers: real digitizer vs injected contact

`tools/inject_swipe.ps1` calls `InjectTouchInput`, but the contact reaches the
shell as a pointer, and a press that dismisses the open panel is served by the
shell's own click-away (`HideStartAndCortana`, method 7), which the bridge now
runs through the one toggle decision: `own toggle decide trigger=clickaway ...
kind=close dir=1`, then `intent kind=close trigger=clickaway ... method=7`. The
mod's own gesture dismiss (`close dismiss kind=...`, the `RequestBridgeDismiss`
at the close's end) is therefore exercised only by a real finger on the
digitizer. The smoke labels the row's producer rather than calling both HID.

A four-trigger smoke that checks only "the menu closed and an `exec dismiss` ran"
cannot tell the two producers apart: both paths end in the same command. The
producer is the line before it. On a real panel the HID close logs `close
accepted ... kind=dismiss`, then `close dismiss kind=<...>` and `exec dismiss
request ... method=1 open=<generation>`; no click-away decision belongs to it,
and the bridge must not answer it `superseded menu=...` (the `open=` is the
generation read by `SwipeIpcReadOpen`, not a tick). On the injected path the
bridge logs `own toggle decide trigger=clickaway ... kind=close`, the intent
line, and the Start side logs `intent recv seq=... kind=close trigger=clickaway
...`, with no `close dismiss kind=` line. The manual check is that pair of lines,
not only the close's outcome.

## Round-2 runtime checks

Each line is what a run must show for one review item; the code half is in
`mods/` and the static limits are named in the item's notes.

The outcome words are distinct and are not interchangeable: **passed native**
(the shell's own path served the press), **published** (an intent or executor
record left this side), **consumed** (the other side applied it),
**expired** (the publisher's budget ran out and a repair ran), and
**superseded** (a newer command or activation replaced the record before it
could act, or before its confirmation could land).

- **A — unload quiescence.** Enable, disable and enable again in the same PIDs.
  `bridge.log` shows `exec worker joined`, `exec window destroyed`, `exec window
  class unregistered` and `ipc close` on the first cycle, then a fresh
  `exec window hwnd=...` on the second; both hosts keep running, and the old DLL
  is gone from the `explorer.exe` and `StartMenuExperienceHost.exe` module lists.
  There is no deferred close and no re-enable refusal any more: a proof that
  cannot be obtained (a worker, a window, a delegate) blocks the unload instead
  of returning success. See [lifecycle.md](lifecycle.md) for the order.
- **B — activation identity and availability.** Disable the Start mod while a
  close is running; `bridge.log` must show `own pass trigger=... reason=no-peer`
  (or `exec pass hide method=... reason=no-peer`) from the moment of the disable,
  and the next `ipc peer alive reason=consumer` must carry a **new** `epoch` and
  its `touch=`/`pen=` words. A press during the gap is **passed native** once; a
  press the bridge already swallowed is not.
- **C — detection budget.** With the Start mod disabled, `own pass trigger=...
  reason=no-peer` appears at the first press; `own press latch dropped
  reason=peer-gone` appears when a latched press outlives its peer.
- **D — capability.** On the shipping build, a click-away close logs
  `own toggle decide trigger=clickaway ... kind=close` and then its intent only
  after the executor exists; with the
  worker refused, `exec pass hide method=7 reason=no-executor` (or
  `no-window`/`wrong-thread`/`no-primitive`) and the shell's own close runs.
- **E — expiry and confirmation.** A `dismiss` whose dispatch starts at or past
  `executeAt + 400 ms` logs `exec dismiss seq=... expired` and no
  `exec dismiss ... hr=`; every successful close is followed by `exec dismiss
  confirmed seq=... visible=hidden` (or `unconfirmed`/`unknown`), and the elapsed
  `after=` is the measured time, not 300. A close superseded before it runs logs
  `exec dismiss ... superseded` (not `expired`). An `unconfirmed`/`unknown`
  dismiss produces `exec dismiss unconfirmed|failed command=...` on the Start
  side and a released direction. With the channel closed, `exec show request
  skipped ... reason=no-channel` and `intent open refused ... reason=no-executor`
  replace a claimed open. **The show timeout emits both lines**: when 400 ms pass
  with no ack, the Start side publishes `exec cancel request target=0
  reason=show-expired` (unless a pending dismiss already owns the executor record)
  and logs `exec ack timeout seq=... kind=show open=... (resync from cloak)`. The
  cancel repairs the executor's record; it does not suppress the publisher's own
  timeout line. A show the bridge answers `superseded` before its deadline clears
  its slot in `ApplyExecAck`, so that one logs `exec show superseded command=...`
  and no timeout. **A superseded dismiss produces no confirmation**: the
  replacement's probe answers, and the old sequence is answered `superseded` by
  the bridge.
- **F — latch count.** The test needs the shell's hide in flight: press to close,
  then two presses while `InternalState` is 9/11 (or while our `Dismiss` call is
  live). Both are latched; the second is appended, not replacing the first
  (`own press latched ... owed=1`, then `own press latch owed count=2`). The serve
  publishes the first open (`own press served ... owed=2`), queues the remainder,
  and the ack barrier makes the next press the opposite direction. That close is
  held until the Start side publishes the open generation it belongs to
  (`own press held ... reason=open-gen`, then `own press open gen ready`) so it is
  not ended by the fresh-open rule; the final
  state is flipped twice (open, then closed), not left open by a lost press. A
  latch filled to four entries drops the arriving press
  (`reason=latch-full`) rather than executing it out of order. The old
  single-slot supersession test (`reason=superseded newer-press`) no longer
  exists: a newer press is an owed toggle. A latch whose dismiss failed logs
  `reason=dismiss-refused` and stops retrying.
- **G — landing order.** With Win+Tab immediately after a Win press, the landing
  logs `intent reconcile seq=... kind=external (before landing recovery)` and no
  `intent open recovered`. On the shipping build the bridge now forwards
  `Dismiss(5)` while it owns the panel: the line is
  `exec pass dismiss method=5 caller=wintab state=... reason=external`, the Start
  side answers `exec cancel request ... reason=external` and
  `close ended ... reason=external`, Task View is the foreground window, and the
  next Win press publishes exactly one open (`own press published ... kind=open`,
  one `exec show`, `own state event=shown`).

## Round-3 runtime checks

- **Readiness floor at the resync boundary.** A post-call resync of a null or
  `0x100` state logs `own state resync skipped reason=state-not-ready raw=...`
  and leaves the cached direction as it was; the valid state logs the usual
  `own state resync cached=... shell=...`. On the shipping build the sentinel is
  visible only before the state is constructed, so this is normally verified by a
  harness that includes the shipping file (outside the repository) and calls
  `OwnResyncFromState` with null, `0x100` and a fake interface: only the last may
  invoke `IsVisible`.
- **Ticket identity.** Cancel a close from a menu that has only a local slot
  (touch close, then a Win press before the slot is requested): the log must say
  `intent open cancel ... ticket=0`, and a later unrelated hide of the same menu
  must produce no `intent open recovered`. With an executor command pending, the
  ticket appears as `ticket=1 ticket_seq=... ticket_expire=...` and either its
  landing serves it or `intent recovery ticket retired ... reason=expired`
  appears after the deadline; a dismiss whose executed ack already arrived also
  arms one (`ticket_seq` = that sequence), because its call returned but its hide
  may still land. The same rule arms the ticket for a qualified pull that cancels
  a published command (`ArmCloseCancelTicket`), so a keyboard close interrupted by
  a finger shows `ticket=1` on the pull's cancel exactly as the press's does. A
  refused/expired/superseded answer for the
  **ticket's** sequence retires it even though the pending slot was already
  cleared by the open (`intent recovery ticket retired seq=... reason=
  superseded|refused|expired`).
- **Publisher epoch.** Restart the Start host immediately after a close request:
  the old dismiss must log `exec dismiss seq=... superseded epoch=... now=...`
  (or `exec show ... superseded`) instead of calling the launcher, and the new
  activation's attach logs `exec cancel request target=0 reason=attach`. A record
  whose menu the section has already left logs `exec dismiss ... superseded
  menu=... now=...`. An already-entered call cannot be recalled; the landing
  rules handle it.
- **Latch epoch.** A Start restart between admission and latch creation must not
  serve the old press: the serve logs `reason=peer-changed` with the old epoch.
  A hidden event that runs off the launcher thread logs `own press latch arm
  posted`.
- **Unconfirmed dismiss.** With the window kept visible after a dismiss (hold it
  open), the log shows `exec dismiss unconfirmed ... visible=visible at_call=...`
  and the Start side follows with a released direction; the next press opens.

## Round-5 runtime checks

- **Before-take pause (A).** Suspend the Start host, send one Win press while its
  heartbeat is still fresh, let the 1000 ms bound run, then resume it. The bridge
  logs `own press ack timeout ... decision=replay reason=revoked`, runs the native
  fallback once, and the section's `intentSeq` stays odd (`tools/ipc.ps1 -State`
  prints `unstable`); the resumed activation logs no `intent recv` for that
  sequence. Exactly one toggle.
- **Claimed but unacked (A/C).** With the consumer paused after the claim (a test
  harness that sleeps after the copy), the timeout logs `decision=consumed
  awaiting=ack` and no `own press fallback`; the late ack releases the queue.
- **Restart with an outstanding intent (B).** Publish one or two presses, kill and
  relaunch the host before it takes them. The watchdog sees the replacement's
  epoch and logs `decision=replay reason=epoch-changed`; the queued press drains
  (`reason=peer-gone`) rather than waiting for an ack the replacement never sends,
  and the next press is one intent with one toggle.
- **Ack, then the second press (C).** Two presses against a paused consumer: the
  second is not published at the 1000 ms take (`decision=consumed awaiting=ack`),
  only after `own press ack`, and its kind is the opposite of the first (close
  then open, or the reverse), not a second same-direction intent.
- **Click-away inside `Dismiss` (D).** A click-away delivered on the executor's
  `Dismiss` stack logs `own hide clickaway ... depth=1 reason=executor`, then the
  one dispatcher's `own toggle decide trigger=clickaway ... kind=open` and
  `own press latched` (or a served press), never `exec pass hide ...
  reason=executor` on the native Hide.

## Round-6 runtime checks

- **Claimed 1500 ms (B).** Claim press 1 and pause after the copy for longer than
  the queue bound, with its heartbeat kept fresh. The timeout logs
  `decision=consumed awaiting=ack`; at 1500 ms there is no `own press fallback
  ... reason=queue-wait` and no press 2 publication. When the pause ends, press 1
  applies and acks, then press 2 is dequeued `reason=ack` and publishes the
  opposite direction. Order preserved; the bound restarts when press 1's slot is
  released.

## The one-pass smoke

`tools/smoke.ps1` drives the four triggers through their real paths and judges
each step by `start_menu.ps1`'s state plus the lines the two halves wrote in that
step: a VK_LWIN tap opens (`kind=open`, `exec show`) and closes (`kind=close`,
`exec dismiss`); an injected edge pull opens only with `reveal pull qualified`
and the executor's ack for the exact show sequence the Start side published; an
injected panel drag closes only with its producer line (`close dismiss kind=...`
for a real digitizer, the click-away decision and its `method=7` intent for the
injected pointer) and the executed dismiss for the sequence that producer
requested; and a desktop click closes only with `own toggle decide
trigger=clickaway ... kind=close`, the matching method-6 dismiss and `win close
observed` — a close the shell performed natively fails that row. The touch close
row names its producer (`producer=hid` or `producer=injected-pointer`); an
injected-pointer row is never quoted as HID evidence.

Three mixed-trigger rows then interrupt a close with a different trigger: a Win
close with a desktop click, a Start-button close with a Win press, and a gesture
close with a desktop click. Each needs the close's own decision (or `close
accepted ... kind=dismiss` for a gesture the click interrupts before its
`close dismiss kind=` line), the reopen decision (`own toggle decide ... kind=open
dir=2`), the cancel that retired the close (`exec cancel request target=...
reason=open-intent`, or `close dismiss cancelled kind=...`), `close ended ...
reason=open-cancel`, no `win close observed`, and a cancellation `from=` below 1
(the panel had already left its rest). The final cloak is read only after that
close's own dismiss deadline — the request's `executeAt`, or the gesture settle's
end plus 30 ms — so a command that was not actually cancelled fails the row. The
row reports the interrupting producer, `from=` and the deadline. Any click row
skipped by `-SkipClickAway` — the standalone click-away and both reversals — is
printed as `skipped: ...` and not added as a passed row. These are the rows that
fail when a click or a press re-closes instead of reversing; the older smoke
passed while the click did exactly that.

The last step fails on any error-shaped line in the run —
a non-zero
`hr=`, `timeout`, `refused`, a lost press or a dropped latch. Run it before
handing the tree to a review: two of the defects fixed during the M3.3 review
rounds were caught by it and by nothing else (a dismiss whose identity guard
compared a generation with a tick, so every close was refused, and an ACK
ordering that hid the answer Start needed).

The trace rotates at its 20,000-line cap, so the script does not trust a line
index: every mark is `GetTickCount64` and every read re-opens the log and keeps
the lines at or after it. The monitor scale is kept fractional (1.25 at 125 %,
1.5 at 150 %, 2 at 200 %), because rounding it puts every injected point off the
band or off the panel.

**The trace must be on for the run.** The mod writes the lines to disk only
while `DiagnosticTrace` is on; `Wh_Log` always receives them, but Windhawk's own
debug logging is a separate switch and does not create the files. The smoke
checks, after its first trigger, that `renderer.log` or `bridge.log` was written
since the run started; if neither was, it prints

```
windhawk-cli mod settings set local@swipe-for-10start DiagnosticTrace=true
```

and exits non-zero. The command needs an elevated shell (the settings live
under HKLM). A file from an earlier session is stale and is rejected the
same way, so the check is about this run, not about the file's existence. With
the trace on, no other switch is needed.

The band-tap rule has its own manual checks, because a tap that opens nothing is
not a row the four triggers can express: tap the taskbar background, the clock,
the tray's empty area and the Start button with the menu closed. The first three
must log `reveal admit ... ok=1 reason=admitted` with no `reveal pull qualified`,
no `reveal show request` and no `exec show`, and leave the menu cloaked; the
Start button must log one shell open (`exec show ... method=11`) and no band
claim. Then pull 23 DIP and 25 DIP and lift: the first requests nothing from this
mod (the shell's own gesture may open it), the second requests one show. Finally
hold a contact still in the band through a Win close: its down is admitted as a
grab (`reveal admit ... reason=grab`), it never reaches 24 DIP, and the landing
logs `win close observed muted=0` and leaves it alive with no `exec show`; if that
finger then keeps pulling past 24 DIP, the same contact opens the menu (below).
A
band pull that does reach 24 DIP after the retract has passed the entry is the
same open request a press makes: it logs `reveal pull qualified ... grab=open
window=open` and the menu follows the finger from the entry (`reveal takeover ...
live=0`, `open follow ...`), with no `intent open cancel` settle and no
`exec show`; the same pull held on the visible panel *before* the crossing
cancels the close and follows from the live position (`grab=taken`). The hidden
window of the same request (`window=hidden`) asks for one show and binds at the
entry.
A down on the panel surface (not in
the band) that reaches 24 DIP at or below the entry logs
`reveal pull qualified ... grab=refused reason=offsurface` and opens nothing.

What it does not cover, on purpose: the click-away step waits for the executor's
`exec visible ... visible=visible` because a click inside the window's presentation
(state 1/3) is swallowed; the open step uses the VK_LWIN tap because the
auto-hidden bar cannot be opened through UIA; and the producer of an injected
panel drag is not fixed. On this build the injected drag produced the real HID
close (`producer=hid`, `close dismiss kind=dismiss`), while other runs and the
earlier captures produced the pointer-derived click-away; the row accepts both and
names the one it saw. The HID producer is not proof of a human finger, and an
injected-pointer producer is not HID evidence. A real finger's panel drag is
still the field check for the HID path: capture `close dismiss kind=...` with its
matching `exec dismiss request ... method=1 open=<generation>` once by hand. A pen
by hand is still owed; one finger end to end, two fingers in both lift orders and
the auto-hide reveal after a fresh logon passed on the shipping build.


## The panel's trailing edge

A `win close observed` line says the window hid; it does not say whether the
panel turned around on the way. `tools/panel_edge_track.ps1` answers that from
pixels: it places one 96 px band inside the panel from the run's own
`reveal panelTrail` rectangle, diffs every frame against the closed-menu
baseline, and reads the extreme changed coordinate along the trailing axis. On
the live left docked bar the edge is vertical and the scan runs along x, which
`settle_track.ps1` does not do (its y band and bottom-edge read are the
top-taskbar layout).

Baseline runs on the installed 0.7.0 build, 2026-11-21, left docked bar. The mod's own
numbers give trail 1053 DIP, `distance` 1392 DIP, scale 2.000, so a fully open
panel's edge is 2106 px; every run measured 2105 px (`at-close=2105`,
`measured-max=2105`). Two runs of each of two sequences, `tc` in ms after the
close tap:

| `tc` of the first sample at or below | 2000 px | 1500 px | 1000 px | 500 px | 200 px | last visible | hidden |
| --- | --- | --- | --- | --- | --- | --- | --- |
| plain Win close, run 1 | 274 | 440 | 511 | 551 | 567 | 127 px @ 567 | 575 |
| plain Win close, run 2 | 261 | 442 | 508 | 550 | 564 | 98 px @ 564 | 573 |
| pull at `tc` 150, run 1 | 264 | 438 | 503 | 545 | 561 | 175 px @ 561 | 567 |
| pull at `tc` 150, run 2 | 275 | 443 | 492 | 540 | 556 | 175 px @ 556 | 564 |

Each run held 110-119 samples at a mean 14.0-15.2 ms period (3.6-5.0 ms minimum
while the panel is hidden, 51-59 ms worst stall when the close lands), and the
edge first leaves 2105 px 79-84 ms after the tap — the settle's first frames are
the slow ones. The visible series ends at 98-175 px, progress 0.279-0.306: the
window is cloaked before the panel slides the last 48 DIP behind the bar, so no
frame exists for the end of the travel. Those samples are `edge=-1`, never
progress 0.

The second row pair is the interruption baseline as the mod behaved before a pull
could abort a close. The pull is admitted while the
close is in flight (`reveal admit ... reason=deferred`; that build's name for
the grant, now `reason=grab`), qualifies at 25 DIP and
is released inside the close. Every retract landmark of the pull runs stays within
14 ms of the plain runs — the two plain runs alone differ by up to 13 ms from each
other at the same landmarks — and no sample shows a plateau or a reversal. That
was the old rule: the pull did not interrupt the Win close, because only a local
gesture-close slot was cancellable. What it leaves behind is a
reopen: the panel is visible again at `tc` 710 and 736, after the landing, which
that build logged as `reveal reopen after close pull=0 late=1` — the release
record, not a live contact, took the menu back. These four CSV pairs stay as the
grab-path reference; a pull that interrupts a Win close now logs
`reveal takeover ... live=1` in the same millisecond as the qualifying report,
and the retract reverses there instead of reaching the landing. The delayed
reopen itself is gone: under the current rule a grab that the close outruns is
refused and consumed, so the same capture would end with the menu closed and
`win close observed ... muted=1`.

### The interruption, measured

The same two sequences against a build where the pull and the press can abort a
close whose dismiss is still armed, run 2026-11-21 on the left docked bar with the
second trigger at `tc` 150:

| run | retract floor | reversal back at 2105 px | the decision it logged |
| --- | --- | --- | --- |
| pull, `panel-edge-interrupt-pull-1.csv` | 1996 px (p 0.962) at `tc` 265-313 | `tc` 463 | `exec cancel request target=24 reason=pull`, `motion sample caller=pull p=0.962`, `reveal takeover from=0.962 offset=-0.611 live=1`, then `exec cancel seq=26 target=24` at the bridge; no `win close observed` |
| Win tap, `panel-edge-interrupt-wintap-1.csv` | 2045 px (p 0.979) at `tc` 208 | `tc` 363 | `own toggle decide ... kind=open dir=2` 141 ms after the close tap, `exec cancel seq=38 target=36`, `intent open cancel from=0.976 ticket=1 ticket_seq=36`, `settle style=win11 dir=open vel=0.00 dur=160 remaining=34`; no `win close observed` |

Both runs ended with the menu open (`STATE at exit cloaked=0 open=True`), against a
pre-interrupt baseline that reached 98-175 px and hid. The close had retracted 38 px
(pull) and 60 px (Win) of its 2105 before the interruption: the pull spends its own
24 DIP before it can qualify (~130 ms of a 520 ms close) and the press was dated
141 ms after the tap. The reversal's first visible frame lags its decision line by
~55-70 ms because the take pins the panel and the replacement settle starts at
zero speed; the capture's own ~13 ms period is part of that.

The tool's `SUMMARY min=`/`start=` cover the whole capture, including the setup
close that precedes the marked one, so the measured window's floor is the value
between `at-close` and its reversal — not the summary's minimum.

### The boundary: a retract already off screen

`panel_edge_track.ps1` cannot read the trailing edge on the bottom bar (`axis=3`)
yet — it warns that the widest measured edge does not reach `trail*scale` — so
this pair is read from the mod's own lines, on the bottom auto-hidden layout
(`distance` 852 DIP, entry 0.310), with the pull's qualification arriving at
different depths of the same 387 ms close:

| pull qualifies at | `progress` at the take | what the mod did |
| --- | --- | --- |
| 150 ms after the tap | 0.934 | `exec cancel request ... reason=pull`, `reveal takeover from=0.934 live=1`; no `win close observed` |
| after the settle ended (~437 ms) | at or below the entry | no cancel; `reveal defer released candidate=1 down=1`, `win close observed`, `reveal reopen after close pull=1 late=0 entry=0.310` |

The second row is why a cancel needs a panel on screen: a takeover at or below the
entry would open the menu from an invisible start, so the close lands. That run
predates the current rule, which serves no touch open of its own at a landing: a
grab on the retract's surface is
refused (`reason=offsurface`) and consumed, the landing logs
`win close observed ... muted=1`, and the menu stays closed; a band down at the
same depth is the open request instead (see the band pull's open request below).
The table cell keeps
that build's own line as the measurement it is.

Frames were captured at 200 % DPI with the tool's own DIB; `t` is the capture
start, so a frame can lag its stamp by one compositor interval, and the CSV's
`edge_dip` is the measured edge divided by the derived scale.

### The ownership change, measured (P1)

A close now owns the panel to its landing: a down during the retract is a grab
only when it lands in the band or on the surface the retract still shows, the
grab's one transfer attempt is spent either way, and a landing adopts no finger.
Four probes on the bottom auto-hidden bar, 2026-11-22, with the installed build
(`axis=3`, open edge 558 px, entry edge 1704 px, entry progress 0.363, work-area
entry 0.310):

| probe | result |
| --- | --- |
| band grab at `tc` 150 | `reveal admit ... reason=deferred`, `reveal pull qualified ... grab=taken`, `reveal takeover from=0.931 ... live=1`; the edge reverses at 692 px and ends at 558 with the menu open (`panel-edge-p1-early-1.csv`) |
| band grab at `tc` 400, qualifying after the settle | `grab=refused reason=offsurface`, `win close observed muted=1`, no `reveal reopen`, no `exec show`, `STATE at exit open=False`; the edge runs monotonically to 1704 and stays (`panel-edge-p1-refused-1.csv`) |
| down in the vacated area (720,450 DIP, against a panel whose trail edge was at 625 DIP) | `reveal admit ... ok=0 reason=offsurface`, the close lands, no show, menu closed |
| press during a live band pull, finger held through the hide | `intent close trigger=win`, `win close observed muted=1`, no touch show after the landing, and the release wrote no record (no `reveal candidate released`) |

The third row caught an inverted sign on the way in: the same down was first
admitted (`reason=deferred`) and took the close over (`grab=taken`, `live=1`),
which is the "menu chases the finger out of the vacated area" the owner reported.
The hit test compares the down's mapped progress with the panel's live one: at or
below it, the panel's trail edge is at or beyond the point, so the point is on the
surface.

The P1 rows above were captured on the build that named the grant
`reason=deferred`; P3 renamed the same line to `reason=grab` and the flag to
`granted`, and P4/P5 gave a band down's late crossing the open request (the band
pull's open request below). The measured text is kept as captured.

`tools/smoke.ps1` stays green on this build.

### The one-take transfer (P2)

The grab's transfer now takes the panel's position once and pins it; the follow
binds to that same value on the same report instead of taking again, and no
settle stands between them. Measured with the same harness on the same bar
(`tc` 150, `panel-edge-p2-early-1.csv`): `exec cancel request target=12
reason=pull`, one `motion sample caller=pull phase=settle p=0.930`, then
`reveal takeover from=0.930 offset=-0.519 live=1` — no `motion sample
caller=activate`, no `settle` between the cancel and the binding — and the edge
reverses at 677 px and ends at 558 with the menu open. The settle that follows
the release (`dur=160 remaining=60`) is the ordinary open settle of the lifted
finger, not an intermediate one.

A grab that loses the race used to refuse and be consumed: `tc` 400 on the same
bar produced `grab=refused reason=no-close` (the report arrived after the landing),
no `reveal reopen`, no `exec show`, and the menu stayed closed
(`panel-edge-p2-late-1.csv`); that row is historical. A band down among those is
now the ordinary pull on a closed menu (see the band pull's open request below).
The press path still settles the panel back
(`intent open cancel from=...` and one `settle dir=open`), because a press has no
finger to bind to. P4 gave a band pull the same settle when the retract refused
it at or below the entry; the pull now keeps its finger and follows instead; see
below.

The `dispatch` refusal (`reason=dispatch`, a published dismiss at or past its
deadline) is unreachable in practice on this build: past the deadline the retract
is already at or below the entry, so `offsurface` answers first. It stays as the
serialisation rule that makes the promise conditional.

### The band pull's open request (P4, revised)

The P1/P2 rows above were captured before a band pull could ask for the open. A
grab whose down was in the band and whose 24 DIP crossing finds the retract
already past the entry is the same open request a press makes. The P4 capture
(`panel-edge-p4-late-open-1.csv`) recorded the first parity: `reveal pull
qualified ... grab=open from=0.000 ticket=...`, the press's cancel body
(`intent open cancel from=...`) and one `settle dir=open`; that reading is
historical. The request now shares only the cancel body: `grab=open window=open`
spends the grant and the same report starts the follow at the entry (`reveal
takeover ... live=0`, `open follow ...`), and a window the retract already hid
logs `window=hidden` and asks for the ordinary show. The refusal text above stands for
a down on the retract's surface and for the vacated-area down refused at
admission. One refusal transition, `ConsumeGrabContact`, still ends a refused
grab; the open outcome does not run it, and a landing does not either — a grant
still on its way to its 24 DIP report survives the landing. A band down's
`no-close` is that same open with nothing left to cancel:
`grab=open window=hidden entry=... reason=no-close`, the ordinary show, and the
follow binds at the entry on the uncloak.

Captured on the installed build the same day (bottom bar, trail 192 DIP,
`distance` 900, entry 0.213 at that moment), one row per route:

| route | measured lines |
| --- | --- |
| retract still shows the surface (`tc` 150) | `grab=taken`, `reveal takeover from=0.936 offset=-0.590 live=1`; the edge reverses at 499 px of 1704 and the menu ends open (`panel-edge-p6-early-1.csv`) |
| retract off screen, window still shown (`tc` 330) | `grab=open window=open entry=0.213 reason=offsurface`, then `reveal takeover from=0.213 offset=0.133 live=0` and `open follow ... live=0` — the follow starts at the entry and the finger drives it, with no settle (`panel-edge-p6-mid-follow-1.csv`) |
| close already landed (`tc` 400) | `win close observed muted=0`, `exec show request`, `reveal pull qualified ... grab=open window=hidden entry=0.213 reason=no-close`; the claim holds the entry, the uncloak binds the follow, and the menu ends open (`panel-edge-p6-late-open-1.csv`) |

The injected pull in the middle row releases at its own qualification — 25 DIP of
travel and no velocity — so its release runs the short-reveal close
(`open follow ... commit=0`, then `settle dir=close ... remaining=284`). That is
the designed outcome for a pull that short, not a lost open; a finger that keeps
pulling commits.

### A consumed contact and silence (P3)

A contact the close consumed keeps its mute through the digitizer's quiet: silence
never ends it while a close is in flight, and a contact with a Contact Identifier
never ends by silence at all. Measured through the hide with a live band finger: a
Win close during the pull landed with `win close observed muted=1`, and the finger
produced no `reveal admit`, no `exec show` and no reopen while it stayed down.

The remaining case — a consumed finger that is *silent* through the landing and
then resumes — cannot be reproduced with the virtual digitizer: an injected
contact that stops moving decays and answers `InjectTouchInput failed: 1460` when
it is touched again, so the instrument cannot deliver the report that would
matter. On this build such a report is inert when it names the consumed contact
(same Contact Identifier) and ends the mute only when it proves another one
(`hid consumed ended ... reason=new-contact|device`). A real digitizer is the
authority for that sequence, and it stays on the hardware list.

## Artifacts

The raw tables behind the Windows 11 fit are in `artifacts/`:
`native-flyout-open.csv` and `native-flyout-close.csv` are the per-frame
`t -> dy -> d` curves of the native flyout (9 ms frames, `d` normalised to the
panel's 578 DIP travel); both were captured with the earlier loop, which stamped
`t` after the `BitBlt` and its copy, so their times are late by that read-back (a
few ms). `settle-trace.csv` is the shipped mod's settle trace in both styles for
the same half-open hold release; it came from the earlier tracker loop, whose
clock starts when the UP injection returns and which stamps `t` after the
`BitBlt` and before the read-back, so its net bias is the `BitBlt`'s copy time
minus the UP call's own time, with no fixed sign — the current tool stamps before
its `BitBlt` and takes t=0 from the UP timestamp. Then
`cursor-follow.csv`, `start-window.csv` and
`gesture-shared.csv` are the cursor, window-rect and bridge-state dumps behind
the data-sources table. `panel-edge-wintap-1.csv`/`-2.csv` and
`panel-edge-wintap-pull-1.csv`/`-2.csv` are the four trailing-edge baselines
above (`t,tc,edge_px,edge_dip,progress`; `t` stamped before the `BitBlt`, `tc`
empty when the run has no close anchor). `artifacts/AGENTS.md` lists each file
with its bias.
