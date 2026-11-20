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
- `tools/read_shared_gesture.ps1` — reads the bridge's
  `Local\WindhawkStartGestureShared` mapping, optionally while injecting a swipe.
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
- `tools/ipc.ps1` — the debug channel's only command publisher; see
  [ipc.md](ipc.md) for the protocol, the v7 records and the commands it takes.
  `-State` prints `peerEpoch` beside `peerTick`/`peerState`, which is how a
  re-enable is told from a still-running activation.

## Round-2 runtime checks

Each line is what a run must show for one review item; the code half is in
`mods/` and the static limits are named in the item's notes.

The outcome words are distinct and are not interchangeable: **passed native**
(the shell's own path served the press), **published** (an intent or executor
record left this side), **consumed** (the other side applied it),
**expired** (the publisher's budget ran out and a repair ran), and
**superseded** (a newer command or activation replaced the record before it
could act, or before its confirmation could land).

- **A — unload acknowledgement.** Enable the bridge, disable it, enable it again.
  `bridge.log` shows `exec window destroyed` (or `destroyed directly`) and
  `ipc close` on the first cycle, then `activation state reset` and a fresh
  `exec window hwnd=...` on the second. A forced timeout must instead show
  `ipc close deferred` and the next enable `exec reinit refused: ...`, never a
  second `exec window hwnd` while the old one lives. A teardown that completes
  between the two enables is **accepted**: `exec teardown completed late` (or
  `exec teardown found a surviving window` and then `completed late`) followed by
  `activation state reset` is the success path, not a failure. See
  [lifecycle.md](lifecycle.md) for the test-build hold that stages the
  creator/reaper interleaving.
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
  `intent close trigger=clickaway ...` only after the executor exists; with the
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
  replace a claimed open. **A cancelled show produces no expiry line**: the
  cancel supersedes the record, so `exec cancel request target=0
  reason=show-expired` is the only line and no `exec ack timeout ... kind=show`
  follows. **A superseded dismiss produces no confirmation**: the replacement's
  probe answers, and the old sequence is answered `superseded` by the bridge.
- **F — latch supersession.** The test needs a **pre-existing latch**: press to
  close, press again while the hide is in flight to create the slot, then press a
  third time after the landing but before the 50 ms serve. The third press sees
  no hide in flight, so the dispatcher discards the slot
  (`own press latch dropped reason=superseded newer-press wait=...`) and the older
  slot is never served. A single close-then-press is not the test: the second
  press only creates the latch. A latch whose dismiss failed logs
  `reason=dismiss-refused` and stops retrying.
- **G — landing order.** With Win+Tab immediately after a Win press, the landing
  logs `intent reconcile seq=... kind=external (before landing recovery)` and no
  `intent open recovered`.

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
  may still land. A refused/expired/superseded answer for the
  **ticket's** sequence retires it even though the pending slot was already
  cleared by the open.
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
the data-sources table. `artifacts/AGENTS.md` lists each file with its bias.
