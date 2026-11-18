# Known limits, risks, and the hardware checks still owed

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md).

## Outstanding hardware checks

- **One physical finger.** A single down, drag, stationary hold and lift, with the
  log showing one owner, no early idle release, `vel=0.00` after the hold, exactly
  one release, and the next touch admitted at once. The virtual digitizer always
  sends coordinates and an asserted tip, so it cannot stand in for this.
- **Two physical fingers.** Keep one finger dragging while the other lands and
  lifts, then reverse which one lifts first. No position jump, no false release, no
  transfer of the tracked contact to an inactive slot. The recorded device dump
  shows the ELAN declares a Tip per contact collection, but slot reuse and report
  cadence are only establishable by hand.
- **Pen.** `TipSwitch`/`InRange` are decoded and a hover is discarded, but no pen
  has produced a report since the decode changed.
- **Taskbar on the right.** The left, top and bottom taskbars are verified on this
  machine; the right side was exercised through the same path (`axis=2`) with
  injected input only, never by hand.

## Known limits and risks

- The left, top and bottom taskbars are verified on this machine, and the right
  side was exercised earlier through the same path (`axis=2`). The dismiss axis
  and the panel band come from the taskbar itself, never from the Start window's
  rect. `SHAppBarMessage(ABM_GETTASKBARPOS)` is the primary source (`uEdge` and
  its docked rectangle, reported unchanged while the bar is hidden); the
  `Shell_TrayWnd`/`Shell_SecondaryTrayWnd` rectangle is the cross-check (the bar
  must be flush to the edge **and** thin across it, so a full-width bottom bar is
  not mistaken for a side one); the work-area inset names the axis only when it
  truly insets, because auto-hide makes it zero by design. An inconclusive
  probe keeps the last axis and band, logs once per undecided episode with what
  each source said (`dismiss axis=... kept, no source named an edge ...`), and
  retries on the next open or broadcast; it never silently means `Left`.
  Measured on this layout:
  `dismiss axis=3 (0=left,1=top,2=right,3=bottom) src=appbar appbar=3
  rc=0,1704,2880,1800 bar=3 rect=0,1798,2880,1894 work=0,0,2880,1800
  mon=0,0,2880,1800`. The band is the bar's strip extended by one bar thickness
  toward the desktop, the axis is part of the layout identity, and both the
  translation and the reveal anchor are axis-aware.
- The same band is the strip a reveal candidate may start in, and it is wider
  than the strip the shell's own edge gesture opens from (about 20 DIP). A pull
  that starts deeper is admitted, the shell does not open the menu under it, and
  the release logs `reveal candidate released unopened travel=...` and moves
  nothing; below 24 DIP of travel that line is silent, so a tap stays a tap.
  A pull the shell does open for and that still reached the menu without a
  candidate — a fresh process before the first snapshot carried a band, a
  refused admission, or a taskbar probe that named no edge — is adopted at
  24 DIP of travel instead of animated from 0. A contact that starts in that
  band while the menu is open is the opening pull's tail: while it stays at
  least 8 DIP away from the taskbar, measured from its own down point, it starts
  no dismiss and settles nothing (`dismiss skipped band tail travel=...`), and a
  press that starts on the panel takes the ordinary dismiss path from its first
  sample.
- A pull that lifts before the shell opens the menu is carried by one record for
  300 ms, and 300 ms is a measured bound, not a guarantee: the open arrived
  107-251 ms after the lift here, and a shell that took longer — a loaded
  machine, a cold start — finds no record and runs the keyboard settle from
  rest, so that pull pauses and then opens instead of carrying the hand. The
  record also needs 24 DIP of travel and a measured distance; a shorter release
  has nothing to carry.
- The scroll gate is a survey of at most six regions. A viewer beyond the sixth
  is dropped and only the deepest six are kept
  (`scroller overflow (kept 6 deepest regions)`), so a press inside the dropped
  viewer finds no region and starts a dismiss. A region whose `ViewChanged`
  registration cannot be made keeps the rect and offsets of the pass that read
  it, so its end-of-list numbers can go stale for the rest of that open.
- The reveal and the dismiss share the 20% (10% vertical) travel and the
  0.45 DIP/ms session-average thresholds, but their visual anchors differ (panel
  edge vs. drag start), so the same physical travel reads as a different
  `progress` in each; the decision uses travel, not progress.
- The panel trailing edge is measured from the root's visible child; if the menu
  layout changes (another panel becomes the first visible child) the anchor needs
  revisiting. The XAML coordinate space starts at `ClientToScreen` of the host
  window's client origin, not at the window rect and not at the work-area origin;
  the window rect alone is 95 px of error here.
- Progress is inserted on a 0.5 DIP movement threshold; a finer threshold costs
  extra property writes on the input thread. At 200% scaling 0.5 DIP is exactly
  1 px (1 DIP = 2 px), so the follow can still step by one pixel: the threshold is
  not sub-pixel.
- A new contact is chosen from a report's real contacts by proximity to the last
  raw position; two fingers moving together can confuse which one starts the
  gesture. Once tracked, another slot or another device cannot move or end it,
  and a count-only lift report does not end it either: the 18 ms idle does.
- Composition translation does not move XAML hit-testing: while the menu is
  dragged, the content still receives touches at its original position.
- After a disable/enable without restarting `StartMenuExperienceHost.exe`, the
  reused XAML root means no new `PrepareRootElement` and no renderer: a new
  contact is tracked but cannot drive the panel until the host restarts. The
  restart in the install instructions is the workaround, not just advice.
- A gesture close is a real Windows tap at the end of its retract: the shell's key
  handler performs the hide and updates its Start toggle. A Win press during the
  retract closes the menu early — the plan is current, so the bridge gives that
  Hide the keyboard lead — and the Start side drops the armed tap when the
  request arrives. A press just after that hide lands in the shell's own settle
  and is ignored; the toggle is not advanced, and the next press opens the menu.
  A press 50 ms after the menu reports closed opens it and animates the panel in.
- The bridge holds every `CStartExperienceManager::Hide` that is not a gesture
  close for the settle the Start mod answered with, plus 30 ms, and that wait
  runs on explorer's UI thread: a click-away close slides out too, and the
  taskbar is blocked for that time (409 ms for the 900 DIP panel, 560 ms plus up
  to 100 ms of answer wait when the answer never arrives). The `reason` argument
  is logged (`hide delayed reason=1 retract=520 lead=550` on the left taskbar of
  this machine, whose 1440 DIP travel hits the 520 ms clamp); the Windows-key
  close reported 1 on this build. The gesture path does not wait.
- The bridge's drain was reached, so it is measured, not hypothetical, and its
  marks are not a bound. An elevated agent disabled the bridge within ~2 ms of a
  trigger file and tapped Win 333 ms later, and the unload landed inside the
  439 ms hold: `hide reason=1`, `hide delayed reason=1 retract=409 lead=439`,
  `ipc unload drained waited=16 ms`, `ipc close`. The CLI does return early — it
  was measured at 30 ms — while the engine applies the unload about 440 ms later;
  the 16 ms is the held wait ending at `g_disabled`, plus the drain's 10 ms poll
  step. The drain is therefore reachable, measured at
  `waited=16 ms`; the `stalled` branch stays code-path only, because nothing
  available makes `Hide_Original` or `FileLog` hang. The two handles one pinned
  image keeps (the gesture mapping and its event) are below explorer's
  handle-count noise (±60), so that instrument cannot see a two-handle effect, and
  a version change leaves the older image's pair too. That limit is reasoned from
  the code, not observed as a failure.
- The close record is an 800 ms window. A gesture close publishes the plan at
  its release and the `tapped` record immediately before the tap; a dropped close
  publishes the cancelled kind at once, and the tap's deadline republication is
  only the fallback. A Windows-key close that lands after the `tapped` record is
  a second Hide under the same stamp and takes the keyboard lead; one that lands
  while only the plan is current is a keyboard close too. Only `tapped` passes a
  Hide without a wait.
- A gesture close that the user cancels by pulling the menu open again drops its
  armed tap before the shell is touched, so no `Hide` runs and the toggle is
  never advanced. The panel is already retracted and is driven purely by the
  follow from then on: it reappears at the mapped position under the finger
  (0.269 at the screen edge on this layout) instead of already being open.
- The root hook is gated, not opportunistic: the file version of `StartUI_.dll`
  must be exactly `10.0.22621.3733` **and** the target prologue plus its `E8`
  call target must match. Anything else logs one line
  (`startui root hook skipped build=... expected=10.0.22621.3733`, or
  `... prologue mismatch`) and installs no hook; without the root there is no
  renderer, so no gesture starts. Before either probe reads, it proves the
  inspected range lies in an executable section of the loaded image (DOS and NT
  headers, `SizeOfImage`, the section table); a range that does not logs
  `startui root hook skipped (target not readable)` or
  `startui anim hook skipped (target not readable)`, which is distinct from a
  build or signature mismatch. A different build needs a new RVA and new
  reference bytes.
- The digitizer mapping assumes the touch panel covers the primary monitor.
