// ==WindhawkMod==
// @id              start-menu-legacy-swipe
// @name            Legacy Start Edge Swipe
// @description     Touch or pen swipe from the taskbar edge opens and dismisses the legacy Start menu
// @version         0.24.0
// @author          doubao
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32 -lhid -Wall -Wextra -I"C:\Users\doubao\source\repos\swipe-for-10start\mods"
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Legacy Start edge swipe (reveal follow and dismiss)

StartMenuExperienceHost-side companion for ExplorerPatcher's legacy Windows 10
Start menu. A one-finger swipe from the taskbar edge opens the menu and makes it
follow the finger; a swipe toward the same edge slides the menu out and
dismisses it. The input layer reads raw HID touch and pen reports; it does not
use XAML pointers, DirectManipulation or the pointer message queue.

What it does:

- Hooks `StartUI::App::PrepareRootElement` (`StartUI_.dll`, RVA `0xAD730` on
  build 10.0.22621.3733, the build the RVA was derived from), captures the
  returned root after it loads and enables Composition translation. The hook is
  gated on that build: the file version of `StartUI_.dll` must match exactly and
  the target must carry the reference prologue and its first call target.
  Anything else logs one line, `startui root hook skipped build=...
  expected=10.0.22621.3733` (or `... prologue mismatch`), and nothing is
  installed.
- Hooks `StartUI::SplitViewFrame::OnGlobalAnimationRequested` (`StartUI_.dll`,
  RVA `0x4164C0`, same build) and can drop the request whose `AnimationType` is
  0 when the debug line `anim=0` sets the switch; the switch starts at `anim=1`,
  the original behaviour. The hook itself is installed on every load and passes
  every request through: it costs one detour per animation request, three
  vtable reads for the arguments and the first four `startui anim` log lines,
  and only an explicit debug command changes what it does. The close request
  (`AnimationType` 1) always reaches the original unchanged. The animation type
  is read from the event arguments with the same three int getters the original
  calls, not from a call argument.
- At every setup it gets the Start CoreWindow HWND via `ICoreWindowInterop`,
  compares `GetWindowRect` with the monitor work area and picks the dismiss
  direction: left if the window is anchored to the work-area left, then top,
  then right, then bottom. It logs `dismiss axis=...` once and publishes the
  axis, the window width/height and the window bounds in DIP for the input
  thread. Setup is not per open: the root is reused, so the same axis is
  refreshed on every menu open and on every work-area or display change (see
  below).
- Installs a `CompositionPropertySet` with `Progress` (animatable),
  `Distance` (window width or height), `DirX` and `DirY`, plus the
  expression
  `Vector3((1 - state.Progress) * state.DirX * state.Distance, (1 - state.Progress) * state.DirY * state.Distance, 0)`.
  `Distance`, `DirX` and `DirY` are rewritten in place when the taskbar moves.
- `Wh_ModAfterInit` starts one input thread. The thread owns one window and
  registers `RAWINPUTDEVICE{0x0D, 0x04}` (touch) and `{0x0D, 0x02}` (pen) with
  `RIDEV_INPUTSINK`, so it still receives reports while the Start menu is in
  front. The window is hidden and top-level rather than message-only, because
  message-only windows do not receive broadcasts: `WM_SETTINGCHANGE` with
  `SPI_SETWORKAREA` and `WM_DISPLAYCHANGE` are the events that move the
  taskbar.
- A taskbar move while the menu is open is caught by those two messages: the
  input thread logs `work area changed` and posts one UI-thread task that
  re-reads the dismiss axis, rewrites `Distance`/`DirX`/`DirY` when either the
  axis or the extent moved, and re-measures the panel edge. Nothing polls; a
  layout that has settled is never re-measured.
- The gesture geometry is refreshed on the menu's own closed-to-open
  transition too. `StartUI::App::PrepareRootElement` does not fire on every
  open (the XAML root is reused), so without this the axis, `Distance` and the
  direction baked into the expression would keep the previous taskbar's layout.
  One refresh per open writes the three scalars and logs
  `gesture geometry updated axis=... dist=...`, or
  `gesture geometry axis=... dist=... (unchanged)` when the values read the
  same.
- The geometry is measured through one request, not two triggers. The open (and
  a work-area or display change) records a `SettledMeasureRequest`: a serial, the
  root/open generation, whether no contact owned the panel edge at that moment,
  and a 400 ms deadline. Two events can complete it — the second `LayoutUpdated`
  pass, when XAML has placed the panel, and that deadline, because no second pass
  may come while nothing moves — and the first to run ends the request, so the
  other finds nothing to do. The turn re-reads the generation, whether a root is
  attached and whether a gesture owns the edge at the time it runs, and the
  publish and the scroller commit re-check the request id, the epoch, the layout
  identity and panel ownership once more at the moment they write: a request
  whose menu closed, or that a newer request replaced, builds its candidate and
  publishes nothing. A close cancels only the request id it captured before the
  deferred UI turn, so a reopen that installs its own request first keeps it.
  Every open and close bumps the generation, so a measurement recorded for one
  open can never be published for the next.
- The scroller cache has the same rule as the panel: a settled pass that finds
  nothing scrollable records `Nothing` only when the search actually finished.
  A traversal that threw, hit its depth limit, or ran while a contact owned the
  panel stays `Unknown` and is retried after 100 ms while the same open is still
  on screen; `Nothing` is never inferred from an incomplete search.
- Each device is decoded from its `RIDI_PREPARSEDDATA` blob, never by opening
  the device. Contact collections are the `0x0D/0x05` and `0x0D/0x22` link
  nodes, plus a pen's `0x0D/0x02` and `0x0D/0x20` nodes when the device has
  them. A device whose input caps declare report IDs carries the ID in the
  first byte of every raw report (HID's own layout, not `RAWINPUTHEADER.wParam`,
  which is only `RIM_INPUT`/`RIM_INPUTSINK`); that byte selects the value caps,
  and a device without report IDs keeps working unchanged.
- Contact identity is decided before any coordinate is used. A report that
  carries no contact field for any slot is an unrelated report ID: it is
  ignored without reaching the sample path, and that is distinct from a decode
  failure, which logs `hid decodefail` and feeds nothing. Otherwise each
  candidate slot's Tip Switch (`0x0D/0x42`), In Range (`0x0D/0x32`) and X/Y are
  decoded, and the slot the mod follows is the one the last tracked (device,
  collection) names; when that identity is new, the report's real contacts are
  the candidates and the nearest to the last tracked raw position breaks the
  tie. A slot is a real contact when the report declares its tip and the tip is
  down, or, when the report declares no tip for it, when it is in range; a
  report that declares neither for the slot says nothing about it. Only the
  tracked contact's own slot can move it or end it: its declared tip going up or
  its declared in-range bit clearing is the lift, so another slot's stale
  coordinates cannot restart it and another slot's stale lift cannot end it, and
  a report that carries no contact field for the tracked slot is not a lift
  either. A report from another device cannot move or lift it. A pen report with
  `In Range` set and `Tip Switch` clear is a hover and drives nothing, because a
  slot with the tip up is not a contact; a pen lift ends the contact even while
  coordinates keep arriving. The one fallback is a device that exposes neither
  Tip Switch nor In Range: no report can then say whether a slot is real, so the
  count/position path is used and its count and positioned collections decide,
  exactly as the mod shipped. That decision is made once at init and logged, not
  guessed per report: `hid usages dev=... tip=... inrange=... count=...
  buttons=... reportids=... coordpath=...` records which usages the device
  exposes and whether it is on the count/position path (`coordpath=1`).
- Raw X/Y is mapped from the HID logical range to physical screen pixels with
  `GetSystemMetrics`, then to DIP with `GetDpiForWindow(...) / 96`.
- The gesture state machine runs on the input thread. It begins at one
  contact inside the Start window (4 DIP margin) while the CoreWindow is not
  DWM-cloaked. While the finger moves it inserts
  `Progress = 1 - delta / Distance`, and only when the change moves the panel
  by at least 0.5 DIP (`fabs(dprogress) * Distance >= 0.5`). The filter is a
  screen distance, not a progress fraction: a fixed 0.004 was 5.8 DIP here,
  which is 11.5 physical px at 200% and read as a staircase.
- The Start CoreWindow HWND outlives the menu and `IsWindowVisible` stays TRUE
  while it is closed, so `DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED)` decides
  whether the menu is open: 0 while shown, 2 while closed. One query runs per
  down, after the settling check.
- A gesture whose perpendicular travel exceeds 120 DIP, is more than 1.5x the
  axis travel and keeps the axis travel below 60 DIP is a scroll, not a
  dismiss: it restores `Progress = 1` without animation.
- Release commits when the axis travel exceeds 20% of `Distance` (10% on a
  vertical axis, where the same drag also scrolls the app list) or the average
  velocity exceeds 0.45 DIP/ms. Both outcomes settle with the style the setting
  selects: a commit retracts `Progress` to 0, a cancel returns it to 1. The
  settle's initial velocity is the gesture's **end** speed, not its average: it
  is measured from the reports inside a window of at most 30 ms and at least
  8 ms before the release, in `Progress` units (the quantity the follow inserts),
  and the elapsed time is the one between the first and the last report of that
  window. A contact that moved the panel nowhere in that window reports 0, so a
  finger dragged 300 DIP, held still for 1.2 s and released starts the settle
  from rest; the old whole-gesture `travel / elapsed` reported 0.20 DIP/ms and
  left the seam at a speed the finger did not have. The speed is signed along
  the gesture axis and projected onto the settle's direction: a release still
  moving away from its target (a pull back before letting go) contributes 0 and
  the settle starts from rest instead of backwards. The unit is `Progress` per
  ms, so one estimator serves both families and the settle divides by the travel
  in the same units. The tap that closes is armed for the end of that settle.
  Every Progress animation inserts the target as the base value before
  `StartAnimation`, which is why no interruption may read that value as a
  position: `TakeProgressNow` answers where the panel is from the curve the
  compositor was handed, stops it and pins the answer.
- A vertical dismiss competes with the app list's own scroll: dragging toward
  the taskbar is how the list scrolls back, and that drag used to close the
  menu. The element that scrolls is measured per open on the UI thread: the
  innermost `ScrollViewer` with `ScrollableWidth` 0 and `ScrollableHeight` above
  0, whose screen rect (XAML rect plus the client origin) and
  `VerticalOffset`/`ScrollableHeight` are published for the input thread. The
  cache has three states, not one flag: `Unknown` (no measurement speaks for this
  open, or only the one taken while the shell still moved the window), `Nothing`
  (measured after the shell settled, and nothing in the panel scrolls) and
  `Scrollable` (measured after the shell settled, with rect and offsets). Only
  `Unknown` defers: a press inside the panel waits for the settled measurement and
  a press outside it passes, because the first report after an open used to
  dismiss the menu while the user was scrolling. `Nothing` allows the dismiss, so
  a panel that genuinely has nothing scrollable is no longer undismissable from
  the inside. Inside a `Scrollable` rect the gate still keeps the end-of-list rule
  (offset 0 on a bottom taskbar, offset == `ScrollableHeight` on a top one, with
  the 24 DIP allowance a fling needs) and otherwise rejects the press with
  `dismiss deferred to scroll vo=... sh=...` at most once per 500 ms. Outside the
  rect the plain drag stays. The rejection starts no session, so the same contact
  becomes a dismiss as soon as its scrolling reaches the end; the contact latch is
  untouched and the next down starts from a clean state. `Left`/`Right` are
  unchanged.
- The settle has two styles, and one setting picks the parameter set for all
  four outcomes: the dismiss commit and cancel, the reveal commit and cancel.
  Both styles run the same damped-spring generator in normalized travel
  `y = (Progress - from) / (to - from)`: `y(0) = 0`, `y(inf) = 1`, and `y'(0)`
  is the component of the end speed **toward the target**, scaled by
  `GestureStartSpeedPercent` (default 100) and divided by the travel left. The
  projection is what makes 100 % mean that component, not the raw release
  vector: a pull that reversed before the lift contributes 0, and a release
  with a sideways component hands the settle only the part that moves the panel
  toward where it is going. The curve is then scaled by its own value at `T`
  (below), which divides the whole motion by about 0.993 on a normal settle and
  is why the seam is that component to within that scale. Open and close
  therefore mirror exactly, and a release from a finger held still (`vel=0`)
  starts the settle at zero speed instead of jumping.
  - Windows 10: damping ratio `0.9`, the spring this mod shipped with.
  - Windows 11 (the default): damping ratio `1.72`. Fitted on this machine to
    the native Quick Settings flyout (`Win+A`), whose show motion this style
    copies: mean `|y - d|` 0.034 over the 306 ms measured, and at most 0.012
    after the first 90 ms.
  - The duration comes from the travel, not from a fitted time. The native
    flyout covers its 578 DIP in 262 ms, an average of 2.2 DIP/ms; the Start
    menu is 900+ DIP and resizable, so copying the flyout's time would make the
    larger panel move faster and change the feel with the menu size.
    `remaining = |to - from| * Distance` is the travel in DIP and
    `T = clamp(remaining / 2.2, 160, 520)` ms, so a 900 DIP close takes 409 ms
    and a close from half open 205 ms.
  - The spring's frequency is derived from `T`, not from a fixed period: omega
    is chosen so five time constants of its slowest root land on `T`. That root
    is `zeta*omega` while the spring oscillates, and `omega*(zeta -
    sqrt(zeta^2 - 1))` once it is overdamped; applying `zeta*omega` to the
    fitted Windows 11 damping of 1.72 would put 0.93 time constants in `T` and
    leave 44% of the travel for the last frame. The style picks the shape, the
    travel picks the timescale.
  - The curve is sampled by 12-128 equal segments, one per 4 ms or less, so at
    120 Hz no corner can fall inside a displayed frame. Every sample is clamped
    to 0..1 and to the sample before it: a flick faster than the remaining
    travel can absorb flattens at the rest position instead of translating past
    it, and convergence stays monotonic. The analytic spring stops
    `1 - exp(-5)`, about 0.7%, short of the target at `T`; writing the exact
    target on one last keyframe made the panel hop that remainder (3.8 DIP on a
    501 DIP travel, after a frame that had moved under 0.1 DIP). The whole curve
    is therefore scaled by its own value at `T`, so the samples converge on the
    target and the landing has no step. The keyframes are linear and no easing
    function is attached, because the samples are the curve.

  One settle-and-arm helper serves both gesture families: it plays the
  animation, opens the suppression window, stamps the close channel and arms the
  tap for the end of the settle, which is that settle's own duration. A close
  whose settle did not start arms nothing. One line records each settle:
  `settle style=win11|win10 dir=open|close vel=... dur=... remaining=...
  frames=... zeta=...`. The settle line's `vel` is the speed toward the target
  in DIP/ms, the same number the release line prints.
  The gesture's release line is written after the settle started: the file
  write costs time on the input thread, and before the animation it only made
  the seam longer.
- Where `Progress` is at any instant has one owner. A `Progress` animation
  inserts its target as the base value, so the recorded value is the animation's
  end, not the panel's position; every interruption that read it (a keyboard
  retract during the open settle, a reveal taking a close over, one settle
  replacing another, the close after a coast) jumped the panel to that end before
  moving again. `TakeProgressNow` answers from the curve the compositor was
  handed, or from the coast's linear line, stops that motion and pins the answer
  as the new base, so the next animation starts where the panel is. With nothing
  recorded nothing animates, and the resting value is the answer. The recorded
  state is the start value, the start time, the duration and the sample array;
  it is cleared when the animation stops, is replaced, or has ended. A settle
  started from a target value is the bug this prevents.
- The dismiss settle's batch completion only touches the state of the animation
  that registered it. The batch and its handler are created before the settle
  starts, the handler is registered after it with the generation that animation
  installed, and every stop or replacement detaches that handler and advances the
  generation, so a completion that the compositor dispatches late cannot stop a
  newer reveal or settle or clear a newer close flag.
- The release itself is one idle window plus one tick, so the panel stood still
  for 20-23 ms after the last report before the retract started, and the eye
  read that as a stop. The panel now coasts through that window: the average
  speed of the last three reports, capped at 6 DIP/ms, continues `Progress` on
  a 200 ms linear animation as soon as no report has arrived for 8 ms. A report
  ends the coast, pins the interpolated value and inserts the real one, so the
  correction is the few DIP the panel drifted; a release ends it first and
  plans the close from where it got to, so the retract starts from a moving
  panel. The idle tick starts the coast before it decides the release, so one
  late or coalesced tick cannot skip the coast and release from the last
  report. Nothing coasts while the menu is closed, inside the 600 ms
  suppression window, or in the reveal's first 50 ms. A held finger never
  coasts: the panel reports at ~170 Hz, so a live contact leaves no 8 ms gap.
- A session ends when no report arrives for 18 ms (`kIdleReleaseMs`); the idle
  tick runs every 5 ms, so the release is noticed about 18-23 ms after the
  last report. The tick is a waitable timer created with
  `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`: measured on this build, WM_TIMER
  stays on the 15.6 ms default tick even while the mod holds a 1 ms
  `timeBeginPeriod(1)` request (resolved at runtime from `winmm.dll` and paired
  with `timeEndPeriod` for the time a contact is down). Without the waitable
  timer the release measured `relms=31`; the 1 ms request alone left it at 32.
- A menu opened without keyboard focus keeps the foreground on `SearchHost`, so
  the legacy menu ignores an injected `VK_ESCAPE`. Both closes are one Windows
  tap. A keyboard close already is one; a gesture close taps `VK_LWIN` at the
  end of its retract, after the panel has left the screen. That tap is the
  shell's own Start toggle: the key handler performs
  `CStartExperienceManager::Hide` and leaves the toggle ready for the next
  Windows press, so no re-arm tap and no explorer-side hide request exist. The
  transfer is one key press, down 60 ms and up, on whichever window has the
  foreground. A Win press during the retract hides the menu early (the stamp is
  already fresh), and the armed tap is then skipped; no press desyncs the
  toggle.
- A down that arrives while the previous commit is still settling is ignored.
  The menu reuses one XAML root, so a commit sets a flag that makes the next
  down stop the animation and restore `Progress = 1` before the drag starts.
- A second contact is not the tracked finger. While the tracked contact is down,
  a report from another slot or another device neither feeds, ends nor restarts
  it, so a palm or an idle finger elsewhere cannot cancel the drag or make it
  jump. When the tracked contact lifts, the release settles as usual and the
  next report starts the following contact from scratch. A lift report is not
  required: a session with no sample for 18 ms ends on its own.
- Reveal follow: a contact that starts within 12 DIP of the taskbar edge while
  the menu is closed is a candidate. EP opens the menu by itself once the
  contact passes its own edge threshold; the first report that sees the menu
  open takes `Progress` over. The follow works in absolute screen DIP against
  the panel edge that meets the finger (`panelTrail`), which the UI thread maps
  from the largest visible child of the root that does not fill it
  (`StartSizingFrame`) and translates by the XAML origin, the client origin of
  the host window in DIP (`ClientToScreen`), not the work-area origin:
  `Progress = 1 - (finger - panelTrail) / (dir * Distance)`.
  That edge is not published on its own. Axis, `Distance`, the panel's whole
  rectangle, the XAML origin and the work area and monitor are one
  `GestureGeometry` snapshot behind a short writer lock: the UI thread is the
  only writer and holds the lock exclusively for one struct assignment; the input
  thread tries a shared acquisition per sample and answers its last whole copy on
  contention. A seqlock alone was not enough, because its counters do not legalize
  reading a plain struct while the writer assigns it. The snapshot carries the
  root/open generation and a layout identity token, so a reader holding a copy
  from before a resize can tell that it is obsolete and ask the writer again
  instead of starting a gesture from an old rectangle. Separately published
  atomics used to leave a rectangle from a fresh measurement beside a trail from
  the old one, and the dismiss hit test then tested a different panel from the one
  the follow mapped against.
  A measurement passes only when its rectangle lies on the monitor and the side
  against the taskbar lies between the work-area edge and the monitor edge — that
  band is the taskbar, so the menu's attaching edge is neither inside the desktop
  nor past the screen, and the band's own width is the tolerance, with 2 DIP of
  rounding. The shell's open storyboard fails that by the width of the panel:
  measured here, its rect is 12,-12..1005,539 where the settled one is
  411,163..1404,714. A rejected measurement publishes nothing and logs
  `reveal panelTrail rejected ...` once; the last whole snapshot stays. The
  snapshot is published at setup, on every root `SizeChanged`, on every menu open
  and on the second layout pass of that open, and on every work-area or display
  change, and the line `reveal panelTrail=...` reports the edge with its
  rectangle, the work area and the generation: once per open, and again whenever
  the value moved by more than 1 DIP. A previous rectangle is carried into a new
  publish only when the whole layout identity matches — axis, travel, client
  origin, work area and monitor — and the carried rect is tested against the
  current taskbar band again; the same travel alone is not the same panel. A
  `SizeChanged` that runs while a contact owns the panel defers its measurement
  and the input thread posts it when the contact ends, so the edge never moves
  under the finger. Renderer geometry (axis and travel) is committed only after
  the composition scalars that use it are written; a failed write leaves the old
  values and publishes nothing.
  A panel that has never been measured has no rectangle at all: the follow then
  uses the root-width mapping this mod shipped with, whose rest position is the
  far screen edge minus the travel, so `Progress` still reads 0 at the taskbar
  edge and 1 at the far one on either side, and the log says
  `reveal panelTrail=... (fallback)`.
  The panel edge therefore meets the finger at every sample, and the
  panel is exactly off screen at the screen edge (Progress 0.269 for a 1052 DIP
  trail on a 1440 DIP root). That mapping drives the visual only. The release
  decision mirrors the dismiss path: it judges the travel away from the edge at
  release, `(finger - edgeStart) * openingSign` clamped at 0, where
  `openingSign` is +1 for a left or top taskbar and -1 for right or bottom. A
  release past 20% of `Distance`, or one whose session-average velocity
  `travel / max(elapsed, 1)` exceeds 0.45 DIP/ms, settles `Progress` to 1 with
  the selected settle style and keeps the menu. Anything else is a cancel:
  `Progress` settles to 0 with that same style, so both close paths read the
  same from any finger position. The settle itself starts at the gesture's end
  speed, measured over the same 8-30 ms window as the dismiss release and
  projected onto the settle's direction, so a pull that reversed before the lift
  starts from rest. The Windows tap that closes the menu is armed
  for the end of the settle, so the panel is already off screen when the shell
  hides it. `Progress`
  stays 0; a later
  open restores it, unless an edge contact is already driving that open. Pulling
  out and pushing back therefore cancels, as it does inside the open menu. The
  line `open follow travel=... max=... progress=... vel=... commit=... panel=... n=...`
  is written once per reveal; the cancel adds `close tap kind=reveal delay=0`,
  or `close tap dropped kind=reveal` when a new pull is already there at the tap.
- A close owns `Progress` the same way from either family. A gesture close
  arrives from its release; a keyboard close arrives through the bridge while the
  finger is still down, so it also ends the live dismiss session and the live
  reveal, mutes the rest of that contact until it lifts, opens the suppression
  window and releases the 1 ms timer period. The mute is established from the
  contact latch and either family before either owner is cleared, so a keyboard
  close that lands while the *reveal* owns the finger mutes it too: ending the
  reveal's flags alone left the finger live, and its next report could start a
  dismiss session on the reopened menu while its release settled that session
  open. Without that state the next report
  of the same contact dragged the panel back and its release settled the menu
  open over a close that was already running. The helper is the same call for
  both; the keyboard close additionally drops an armed tap, because the Hide it
  serves would otherwise toggle the menu back open.
- Every close the mod starts suppresses the reopen restore from the moment its
  settle starts until the shell reports the window cloaked (`win close
  observed`), at most 600 ms. The
  reused root reports itself open until the shell has run the hide behind the
  tap, and restoring `Progress = 1` in that gap flashed the fully open panel
  while the close was in flight. For the same window a contact on the edge counts
  as a new reveal attempt even while the window still reports open: otherwise the
  down was read as a drag of the settling panel, which stopped the settle and
  restored `Progress = 1` on the first sample.
- The candidate path polls the cloaked attribute at most once per 20 ms; the
  cached answer is dropped on every lift, so each contact starts from a fresh
  query.
- A closed-to-open transition that no reveal owns (a Windows-key, Start-button
  or tray open) runs the settle a gesture open runs:
  `BeginGestureSettle(0, 1, 0, ...)` starts from rest, the travel plans the
  duration and the style picks the damping, so a keyboard open and an edge pull
  are one motion and there is no second curve. The settle needs the measured
  travel: an open that lands before the first geometry publish (`Distance`
  still 0) restores the resting value instead and lets the shell's own
  storyboard run. `PollRootVisibility` polls the
  cloaked attribute at most once per 20 ms on the 5 ms idle tick and sees the
  transition; a live edge candidate and the 600 ms suppression window both
  suppress the animation. A close the shell performs by itself logs
  `win close observed`, leaves `Progress` where the close left it, and lets the
  next open animate from 0.
  The Windows-key close is animated now: the explorer-side bridge asks this mod
  for the retract through the shared channel and holds
  `CStartExperienceManager::Hide` until the settle it starts has run. Only this
  process can plan that settle, so it answers with the planned duration and the
  bridge waits exactly that plus a 30 ms margin; the mod runs the selected
  settle style from rest (520 ms at this layout's 1440 DIP travel, 409 ms for
  the 900 DIP panel) and the panel slides out instead of vanishing. One line
  per transition.
- Every gesture close stamps the close channel at the release. The tap comes at
  the end of the settle, so the Hide it causes needs no wait; the stamp, which
  carries a sequence number, tells the bridge that this Hide is that tap's and
  not a keyboard close. A stamp the bridge has already consumed, or one older
  than its 800 ms window, means a keyboard close and takes the keyboard lead.
- A new edge pull during the settle cancels the close before the shell is
  touched: the tap is dropped and the pull keeps driving the panel, so no Hide
  runs and the toggle is never advanced. The pull generation is sampled at the
  release and checked once when the tap would run; the drop overrides the stamp
  with the cancelled kind, so a later keyboard close uses its own lead.

What it does not do:

- It does not read explorer memory or call into explorer. Every word that
  crosses the process boundary goes through the shared section and its events.
- It does not attach XAML pointer handlers and has no DirectManipulation
  hook.
- It does not remove the shell's open storyboard. Measured with `anim=0`:
  dropping the open request leaves the Start window uncloaked with no pane
  drawn, because the same request is the step that presents the content. The
  switch therefore stays at `anim=1` and the open still slides from the shell.
  The hook is installed anyway, because the debug command has to reach it
  without a reload; it passes the request through and costs one detour plus
  three vtable reads.

Unload:

- One stop event ends the input thread, and the thread's wait sees it even when
  the thread has not published its window yet. The stop event is a prerequisite:
  if it cannot be created at init, the input thread is not started at all
  (`hid thread not started (stop event unavailable)`), because a gesture source
  that cannot be stopped is worse than none. Every wait on the input thread and
  on the module poll thread is bounded or watches that event, so the stop is
  observed within one short wait; the Windows tap's 60 ms hold waits on the event
  as well, and the key is released either way. `Wh_ModBeforeUninit` stops and
  waits the module poll thread for 10 s, sets the stop event, posts the close
  message as a second signal for a queued message that wins the wait, and waits
  10 s for the input thread. `Wh_ModUninit` then revokes every XAML delegate on
  the UI thread and waits for that turn, detaches the settle batch's `Completed`
  handler and drains a callback the compositor already dispatched, and only then
  releases the composition state and the IPC view. Every registration keeps its
  own revoke token and its own try block
  (root `Loaded` and `SizeChanged`, the layout watch, the scroller's
  `ViewChanged`, the settle batch's `Completed`), so removing one cannot lose the
  others if it throws. A thread or a callback that does not finish in time leaves
  every resource in place and logs which one
  (`unload hid thread still running; ipc view leaked`,
  `unload residual=...; process must restart to finish unloading`): unmapping the
  section or dropping the composition objects under running module code would
  crash it, and a late delegate sees `g_unloading` on entry. The module is then
  not gone; the process must restart to finish the unload.

Settings:

- `AnimationStyle` (default `Windows 11`), shown as "Menu animation style",
  picks which damping the settle spring runs. The code compares the option
  string itself: exactly `Windows 10` selects the shipped 0.9 spring, and
  every other value, including the default, selects the fitted 1.72 one.
  Both styles are the same spring generator, so a style switch changes the
  feel and never the mechanism. The duration is not a setting: the travel
  left divides by the flyout's 2.2 DIP/ms and clamps to 160-520 ms, so a
  bigger menu takes longer instead of moving faster. A Windows-key open runs
  the same settle from rest, so the style changes how a keyboard open looks
  too. `Wh_ModInit` reads the value once into an atomic enum and logs
  `settle style selected=... setting=...`; `Wh_ModSettingsChanged` refreshes
  the atomic, so a change applies to the next settle and no settle performs
  a setting lookup.
- `GestureStartSpeedPercent` (default 100, clamped to 0-300), shown as
  "Animation start speed (% of swipe speed)", is the settle's start speed as
  a percentage of the estimated end speed component toward the target over the
  travel left, in either style. 100 continues at that component, so the seam
  has no speed step: the end speed is measured over the last 8-30 ms of reports,
  projected onto the settle's direction, and the curve's own end-value scale
  (about 0.993, see above) divides the whole motion. It is not the raw release
  vector: a finger moving sideways or back toward where it came from hands the
  settle only the part that moves the panel toward where it is going. Higher
  snaps away faster; 0 starts from rest, which a finger held still reports
  anyway. The end speed is the last 8-30 ms of
  reports, so a hold before the lift reports 0 where a whole-gesture average
  reported the drag's speed. Windows-key opens and closes
  have no finger speed and always start from rest, so this setting does not
  change them; the style does. `Wh_ModInit` reads it once into an atomic and
  logs `settle start speed=...`; `Wh_ModSettingsChanged` refreshes the
  atomic, so a change applies to the next settle and no settle performs a
  setting lookup.

Debug channel (kept in the build):

- The channel is shared memory plus three events, described in
  `swipe_ipc_shared.h`; no file is involved and nothing polls.
  `tools/ipc.ps1` writes a command and reads the counters:
  - `pwsh -File tools\ipc.ps1 p=0.5` inserts `Progress`, clamped to 0..1.
  - `pwsh -File tools\ipc.ps1 t=-300,0,0` sets `UIElement.Translation` on the UI
    thread.
  - `pwsh -File tools\ipc.ps1 e=-300,0` sets `Properties["Offset"]` and
    `Visual.Offset` on the visual.
  - `pwsh -File tools\ipc.ps1 anim=0` suppresses the shell's open storyboard;
    `anim=1` restores it. The default is `anim=1`, because suppression leaves
    the pane undrawn.
  - `pwsh -File tools\ipc.ps1 -State` prints the counters: the command sequence,
    the close stamp, the retract request and the plan.
  The command event wakes the raw input thread, so a command runs within
  milliseconds instead of waiting for a poll. The event is manual-reset and this
  side resets it, so a command published while one is running is not lost.
- The `retract` request is its own event, not a command: the bridge publishes a
  sequence number right before it holds `CStartExperienceManager::Hide`, and
  this side plans the settle from the current `Progress` and answers with its
  duration on the same channel. `retract=<settleMs>` names the animation that
  just started; the "no settle" sentinel says that nothing started, so the
  bridge keeps its own default lead. The answer echoes the request's sequence
  number, so an answer to an earlier close can never satisfy a later one. The
  request carries the menu-open generation the bridge saw and a deadline, so a
  request that outlived its close is answered with the sentinel and starts
  nothing, and the bridge marks its request done when that Hide completes. Open
  generation 0 is not a menu: `SwipeIpcReadOpen` reports `ERROR_NOT_READY`
  instead of a snapshot, and this side refuses a request that names 0 exactly
  like one whose generation is gone, so a keyboard close can never start a settle
  for a menu that never published an open.
- The section outlives either process, so both sides record the sequence numbers
  at attach and act only on what is newer. That is what the one-second freshness
  check on the old command file did: a request that predates this process
  belongs to a menu this process never drew (`debug retract ignored (stale)`
  names a request whose generation, deadline or done marker says its close is
  over).
- The `close record` is three publishes of one seqlock record: the plan at the
  release (`tapped=0`), the cancel the moment a new pull drops the close, and
  the tapped record with a fresh sequence immediately before the Windows tap.
  Only the tapped one tells the bridge that a Hide needs no lead. The open
  publisher is also a close-record writer: publishing a new open generation
  publishes the cancelled kind in the same place, so an unconsumed tapped record
  from the previous menu cannot survive into the next one. The bridge's own
  rule — refuse a stamp older than the open tick — is a second guard, not the
  binding this side relies on.
- Every record is a seqlock, so the bridge never reads a half-written close
  record, plan or open generation, and this side never reads half a command.
  The command record is also serialised by a named mutex, because more than one
  tool can publish into it.
- The files this channel replaced (`debug.txt`, `close-mode.txt`,
  `retract-plan.txt`) are deleted at init; they are transport, not state, and a
  leftover would invite a reader that no longer exists.

Diagnostics:

- `mod init`, `settle style selected=... setting=...`,
  `settle start speed=...%`,
  `settle plan speed=... dip/ms dur=160..520 segment=4.0 ms frames=13..129`,
  `settle spring win10 zeta=...`,
  `settle spring win11 zeta=...`, `hid init`,
  `hid stop event failed err=...`,
  `hid thread not started (stop event unavailable)`,
  `hid reg usage=... ok=... err=...`,
  `hid down x=... y=... axis=... dist=...`,
  `hid release src=idle|report|abort delta=... final=... perp=... vel=...
  commit=... n=... relms=...` (`vel` is the end speed toward the settle's
  target in DIP/ms, so a hold before the lift reports 0.00),
  `settle style=win11|win10 dir=open|close vel=... dur=... remaining=...
  frames=... zeta=...` (`vel` is that same end speed toward the target;
  `frames` counts keyframes, so the segment count is one
  less),
  `win open dur=...`, `win open restore (no geometry)`, `win close observed`,
  `close tap kind=dismiss|reveal delay=...`,
  `close tap dropped kind=dismiss|reveal`,
  `close tap skipped kind=... (already closed)`,
  `close mode stamp failed kind=...`, `close mode cancel stamp failed`,
  `open follow travel=... max=... progress=... vel=... commit=... panel=... n=...`,
  `reveal panelTrail=... rect=...,...,...,... work=...,...,...,... gen=...`
  (once per open from the open-time turn, and again whenever the value moved by
  more than 1 DIP), or
  `reveal panelTrail=... (fallback)` once, when no panel has been measured, or
  `reveal panelTrail rejected ... rect=... work=... mon=...` once, when the
  measured rect is not the panel this layout shows and the last whole snapshot
  was kept),
  `hid decodefail dev=... err=...` (a field that belongs to the report and
  could not be decoded; logged once per device),
  `hid usages dev=... tip=... inrange=... count=... buttons=... reportids=...
  coordpath=...` (once per device, at init; `coordpath=1` is the logged
  count/position fallback for a device with no tip and no in-range usage),
  `attached root=...`, `dismiss axis=...`, `debug p=...`, `debug t=...`,
  `debug e=...`, `debug anim=...`, `debug retract from=... dur=...`,
  `debug retract failed`, `retract answer failed dur=...`,
  `debug retract ignored (stale)`, `ipc open created=... cmd=... retract=...`,
  `ipc open failed err=...`, `ipc legacy files removed=...`,
  `gesture geometry updated axis=... dist=...` (or
  `gesture geometry axis=... dist=... (unchanged)` once per open),
  `gesture geometry write failed ...`, `gesture geometry queue err=...`,
  `work area changed`,
  `scroller rect=...,...,...,... vo=... sh=...` (once per open, when the
  scrolling element was measured, and again only if the settled pass moved it),
  `scroller none (measured, nothing scrolls)` once per open when the settled pass
  found nothing scrollable,
  `scroller retry why=origin|incomplete|not-allowed|offsets|commit gen=...` once
  per open when a settled pass could not commit and a retry or deferred pass was
  arranged,
  `dismiss deferred to scroll vo=... sh=...` (at most once per 500 ms),
  `dismiss skipped outside panel axis=... x=... y=... rect=... trail=... gen=...`
  (at most once per second),
  `startui anim hooked`,
  `startui anim hook skipped`, `startui root hook skipped ...` and the first
  four `startui anim type=... edge=... trans=... suppress=...` calls go to
  `renderer.log`,
  `unload hid thread stopped` (or
  `unload hid thread still running; ipc view leaked` and
  `unload hid thread wait=... ms timed out`), `unload poll thread still running`
  and `unload poll thread wait=... ms timed out` when a thread outlives its wait,
  and `unload residual=...; process must restart to finish unloading` naming the
  resource left in place.
- Only begin and release are logged, never individual samples or coasts. The
  `edge` line stays in the code behind `g_debugEdge`, which is false; it measured
  the raw edge contact and EP's own open threshold. The `coast start/stop` lines
  are behind `g_debugCoast`, also false; they measured the coast's endpoints and
  showed that `CompositionPropertySet.TryGetScalar` answers with the stored base
  value while a coast younger than a compositor frame is running, which is why
  the coast's own line, not a read-back, is what gets pinned.

Known risks:

- Raw input has one sink per usage per process. Another mod that registers
  `0x0D/0x04` (the `inputprobe` diagnostic does) redirects or removes the
  stream; `inputprobe` must stay disabled.
- The digitizer is assumed to cover the primary screen and match the window
  DPI. A panel on another display, a rotated panel or a non-default logical
  range decodes to the wrong DIP point.
- The legacy menu also receives the same touch through its normal XAML input,
  so a drag can scroll or click a tile while the menu slides.
- The reveal takes `Progress` over on the first report that sees the menu open,
  so the menu drops to the computed progress at that instant (0.269 at the
  screen edge on this layout) and then follows. The mapping needs the panel
  trail: it is the largest visible child of the root that does not fill it. If
  that child is missing or unlaid when a measurement runs, or the rect it names
  is not the panel this layout shows, nothing is published and the follow uses
  the root-width fallback (log `reveal panelTrail=... (fallback)`). A reveal
  that starts on the very first open of a fresh process therefore lags the finger
  until the settled pass has measured the panel once; every later open carries
  that rect.
- The first measurement of an open is taken too early to be trusted: `Setup` and
  the root's first `SizeChanged` run while the shell's open storyboard still has
  the frame out of place. Measured here (right taskbar), that frame is
  12,-12..1005,539 where the settled rect is 411,163..1404,714, so its taskbar
  edge is 387 DIP short of the taskbar band and it is rejected rather than
  published. The request's settled pass is what publishes the real rect; a gesture
  that begins inside those first two layout passes maps against the previous
  open's rect (or the fallback).
- The trail assumes the XAML coordinate space starts at the host window's client
  origin, which is what `ClientToScreen` returns. Measured on this build with a
  bottom taskbar: the CoreWindow sits at x=447 px while the work area starts at
  0, so the panel's frame right of 1005 DIP is drawn at 1228.5 DIP. A host that
  lays its content out somewhere else than its client area would move the trail
  by that difference.
- The snapshot is republished on every root `SizeChanged`, on every open and on
  every work-area or display change. A panel resize during a follow moves the
  mapping, so the panel edge jumps to the new trail under the same finger. The
  measurement skips the rect while a gesture owns the edge, so a live follow
  never has its anchor moved; the axis and the travel are still refreshed, and a
  travel that changed invalidates the rect instead of carrying it into the new
  layout.
- A taskbar move is noticed only through `WM_SETTINGCHANGE`
  (`SPI_SETWORKAREA`) or `WM_DISPLAYCHANGE`; an arrangement change that raises
  neither keeps the old axis until the next open, which always refreshes it.
- `Distance` is sampled at takeover. A layout change during the follow keeps
  the old extent until the contact lifts.
- Composition calls and the settle batch run from the input thread. The batch
  completion arrives on the compositor's dispatcher; a completion that never
  arrives leaves `Progress` at the settle's target, which is where the animation
  ended anyway, and the next settle, drag or take advances the generation and
  clears the settling flag, so no later down waits on that callback.
- The end speed needs two reports inside 30 ms. A stream slower than about
  33 Hz has no pair in the window, and the settle then starts from rest rather
  than from a speed the data cannot support. The panel reports at ~170 Hz, so
  this only affects a coarsely injected gesture.
- A committed dismiss taps the Windows key at the end of its retract. That tap is
  the close: with the explorer-side bridge disabled or its hooks refused, the
  menu still hides, and the panel has already slid out. The tap is global: it
  lands on whichever window has the foreground, and a lone Windows tap opens
  Start if the menu is already gone.
- A pull that starts during the retract cancels the tap before it is sent, so
  the shell is never touched and the menu stays open. That check runs once, when
  the tap would run; a pull after the tap is a normal open of a closed menu.
- A Win press during the retract hides the menu early (the close stamp is
  already fresh), and the armed tap is skipped because the window is closed.
  A press just after that hide lands in the shell's own settle and is ignored;
  the toggle is not advanced, and the next press opens the menu.
- The suppression window is time based until `win close observed` clears it. A
  reopen that lands before the shell reports the close is not animated; the
  panel stays at 0 until the observed close clears the window and the next open
  animates it. In practice the window is the hide delay, not the full 600 ms.
- The tap holds the input thread for 60 ms. It runs after the retract, on the
  input thread, and is global; a new contact's reports queue behind it.
- The close stamp is read only inside its 800 ms window. A gesture close stamps
  it at its release; the tap's Hide arrives at the end of the settle
  (409 ms for a full 900 DIP panel at the planned 2.2 DIP/ms) and runs
  at once. A dropped close stamps the cancelled kind, which the bridge treats as
  a keyboard close, so a Win press within the window still takes the keyboard lead.
- A keyboard close's lead is the answer this mod publishes on the channel,
  plus the bridge's 30 ms margin. The answer can only be sent after the
  bridge's request has reached this thread, so the shell hides one margin after
  the settle has run to its end, never while it is still moving. When the answer
  never arrives (the mod is disabled, the settle did not start) the bridge waits
  its own 560 ms instead: that covers the 520 ms clamp plus the 30 ms margin, so
  even a panel whose whole travel hits the clamp keeps its last frames.
- `DwmGetWindowAttribute` is resolved at init from `dwmapi.dll`. If the export
  is missing, no gesture starts.
- The `PrepareRootElement` and `OnGlobalAnimationRequested` RVAs are specific to
  `StartUI_.dll` 10.0.22621.3733. Both hooks check their target before they
  install and refuse on any other build. The root hook reads the file version
  and then the target prologue and its first call target, and logs
  `startui root hook skipped build=... expected=10.0.22621.3733` (or
  `... prologue mismatch`); without the root there is no renderer, so no
  gesture starts. The animation hook checks the prologue and the three argument
  getters, logs `startui anim hook skipped` and keeps the original animation.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- AnimationStyle: Windows 11
  $name: Menu animation style
  $description: >-
    How the menu moves whenever it slides: touch swipes, and Windows-key
    opens and closes too. Both choices are the same spring, only the damping
    differs. Windows 11 (1.72) is fitted to the native Quick Settings panel
    (Win+A) and glides into place; Windows 10 (0.9) is slower off the mark
    and lands firmer, the feel this mod shipped with. A bigger menu takes
    longer rather than moving faster: the duration follows the travel and is
    not a setting.
  $options:
  - Windows 11: glides like the Win+A Quick Settings panel
  - Windows 10: slower off the mark, lands firmer (as shipped)
- GestureStartSpeedPercent: 100
  $name: Animation start speed (% of swipe speed)
  $description: >-
    How fast the menu keeps moving at the instant you lift your finger. The
    speed is measured from the last 8-30 ms of reports and projected onto the
    direction the menu is about to move: 100 continues at that component, a
    higher value snaps it away faster (up to 300), 0 starts from a standstill.
    A finger held still has no speed to carry, and sideways motion does not
    count, because only the part toward the target enters the motion. Neither
    do Windows-key opens and closes: they start from rest whatever you set.
*/
// ==/WindhawkModSettings==

/*
 * [INPUT]: depends on StartUI::App::PrepareRootElement and
 *   StartUI::SplitViewFrame::OnGlobalAnimationRequested plus its three argument
 *   getters in StartUI_.dll; on RegisterRawInputDevices, GetRawInputData,
 *   GetRawInputDeviceInfoW,
 *   GetDpiForWindow, GetSystemMetrics, GetWindowRect, MonitorFromWindow,
 *   GetMonitorInfoW and keybd_event from
 *   user32; on DwmGetWindowAttribute, resolved at init from dwmapi.dll; on
 *   GetFileVersionInfoSizeW, GetFileVersionInfoW and VerQueryValueW, resolved
 *   at init from version.dll, which gate the root hook to StartUI_.dll
 *   10.0.22621.3733;
 *   on timeBeginPeriod and timeEndPeriod, resolved at init from winmm.dll so the
 *   process holds a 1 ms period while a contact is down; on the
 *   CREATE_WAITABLE_TIMER_HIGH_RESOLUTION waitable timer that drives the 5 ms
 *   idle tick (WM_TIMER stays on the 15.6 ms tick on this build); on
 *   HidP_GetCaps, HidP_GetLinkCollectionNodes, HidP_GetValueCaps,
 *   HidP_GetButtonCaps, HidP_GetUsages and
 *   HidP_GetUsageValue from hid.dll; on the CompositionPropertySet, the
 *   expression and one damped-spring curve sampled into keyframes, which every
 *   `Progress` animation runs: the gesture opens and closes and the keyboard
 *   open, at `GestureStartSpeedPercent` of the gesture's end speed (zero for the
 *   keyboard open); on
 *   VisualTreeHelper, TransformToVisual and LayoutUpdated
 *   from Windows.UI.Xaml.Media to map the panel rectangle and to follow the
 *   layout (the second pass is where the panel is placed, which is what the
 *   measurement request waits for);
 *   on ScrollViewer, its ScrollableWidth/ScrollableHeight/VerticalOffset and its
 *   ViewChanged event from Windows.UI.Xaml.Controls, which name the element the
 *   app list scrolls with; on
 *   ClientToScreen, which turns the host window's client origin into the screen
 *   offset of the XAML coordinate space; on the
 *   CoreDispatcher
 *   captured under the composition mutex in Setup, which the input thread posts
 *   the measurement request to; on the `WM_SETTINGCHANGE`
 *   (`SPI_SETWORKAREA`) and
 *   `WM_DISPLAYCHANGE` broadcasts the hidden top-level input window receives
 *   (a message-only window would not, which is why the window is not
 *   message-only); on the shared IPC channel (`swipe_ipc_shared.h`): the command
 *   event carries `p`/`t`/`e`/`anim` from `tools/ipc.ps1`, the retract event
 *   carries the bridge's request bound to an open generation and a deadline, and
 *   the section publishes this mod's close plan, its cancel, the tapped record
 *   that marks the moment of the Windows tap, the open generation and its answer
 *   to that request, every record under its own seqlock. Open generation 0 is
 *   not a menu: `SwipeIpcReadOpen` reports `ERROR_NOT_READY` and this side
 *   refuses a request that names 0 exactly like one from a closed menu.
 * [OUTPUT]: one hidden top-level window, one raw-input thread, one module-scan
 *   thread and one stop event per process; the threads' waits include that
 *   event, so they stop even when no window was published. The stop event is a
 *   prerequisite: without it
 *   the input thread is not started and no gestures run. Unload order: set the
 *   stop event, wait 10 s for the module poll thread and the input thread, cancel
 *   the settle batch and drain an already-dispatched completion, revoke every
 *   XAML delegate on the UI thread and wait for that turn, then release the
 *   composition state and the IPC view; a thread or callback that outlives its
 *   wait leaves every resource in place and logs which one, and the process must
 *   restart to finish the unload. Every delegate registration keeps its own
 *   revoke token (root Loaded and SizeChanged, the layout watch, the scroller's
 *   ViewChanged, the settle batch's Completed) and its own try block.
 *   The
 *   Composition `Progress` scalar plus the `Distance`/`DirX`/`DirY` inputs the
 *   expression reads, which the open-time and work-area refresh rewrites when
 *   the taskbar moves. One `GestureGeometry` snapshot carries the axis, the
 *   travel, the XAML origin, the work area and monitor, and the panel rect with
 *   the edge derived from it, in absolute screen DIP, behind a short writer lock:
 *   one writer on the UI thread, one whole read per gesture sample on the input
 *   thread, the root/open generation and a layout identity token the measurement
 *   was taken in. A cached copy whose token is older than the published layout
 *   is rejected for a new gesture. It is published by one point (the panel
 *   measurement) from Setup, SizeChanged, every menu open and every work-area or
 *   display change, never by a timer; a rect that is not this layout's panel is
 *   rejected whole and the last snapshot stays; a rect is carried into a new
 *   publish only when the whole layout identity matches. The measurements
 *   themselves are one request per open (serial, epoch, trail-idle decision,
 *   deadline) that the second `LayoutUpdated` pass or the deadline completes,
 *   whichever runs first; a close cancels the request id it captured, and the
 *   publish and scroller commit re-check the request id, epoch, layout identity
 *   and panel ownership at the moment they write. One `VK_LWIN` tap armed for
 *   the end of every
 *   committed dismiss or short reveal, the shell's own Start toggle that performs
 *   `CStartExperienceManager::Hide`; the tap is dropped when a new pull
 *   generation exists when it would run, and no request file or re-arm tap
 *   exists; one settle in the parameter set `AnimationStyle` selects, and one
 *   settle-and-arm helper for all four gesture outcomes. Both styles are the
 *   same spring generator, so open and close mirror exactly and both start at
 *   `GestureStartSpeedPercent` of the gesture's end speed component toward the
 *   target, divided by the travel left: 100 % names that component, not the raw
 *   release vector, because the end speed is projected onto the settle's
 *   direction before it enters the seam and the whole curve is scaled by its own
 *   value at the duration (about 0.993 on a normal settle). `Windows 11` (the
 *   default) keeps damping 1.72, fitted on this
 *   machine to the native Quick Settings flyout; `Windows 10` keeps damping 0.9,
 *   the spring this mod shipped with. The end speed is measured from the reports
 *   of the last 8-30 ms only, in `Progress` units, signed along the
 *   gesture axis, and projected onto the settle's direction: a contact that did
 *   not move the panel in that window (a finger held still) reports 0, and a
 *   release still moving away from its target contributes 0 rather than starting
 *   the settle backwards. The duration is the panel travel divided
 *   by 2.2 DIP/ms, the native flyout's own speed, clamped to 160-520 ms, and the
 *   frequency is derived from it so five time constants of the slowest root land
 *   on that duration. The response is sampled by 12-128 linear keyframes, one
 *   per 4 ms or less and each clamped to 0..1 and to its predecessor; the whole
 *   curve is scaled by its own value at the duration, so the last sample is the
 *   target and the panel lands without a step. The
 *   helper also owns the suppression window, the close record and the
 *   armed tap, whose delay is the settle's own duration (160-520 ms), so
 *   neither gesture family can skip one; the close record is the plan at the
 *   release, the cancel when a new pull drops the tap, the tapped record
 *   published immediately before the tap itself, and the cancel published with
 *   every new open generation, so no stamp from the previous menu survives into
 *   the next; both settings are read once at
 *   init
 *   and refreshed on a settings change, logged as
 *   `settle style selected=... setting=...` and
 *   `settle start speed=...%`; a linear coast of
 *   `Progress` at the finger's smoothed speed while
 *   no report arrives, started by the idle tick before that tick decides the
 *   release, ended by the next report or by the release, which then
 *   settles from the value the coast reached; one owner of where `Progress` is
 *   at time t (`TakeProgressNow`), which answers from the recorded curve or the
 *   coast's line, stops that motion and pins the answer, so no interruption
 *   starts a settle from an animation's target; one helper that gives a close
 *   the same ownership whether it came from a gesture or the keyboard (the live
 *   sessions ended, the rest of a contact still down muted, the suppression
 *   window open, the timer period released); one cached scroller per
 *   open (the innermost vertically scrollable element the app list uses), with
 *   its screen rect and its `VerticalOffset`/`ScrollableHeight` published for the
 *   input thread and refreshed from its `ViewChanged` event, in three states:
 *   Unknown (no measurement speaks for this open, the only one that defers),
 *   Nothing (a completed search after the shell settled found nothing scrolls,
 *   so the press dismisses) and Scrollable (rect and offsets, so a vertical
 *   dismiss that would only scroll the list is rejected and logs
 *   `dismiss deferred to scroll`); a search that threw or hit its depth limit is
 *   not a negative answer, so it stays Unknown and is retried while the same
 *   open is on screen; a
 *   0-to-1 settle from rest for a closed-to-open transition no reveal owns, with
 *   the same curve and the travel-planned duration a gesture open uses, plus a
 *   `win close observed` line when the shell hides the window by
 *   itself; a 1 ms timer period (timeBeginPeriod/timeEndPeriod) held exactly
 *   while a contact is down and one 5 ms high-resolution idle tick that detects
 *   the release; a suppression window, at most 600 ms, after either close in
 *   which a closed-to-open report does not restore `Progress` and an edge
 *   contact starts a new reveal rather than a drag of the settling panel; the
 *   window ends as soon as the shell reports the window cloaked; the
 *   always-installed animation hook, which passes every request through unless
 *   the `anim=0` debug command suppresses the open request (type 0; never by
 *   default, because dropping it leaves the pane undrawn) while
 *   the close request (type 1) always passes through; four debug commands
 *   (`p`, `t`, `e`, `anim`) that apply a Progress value, the XAML translation,
 *   the visual offset or the animation mode, and the
 *   answer to the bridge's retract request (its planned duration, or the
 *   no-settle sentinel) published on the same channel the moment the requested
 *   settle starts, so the bridge can hold the shell's Hide for exactly that
 *   duration; no exported symbols.
 * [POS]: input and gesture layer of the legacy Start mod. It replaces the XAML
 *   pointer and DirectManipulation paths with raw HID decoding, owns two
 *   gestures (the dismiss drag inside the open menu and the reveal follow that
 *   takes the menu's own open over from the taskbar edge and pins the panel's
 *   trailing edge to the finger), and re-reads the taskbar edge behind both on
 *   every open and on every work-area broadcast. Every sample takes one
 *   `GestureGeometry` snapshot, so the press hit test, the reveal follow, the
 *   release decision and the log line of that report all describe one panel;
 *   the snapshot is read under a shared writer lock and a cached copy from an
 *   older layout is rejected for a new gesture; the geometry is measured by one
 *   request per open, so no two triggers can
 *   measure the same layout twice or publish a measurement their epoch no
 *   longer owns, and a close cancels only the request id it captured. The
 *   animation hook is
 *   installed on every load and passes requests through; only the debug switch
 *   can drop the shell's open storyboard, and it is never the default. The
 *   raw HID decode follows HID's own layout: the report ID is the first byte of
 *   a report from a device that declares report IDs, contact identity is the
 *   (device, collection) slot whose own tip/in-range says it is real, and a pen
 *   in range with
 *   its tip up is a hover that drives nothing. A report id that carries no
 *   contact field is ignored whole, and a device with neither tip nor in-range
 *   usage is the logged count/position fallback. It also owns the close decision:
 *   the settle runs first, then the tap at its end; both settle styles are the
 *   same spring generator and differ only in damping, so a style switch can
 *   never change how the end speed enters the motion; the keyboard open
 *   is that settle started from rest, so no second open curve exists; a pull
 *   generation checked
 *   at the tap drops it, so a re-pull never touches the shell, and the cancel it
 *   publishes at once is what keeps that close out of the bridge's tap
 *   decision. A settle started from the value an earlier animation was heading
 *   to is the bug `TakeProgressNow` prevents: the panel's position is answered
 *   from the recorded curve and pinned before anything new moves it, on every
 *   interruption and in both gesture families. The explorer-side
 *   bridge still calls the original Hide; it only skips its keyboard lead on the
 *   tapped record the tap itself publishes, and otherwise waits the duration the
 *   answer names.
 *   The composition helpers below it are shared with the root hook above it.
 * [PROTOCOL]: Update this header on change, then check AGENTS.md
 */

#include <windhawk_utils.h>
#include <windows.h>
#undef GetCurrentTime  // MinGW winbase.h macro vs. Timeline.GetCurrentTime

#include <hidusage.h>
#include <hidpi.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <new>
#include <vector>

#include "swipe_ipc_shared.h"

namespace {

namespace wuc = winrt::Windows::UI::Composition;
namespace wucore = winrt::Windows::UI::Core;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;

using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;

void FileLog(const wchar_t* format, ...) {
    static SRWLOCK lock = SRWLOCK_INIT;

    WCHAR storage[MAX_PATH] = {};
    if (!Wh_GetModStoragePath(storage, ARRAYSIZE(storage))) {
        return;
    }

    WCHAR path[MAX_PATH] = {};
    swprintf_s(path, L"%s\\renderer.log", storage);

    WCHAR line[1024] = {};
    va_list args;
    va_start(args, format);
    vswprintf_s(line, ARRAYSIZE(line), format, args);
    va_end(args);

    char utf8[2048] = {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8,
                                    sizeof(utf8), nullptr, nullptr);
    if (bytes <= 1) {
        return;
    }

    AcquireSRWLockExclusive(&lock);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
        WriteFile(file, "\r\n", 2, &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&lock);
}

constexpr uintptr_t kPreferredBase = 0x180000000;

// StartUI::App::PrepareRootElement(Frame^, PerFrameData^, bool) -> UIElement^
// StartUI_.dll 10.0.22621.3733 (x86-64), the build this RVA was derived from.
// The RVA moves with the build, so the installer refuses any other one: it
// checks the file version first and then the target bytes below.
constexpr uintptr_t kPrepareRootElementVa = 0x1800AD730;

// The reference build's StartUI_.dll version. `Wh_ModInit` resolves version.dll
// at runtime and `StartUIBuildMatches` compares all four parts; a different
// build gets no root hook instead of a hook at a guessed address.
constexpr WORD kStartUIBuildMajor = 10;
constexpr WORD kStartUIBuildMinor = 0;
constexpr WORD kStartUIBuildNumber = 22621;
constexpr WORD kStartUIBuildRevision = 3733;

// Bytes 0x00..0x1C of PrepareRootElement on the reference build:
//   mov [rsp+8], rbx; mov [rsp+0x10], rsi; mov [rsp+0x18], rdi
//   push rbp; push r12; push r13; push r14; push r15
//   mov rbp, rsp; sub rsp, 0x80
//   mov rsi, rdx (perFrameData); mov r13, rcx (frame)
// A `call` to the function's first helper follows at +0x3E; its rel32 target
// is resolved as well, so the RVA cannot point at a different function that
// happens to start with the same eleven instructions.
constexpr uint8_t kPreparePrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08,  // mov qword ptr [rsp+0x08], rbx
    0x48, 0x89, 0x74, 0x24, 0x10,  // mov qword ptr [rsp+0x10], rsi
    0x48, 0x89, 0x7C, 0x24, 0x18,  // mov qword ptr [rsp+0x18], rdi
    0x55,                          // push rbp
    0x41, 0x54,                    // push r12
    0x41, 0x55,                    // push r13
    0x41, 0x56,                    // push r14
    0x41, 0x57,                    // push r15
    0x48, 0x8B, 0xEC,              // mov  rbp, rsp
    0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,  // sub rsp, 0x80
    0x48, 0x8B, 0xF2,              // mov  rsi, rdx
    0x4C, 0x8B, 0xE9,              // mov  r13, rcx
};
constexpr size_t kPrepareCallOffset = 0x3E;
constexpr uintptr_t kPrepareFirstCalleeVa = 0x1800B2684;

// StartUI::SplitViewFrame::OnGlobalAnimationRequested(sender, args) -> void
// StartUI_.dll 10.0.22621.3733 (x86-64).
//
// The handler forwards the shell's animation request to a delegate stored at
// this+0x3C0. The animation type is NOT an argument: the handler reads three
// ints from the args object through the wrappers below (vtable slots 6, 7 and
// 8). The function never writes eax and has no direct call site, so its return
// value is unused and the hook must return void as well.
constexpr uintptr_t kOnGlobalAnimationRequestedVa = 0x1804164C0;
constexpr uintptr_t kAnimTypeGetterVa = 0x18041A978;
constexpr uintptr_t kAnimEdgeGetterVa = 0x18041AA20;
constexpr uintptr_t kAnimTransitionGetterVa = 0x18041A9CC;

// Bytes 0x00..0x27 of the target. The three E8 calls at +0x27, +0x33 and
// +0x3F read the animation type, the taskbar edge and the transition type; the
// installer resolves their rel32 targets before it trusts this layout.
constexpr uint8_t kAnimPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,  // mov qword ptr [rsp+0x10], rbx
    0x48, 0x89, 0x74, 0x24, 0x20,  // mov qword ptr [rsp+0x20], rsi
    0x57,                          // push rdi
    0x48, 0x83, 0xEC, 0x70,        // sub  rsp, 0x70
    0x48, 0x8B, 0x05,              // mov  rax, [rip+cookie]
    0x00, 0x00, 0x00, 0x00,        //      ... displacement, checked below
    0x48, 0x33, 0xC4,              // xor  rax, rsp
    0x48, 0x89, 0x44, 0x24, 0x60,  // mov  qword ptr [rsp+0x60], rax
    0x49, 0x8B, 0xD8,              // mov  rbx, r8   (args)
    0x48, 0x8B, 0xF9,              // mov  rdi, rcx  (self)
    0x49, 0x8B, 0xC8,              // mov  rcx, r8
};

// ---------------------------------------------------------------------------
// The gesture settle: one damped spring, two parameter sets
//
// The setting picks the parameter set; every outcome uses the picked one: the
// dismiss commit and cancel, the reveal commit and cancel. Both styles are the
// same generator in normalized travel, so open and close mirror exactly and
// both start at the finger's release speed.
//
// Why a spring and not a bezier. A cubic-bezier with a fixed duration has an
// initial slope that is only a multiple of the animation's own average speed,
// and covering the remaining travel in a fixed time forces the curve to
// accelerate. cubic-bezier(0.1, 0.9, 0.2, 1), the style this mod used to call
// Windows 11, starts at 9x its average speed: on a panel released half open it
// moved 21.6 DIP in the first millisecond. That is the jump the user saw. A
// spring's initial velocity is the finger's release speed (scaled by the
// setting) in either direction, and its damping supplies the deceleration.
//
// The spring works in normalized travel `y = (p - from) / (to - from)`, so
// y(0) = 0 and y(inf) = 1. With natural frequency omega, damping ratio zeta and
// `decay = zeta*omega`:
//
//   underdamped (zeta < 1), wd = omega*sqrt(1 - zeta^2):
//     y(t) = 1 - exp(-decay*t)
//                 * (cos(wd*t) + ((decay - y0) / wd) * sin(wd*t))
//   critically damped (zeta = 1):
//     y(t) = 1 - exp(-omega*t) * (1 + (omega - y0) * t)
//   overdamped (zeta > 1), wd = omega*sqrt(zeta^2 - 1):
//     y(t) = 1 - exp(-decay*t)
//                 * (cosh(wd*t) + ((decay - y0) / wd) * sinh(wd*t))
//
// y0 = y'(0) is the scaled release speed divided by the travel left in DIP.
// The gesture reports that speed as a magnitude (DIP/ms along the axis, away
// from the release point), so y0 is positive and the spring always relaxes from
// 0 toward 1; `(to - from)` carries the direction of the panel.
//
// The duration comes from the speed, not from a fitted time. The native Quick
// Settings flyout is 578 DIP tall and travels it in 262 ms, an average of
// 2.2 DIP/ms. Copying its 253 ms instead made the 900+ DIP Start menu race and
// made the feel follow the panel's size, so every settle lasts the time its own
// travel needs at that speed:
//
//   remaining = |to - from| * Distance                DIP of panel travel
//   T         = clamp(remaining / 2.2 DIP/ms, 160, 520)  ms
//   omega     = 5 / (time constant of the slowest root * T)
//
// The frequency follows from T, not from a period. Five time constants of the
// slowest root land exactly on T. That root is zeta*omega while the spring
// oscillates, so omega = 5/(zeta*T) there, and omega*(zeta - sqrt(zeta^2-1))
// once it is overdamped: applying zeta*omega to the overdamped Windows 11 set
// would put only 0.93 time constants in T and leave 44% of the travel for the
// last frame. Either way the curve is one shape stretched in time: the damping
// ratio is the style, T is the travel.
//
// The analytic curve is 1 - exp(-5) ~ 0.7% short of the target at T. Writing
// the exact target on a final keyframe made the panel hop that remainder after
// a frame that had moved under 0.1 DIP (3.8 DIP on a 501 DIP travel), so the
// whole curve is scaled by its own value at T instead: every sample rises by the
// same fraction and the last one reaches the target with no step of its own.
// One segment is at most 4 ms, so at 120 Hz (8.3 ms per frame) no corner can
// fall inside a displayed frame; the floor of 12 segments keeps the shape of
// the shortest settle, and 128 caps the keyframe count.
//
// Windows 10 keeps the damping this mod shipped with: 0.9.
//
// Windows 11 is fitted to the native flyout on this machine. Measured with
// Win+A (Quick Settings, the same Fluent motion), 9 ms per frame, panel offset
// tracked against its settled frame: the native show travels its 578 DIP in
// 262 ms. The area-minimizing spring with y0 = 0 over that curve is damping
// 1.72, period 0.102 s at 578 DIP: mean |y - d| = 0.034 over the 306 ms
// measured, at most 0.012 after the first 90 ms. The damping is what the fit
// keeps; the period is derived from the travel now, so the 900 DIP panel takes
// 409 ms and moves at the flyout's 2.2 DIP/ms. The native curve is faster than
// that spring in its first 30 ms (0.54 of the travel against 0.38), because the
// shell starts that animation with its own initial velocity; a settle that must
// continue the finger cannot copy that, and this fit deliberately matches the
// deceleration after the first frames instead.
// ---------------------------------------------------------------------------
constexpr double kWin10SpringDamping = 0.9;
constexpr double kWin11SpringDamping = 1.72;
constexpr double kSettleTimeConstants = 5.0;
// The native panel's average speed: 578 DIP in 262 ms. Duration is travel
// divided by this, so the 900 DIP menu takes 409 ms and a half-open one 205 ms,
// whatever the panel measures.
constexpr float kSettleSpeedDipPerMs = 2.2f;
constexpr std::chrono::milliseconds kSettleMinDuration{160};
constexpr std::chrono::milliseconds kSettleMaxDuration{520};
// One segment per 4 ms: at 120 Hz a segment is shorter than one compositor
// frame, so a corner never lands inside a displayed frame.
constexpr double kSettleSegmentMs = 4.0;
constexpr int kSettleMinSteps = 12;
constexpr int kSettleMaxSteps = 128;
// The frames: the segments plus the landing frame at T.
constexpr int kSettleMaxFrames = kSettleMaxSteps + 1;

// The Windows 11 style used to be one eased pair on the Fluent decelerate
// curve over a fixed 300 ms. That curve is gone: it starts at 9x its average
// speed, which is the jump a release at half open produced. Both styles now run
// the spring below.

// The gesture start-speed setting: the settle's initial velocity as a
// percentage of the component of the finger's end speed that points at the
// target. 100 means that component, divided then only by the curve's own
// end-value scale (about 1/0.993); 0 starts from rest. Both styles read it.
constexpr int kGestureStartSpeedDefault = 100;
constexpr int kGestureStartSpeedMin = 0;
constexpr int kGestureStartSpeedMax = 300;

// Every gesture close ends in one Windows tap at the end of its settle. The tap
// is the shell's own Start toggle: the key handler performs Hide, so no
// explorer-side request and no re-arm tap exist. The settle runs first, so the
// panel is already off screen when the window hides; the close channel tells the
// bridge that the Hide belongs to this tap and needs no further delay.
constexpr uint64_t kCloseTapDelayMs = 0;

// A close this mod started hides the menu a moment later. Until then the shell
// still reports the window open, so the reopen poll must not read that as a
// fresh open and restore Progress while the close is in flight.
constexpr uint64_t kRestoreSuppressMs = 600;

// The tap has to run inside that window, or the poll reads the still-open window
// as a fresh open and restores Progress while the close is in flight. Both
// styles must land inside it, and the clamp is their upper bound.
static_assert(static_cast<uint64_t>(kSettleMaxDuration.count()) <=
                  kRestoreSuppressMs,
              "the close tap must run inside the restore suppression");

// Gesture thresholds. Delta and Distance are DIP, velocity is DIP/ms.
constexpr float kCommitDistanceFraction = 0.20f;
// A vertical dismiss starts counting only once the list underneath has reached
// its end, so the remaining drag is short by nature; 20% of the panel would ask
// for a drag most people never make.
constexpr float kCommitDistanceFractionVertical = 0.10f;
constexpr float kCommitVelocity = 0.45f;
constexpr float kScrollAbortPerp = 120.0f;
constexpr float kScrollAbortRatio = 1.5f;
constexpr float kScrollAbortMaxDelta = 60.0f;
// The insert filter is a screen distance, not a progress fraction: one step of
// the scalar must move the panel by at least this many DIP. A fixed 0.004 was
// 5.8 DIP on the 1440 DIP distance here, which is 11.5 physical px at 200%
// scaling and read as a staircase during a drag. The panel reports at ~170 Hz,
// so writing Progress per report is affordable and buys a continuous follow.
constexpr float kProgressEpsilonDip = 0.5f;
constexpr float kWindowMarginDip = 4.0f;

// A vertical dismiss and the app list's own scroll are the same downward drag.
// The element that actually scrolls the app list is cached per open, and a
// dismiss may only start on it when it is already at the end the drag would
// scroll away from: a list at its top cannot scroll further up, so the drag has
// nothing left to do and belongs to the dismiss. `kScrollerEndSlopDip` absorbs
// the fraction of a DIP a viewport leaves at its end; `kScrollerAxisSlopDip`
// decides which viewer counts as scrollable at all (the wrapper ScrollViewer
// reports a horizontal extent of 8380 and must never be picked).
// The list rarely stops exactly at its end after a fling, so the end test is
// generous; the panel-band test keeps a tight tolerance.
constexpr float kScrollerEndSlopDip = 24.0f;
constexpr float kPanelBandSlopDip = 2.0f;
constexpr float kScrollerAxisSlopDip = 0.5f;
constexpr uint32_t kScrollerScanMaxDepth = 32;
// The reject line is diagnostic, not a per-sample measurement: the digitizer
// reports at ~170 Hz, so it speaks at most once per window.
constexpr uint64_t kDismissRejectLogMs = 500;

// Reveal follow (edge swipe to open). A contact that starts inside the edge
// band on the taskbar side can drive the menu's own open. The release decision
// uses the dismiss thresholds on the travel away from the edge, not on the
// mapped progress: the screen edge already maps to Progress 0.269 here, so any
// progress threshold commits every release.
constexpr float kEdgeBandDip = 12.0f;
// The panel reports at ~170 Hz (about 6 ms apart) and keeps reporting while the
// finger rests, so a short silence really is a lift. 18 ms is above the 15.6 ms
// quantum that an injected step really takes (measured: Sleep(8) in pwsh yields
// 15-16 ms gaps), so a step never reads as a lift, and the 5 ms high-resolution
// idle tick then lands the release 18-23 ms after the last report.
constexpr uint64_t kIdleReleaseMs = 18;
constexpr UINT_PTR kIdleTimerId = 1;
constexpr UINT kIdleTimerMs = 5;  // also the waitable timer period
// The DWM cloaked query is a syscall. The report handler accepts one answer
// per interval and reuses it for every other report.
constexpr uint64_t kOpenCheckIntervalMs = 20;

// A measured panel rectangle is this layout's panel only when it lies on the
// monitor and the side against the taskbar sits on the work-area edge (or on the
// monitor edge, when the taskbar overlays the screen), because that is where the
// shell attaches the menu.
//
// `kPanelLayoutMarginDip` rounds a border that sits exactly on an edge; nothing
// larger is a layout fact. `kPanelAnchorMarginDip` is the same rounding for the
// taskbar band, whose own width is the real tolerance. Neither is
// `kScrollerEndSlopDip` (24 DIP): that number is a scroll-position allowance for
// a list a fling stops short of its end, and reusing it for layout is what
// accepted a storyboard frame on a work area whose origin is 0 (this machine:
// panel rect 12,-12..1005,539 while the settled rect is 411,163..1404,714).
constexpr float kPanelLayoutMarginDip = 2.0f;
constexpr float kPanelAnchorMarginDip = 2.0f;

// The settled measurement's guaranteed path. XAML may never deliver a second
// LayoutUpdated while nothing moves, so a request that only waits for the layout
// watch could wait forever; this deadline runs the same pass.
constexpr uint64_t kSettledMeasureDeadlineMs = 400;

// ---------------------------------------------------------------------------
// Coast between reports
//
// The release is one idle window plus one tick: report, 18 ms of silence, then
// the tick that plans the close, measured 20-23 ms. The panel stood still for
// that whole window, and the eye reads the stop before the retract starts. The
// panel now keeps moving at the finger's own speed until the close takes over:
//
// - the speed is the average over the last three reports, so one slow report
//   cannot fling the panel, and it is capped at 6 DIP/ms so a bad sample cannot
//   throw it across the screen either;
// - the coast starts after 8 ms without a report, so it is already moving when
//   the release is planned 10-15 ms later;
// - a report ends it, pins the interpolated value and inserts the real one, so
//   the correction is the few DIP the panel drifted;
// - the release ends it first and plans the close from where it got to.
//
// The 200 ms run is the animation's own length, not how long it lives: the
// release or the next report always interrupts it. `Progress` is linear here,
// never eased: a curve would slow the panel in the first frames, which is the
// stop the coast exists to hide.
//
// The real digitizer reports at ~170 Hz (5.9 ms), so a contact that is still
// down never leaves an 8 ms gap and a held finger never coasts.
constexpr uint64_t kCoastStartMs = 8;
constexpr std::chrono::milliseconds kCoastDuration{200};
// Below this speed the coast would move the panel about one DIP in the window it
// lives, and a report stream slower than the panel's own, such as an injector's
// 10 ms keep-alive, must not nudge it. No coast starts there.
constexpr float kCoastMinVelocityDip = 0.1f;
// Progress moves with the panel in both gestures, so the one clamp serves both:
// DIP per millisecond, reached here with Distance.
constexpr float kCoastMaxVelocityDip = 6.0f;
// The reveal maps Progress from the panel edge the snapshot carries; for the
// first 50 ms of a follow that mapping is still settling.
constexpr uint64_t kRevealCoastSettleMs = 50;
// The coast start/stop lines measured the coast; off in the shipping build.
constexpr bool g_debugCoast = false;

// Raw input and HID decoding limits.
constexpr int kMaxHidDevices = 4;
constexpr int kMaxContactCollections = 8;
constexpr int kMaxLinkNodes = 64;
constexpr int kHidPathChars = 512;
constexpr wchar_t kHidWindowClass[] =
    L"WindhawkLegacyStartSwipeRawInputWindow";

constexpr USAGE kUsagePageGeneric = 0x01;
constexpr USAGE kUsagePageDigitizer = 0x0D;
constexpr USAGE kUsageTouchScreen = 0x04;
constexpr USAGE kUsagePen = 0x02;
constexpr USAGE kUsageContact = 0x05;
constexpr USAGE kUsageFinger = 0x22;
// A pen's data lives in its own link collection, not in a contact collection.
constexpr USAGE kUsageStylus = 0x20;
constexpr USAGE kUsageX = 0x30;
constexpr USAGE kUsageY = 0x31;
constexpr USAGE kUsageInRange = 0x32;
constexpr USAGE kUsageTipSwitch = 0x42;
constexpr USAGE kUsageContactCount = 0x54;

// ICoreWindowInterop (45D64A29-A63E-4CB6-B498-5781D298CB4F). Slot 3 is
// get_WindowHandle; the interface is classic COM, so it is called directly.
constexpr GUID kCoreWindowInteropIid = {
    0x45D64A29, 0xA63E, 0x4CB6,
    {0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F}};
using GetCoreWindowHandleAbi = HRESULT(STDMETHODCALLTYPE*)(void*, HWND*);

// DWMWA_CLOAKED (dwmapi.h value 14). The CoreWindow HWND outlives the menu and
// IsWindowVisible stays TRUE while it is closed, so cloaking is the only
// reliable open/closed signal: 0 while the menu is shown.
constexpr DWORD kDwmwaCloaked = 14;
using DwmGetWindowAttribute_t = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);

// ---------------------------------------------------------------------------
// Timer resolution
//
// The idle timer detects the release: the tick is every 5 ms, but the process
// keeps the default 15.6 ms system timer quantum, so a 20 ms silence plus one
// quantum measured `relms=31`. Two separate clocks have to be fixed, and each
// was measured on this build (26300.9974):
//
// - the tick: a 1 ms timeBeginPeriod request is held while a contact is down,
//   as asked, and it raises the process resolution (Sleep(1) takes 1-2 ms).
//   WM_TIMER ignores it: a 5 ms SetTimer still delivered every 15.6 ms. The
//   idle tick is therefore a waitable timer created with
//   CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, which fires at the requested 5 ms
//   with or without the period request (measured 5-6 ms). SetTimer remains the
//   fallback when the timer cannot be created.
// - the clock: GetTickCount64 also advances only every 15.6 ms, so an 18 ms
//   silence read as 31. The silence is measured with QueryPerformanceCounter
//   (QpcMsNow below).
//
// The silence window itself is 18 ms: above the 15.6 ms quantum that an
// injected step really takes, below the 20 ms the mod used to wait. A 15 ms
// window split every injected step into its own one-sample session.
// ---------------------------------------------------------------------------

constexpr UINT kContactTimerPeriodMs = 1;
using TimePeriod_t = UINT(WINAPI*)(UINT);

// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (winbase.h 0x00000002). Spelled out so
// the build does not depend on the MinGW headers carrying it.
constexpr DWORD kCreateWaitableTimerHighResolution = 0x00000002;

std::atomic<TimePeriod_t> g_timeBeginPeriod{nullptr};
std::atomic<TimePeriod_t> g_timeEndPeriod{nullptr};

bool g_timerPeriodHeld = false;  // input thread only

void BeginContactTiming() {
    if (g_timerPeriodHeld) {
        return;
    }

    const TimePeriod_t begin =
        g_timeBeginPeriod.load(std::memory_order_relaxed);
    if (!begin || begin(kContactTimerPeriodMs) != 0) {
        // No 1 ms quantum: the release still works on the default tick, only
        // later. The flag stays clear so no unmatched end is sent.
        return;
    }

    g_timerPeriodHeld = true;
}

void EndContactTiming() {
    if (!g_timerPeriodHeld) {
        return;
    }

    g_timerPeriodHeld = false;
    const TimePeriod_t end = g_timeEndPeriod.load(std::memory_order_relaxed);
    if (end) {
        end(kContactTimerPeriodMs);
    }
}

// The idle tick, independent of the 15.6 ms WM_TIMER quantum. Created on the
// input thread and closed by it; nullptr means "use the WM_TIMER tick", which
// is slower but still releases.
HANDLE CreateIdleWaitableTimer() {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                                          kCreateWaitableTimerHighResolution,
                                          TIMER_ALL_ACCESS);
    if (!timer) {
        return nullptr;
    }

    // Relative due time in 100 ns units, so the first tick is one period away.
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(kIdleTimerMs) * 10000;
    if (!SetWaitableTimer(timer, &due, kIdleTimerMs, nullptr, nullptr, FALSE)) {
        CloseHandle(timer);
        return nullptr;
    }

    return timer;
}

// GetTickCount64 advances on the system tick (15.6 ms here), so it cannot tell
// an 18 ms silence from a 31 ms one: an 18 ms threshold read as 31 and the
// release stayed where it was. The contact silence that ends a gesture is
// measured with QueryPerformanceCounter; the coarse tick stays for the velocity
// windows and the suppression deadlines, which do not need better than a tick.
uint64_t QpcMsNow() {
    static const uint64_t frequency = []() {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart > 0 ? static_cast<uint64_t>(value.QuadPart)
                                  : 1;
    }();

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart) * 1000 / frequency;
}

enum class DismissAxis : int32_t {
    Left = 0,
    Top = 1,
    Right = 2,
    Bottom = 3,
};

bool IsHorizontalAxis(int32_t axis) {
    return axis == static_cast<int32_t>(DismissAxis::Left) ||
           axis == static_cast<int32_t>(DismissAxis::Right);
}

// The translation the composition expression applies at Progress 0: the
// direction the panel leaves along and the root extent it travels. Setup bakes
// these in, and the event-driven refresh rewrites them when the taskbar moves.
void AxisTranslation(int32_t axis, float width, float height, float& dirX,
                     float& dirY, float& distance) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
            dirX = -1.0f;
            dirY = 0.0f;
            distance = width;
            break;
        case DismissAxis::Top:
            dirX = 0.0f;
            dirY = -1.0f;
            distance = height;
            break;
        case DismissAxis::Right:
            dirX = 1.0f;
            dirY = 0.0f;
            distance = width;
            break;
        case DismissAxis::Bottom:
            dirX = 0.0f;
            dirY = 1.0f;
            distance = height;
            break;
    }
}

std::atomic<bool> g_disabled{false};
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_stopDismissCallback{false};

// The open storyboard drops only when the debug line `anim=0` asks for it.
// Measured on this build: skipping the open request leaves the window open
// (DWMWA_CLOAKED 0) with no pane drawn, because that request is also the step
// that presents the SplitViewFrame content. The switch therefore starts at the
// original behaviour.
std::atomic<bool> g_suppressOpenAnim{false};
std::atomic<int32_t> g_animLogCount{0};

// ---------------------------------------------------------------------------
// The settle style setting
//
// The setting stores its display label, so the label the user picks is the
// value the code compares: exactly "Windows 10" selects the spring, and every
// other value (including the default "Windows 11") selects the Fluent curve. A
// missing or unrecognized value therefore lands on the default behaviour
// instead of inventing a third one.
// ---------------------------------------------------------------------------
enum class SettleStyle : int32_t {
    Win11 = 0,
    Win10 = 1,
};

constexpr wchar_t kAnimationStyleWin10[] = L"Windows 10";

// Read once at init and once per settings change; every settle reads this
// atomic instead of the setting string.
std::atomic<SettleStyle> g_settleStyle{SettleStyle::Win11};

const wchar_t* SettleStyleName(SettleStyle style) {
    return style == SettleStyle::Win10 ? L"win10" : L"win11";
}

// Reads the style setting into the atomic. Wh_ModInit calls it once and
// Wh_ModSettingsChanged calls it again, so no settle pays for a string lookup.
void LoadAnimationStyle() {
    PCWSTR value = Wh_GetStringSetting(L"AnimationStyle");
    const SettleStyle style =
        wcscmp(value, kAnimationStyleWin10) == 0 ? SettleStyle::Win10
                                                 : SettleStyle::Win11;
    FileLog(L"settle style selected=%s setting=%s", SettleStyleName(style),
            value);
    Wh_FreeStringSetting(value);
    g_settleStyle.store(style, std::memory_order_relaxed);
}

// The percentage from the mod settings: the Windows 10 spring's initial
// velocity as a percentage of the finger's release speed. Wh_ModInit and
// Wh_ModSettingsChanged write it, every spring settle reads it once; no setting
// lookup runs per settle.
std::atomic<int> g_gestureStartSpeedPercent{kGestureStartSpeedDefault};

// Reads the setting into the atomic, clamped. Wh_ModInit calls it once and
// Wh_ModSettingsChanged calls it again, so no settle pays for a setting lookup.
void LoadGestureStartSpeed() {
    const int percent =
        std::clamp(Wh_GetIntSetting(L"GestureStartSpeedPercent"),
                   kGestureStartSpeedMin, kGestureStartSpeedMax);
    g_gestureStartSpeedPercent.store(percent, std::memory_order_relaxed);
    FileLog(L"settle start speed=%d%%", percent);
}

// ---------------------------------------------------------------------------
// Composition state
//
// Progress is the only animated scalar; Distance is the static window width
// or height. Both the property set and the visual are agile Composition
// objects, so the input thread and the settle batch callback can use them.
// The mutex protects the projection pointers and the check-then-update
// sequence against teardown; it is never held across a wait.
// ---------------------------------------------------------------------------

// `Progress` 1 is the menu at rest on screen, 0 is it fully slid out.
constexpr float kProgressRest = 1.0f;

std::mutex g_compositionMutex;
wuc::Compositor g_compositor{nullptr};
wuc::CompositionPropertySet g_state{nullptr};
wuc::ExpressionAnimation g_translationExpression{nullptr};
wuc::Visual g_visual{nullptr};
wuc::CompositionScopedBatch g_dismissBatch{nullptr};
// The Completed registration of that batch, kept so the handler can be removed
// before the module goes away. Written under g_compositionMutex.
winrt::event_token g_dismissBatchToken{};

// The value the renderer currently shows. Composition has no read-back, so every
// write to `Progress` records it here: the base a direct insert writes, the value
// a coast is stopped at, and the target a settle inserts before StartAnimation.
// While an animation runs this is the animation's TARGET, not the position; the
// only reader allowed to turn it into a position is `TakeProgressNow`.
std::atomic<float> g_progressNow{kProgressRest};

// The settle the compositor is playing right now: the start, the start time, the
// duration and the exact samples handed over as keyframes. `g_progressNow` is
// where that animation is heading, and the read-back that could tell the two
// apart does not exist (StopCoast records the measurement: TryGetScalar answers
// with the base value while the animation is young). So the curve is kept here,
// and `TakeProgressNow` answers where the panel is at any instant, exactly as
// `CoastValueAt` does for the coast. Every stop or replacement clears the frame
// count, so nothing can plan from a curve that is no longer on screen. Written
// under g_compositionMutex.
float g_settleSamples[kSettleMaxFrames] = {};
int g_settleFrames = 0;
uint64_t g_settleStartQpc = 0;
uint64_t g_settleDurationMs = 0;

// The coast owns Progress between reports. Its endpoints and start time are kept
// here because the animation is linear: `CoastValueAt` answers where the panel is
// at any instant, and StopCoast turns that answer into a base value so the next
// sample or the close continues from it. All four are input thread state; the
// flag is atomic because ResetComposition clears it from the UI thread.
std::atomic<bool> g_coasting{false};
uint64_t g_coastStartQpc = 0;
float g_coastFrom = 1.0f;
float g_coastTo = 1.0f;

wux::FrameworkElement g_root{nullptr};  // UI thread only
// Guarded by g_compositionMutex: Setup writes it on the UI thread, the input
// thread copies it into a local before posting, and the unload revoke copies it
// the same way. A CoreDispatcher copy is a COM reference-counted pointer, so it
// must never be read on one thread while another overwrites it.
wucore::CoreDispatcher g_dispatcher{nullptr};
std::atomic<DWORD> g_uiThreadId{0};
std::atomic<uint32_t> g_setupGeneration{0};

// The settled-trail watch. The open transition re-measures the panel edge once,
// but that measurement can land while the shell's open storyboard is still
// growing the frame into place (measured on a bottom taskbar: panel top -12 DIP
// where the settled panel starts at 200). The layout event is the only signal
// that says the panel has been placed, so the watch counts layout passes and
// corrects the trail on the second one, then detaches itself.
//
// `g_layoutTicksLeft` is 0 when no watch is armed, which is also what the close
// teardown leaves behind; both fields are UI thread only.
winrt::event_token g_layoutToken{};
uint32_t g_layoutTicksLeft = 0;
// The element the layout token was registered on, so its revoke does not depend
// on g_root still pointing at it.
wux::FrameworkElement g_layoutRoot{nullptr};

// ---------------------------------------------------------------------------
// The scrolling element of the open menu
//
// A vertical dismiss and the app list's own scroll are the same downward drag,
// so the dismiss needs to know whether the element under the press point can
// still scroll away from it. Measured on this build, exactly one element does
// that: the ScrollViewer inside StartUI.AllAppsGridListView, XAML
// 85,230-345,839, ScrollableHeight 3664, VerticalOffset 0 on every open. Its
// screen rect, offset and extent are cached per open and published as plain
// values; the input thread reads them once per down and never touches XAML.
//
// The numbers belong together, so they are one POD behind a mutex instead of
// four atomics: a torn rect/offset pair could allow or reject the wrong press.
// The lock is taken once per down on the input side and only when the cache is
// measured or scrolled on the UI side, so it is never contended.
//
// "Nothing scrollable here" and "not measured yet" were the same `valid == false`
// cache, and the gate read both as "cannot decide", so a panel with genuinely
// nothing scrollable could never be dismissed from inside. They are separate
// states now:
//
// - Unknown: no measurement that speaks for this open — nothing measured yet, a
//   measurement that failed, or the open-time pass whose rect belongs to a window
//   the shell is still moving. The gate defers while the press is inside the
//   panel and lets a press outside it pass.
// - Nothing: measured after the shell settled, and nothing in the panel can
//   scroll. Every vertical press belongs to the dismiss.
// - Scrollable: measured after the shell settled; a press inside the rect may
//   scroll, so the end-of-list test decides.
// ---------------------------------------------------------------------------
enum class ScrollerState {
    Unknown = 0,
    Nothing = 1,
    Scrollable = 2,
};

struct ScrollerCache {
    ScrollerState state = ScrollerState::Unknown;
    uint32_t generation = 0;  // the open epoch this rect was measured in
    float left = 0.0f;  // screen DIP, all four from the XAML origin
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float verticalOffset = 0.0f;
    float scrollableHeight = 0.0f;
};

std::mutex g_scrollerMutex;
ScrollerCache g_scroller;

// UI thread only: the element the cache describes and its ViewChanged handler.
// A stale handler is revoked before the next element replaces it.
wuxc::ScrollViewer g_scrollerViewer{nullptr};
winrt::event_token g_scrollerToken{};
// The rect line speaks once per open, not once per measurement: the open
// transition and the settled pass both measure the same element, and only a
// rect that actually moved is worth a second line. The `scroller none` line is
// the settled pass telling the log that the gate now allows a press that has
// nothing to scroll, so it is also once per open.
bool g_scrollerLogged = false;
bool g_scrollerNoneLogged = false;
float g_scrollerLoggedRect[4] = {};
// Coarse tick of the last `dismiss deferred to scroll` line. The digitizer
// reports at ~170 Hz, so the rejection is rate limited; input thread only.
uint64_t g_scrollerRejectLogTick = 0;

winrt::event_token g_loadedToken{};
winrt::event_token g_sizeToken{};
// The tokens belong to the objects they were registered on, and a Setup that
// never finished leaves g_root null while a Loaded handler is still attached.
// These keep the exact element so the revoke can run unconditionally.
wux::FrameworkElement g_loadedRoot{nullptr};   // UI thread only
wux::FrameworkElement g_sizeRoot{nullptr};     // UI thread only

std::atomic<bool> g_dismissSettling{false};
std::atomic<uint32_t> g_dismissSerial{0};
// Set whenever this mod starts a close itself: the committed dismiss and the
// cancelled reveal both set it when their settle starts. PollRootVisibility
// must not read the still-open window as a reopen while the close is in flight.
// 0 means no close of this mod is pending.
std::atomic<uint64_t> g_suppressRestoreUntil{0};

// ---------------------------------------------------------------------------
// The gesture close
//
// Both close paths end in the same place: one Windows tap at the end of the
// settle. The tap is the shell's own Start toggle, so the key handler runs
// CStartExperienceManager::Hide exactly as it does for a keyboard close, and the
// next Windows press opens the menu. The settle runs first, so the panel is off
// screen when the window hides.
//
// A new pull that starts during the settle cancels the close: the tap is
// dropped before the shell is touched, so no Hide runs and the menu stays open.
// The pull generation is sampled at the release and checked once when the tap
// would run; the cancel is published the moment the pull begins, not at the
// tap's deadline, so no Hide in between can be read as the tap's. This is one
// scheduled tap, not a request file.
// ---------------------------------------------------------------------------
enum class CloseKind {
    None,
    Dismiss,
    RevealCancel,
};

// A pull is a contact that starts in the taskbar edge band and becomes a reveal
// candidate (BeginEdgeCandidate). The counter names the pull generation: a close
// released under one generation is dropped when a later pull exists by the time
// its tap would run.
uint32_t g_pullSerial = 0;  // input thread only

// The one tap slot. A close cannot start while another tap is still waiting.
uint64_t g_pendingTapTick = 0;  // input thread only, 0 = none
CloseKind g_pendingTapKind = CloseKind::None;
uint32_t g_pendingTapPull = 0;

// Arms the tap for a close that has just started its settle. The deadline is
// the end of that settle.
void ScheduleCloseTap(CloseKind kind, uint64_t delayMs) {
    g_pendingTapTick = GetTickCount64() + delayMs;
    g_pendingTapKind = kind;
    g_pendingTapPull = g_pullSerial;
}

std::atomic<HWND> g_coreHwnd{nullptr};
// Resolved in Wh_ModInit. A runtime resolve keeps the mod loadable without
// dwmapi.dll in the import table.
std::atomic<DwmGetWindowAttribute_t> g_dwmGetWindowAttribute{nullptr};

// The version.dll entry points the root hook's build gate needs. Resolved at
// init like the two above, so the mod links without version.lib.
using GetFileVersionInfoSizeW_t = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
using GetFileVersionInfoW_t = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
using VerQueryValueW_t = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
std::atomic<GetFileVersionInfoSizeW_t> g_getFileVersionInfoSize{nullptr};
std::atomic<GetFileVersionInfoW_t> g_getFileVersionInfo{nullptr};
std::atomic<VerQueryValueW_t> g_verQueryValue{nullptr};

// Filled in at setup and on every PrepareRootElement.
int32_t g_dismissAxis = static_cast<int32_t>(DismissAxis::Left);
float g_dismissDistance = 0.0f;

// Published by the UI thread and read by the input thread. No XAML object is
// shared: only plain values.
//
// The renderer generation answers "is there a property set and an expression to
// drive right now". Setup raises it, ResetComposition clears it when the root,
// its property set and its expression are dropped. A historical "was set up
// once" latch cannot answer that: after a ResetRenderer the old flag stayed set
// and a gesture could start against a renderer that no longer existed.
std::atomic<uint32_t> g_rendererGeneration{0};

bool RendererReady() {
    return g_rendererGeneration.load(std::memory_order_acquire) != 0;
}
// Diagnostics for a dismiss that did not start: one line per second, not per
// report, because a deferred scroll produces a sample every ~6 ms.
uint64_t g_skipLogTick = 0;
std::atomic<int64_t> g_winRectDip[4]{};  // left, top, right, bottom

// ---------------------------------------------------------------------------
// The gesture geometry snapshot
//
// Placing a press, mapping the reveal follow and planning a settle all need the
// same measurement: which side the taskbar is on, how far the root travels, where
// the visible panel is and which of its edges meets the finger. Published as
// separate atomics, a rejected measurement still wrote its rectangle, so a "keep
// the last good value" decision left a rect from the new measurement beside a
// trail from the old one, and the hit test and the follow then read different
// geometries. The rect and the trail could not even be compared: nothing said
// which measurement either belonged to.
//
// One plain struct carries the whole value behind a short writer lock: the UI
// thread is the only writer and takes `g_geometryLock` exclusively for one struct
// assignment; the input thread tries a shared acquisition per sample and answers
// its last accepted copy when the writer holds the lock. A seqlock alone was not
// enough here: its sequence numbers detect a change but do not legalize reading
// the plain struct while the writer assigns it, which is a data race under the
// C++ memory model however wide the fences are.
//
// `layout` names the whole layout the snapshot was taken in: the axis, the
// travel, the XAML client origin and the work area and monitor. `layoutVersion`
// changes whenever that identity changes, so a reader holding a cached copy can
// tell that it predates a resize and must ask the writer again instead of
// starting a gesture from an old rectangle.
//
// `generation` names the root/open epoch the measurement belongs to. Every
// recorded request and every published snapshot carries it, so a measurement
// recorded before an open cannot be published for the next one.
// ---------------------------------------------------------------------------
struct LayoutIdentity {
    int32_t axis = static_cast<int32_t>(DismissAxis::Left);
    float distance = 0.0f;  // the root extent the composition slides by, DIP
    float originX = 0.0f;   // XAML client origin, absolute screen DIP
    float originY = 0.0f;
    float workLeft = 0.0f;
    float workTop = 0.0f;
    float workRight = 0.0f;
    float workBottom = 0.0f;
    float monitorLeft = 0.0f;
    float monitorTop = 0.0f;
    float monitorRight = 0.0f;
    float monitorBottom = 0.0f;
};

struct GestureGeometry {
    uint32_t generation = 0;     // root/open epoch the measurement was taken in
    uint32_t layoutVersion = 0;  // identity token, bumped when `layout` changes
    LayoutIdentity layout;
    bool panel = false;          // left/top/right/bottom hold a real measurement
    float left = 0.0f;           // the visible panel, absolute screen DIP
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    int32_t axis() const { return layout.axis; }
    float distance() const { return layout.distance; }
};

// The one "same travel" policy. RefreshGestureGeometry's change test and the
// carry-over test in the publish path both ask this function, so a layout can
// never count as unchanged in one place and changed in the other.
constexpr float kLayoutDistanceEpsilonDip = 0.5f;

bool SameLayoutDistance(float a, float b) {
    return std::fabs(a - b) <= kLayoutDistanceEpsilonDip;
}

// The whole identity, not just the axis and the travel: the same extents on a
// different work area, taskbar thickness or cross-axis origin put the panel
// somewhere else, and a rect carried across that boundary is not this layout's
// panel.
bool SameLayoutIdentity(const LayoutIdentity& a, const LayoutIdentity& b) {
    return a.axis == b.axis && SameLayoutDistance(a.distance, b.distance) &&
           a.originX == b.originX && a.originY == b.originY &&
           a.workLeft == b.workLeft && a.workTop == b.workTop &&
           a.workRight == b.workRight && a.workBottom == b.workBottom &&
           a.monitorLeft == b.monitorLeft && a.monitorTop == b.monitorTop &&
           a.monitorRight == b.monitorRight &&
           a.monitorBottom == b.monitorBottom;
}

// Odd while g_geometry is being written is no longer needed: the writer holds
// this lock exclusively and a reader that cannot take it shared uses its cache.
SRWLOCK g_geometryLock = SRWLOCK_INIT;
// The identity token of the latest published layout. The writer raises it when
// the identity changes and stamps the snapshot with it; a reader compares its
// cached copy against it before starting a gesture.
std::atomic<uint32_t> g_layoutVersion{1};
GestureGeometry g_geometry;       // guarded by g_geometryLock, writer copy
GestureGeometry g_inputGeometry;  // input thread only: the last whole read

// The epoch a measurement belongs to: a root installed or dropped, or an open or
// close of the reused root. 0 is reserved for "no expectation", so the count
// starts at 1.
std::atomic<uint32_t> g_openGeneration{1};

float TrailEdge(int32_t axis, float left, float top, float right, float bottom);
float GeometryTrailDip(const GestureGeometry& geometry);
float FallbackTrailDip(int32_t axis, float distance);
bool PanelContainsPress(const GestureGeometry& geometry, float x, float y);
GestureGeometry ReadGestureGeometry(bool waitForWriter = false);
bool GeometryLayoutCurrent(const GestureGeometry& geometry);
void PublishGestureGeometry(const GestureGeometry& candidate);

// Input thread → UI thread: a live contact drives the panel edge right now. A
// request recorded on an idle panel must not move that edge under a finger that
// started in between, so the measurement re-reads this when its turn runs.
std::atomic<bool> g_panelOwned{false};

// ---------------------------------------------------------------------------
// The settled-measurement request
//
// The open only says "the layout is about to move": the panel's final place and
// the list's final extent arrive at the second LayoutUpdated pass, and the first
// pass usually precedes them. One request is therefore recorded when the menu
// opens and when the system announces a work-area or display change, and two
// triggers can complete it — the layout watch's second pass and a deadline,
// whichever comes first.
//
// Both triggers used to run their own copy of the work, the trail-idle decision
// was frozen when it was queued, and neither checked the state when it ran, so a
// stale post could measure a menu that had already closed. One request now does
// both: it carries a serial that a newer request replaces, the root/open epoch it
// was recorded in and the trail-idle decision; the turn that runs it re-reads the
// epoch, whether a root is attached and whether a gesture owns the panel, and the
// first trigger to run ends the request so the other finds nothing to do.
//
// The lock is taken once per open and once per deadline, never per report.
// ---------------------------------------------------------------------------
struct SettledMeasureRequest {
    uint32_t id = 0;          // 0 when no request is live
    uint32_t generation = 0;  // the epoch it was recorded in
    bool trailIdle = false;   // no contact owned the edge when it was recorded
    uint64_t dueTick = 0;     // its deadline, 0 once the deadline was taken
};

std::mutex g_measureMutex;
SettledMeasureRequest g_measureRequest;
uint32_t g_measureSerial = 0;  // input thread only

SettledMeasureRequest StartMeasureRequest(bool trailIdle);
bool TakeMeasureDeadline(uint64_t tickMs, SettledMeasureRequest& request);
bool MeasureRequestLive(const SettledMeasureRequest& request);
bool MeasureRequestLiveId(uint32_t id);
void EndMeasureRequest(uint32_t id);
void CancelMeasureRequest();
void CancelMeasureRequestIfId(uint32_t id);
SettledMeasureRequest SnapshotMeasureRequest();

// A settled scroller pass can fail to commit: the layout tree threw, the depth
// limit cut the search short, or a contact owned the panel while it ran. The
// cache stays Unknown then, and the input thread re-posts the same settled pass
// after this delay while the same open is still on screen. `generation` binds
// the retry to that open, so a close or a new open drops it.
constexpr uint64_t kScrollerRetryMs = 100;
std::atomic<uint64_t> g_scrollerRetryAt{0};
std::atomic<uint32_t> g_scrollerRetryGeneration{0};

// UI thread only. Setup resets the open log; a failed mapping logs its fallback
// once per process so a missing panel does not flood the log every resize. A
// measurement rejected by the layout guard logs once, and the last good snapshot
// stays published whole.
bool g_panelTrailLogged = false;
bool g_panelTrailFallbackLogged = false;
bool g_panelTrailRejectedLogged = false;

HANDLE g_hidThread = nullptr;
std::atomic<HWND> g_hidHwnd{nullptr};
// The input thread's stop signal, created at init: the thread's wait watches it
// next to the idle tick and the two IPC events, so it leaves the loop even when
// the thread has not published g_hidHwnd yet. Set once, by the unload.
HANDLE g_hidStopEvent = nullptr;
// Set when a thread outlived its wait. Unload then leaves every resource in
// place and says so: the module cannot honestly claim it is gone while module
// code still runs on a thread Windhawk is about to unmap.
std::atomic<bool> g_hidThreadLeaked{false};
std::atomic<bool> g_pollThreadLeaked{false};

// One count per registered XAML callback (Loaded, SizeChanged, LayoutUpdated,
// ViewChanged). Every registration is revoked on the object it was made on; the
// count is the unload's proof that nothing is left to fire, and it is what lets
// the unload tell "nothing was ever registered" from "the dispatcher could not
// revoke it". The settle batch is not counted here: its token is revoked and its
// already-dispatched callback drained separately.
std::atomic<uint32_t> g_uiRegistrationCount{0};

// The settle batch's Completed handler may already be dispatched when the
// token is revoked. The handler raises this on entry and lowers it on every exit;
// the unload drains it before the composition objects are dropped.
std::atomic<int> g_dismissCallbackDepth{0};

// A SizeChanged that runs while a contact owns the panel defers its measurement
// instead of re-anchoring under the finger. The input thread posts the deferred
// settled pass when ownership ends.
std::atomic<bool> g_measureDeferred{false};

// Defined with the unload helpers; PrepareRootElement uses them to reset on
// every open.
void RevokeHandlers();
void ResetComposition();
void ResetRenderer();
HWND FindStartCoreWindow();

// Defined with the UI-thread publication below. The one geometry measurement and
// its single publish point: `measurePanel` re-derives the rect, and
// `expectedGeneration` is the epoch a queued request was recorded in (0 for the
// synchronous Setup/SizeChanged paths). `requestId` is 0 on those synchronous
// paths and the live request's id otherwise: the publish re-checks it at commit,
// after the candidate is built, so a superseded turn publishes nothing.
bool PublishPanelTrailDip(bool measurePanel, uint32_t expectedGeneration,
                          uint32_t requestId);

// Defined with the UI-thread publication below. Posts one turn of the measurement
// for a request: the open/work-area turn passes `settled` false, the deadline and
// the layout watch pass it true for the pass after the shell placed the panel.
void PostGeometryMeasurement(const SettledMeasureRequest& request, bool settled);

// Defined with the UI-thread publication below. The open transition arms this
// watch, which runs the settled measurement on the second layout pass, when the
// shell's open storyboard has placed the panel. The close teardown disarms it.
void ArmLayoutWatch(const SettledMeasureRequest& request);
void StopSettledTrailWatch();
void StopSettledTrailWatchIfGeneration(uint32_t maxGeneration);

// Defined with the scroller cache below. Measures the scrolling element an open
// menu scrolls with, publishes its screen rect and offsets for the input thread,
// and drops the cached element and its ViewChanged handler on close/teardown. It
// validates the request and the epoch when it commits, and an incomplete or
// owned pass leaves the state Unknown for the retry below.
void CacheScrollableElement(bool settled, uint32_t expectedGeneration,
                            uint32_t requestId);
void ReleaseScrollableElement();
void ReleaseScrollableElementIfGeneration(uint32_t maxGeneration);
void ClearScrollerRetry();

// Defined with the UI-thread publication below. Recomputes the dismiss axis and
// the translation the composition expression bakes in, and rewrites the
// composition scalars when either moved. It publishes nothing: its callers
// publish the one snapshot afterwards. False when the composition write failed:
// the axis and travel are then left at the values the expression still holds.
bool RefreshGestureGeometry();

// Defined with the input-thread gesture state below. Publishes whether a live
// contact drives the panel edge, which the UI-side measurement re-reads when it
// runs. Called after every state change on that thread.
void PublishPanelOwnership();

// Defined with the input-thread gesture state below. The debug `retract` command
// clears it: the shell is about to hide the window, so nothing needs to restore
// `Progress`, and the next open restores it anyway.
extern bool g_commitNeedsProgressReset;

// Defined with the gesture state below: both close families open the same
// ownership state (no session drives Progress, and the suppression window is
// open) through this one helper.
void TakeCloseOwnership(uint64_t tickMs, bool contactStillDown);

// The cross-process channel with the explorer-side bridge and the debug tools.
// Defined with the channel helpers below; a gesture close publishes its plan,
// its cancel and the moment its Windows tap goes down through it.
SwipeIpcChannel g_ipc;

// The close record goes through the shared channel below, on the input thread:
// the plan when the tap is armed, the cancel when a new pull drops it, and the
// tapped record immediately before the tap itself, so the bridge can tell the
// tap's own Hide from a keyboard close.

// Defined with the input-thread gesture state below; the close tap needs the
// kind for its log line.
const wchar_t* CloseKindName(CloseKind kind);

// ---------------------------------------------------------------------------
// Composition helpers
// ---------------------------------------------------------------------------

// The dispatcher is UI-thread state, so every other thread copies it here first:
// the copy holds its own reference and cannot race the next Setup. No lock is
// held while the caller posts.
wucore::CoreDispatcher CopyDispatcher() {
    std::lock_guard lock(g_compositionMutex);
    return g_dispatcher;
}

// The settle batch's generation is the only proof that a batch completion
// belongs to the animation that is still on screen. The compositor runs a
// `Completed` handler after the fact, so a late one can arrive when a newer
// settle already owns `Progress`; the handler compares its generation with this
// counter and touches nothing when they differ. Every path that stops or replaces
// the settle animation therefore advances the counter, and the `g_dismissSettling`
// flag ends with the settle it described. Callers hold g_compositionMutex.
void InvalidateDismissBatchLocked() {
    g_dismissSerial.fetch_add(1, std::memory_order_relaxed);
    g_dismissSettling.store(false, std::memory_order_relaxed);
}

// Detaches the stored batch's Completed handler so a completion that has not been
// dispatched yet never reaches module code that no longer owns the animation.
// Caller holds g_compositionMutex. A handler the compositor has already
// dispatched cannot be recalled here, which is why the generation check in the
// handler exists as well.
void RevokeDismissBatchLocked() {
    wuc::CompositionScopedBatch batch = g_dismissBatch;
    winrt::event_token token = g_dismissBatchToken;
    g_dismissBatch = nullptr;
    g_dismissBatchToken = {};
    if (batch && token.value) {
        try {
            batch.Completed(token);
        } catch (...) {
            // A batch mid-completion has no handler left to remove. The unload
            // drains the callback separately, so there is no residual claim
            // here.
        }
    }
}

bool ShowAtRest() {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state) {
        return false;
    }

    try {
        g_state.StopAnimation(L"Progress");
        // No settle is on screen any more, so no retract may plan from its curve
        // and no batch completion may still stop Progress.
        g_settleFrames = 0;
        RevokeDismissBatchLocked();
        InvalidateDismissBatchLocked();
        g_coasting.store(false, std::memory_order_relaxed);
        g_state.InsertScalar(L"Progress", kProgressRest);
        g_progressNow.store(kProgressRest, std::memory_order_relaxed);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Composition reset failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Composition reset failed with an unknown exception");
        return false;
    }

    return true;
}

bool InsertProgress(float progress) {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state) {
        return false;
    }

    try {
        // A direct insert is the new base value, so any animation driving the
        // scalar ends here: the sample it belongs to has already been replaced.
        // A coast is the usual case; a settle still on screen is the takeover
        // case, and its batch completion must not fire against the new value.
        const bool coasting =
            g_coasting.exchange(false, std::memory_order_relaxed);
        if (coasting || g_settleFrames > 0) {
            if (g_settleFrames > 0) {
                RevokeDismissBatchLocked();
                InvalidateDismissBatchLocked();
                g_settleFrames = 0;
            }
            g_state.StopAnimation(L"Progress");
        }
        g_state.InsertScalar(L"Progress", progress);
        g_progressNow.store(progress, std::memory_order_relaxed);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Progress update failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Progress update failed with an unknown exception");
        return false;
    }

    return true;
}

// The one Progress animation in this file: a sampled polyline. Every keyframe is
// linear and no easing function is attached, because the spring samples are the
// curve. An easing function on a segment would zero or reshape the initial slope
// the spring exists to keep. The frame count is 13-129: one segment per 4 ms of
// the settle plus the landing frame at T, where the scaled curve is the target.
//
// Every open and every close comes through here, from a gesture and from the
// Windows key alike, so there is one motion and no second curve to drift from it.
//
// The sample list starts at the current position, so the animation needs no
// separate start value: `samples[0]` is where the panel is.
//
// The target is inserted as the base value *before* StartAnimation, so a
// cancelled or interrupted animation still lands on the intended end state
// instead of snapping back to the drag position.
bool AnimateProgressTo(float to, std::chrono::milliseconds duration,
                       const float* settleSamples, int settleSampleCount,
                       uint32_t* serialOut) {
    if (serialOut) {
        *serialOut = 0;
    }
    std::lock_guard lock(g_compositionMutex);
    if (!g_state || !g_compositor || !settleSamples || settleSampleCount < 2) {
        return false;
    }

    try {
        wuc::ScalarKeyFrameAnimation animation =
            g_compositor.CreateScalarKeyFrameAnimation();
        // Equal steps across the duration; the first sample is the release
        // position and the last is the target.
        const float last = static_cast<float>(settleSampleCount - 1);
        for (int i = 0; i < settleSampleCount; ++i) {
            animation.InsertKeyFrame(
                static_cast<float>(i) / last,
                std::clamp(settleSamples[i], 0.0f, 1.0f));
        }
        animation.Duration(duration);

        const uint64_t startQpc = QpcMsNow();
        g_state.StopAnimation(L"Progress");
        // The previous curve stops here, so it may not be read as a position from
        // this point on, and its batch completion may not touch the animation that
        // replaces it. If the new animation never starts, the base value inserted
        // below is where the panel is.
        g_settleFrames = 0;
        RevokeDismissBatchLocked();
        g_coasting.store(false, std::memory_order_relaxed);
        g_state.InsertScalar(L"Progress", std::clamp(to, 0.0f, 1.0f));
        g_state.StartAnimation(L"Progress", animation);
        g_progressNow.store(std::clamp(to, 0.0f, 1.0f),
                            std::memory_order_relaxed);
        // This animation replaces whatever owned Progress: an older batch's
        // completion must not stop it, and no dismiss settle is still running.
        // The serial this animation installs is minted here, under the same lock
        // that started it: a batch that reads it later cannot capture the serial
        // of a reset that happened in between.
        InvalidateDismissBatchLocked();
        if (serialOut) {
            *serialOut = g_dismissSerial.load(std::memory_order_relaxed);
        }
        // Keep the curve for every interruption, which has to know where this
        // animation leaves the panel. The samples are the keyframes just handed
        // over, so the interpolation is the same motion the compositor plays.
        const int frames = std::min(settleSampleCount, kSettleMaxFrames);
        std::memcpy(g_settleSamples, settleSamples, sizeof(float) * frames);
        g_settleFrames = frames;
        g_settleStartQpc = startQpc;
        g_settleDurationMs = static_cast<uint64_t>(duration.count());
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Progress animation failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Progress animation failed with an unknown exception");
        return false;
    }

    return true;
}

// Where the linear coast is at qpcNow: from + (to - from) * elapsed / duration.
// Exact, because the animation has no easing and its start time is known here.
float CoastValueAt(uint64_t qpcNow) {
    if (qpcNow <= g_coastStartQpc) {
        return g_coastFrom;
    }

    const uint64_t duration = static_cast<uint64_t>(kCoastDuration.count());
    const uint64_t elapsed = qpcNow - g_coastStartQpc;
    if (elapsed >= duration) {
        return g_coastTo;
    }

    const float t = static_cast<float>(elapsed) / static_cast<float>(duration);
    return std::clamp(g_coastFrom + (g_coastTo - g_coastFrom) * t, 0.0f, 1.0f);
}

// Where the recorded settle puts `Progress` at qpcNow. Its keyframes are exactly
// g_settleSamples, equally spaced and linear, so the value is one interpolation
// between two of them. Once the duration has passed the answer is the last
// sample, which is the target the animation was handed and where the panel
// actually is. Caller holds g_compositionMutex.
float SettleValueAt(uint64_t qpcNow) {
    if (g_settleFrames < 2 || g_settleDurationMs == 0) {
        return std::clamp(g_progressNow.load(std::memory_order_relaxed), 0.0f,
                          1.0f);
    }
    if (qpcNow <= g_settleStartQpc) {
        return std::clamp(g_settleSamples[0], 0.0f, 1.0f);
    }

    const uint64_t elapsed = qpcNow - g_settleStartQpc;
    if (elapsed >= g_settleDurationMs) {
        return std::clamp(g_settleSamples[g_settleFrames - 1], 0.0f, 1.0f);
    }

    const double position = static_cast<double>(elapsed) /
                            static_cast<double>(g_settleDurationMs) *
                            (g_settleFrames - 1);
    const int index = std::min(static_cast<int>(position), g_settleFrames - 2);
    const float fraction = static_cast<float>(position - index);
    const float value =
        g_settleSamples[index] +
        (g_settleSamples[index + 1] - g_settleSamples[index]) * fraction;
    return std::clamp(value, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Where `Progress` is at time t: one owner
// ---------------------------------------------------------------------------
//
// `g_progressNow` records the base value written to the property set, and a
// settle inserts its TARGET as that base before it animates the samples over it.
// While an animation runs, the recorded value is therefore where the panel is
// going, not where it is. Reading it as a position made every interruption start
// the panel at the animation's end and jump there before moving again: the
// keyboard retract during the open settle, a reveal taking a close over, one
// settle replacing another, and the close after a coast. A settle started from a
// target value is the bug this function prevents.
//
// This is the single entry point, and it takes rather than reads: it answers from
// the curve the compositor was handed (or from the coast's own line), stops that
// motion, and pins the answer as the new base value. The caller starts from a
// panel that is standing still where its last frame left it. With nothing
// recorded nothing animates, so the base value is a real position and the answer
// is the resting value.
float TakeProgressNow() {
    const uint64_t qpcNow = QpcMsNow();

    std::lock_guard lock(g_compositionMutex);
    if (!g_state) {
        // No renderer, so no animation can be on screen and the resting value is
        // the only answer that cannot be a stale target.
        return kProgressRest;
    }

    const bool coasting = g_coasting.load(std::memory_order_relaxed);
    const bool settling = g_settleFrames >= 2 && g_settleDurationMs != 0;
    if (!coasting && !settling) {
        // Nothing animates, so the panel is at rest and the value every stop
        // wrote IS its resting position: the base value is only a target while an
        // animation runs over it, and no path leaves a stopped animation's target
        // behind (ShowAtRest, StopCoast, the take itself and every direct insert
        // write the value first). With no renderer there is no recording at all,
        // and the resting value 1 is the only answer that cannot be a target.
        return std::clamp(g_progressNow.load(std::memory_order_relaxed), 0.0f,
                          1.0f);
    }

    const float value =
        std::clamp(coasting ? CoastValueAt(qpcNow) : SettleValueAt(qpcNow),
                   0.0f, 1.0f);
    try {
        g_state.StopAnimation(L"Progress");
        g_state.InsertScalar(L"Progress", value);
        g_progressNow.store(value, std::memory_order_relaxed);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Progress take failed: %08X %s",
               static_cast<unsigned>(e.code()), e.message().c_str());
        return value;
    } catch (...) {
        Wh_Log(L"Progress take failed with an unknown exception");
        return value;
    }

    // The motion is gone: neither its curve nor its batch completion may act on
    // the value that was just pinned.
    g_settleFrames = 0;
    g_settleDurationMs = 0;
    g_coasting.store(false, std::memory_order_relaxed);
    RevokeDismissBatchLocked();
    InvalidateDismissBatchLocked();
    return value;
}

// Starts the continuation between reports. This is deliberately not
// AnimateProgressTo: that helper inserts the target as the base value, and a
// coast must leave the base alone so a report can replace the motion without the
// panel jumping to the end of the coast first. Nothing is logged per coast in the
// shipping build.
bool StartCoast(float from, float to) {
    const uint64_t qpcNow = QpcMsNow();
    from = std::clamp(from, 0.0f, 1.0f);
    to = std::clamp(to, 0.0f, 1.0f);

    std::lock_guard lock(g_compositionMutex);
    if (!g_state || !g_compositor) {
        return false;
    }

    try {
        wuc::ScalarKeyFrameAnimation animation =
            g_compositor.CreateScalarKeyFrameAnimation();
        animation.InsertKeyFrame(0.0f, from);
        // No easing on the keyframe at t=1: the panel keeps the finger's speed
        // for the whole run, which is the only reason the coast is invisible.
        animation.InsertKeyFrame(1.0f, to);
        animation.Duration(kCoastDuration);

        g_state.StopAnimation(L"Progress");
        g_state.StartAnimation(L"Progress", animation);
        // The coast now owns Progress; a settle curve and its batch recorded
        // earlier are gone.
        g_settleFrames = 0;
        RevokeDismissBatchLocked();
        InvalidateDismissBatchLocked();
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Progress coast failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Progress coast failed with an unknown exception");
        return false;
    }

    g_coastFrom = from;
    g_coastTo = to;
    g_coastStartQpc = qpcNow;
    g_coasting.store(true, std::memory_order_relaxed);
    return true;
}

// Ends the coast and pins the value the panel shows at this instant, so the next
// sample or the close animation continues from a moving panel instead of
// snapping back to the last report.
//
// The linear line above is the source of truth, not a read-back: measured on this
// build, CompositionPropertySet.TryGetScalar answers with the stored base value
// while an animation younger than one compositor frame is running (a coast that
// lived 15 ms at -0.23 DIP/ms read back 0.722, the value of the report before it,
// where the line read 0.720). Pinning the read-back would undo exactly the motion
// the coast was started for.
bool StopCoast(float& value) {
    const uint64_t qpcNow = QpcMsNow();
    value = CoastValueAt(qpcNow);

    std::lock_guard lock(g_compositionMutex);
    g_coasting.store(false, std::memory_order_relaxed);
    if (!g_state) {
        return false;
    }

    try {
        // The animation has to be stopped before its value can become the base.
        g_state.StopAnimation(L"Progress");
        g_settleFrames = 0;
        RevokeDismissBatchLocked();
        InvalidateDismissBatchLocked();
        g_state.InsertScalar(L"Progress", value);
        g_progressNow.store(value, std::memory_order_relaxed);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Progress coast stop failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Progress coast stop failed with an unknown exception");
        return false;
    }

    if (g_debugCoast) {
        FileLog(L"coast stop value=%.3f", value);
    }
    return true;
}

// The settle every gesture outcome runs: the dismiss commit, the dismiss
// cancel, the reveal commit and the reveal cancel. `from` is where the panel is
// at release, `to` is 0 for a close and 1 for an open, and `velocity` is the
// release speed in Progress per millisecond, signed along the gesture axis, as
// the end-velocity estimator reports it. `duration` is the animation's length,
// which is also what the close tap is armed with.
//
// One generator, two parameter sets. The style setting only picks the damping
// ratio; everything below runs once for both:
//
// - Open and close are exact mirrors, because the spring works in normalized
//   travel: y(0) = 0, y(inf) = 1, and y'(0) = y0 is the scaled release speed
//   divided by the travel left. The sign of the settle direction cancels there,
//   because (to - from) carries it; the spring always relaxes toward its target.
//   A release at rest gets y0 = 0, the plain step response, so a finger held
//   still for a moment starts the settle at zero speed.
//   A release faster than the remaining travel can absorb overshoots the
//   target, and the 0..1 clamp turns that overshoot into a plateau at the rest
//   position instead of a translation past it.
// - The duration is the travel divided by kSettleSpeedDipPerMs, clamped to
//   160-520 ms, so the panel moves at one speed whatever its size is. The
//   frequency is derived from that duration (see the constant block), so five
//   time constants of the spring's slowest root land on T.
// - The frames are a polyline of 12-128 equal segments, at most 4 ms each, plus
//   one landing frame at T. The whole curve is scaled by its own value at T, so
//   the samples converge on the target with no step at the landing. Each sample
//   is clamped to 0..1 and to the previous sample, so convergence is monotonic.
// - Both sets are overdamped or near it; the sampler therefore carries the
//   underdamped, critically damped and overdamped forms of the same response.

// At most 4 ms per segment, at least 12, at most 128. The samples take one
// segment less than the frames: the last frame is the scaled curve at T, which is
// the target.
int SettleStepsFor(int64_t durationMs) {
    const double steps =
        std::ceil(static_cast<double>(durationMs) / kSettleSegmentMs);
    return std::clamp(static_cast<int>(steps), kSettleMinSteps,
                      kSettleMaxSteps);
}

bool BeginGestureSettle(float from, float to, float velocity,
                        std::chrono::milliseconds& duration,
                        uint32_t* serialOut = nullptr) {
    const float start = std::clamp(from, 0.0f, 1.0f);
    const float target = std::clamp(to, 0.0f, 1.0f);

    const SettleStyle style = g_settleStyle.load(std::memory_order_relaxed);
    const double zeta =
        style == SettleStyle::Win11 ? kWin11SpringDamping : kWin10SpringDamping;

    // Plan from the travel, not from a time: the native flyout crosses its 578
    // DIP in 262 ms and this keeps that speed, so a 900 DIP panel takes 409 ms
    // and a resized one takes what its own travel needs. The travel is the
    // snapshot's, so the plane the gesture followed is the plane the settle plans
    // against.
    const float distance =
        std::max(ReadGestureGeometry().distance(), 0.0f);
    const float remaining = std::fabs(target - start) * distance;
    const int64_t durationMs = std::clamp(
        static_cast<int64_t>(std::llround(
            static_cast<double>(remaining) /
            static_cast<double>(kSettleSpeedDipPerMs))),
        static_cast<int64_t>(kSettleMinDuration.count()),
        static_cast<int64_t>(kSettleMaxDuration.count()));
    duration = std::chrono::milliseconds(durationMs);

    const float startScale =
        static_cast<float>(g_gestureStartSpeedPercent.load(
            std::memory_order_relaxed)) /
        100.0f;

    // `velocity` is Progress per millisecond, signed along the gesture axis:
    // positive means Progress is rising. The settle only ever moves toward its
    // target, so a release that is still moving away from it (a pull back before
    // letting go) contributes nothing instead of starting the settle backwards;
    // the finger has already reversed and there is no speed to carry that way.
    const float travel = target - start;  // signed, Progress units
    const float toward =
        travel == 0.0f ? 0.0f
                       : std::max(0.0f, travel > 0.0f ? velocity : -velocity);

    // Work in milliseconds throughout: the duration is in ms and y0 is 1/ms.
    // Keeping y0 in seconds would make the initial slope 1000x too small and
    // leave the seam without the finger's velocity. The travel is in Progress
    // units, so dividing by it cancels the same `Distance` the estimator divided
    // by: both gesture families hand over Progress/ms and one formula serves them.
    const double tTotal = static_cast<double>(durationMs);  // ms

    // Normalized initial slope, 1/ms. The sign cancels against (to - from), so
    // this is always positive: the spring leaves the release position toward
    // the target in both directions.
    const double y0 =
        std::fabs(travel) > 0.0f
            ? static_cast<double>(startScale * toward) /
                  static_cast<double>(std::fabs(travel))
            : 0.0;

    // Pick omega so that five time constants of the slowest root land on T.
    // That root is zeta*omega while the spring oscillates; once it is
    // overdamped the slow pole is omega*(zeta - sqrt(zeta^2-1)), which is why
    // the ratio is normalized by the slowest root instead of by zeta.
    const bool overdamped = zeta > 1.0;
    const bool critical = std::fabs(zeta - 1.0) <= 1e-9;
    const double slowestFactor =
        overdamped ? zeta - std::sqrt(zeta * zeta - 1.0) : zeta;
    const double omega = kSettleTimeConstants / (slowestFactor * tTotal);
    const double decay = zeta * omega;  // 1/ms
    const double wd =
        overdamped
            ? omega * std::sqrt(zeta * zeta - 1.0)
            : omega * std::sqrt(std::max(0.0, 1.0 - zeta * zeta));

    // The samples stop one segment short of T; the frame after them is the
    // landing at T. The analytic curve is still short of the target at T:
    // five time constants of the slowest root leave 1 - exp(-5) ~ 0.7%, which is
    // 3.8 DIP on a 501 DIP travel and read as one small hop when the landing
    // frame carried the target itself. Scaling the whole curve by its own value
    // at T makes every sample 0.7% larger instead, so the samples converge on the
    // target with no step at all.
    const auto curveAt = [&](double t) -> double {
        const double envelope = std::exp(-decay * t);
        if (overdamped) {
            return 1.0 - envelope *
                             (std::cosh(wd * t) +
                              ((decay - y0) / wd) * std::sinh(wd * t));
        }
        if (critical) {
            return 1.0 - envelope * (1.0 + (omega - y0) * t);
        }
        return 1.0 - envelope *
                         (std::cos(wd * t) +
                          ((decay - y0) / wd) * std::sin(wd * t));
    };
    // A curve that already reaches the target before T is left alone: it is
    // clamped to 1 there and the landing frame adds nothing.
    const double endValue = std::clamp(curveAt(tTotal), 0.5, 1.0);

    const int steps = SettleStepsFor(durationMs);
    const int frameCount = steps + 1;
    float samples[kSettleMaxFrames] = {};
    samples[0] = start;
    float previous = start;
    for (int i = 1; i < steps; ++i) {
        const double t = tTotal * i / static_cast<double>(steps);
        const double y = std::clamp(curveAt(t) / endValue, 0.0, 1.0);
        float value = std::clamp(
            start + (target - start) * static_cast<float>(y), 0.0f, 1.0f);
        // Monotonic convergence: a sample never moves the panel back past one
        // it already showed, so an overshoot reads as a stop at rest and a
        // ringing curve never rebounds on screen.
        value = target >= start ? std::max(value, previous)
                                : std::min(value, previous);
        samples[i] = value;
        previous = value;
    }
    samples[steps] = target;

    if (!AnimateProgressTo(target, duration, samples, frameCount, serialOut)) {
        return false;
    }

    FileLog(L"settle style=%s dir=%s vel=%.2f dur=%llu remaining=%.0f "
            L"frames=%d zeta=%.2f",
            SettleStyleName(style), target >= start ? L"open" : L"close",
            toward * distance, static_cast<unsigned long long>(duration.count()),
            remaining, frameCount, zeta);
    return true;
}

// The one dismissal every close uses. The tap is the shell's own Start toggle:
// the key handler decides Hide exactly as it does for a keyboard close, so a
// menu opened without keyboard focus closes too, where an injected VK_ESCAPE is
// ignored. The 60 ms hold is the press the dismiss path always used; measured, a
// 20 ms hold was missed intermittently and the menu stayed open. It runs on the
// input thread and is global: it lands on whichever window has the foreground.
void TapWindowsKey() {
    keybd_event(VK_LWIN, 0, 0, 0);
    // The 60 ms hold is the press the dismiss path always used. Waiting on the
    // stop event instead of Sleep lets an unload observe the stop at once; the
    // key is still released either way, so no Windows key is left down.
    if (g_hidStopEvent) {
        WaitForSingleObject(g_hidStopEvent, 60);
    } else {
        Sleep(60);
    }
    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
}

const wchar_t* CloseKindName(CloseKind kind) {
    return kind == CloseKind::Dismiss ? L"dismiss" : L"reveal";
}

// The release speed as the seam and the release line report it: DIP per
// millisecond toward the settle's target. `progressPerMs` is signed along the
// gesture axis, so the projection onto the settle's direction is made once here.
// A release still moving away from its target contributes 0: the finger has
// already reversed, and a settle must never leave the seam backwards.
//
// Called by BeginGestureSettle and by both release lines, so the number the log
// prints is the number the spring starts with.
float SpeedTowardDip(float progressPerMs, float from, float to, float distance) {
    if (to == from) {
        return 0.0f;  // the panel already stands at its target
    }
    const float toward =
        std::max(0.0f, to > from ? progressPerMs : -progressPerMs);
    return toward * std::max(distance, 0.0f);
}

// The tail every close shares: ownership, the close plan the bridge reads, and
// the tap armed for the end of the settle. The dismiss and the reveal cancel both
// come through here, so the two families cannot diverge. The plan is not the tap:
// it is published tapped = 0, so a Hide that arrives while the settle runs (the
// user pressing the key) is a keyboard close for the bridge and gets its own
// lead.
void ArmGestureClose(CloseKind kind, std::chrono::milliseconds duration) {
    // A finger that has just lifted is already leaving the panel, so this close
    // does not have to mute a contact that will keep reporting.
    TakeCloseOwnership(GetTickCount64(), false);
    if (!SwipeIpcPublishClose(&g_ipc, kSwipeCloseKindGesture, kCloseTapDelayMs,
                              0, g_pullSerial)) {
        FileLog(L"close mode stamp failed kind=%s", CloseKindName(kind));
    }
    ScheduleCloseTap(kind, static_cast<uint64_t>(duration.count()));
}

// The commit decision both gesture families make at release. The travel at
// release and the session average are the whole-gesture tests this mod was
// measured against; only the settle's seam reads the end speed. One function so
// the dismiss and the reveal cannot drift apart on the thresholds.
//
// `travelDip` is the release travel away from the start, `elapsedMs` the session
// length, and `distance` the panel travel the threshold is a fraction of.
bool ReleaseCommits(bool allowCommit, float travelDip, uint64_t elapsedMs,
                    float distance, int32_t axis) {
    if (!allowCommit || !(distance > 0.0f)) {
        return false;
    }
    const float averageVelocity =
        travelDip / static_cast<float>(elapsedMs > 0 ? elapsedMs : 1);
    const float threshold = IsHorizontalAxis(axis)
                                ? kCommitDistanceFraction
                                : kCommitDistanceFractionVertical;
    return travelDip > threshold * distance ||
           averageVelocity > kCommitVelocity;
}

// The close every gesture path runs, from the value the coast left. A close
// exists only once its settle started: the suppression, the stamp and the tap
// follow that animation, never a failed one. The tap is armed for the end of the
// settle, so the panel is off screen when the shell hides the window. The
// dismiss wraps this call in its settle batch; the reveal cancel calls it
// directly.
bool BeginGestureClose(float from, float velocity, CloseKind kind,
                       uint32_t* serialOut = nullptr) {
    std::chrono::milliseconds duration{0};
    if (!BeginGestureSettle(from, 0.0f, velocity, duration, serialOut)) {
        return false;
    }
    ArmGestureClose(kind, duration);
    return true;
}

// The open counterpart of BeginGestureClose: the same settle style and the same
// release velocity, mirrored. A reveal commit and a cancelled dismiss
// therefore settle with one mechanism and only the target differs.
bool BeginGestureOpen(float from, float velocity) {
    std::chrono::milliseconds duration{0};
    return BeginGestureSettle(from, 1.0f, velocity, duration);
}

// Removes the settle batch's Completed handler. Called before a new batch
// replaces the old one and at unload, so no compositor callback can run module
// code that is about to disappear. The handler also checks
// g_stopDismissCallback and its serial, because a callback already dispatched
// cannot be recalled.
void RevokeDismissBatch() {
    std::lock_guard lock(g_compositionMutex);
    RevokeDismissBatchLocked();
}

// Raises the in-flight count the unload drains. A completion the compositor has
// already dispatched cannot be recalled by revoking its token; this guard is how
// the unload can prove the callback has finished before the composition state
// goes away.
struct DismissCallbackGuard {
    DismissCallbackGuard() {
        g_dismissCallbackDepth.fetch_add(1, std::memory_order_relaxed);
    }
    ~DismissCallbackGuard() {
        g_dismissCallbackDepth.fetch_sub(1, std::memory_order_relaxed);
    }
};

// The close both gesture paths schedule. The settle runs first; the tap is
// armed for its end, where RunPendingCloseTap checks the pull generation and
// either taps or drops the close. No deferral file and no bridge request exist.
//
// Committed dismiss: the settle batch observes the settle, the shared close
// tail arms the tap. The settle itself, the log line, the suppression window,
// the close stamp and the tap all come from BeginGestureClose, so this path
// and the reveal cancel cannot diverge.
bool BeginDismissSettle(float from, float velocity) {
    wuc::CompositionScopedBatch batch{nullptr};
    winrt::event_token batchToken{};
    uint32_t serial = 0;
    bool handlerRegistered = false;

    // One batch at a time: the previous handler goes before this one exists.
    RevokeDismissBatch();

    try {
        {
            std::lock_guard lock(g_compositionMutex);
            if (!g_state || !g_compositor) {
                return false;
            }

            // The batch must be open before the animation starts, or it scopes
            // nothing. Its handler is registered after the start, because only
            // then is the generation of the animation that is actually on screen
            // known.
            batch = g_compositor.CreateScopedBatch(
                wuc::CompositionBatchTypes::Animation);
        }

        // The serial is minted inside AnimateProgressTo, under the lock that
        // starts the animation. Reading it after that lock is released let a
        // reset in between hand this batch the replacement's serial.
        if (!BeginGestureClose(from, velocity, CloseKind::Dismiss, &serial)) {
            // No animation, so the batch holds nothing and no handler may be
            // registered: a close that never settled must not clear a flag.
            {
                std::lock_guard lock(g_compositionMutex);
                InvalidateDismissBatchLocked();
            }
            batch.End();
            return false;
        }

        // The serial belongs to the animation on screen only if nothing replaced
        // it while the lock was free. A reset that did replaced it; register
        // nothing then.
        bool serialLive = false;
        {
            std::lock_guard lock(g_compositionMutex);
            serialLive =
                serial != 0 &&
                serial == g_dismissSerial.load(std::memory_order_relaxed);
            if (serialLive) {
                g_dismissSettling.store(true, std::memory_order_relaxed);
            }
        }
        if (!serialLive) {
            batch.End();
            return false;
        }

        batchToken = batch.Completed(
            [serial](
                winrt::Windows::Foundation::IInspectable const&,
                wuc::CompositionBatchCompletedEventArgs const&) {
                DismissCallbackGuard guard;
                try {
                    std::lock_guard lock(g_compositionMutex);

                    // The stop flag and the generation guard the state update
                    // against unload and against a newer settle or reveal.
                    if (g_stopDismissCallback.load(
                            std::memory_order_relaxed) ||
                        serial != g_dismissSerial.load(
                                      std::memory_order_relaxed)) {
                        return;
                    }

                    // This is the settle that registered the handler, and it has
                    // reached its target. Leave Progress there: resetting it
                    // here flashes the fully open menu for the frames between
                    // this reset and the shell hiding the window. The next
                    // session resets it instead. The hide itself is the Windows
                    // tap's job, so this compositor callback only ends the
                    // settle, and its curve is no longer on screen.
                    if (g_state) {
                        g_state.StopAnimation(L"Progress");
                    }
                    g_settleFrames = 0;
                    g_dismissSettling.store(false, std::memory_order_relaxed);
                } catch (...) {
                    g_dismissSettling.store(false, std::memory_order_relaxed);
                }
            });
        handlerRegistered = batchToken.value != 0;

        // End the batch outside the lock: its completion handler takes the
        // same mutex.
        batch.End();

        bool keepHandler = false;
        {
            std::lock_guard lock(g_compositionMutex);
            if (handlerRegistered &&
                serial == g_dismissSerial.load(std::memory_order_relaxed)) {
                g_dismissBatch = batch;
                g_dismissBatchToken = batchToken;
                keepHandler = true;
            }
        }
        if (!keepHandler && handlerRegistered) {
            // A registration that lost its animation is revoked, not dropped:
            // the token is the only handle on a handler that could still run.
            try {
                batch.Completed(batchToken);
            } catch (...) {
            }
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Dismiss animation failed: %08X %s", e.code(),
               e.message().c_str());
        if (batch && batchToken.value) {
            try {
                batch.Completed(batchToken);
            } catch (...) {
            }
        }
        std::lock_guard lock(g_compositionMutex);
        InvalidateDismissBatchLocked();
        return false;
    } catch (...) {
        Wh_Log(L"Dismiss animation failed with an unknown exception");
        if (batch && batchToken.value) {
            try {
                batch.Completed(batchToken);
            } catch (...) {
            }
        }
        std::lock_guard lock(g_compositionMutex);
        InvalidateDismissBatchLocked();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Cross-process channel
//
// Kept in the shipping build. Both directions run through the one section in
// swipe_ipc_shared.h and its events, so no file is involved and nothing polls:
//   Local\SwipeFor10Start_v2_Cmd         p=, t=, e=, anim=   (tools -> here)
//   Local\SwipeFor10Start_v2_RetractReq  "how long is the retract?"  (bridge)
//   Local\SwipeFor10Start_v2_RetractPlan that many ms, or "none"     (here)
//   ..._State.closeSeq                   "this Hide is my own tap"   (here)
//   ..._State.openSeq                    "this is a new menu"        (here)
//
// Every record is a seqlock: this side bumps the counter to odd, writes, and
// bumps it to even, so the bridge can never accept a half-written close record,
// plan or open generation, and this side never accepts half a command or a
// half-written request. The command record is additionally serialised by a named
// mutex, because more than one tool can publish into it.
//
// The section outlives either process, so its sequence numbers start from
// whatever the other side left. This mod records them at attach and acts only
// on what is newer, which is what the one-second freshness check on the old
// command file did: a command or request that predates this process belongs to
// a menu this process never drew. A request is additionally bound to the menu
// open it was written for, and to the close it serves: see ApplyRetractRequest.
// ---------------------------------------------------------------------------

// The highest sequence this process has acted on. Set from the section at
// attach, advanced by the input thread only.
LONG64 g_ipcCommandSeq = 0;
LONG64 g_ipcRetractSeq = 0;

void OpenIpcChannel() {
    bool created = false;
    DWORD error = 0;
    if (!SwipeIpcOpen(&g_ipc, &created, &error)) {
        FileLog(L"ipc open failed err=%u", static_cast<unsigned>(error));
        return;
    }

    // Everything published before this attach belongs to an earlier menu. The
    // raw counters are enough for a seed: the first record this process accepts
    // must be newer than anything the previous one could have written.
    g_ipcCommandSeq = SwipeIpcLoad64(&g_ipc.state->cmdSeq);
    g_ipcRetractSeq = SwipeIpcLoad64(&g_ipc.state->retractRequestSeq);
    FileLog(L"ipc open created=%d cmd=%lld retract=%lld", created ? 1 : 0,
            static_cast<long long>(g_ipcCommandSeq),
            static_cast<long long>(g_ipcRetractSeq));
}

// Deletes the files this channel used before it moved to shared memory. They
// carry no state worth keeping, and a leftover retract-plan.txt invites a
// reader that no longer exists.
void RemoveLegacyChannelFiles() {
    WCHAR storage[MAX_PATH] = {};
    if (!Wh_GetModStoragePath(storage, ARRAYSIZE(storage))) {
        return;
    }

    const wchar_t* names[] = {L"debug.txt", L"close-mode.txt",
                              L"retract-plan.txt"};
    int removed = 0;
    for (const wchar_t* name : names) {
        WCHAR path[MAX_PATH] = {};
        if (swprintf_s(path, ARRAYSIZE(path), L"%s\\%s", storage, name) > 0 &&
            DeleteFileW(path)) {
            ++removed;
        }
    }
    if (removed > 0) {
        FileLog(L"ipc legacy files removed=%d", removed);
    }
}

// Tells the explorer-side bridge how long the settle it asked for lasts, or that
// no settle started. Called on the input thread right after the animation
// started: the bridge is holding the shell's Hide until this answer arrives, so
// the answer comes before the diagnostic line. The answer echoes the request's
// sequence number, so an answer to an earlier close cannot satisfy a later one.
// A failed answer only costs the bridge's own fallback lead, never the
// animation.
bool AnswerRetract(LONG64 requestSequence, uint64_t settleMs) {
    return SwipeIpcAnswerRetract(&g_ipc, requestSequence,
                                 static_cast<int32_t>(settleMs));
}

// Parses "v[,v...]" up to maxCount values. A missing, partial or trailing
// token rejects the whole line.
bool ParseFloatList(const char* text, int maxCount, float* values,
                    int& count) {
    count = 0;
    const char* cursor = text;

    while (count < maxCount) {
        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }

        char* end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(value)) {
            return false;
        }
        values[count++] = value;
        cursor = end;

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == ',') {
            ++cursor;
            continue;
        }
        break;
    }

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') {
        ++cursor;
    }
    return *cursor == '\0';
}

// The alternative to the expression: write the visual Offset directly. Both
// the property set entry and the visual property are written, because the
// probe must cover both forms the visual may read.
bool SetVisualOffset(float x, float y) {
    std::lock_guard lock(g_compositionMutex);
    if (!g_visual) {
        return false;
    }

    try {
        g_visual.Properties().InsertVector3(L"Offset", float3{x, y, 0.0f});
        g_visual.Offset(float3{x, y, 0.0f});
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Visual offset failed: %08X %s",
               static_cast<unsigned>(e.code()), e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Visual offset failed with an unknown exception");
        return false;
    }

    return true;
}

// The other alternative: the XAML framework translation property, which must be
// set on the UI thread. The call is asynchronous, so the applied line is logged
// from the callback and never claims a change the dispatcher refused.
void RequestTranslation(float x, float y, float z) {
    wucore::CoreDispatcher dispatcher = CopyDispatcher();
    if (!dispatcher || g_unloading.load(std::memory_order_relaxed)) {
        return;
    }

    try {
        dispatcher.TryRunAsync(
            wucore::CoreDispatcherPriority::Normal,
            [x, y, z]() {
                try {
                    wux::FrameworkElement root = g_root;
                    if (!root) {
                        return;
                    }
                    wuxh::ElementCompositionPreview::SetIsTranslationEnabled(
                        root, true);
                    root.Translation(float3{x, y, z});
                    FileLog(L"debug t=%.1f,%.1f,%.1f", x, y, z);
                } catch (winrt::hresult_error const& e) {
                    FileLog(L"debug t fail err=%08X",
                            static_cast<unsigned>(e.code()));
                } catch (...) {
                    FileLog(L"debug t fail err=unknown");
                }
            });
    } catch (winrt::hresult_error const& e) {
        FileLog(L"debug t queue err=%08X", static_cast<unsigned>(e.code()));
    } catch (...) {
        FileLog(L"debug t queue err=unknown");
    }
}

void ApplyDebugLine(const char* line) {
    while (*line == ' ' || *line == '\t' || *line == '\r') {
        ++line;
    }
    if (line[0] == '\0') {
        return;
    }

    // anim=0 drops the shell's own open storyboard, anim=1 restores it. The
    // switch is its own command word, so it is matched before the one-letter
    // commands and their `letter=value` shape.
    if (std::strncmp(line, "anim=", 5) == 0) {
        const char* value = line + 5;
        while (*value == ' ' || *value == '\t') {
            ++value;
        }
        if (*value != '0' && *value != '1') {
            return;
        }
        const int anim = *value - '0';
        g_suppressOpenAnim.store(anim == 0, std::memory_order_relaxed);
        FileLog(L"debug anim=%d", anim);
        return;
    }

    if (line[1] != '=') {
        return;
    }

    float values[3] = {};
    int count = 0;

    switch (line[0]) {
        case 'p':
            if (!ParseFloatList(line + 2, 1, values, count) || count != 1) {
                return;
            }
            {
                const float progress = std::clamp(values[0], 0.0f, 1.0f);
                if (InsertProgress(progress)) {
                    FileLog(L"debug p=%.3f", progress);
                }
            }
            return;
        case 't':
            if (!ParseFloatList(line + 2, 3, values, count) || count != 3) {
                return;
            }
            RequestTranslation(values[0], values[1], values[2]);
            return;
        case 'e':
            if (!ParseFloatList(line + 2, 2, values, count) || count < 1) {
                return;
            }
            {
                const float y = count > 1 ? values[1] : 0.0f;
                if (SetVisualOffset(values[0], y)) {
                    FileLog(L"debug e=%.1f,%.1f", values[0], y);
                }
            }
            return;
        default:
            return;
    }
}

void ApplyDebugCommands(char* text) {
    char* cursor = text;
    while (*cursor != '\0') {
        char* newline = std::strchr(cursor, '\n');
        if (newline) {
            *newline = '\0';
        }
        ApplyDebugLine(cursor);
        if (!newline) {
            return;
        }
        cursor = newline + 1;
    }
}

// Applies the newest debug command, if one arrived. The command event is
// manual-reset, so a command published while this runs is not lost: the next
// pass takes it. Nothing runs before the renderer exists, because before the
// first Setup neither mechanism has an object to act on; a command that arrives
// then stays in the section and is replaced by the next one.
void ApplyPendingCommand() {
    ResetEvent(g_ipc.commandEvent);
    if (g_unloading.load(std::memory_order_relaxed) ||
        !RendererReady()) {
        return;
    }

    char text[kSwipeIpcCommandMaxBytes] = {};
    if (!SwipeIpcTakeCommand(&g_ipc, text, sizeof(text), &g_ipcCommandSeq)) {
        return;
    }
    ApplyDebugCommands(text);
}

// The bridge asks how long the retract it just triggered lasts, and holds
// CStartExperienceManager::Hide until the answer arrives. The close is already
// decided, so the panel slides out on the same settle a committed dismiss uses.
// Only this process can plan it: the travel belongs to the panel, not the shell.
//
// The request is refused, with the no-settle answer, when it belongs to a menu
// that is gone: a generation that is not the current open generation, a deadline
// that has passed, a request the bridge already marked done, or one this process
// handled before. A late request must never start a settle under the menu the
// user opened since; the bridge then keeps its own fixed lead.
void ApplyRetractRequest() {
    SwipeIpcRetractRequest request{};
    if (!SwipeIpcReadRetractRequest(&g_ipc, &request)) {
        return;  // no stable record: the bridge falls back to its fixed lead
    }
    if (request.seq == 0 || request.seq == g_ipcRetractSeq) {
        return;
    }
    g_ipcRetractSeq = request.seq;

    const uint64_t now = GetTickCount64();
    uint64_t openGeneration = 0;
    uint64_t openTick = 0;
    const bool openKnown =
        SwipeIpcReadOpen(&g_ipc, &openGeneration, &openTick);
    const bool cancelled = request.seq <= SwipeIpcReadRetractCancel(&g_ipc);
    // Generation 0 is not a menu. SwipeIpcReadOpen already reports
    // ERROR_NOT_READY instead of a snapshot for an unpublished counter, and a
    // request that names 0 is refused exactly like one from a closed menu: a
    // keyboard close must never start a settle for a menu that never published
    // an open.
    if (!openKnown || request.open == 0 ||
        request.open != openGeneration || cancelled ||
        (now > request.deadline)) {
        // A request that sat unread, or one whose close is over, belongs to a
        // menu this process no longer has on screen. Answer, so a bridge still
        // waiting stops waiting, but start nothing.
        FileLog(L"debug retract ignored (stale)");
        AnswerRetract(request.seq, kSwipeRetractPlanNone);
        return;
    }
    if (g_unloading.load(std::memory_order_relaxed) ||
        !RendererReady()) {
        AnswerRetract(request.seq, kSwipeRetractPlanNone);
        return;
    }

    // This Hide is the user's Windows press, not this mod's tap: an armed tap
    // would toggle the menu back open while the shell is hiding it, so the
    // close being superseded drops its tap here.
    if (g_pendingTapTick != 0) {
        const CloseKind dropped = g_pendingTapKind;
        g_pendingTapTick = 0;
        g_pendingTapKind = CloseKind::None;
        FileLog(L"close tap dropped kind=%s", CloseKindName(dropped));
        if (!SwipeIpcPublishClose(&g_ipc, kSwipeCloseKindCancelled, 0, 0,
                                  g_pullSerial)) {
            FileLog(L"close mode cancel stamp failed");
        }
    }

    // The close owns Progress from here exactly as a gesture close does: the
    // contact that was driving it is dropped, the suppression window opens, and
    // a later report or release of that contact cannot settle the menu open
    // under the close. The finger is still down, so its reports are muted until
    // it lifts.
    TakeCloseOwnership(now, true);

    // Where the panel is now, not where the last animation was heading: a
    // keyboard close during the open settle would otherwise start this retract
    // from 1.0 and the panel would jump to fully open before it slid out. The
    // take stops the old animation and pins the value it answers.
    const float from = TakeProgressNow();
    // The keyboard close has no release speed of its own, so it settles from
    // rest: the plain step response of the selected spring.
    std::chrono::milliseconds duration{0};
    if (BeginGestureSettle(from, 0.0f, 0.0f, duration)) {
        // The window is about to be hidden: the next open restores Progress by
        // itself, so no restore is owed after this retract.
        g_commitNeedsProgressReset = false;
        // The bridge is waiting for this answer, so it goes out before the
        // diagnostic line.
        if (!AnswerRetract(request.seq,
                           static_cast<uint64_t>(duration.count()))) {
            FileLog(L"retract answer failed dur=%llu",
                    static_cast<unsigned long long>(duration.count()));
        }
        FileLog(L"debug retract from=%.3f dur=%llu", from,
                static_cast<unsigned long long>(duration.count()));
    } else {
        // No animation, so there is no duration to wait for: the bridge uses
        // its own lead.
        FileLog(L"debug retract failed");
        AnswerRetract(request.seq, kSwipeRetractPlanNone);
    }
}


// ---------------------------------------------------------------------------
// Raw HID input
//
// The legacy Start consumes the pointer stream before XAML sees it, so the
// only reliable live source is raw input. Each digitizer is decoded from its
// RIDI_PREPARSEDDATA blob; the device is never opened.
// ---------------------------------------------------------------------------

struct HidDevice {
    HANDLE handle = nullptr;
    bool ready = false;
    bool decodeFailLogged = false;
    uint64_t lastUsedTick = 0;
    WCHAR path[kHidPathChars] = {};
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    std::vector<HIDP_VALUE_CAPS> valueCaps;
    std::vector<HIDP_BUTTON_CAPS> buttonCaps;
    USHORT contactNodes[kMaxContactCollections] = {};
    int contactNodeCount = 0;
    USHORT contactCountLink = 0;
    bool hasContactCount = false;
    // Decoded once at init. `hasReportIds` says the first byte of every raw
    // report is the report ID (HID's layout); the two flags say whether the
    // device exposes the pen/like button usages at all, which is what the
    // `hid usages` line records.
    bool hasReportIds = false;
    bool hasTipSwitch = false;
    bool hasInRange = false;
    // The device exposes neither Tip Switch nor In Range, so no report can say
    // whether a slot is a real contact. Decided once at init, logged as
    // `coordpath` in `hid usages`, and the count/position path is then the only
    // answer for this device. Every other device is decoded by its buttons.
    bool coordinatePath = false;
};

HidDevice g_hidDevices[kMaxHidDevices];
int g_hidDeviceCount = 0;  // input thread only
bool g_hidCacheFullLogged = false;

void CopyDevicePath(HANDLE handle, WCHAR* out, size_t outCount) {
    out[0] = L'\0';

    UINT chars = 0;
    SetLastError(0);
    if (GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, nullptr, &chars) ==
            static_cast<UINT>(-1) ||
        chars == 0) {
        wcsncpy_s(out, outCount, L"?", _TRUNCATE);
        return;
    }

    WCHAR* name = new (std::nothrow) WCHAR[chars + 1];
    if (!name) {
        wcsncpy_s(out, outCount, L"?", _TRUNCATE);
        return;
    }

    name[0] = L'\0';
    UINT fetched = chars;
    SetLastError(0);
    if (GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, name, &fetched) ==
        static_cast<UINT>(-1)) {
        wcsncpy_s(out, outCount, L"?", _TRUNCATE);
    } else {
        name[chars] = L'\0';
        wcsncpy_s(out, outCount, name, _TRUNCATE);
    }

    delete[] name;
}

void LogDecodeFail(HidDevice& device, unsigned error) {
    if (device.decodeFailLogged) {
        return;
    }

    device.decodeFailLogged = true;
    FileLog(L"hid decodefail dev=%s err=%u", device.path, error);
}

// Reads the path, the preparsed blob, the contact collection indices and the
// value caps once per device. Runs on the input thread.
void InitHidDevice(HidDevice& device) {
    CopyDevicePath(device.handle, device.path, ARRAYSIZE(device.path));

    UINT bytes = 0;
    SetLastError(0);
    if (GetRawInputDeviceInfoW(device.handle, RIDI_PREPARSEDDATA, nullptr,
                               &bytes) == static_cast<UINT>(-1) ||
        bytes == 0 || bytes > 64 * 1024) {
        LogDecodeFail(device, GetLastError());
        return;
    }

    BYTE* blob = new (std::nothrow) BYTE[bytes];
    if (!blob) {
        return;
    }

    UINT fetched = bytes;
    SetLastError(0);
    if (GetRawInputDeviceInfoW(device.handle, RIDI_PREPARSEDDATA, blob,
                               &fetched) == static_cast<UINT>(-1) ||
        fetched == 0) {
        LogDecodeFail(device, GetLastError());
        delete[] blob;
        return;
    }

    device.preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(blob);

    HIDP_CAPS caps{};
    const NTSTATUS capsStatus = HidP_GetCaps(device.preparsed, &caps);
    if (capsStatus != HIDP_STATUS_SUCCESS) {
        device.decodeFailLogged = true;
        FileLog(L"hid decodefail dev=%s err=0x%08X", device.path,
                static_cast<unsigned>(capsStatus));
        return;
    }

    // The logical range of X and Y is needed to map raw values to pixels.
    if (caps.NumberInputValueCaps > 0) {
        device.valueCaps.resize(caps.NumberInputValueCaps);
        USHORT length = caps.NumberInputValueCaps;
        if (HidP_GetValueCaps(HidP_Input, device.valueCaps.data(), &length,
                              device.preparsed) == HIDP_STATUS_SUCCESS) {
            device.valueCaps.resize(length);
        } else {
            device.valueCaps.clear();
        }
    }

    // Button usages: Tip Switch and In Range are buttons, not values, so they
    // are read with HidP_GetUsages. The caps are also the only way to know
    // whether a report carries them at all.
    if (caps.NumberInputButtonCaps > 0) {
        device.buttonCaps.resize(caps.NumberInputButtonCaps);
        USHORT length = caps.NumberInputButtonCaps;
        if (HidP_GetButtonCaps(HidP_Input, device.buttonCaps.data(), &length,
                               device.preparsed) == HIDP_STATUS_SUCCESS) {
            device.buttonCaps.resize(length);
        } else {
            device.buttonCaps.clear();
        }
    }

    // HID's own report-ID rule: if any input cap declares a non-zero ReportID,
    // every report of the device starts with that ID byte and the payload
    // follows it. RAWINPUTHEADER.wParam is RIM_INPUT/RIM_INPUTSINK and carries
    // no report ID, so this is the only place the answer can come from.
    for (const HIDP_VALUE_CAPS& cap : device.valueCaps) {
        if (cap.ReportID != 0) {
            device.hasReportIds = true;
            break;
        }
    }
    if (!device.hasReportIds) {
        for (const HIDP_BUTTON_CAPS& cap : device.buttonCaps) {
            if (cap.ReportID != 0) {
                device.hasReportIds = true;
                break;
            }
        }
    }

    // Contact collections are 0x0D/0x05 on classic digitizers and 0x0D/0x22
    // (Finger) on the ELAN2513. Both carry X/Y per contact. A pen's data lives
    // in its own 0x0D/0x02 or 0x0D/0x20 link node, so it is collected too: on
    // a touch-only panel neither exists and nothing changes.
    HIDP_LINK_COLLECTION_NODE nodes[kMaxLinkNodes]{};
    ULONG nodeCount = kMaxLinkNodes;
    if (HidP_GetLinkCollectionNodes(nodes, &nodeCount, device.preparsed) ==
        HIDP_STATUS_SUCCESS) {
        if (nodeCount > kMaxLinkNodes) {
            nodeCount = kMaxLinkNodes;
        }

        // Two passes, so a contact/finger node always precedes a pen node: the
        // first collection with a position wins while no session is active, and
        // a touch must never lose that tie to a pen hovering nearby.
        for (int pass = 0; pass < 2; ++pass) {
            for (ULONG i = 0; i < nodeCount; ++i) {
                if (nodes[i].LinkUsagePage != kUsagePageDigitizer) {
                    continue;
                }
                const USAGE usage = nodes[i].LinkUsage;
                const bool isContact =
                    usage == kUsageContact || usage == kUsageFinger;
                const bool isPen = usage == kUsagePen || usage == kUsageStylus;
                if ((pass == 0) != isContact || (!isContact && !isPen)) {
                    continue;
                }
                if (device.contactNodeCount < kMaxContactCollections) {
                    device.contactNodes[device.contactNodeCount++] =
                        static_cast<USHORT>(i);
                }
            }
        }
    }

    // Which of the two button usages this device actually exposes. The flags
    // name the usage, not the report: the per-report test in
    // ReadContactButtons also checks the report ID and the link collection.
    for (const HIDP_BUTTON_CAPS& cap : device.buttonCaps) {
        if (cap.UsagePage != kUsagePageDigitizer) {
            continue;
        }
        const bool matchesTip =
            cap.IsRange ? (cap.Range.UsageMin <= kUsageTipSwitch &&
                           kUsageTipSwitch <= cap.Range.UsageMax)
                        : (cap.NotRange.Usage == kUsageTipSwitch);
        const bool matchesRange =
            cap.IsRange ? (cap.Range.UsageMin <= kUsageInRange &&
                           kUsageInRange <= cap.Range.UsageMax)
                        : (cap.NotRange.Usage == kUsageInRange);
        device.hasTipSwitch = device.hasTipSwitch || matchesTip;
        device.hasInRange = device.hasInRange || matchesRange;
    }

    // A device that exposes neither usage can never say whether a slot is a
    // real contact. That decision is made once here, not guessed per report,
    // and the `hid usages` line records it: the count/position path is the only
    // answer such a device has.
    device.coordinatePath = !device.hasTipSwitch && !device.hasInRange;

    // The Contact Count that reports how many contacts are down lives in the
    // top-level digitizer collection, not in collection 0. Reading it with the
    // wrong collection makes a lift report look like "still one contact",
    // which forces the idle timer to guess the release.
    for (const HIDP_VALUE_CAPS& cap : device.valueCaps) {
        if (cap.UsagePage != kUsagePageDigitizer) {
            continue;
        }
        const bool matches =
            cap.IsRange ? (cap.Range.UsageMin <= kUsageContactCount &&
                           kUsageContactCount <= cap.Range.UsageMax)
                        : (cap.NotRange.Usage == kUsageContactCount);
        if (!matches) {
            continue;
        }
        device.contactCountLink = cap.LinkCollection;
        device.hasContactCount = true;
        break;
    }
    FileLog(L"hidcc dev=%s have=%d link=%u", device.path,
            device.hasContactCount ? 1 : 0,
            static_cast<unsigned>(device.contactCountLink));

    // One line per device so a future pen test can tell a hover that was
    // ignored from a device that never reported the usage at all. `coordpath`
    // is the logged fallback decision: 1 means this device carries no button
    // usage the mod can read, so its count/position path is used.
    FileLog(L"hid usages dev=%s tip=%d inrange=%d count=%d buttons=%u "
            L"reportids=%d coordpath=%d",
            device.path, device.hasTipSwitch ? 1 : 0,
            device.hasInRange ? 1 : 0, device.hasContactCount ? 1 : 0,
            static_cast<unsigned>(device.buttonCaps.size()),
            device.hasReportIds ? 1 : 0, device.coordinatePath ? 1 : 0);

    device.ready = true;
}

HidDevice* GetHidDevice(HANDLE handle) {
    if (!handle) {
        return nullptr;
    }

    const uint64_t now = GetTickCount64();
    for (int i = 0; i < g_hidDeviceCount; ++i) {
        if (g_hidDevices[i].handle == handle) {
            g_hidDevices[i].lastUsedTick = now;
            return &g_hidDevices[i];
        }
    }

    HidDevice* device = nullptr;
    if (g_hidDeviceCount < kMaxHidDevices) {
        device = &g_hidDevices[g_hidDeviceCount++];
    } else {
        // Injectors create a fresh virtual digitizer handle for every process,
        // so the table must recycle instead of going deaf after four devices.
        device = &g_hidDevices[0];
        for (int i = 1; i < g_hidDeviceCount; ++i) {
            if (g_hidDevices[i].lastUsedTick < device->lastUsedTick) {
                device = &g_hidDevices[i];
            }
        }
        delete[] reinterpret_cast<BYTE*>(device->preparsed);
        if (!g_hidCacheFullLogged) {
            g_hidCacheFullLogged = true;
            FileLog(L"hid device table recycled (was full)");
        }
    }

    *device = HidDevice{};
    device->handle = handle;
    device->lastUsedTick = now;
    InitHidDevice(*device);
    return device;
}

void FreeHidDevices() {
    for (int i = 0; i < g_hidDeviceCount; ++i) {
        delete[] reinterpret_cast<BYTE*>(g_hidDevices[i].preparsed);
        g_hidDevices[i] = HidDevice{};
    }
    g_hidDeviceCount = 0;
}

// The answer of one HidP read. `Absent` means the report does not carry the
// field at all (another report ID, another collection), which is not a failure;
// `Error` means the field belongs to this report and could not be decoded. The
// two must stay apart: only `Absent` may be read as "this device has no such
// usage", and `Error` must never become "no contact".
enum class HidField : uint8_t { Ok, Absent, Error };

HidField ClassifyHidStatus(NTSTATUS status) {
    if (status == HIDP_STATUS_SUCCESS) {
        return HidField::Ok;
    }
    // HID's own answers for "this report or collection does not carry it".
    if (status == HIDP_STATUS_USAGE_NOT_FOUND ||
        status == HIDP_STATUS_INCOMPATIBLE_REPORT_ID ||
        status == HIDP_STATUS_REPORT_DOES_NOT_EXIST) {
        return HidField::Absent;
    }
    return HidField::Error;
}

HidField ReadUsageValue(const HidDevice& device, USAGE usagePage,
                        USHORT collection, USAGE usage, const BYTE* report,
                        ULONG reportLength, ULONG* value,
                        NTSTATUS* statusOut = nullptr) {
    if (!device.preparsed) {
        if (statusOut) {
            *statusOut = HIDP_STATUS_INVALID_PREPARSED_DATA;
        }
        return HidField::Error;
    }

    ULONG parsed = 0;
    const NTSTATUS status = HidP_GetUsageValue(
        HidP_Input, usagePage, collection, usage, &parsed, device.preparsed,
        reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)), reportLength);
    if (statusOut) {
        *statusOut = status;
    }
    if (status != HIDP_STATUS_SUCCESS) {
        return ClassifyHidStatus(status);
    }

    *value = parsed;
    return HidField::Ok;
}

struct HidAxisRange {
    long min = 0;
    long max = 0;
    bool valid = false;
};

// Selects the value cap that describes this report. reportId is the report's
// own first byte when the device declares report IDs (HID's layout; see
// HidDevice::hasReportIds), 0 for a device without them.
HidAxisRange FindAxisRange(const HidDevice& device, USAGE usage,
                           USHORT reportId) {
    HidAxisRange range;

    for (const HIDP_VALUE_CAPS& cap : device.valueCaps) {
        if (cap.UsagePage != kUsagePageGeneric) {
            continue;
        }

        const bool usageMatches =
            cap.IsRange
                ? (usage >= cap.Range.UsageMin && usage <= cap.Range.UsageMax)
                : (cap.NotRange.Usage == usage);
        if (!usageMatches) {
            continue;
        }
        if (device.hasReportIds ? cap.ReportID != reportId
                                : cap.ReportID != 0) {
            continue;
        }
        if (cap.LogicalMax <= cap.LogicalMin) {
            continue;
        }

        range.min = cap.LogicalMin;
        range.max = cap.LogicalMax;
        range.valid = true;
        break;
    }

    return range;
}

// Reads the button usages of one contact collection in one report. Tip Switch
// and In Range are buttons, so HidP_GetUsageValue never finds them; the value
// caps above only ever describe axes. `field` keeps three answers apart: the
// report carries the collection's digitizer buttons, it does not, or the decode
// failed. The last two used to be the same empty answer, so a decode failure
// was indistinguishable from a device that declares no buttons at all.
struct HidContactButtons {
    HidField field = HidField::Absent;
    NTSTATUS status = HIDP_STATUS_SUCCESS;
    bool tipDeclared = false;
    bool tipDown = false;
    bool inRangeDeclared = false;
    bool inRangeDown = false;
};

HidContactButtons ReadContactButtons(const HidDevice& device, USHORT collection,
                                     USHORT reportId, const BYTE* report,
                                     ULONG reportLength) {
    HidContactButtons state;
    if (!device.preparsed) {
        state.field = HidField::Error;
        state.status = HIDP_STATUS_INVALID_PREPARSED_DATA;
        return state;
    }
    if (device.buttonCaps.empty()) {
        return state;  // the device declares no button usages at all
    }

    USAGE usages[128] = {};
    ULONG count = ARRAYSIZE(usages);
    const NTSTATUS status = HidP_GetUsages(
        HidP_Input, kUsagePageDigitizer, collection, usages, &count,
        device.preparsed, reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)),
        reportLength);
    state.status = status;
    state.field = ClassifyHidStatus(status);
    if (state.field != HidField::Ok) {
        // Absent: this report or collection carries none of the device's
        // digitizer buttons. Error: it does and the decode failed.
        return state;
    }

    for (const HIDP_BUTTON_CAPS& cap : device.buttonCaps) {
        if (cap.UsagePage != kUsagePageDigitizer ||
            cap.LinkCollection != collection ||
            (device.hasReportIds ? cap.ReportID != reportId
                                 : cap.ReportID != 0)) {
            continue;
        }

        const auto pressed = [&usages, count](USAGE usage) {
            for (ULONG i = 0; i < count; ++i) {
                if (usages[i] == usage) {
                    return true;
                }
            }
            return false;
        };
        const bool matchesTip =
            cap.IsRange ? (cap.Range.UsageMin <= kUsageTipSwitch &&
                           kUsageTipSwitch <= cap.Range.UsageMax)
                        : (cap.NotRange.Usage == kUsageTipSwitch);
        const bool matchesRange =
            cap.IsRange ? (cap.Range.UsageMin <= kUsageInRange &&
                           kUsageInRange <= cap.Range.UsageMax)
                        : (cap.NotRange.Usage == kUsageInRange);
        if (matchesTip) {
            state.tipDeclared = true;
            state.tipDown = state.tipDown || pressed(kUsageTipSwitch);
        }
        if (matchesRange) {
            state.inRangeDeclared = true;
            state.inRangeDown = state.inRangeDown || pressed(kUsageInRange);
        }
    }

    return state;
}

// One contact slot decoded from one report. `declared` says the report carries
// a contact field for this slot (a coordinate or a digitizer button);
// `hasPosition` says both coordinates are present and not both zero.
struct HidContactSlot {
    bool declared = false;
    bool hasPosition = false;
    long rawX = 0;
    long rawY = 0;
    HidContactButtons buttons{};
};

// Decodes one slot's coordinates and buttons. False means the report carries a
// field for this slot that could not be decoded; `error` is HID's status.
bool DecodeContactSlot(const HidDevice& device, USHORT collection,
                       USHORT reportId, const BYTE* report, ULONG reportLength,
                       HidContactSlot& slot, NTSTATUS& error) {
    ULONG x = 0;
    ULONG y = 0;
    NTSTATUS status = HIDP_STATUS_SUCCESS;
    const HidField xField = ReadUsageValue(device, kUsagePageGeneric, collection,
                                           kUsageX, report, reportLength, &x,
                                           &status);
    if (xField == HidField::Error) {
        error = status;
        return false;
    }
    const HidField yField = ReadUsageValue(device, kUsagePageGeneric, collection,
                                           kUsageY, report, reportLength, &y,
                                           &status);
    if (yField == HidField::Error) {
        error = status;
        return false;
    }

    // A report that leaves both coordinates at zero is a lift report on a
    // device that keeps the last position in the fields, not a contact at the
    // raw origin; the old decode made the same test.
    slot.hasPosition =
        xField == HidField::Ok && yField == HidField::Ok && !(x == 0 && y == 0);
    slot.rawX = static_cast<long>(x);
    slot.rawY = static_cast<long>(y);

    slot.buttons =
        ReadContactButtons(device, collection, reportId, report, reportLength);
    if (slot.buttons.field == HidField::Error) {
        error = slot.buttons.status;
        return false;
    }

    slot.declared = xField == HidField::Ok || yField == HidField::Ok ||
                    slot.buttons.field == HidField::Ok;
    return true;
}

// What this report says about a slot. `Unknown` means it carries no tip and no
// in-range field for the slot, so it cannot say whether a contact is real; it is
// never read as a lift.
enum class HidContactState : uint8_t { Contact, NotContact, Unknown };

HidContactState SlotContactState(const HidContactSlot& slot) {
    if (slot.buttons.tipDeclared) {
        return slot.buttons.tipDown ? HidContactState::Contact
                                    : HidContactState::NotContact;
    }
    if (slot.buttons.inRangeDeclared) {
        return slot.buttons.inRangeDown ? HidContactState::Contact
                                        : HidContactState::NotContact;
    }
    return HidContactState::Unknown;
}

int32_t ToScreenPixel(long raw, const HidAxisRange& range, int screenSize) {
    if (!range.valid || screenSize <= 1) {
        return static_cast<int32_t>(raw);
    }

    const int64_t span = static_cast<int64_t>(range.max) - range.min;
    if (span <= 0) {
        return static_cast<int32_t>(raw);
    }

    const int64_t value = static_cast<int64_t>(raw) - range.min;
    int64_t pixel = value * (screenSize - 1) / span;
    if (pixel < 0) {
        pixel = 0;
    }
    if (pixel > screenSize - 1) {
        pixel = screenSize - 1;
    }
    return static_cast<int32_t>(pixel);
}

// The HID thread never sees the XAML thread's CoreWindow pointer. The UI
// thread stores it in Setup; if that did not happen, look it up once.
HWND FindStartCoreWindow() {
    DWORD processId = GetCurrentProcessId();
    HWND hwnd = FindWindowW(L"Windows.UI.Core.CoreWindow", nullptr);
    while (hwnd) {
        DWORD owner = 0;
        GetWindowThreadProcessId(hwnd, &owner);
        if (owner == processId) {
            return hwnd;
        }
        hwnd = FindWindowExW(nullptr, hwnd, L"Windows.UI.Core.CoreWindow",
                             nullptr);
    }
    return nullptr;
}

// Cached answer of StartWindowIsOpen() for the report handler. The cloaked
// query is a syscall, so one answer serves every report inside 20 ms; the
// cache is dropped on each lift so a down always starts from a fresh query.
bool g_openCheckValid = false;     // input thread only
bool g_openCheckOpen = false;      // input thread only
uint64_t g_lastOpenCheckTick = 0;  // input thread only

// The menu closes by cloaking its CoreWindow, not by destroying or hiding it.
// Called once per down; nothing is cached here, because the menu opens and
// closes many times on the same HWND.
bool StartWindowIsOpen() {
    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        return false;
    }

    DwmGetWindowAttribute_t dwmGetWindowAttribute =
        g_dwmGetWindowAttribute.load(std::memory_order_relaxed);
    if (!dwmGetWindowAttribute) {
        return false;
    }

    DWORD cloaked = 0;
    const HRESULT hr =
        dwmGetWindowAttribute(hwnd, kDwmwaCloaked, &cloaked, sizeof(cloaked));
    return hr == S_OK && cloaked == 0;
}

// The candidate contact needs the open state on every report, but the cloaked
// query is a syscall. One answer serves all reports inside 20 ms; the cache is
// invalidated when a contact lifts, so a down always starts from a fresh one.
bool StartWindowIsOpenThrottled(uint64_t tickMs) {
    if (g_openCheckValid &&
        tickMs - g_lastOpenCheckTick < kOpenCheckIntervalMs) {
        return g_openCheckOpen;
    }

    g_lastOpenCheckTick = tickMs;
    g_openCheckValid = true;
    g_openCheckOpen = StartWindowIsOpen();
    return g_openCheckOpen;
}

float HidDpiScale() {
    static HWND cachedHwnd = nullptr;
    static float cachedScale = 1.0f;

    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        hwnd = FindStartCoreWindow();
        if (hwnd) {
            g_coreHwnd.store(hwnd, std::memory_order_relaxed);
        }
    }
    if (!hwnd) {
        return 1.0f;
    }

    if (hwnd != cachedHwnd) {
        const UINT dpi = GetDpiForWindow(hwnd);
        if (dpi == 0) {
            // The window may still be initializing; retry on the next report
            // instead of caching a wrong scale for the whole session.
            return 1.0f;
        }
        cachedHwnd = hwnd;
        cachedScale = static_cast<float>(dpi) / 96.0f;
    }

    return cachedScale;
}

struct HidGestureSession {
    bool active = false;
    bool suppress = false;  // ignore samples until all contacts are released
    long lastRawX = 0;
    long lastRawY = 0;
    float startX = 0.0f;
    float startY = 0.0f;
    // Progress where the drag began. A drag that takes over a running settle
    // continues from the position the panel is at, so the mapping is relative to
    // that base instead of always to the resting value.
    float startProgress = 1.0f;
    uint64_t startTick = 0;
    uint64_t lastTick = 0;
    // Last sample on the QPC clock. The idle release compares against this
    // instead of lastTick, because GetTickCount64 only advances every 15.6 ms.
    uint64_t lastSampleQpc = 0;
    float curDelta = 0.0f;
    float maxDelta = 0.0f;
    float maxPerp = 0.0f;
    float lastProgress = 1.0f;
    int samples = 0;
};

HidGestureSession g_hidSession;  // input thread only

// Reveal follow. The contact that starts on the taskbar edge is remembered
// while the menu is closed. EP opens the menu by itself once the contact
// passes its own edge threshold, and the same contact is still down at that
// moment, so it takes Progress over and the menu follows it from there.
//
// `distance` is the root extent along the axis: Progress 1 leaves the root at
// rest, Progress 0 slides it out by Distance. `panelTrail` is the panel edge in
// absolute screen DIP that the finger meets: the reveal is
// Progress = 1 - (finger - panelTrail) / (dir * distance), so the panel's
// trailing edge sits under the finger at every sample and Progress no longer
// depends on when the menu was seen opening. That mapping is visual only.
// `travel` is the decision quantity: the distance the finger moved away from
// the taskbar edge since the touch began, clamped at 0, so pulling out and back
// reads 0 at release. `maxTravel` is the furthest it got; the log shows it.
// `progress` is the last computed value, `lastInserted` the last one that
// reached the property set, so the 0.5 DIP change filter compares against what
// the renderer actually shows.
struct HidRevealSession {
    bool edgeCandidate = false;
    bool active = false;
    float edgeStartX = 0.0f;  // first report, kept for the edge-band test
    float edgeStartY = 0.0f;
    float distance = 0.0f;
    float panelTrail = 0.0f;
    float travel = 0.0f;
    float maxTravel = 0.0f;
    float progress = 0.0f;
    float lastInserted = 1.0f;
    int samples = 0;
    uint64_t startTick = 0;
    uint64_t lastTick = 0;
    uint64_t lastSampleQpc = 0;  // idle release clock, see HidGestureSession
    // QPC time of the report that took Progress over. The trail mapping is still
    // settling for the first 50 ms of the follow, so the coast stays off there.
    uint64_t activeSinceQpc = 0;
};

HidRevealSession g_reveal;  // input thread only

// True from the first report of a contact until it lifts. The edge test runs
// once per contact, never on a finger that slides into the band later.
bool g_contactDown = false;      // input thread only
uint64_t g_contactTick = 0;      // input thread only, last report of a contact

// Which decoded contact the mod follows: the raw-input device whose report
// carried it and the slot inside that device's contact collections. Contact
// identity has to be carried, not re-guessed per report: slot 2 can still hold
// the previous finger's coordinates while slot 0 is the live one, and a lift
// from the stale slot must not end the tracked finger.
struct HidContactTrack {
    HANDLE device = nullptr;
    int slot = -1;
};

HidContactTrack g_trackedContact;  // input thread only

void ForgetTrackedContact() {
    g_trackedContact = HidContactTrack{};
}

// Screen size in DIP, rewritten by FeedHidReport before every state-machine
// step. The right and bottom edge bands are measured from it.
float g_screenDipWidth = 0.0f;   // input thread only
float g_screenDipHeight = 0.0f;  // input thread only

// Set when a commit hands Progress to the settle animation. The reuse of one
// XAML root means the next drag can still see a translated root or a base
// value of 0, so BeginHidSession restores the resting value first.
bool g_commitNeedsProgressReset = false;  // input thread only
bool g_rootWasOpen = false;              // input thread only
uint64_t g_edgeLogTick = 0;              // input thread only, diagnostic
// The edge line was a measurement instrument: it is off in the shipping build.
constexpr bool g_debugEdge = false;

// ---------------------------------------------------------------------------
// A close owns Progress
// ---------------------------------------------------------------------------
//
// A gesture close and a keyboard close must leave the mod in one state, or the
// two families diverge where it matters: the keyboard close arrives while the
// finger is still on the glass and a session is still driving `Progress`. Without
// this the next report of that contact dragged the panel back and the release
// settled the menu open over a close that was already running.
//
// The ownership state is:
//
// - the suppression window is open, so the reopen poll does not restore the
//   resting value and no drag starts until the shell has hidden the window;
// - no gesture session owns Progress. `contactStillDown` mutes the rest of that
//   contact (its reports would otherwise start a new session, and its release
//   would settle an open); a gesture close arrives from its own release, where
//   the contact is already ending, so nothing is muted there. The mute is read
//   from the contact latch, so it covers the finger whether the dismiss session
//   or the reveal owned it;
// - the 1 ms timer period is released: no session is left that needs it.
//
// One helper, called by both families. It changes no tap and no close record: the
// keyboard close drops the armed tap at its caller, the gesture close arms one.
void TakeCloseOwnership(uint64_t tickMs, bool contactStillDown) {
    g_suppressRestoreUntil.store(tickMs + kRestoreSuppressMs,
                                 std::memory_order_relaxed);

    // The mute belongs to the contact, not to the session that happened to own
    // it. Read the latch and both owners before either is cleared: a keyboard
    // close can arrive while the *reveal* owns the contact, and clearing only
    // the reveal flags left the finger live, so its next report could start a
    // dismiss session on the reopened menu and its release could settle an open.
    const bool contactLive =
        g_contactDown || g_hidSession.active || g_reveal.edgeCandidate;
    if (contactStillDown && contactLive) {
        g_hidSession.suppress = true;
    }

    g_hidSession.active = false;
    // A live reveal is ended whole: a keyboard close must not leave it owning
    // the panel, and its release must not settle an open. Only the two flags are
    // cleared; the measurements the release line prints stay readable.
    g_reveal.edgeCandidate = false;
    g_reveal.active = false;
    EndContactTiming();
    // Nothing owns the panel edge any more, so a settled measurement may move it
    // even though the finger is still on the glass.
    PublishPanelOwnership();
}

// The coast runs on the finger's own speed, smoothed over the last three
// reports. At the panel's ~170 Hz that is a 12 ms window: long enough to ignore
// one slow report, short enough to follow a real stop. Each sample is the mapped
// progress and the QPC time it arrived at. Progress moves with the panel in both
// gestures, so its sign needs no per-gesture correction.
struct CoastVelocitySample {
    float progress = 0.0f;
    uint64_t qpc = 0;
};

constexpr int kCoastVelocitySamples = 3;
CoastVelocitySample g_coastVelocity[kCoastVelocitySamples];  // input thread
int g_coastVelocityCount = 0;                                // input thread

void ResetCoastVelocity() {
    g_coastVelocityCount = 0;
}

// Keeps the last three reports and drops the oldest.
void SampleCoastVelocity(float progress, uint64_t qpcMs) {
    if (g_coastVelocityCount == kCoastVelocitySamples) {
        std::memmove(g_coastVelocity, g_coastVelocity + 1,
                     sizeof(CoastVelocitySample) *
                         (kCoastVelocitySamples - 1));
        g_coastVelocityCount--;
    }
    g_coastVelocity[g_coastVelocityCount++] =
        CoastVelocitySample{progress, qpcMs};
}

// Progress per millisecond over the whole window. The 6 DIP/ms cap lives in DIP
// space: on this 1440 DIP root it is a fast flick, and one injected jump between
// two reports must not throw the panel across the screen.
float CoastVelocity(float distance) {
    if (g_coastVelocityCount < 2 || !(distance > 0.0f)) {
        return 0.0f;
    }

    const CoastVelocitySample& oldest = g_coastVelocity[0];
    const CoastVelocitySample& newest =
        g_coastVelocity[g_coastVelocityCount - 1];
    if (newest.qpc <= oldest.qpc) {
        return 0.0f;
    }

    const float elapsedMs = static_cast<float>(newest.qpc - oldest.qpc);
    const float velocity = (newest.progress - oldest.progress) / elapsedMs;
    const float limit = kCoastMaxVelocityDip / distance;
    return std::clamp(velocity, -limit, limit);
}

// ---------------------------------------------------------------------------
// The release speed: the end of the gesture, not its average
// ---------------------------------------------------------------------------
//
// `travel / elapsed` over the whole session is not a release speed. A finger
// that dragged 300 DIP, rested for 1.2 s and then lifted reported 0.20 DIP/ms,
// and the settle left the seam at a speed the finger did not have; the settings
// page's promise that a held finger starts from rest was false. The release
// speed is therefore measured from the reports inside one short window at the
// end of the gesture:
//
// - at most 30 ms, so a stop is seen: a finger that held still reports the same
//   value for that whole window and the answer is 0;
// - at least 8 ms, so one report cannot decide it: the window is the oldest
//   report that is still inside 30 ms and at least 8 ms old;
// - elapsed between the first and the last report of that window.
//
// The sample value is Progress: the quantity the follow inserts and the settle
// animates, which is what lets one estimator serve both gesture families. The
// projection onto the gesture axis is already in that mapping (AxisDelta carries
// the dismiss axis sign, AxisOpeningSign the reveal direction) and so is the
// division by the same `Distance` the settle plans its travel with. A report that
// did not move the panel only moves the window's end, so a contact resting on the
// glass cannot accumulate speed from an old motion.
//
// The value is signed along the gesture axis: positive means Progress is rising.
// `SpeedTowardDip` projects it onto the settle's direction and clamps a release
// that is moving away from its target to 0.
constexpr uint64_t kEndVelocityWindowMs = 30;
constexpr uint64_t kEndVelocityMinMs = 8;
constexpr int kEndVelocitySamples = 8;

struct EndVelocitySample {
    float progress = 0.0f;
    uint64_t qpc = 0;
};

EndVelocitySample g_endVelocity[kEndVelocitySamples];
int g_endVelocityCount = 0;  // input thread only

void ResetEndVelocity() {
    g_endVelocityCount = 0;
}

// Keeps the last few reports of the moving panel. A report that repeats the last
// value did not move the panel: it only makes the window end here.
void SampleEndVelocity(float progress, uint64_t qpcMs) {
    if (g_endVelocityCount > 0) {
        EndVelocitySample& newest = g_endVelocity[g_endVelocityCount - 1];
        if (newest.progress == progress) {
            newest.qpc = qpcMs;
            return;
        }
    }

    if (g_endVelocityCount == kEndVelocitySamples) {
        std::memmove(g_endVelocity, g_endVelocity + 1,
                     sizeof(EndVelocitySample) * (kEndVelocitySamples - 1));
        g_endVelocityCount--;
    }
    g_endVelocity[g_endVelocityCount++] = EndVelocitySample{progress, qpcMs};
}

// Progress per millisecond at the release, signed along the gesture axis. 0 when
// the window holds no movement, which is what a held contact reports.
float EndVelocityProgressPerMs() {
    if (g_endVelocityCount < 2) {
        return 0.0f;
    }

    const EndVelocitySample& newest = g_endVelocity[g_endVelocityCount - 1];
    const EndVelocitySample* first = nullptr;
    // Oldest to newest, so the ages only shrink. The first entry inside the band
    // is the oldest one that is at least 8 ms old and at most 30 ms old, which
    // is the widest window the rules allow; every later entry is shorter. A
    // sample younger than 8 ms ends the scan because every later one is younger
    // still. No entry in the band (one report, equal timestamps, or a stream
    // coarser than the window) answers zero without a division.
    for (int i = 0; i < g_endVelocityCount - 1; ++i) {
        const uint64_t age =
            newest.qpc >= g_endVelocity[i].qpc ? newest.qpc - g_endVelocity[i].qpc
                                               : 0;
        if (age > kEndVelocityWindowMs) {
            continue;
        }
        if (age < kEndVelocityMinMs) {
            break;
        }
        first = &g_endVelocity[i];
        break;
    }

    if (!first || newest.qpc <= first->qpc) {
        return 0.0f;
    }

    const float elapsed = static_cast<float>(newest.qpc - first->qpc);
    if (!(elapsed > 0.0f)) {
        return 0.0f;
    }
    return (newest.progress - first->progress) / elapsed;
}

float AxisDelta(int32_t axis, const HidGestureSession& session, float x,
                float y) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
            return session.startX - x;
        case DismissAxis::Right:
            return x - session.startX;
        case DismissAxis::Top:
            return session.startY - y;
        case DismissAxis::Bottom:
            return y - session.startY;
    }
    return session.startX - x;
}

float PerpDelta(int32_t axis, const HidGestureSession& session, float x,
                float y) {
    if (IsHorizontalAxis(axis)) {
        return std::fabs(y - session.startY);
    }
    return std::fabs(x - session.startX);
}

bool PointInStartWindow(float x, float y) {
    const int64_t left = g_winRectDip[0].load(std::memory_order_relaxed);
    const int64_t top = g_winRectDip[1].load(std::memory_order_relaxed);
    const int64_t right = g_winRectDip[2].load(std::memory_order_relaxed);
    const int64_t bottom = g_winRectDip[3].load(std::memory_order_relaxed);

    // Without published bounds, do not reject a plausible touch.
    if (right <= left || bottom <= top) {
        return true;
    }

    return x >= static_cast<float>(left) - kWindowMarginDip &&
           x <= static_cast<float>(right) + kWindowMarginDip &&
           y >= static_cast<float>(top) - kWindowMarginDip &&
           y <= static_cast<float>(bottom) + kWindowMarginDip;
}

// The taskbar sits on the side the dismiss axis slides out along, so that axis
// also names the edge band the reveal starts from.
bool PointInEdgeBand(int32_t axis, float x, float y) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
            return x <= kEdgeBandDip;
        case DismissAxis::Top:
            return y <= kEdgeBandDip;
        case DismissAxis::Right:
            return g_screenDipWidth > kEdgeBandDip &&
                   x >= g_screenDipWidth - kEdgeBandDip;
        case DismissAxis::Bottom:
            return g_screenDipHeight > kEdgeBandDip &&
                   y >= g_screenDipHeight - kEdgeBandDip;
    }
    return false;
}

// Finger position on the gesture axis in absolute screen DIP. The reveal
// formula compares it to the panel trail directly, so the same drag yields the
// same progress on every open.
float AxisScreenPosition(int32_t axis, float x, float y) {
    return IsHorizontalAxis(axis) ? x : y;
}

// The direction the menu slides out along, as the DirX/DirY sign: -1 for a
// left or top taskbar, +1 for right or bottom.
float AxisDirection(int32_t axis) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
        case DismissAxis::Top:
            return -1.0f;
        case DismissAxis::Right:
        case DismissAxis::Bottom:
            return 1.0f;
    }
    return -1.0f;
}

// The direction a revealing finger moves away from the taskbar edge: the
// opposite of AxisDirection, so +1 for a left or top taskbar and -1 for right
// or bottom. The reveal's travel and velocity use it; the visual mapping does
// not, because that one already divides by AxisDirection.
float AxisOpeningSign(int32_t axis) {
    return -AxisDirection(axis);
}

// ---------------------------------------------------------------------------
// The geometry snapshot, read side
// ---------------------------------------------------------------------------

// The rect side the panel is revealed toward: the side away from the taskbar,
// which is the one that meets the finger. A left taskbar reveals the panel's
// right edge, a top taskbar its bottom, a right taskbar its left, a bottom
// taskbar its top. This is the only place the mapping exists, so a published rect
// and the edge derived from it cannot disagree.
float TrailEdge(int32_t axis, float left, float top, float right, float bottom) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
            return right;
        case DismissAxis::Top:
            return bottom;
        case DismissAxis::Right:
            return left;
        case DismissAxis::Bottom:
            return top;
    }
    return right;
}

float GeometryTrailDip(const GestureGeometry& geometry) {
    return geometry.panel
               ? TrailEdge(geometry.axis(), geometry.left, geometry.top,
                           geometry.right, geometry.bottom)
               : 0.0f;
}

// The trail a reader uses when no panel has been measured: the root-width travel
// this mod shipped with, whose rest position is the far screen edge minus the
// travel. `distance` spans the screen, so on a left or top taskbar that position
// is the travel itself and on a right or bottom one it is 0, the screen origin.
// Both put Progress 0 at the taskbar edge and Progress 1 at the far edge, so a
// reveal that starts before the first good measurement still follows the finger
// on either side instead of pinning the panel open.
float FallbackTrailDip(int32_t axis, float distance) {
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
        case DismissAxis::Top:
            return distance;
        case DismissAxis::Right:
        case DismissAxis::Bottom:
            return 0.0f;
    }
    return distance;
}

// Whether a press belongs to the panel. The measured rectangle decides it, with
// the 2 DIP the rect's own border needs. A panel that was never measured cannot
// place a press, so it does not reject one: the window test and the gesture's own
// guards still apply, and a wrong "outside" would swallow a real dismiss.
bool PanelContainsPress(const GestureGeometry& geometry, float x, float y) {
    if (!geometry.panel) {
        return true;
    }
    return x >= geometry.left - kPanelBandSlopDip &&
           x <= geometry.right + kPanelBandSlopDip &&
           y >= geometry.top - kPanelBandSlopDip &&
           y <= geometry.bottom + kPanelBandSlopDip;
}

// Input thread only: the only reader of the snapshot. The write side reads
// `g_geometry` under the same lock, because it is the thread that writes it.
//
// The writer holds g_geometryLock for one struct assignment, so a shared
// acquisition normally succeeds outright. If it does not, the caller answers the
// last whole copy instead of blocking a report; a new gesture asks for
// `waitForWriter` because it must not start from a snapshot the writer is
// replacing at that instant.
GestureGeometry ReadGestureGeometry(bool waitForWriter) {
    if (!waitForWriter) {
        if (TryAcquireSRWLockShared(&g_geometryLock)) {
            g_inputGeometry = g_geometry;
            ReleaseSRWLockShared(&g_geometryLock);
            return g_inputGeometry;
        }
        return g_inputGeometry;
    }

    AcquireSRWLockShared(&g_geometryLock);
    g_inputGeometry = g_geometry;
    ReleaseSRWLockShared(&g_geometryLock);
    return g_inputGeometry;
}

// Whether this snapshot describes the layout that is still current. A cached
// copy from before a resize fails the test: the cached struct's identity token
// is older than the one the writer published with the new layout.
bool GeometryLayoutCurrent(const GestureGeometry& geometry) {
    return geometry.layoutVersion ==
           g_layoutVersion.load(std::memory_order_acquire);
}

// Whether a vertical dismiss may start here. A vertical drag competes with the
// content's own scrolling: dragging toward the taskbar is how the app list
// scrolls back, and a dismiss started on that drag closed the menu instead.
// Measured on this build, the list is at VerticalOffset 0 on every open and moves
// exactly with its content, so the rule is the scroll position, not a guess at
// the user's intent:
//
// - the scroller state is Nothing: the panel measured nothing that scrolls, so
//   the drag has no content to scroll and belongs to the dismiss;
// - the state is Unknown: no measurement speaks for this open. Wait for the
//   settled pass, but only while the press is inside the panel: a press on the
//   desktop still passes;
// - the press is outside the cached scroller (tiles, rail, panel background): the
//   drag has no content to scroll, allow it;
// - the press is inside it and the list is already at the end the drag would
//   scroll away from (offset 0 for a bottom taskbar, offset == scrollable height
//   for a top one): there is nothing left to scroll, allow it;
// - otherwise the same drag would scroll the list, so the dismiss waits. The
//   contact is not consumed: BeginHidSession runs again on every later sample,
//   so once the user has scrolled the list to its end the rest of that same drag
//   becomes a dismiss.
//
// Left and right keep the plain drag: a sideways drag does not scroll a vertical
// list.
bool VerticalDismissAllowed(const GestureGeometry& geometry, int32_t axis,
                            float x, float y, uint64_t tickMs) {
    if (IsHorizontalAxis(axis)) {
        return true;
    }

    ScrollerCache scroller{};
    {
        std::lock_guard lock(g_scrollerMutex);
        scroller = g_scroller;
    }

    // A cache measured for another open says nothing about this one. The close
    // path releases it, but a reopen that lands before that deferred turn runs
    // must not gate its first press against the previous menu's rect.
    if (scroller.generation !=
        g_openGeneration.load(std::memory_order_acquire)) {
        scroller = ScrollerCache{};
    }

    if (scroller.state == ScrollerState::Nothing) {
        return true;  // measured: nothing in this panel can take the drag
    }
    if (scroller.state == ScrollerState::Unknown) {
        // No measurement yet, or only the transitional one. Failing open here let
        // a swipe right after the open dismiss the menu while the user was
        // scrolling; wait for the settled pass instead, but only while the press
        // is inside the panel so a press on the desktop still passes.
        return !PanelContainsPress(geometry, x, y);
    }

    if (x < scroller.left || x > scroller.right || y < scroller.top ||
        y > scroller.bottom) {
        return true;  // the press is not on the scrolling content
    }

    const bool atEnd =
        axis == static_cast<int32_t>(DismissAxis::Bottom)
            ? scroller.verticalOffset <= kScrollerEndSlopDip
            : scroller.verticalOffset >=
                  scroller.scrollableHeight - kScrollerEndSlopDip;
    if (atEnd) {
        return true;
    }

    // At most one line per window: the digitizer keeps reporting while the
    // finger rests, and each report asks again.
    if (tickMs >= g_scrollerRejectLogTick &&
        tickMs - g_scrollerRejectLogTick >= kDismissRejectLogMs) {
        g_scrollerRejectLogTick = tickMs;
        FileLog(L"dismiss deferred to scroll vo=%.0f sh=%.0f",
                scroller.verticalOffset, scroller.scrollableHeight);
    }
    return false;
}

// Runs on the first report of a contact. A contact that starts on the taskbar
// edge while the menu is closed can own the menu's own open; anything else is
// left to the dismiss path. A touch on the edge of an *open* menu is a dismiss
// start, not a candidate, unless a close of this mod is still in flight.
//
// A dismiss that is still settling does not block a candidate: the edge contact
// during its retract is the pull that can still cancel the close if it began
// before the tap, and refusing it would let the tap take the menu away under the
// finger.
bool BeginEdgeCandidate(const GestureGeometry& geometry, float x, float y,
                        uint64_t tickMs) {
    if (!RendererReady() || !GeometryLayoutCurrent(geometry)) {
        return false;
    }

    if (!(geometry.distance() > 0.0f)) {
        return false;  // renderer not ready: no distance to follow
    }

    const int32_t axis = geometry.axis();
    if (!PointInEdgeBand(axis, x, y)) {
        return false;
    }

    // A close this mod started still reports the window open for a moment. A
    // contact on the edge in that window is a new reveal attempt, not a drag of
    // the settling panel: the shell opens the menu again under the contact
    // and the follow takes over. Treating it as a dismiss drag froze the
    // retract and restored Progress 1 on the down.
    const bool closeInFlight =
        tickMs < g_suppressRestoreUntil.load(std::memory_order_relaxed);
    if (!closeInFlight && StartWindowIsOpenThrottled(tickMs)) {
        return false;
    }

    g_reveal = HidRevealSession{};
    g_reveal.edgeCandidate = true;
    g_reveal.edgeStartX = x;
    g_reveal.edgeStartY = y;
    g_reveal.startTick = tickMs;
    g_reveal.lastTick = tickMs;
    g_reveal.lastSampleQpc = QpcMsNow();
    BeginContactTiming();
    // The release speed belongs to this contact only.
    ResetEndVelocity();

    // The new pull owns the panel from this report on. It starts a new pull
    // generation; a close tap checks it once and is skipped while this pull
    // exists, so the shell never hides the menu the finger is pulling out.
    g_pullSerial++;
    // A close that armed its tap is cancelled the moment this pull begins, not
    // at the tap's deadline: until the cancel is published the record still says
    // "a gesture close is in flight", and a Hide in between would be read as
    // that close's own tap.
    if (g_pendingTapTick != 0 &&
        !SwipeIpcPublishClose(&g_ipc, kSwipeCloseKindCancelled, 0, 0,
                              g_pullSerial)) {
        FileLog(L"close mode cancel stamp failed");
    }
    return true;
}

// One reveal step. While the menu is still closed the contact is only
// remembered; the moment it is open under the contact, the follow takes
// Progress over and every later report drives it.
void UpdateRevealSession(const GestureGeometry& geometry, float x, float y,
                         uint64_t tickMs) {
    HidRevealSession& reveal = g_reveal;
    const uint64_t sampleQpc = QpcMsNow();
    reveal.lastTick = tickMs;
    reveal.lastSampleQpc = sampleQpc;

    if (!reveal.active) {
        if (!StartWindowIsOpenThrottled(tickMs)) {
            return;
        }

        const float distance = geometry.distance();
        if (!(distance > 0.0f)) {
            // A new root is being installed under the contact, so this contact
            // no longer has a renderer to drive.
            reveal.edgeCandidate = false;
            EndContactTiming();
            return;
        }

        reveal.active = true;
        reveal.activeSinceQpc = sampleQpc;
        reveal.distance = distance;
        reveal.progress = 0.0f;
        reveal.samples = 0;

        // The mapped progress below (0.269 at the screen edge on this layout)
        // replaces whatever the panel was doing, so the first step of the follow
        // must not become the coast speed, and the window starts at the value the
        // panel really shows, not at the target of a settle it is taking over.
        ResetCoastVelocity();
        ResetEndVelocity();

        // A cancel or dismiss animation from a previous gesture may still own
        // Progress; the follow must drive the scalar from this sample on, and the
        // takeover pins the position the panel has now so no frame between the
        // two writes shows the old animation's target. A close tap that is still
        // ahead is not touched here: it re-checks the pull generation once, so a
        // pull that began first drops the close.
        reveal.lastInserted = TakeProgressNow();

        // Diagnostic: the position the takeover replaces, so a reopen backlash
        // can be traced to a value left behind or to an animation still running.
        // This is the pinned value, not the recorded target.
        FileLog(L"reveal takeover from=%.3f", reveal.lastInserted);
    }

    const int32_t axis = geometry.axis();
    const float finger = AxisScreenPosition(axis, x, y);
    // Decision quantity: how far the finger moved away from the taskbar edge
    // since the touch began. Signed by the opening direction and clamped at 0,
    // so a pull back toward the edge reads 0 and cannot commit.
    reveal.travel = std::max(
        0.0f, (finger - AxisScreenPosition(axis, reveal.edgeStartX,
                                           reveal.edgeStartY)) *
                  AxisOpeningSign(axis));
    reveal.maxTravel = std::max(reveal.maxTravel, reveal.travel);

    // The trail is the panel edge this open measured. Without a measurement the
    // root-width fallback places it at the far screen edge, which is where the
    // travel maps Progress 0 to the taskbar edge and 1 to the far one.
    const float panelTrail =
        geometry.panel ? GeometryTrailDip(geometry)
                       : FallbackTrailDip(axis, reveal.distance);
    reveal.panelTrail = panelTrail;

    // Absolute screen DIP on both sides of the subtraction: the panel's
    // trailing edge meets the finger, so the progress no longer depends on
    // where the finger was when the menu was first seen open.
    const float progress = std::clamp(
        1.0f - (finger - panelTrail) / (AxisDirection(axis) * reveal.distance),
        0.0f, 1.0f);

    // A report ends the coast: pin where it got to, then insert the real value
    // below even when it is inside the 0.5 DIP filter, so the panel returns to
    // the finger at once. The correction is only the few DIP it drifted.
    float coastValue = reveal.lastInserted;
    const bool resumed =
        g_coasting.load(std::memory_order_relaxed) && StopCoast(coastValue);

    reveal.progress = progress;
    reveal.samples++;
    SampleCoastVelocity(progress, sampleQpc);
    if ((resumed ||
         std::fabs(progress - reveal.lastInserted) * reveal.distance >=
             kProgressEpsilonDip) &&
        InsertProgress(progress)) {
        reveal.lastInserted = progress;
    }
    // The release speed reads what the panel actually showed, so a report the
    // 0.5 DIP filter ignored cannot count as movement.
    SampleEndVelocity(reveal.lastInserted, sampleQpc);
}

// Release of the edge contact. A candidate that never saw the menu open ends
// silently: it did not touch Progress. A reveal that ran logs exactly one line.
//
// The decision mirrors ReleaseHidSession: it judges the travel at the moment of
// release, not the mapped progress. Progress reads 0.269 at the screen edge on
// this layout, so a progress threshold would commit a pull-out that never left
// the edge and a pull out and back alike.
void ReleaseRevealSession(const GestureGeometry& geometry) {
    HidRevealSession& reveal = g_reveal;
    if (!reveal.edgeCandidate) {
        return;
    }

    reveal.edgeCandidate = false;
    if (!reveal.active) {
        EndContactTiming();
        return;
    }
    reveal.active = false;

    // The commit decision keeps the whole-gesture tests this mod was measured
    // against: the travel at release and the session average. Only the seam reads
    // the end of the gesture.
    const uint64_t elapsed = reveal.lastTick > reveal.startTick
                                 ? reveal.lastTick - reveal.startTick
                                 : 0;
    const float travel = std::max(0.0f, reveal.travel);
    const float distance = reveal.distance;
    const float progress = std::clamp(reveal.progress, 0.0f, 1.0f);
    const bool commit =
        ReleaseCommits(true, travel, elapsed, distance, geometry.axis());

    // The release line is written after the animation starts: the file write
    // costs time on the input thread, and every millisecond before the
    // animation is a frame the panel stands still at the seam.
    EndContactTiming();

    // The coast carried the panel past the last report, so both outcomes start
    // from where it got to. `progress` above stays the mapped release position,
    // which is the measurement, not the point the animation starts from.
    const float from = TakeProgressNow();
    // Progress per millisecond at the end of the gesture, signed along the
    // reveal axis. The settle projects it onto its own direction.
    const float endVelocity = EndVelocityProgressPerMs();
    const float releaseSpeed = SpeedTowardDip(endVelocity, from,
                                              commit ? 1.0f : 0.0f, distance);

    if (commit) {
        // Let go past the commit point: the menu stays open and the follow
        // finishes the slide instead of snapping.
        const bool started = BeginGestureOpen(from, endVelocity);
        FileLog(L"open follow travel=%.0f max=%.0f progress=%.2f vel=%.2f "
                L"commit=%d panel=%.0f n=%d",
                travel, reveal.maxTravel, progress, releaseSpeed,
                commit ? 1 : 0, reveal.panelTrail, reveal.samples);
        if (!started) {
            ShowAtRest();
        }
        return;
    }

    // Short reveal: the same settle, the same log line, the same close
    // stamp, the same suppression window and the same armed tap as the
    // committed dismiss, from the same release speed. Both close paths read
    // identically from any finger position.
    //
    // Progress stays at 0 afterwards and is not restored here: the hide takes a
    // moment, and restoring now would flash the fully open menu while the close
    // is in flight. The flag makes the next down restore the resting value once
    // the shell has finished hiding the reused root.
    const bool started =
        BeginGestureClose(from, endVelocity, CloseKind::RevealCancel);
    FileLog(L"open follow travel=%.0f max=%.0f progress=%.2f vel=%.2f "
            L"commit=%d panel=%.0f n=%d",
            travel, reveal.maxTravel, progress, releaseSpeed, commit ? 1 : 0,
            reveal.panelTrail, reveal.samples);
    if (started) {
        g_commitNeedsProgressReset = true;
        return;
    }

    // No settle ran, so no close is owed and no tap may be armed under a panel
    // that is still on screen. The committed dismiss puts its panel back in the
    // same case; this one leaves the menu open, fully in.
    ShowAtRest();
}

void LogHidRelease(const HidGestureSession& session, float velocity,
                   bool commit, uint64_t releaseQpc, const wchar_t* source) {
    const uint64_t releaseDelay = releaseQpc > session.lastSampleQpc
                                      ? releaseQpc - session.lastSampleQpc
                                      : 0;
    FileLog(
        L"hid release src=%s delta=%.1f final=%.1f perp=%.1f vel=%.2f "
        L"commit=%d n=%d relms=%llu",
        source, session.maxDelta, session.curDelta, session.maxPerp, velocity,
        commit ? 1 : 0, session.samples,
        static_cast<unsigned long long>(releaseDelay));
}

void BeginHidSession(const GestureGeometry& geometry, float x, float y,
                     uint64_t tickMs) {
    // A close this mod started is still in flight: the panel is leaving, the
    // shell's Hide is ahead, and a session here would drag Progress back to rest
    // and flash the menu while it is being hidden.
    if (g_dismissSettling.load(std::memory_order_relaxed) ||
        tickMs < g_suppressRestoreUntil.load(std::memory_order_relaxed)) {
        if (tickMs > g_skipLogTick + 1000) {
            g_skipLogTick = tickMs;
            FileLog(L"dismiss skipped settling");
        }
        return;
    }

    const float distance = geometry.distance();
    if (!(distance > 0.0f)) {
        if (tickMs > g_skipLogTick + 1000) {
            g_skipLogTick = tickMs;
            FileLog(L"dismiss skipped distance=0");
        }
        return;
    }

    // The renderer must exist and the menu must be on screen right now. A
    // closed menu keeps its HWND and reports itself visible, so only the DWM
    // cloaked attribute answers this.
    const bool rendererReady = RendererReady();
    if (!rendererReady || !GeometryLayoutCurrent(geometry) ||
        !StartWindowIsOpenThrottled(tickMs)) {
        if (tickMs > g_skipLogTick + 1000) {
            g_skipLogTick = tickMs;
            FileLog(L"dismiss skipped closed root=%d",
                    rendererReady ? 1 : 0);
        }
        return;
    }

    const int32_t axis = geometry.axis();

    // A press outside the panel belongs to the desktop, not to the menu. The
    // Start window is full-screen, so PointInStartWindow alone accepted a click
    // on the desktop next to the panel; on release that session animated
    // Progress back to 1 and flashed the menu while the shell was hiding it.
    // The rectangle and the axis come from the same snapshot, so the test cannot
    // mix a rect from one measurement with a trail from another.
    if (!PanelContainsPress(geometry, x, y)) {
        if (tickMs > g_skipLogTick + 1000) {
            g_skipLogTick = tickMs;
            FileLog(L"dismiss skipped outside panel axis=%d x=%.0f y=%.0f "
                    L"rect=%.0f,%.0f,%.0f,%.0f trail=%.0f gen=%u",
                    axis, x, y, geometry.left, geometry.top, geometry.right,
                    geometry.bottom, GeometryTrailDip(geometry),
                    geometry.generation);
        }
        return;
    }

    // A vertical drag on the app list belongs to the list while it can still
    // scroll that way. The rejection starts no session, and this function runs
    // again on the next sample of the same contact, so a drag that scrolls the
    // list to its end still turns into a dismiss without a new contact. The
    // contact latch itself is left untouched: it only tracks that a finger is
    // down, and the lift path clears it, so the next down is unaffected.
    if (!VerticalDismissAllowed(geometry, axis, x, y, tickMs)) {
        if (tickMs > g_skipLogTick + 1000) {
            g_skipLogTick = tickMs;
            FileLog(L"dismiss skipped vertical axis=%d x=%.0f y=%.0f", axis,
                    x, y);
        }
        return;
    }

    HidGestureSession& session = g_hidSession;
    session = HidGestureSession{};
    session.active = true;
    session.startX = x;
    session.startY = y;
    session.startTick = tickMs;
    session.lastTick = tickMs;
    session.lastSampleQpc = QpcMsNow();
    session.samples = 1;
    BeginContactTiming();
    // The speed of the previous gesture must not survive into this one.
    ResetCoastVelocity();
    ResetEndVelocity();

    // A cancel or open settle from the previous gesture may still be running;
    // take the position the panel has now and stop it. Taking is what keeps this
    // drag from starting at the animation's target: the drag continues from the
    // pinned value, so a takeover mid-settle has no jump. After a commit the base
    // value can still be 0, because the settle callback may not have run yet, so
    // restore the resting value before the first sample drives the scalar. The
    // restore is skipped while a close this mod started is still in flight:
    // inserting 1 there flashed the fully open panel until the shell hid the
    // window. The flag stays set so the next open restores the scalar.
    session.startProgress = TakeProgressNow();
    if (g_commitNeedsProgressReset &&
        tickMs >= g_suppressRestoreUntil.load(std::memory_order_relaxed)) {
        g_commitNeedsProgressReset = false;
        session.startProgress = kProgressRest;
        InsertProgress(kProgressRest);
    }
    session.lastProgress = session.startProgress;

    FileLog(L"hid down x=%.1f y=%.1f axis=%d dist=%.0f", x, y, axis,
            distance);
}

void ReleaseHidSession(const GestureGeometry& geometry, bool allowCommit,
                       const wchar_t* source) {
    HidGestureSession& session = g_hidSession;
    if (!session.active) {
        return;
    }

    // The commit decision keeps the whole-gesture tests this mod was measured
    // against: the travel at release and the session average. Only the seam reads
    // the end of the gesture.
    const uint64_t lastSample = session.lastTick;
    const uint64_t elapsed =
        lastSample > session.startTick ? lastSample - session.startTick : 0;
    // Judge on where the finger actually let go, not on how far it once went:
    // dragging out and back in must cancel, not dismiss.
    const float finalDelta = std::max(0.0f, session.curDelta);
    const float distance = geometry.distance();
    const bool commit =
        ReleaseCommits(allowCommit, finalDelta, elapsed, distance,
                       geometry.axis());

    const uint64_t releaseQpc = QpcMsNow();
    EndContactTiming();

    // The coast continued the drag after the last report; the close has to start
    // from where it got to, not from the last report, or the panel jumps back
    // before it settles. Both gesture families read it through the same call.
    const float from = TakeProgressNow();
    // Progress per millisecond at the end of the gesture, signed along the
    // dismiss axis: a finger still moving toward the taskbar is negative.
    const float endVelocity = EndVelocityProgressPerMs();
    const float releaseSpeed = SpeedTowardDip(
        endVelocity, from, commit ? 0.0f : 1.0f, distance);

    if (commit) {
        const bool started = BeginDismissSettle(from, endVelocity);
        // The release line is written after the settle started: the file write
        // costs time on the input thread, and every millisecond before the
        // animation is a frame the panel stands still at the seam.
        LogHidRelease(session, releaseSpeed, commit, releaseQpc, source);
        if (started) {
            g_commitNeedsProgressReset = true;
        } else {
            ShowAtRest();
        }
        session.active = false;
        return;
    }

    session.active = false;
    // A close started by the shell (a click outside, a Windows-key press) hides
    // the window while this contact is still up. Animating Progress back to rest
    // then replays the panel over a window that is being hidden, which is the
    // flash the user reported. Leave Progress to the close.
    if (g_dismissSettling.load(std::memory_order_relaxed) ||
        GetTickCount64() <
            g_suppressRestoreUntil.load(std::memory_order_relaxed)) {
        FileLog(L"cancel animation skipped (close in flight)");
        return;
    }
    if (distance > 0.0f) {
        BeginGestureOpen(from, endVelocity);
    } else {
        ShowAtRest();
    }
    LogHidRelease(session, releaseSpeed, commit, releaseQpc, source);
}

// Scroll abort: the menu returns to its resting position at once; no
// animation and no dismissal.
void AbortHidSession(const GestureGeometry& geometry) {
    HidGestureSession& session = g_hidSession;
    if (!session.active) {
        return;
    }

    // The same end-of-gesture speed the release lines report, without a target
    // to project onto: an abort has no seam, so the magnitude is enough.
    const float distance = std::max(geometry.distance(), 0.0f);
    const float velocity =
        std::fabs(EndVelocityProgressPerMs()) * distance;

    LogHidRelease(session, velocity, false, QpcMsNow(), L"abort");
    session.active = false;
    EndContactTiming();
    ShowAtRest();
}

void UpdateHidSession(const GestureGeometry& geometry, float x, float y,
                      uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;

    const float distance = geometry.distance();
    if (!(distance > 0.0f)) {
        AbortHidSession(geometry);
        return;
    }

    // A report ends the coast: pin where it got to, then insert the real value
    // below even when it is inside the 0.5 DIP filter, so the panel returns to
    // the finger at once. The correction is only the few DIP it drifted.
    float coastValue = session.lastProgress;
    const bool resumed =
        g_coasting.load(std::memory_order_relaxed) && StopCoast(coastValue);

    const int32_t axis = geometry.axis();
    const float delta = AxisDelta(axis, session, x, y);
    const float perpendicular = PerpDelta(axis, session, x, y);

    session.lastTick = tickMs;
    const uint64_t sampleQpc = QpcMsNow();
    session.lastSampleQpc = sampleQpc;
    session.curDelta = delta;
    session.maxDelta = std::max(session.maxDelta, delta);
    session.maxPerp = std::max(session.maxPerp, perpendicular);
    session.samples++;

    // A mostly perpendicular movement with little axis travel is a scroll of
    // the content, not a dismiss.
    if (session.maxPerp > kScrollAbortPerp &&
        session.maxPerp > kScrollAbortRatio * session.maxDelta &&
        session.maxDelta < kScrollAbortMaxDelta) {
        FileLog(L"dismiss aborted perpendicular perp=%.0f delta=%.0f",
                session.maxPerp, session.maxDelta);
        AbortHidSession(geometry);
        return;
    }

    // A drag that took over a running settle continues from the position the
    // panel was at, so the mapping is measured from the session's own base.
    const float progress = std::clamp(
        session.startProgress - delta / distance, 0.0f, 1.0f);
    SampleCoastVelocity(progress, sampleQpc);
    // The 0.5 DIP filter is the whole gate: a movement toward the open side of a
    // panel already at rest lands on Progress 1 and changes nothing, and a drag
    // that started mid-settle must follow in either direction.
    if (resumed ||
        std::fabs(progress - session.lastProgress) * distance >=
            kProgressEpsilonDip) {
        InsertProgress(progress);
        session.lastProgress = progress;
    }
    // The release speed reads the value the panel actually shows.
    SampleEndVelocity(session.lastProgress, sampleQpc);
}

// One state-machine step per report. It never touches a XAML object;
// composition goes through the mutex-guarded helpers. The geometry snapshot comes
// in as one value for the whole report, so the hit test, the follow and the
// release of this sample cannot read different measurements.
void OnHidSampleStep(const GestureGeometry& geometry, int contacts, float xDip,
                     float yDip, uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;

    // Extra contacts (palm, ghost slot) are filtered when the report is
    // decoded; by the time a sample reaches here it always means "the tracked
    // finger is present".
    if (contacts <= 0) {
        if (session.active) {
            ReleaseHidSession(geometry, true, L"report");
        }
        session.suppress = false;
        ReleaseRevealSession(geometry);
        g_contactDown = false;
        g_openCheckValid = false;
        return;
    }

    if (session.suppress) {
        // A close muted this contact: its reports are not a gesture, but the
        // contact is still on the glass, so the latch must not expire under it.
        // The lift report clears both.
        g_contactDown = true;
        g_contactTick = tickMs;
        return;
    }

    if (session.active) {
        UpdateHidSession(geometry, xDip, yDip, tickMs);
        return;
    }

    // The edge test belongs to the first report of the contact: a finger that
    // slides into the band after starting elsewhere is not a reveal candidate,
    // and a candidate that already exists owns the contact until it lifts.
    const bool newContact =
        !g_contactDown ||
        (tickMs > g_contactTick && tickMs - g_contactTick >= kIdleReleaseMs);
    g_contactDown = true;
    g_contactTick = tickMs;
    if (newContact) {
        BeginEdgeCandidate(geometry, xDip, yDip, tickMs);
    }
    if (g_reveal.edgeCandidate) {
        UpdateRevealSession(geometry, xDip, yDip, tickMs);
        return;
    }

    if (!PointInStartWindow(xDip, yDip)) {
        return;
    }
    BeginHidSession(geometry, xDip, yDip, tickMs);
}

// Publishes whether a live contact owns the panel edge. The UI side re-reads it
// when a measurement runs, so a request recorded on an idle panel cannot move the
// edge under a finger that started in between.
//
// The same call is where a SizeChanged-deferred measurement is released: the
// contact that owned the panel just ended, so the edge can move again. One post
// per deferred resize, never a poll.
void PublishPanelOwnership() {
    const bool owned = g_hidSession.active || g_reveal.edgeCandidate;
    g_panelOwned.store(owned, std::memory_order_relaxed);
    if (!owned && g_measureDeferred.exchange(false, std::memory_order_relaxed) &&
        !g_unloading.load(std::memory_order_relaxed)) {
        const SettledMeasureRequest request = StartMeasureRequest(true);
        PostGeometryMeasurement(request, true);
    }
}

// Every report enters here. The snapshot is taken once, for the whole sample.
void OnHidSample(int contacts, float xDip, float yDip, uint64_t tickMs) {
    GestureGeometry geometry = ReadGestureGeometry();
    // A new contact may not start from a cached copy that predates a resize.
    // The report path uses the try-read; the first report of a contact can
    // afford the writer lock, and an obsolete identity means no gesture until
    // the writer publishes the new layout.
    const bool newContact =
        !g_contactDown ||
        (tickMs > g_contactTick && tickMs - g_contactTick >= kIdleReleaseMs);
    if (newContact && !GeometryLayoutCurrent(geometry)) {
        geometry = ReadGestureGeometry(true);
    }
    OnHidSampleStep(geometry, contacts, xDip, yDip, tickMs);
    PublishPanelOwnership();
}

// The coast through the window between the last report and the release. Only the
// idle tick runs while the stream is silent, so it starts the coast, and it is
// also the tick that ends the session 10-15 ms later: that release stops the
// coast and plans the close from its value.
void MaybeCoast(const GestureGeometry& geometry, uint64_t tickMs,
                uint64_t qpcNow) {
    if (g_coasting.load(std::memory_order_relaxed)) {
        // Nothing interrupted this coast: pin the value it was heading to. The
        // animation is over at this point, and its base value is the report
        // before it, which would snap the panel back.
        if (qpcNow >= g_coastStartQpc &&
            qpcNow - g_coastStartQpc >=
                static_cast<uint64_t>(kCoastDuration.count())) {
            if (g_debugCoast) {
                FileLog(L"coast end value=%.3f", g_coastTo);
            }
            InsertProgress(g_coastTo);  // ends the coast, see InsertProgress
        }
        return;
    }

    // A close this mod started still owns Progress, and a menu that is already
    // closing has no finger motion left to continue.
    if (tickMs < g_suppressRestoreUntil.load(std::memory_order_relaxed)) {
        return;
    }

    const bool revealActive = g_reveal.active;
    if (!g_hidSession.active && !revealActive) {
        return;
    }

    const float distance = geometry.distance();
    if (!(distance > 0.0f)) {
        return;
    }

    // The reveal maps Progress from the panel edge in the snapshot, which the UI
    // thread publishes per open. The first 50 ms of the follow settle.
    if (revealActive &&
        (qpcNow < g_reveal.activeSinceQpc ||
         qpcNow - g_reveal.activeSinceQpc < kRevealCoastSettleMs)) {
        return;
    }

    const uint64_t lastSample =
        revealActive ? g_reveal.lastSampleQpc : g_hidSession.lastSampleQpc;
    if (qpcNow < lastSample || qpcNow - lastSample < kCoastStartMs) {
        return;
    }

    // The HWND outlives the menu and IsWindowVisible stays TRUE while it is
    // closed, so only the cloaked attribute says the menu is on screen. One
    // query per coast is affordable where one per report would not be.
    if (!StartWindowIsOpen()) {
        return;
    }

    const float velocity = CoastVelocity(distance);
    if (std::fabs(velocity) * distance < kCoastMinVelocityDip) {
        return;
    }

    const float from = std::clamp(
        revealActive ? g_reveal.lastInserted : g_hidSession.lastProgress,
        0.0f, 1.0f);
    const float to = std::clamp(
        from + velocity * static_cast<float>(kCoastDuration.count()), 0.0f,
        1.0f);
    if (to == from) {
        return;  // the travel is used up; there is nothing to continue
    }

    if (StartCoast(from, to) && g_debugCoast) {
        FileLog(L"coast start from=%.3f to=%.3f vel=%.2f", from, to,
                velocity * distance);
    }
}

// A device that leaves the surface without a lift report still ends the
// gesture; the idle tick only has to notice the silence, which is measured on
// the QPC clock because the tick clock is quantised to 15.6 ms.
void CheckHidIdleRelease(uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;
    const uint64_t qpcNow = QpcMsNow();
    // One snapshot for the whole tick: the coast, the release and the reveal all
    // decide against the same measurement.
    const GestureGeometry geometry = ReadGestureGeometry();

    // First, before any release is decided. The coast has to be running when the
    // release is planned, and the release is the very next thing this tick does.
    // On a clean 5 ms tick the coast starts 8-13 ms after the last report and the
    // release lands 10-16 ms later; a coalesced tick, which a busy input thread
    // produces, could otherwise cross both thresholds at once and leave the
    // panel standing still for the whole gap. Starting it here makes that tick
    // start the coast and then hand it to the release, which stops it and settles
    // from its value, exactly as on the clean path.
    MaybeCoast(geometry, tickMs, qpcNow);

    // The settled measurement's deadline: the layout watch needs a second layout
    // pass, and one may never come after the open storyboard finishes. The
    // request names itself, so the turn this posts and the layout watch's pass
    // cannot both measure: whichever runs first clears the request.
    SettledMeasureRequest due{};
    if (TakeMeasureDeadline(tickMs, due)) {
        PostGeometryMeasurement(due, true);
    }

    // A settled scroller pass that could not commit (an unreadable tree, the
    // depth limit, or a live contact on the panel) left the gate Unknown for
    // this open. Re-post that pass once the delay has passed, but only while the
    // same open is still on screen: a close or a new open drops the token.
    const uint64_t scrollerRetryAt =
        g_scrollerRetryAt.load(std::memory_order_relaxed);
    if (scrollerRetryAt != 0 && tickMs >= scrollerRetryAt &&
        g_scrollerRetryAt.exchange(0, std::memory_order_relaxed) ==
            scrollerRetryAt) {
        const uint32_t retryGeneration =
            g_scrollerRetryGeneration.load(std::memory_order_relaxed);
        if (retryGeneration != 0 &&
            retryGeneration ==
                g_openGeneration.load(std::memory_order_relaxed) &&
            StartWindowIsOpen()) {
            PostGeometryMeasurement(
                StartMeasureRequest(!g_hidSession.active &&
                                    !g_reveal.edgeCandidate),
                true);
        }
    }

    const bool sessionIdle =
        session.active && qpcNow >= session.lastSampleQpc &&
        qpcNow - session.lastSampleQpc >= kIdleReleaseMs;
    if (sessionIdle) {
        ReleaseHidSession(geometry, true, L"idle");
    }

    const bool revealIdle =
        g_reveal.edgeCandidate && qpcNow >= g_reveal.lastSampleQpc &&
        qpcNow - g_reveal.lastSampleQpc >= kIdleReleaseMs;
    if (revealIdle) {
        ReleaseRevealSession(geometry);
    }

    // A contact that holds no session is not tracked per report, so the same
    // silence that ends a gesture ends the "this contact already started"
    // latch. Without it a lost lift would swallow the next edge swipe. The
    // mute a keyboard close installed belongs to that contact too: with the
    // contact gone there is nothing left to suppress, and the next contact must
    // start normally instead of being ignored until some later lift.
    if (!session.active && !g_reveal.edgeCandidate && g_contactDown &&
        tickMs > g_contactTick && tickMs - g_contactTick >= kIdleReleaseMs) {
        g_contactDown = false;
        session.suppress = false;
        g_openCheckValid = false;
        ForgetTrackedContact();
    }

    PublishPanelOwnership();
}

// The tap both close paths arm. It runs on the input thread at the end of the
// retract. A pull that started after the release owns the menu again, so the tap
// is dropped and no Hide ever reaches the shell. The close record then names the
// cancel with tapped = 0, so a later keyboard close takes its own lead.
void RunPendingCloseTap(uint64_t tickMs) {
    if (g_pendingTapTick == 0 || tickMs < g_pendingTapTick) {
        return;
    }

    const CloseKind kind = g_pendingTapKind;
    const uint32_t pull = g_pendingTapPull;
    g_pendingTapTick = 0;
    g_pendingTapKind = CloseKind::None;

    if (g_pullSerial != pull || g_reveal.active || g_reveal.edgeCandidate) {
        if (!SwipeIpcPublishClose(&g_ipc, kSwipeCloseKindCancelled, 0, 0,
                                  pull)) {
            FileLog(L"close mode cancel stamp failed");
        }
        FileLog(L"close tap dropped kind=%s", CloseKindName(kind));
        return;
    }

    // A Win press during the retract has already hidden the menu. The tap would
    // only toggle it back open.
    if (!StartWindowIsOpen()) {
        FileLog(L"close tap skipped kind=%s (already closed)",
                CloseKindName(kind));
        return;
    }

    // The record the bridge waits for is the tap itself: a fresh sequence with
    // tapped = 1 goes out immediately before the key, so the Hide that follows
    // is provably this tap's and nothing else's.
    if (!SwipeIpcPublishClose(&g_ipc, kSwipeCloseKindGesture, kCloseTapDelayMs,
                              1, pull)) {
        FileLog(L"close mode stamp failed kind=%s", CloseKindName(kind));
    }
    TapWindowsKey();
    FileLog(L"close tap kind=%s delay=%llu", CloseKindName(kind),
            static_cast<unsigned long long>(kCloseTapDelayMs));
}

// ---------------------------------------------------------------------------
// The measurement request, both sides
// ---------------------------------------------------------------------------

// Input thread only. Records the one live request. A newer request replaces the
// older one, so a turn already posted for the old one finds itself superseded.
SettledMeasureRequest StartMeasureRequest(bool trailIdle) {
    std::lock_guard lock(g_measureMutex);
    g_measureRequest.id = ++g_measureSerial;
    g_measureRequest.generation =
        g_openGeneration.load(std::memory_order_relaxed);
    g_measureRequest.trailIdle = trailIdle;
    g_measureRequest.dueTick = GetTickCount64() + kSettledMeasureDeadlineMs;
    return g_measureRequest;
}

// Input thread only: the deadline, taken once. A request that the layout watch
// already completed has no live id, so this answers false.
bool TakeMeasureDeadline(uint64_t tickMs, SettledMeasureRequest& request) {
    std::lock_guard lock(g_measureMutex);
    if (g_measureRequest.id == 0 || g_measureRequest.dueTick == 0 ||
        tickMs < g_measureRequest.dueTick) {
        return false;
    }
    g_measureRequest.dueTick = 0;  // one shot per request
    request = g_measureRequest;
    return true;
}

// Whether this request is still the live one. A turn asks once, at entry.
bool MeasureRequestLive(const SettledMeasureRequest& request) {
    std::lock_guard lock(g_measureMutex);
    return request.id != 0 && g_measureRequest.id == request.id;
}

// The commit-time check for a turn that already knows its id. The request may
// have been superseded while the candidate was being built.
bool MeasureRequestLiveId(uint32_t id) {
    if (id == 0) {
        return false;
    }
    std::lock_guard lock(g_measureMutex);
    return g_measureRequest.id == id;
}

// Ends the request the given id owns. A newer request keeps its own.
void EndMeasureRequest(uint32_t id) {
    std::lock_guard lock(g_measureMutex);
    if (g_measureRequest.id == id) {
        g_measureRequest = SettledMeasureRequest{};
    }
}

// Drops whatever is live, on teardown: nothing measured for the previous open
// may land on the next one.
void CancelMeasureRequest() {
    std::lock_guard lock(g_measureMutex);
    g_measureRequest = SettledMeasureRequest{};
}

// The close path cancels the request it saw, by id. An id that no longer names
// the live request belongs to the previous open: the close turn that arrives
// after a reopen must not cancel the reopen's request.
void CancelMeasureRequestIfId(uint32_t id) {
    if (id == 0) {
        return;
    }
    std::lock_guard lock(g_measureMutex);
    if (g_measureRequest.id == id) {
        g_measureRequest = SettledMeasureRequest{};
    }
}

// The close path captures the request before the UI turn is posted, so the turn
// can cancel exactly that request and no later one.
SettledMeasureRequest SnapshotMeasureRequest() {
    std::lock_guard lock(g_measureMutex);
    return g_measureRequest;
}

// Runs the one geometry measurement on the UI thread. Every trigger comes through
// here, so the axis, the travel, the panel rect and the scroller are measured and
// validated against the state at the moment of the run: the epoch the request was
// recorded in, whether a root is attached, and whether a gesture owns the panel.
// `settled` is the pass after the shell placed the panel; it is also the pass
// that ends the request.
void RunGeometryMeasurement(const SettledMeasureRequest& request, bool settled) {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return;
    }
    if (!MeasureRequestLive(request)) {
        return;  // superseded, already run, or cancelled by a close
    }
    if (request.generation != g_openGeneration.load(std::memory_order_relaxed)) {
        // The menu this was recorded for is gone: a close or a new open moved
        // the epoch, so nothing measured now belongs to what is on screen.
        EndMeasureRequest(request.id);
        return;
    }
    if (!g_root) {
        EndMeasureRequest(request.id);
        return;
    }
    bool haveRenderer = false;
    {
        std::lock_guard lock(g_compositionMutex);
        haveRenderer = g_state != nullptr;
    }
    if (!haveRenderer) {
        // The measure mutex is never taken under the composition mutex; the two
        // sections here are separate for that reason.
        EndMeasureRequest(request.id);
        return;
    }

    try {
        if (!RefreshGestureGeometry()) {
            // The composition did not take the new axis or travel, so the
            // published snapshot must keep the old ones. Nothing to measure.
            EndMeasureRequest(request.id);
            return;
        }
        if (!MeasureRequestLive(request)) {
            return;  // a newer request owns the open now
        }
        if (!settled) {
            // One trail line per open, and the open-time turn is where it starts:
            // the settled turn then only speaks when the value moved.
            g_panelTrailLogged = false;
        }
        // The edge may only move while no contact drives it. This is re-read
        // inside the publish, at commit, not taken when the request was recorded:
        // a finger that started between the two must not have the edge moved
        // under it. The generation and the request id are re-checked there too.
        const bool edgeIdle =
            request.trailIdle && !g_panelOwned.load(std::memory_order_relaxed);
        if (!PublishPanelTrailDip(edgeIdle, request.generation, request.id)) {
            EndMeasureRequest(request.id);
            return;
        }
        if (!MeasureRequestLive(request)) {
            return;
        }
        // The dismiss gate needs the scrolling element of this open. The
        // open-time pass records it, the settled pass records it again with the
        // list's final extent, and the settled pass is what arms the gate. It
        // validates the same request and epoch before it commits.
        CacheScrollableElement(settled, request.generation, request.id);
    } catch (...) {
        // A closing menu must not break the dispatcher turn.
        EndMeasureRequest(request.id);
        return;
    }

    if (settled) {
        EndMeasureRequest(request.id);
        return;
    }

    // The settled pass belongs to the layout watch's second pass; the deadline in
    // the request is the guaranteed path when no second pass comes.
    ArmLayoutWatch(request);
}

// Posts one turn of the measurement. The request travels with the turn, so a turn
// that runs late can tell a superseded request from the live one.
void PostGeometryMeasurement(const SettledMeasureRequest& request, bool settled) {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return;
    }
    wucore::CoreDispatcher dispatcher = CopyDispatcher();
    if (!dispatcher) {
        return;
    }
    try {
        dispatcher.TryRunAsync(wucore::CoreDispatcherPriority::Normal,
                               [request, settled]() {
                                   RunGeometryMeasurement(request, settled);
                               });
    } catch (winrt::hresult_error const& e) {
        FileLog(L"gesture geometry queue err=%08X",
                static_cast<unsigned>(e.code()));
    } catch (...) {
        FileLog(L"gesture geometry queue err=unknown");
    }
}

// Posted by the input thread when the shell reports the window cloaked. The
// measurement request and both watches belong to the open that just ended: a
// measurement of that layout must not publish into the next open, and a cached
// scroller would gate the next open against the old rect and offsets. The UI
// thread releases them.
//
// The turn names what it owns instead of clearing whatever is live: the request
// id captured before the close, and the closed open's generation for the layout
// watch and the scroller cache. A reopen that installs its own request and its
// own watch before this turn runs keeps them.
void PostUiOpenWatchRelease(uint32_t closedGeneration, uint32_t requestId) {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return;
    }

    wucore::CoreDispatcher dispatcher = CopyDispatcher();
    if (!dispatcher) {
        return;
    }

    try {
        dispatcher.TryRunAsync(
            wucore::CoreDispatcherPriority::Normal,
            [closedGeneration, requestId]() {
                try {
                    if (g_unloading.load(std::memory_order_relaxed)) {
                        return;
                    }
                    CancelMeasureRequestIfId(requestId);
                    StopSettledTrailWatchIfGeneration(closedGeneration);
                    ReleaseScrollableElementIfGeneration(closedGeneration);
                    // The retry token is not cleared here: a newer open may
                    // already have scheduled its own, and a stale token is
                    // dropped by the generation check in the idle tick.
                } catch (...) {
                    // Never let an exception escape the dispatcher callback.
                }
            });
    } catch (winrt::hresult_error const& e) {
        FileLog(L"open watch release err=%08X", static_cast<unsigned>(e.code()));
    } catch (...) {
        FileLog(L"open watch release err=unknown");
    }
}

// Two things happen here. A closed-to-open transition that no reveal owns is a
// keyboard, Start-button or tray open: the shell slides its own storyboard and
// the mod makes `Progress` follow from 0, so the same root slides the same way a
// gesture open does. The settle needs the measured travel, so an open that lands
// before the first geometry publish restores the resting value and lets the
// shell's storyboard run instead. A close the shell performs by itself (Windows
// key) is not fought: nothing is inserted, and the next open starts from 0
// again.
//
// The poll runs on the 5 ms idle tick but accepts one cloaked answer per 20 ms;
// it used 100 ms for the restore check alone. Measured against the uncloak, a
// 100 ms throttle started the open slide up to 94 ms late, so the panel sat at
// rest first and then jumped out and slid back in. 20 ms keeps the start within
// one compositor frame of the shell's own open, which is what makes the two read
// as one animation.
//
// The open transition is also the one place the gesture geometry is refreshed
// without a system event: the XAML root is reused between opens, so
// PrepareRootElement does not fire again and the axis, Distance and DirX/DirY
// captured by Setup can belong to a taskbar that has moved since.
void PollRootVisibility(uint64_t tickMs) {
    static uint64_t lastPollTick = 0;
    if (tickMs - lastPollTick < kOpenCheckIntervalMs) {
        return;
    }
    lastPollTick = tickMs;

    const bool open = StartWindowIsOpen();
    const bool justOpened = open && !g_rootWasOpen;
    // A close this mod started reports the window open until the shell has run
    // the Hide its tap caused. Reading that as a reopen restored Progress 1
    // while the close was still in flight, so a follow that began in that window
    // flashed the panel fully open and the window then vanished.
    const bool closeInFlight =
        tickMs < g_suppressRestoreUntil.load(std::memory_order_relaxed);
    // An edge contact that is still down owns the open: the follow drives the
    // scalar from the finger, and inserting a fully open frame here would fight
    // it.
    const bool gestureOwnsProgress = g_reveal.edgeCandidate;
    if (justOpened) {
        // A fresh menu, so every retract request written for the previous one is
        // void from this point on, whatever its own deadline says. The
        // generation goes out before any close of this open can be answered; the
        // 20 ms poll below is the only delay, and no user closes a menu they have
        // not seen yet.
        SwipeIpcPublishOpen(&g_ipc, tickMs);
        // The open publish also cancels the close stamp the previous menu left.
        // The bridge refuses a stamp older than the open tick, but an
        // unconsumed tapped record from the previous menu can still be the
        // newest record when the next Hide arrives inside 800 ms; publishing the
        // cancelled kind here is what proves no tap of this menu is in flight.
        // This is the one place that has to do it: the open publisher.
        if (!SwipeIpcPublishClose(&g_ipc, kSwipeCloseKindCancelled, 0, 0,
                                  g_pullSerial)) {
            FileLog(L"close mode cancel stamp failed");
        }
        // A new open is also a new geometry epoch: every measurement recorded for
        // the previous one is stale from this point on, and a request that was
        // still live is cancelled by the epoch check in its turn.
        g_openGeneration.fetch_add(1, std::memory_order_relaxed);
        // The keyboard open is the gesture open from rest: one curve for every
        // open, so a Windows press and an edge pull cannot look different, and
        // the travel plans the duration exactly as it does there.
        if (!closeInFlight && !gestureOwnsProgress) {
            // The settle needs the measured travel, from the snapshot the follow
            // reads. The measurement request below runs after this poll, so an
            // open that lands before the first publish still has Distance 0: a
            // settle planned from it would run the 160 ms floor. Restore the
            // resting value instead and let the shell's own storyboard run.
            if (ReadGestureGeometry().distance() > 0.0f) {
                std::chrono::milliseconds duration{0};
                if (BeginGestureSettle(0.0f, 1.0f, 0.0f, duration)) {
                    // The animation now owns Progress, so no later path needs to
                    // restore the resting value for this open.
                    g_commitNeedsProgressReset = false;
                    FileLog(L"win open dur=%llu",
                            static_cast<unsigned long long>(duration.count()));
                }
            } else {
                ShowAtRest();
                FileLog(L"win open restore (no geometry)");
            }
        }

        // One measurement request per open, recorded before the first dismiss of
        // this open can start. The trail is only re-measured while no contact owns
        // the edge: a live follow reads it every sample, and the run-time check
        // catches a finger that starts between this record and the turn.
        const SettledMeasureRequest request = StartMeasureRequest(
            !closeInFlight && !g_hidSession.active && !g_reveal.edgeCandidate);
        PostGeometryMeasurement(request, false);
    } else if (!open && g_rootWasOpen) {
        // The close has landed, so its suppression no longer applies. Without
        // this a Win press right after the hide opened the window with Progress
        // still at 0, and the panel was invisible until the next open.
        g_suppressRestoreUntil.store(0, std::memory_order_relaxed);
        // The closed menu shows no panel, and the layout of the next open is a
        // new epoch: a measurement still in flight belongs to the menu that just
        // went away. The request id is captured before the epoch moves, so the
        // deferred release cancels exactly the request of this open.
        const uint32_t closedGeneration =
            g_openGeneration.load(std::memory_order_relaxed);
        const SettledMeasureRequest closing = SnapshotMeasureRequest();
        g_openGeneration.fetch_add(1, std::memory_order_relaxed);
        FileLog(L"win close observed");
        // The measurement request and the cached scroller belong to the open that
        // just ended; the UI thread releases them.
        PostUiOpenWatchRelease(closedGeneration, closing.id);
    }

    g_rootWasOpen = open;
}

// Maps one decoded contact point to screen DIP and feeds it to the state
// machine: one call per report that names a real contact at a position.
void FeedContactPoint(HidDevice& device, long rawX, long rawY, USHORT reportId,
                      uint64_t tickMs) {
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if (screenWidth <= 1 || screenHeight <= 1) {
        LogDecodeFail(device, 0);
        return;
    }

    const HidAxisRange xRange = FindAxisRange(device, kUsageX, reportId);
    const HidAxisRange yRange = FindAxisRange(device, kUsageY, reportId);
    const float dpiScale = HidDpiScale();

    const int32_t pixelX = ToScreenPixel(rawX, xRange, screenWidth);
    const int32_t pixelY = ToScreenPixel(rawY, yRange, screenHeight);
    const float xDip = static_cast<float>(pixelX) / dpiScale;
    const float yDip = static_cast<float>(pixelY) / dpiScale;

    // The right and bottom edge bands are measured from the screen, which this
    // thread only knows in pixels; publish it in the same DIP space as xDip.
    g_screenDipWidth = static_cast<float>(screenWidth) / dpiScale;
    g_screenDipHeight = static_cast<float>(screenHeight) / dpiScale;

    // Diagnostic: does the digitizer see a touch that starts on the screen edge
    // before the menu is open? Off in the shipping build; it measured the edge
    // contact and EP's own open threshold.
    if (g_debugEdge && !g_hidSession.active && xDip < 200.0f &&
        tickMs > g_edgeLogTick + 100) {
        g_edgeLogTick = tickMs;
        FileLog(L"edge x=%.0f y=%.0f open=%d", xDip, yDip,
                StartWindowIsOpen() ? 1 : 0);
    }

    OnHidSample(1, xDip, yDip, tickMs);
    // Where this contact was last seen. Only the tie between two real contacts
    // in one report reads it, and it is written after the state machine, so a
    // session that starts on this report cannot reset it.
    g_hidSession.lastRawX = rawX;
    g_hidSession.lastRawY = rawY;
}

void FeedHidReport(HidDevice& device, const BYTE* report,
                   ULONG reportLength, uint64_t tickMs) {
    if (!device.ready || !device.preparsed) {
        LogDecodeFail(device, 0);
        return;
    }

    // HID puts the report ID in the first byte of a report from a device that
    // declares report IDs. RAWINPUTHEADER.wParam is not it: that is RIM_INPUT
    // (0) or RIM_INPUTSINK (1), so using it here would either defeat the
    // report-ID filter or select the wrong value cap.
    const USHORT reportId =
        device.hasReportIds && reportLength > 0 ? report[0] : 0;

    USHORT collections[kMaxContactCollections] = {};
    int slotCount = 0;
    if (device.contactNodeCount > 0) {
        slotCount = device.contactNodeCount;
        for (int i = 0; i < slotCount; ++i) {
            collections[i] = device.contactNodes[i];
        }
    } else {
        slotCount = 1;
        collections[0] = 0;
    }

    // Contact Count is read for two reasons: a report that carries only it still
    // speaks about contact state (it is not an unrelated report ID), and on a
    // count/position device it is the number of fingers. It never speaks for a
    // tracked contact whose own slot this report does not carry.
    ULONG topLevelCount = 0;
    const HidField countField =
        device.hasContactCount
            ? ReadUsageValue(device, kUsagePageDigitizer,
                             device.contactCountLink, kUsageContactCount,
                             report, reportLength, &topLevelCount)
            : HidField::Absent;
    if (countField == HidField::Error) {
        LogDecodeFail(device, 0);
        return;
    }

    // Identity first. A tracked contact is moved or ended only by its own slot
    // in its own device's report: another slot's coordinates, another slot's
    // tip-up or another device cannot speak for it. A report whose ID carries
    // none of the contact fields finds nothing declared here and is dropped,
    // instead of feeding a zero that would synthesize a lift.
    if (g_trackedContact.slot >= 0) {
        if (g_trackedContact.device != device.handle) {
            return;
        }
        if (g_trackedContact.slot >= slotCount) {
            ForgetTrackedContact();  // the device table recycled this handle
        } else {
            HidContactSlot slot;
            NTSTATUS error = HIDP_STATUS_SUCCESS;
            if (!DecodeContactSlot(device, collections[g_trackedContact.slot],
                                   reportId, report, reportLength, slot,
                                   error)) {
                LogDecodeFail(device, static_cast<unsigned>(error));
                return;
            }
            if (!slot.declared) {
                return;  // this report carries nothing for the tracked slot
            }
            const HidContactState state = SlotContactState(slot);
            if (state == HidContactState::NotContact) {
                // The tracked contact ended: its declared tip went up (a pen
                // lift) or its declared in-range bit cleared. No other slot in
                // this report restarts it. `Unknown` never ends it: a report
                // that carries no contact field for the slot cannot speak for
                // the contact.
                ForgetTrackedContact();
                OnHidSample(0, 0.0f, 0.0f, tickMs);
                return;
            }
            if (!slot.hasPosition) {
                // A contact that has not sent a point in this report; the next
                // one of this slot carries one.
                return;
            }
            FeedContactPoint(device, slot.rawX, slot.rawY, reportId, tickMs);
            return;
        }
    }

    // No identity yet: decode every candidate slot. `declared` separates an
    // unrelated report ID (no contact field at all, ignored whole) from a decode
    // failure (logged and dropped), which the old single "no buttons" answer
    // could not tell apart.
    HidContactSlot slots[kMaxContactCollections];
    bool anyDeclared = countField == HidField::Ok;
    for (int i = 0; i < slotCount; ++i) {
        NTSTATUS error = HIDP_STATUS_SUCCESS;
        if (!DecodeContactSlot(device, collections[i], reportId, report,
                               reportLength, slots[i], error)) {
            LogDecodeFail(device, static_cast<unsigned>(error));
            return;
        }
        anyDeclared = anyDeclared || slots[i].declared;
    }
    if (!anyDeclared) {
        return;  // unrelated report: no contact field, no sample
    }

    if (device.coordinatePath) {
        // The device declares neither Tip Switch nor In Range, so a position is
        // its only contact evidence. This is the path this mod shipped with and
        // the only per-device fallback left.
        int nonZero = 0;
        int chosen = -1;
        long chosenX = 0;
        long chosenY = 0;
        long bestDistance = 0;
        for (int i = 0; i < slotCount; ++i) {
            if (!slots[i].hasPosition) {
                continue;
            }
            nonZero++;
            if (!g_hidSession.active) {
                if (chosen < 0) {
                    chosen = i;
                    chosenX = slots[i].rawX;
                    chosenY = slots[i].rawY;
                }
                continue;
            }
            const long dx = slots[i].rawX - g_hidSession.lastRawX;
            const long dy = slots[i].rawY - g_hidSession.lastRawY;
            const long distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (chosen < 0 || distance < bestDistance) {
                chosen = i;
                chosenX = slots[i].rawX;
                chosenY = slots[i].rawY;
                bestDistance = distance;
            }
        }

        const int contacts = countField == HidField::Ok
                                 ? static_cast<int>(topLevelCount)
                                 : nonZero;
        if (countField == HidField::Ok && contacts == 0) {
            // Contact Count is authoritative: on a lift report the position
            // fields often keep their last value, so trusting them would hide
            // the lift and force the idle timer to guess.
            OnHidSample(0, 0.0f, 0.0f, tickMs);
            return;
        }
        if (chosen < 0) {
            // No positioned collection: with no count to trust this is a lift;
            // with a count it means the position fields were not decoded.
            if (contacts <= 0) {
                OnHidSample(0, 0.0f, 0.0f, tickMs);
            } else {
                LogDecodeFail(device, 0);
            }
            return;
        }
        FeedContactPoint(device, chosenX, chosenY, reportId, tickMs);
        return;
    }

    // A button device: the report's real contacts are the candidates, so a slot
    // whose tip is up with stale coordinates can neither win the choice nor
    // restart the tracked finger. The slot nearest the last tracked raw position
    // breaks a tie between two real contacts.
    int realContacts = 0;
    int chosen = -1;
    long bestDistance = 0;
    for (int i = 0; i < slotCount; ++i) {
        if (SlotContactState(slots[i]) != HidContactState::Contact) {
            continue;
        }
        realContacts++;
        if (!slots[i].hasPosition) {
            continue;
        }
        const long dx = slots[i].rawX - g_hidSession.lastRawX;
        const long dy = slots[i].rawY - g_hidSession.lastRawY;
        const long distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (chosen < 0 || distance < bestDistance) {
            chosen = i;
            bestDistance = distance;
        }
    }

    if (chosen < 0) {
        if (realContacts > 0) {
            // A real contact whose point has not arrived yet; the report that
            // carries one starts it.
            return;
        }
        if (countField == HidField::Ok && topLevelCount > 0) {
            // Contacts are down, but this report names none of them with a
            // position. Feeding a lift here would end a contact it does not
            // speak for.
            return;
        }
        // No contact in this report: this is the lift, and doing nothing would
        // leave the gesture to the 18 ms idle timer.
        OnHidSample(0, 0.0f, 0.0f, tickMs);
        return;
    }

    g_trackedContact.device = device.handle;
    g_trackedContact.slot = chosen;
    FeedContactPoint(device, slots[chosen].rawX, slots[chosen].rawY, reportId,
                     tickMs);
}

void HandleHidInput(const RAWINPUT* raw) {
    const RAWHID& hid = raw->data.hid;
    if (hid.dwSizeHid == 0 || hid.dwCount == 0) {
        return;
    }

    HidDevice* device = GetHidDevice(raw->header.hDevice);
    if (!device) {
        return;
    }

    const uint64_t tickMs = GetTickCount64();
    for (UINT i = 0; i < hid.dwCount; ++i) {
        const BYTE* report =
            hid.bRawData + static_cast<size_t>(i) * hid.dwSizeHid;
        FeedHidReport(*device, report, hid.dwSizeHid, tickMs);
    }
}

void HandleRawInput(HRAWINPUT rawHandle) {
    BYTE stackBuffer[512];
    UINT size = sizeof(stackBuffer);
    SetLastError(0);
    UINT read = GetRawInputData(rawHandle, RID_INPUT, stackBuffer, &size,
                                sizeof(RAWINPUTHEADER));
    BYTE* heapBuffer = nullptr;

    if (read == static_cast<UINT>(-1)) {
        if (size <= sizeof(stackBuffer) || size > 64 * 1024) {
            return;
        }
        heapBuffer = new (std::nothrow) BYTE[size];
        if (!heapBuffer) {
            return;
        }
        UINT heapSize = size;
        read = GetRawInputData(rawHandle, RID_INPUT, heapBuffer, &heapSize,
                               sizeof(RAWINPUTHEADER));
        if (read == static_cast<UINT>(-1)) {
            delete[] heapBuffer;
            return;
        }
    }

    const auto* raw = reinterpret_cast<const RAWINPUT*>(
        heapBuffer ? heapBuffer : stackBuffer);
    if (raw->header.dwType == RIM_TYPEHID) {
        HandleHidInput(raw);
    }

    delete[] heapBuffer;
}

LRESULT CALLBACK HidWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                               LPARAM lParam) {
    switch (msg) {
        case WM_INPUT:
            HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
            return 0;
        case WM_TIMER:
            if (wParam == kIdleTimerId) {
                const uint64_t now = GetTickCount64();
                RunPendingCloseTap(now);
                CheckHidIdleRelease(now);
                PollRootVisibility(now);
            }
            return 0;
        // The taskbar moved or the display layout changed: the dismiss axis and
        // the translation baked into the composition expression are stale, and
        // the panel rect was measured against the old work area. This is the
        // event that replaces any polling for both. The message arrives on this
        // thread; the measurement is posted to the UI thread, which owns the XAML
        // root and the property set.
        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETWORKAREA) {
                FileLog(L"work area changed");
                PostGeometryMeasurement(
                    StartMeasureRequest(!g_hidSession.active &&
                                        !g_reveal.edgeCandidate),
                    false);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            FileLog(L"work area changed");
            PostGeometryMeasurement(
                StartMeasureRequest(!g_hidSession.active &&
                                    !g_reveal.edgeCandidate),
                false);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void RegisterHidUsage(USAGE usage, HWND hwnd) {
    RAWINPUTDEVICE device{};
    device.usUsagePage = kUsagePageDigitizer;
    device.usUsage = usage;
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = hwnd;

    SetLastError(0);
    const BOOL ok = RegisterRawInputDevices(&device, 1, sizeof(device));
    const DWORD error = ok ? 0 : GetLastError();
    FileLog(L"hid reg usage=%u ok=%d err=%u", static_cast<unsigned>(usage),
            ok ? 1 : 0, static_cast<unsigned>(error));
}

void UnregisterHidUsage(USAGE usage) {
    RAWINPUTDEVICE device{};
    device.usUsagePage = kUsagePageDigitizer;
    device.usUsage = usage;
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = nullptr;
    RegisterRawInputDevices(&device, 1, sizeof(device));
}

DWORD WINAPI HidInputThread(LPVOID) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = HidWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kHidWindowClass;

    if (!RegisterClassExW(&windowClass)) {
        FileLog(L"hid thread fail err=%u",
                static_cast<unsigned>(GetLastError()));
        return 0;
    }

    // A hidden top-level window, not a message-only one: message-only windows
    // are documented not to receive broadcast messages, and WM_SETTINGCHANGE
    // (SPI_SETWORKAREA) and WM_DISPLAYCHANGE are broadcasts. That is the whole
    // event-driven axis refresh. The window is never shown, has no size and no
    // extended style, so it does not appear in Alt-Tab or the taskbar; Raw Input
    // with RIDEV_INPUTSINK targets it exactly as it did a message-only window.
    HWND hwnd = CreateWindowExW(0, kHidWindowClass, L"", 0, 0, 0, 0, 0, nullptr,
                                nullptr, windowClass.hInstance, nullptr);
    if (!hwnd) {
        FileLog(L"hid thread fail err=%u",
                static_cast<unsigned>(GetLastError()));
        UnregisterClassW(kHidWindowClass, windowClass.hInstance);
        return 0;
    }

    g_hidHwnd.store(hwnd, std::memory_order_relaxed);

    RegisterHidUsage(kUsageTouchScreen, hwnd);
    RegisterHidUsage(kUsagePen, hwnd);

    // The idle tick: a high-resolution waitable timer, because WM_TIMER stays on
    // the 15.6 ms tick on this build even while a 1 ms period is held. The
    // WM_TIMER tick stays as the fallback.
    HANDLE idleTimer = CreateIdleWaitableTimer();
    if (!idleTimer) {
        FileLog(L"hid timer fallback err=%u",
                static_cast<unsigned>(GetLastError()));
        SetTimer(hwnd, kIdleTimerId, kIdleTimerMs, nullptr);
    }

    // Object 0 is the stop event, then the idle tick when the waitable timer
    // exists, then the debug command event and the bridge's retract request,
    // then the message queue. The stop event does not depend on the window: the
    // unload sets it even if this thread has not published the HWND yet, and it
    // is checked again after every message batch, because a queued message can
    // win the wait over a just-signalled event.
    HANDLE waits[4] = {};
    DWORD waitCount = 0;
    if (g_hidStopEvent) {
        waits[waitCount++] = g_hidStopEvent;
    }
    if (idleTimer) {
        waits[waitCount++] = idleTimer;
    }
    if (g_ipc.commandEvent) {
        waits[waitCount++] = g_ipc.commandEvent;
    }
    if (g_ipc.retractRequestEvent) {
        waits[waitCount++] = g_ipc.retractRequestEvent;
    }

    MSG msg;
    bool running = true;
    while (running) {
        // Bounded, so the stop flag is observed even if the stop event were
        // missing from the set; the event itself is object 0 and wakes this at
        // once.
        const DWORD wait =
            MsgWaitForMultipleObjects(waitCount, waits, FALSE, 10, QS_ALLINPUT);
        if (wait == WAIT_TIMEOUT) {
            if (g_hidStopEvent &&
                WaitForSingleObject(g_hidStopEvent, 0) == WAIT_OBJECT_0) {
                running = false;
            }
            continue;
        }
        if (wait == WAIT_OBJECT_0 + waitCount) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (running && g_hidStopEvent &&
                WaitForSingleObject(g_hidStopEvent, 0) == WAIT_OBJECT_0) {
                running = false;
            }
            continue;
        }
        if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + waitCount) {
            const HANDLE signalled = waits[wait - WAIT_OBJECT_0];
            if (signalled == g_hidStopEvent) {
                running = false;
                continue;
            }
            if (signalled == idleTimer) {
                const uint64_t now = GetTickCount64();
                RunPendingCloseTap(now);
                CheckHidIdleRelease(now);
                PollRootVisibility(now);
                continue;
            }
            if (signalled == g_ipc.commandEvent) {
                ApplyPendingCommand();
                continue;
            }
            if (signalled == g_ipc.retractRequestEvent) {
                ApplyRetractRequest();
                continue;
            }
            continue;
        }
        break;  // WAIT_FAILED: leave the loop
    }
    if (running) {
        FileLog(L"hid wait failed err=%u",
                static_cast<unsigned>(GetLastError()));
    }

    KillTimer(hwnd, kIdleTimerId);
    if (idleTimer) {
        CancelWaitableTimer(idleTimer);
        CloseHandle(idleTimer);
    }
    UnregisterHidUsage(kUsageTouchScreen);
    UnregisterHidUsage(kUsagePen);

    g_hidSession = HidGestureSession{};
    g_reveal = HidRevealSession{};
    g_panelOwned.store(false, std::memory_order_relaxed);
    // No measurement of this thread's opens is left to run.
    CancelMeasureRequest();
    // A contact that is still down when the mod unloads must not leave its 1 ms
    // timer request behind.
    EndContactTiming();
    g_contactDown = false;
    g_openCheckValid = false;
    g_pendingTapTick = 0;
    g_pendingTapKind = CloseKind::None;
    g_coasting.store(false, std::memory_order_relaxed);
    ResetCoastVelocity();
    FreeHidDevices();

    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    g_hidHwnd.store(nullptr, std::memory_order_relaxed);
    UnregisterClassW(kHidWindowClass, windowClass.hInstance);
    return 0;
}

// ---------------------------------------------------------------------------
// UI-thread state publication
// ---------------------------------------------------------------------------

float DpiScaleForWindow(HWND hwnd) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

// UI thread only. The single writer of the snapshot the input thread reads: one
// complete value in, one complete value out, under the short exclusive writer
// lock. The snapshot's identity token is minted here: the same identity keeps the
// token, a changed one raises it, so a reader can tell a copy from before a resize
// from the layout on screen now.
void PublishGestureGeometry(const GestureGeometry& candidate) {
    AcquireSRWLockExclusive(&g_geometryLock);
    const bool sameLayout = SameLayoutIdentity(g_geometry.layout, candidate.layout);
    GestureGeometry published = candidate;
    if (sameLayout && g_geometry.layoutVersion != 0) {
        published.layoutVersion = g_geometry.layoutVersion;
    } else {
        // A RefreshGestureGeometry that committed a changed layout already
        // raised the token; otherwise (Setup, teardown) the publish itself is
        // the change and raises it here.
        uint32_t version = g_layoutVersion.load(std::memory_order_acquire);
        if (version <= g_geometry.layoutVersion) {
            version =
                g_layoutVersion.fetch_add(1, std::memory_order_acq_rel) + 1;
        }
        published.layoutVersion = version;
    }
    g_geometry = published;
    ReleaseSRWLockExclusive(&g_geometryLock);

    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        return;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return;
    }

    const float scale = DpiScaleForWindow(hwnd);
    g_winRectDip[0].store(
        static_cast<int64_t>(std::llround(rect.left / scale)),
        std::memory_order_relaxed);
    g_winRectDip[1].store(
        static_cast<int64_t>(std::llround(rect.top / scale)),
        std::memory_order_relaxed);
    g_winRectDip[2].store(
        static_cast<int64_t>(std::llround(rect.right / scale)),
        std::memory_order_relaxed);
    g_winRectDip[3].store(
        static_cast<int64_t>(std::llround(rect.bottom / scale)),
        std::memory_order_relaxed);
}

// Maps the panel rectangle into absolute screen DIP. The visible panel is the
// largest visible child of the root that does not fill it (StartSizingFrame); the
// rest of the root canvas is transparent and must not be part of the reveal
// travel. The edge the reveal anchors to is derived from this rect by TrailEdge,
// so the rect and the edge cannot disagree. Runs on the UI thread and returns
// false while that child is not laid out yet.
//
// The screen origin of the XAML coordinate space, in DIP. The content is laid
// out in the host window's client area, and that area does not start at the
// work-area origin: measured on a bottom taskbar, the CoreWindow sits at x=447
// px while the work area starts at 0, so the panel's frame right of 1005 DIP is
// drawn at 1228.5 DIP, not 1005. The work area only matched on a left taskbar
// because the island was inset by exactly the taskbar width; the client origin
// holds for every axis and every layout. Both the panel rect and the cached
// scroller rect are mapped through it, so it lives in one place.
bool XamlOriginDip(float& originX, float& originY) {
    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        return false;
    }

    POINT clientOrigin{0, 0};
    if (!ClientToScreen(hwnd, &clientOrigin)) {
        return false;
    }

    const float scale = DpiScaleForWindow(hwnd);
    if (!(scale > 0.0f)) {
        return false;
    }

    originX = static_cast<float>(clientOrigin.x) / scale;
    originY = static_cast<float>(clientOrigin.y) / scale;
    return true;
}

// The panel rect a measurement produced, before anything is published. Only a
// measurement that PanelRectAccepted sanctions becomes part of a snapshot.
struct PanelMeasurement {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// A screen rectangle in DIP.
struct DisplayRectDip {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// Where the panel lives: the monitor's work area and the monitor itself. The
// taskbar is what shrinks the work area, so the panel is attached to the
// work-area edge on the taskbar's side — or to the monitor edge when the taskbar
// overlays the screen instead of shrinking it.
struct PanelHomeDip {
    DisplayRectDip work;
    DisplayRectDip monitor;
};

bool MeasurePanelRect(PanelMeasurement& panel) {
    wux::FrameworkElement root = g_root;
    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!root || !hwnd) {
        return false;
    }

    float frameLeft = 0.0f;
    float frameTop = 0.0f;
    float frameRight = 0.0f;
    float frameBottom = 0.0f;
    bool measured = false;
    float bestArea = -1.0f;
    bool haveNonFilling = false;
    float nonFillingArea = -1.0f;
    float nfLeft = 0.0f, nfTop = 0.0f, nfRight = 0.0f, nfBottom = 0.0f;

    // The visible panel is the largest child that actually lies inside the
    // root. Taking the first visible child picked a wrapper that sits 12 DIP
    // above the root on a bottom taskbar, which put the reveal anchor outside
    // the screen.
    const float rootWidth = static_cast<float>(root.ActualWidth());
    const float rootHeight = static_cast<float>(root.ActualHeight());

    const int32_t childCount =
        wux::Media::VisualTreeHelper::GetChildrenCount(root);
    for (int32_t i = 0; i < childCount; ++i) {
        wux::FrameworkElement child =
            wux::Media::VisualTreeHelper::GetChild(root, i)
                .try_as<wux::FrameworkElement>();
        if (!child) {
            continue;
        }
        const float width = static_cast<float>(child.ActualWidth());
        const float height = static_cast<float>(child.ActualHeight());
        if (!(width > 0.0f) || !(height > 0.0f) ||
            child.Visibility() != wux::Visibility::Visible) {
            continue;
        }

        // The transform is relative to the window's XAML root; null asks for
        // that root. A scaling or offset transform stays in the mapping.
        wux::Media::GeneralTransform transform = child.TransformToVisual(nullptr);
        const winrt::Windows::Foundation::Point topLeft =
            transform.TransformPoint({0.0f, 0.0f});
        const float left = topLeft.X;
        const float top = topLeft.Y;
        const float right = transform.TransformPoint({width, 0.0f}).X;
        const float bottom = transform.TransformPoint({0.0f, height}).Y;

        const float overlapWidth =
            std::max(0.0f, std::min(right, rootWidth) - std::max(left, 0.0f));
        const float overlapHeight =
            std::max(0.0f, std::min(bottom, rootHeight) - std::max(top, 0.0f));
        const float area = overlapWidth * overlapHeight;
        if (!(area > 0.0f)) {
            continue;
        }

        // A child that fills the root is the hosting wrapper, not the panel, so
        // it is only a fallback when nothing smaller is visible.
        const float rootArea = rootWidth * rootHeight;
        if (rootArea <= 0.0f || area < 0.98f * rootArea) {
            if (area > nonFillingArea) {
                nonFillingArea = area;
                nfLeft = left;
                nfTop = top;
                nfRight = right;
                nfBottom = bottom;
                haveNonFilling = true;
            }
        }
        if (area > bestArea) {
            bestArea = area;
            frameLeft = left;
            frameTop = top;
            frameRight = right;
            frameBottom = bottom;
            measured = true;
        }
    }

    if (haveNonFilling) {
        frameLeft = nfLeft;
        frameTop = nfTop;
        frameRight = nfRight;
        frameBottom = nfBottom;
    }

    if (!measured) {
        return false;
    }

    // The frame is in XAML root coordinates; the origin turns it into a screen
    // rectangle for whichever side the taskbar is on.
    float originX = 0.0f;
    float originY = 0.0f;
    if (!XamlOriginDip(originX, originY)) {
        return false;
    }

    panel.left = originX + frameLeft;
    panel.top = originY + frameTop;
    panel.right = originX + frameRight;
    panel.bottom = originY + frameBottom;
    return true;
}

bool PanelHomeForWindow(PanelHomeDip& home) {
    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        return false;
    }

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return false;
    }

    const float scale = DpiScaleForWindow(hwnd);
    if (!(scale > 0.0f)) {
        return false;
    }

    home.work.left = info.rcWork.left / scale;
    home.work.top = info.rcWork.top / scale;
    home.work.right = info.rcWork.right / scale;
    home.work.bottom = info.rcWork.bottom / scale;
    home.monitor.left = info.rcMonitor.left / scale;
    home.monitor.top = info.rcMonitor.top / scale;
    home.monitor.right = info.rcMonitor.right / scale;
    home.monitor.bottom = info.rcMonitor.bottom / scale;
    return true;
}

// Whether a measured rect is the panel this layout shows. The rule lives here,
// once, and both of its tests say why their margin is what it is:
//
// - the rect has an extent and lies on the monitor. kPanelLayoutMarginDip rounds
//   a border that sits exactly on an edge; nothing larger is a layout fact, and
//   the 24 DIP of kScrollerEndSlopDip is a scroll-position allowance, not a
//   layout one.
// - the side against the taskbar lies between the work-area edge and the monitor
//   edge, because that band is the taskbar: the menu is attached to the
//   taskbar's side of the desktop, so that edge is not inside the work area and
//   not past the screen. The band's width is the taskbar's own inset, so the
//   rule needs no invented tolerance; on a taskbar that does not shrink the work
//   area (auto-hidden, or overlaying the screen) the band collapses onto the
//   monitor edge and the test becomes "on the screen edge".
//
// This is the test that separates the settled frame from the open storyboard's.
// Measured here (right taskbar, work 0,0,2784,1800, monitor 0,0,2880,1800 at
// 200%): the storyboard rect is 12,-12..1005,539 and the settled one is
// 411,163..1404,714. The storyboard's right edge is 387 DIP short of the
// taskbar band, so it is rejected whole; the settled right edge at 1404 sits
// 12 DIP under the work-area edge and 36 DIP inside the screen, which is where
// the shell attaches it.
bool PanelRectAccepted(const PanelMeasurement& panel,
                       const PanelHomeDip& home, int32_t axis) {
    if (!(panel.right > panel.left) || !(panel.bottom > panel.top)) {
        return false;
    }
    if (panel.left < home.monitor.left - kPanelLayoutMarginDip ||
        panel.right > home.monitor.right + kPanelLayoutMarginDip ||
        panel.top < home.monitor.top - kPanelLayoutMarginDip ||
        panel.bottom > home.monitor.bottom + kPanelLayoutMarginDip) {
        return false;
    }

    // The band between the work area and the monitor on the taskbar's side.
    const auto inTaskbarBand = [](float edge, float workEdge,
                                  float monitorEdge) {
        const float low = std::min(workEdge, monitorEdge);
        const float high = std::max(workEdge, monitorEdge);
        return edge >= low - kPanelAnchorMarginDip &&
               edge <= high + kPanelAnchorMarginDip;
    };
    switch (static_cast<DismissAxis>(axis)) {
        case DismissAxis::Left:
            return inTaskbarBand(panel.left, home.work.left, home.monitor.left);
        case DismissAxis::Top:
            return inTaskbarBand(panel.top, home.work.top, home.monitor.top);
        case DismissAxis::Right:
            return inTaskbarBand(panel.right, home.work.right,
                                 home.monitor.right);
        case DismissAxis::Bottom:
            return inTaskbarBand(panel.bottom, home.work.bottom,
                                 home.monitor.bottom);
    }
    return false;
}

// The one measurement and the one publish point of the geometry.
//
// `measurePanel` says this pass may re-derive the rect. It is false when a live
// gesture owns the edge (the edge must not move under the finger) and for a
// request that only refreshes the axis and the travel.
//
// The trail line speaks once per open — the request's open-time turn is what
// clears the flag — and again whenever the value moved by more than 1 DIP, so a
// stable layout does not repeat it and a resize cannot hide it.
//
// `expectedGeneration` is the epoch a queued request was recorded in; a
// measurement that runs after that epoch ended publishes nothing. 0 publishes
// whatever root is attached, which is what the synchronous Setup and SizeChanged
// paths want. `requestId` is the live measurement request on those queued paths,
// and 0 on the synchronous ones. The candidate is built first and validated at
// commit, so a turn that a newer request replaced measures but publishes nothing.
//
// The previous rect is carried over only when the whole layout identity matches:
// axis, travel, client origin, work area and monitor. The same travel on a moved
// taskbar or a different cross-axis origin is not the same panel; the carried
// rect is also tested against the current taskbar band before it is accepted.
// Dropping it leaves the snapshot with no panel, which reads as "not measured
// yet" and makes the follow use its fallback.
bool PublishPanelTrailDip(bool measurePanel, uint32_t expectedGeneration,
                          uint32_t requestId) {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return false;
    }
    if (!(g_dismissDistance > 0.0f)) {
        return false;  // no renderer, no travel to publish
    }

    const uint32_t generation = g_openGeneration.load(std::memory_order_relaxed);
    if (expectedGeneration != 0 && generation != expectedGeneration) {
        return false;
    }

    GestureGeometry previous;
    AcquireSRWLockShared(&g_geometryLock);
    previous = g_geometry;
    ReleaseSRWLockShared(&g_geometryLock);

    GestureGeometry candidate;
    candidate.generation = generation;
    candidate.layout.axis = g_dismissAxis;
    candidate.layout.distance = g_dismissDistance;

    float originX = 0.0f;
    float originY = 0.0f;
    const bool haveOrigin = XamlOriginDip(originX, originY);
    if (haveOrigin) {
        candidate.layout.originX = originX;
        candidate.layout.originY = originY;
    }

    // The panel's home on every axis, so it is read once for the validity test
    // and for the lines that report it.
    PanelHomeDip home;
    const bool haveHome = PanelHomeForWindow(home);
    if (haveHome) {
        candidate.layout.workLeft = home.work.left;
        candidate.layout.workTop = home.work.top;
        candidate.layout.workRight = home.work.right;
        candidate.layout.workBottom = home.work.bottom;
        candidate.layout.monitorLeft = home.monitor.left;
        candidate.layout.monitorTop = home.monitor.top;
        candidate.layout.monitorRight = home.monitor.right;
        candidate.layout.monitorBottom = home.monitor.bottom;
    }

    // A rect may be carried only inside one layout identity, and only when it
    // still sits on the taskbar band that identity describes.
    bool carriedPanel = false;
    PanelMeasurement carried;
    if (previous.panel && haveHome && haveOrigin &&
        SameLayoutIdentity(previous.layout, candidate.layout)) {
        carried.left = previous.left;
        carried.top = previous.top;
        carried.right = previous.right;
        carried.bottom = previous.bottom;
        if (PanelRectAccepted(carried, home, candidate.layout.axis)) {
            carriedPanel = true;
            candidate.panel = true;
            candidate.left = previous.left;
            candidate.top = previous.top;
            candidate.right = previous.right;
            candidate.bottom = previous.bottom;
        }
    }

    bool measuredPanel = false;
    if (measurePanel) {
        PanelMeasurement panel;
        bool havePanel = false;
        try {
            havePanel = MeasurePanelRect(panel);
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"Panel rect mapping failed: %08X", e.code());
        } catch (...) {
            // A tree mid-teardown must not break Setup or SizeChanged.
        }

        if (havePanel && haveHome) {
            if (PanelRectAccepted(panel, home, candidate.layout.axis)) {
                candidate.panel = true;
                candidate.left = panel.left;
                candidate.top = panel.top;
                candidate.right = panel.right;
                candidate.bottom = panel.bottom;
                measuredPanel = true;
            } else if (!g_panelTrailRejectedLogged) {
                // The rect was measured but is not a panel: the storyboard has
                // not placed it, or the layout is between sizes. Nothing from it
                // is published; the snapshot keeps its last whole value.
                g_panelTrailRejectedLogged = true;
                FileLog(L"reveal panelTrail rejected %.0f "
                        L"rect=%.0f,%.0f,%.0f,%.0f work=%.0f,%.0f,%.0f,%.0f "
                        L"mon=%.0f,%.0f,%.0f,%.0f",
                        TrailEdge(candidate.layout.axis, panel.left, panel.top,
                                  panel.right, panel.bottom),
                        panel.left, panel.top, panel.right, panel.bottom,
                        home.work.left, home.work.top, home.work.right,
                        home.work.bottom, home.monitor.left, home.monitor.top,
                        home.monitor.right, home.monitor.bottom);
            }
        }
    }

    // Commit checks, after the candidate exists: the request that asked for this
    // pass must still be live, its epoch must still be the open on screen, the
    // layout the candidate was built from must still be the one the writer holds,
    // and no live contact may own the panel edge. A contact that appeared while
    // the measurement ran drops the freshly measured rect; a carried rect from
    // the same identity stays only because the identity did not move.
    if (g_unloading.load(std::memory_order_relaxed)) {
        return false;
    }
    if (expectedGeneration != 0 &&
        g_openGeneration.load(std::memory_order_relaxed) != expectedGeneration) {
        return false;
    }
    if (requestId != 0 && !MeasureRequestLiveId(requestId)) {
        return false;
    }
    if (g_dismissAxis != candidate.layout.axis ||
        !SameLayoutDistance(g_dismissDistance, candidate.layout.distance)) {
        return false;
    }
    if (g_panelOwned.load(std::memory_order_relaxed)) {
        // The edge may not move under the finger. A freshly measured rect is
        // discarded; the rect this layout already carried stays, because the
        // identity did not move and it is the anchor the follow is using.
        if (measuredPanel) {
            measuredPanel = false;
            candidate.panel = carriedPanel;
            if (carriedPanel) {
                candidate.left = carried.left;
                candidate.top = carried.top;
                candidate.right = carried.right;
                candidate.bottom = carried.bottom;
            }
        }
    }

    // The snapshot goes out as one unit: the axis and the travel always, and the
    // panel only with a rect this pass or a carried one this layout sanctioned.
    PublishGestureGeometry(candidate);

    if (candidate.panel) {
        const float trail = GeometryTrailDip(candidate);
        if (measuredPanel) {
            // One trail line per open: the open-time turn speaks even when the
            // value did not move (the request cleared the flag), the settled turn
            // and every later pass only when it did, so a stable layout does not
            // repeat the line and a moved one cannot hide.
            const float previousTrail =
                previous.panel ? GeometryTrailDip(previous) : -1.0f;
            const bool moved = !previous.panel ||
                               std::fabs(trail - previousTrail) > 1.0f;
            if (moved || !g_panelTrailLogged) {
                g_panelTrailLogged = true;
                FileLog(L"reveal panelTrail=%.0f rect=%.0f,%.0f,%.0f,%.0f "
                        L"work=%.0f,%.0f,%.0f,%.0f gen=%u",
                        trail, candidate.left, candidate.top, candidate.right,
                        candidate.bottom, home.work.left, home.work.top,
                        home.work.right, home.work.bottom, generation);
            }
        }
        return true;
    }

    // No panel measured for this layout: the follow uses the root-width fallback,
    // which is the mapping this mod shipped with, and the hit test accepts a press
    // it cannot place.
    if (!g_panelTrailFallbackLogged) {
        g_panelTrailFallbackLogged = true;
        FileLog(L"reveal panelTrail=%.0f (fallback)",
                FallbackTrailDip(candidate.layout.axis,
                                 candidate.layout.distance));
    }
    return true;
}

// ---------------------------------------------------------------------------
// The settled measurement's layout watch
//
// The open re-measures the panel once, but that measurement can land while the
// shell's own open storyboard still has the frame out of place: measured on this
// machine (right taskbar), the storyboard rect is 12,-12..1005,539 where the
// settled one is 411,163..1404,714, and a bottom taskbar read a panel top of -12
// DIP where the settled panel starts at 200. The reveal follow maps `Progress`
// against that edge, so an early measurement anchors the panel by the width of
// the panel for that whole open.
//
// LayoutUpdated is the one signal that arrives when XAML has placed the panel, and
// it costs nothing while no watch is armed. The watch skips the first pass (it
// usually precedes the placement), runs the request's settled measurement on the
// second, then revokes its own token, so at most one watch ever exists and no
// handler is left behind. The close path and the renderer teardown revoke it too,
// because a menu that closed before the second pass must not measure anything
// later.
//
// The watch carries the request it belongs to, not a decision taken when it was
// armed: a callback that outlived a newer request measures nothing.
// ---------------------------------------------------------------------------
SettledMeasureRequest g_layoutRequest{};  // UI thread only

void StopSettledTrailWatch() {
    wux::FrameworkElement root = g_layoutRoot ? g_layoutRoot : g_root;
    if (g_layoutToken.value && root) {
        try {
            root.LayoutUpdated(g_layoutToken);
            g_uiRegistrationCount.fetch_sub(1, std::memory_order_relaxed);
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"LayoutUpdated revoke failed: %08X", e.code());
        } catch (...) {
            // A root mid-teardown cannot revoke; the request guard in the
            // handler keeps a stale callback from measuring the next open. The
            // registration count stays raised, so the unload reports the
            // residual instead of claiming the handler is gone.
        }
    }
    g_layoutToken = {};
    g_layoutRoot = nullptr;
    g_layoutTicksLeft = 0;
    g_layoutRequest = SettledMeasureRequest{};
}

// The close path's deferred cleanup names the epoch that ended. A watch armed by
// a newer open belongs to that open and is left alone.
void StopSettledTrailWatchIfGeneration(uint32_t maxGeneration) {
    if (g_layoutTicksLeft != 0 && g_layoutRequest.generation > maxGeneration) {
        return;
    }
    StopSettledTrailWatch();
}

void ArmLayoutWatch(const SettledMeasureRequest& request) {
    wux::FrameworkElement root = g_root;
    if (!root) {
        return;  // no root, no layout to wait for
    }

    // One watch per open: a work-area broadcast records a new request, and the
    // previous watch would otherwise measure the same settled layout twice.
    StopSettledTrailWatch();
    g_layoutTicksLeft = 2;
    g_layoutRequest = request;
    g_layoutRoot = root;

    try {
        g_layoutToken = root.LayoutUpdated([](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::Foundation::IInspectable const&) {
            try {
                if (g_layoutTicksLeft == 0) {
                    return;  // a callback that outlived its revoke
                }
                if (--g_layoutTicksLeft > 0) {
                    return;  // the first pass precedes the panel placement
                }
                const SettledMeasureRequest request = g_layoutRequest;
                RunGeometryMeasurement(request, true);
            } catch (winrt::hresult_error const& e) {
                Wh_Log(L"Settled trail failed: %08X", e.code());
            } catch (...) {
                // Never let an exception escape into XAML.
            }
            // Revoked after the measurement, and also when one threw, so the
            // handler never outlives the open it belongs to.
            StopSettledTrailWatch();
        });
        if (g_layoutToken.value) {
            g_uiRegistrationCount.fetch_add(1, std::memory_order_relaxed);
        }
    } catch (winrt::hresult_error const& e) {
        g_layoutToken = {};
        g_layoutTicksLeft = 0;
        g_layoutRequest = SettledMeasureRequest{};
        FileLog(L"layout watch failed err=%08X", static_cast<unsigned>(e.code()));
    } catch (...) {
        g_layoutToken = {};
        g_layoutTicksLeft = 0;
        g_layoutRequest = SettledMeasureRequest{};
        FileLog(L"layout watch failed err=unknown");
    }
}

// ---------------------------------------------------------------------------
// Scroller cache (the element a vertical dismiss competes with)
//
// Measured on this build, the menu contains three ScrollViewers, and only one of
// them can take a drag away from a dismiss:
//
// - the wrapper around StartUI.AllAppsGridListView (depth 16, 85,230-345,839)
//   reports ScrollableWidth 8380 and VerticalOffset 2252 whatever the content
//   does: its offsets are not the visual position, and its huge horizontal
//   extent marks it as a host rather than a list;
// - StartUI.TileListView's viewer (353,225-1005,839) and the rail's
//   (13,656-60,794) report ScrollableHeight 0, so they have nothing to scroll
//   and a drag on them belongs to the dismiss;
// - the ScrollViewer inside the AllApps ListView (depth 21, same rect as the
//   wrapper, ScrollableHeight 3664) is the one that moves with the list.
//
// Selection is therefore: the innermost ScrollViewer whose ScrollableWidth is
// zero and whose ScrollableHeight is not, with a non-empty rect. Depth beats
// area, so the live list wins over its wrapper at the same rect; at equal depth
// the smaller rect wins, because the more specific element is the one a press
// lands on. The rect is mapped to screen DIP through the same origin as the
// panel rect, so the input thread can test a press point against it directly.
// ---------------------------------------------------------------------------
struct ScrollerCandidate {
    wuxc::ScrollViewer viewer{nullptr};
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float area = 0.0f;
    uint32_t depth = 0;
    bool found = false;
};

// `complete` says the traversal reached every branch it was allowed to read. A
// throw, an unreadable child or the depth limit clears it: "no element found"
// then means "the search did not finish", never "nothing here scrolls".
void FindScrollableElement(const wux::UIElement& element, uint32_t depth,
                           float originX, float originY,
                           ScrollerCandidate& best, bool& complete) {
    if (!element) {
        return;
    }
    if (depth > kScrollerScanMaxDepth) {
        complete = false;
        return;
    }

    try {
        wuxc::ScrollViewer viewer = element.try_as<wuxc::ScrollViewer>();
        if (viewer) {
            const winrt::Windows::Foundation::Size size = element.ActualSize();
            const wux::Media::GeneralTransform transform =
                element.TransformToVisual(nullptr);
            const winrt::Windows::Foundation::Point topLeft =
                transform.TransformPoint({0.0f, 0.0f});
            const winrt::Windows::Foundation::Point bottomRight =
                transform.TransformPoint({size.Width, size.Height});

            const float left = originX + topLeft.X;
            const float top = originY + topLeft.Y;
            const float right = originX + bottomRight.X;
            const float bottom = originY + bottomRight.Y;
            const float area = (right - left) * (bottom - top);

            // A viewer that cannot scroll vertically cannot take the drag, and
            // a collapsed or inverted rect is not a place a finger can press.
            if (viewer.ScrollableWidth() <= kScrollerAxisSlopDip &&
                viewer.ScrollableHeight() > kScrollerAxisSlopDip &&
                right > left && bottom > top && area > 1.0f) {
                if (!best.found || depth > best.depth ||
                    (depth == best.depth && area < best.area)) {
                    best.viewer = viewer;
                    best.left = left;
                    best.top = top;
                    best.right = right;
                    best.bottom = bottom;
                    best.area = area;
                    best.depth = depth;
                    best.found = true;
                }
            }
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Scroller measure failed: %08X", e.code());
        complete = false;
    } catch (...) {
        // A branch mid-teardown ends this branch only, but the answer is no
        // longer whole.
        complete = false;
    }

    try {
        const int32_t count =
            wux::Media::VisualTreeHelper::GetChildrenCount(element);
        if (depth >= kScrollerScanMaxDepth && count > 0) {
            complete = false;  // children exist below the limit; not searched
            return;
        }
        for (int32_t i = 0; i < count; ++i) {
            FindScrollableElement(
                wux::Media::VisualTreeHelper::GetChild(element, i)
                    .try_as<wux::UIElement>(),
                depth + 1, originX, originY, best, complete);
        }
    } catch (...) {
        // A child being attached or removed ends this branch only.
        complete = false;
    }
}

// Drops only the handler, so a re-measure can subscribe to the same or another
// element. Runs on the UI thread.
void RevokeScrollerWatch() {
    wuxc::ScrollViewer viewer = g_scrollerViewer;
    if (g_scrollerToken.value && viewer) {
        try {
            viewer.ViewChanged(g_scrollerToken);
            g_uiRegistrationCount.fetch_sub(1, std::memory_order_relaxed);
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"ViewChanged revoke failed: %08X", e.code());
        } catch (...) {
            // The element is gone; its handler dies with it. The count stays
            // raised, so the unload reports the residual.
        }
    }
    g_scrollerToken = {};
    g_scrollerViewer = nullptr;
}

// Publishes the offsets of the cached element. ViewChanged fires on the UI
// thread and only while the list actually scrolls, so this is the whole cost of
// keeping the gate honest.
void RefreshScrollerOffsets() {
    wuxc::ScrollViewer viewer = g_scrollerViewer;
    if (!viewer) {
        return;
    }

    try {
        const float verticalOffset = viewer.VerticalOffset();
        const float scrollableHeight = viewer.ScrollableHeight();
        std::lock_guard lock(g_scrollerMutex);
        if (g_scroller.state == ScrollerState::Scrollable) {
            g_scroller.verticalOffset = verticalOffset;
            g_scroller.scrollableHeight = scrollableHeight;
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Scroller offset read failed: %08X", e.code());
    } catch (...) {
        // A closed menu must not break the handler.
    }
}

// True when a scroller result may still be committed: the request that asked
// for it is live, its open is still on screen, and no contact owns the panel.
bool ScrollerCommitAllowed(uint32_t expectedGeneration, uint32_t requestId) {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return false;
    }
    if (expectedGeneration != 0 &&
        g_openGeneration.load(std::memory_order_relaxed) != expectedGeneration) {
        return false;
    }
    if (requestId != 0 && !MeasureRequestLiveId(requestId)) {
        return false;
    }
    return !g_panelOwned.load(std::memory_order_relaxed);
}

// Binds the retry to the open that asked for it. The input thread only re-posts
// while that generation is still current and the menu is still on screen.
void ScheduleScrollerRetry(uint32_t generation) {
    if (generation == 0) {
        return;
    }
    g_scrollerRetryGeneration.store(generation, std::memory_order_relaxed);
    g_scrollerRetryAt.store(GetTickCount64() + kScrollerRetryMs,
                            std::memory_order_relaxed);
}

// UI thread only: one line per open naming why a settled scroller pass could not
// commit, so a retry loop is visible in the log instead of silent.
bool g_scrollerRetryLogged = false;

// A failed settled pass. A live contact on the panel is not a failure to retry at
// 10 Hz: the input thread posts one deferred pass when ownership ends, exactly as
// it does for a SizeChanged under the finger. A pass that lost its request or its
// open is left to the request that replaced it. Everything else is retried after
// `kScrollerRetryMs` while the same open is still on screen.
void NoteScrollerFailure(const wchar_t* reason, uint32_t generation,
                         bool retry) {
    if (!g_scrollerRetryLogged) {
        g_scrollerRetryLogged = true;
        FileLog(L"scroller retry why=%s gen=%u", reason, generation);
    }
    if (g_panelOwned.load(std::memory_order_relaxed)) {
        g_measureDeferred.store(true, std::memory_order_relaxed);
        return;
    }
    if (retry) {
        ScheduleScrollerRetry(generation);
    }
}

void ClearScrollerRetry() {
    g_scrollerRetryAt.store(0, std::memory_order_relaxed);
}

// Finds and caches the element for the open that is being measured. Runs on the
// UI thread from the open transition and again from the settled layout pass; the
// second call replaces the first measurement, which can still belong to the
// storyboard.
//
// Only the settled pass can arm the gate. The open-time pass records the element
// (so its ViewChanged handler is attached early and its rect is logged) but leaves
// the state Unknown: the rect belongs to a window the shell is still moving, and
// the old code armed the gate against it as soon as `valid` was set.
//
// A settled pass that completes and finds no scrollable element records Nothing,
// not Unknown. That is the state that lets a vertical press inside the panel
// dismiss the menu, which is what a panel with genuinely nothing scrollable has
// to do; Unknown is reserved for "no whole measurement speaks for this open".
// A search that threw, that hit the depth limit, or that ran while a contact
// owned the panel is not a negative answer: it stays Unknown and schedules the
// retry the input thread posts after `kScrollerRetryMs`.
//
// The result is built locally and committed once, after the request, the epoch,
// the client origin and panel ownership are re-checked: a turn a newer request
// replaced writes nothing.
void CacheScrollableElement(bool settled, uint32_t expectedGeneration,
                            uint32_t requestId) {
    wux::FrameworkElement root = g_root;
    if (!root) {
        return;
    }

    float originX = 0.0f;
    float originY = 0.0f;
    if (!XamlOriginDip(originX, originY)) {
        if (settled) {
            NoteScrollerFailure(L"origin", expectedGeneration, true);
        }
        return;
    }

    ScrollerCandidate best;
    bool complete = true;
    try {
        FindScrollableElement(root, 0, originX, originY, best, complete);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Scroller search failed: %08X", e.code());
        complete = false;
    } catch (...) {
        // An unreadable tree leaves the previous cache alone.
        complete = false;
    }

    // "Not found" is only a measurement when the traversal finished. A found
    // element is usable even if another branch was unreadable: the candidate was
    // read whole, and refusing it would leave the gate Unknown for certain.
    if (!best.found && !complete) {
        // Stay Unknown so the vertical gate keeps deferring and retry the pass.
        if (settled) {
            NoteScrollerFailure(L"incomplete", expectedGeneration, true);
        }
        return;
    }

    if (!best.found) {
        if (!ScrollerCommitAllowed(expectedGeneration, requestId)) {
            if (settled) {
                NoteScrollerFailure(L"not-allowed", expectedGeneration, false);
            }
            return;
        }
        {
            std::lock_guard lock(g_scrollerMutex);
            // Nothing scrolls here, and only the settled pass can make that a
            // fact: the open transition can see an empty tree while the shell is
            // still building the list, so it leaves the state Unknown.
            g_scroller = ScrollerCache{};
            if (settled) {
                g_scroller.state = ScrollerState::Nothing;
                g_scroller.generation = expectedGeneration;
            }
        }
        if (settled && !g_scrollerNoneLogged) {
            g_scrollerNoneLogged = true;
            FileLog(L"scroller none (measured, nothing scrolls)");
        }
        return;
    }

    float verticalOffset = 0.0f;
    float scrollableHeight = 0.0f;
    try {
        verticalOffset = best.viewer.VerticalOffset();
        scrollableHeight = best.viewer.ScrollableHeight();
    } catch (...) {
        // A viewer that cannot be read is no better than no viewer, and it does
        // not tell the gate anything: the state stays as it was.
        if (settled) {
            NoteScrollerFailure(L"offsets", expectedGeneration, true);
        }
        return;
    }

    // Commit validation: the client origin the rect was mapped through must
    // still be the current one, and the request, epoch and ownership must still
    // be the ones the search was made for.
    float commitOriginX = 0.0f;
    float commitOriginY = 0.0f;
    if (!XamlOriginDip(commitOriginX, commitOriginY) ||
        commitOriginX != originX || commitOriginY != originY ||
        !ScrollerCommitAllowed(expectedGeneration, requestId)) {
        if (settled) {
            NoteScrollerFailure(L"commit", expectedGeneration, true);
        }
        return;
    }

    // One handler per measurement: the element must not be observed twice.
    RevokeScrollerWatch();

    {
        std::lock_guard lock(g_scrollerMutex);
        g_scroller = ScrollerCache{};
        g_scroller.state = settled ? ScrollerState::Scrollable
                                   : ScrollerState::Unknown;
        g_scroller.generation = expectedGeneration;
        g_scroller.left = best.left;
        g_scroller.top = best.top;
        g_scroller.right = best.right;
        g_scroller.bottom = best.bottom;
        g_scroller.verticalOffset = verticalOffset;
        g_scroller.scrollableHeight = scrollableHeight;
    }

    // One line per open. The settled pass speaks only when it moved the rect the
    // first pass logged, so a storyboard that was still growing cannot hide the
    // final geometry.
    const bool moved = !g_scrollerLogged ||
                       std::fabs(best.left - g_scrollerLoggedRect[0]) > 1.0f ||
                       std::fabs(best.top - g_scrollerLoggedRect[1]) > 1.0f ||
                       std::fabs(best.right - g_scrollerLoggedRect[2]) > 1.0f ||
                       std::fabs(best.bottom - g_scrollerLoggedRect[3]) > 1.0f;
    if (moved) {
        g_scrollerLogged = true;
        g_scrollerLoggedRect[0] = best.left;
        g_scrollerLoggedRect[1] = best.top;
        g_scrollerLoggedRect[2] = best.right;
        g_scrollerLoggedRect[3] = best.bottom;
        FileLog(L"scroller rect=%.0f,%.0f,%.0f,%.0f vo=%.0f sh=%.0f", best.left,
                best.top, best.right, best.bottom, verticalOffset,
                scrollableHeight);
    }

    g_scrollerViewer = best.viewer;
    try {
        g_scrollerToken = best.viewer.ViewChanged(
            [](auto const&, auto const&) { RefreshScrollerOffsets(); });
        if (g_scrollerToken.value) {
            g_uiRegistrationCount.fetch_add(1, std::memory_order_relaxed);
        }
    } catch (winrt::hresult_error const& e) {
        g_scrollerToken = {};
        FileLog(L"scroller watch failed err=%08X", static_cast<unsigned>(e.code()));
    } catch (...) {
        g_scrollerToken = {};
        FileLog(L"scroller watch failed err=unknown");
    }
}

// Drops the cached element, its offsets and its handler. The close path and the
// renderer teardown call it, so a closed menu can never gate a press against the
// rect of the previous open, and the next open starts from Unknown.
void ReleaseScrollableElement() {
    RevokeScrollerWatch();
    g_scrollerLogged = false;
    g_scrollerNoneLogged = false;
    g_scrollerRetryLogged = false;
    g_scrollerLoggedRect[0] = 0.0f;
    g_scrollerLoggedRect[1] = 0.0f;
    g_scrollerLoggedRect[2] = 0.0f;
    g_scrollerLoggedRect[3] = 0.0f;
    std::lock_guard lock(g_scrollerMutex);
    g_scroller = ScrollerCache{};
}

// The close's deferred cleanup names the epoch that ended. A cache already
// measured for a newer open belongs to that open and is left alone.
void ReleaseScrollableElementIfGeneration(uint32_t maxGeneration) {
    {
        std::lock_guard lock(g_scrollerMutex);
        if (g_scroller.generation > maxGeneration) {
            return;
        }
    }
    ReleaseScrollableElement();
}

// The HWND is needed for the DWM cloaked query that decides whether the menu
// is open, and for the window bounds the input thread tests against.
void CaptureCoreWindowHandle() {
    g_coreHwnd.store(nullptr, std::memory_order_relaxed);

    try {
        wucore::CoreWindow coreWindow =
            wucore::CoreWindow::GetForCurrentThread();
        if (!coreWindow) {
            return;
        }

        void* windowAbi = winrt::get_abi(coreWindow);
        if (!windowAbi) {
            return;
        }

        void* interop = nullptr;
        if (FAILED(static_cast<IUnknown*>(windowAbi)->QueryInterface(
                kCoreWindowInteropIid, &interop)) ||
            !interop) {
            return;
        }

        HWND hwnd = nullptr;
        auto getWindowHandle = reinterpret_cast<GetCoreWindowHandleAbi>(
            (*reinterpret_cast<void***>(interop))[3]);
        if (getWindowHandle) {
            getWindowHandle(interop, &hwnd);
        }
        static_cast<IUnknown*>(interop)->Release();
        g_coreHwnd.store(hwnd, std::memory_order_relaxed);
    } catch (...) {
        // No HWND means no cloaked query, so no gesture starts.
    }
}

// Chooses the dismiss direction from the side of the work area the Start
// window is anchored to. Runs on the UI thread, from Setup and from the
// event-driven refresh. `logAxis` writes the full measurement line: the refresh
// passes false and speaks only when the axis actually moved, so an unchanged
// layout does not repeat four rect values on every open.
void UpdateDismissAxis(bool logAxis) {
    HWND hwnd = g_coreHwnd.load(std::memory_order_relaxed);
    if (!hwnd) {
        return;
    }

    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) {
        return;
    }

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return;
    }

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }

    const RECT& work = info.rcWork;
    const RECT& monitorRect = info.rcMonitor;

    // The Start window is full-screen, so its own rect cannot say which side
    // the taskbar is on: it touches every monitor edge and the first comparison
    // would always pick Left. The work area is the reliable signal, because the
    // taskbar is what shrinks it.
    DismissAxis axis = DismissAxis::Left;
    const long insetLeft = work.left - monitorRect.left;
    const long insetTop = work.top - monitorRect.top;
    const long insetRight = monitorRect.right - work.right;
    const long insetBottom = monitorRect.bottom - work.bottom;
    const long largest = std::max(std::max(insetLeft, insetTop),
                                  std::max(insetRight, insetBottom));
    if (largest > 4) {
        if (largest == insetLeft) {
            axis = DismissAxis::Left;
        } else if (largest == insetTop) {
            axis = DismissAxis::Top;
        } else if (largest == insetRight) {
            axis = DismissAxis::Right;
        } else {
            axis = DismissAxis::Bottom;
        }
    } else if (windowRect.left <= work.left + 4) {
        // No inset: the taskbar is auto-hidden or on another monitor. Fall back
        // to the old window-rect heuristic.
        if (windowRect.top <= work.top + 4) {
            axis = DismissAxis::Top;
        }
    }

    const int32_t previousAxis = g_dismissAxis;
    g_dismissAxis = static_cast<int32_t>(axis);
    if (logAxis || g_dismissAxis != previousAxis) {
        FileLog(L"dismiss axis=%d (0=left,1=top,2=right,3=bottom) "
                L"win=%d,%d,%d,%d work=%d,%d,%d,%d mon=%d,%d,%d,%d",
                g_dismissAxis, static_cast<int>(windowRect.left),
                static_cast<int>(windowRect.top),
                static_cast<int>(windowRect.right),
                static_cast<int>(windowRect.bottom), static_cast<int>(work.left),
                static_cast<int>(work.top), static_cast<int>(work.right),
                static_cast<int>(work.bottom), static_cast<int>(monitorRect.left),
                static_cast<int>(monitorRect.top),
                static_cast<int>(monitorRect.right),
                static_cast<int>(monitorRect.bottom));
    }

    // The snapshot is not published here: the caller publishes the whole
    // measurement once, after the travel it needs is computed.
}

// Refreshes the dismiss axis and the translation the composition expression
// bakes in. Runs on the UI thread when the menu opens and when the system
// announces a work-area or display change.
//
// `StartUI::App::PrepareRootElement` does not fire on every open: the XAML root
// is reused, so the axis, Distance, DirX and DirY captured by Setup can belong
// to a taskbar that has moved since, and the expression would keep sliding the
// panel the old way. The refresh rewrites all three composition inputs when the
// axis or the extent changed; an unchanged layout keeps the scalars the property
// set already holds and only records the check.
//
// It publishes nothing: the caller follows it with one PublishPanelTrailDip, so
// the scalars and the snapshot cannot disagree.
//
// A root that is not attached has no renderer to write: the refresh is a no-op
// then.
bool RefreshGestureGeometry() {
    wux::FrameworkElement root = g_root;
    if (!root) {
        return false;
    }

    {
        std::lock_guard lock(g_compositionMutex);
        if (!g_state) {
            return false;
        }
    }

    const int32_t previousAxis = g_dismissAxis;
    const float previousDistance = g_dismissDistance;
    UpdateDismissAxis(false);

    float dirX = 0.0f;
    float dirY = 0.0f;
    float distance = 0.0f;
    AxisTranslation(g_dismissAxis, static_cast<float>(root.ActualWidth()),
                    static_cast<float>(root.ActualHeight()), dirX, dirY,
                    distance);
    if (!(distance > 0.0f)) {
        // The root has not been laid out yet. Restore the axis so a failed read
        // does not leave the hit test on a side the expression never took.
        g_dismissAxis = previousAxis;
        return false;
    }

    const bool changed = g_dismissAxis != previousAxis ||
                         !SameLayoutDistance(distance, previousDistance);

    {
        std::lock_guard lock(g_compositionMutex);
        if (!g_state) {
            g_dismissAxis = previousAxis;
            return false;
        }
        if (changed) {
            try {
                g_state.InsertScalar(L"Distance", distance);
                g_state.InsertScalar(L"DirX", dirX);
                g_state.InsertScalar(L"DirY", dirY);
            } catch (winrt::hresult_error const& e) {
                FileLog(L"gesture geometry write failed err=%08X",
                        static_cast<unsigned>(e.code()));
                // The expression still slides by the old axis and travel, so the
                // published snapshot must keep describing them. A half-applied
                // write is rolled back as far as the scalar the geometry owns.
                g_dismissAxis = previousAxis;
                return false;
            } catch (...) {
                FileLog(L"gesture geometry write failed err=unknown");
                g_dismissAxis = previousAxis;
                return false;
            }
        }
    }

    // Only now, with the composition carrying the new scalars, is the axis and
    // travel committed as the renderer geometry. A failed write above leaves the
    // old values in place, so no reader can follow a layout the expression does
    // not use. A changed identity also raises the layout token here, before any
    // publish: until a snapshot carries it, a cached copy from the old layout is
    // rejected for a new gesture instead of starting from stale travel.
    g_dismissDistance = distance;
    if (changed) {
        g_layoutVersion.fetch_add(1, std::memory_order_acq_rel);
    }

    // The published snapshot has to match the scalars the expression reads, so it
    // is written by the caller right after this, never on its own.
    FileLog(changed ? L"gesture geometry updated axis=%d dist=%.0f"
                    : L"gesture geometry axis=%d dist=%.0f (unchanged)",
            g_dismissAxis, distance);
    return true;
}

// Runs on the UI thread, after the root element is loaded.
bool Setup(wux::FrameworkElement const& root) {
    if (!root) {
        return false;
    }

    {
        std::lock_guard lock(g_compositionMutex);
        if (g_state) {
            return true;
        }
    }

    g_uiThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
    g_root = root;
    g_panelTrailLogged = false;  // one reveal trail line per open

    CaptureCoreWindowHandle();
    if (!g_coreHwnd.load(std::memory_order_relaxed)) {
        Wh_Log(L"No CoreWindow HWND; gestures stay disabled");
    }
    UpdateDismissAxis(true);

    wuxh::ElementCompositionPreview::SetIsTranslationEnabled(root, true);

    wuc::Visual visual = wuxh::ElementCompositionPreview::GetElementVisual(root);
    wuc::Compositor compositor = visual.Compositor();

    const float width = static_cast<float>(root.ActualWidth());
    const float height = static_cast<float>(root.ActualHeight());

    // The first geometry of this renderer. The open transition and every
    // work-area change after it refresh these three values in place.
    float dirX = 0.0f;
    float dirY = 0.0f;
    float distance = 0.0f;
    AxisTranslation(g_dismissAxis, width, height, dirX, dirY, distance);

    wuc::CompositionPropertySet state = compositor.CreatePropertySet();
    state.InsertScalar(L"Progress", 1.0f);  // Normal Start is visible.
    state.InsertScalar(L"Distance", distance);
    state.InsertScalar(L"DirX", dirX);
    state.InsertScalar(L"DirY", dirY);

    // The translation property must exist before the expression is started.
    visual.Properties().InsertVector3(L"Translation",
                                      float3{0.0f, 0.0f, 0.0f});

    wuc::ExpressionAnimation translation = compositor.CreateExpressionAnimation(
        L"Vector3((1 - state.Progress) * state.DirX * state.Distance, "
        L"(1 - state.Progress) * state.DirY * state.Distance, 0)");
    translation.SetReferenceParameter(L"state", state);
    visual.StartAnimation(L"Translation", translation);

    {
        // Captured before the lock: the reference-counted copy is made outside
        // the section the compositor callback also takes.
        wucore::CoreDispatcher dispatcher = root.Dispatcher();
        std::lock_guard lock(g_compositionMutex);
        g_compositor = compositor;
        g_visual = visual;
        g_state = state;
        g_translationExpression = translation;
        // Published under the mutex every reader copies it from: the input
        // thread posts through CopyDispatcher.
        g_dispatcher = dispatcher;
    }

    // The property set now carries this travel, so the renderer geometry may be
    // committed to it. A throw above leaves the old value and no renderer.
    g_dismissDistance = distance;

    // A resize changes the extent the expression slides by, and it may be the
    // first real layout after Setup. The refresh owns the axis check as well, so
    // a root that is laid out again after a taskbar move is corrected here too.
    g_sizeRoot = root;
    g_sizeToken = root.SizeChanged(
        [](winrt::Windows::Foundation::IInspectable const&,
           wux::SizeChangedEventArgs const&) {
            try {
                // A contact owns the panel: the edge must not move under the
                // finger, so defer the whole refresh instead of re-anchoring it.
                // The input thread posts the deferred settled pass when the
                // contact ends.
                if (g_panelOwned.load(std::memory_order_relaxed)) {
                    g_measureDeferred.store(true, std::memory_order_relaxed);
                    return;
                }
                if (!RefreshGestureGeometry()) {
                    return;
                }
                // The root was resized by the panel layout, so the rect is
                // remeasured with the new extent in force. The pass is synchronous
                // with the layout event, so it needs no epoch of its own: the
                // snapshot it writes carries the current one.
                PublishPanelTrailDip(true, 0, 0);
            } catch (winrt::hresult_error const& e) {
                Wh_Log(L"SizeChanged update failed: %08X", e.code());
            } catch (...) {
                // Never let an exception escape into XAML.
            }
        });
    if (g_sizeToken.value) {
        g_uiRegistrationCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Everything the input thread reads is published before the root opens. This
    // is the initial anchor: the frame can still be off its settled position
    // here, so the open transition re-measures it, and the measurement below is
    // rejected as "not the panel" when the storyboard has not placed it.
    PublishPanelTrailDip(true, 0, 0);
    g_rendererGeneration.fetch_add(1, std::memory_order_release);

    const winrt::hstring rootClassName = winrt::get_class_name(root);
    FileLog(L"attached root=%s size=%.0fx%.0f", rootClassName.c_str(), width,
            height);

    Wh_Log(L"Dismiss input armed; composition translation enabled "
           L"root=%s (distance=%.1f)",
           rootClassName.c_str(), distance);
    return true;
}

// ---------------------------------------------------------------------------
// Hook: StartUI::App::PrepareRootElement
// ---------------------------------------------------------------------------

using PrepareRootElement_t = void* (*)(void*, void*, bool);
PrepareRootElement_t PrepareRootElement_Original = nullptr;

void* PrepareRootElement_Hook(void* frame, void* perFrameData, bool flag) {
    void* result = PrepareRootElement_Original
                       ? PrepareRootElement_Original(frame, perFrameData, flag)
                       : nullptr;
    if (!result || g_unloading.load(std::memory_order_relaxed)) {
        return result;
    }

    try {
        wux::UIElement element{nullptr};
        winrt::copy_from_abi(element, result);  // Adds a reference.

        wux::FrameworkElement root = element.try_as<wux::FrameworkElement>();
        if (!root) {
            Wh_Log(L"PrepareRootElement returned a non-FrameworkElement root");
            return result;
        }

        Wh_Log(L"PrepareRootElement root class=%s name=%s",
               winrt::get_class_name(root).c_str(), root.Name().c_str());

        // A new open: drop the previous root's property set, animation and
        // gesture state before installing the fresh renderer.
        ResetRenderer();

        // The dispatcher is UI-thread state and the only way to revoke a
        // delegate registered below if Setup never runs. Capture it here, under
        // the same mutex every reader takes, before the first registration.
        try {
            wucore::CoreDispatcher dispatcher = root.Dispatcher();
            if (dispatcher) {
                std::lock_guard lock(g_compositionMutex);
                g_dispatcher = dispatcher;
            }
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"Dispatcher capture failed: %08X", e.code());
        } catch (...) {
            Wh_Log(L"Dispatcher capture failed with an unknown exception");
        }

        bool alreadyLoaded = false;
        try {
            alreadyLoaded = root.IsLoaded();
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"IsLoaded check failed: %08X", e.code());
        }

        if (alreadyLoaded) {
            Setup(root);
        } else {
            const winrt::hstring rootClassName = winrt::get_class_name(root);
            FileLog(L"root pending load class=%s", rootClassName.c_str());

            const uint32_t generation =
                g_setupGeneration.load(std::memory_order_relaxed);
            auto weak = winrt::make_weak(root);
            g_loadedRoot = root;
            g_loadedToken = root.Loaded(
                [weak, generation](
                    winrt::Windows::Foundation::IInspectable const& sender,
                    wux::RoutedEventArgs const&) {
                    try {
                        if (generation != g_setupGeneration.load(
                                              std::memory_order_relaxed)) {
                            return;
                        }
                        wux::FrameworkElement current =
                            sender.try_as<wux::FrameworkElement>();
                        if (!current) {
                            current = weak.get();
                        }
                        if (current) {
                            Setup(current);
                        }
                    } catch (winrt::hresult_error const& e) {
                        Wh_Log(L"Loaded setup failed: %08X %s", e.code(),
                               e.message().c_str());
                    } catch (...) {
                        Wh_Log(L"Loaded setup failed with an unknown exception");
                    }
                });
            if (g_loadedToken.value) {
                g_uiRegistrationCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"PrepareRootElement hook failed: %08X %s", e.code(),
               e.message().c_str());
    } catch (...) {
        Wh_Log(L"PrepareRootElement hook failed with an unknown exception");
    }

    return result;
}

// ---------------------------------------------------------------------------
// Hook: StartUI::SplitViewFrame::OnGlobalAnimationRequested
//
// The shell raises this request to start the XAML open and close storyboards.
// Dropping the open request leaves the pane at its final layout position, so
// the composition `Progress` expression is the only remaining motion. The
// close request (type 1) always reaches the original.
// ---------------------------------------------------------------------------

using OnGlobalAnimationRequested_t = void (*)(void*, void*, void*);
OnGlobalAnimationRequested_t OnGlobalAnimationRequested_Original = nullptr;

// The args object exposes the three ints through vtable slots 6, 7 and 8; the
// wrappers at the RVAs above dispatch exactly those slots and return the value
// in eax. Null until the layout check in InstallAnimHook succeeds.
using AnimArgsIntGetter_t = int (*)(void*);
AnimArgsIntGetter_t g_animTypeGetter = nullptr;
AnimArgsIntGetter_t g_animEdgeGetter = nullptr;
AnimArgsIntGetter_t g_animTransitionGetter = nullptr;

void OnGlobalAnimationRequested_Hook(void* self, void* sender, void* args) {
    int type = -1;
    int edge = -1;
    int transition = -1;
    if (args && g_animTypeGetter && g_animEdgeGetter &&
        g_animTransitionGetter) {
        type = g_animTypeGetter(args);
        edge = g_animEdgeGetter(args);
        transition = g_animTransitionGetter(args);
    }

    // Type 0 is the open request. Any other value, and any unreadable type,
    // keeps the shell's own behaviour.
    const bool suppress =
        type == 0 && g_suppressOpenAnim.load(std::memory_order_relaxed);

    if (g_animLogCount.fetch_add(1, std::memory_order_relaxed) < 4) {
        FileLog(L"startui anim type=%d edge=%d trans=%d suppress=%d", type,
                edge, transition, suppress ? 1 : 0);
    }

    if (suppress) {
        return;
    }

    if (OnGlobalAnimationRequested_Original) {
        OnGlobalAnimationRequested_Original(self, sender, args);
    }
}

// ---------------------------------------------------------------------------
// Hook installation
// ---------------------------------------------------------------------------

std::atomic<bool> g_startUIHooked{false};
std::atomic<bool> g_rootHookRefused{false};
std::atomic<bool> g_animHookAttempted{false};
std::atomic<bool> g_afterInit{false};
std::atomic<bool> g_stopPolling{false};
HANDLE g_pollThread = nullptr;

// The file version of StartUI_.dll, read through version.dll. `known` is false
// when the resource or the export is missing, which is a refusal like any other.
struct StartUIBuild {
    WORD major = 0;
    WORD minor = 0;
    WORD build = 0;
    WORD revision = 0;
    bool known = false;
};

StartUIBuild ReadStartUIBuild(HMODULE module) {
    StartUIBuild result;

    const GetFileVersionInfoSizeW_t sizeFn =
        g_getFileVersionInfoSize.load(std::memory_order_relaxed);
    const GetFileVersionInfoW_t infoFn =
        g_getFileVersionInfo.load(std::memory_order_relaxed);
    const VerQueryValueW_t queryFn =
        g_verQueryValue.load(std::memory_order_relaxed);
    if (!sizeFn || !infoFn || !queryFn) {
        return result;
    }

    WCHAR path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        return result;
    }

    DWORD ignored = 0;
    const DWORD bytes = sizeFn(path, &ignored);
    if (bytes == 0 || bytes > 64 * 1024) {
        return result;
    }

    std::vector<BYTE> data(bytes);
    if (!infoFn(path, 0, bytes, data.data())) {
        return result;
    }

    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixedBytes = 0;
    if (!queryFn(data.data(), L"\\", reinterpret_cast<LPVOID*>(&fixed),
                 &fixedBytes) ||
        !fixed || fixedBytes < sizeof(VS_FIXEDFILEINFO)) {
        return result;
    }

    result.major = HIWORD(fixed->dwFileVersionMS);
    result.minor = LOWORD(fixed->dwFileVersionMS);
    result.build = HIWORD(fixed->dwFileVersionLS);
    result.revision = LOWORD(fixed->dwFileVersionLS);
    result.known = true;
    return result;
}

bool StartUIBuildMatches(const StartUIBuild& build) {
    return build.known && build.major == kStartUIBuildMajor &&
           build.minor == kStartUIBuildMinor &&
           build.build == kStartUIBuildNumber &&
           build.revision == kStartUIBuildRevision;
}

// Confirms that the target is the function the RVA was derived from: the exact
// prologue and the first call's resolved target. The caller checks the file
// version first, so this only ever probes the reference build's own image.
bool PrepareRootElementPrologueMatches(HMODULE module) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const uintptr_t function = base + (kPrepareRootElementVa - kPreferredBase);
    const auto* code = reinterpret_cast<const uint8_t*>(function);

    for (size_t i = 0; i < sizeof(kPreparePrologue); ++i) {
        if (code[i] != kPreparePrologue[i]) {
            return false;
        }
    }

    if (code[kPrepareCallOffset] != 0xE8) {
        return false;
    }

    int32_t displacement = 0;
    std::memcpy(&displacement, code + kPrepareCallOffset + 1,
                sizeof(displacement));
    return function + kPrepareCallOffset + 5 + displacement ==
           base + (kPrepareFirstCalleeVa - kPreferredBase);
}

void InstallStartUIHooks(HMODULE module) {
    if (!module || g_startUIHooked.exchange(true)) {
        return;
    }
    if (g_disabled.load(std::memory_order_relaxed)) {
        g_startUIHooked.store(false);
        return;
    }

    // The RVA is a build constant, not a pattern: refuse any other StartUI_ and
    // say which build refused. One line per process; the poll thread stops
    // retrying because a different build cannot become the right one.
    const StartUIBuild build = ReadStartUIBuild(module);
    if (!StartUIBuildMatches(build)) {
        g_startUIHooked.store(false);
        if (!g_rootHookRefused.exchange(true)) {
            if (build.known) {
                FileLog(L"startui root hook skipped build=%u.%u.%u.%u "
                        L"expected=10.0.22621.3733",
                        static_cast<unsigned>(build.major),
                        static_cast<unsigned>(build.minor),
                        static_cast<unsigned>(build.build),
                        static_cast<unsigned>(build.revision));
                Wh_Log(L"StartUI_.dll %u.%u.%u.%u is not the 10.0.22621.3733 "
                       L"build this RVA was derived from; the root hook is "
                       L"refused",
                       static_cast<unsigned>(build.major),
                       static_cast<unsigned>(build.minor),
                       static_cast<unsigned>(build.build),
                       static_cast<unsigned>(build.revision));
            } else {
                FileLog(L"startui root hook skipped build=? (no version "
                        L"resource)");
                Wh_Log(L"StartUI_.dll has no readable version; the root hook "
                       L"is refused");
            }
        }
        return;
    }

    if (!PrepareRootElementPrologueMatches(module)) {
        g_startUIHooked.store(false);
        if (!g_rootHookRefused.exchange(true)) {
            FileLog(L"startui root hook skipped build=10.0.22621.3733 "
                    L"prologue mismatch");
            Wh_Log(L"StartUI_.dll 10.0.22621.3733 does not carry the "
                   L"PrepareRootElement prologue this hook was written for; "
                   L"the root hook is refused");
        }
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    void* target = reinterpret_cast<void*>(
        base + (kPrepareRootElementVa - kPreferredBase));

    if (!Wh_SetFunctionHook(target,
                            reinterpret_cast<void*>(PrepareRootElement_Hook),
                            reinterpret_cast<void**>(
                                &PrepareRootElement_Original))) {
        Wh_Log(L"Failed to hook StartUI::App::PrepareRootElement at %p",
               target);
        g_startUIHooked.store(false);
        return;
    }

    Wh_Log(L"Hooked StartUI::App::PrepareRootElement at %p", target);
}

// Confirms that the target is the function the hook was written for: the exact
// prologue, the three int getters called in order, and the argument registers
// those calls imply. A different StartUI_ build fails here and the hook is left
// out instead of being installed on a guessed ABI.
bool AnimHookAbiMatches(uintptr_t base) {
    const auto* code = reinterpret_cast<const uint8_t*>(
        base + (kOnGlobalAnimationRequestedVa - kPreferredBase));
    constexpr size_t kCookieDispBegin = 0x12;
    constexpr size_t kCookieDispEnd = 0x16;

    for (size_t i = 0; i < sizeof(kAnimPrologue); ++i) {
        if (i >= kCookieDispBegin && i < kCookieDispEnd) {
            continue;
        }
        if (code[i] != kAnimPrologue[i]) {
            return false;
        }
    }

    const uintptr_t getters[] = {
        base + (kAnimTypeGetterVa - kPreferredBase),
        base + (kAnimEdgeGetterVa - kPreferredBase),
        base + (kAnimTransitionGetterVa - kPreferredBase),
    };
    // The three calls are separated by their result stores, so they are not
    // adjacent: call type, store; call edge, store; call transition, store.
    constexpr size_t kGetterCallOffsets[] = {0x27, 0x33, 0x3F};
    const uintptr_t function = reinterpret_cast<uintptr_t>(code);
    for (size_t i = 0; i < ARRAYSIZE(getters); ++i) {
        const size_t offset = kGetterCallOffsets[i];
        if (code[offset] != 0xE8) {
            return false;
        }
        int32_t displacement = 0;
        std::memcpy(&displacement, code + offset + 1, sizeof(displacement));
        if (function + offset + 5 + displacement != getters[i]) {
            return false;
        }
    }

    return true;
}

bool InstallAnimHook(HMODULE module) {
    if (!module || g_animHookAttempted.exchange(true)) {
        return false;
    }
    if (g_disabled.load(std::memory_order_relaxed)) {
        g_animHookAttempted.store(false);
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    void* target = reinterpret_cast<void*>(
        base + (kOnGlobalAnimationRequestedVa - kPreferredBase));

    if (!AnimHookAbiMatches(base)) {
        Wh_Log(L"StartUI_.dll does not match the expected "
               L"OnGlobalAnimationRequested layout");
        FileLog(L"startui anim hook skipped");
        return false;
    }

    const AnimArgsIntGetter_t typeGetter = reinterpret_cast<AnimArgsIntGetter_t>(
        base + (kAnimTypeGetterVa - kPreferredBase));
    const AnimArgsIntGetter_t edgeGetter = reinterpret_cast<AnimArgsIntGetter_t>(
        base + (kAnimEdgeGetterVa - kPreferredBase));
    const AnimArgsIntGetter_t transitionGetter =
        reinterpret_cast<AnimArgsIntGetter_t>(
            base + (kAnimTransitionGetterVa - kPreferredBase));

    if (!Wh_SetFunctionHook(
            target, reinterpret_cast<void*>(OnGlobalAnimationRequested_Hook),
            reinterpret_cast<void**>(&OnGlobalAnimationRequested_Original))) {
        Wh_Log(L"Failed to hook "
               L"StartUI::SplitViewFrame::OnGlobalAnimationRequested at %p",
               target);
        FileLog(L"startui anim hook skipped");
        return false;
    }

    g_animTypeGetter = typeGetter;
    g_animEdgeGetter = edgeGetter;
    g_animTransitionGetter = transitionGetter;

    Wh_Log(L"Hooked StartUI::SplitViewFrame::OnGlobalAnimationRequested at %p",
           target);
    FileLog(L"startui anim hooked");
    return true;
}

void TryInstallFromLoadedModules(bool apply) {
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }

    bool installed = false;
    HMODULE startUI = GetModuleHandleW(L"StartUI_.dll");
    if (startUI) {
        if (!g_startUIHooked.load(std::memory_order_relaxed) &&
            !g_rootHookRefused.load(std::memory_order_relaxed)) {
            InstallStartUIHooks(startUI);
            installed = true;
        }
        if (!g_animHookAttempted.load(std::memory_order_relaxed)) {
            if (InstallAnimHook(startUI)) {
                installed = true;
            }
        }
    }

    if (installed && apply && g_afterInit.load(std::memory_order_relaxed)) {
        Wh_ApplyHookOperations();
    }
}

// ---------------------------------------------------------------------------
// Late load of StartUI_.dll
// ---------------------------------------------------------------------------

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original = nullptr;

HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR fileName, HANDLE file,
                                   DWORD flags) {
    HMODULE module = LoadLibraryExW_Original
                         ? LoadLibraryExW_Original(fileName, file, flags)
                         : nullptr;

    if (module && !g_startUIHooked.load(std::memory_order_relaxed) &&
        !g_rootHookRefused.load(std::memory_order_relaxed)) {
        if (module == GetModuleHandleW(L"StartUI_.dll")) {
            TryInstallFromLoadedModules(true);
        }
    }

    return module;
}

DWORD WINAPI PollModulesThread(LPVOID) {
    // The wait is the stop signal itself, so a disable is observed at once
    // instead of after the current sleep. 2000 x 10 ms is the same 20 s budget
    // the old 400 x 50 ms loop had.
    for (int i = 0; i < 2000 && !g_stopPolling.load(std::memory_order_relaxed);
         i++) {
        TryInstallFromLoadedModules(true);
        if (g_startUIHooked.load(std::memory_order_relaxed) ||
            g_rootHookRefused.load(std::memory_order_relaxed)) {
            break;
        }
        if (g_hidStopEvent) {
            WaitForSingleObject(g_hidStopEvent, 10);
        } else {
            Sleep(10);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------

// Runs on the UI thread. Each registration is revoked on the object it was made
// on, in its own try block: a throw on one must not lose the others.
void RevokeHandlers() {
    // The settled-trail watch and the cached scroller belong to this root, so
    // both are dropped before its reference goes away: their handlers hold XAML
    // objects and must never run against the next open.
    StopSettledTrailWatch();
    ReleaseScrollableElement();

    // A Setup that never finished leaves g_root null while the Loaded handler is
    // already attached, so each root keeps its own reference until its token is
    // revoked.
    wux::FrameworkElement root = g_root;
    wux::FrameworkElement sizeRoot = g_sizeRoot ? g_sizeRoot : root;
    wux::FrameworkElement loadedRoot = g_loadedRoot ? g_loadedRoot : root;

    if (g_sizeToken.value && sizeRoot) {
        try {
            sizeRoot.SizeChanged(g_sizeToken);
            g_uiRegistrationCount.fetch_sub(1, std::memory_order_relaxed);
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"Failed to revoke SizeChanged: %08X %s", e.code(),
                   e.message().c_str());
        } catch (...) {
            Wh_Log(L"Failed to revoke SizeChanged with an unknown exception");
        }
        g_sizeToken = {};
    }
    if (g_loadedToken.value && loadedRoot) {
        try {
            loadedRoot.Loaded(g_loadedToken);
            g_uiRegistrationCount.fetch_sub(1, std::memory_order_relaxed);
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"Failed to revoke Loaded: %08X %s", e.code(),
                   e.message().c_str());
        } catch (...) {
            Wh_Log(L"Failed to revoke Loaded with an unknown exception");
        }
        g_loadedToken = {};
    }

    g_sizeRoot = nullptr;
    g_loadedRoot = nullptr;
    // Release the XAML reference on the UI thread.
    g_root = nullptr;
}

// How long the unload waits for its threads and its UI turn before it stops
// freeing anything. The threads observe the stop signal within one short wait;
// 10 s therefore means a stuck thread, not a slow one.
constexpr DWORD kUnloadThreadWaitMs = 10000;
constexpr DWORD kUnloadUiRevokeWaitMs = 10000;
constexpr DWORD kUnloadCallbackDrainMs = 2000;

// Waits until no dismiss completion is inside module code. A callback that the
// compositor already dispatched cannot be recalled by revoking its token; this
// is the proof that it has finished before the composition objects go away.
bool WaitForDismissCallbackDrain(DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (g_dismissCallbackDepth.load(std::memory_order_acquire) != 0) {
        if (GetTickCount64() >= deadline) {
            return false;
        }
        Sleep(1);
    }
    return true;
}

bool RevokeHandlersOnUiThread() {
    if (g_uiRegistrationCount.load(std::memory_order_acquire) == 0) {
        return true;  // nothing was ever registered
    }

    const DWORD uiThread = g_uiThreadId.load(std::memory_order_relaxed);
    if (uiThread != 0 && uiThread == GetCurrentThreadId()) {
        RevokeHandlers();
        return g_uiRegistrationCount.load(std::memory_order_acquire) == 0;
    }

    wucore::CoreDispatcher dispatcher = CopyDispatcher();
    if (!dispatcher) {
        // A registration exists but Setup never got as far as capturing the
        // dispatcher. There is no UI thread to post to, and touching XAML from
        // this one would be worse.
        Wh_Log(L"No CoreDispatcher captured; XAML handlers left attached");
        return false;
    }

    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!done) {
        return false;
    }

    try {
        dispatcher.RunAsync(wucore::CoreDispatcherPriority::Normal,
                            [done]() {
                                try {
                                    RevokeHandlers();
                                } catch (...) {
                                    // Never let an exception escape the
                                    // dispatcher callback.
                                }
                                SetEvent(done);
                            });
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Dispatcher.RunAsync failed: %08X %s", e.code(),
               e.message().c_str());
        CloseHandle(done);
        return false;
    } catch (...) {
        CloseHandle(done);
        return false;
    }

    if (WaitForSingleObject(done, kUnloadUiRevokeWaitMs) == WAIT_OBJECT_0) {
        CloseHandle(done);
        return g_uiRegistrationCount.load(std::memory_order_acquire) == 0;
    }

    // The queued callback may still signal the event; leaking one handle is
    // safer than closing a handle it will use later. The caller must not free
    // what that callback can still touch.
    Wh_Log(L"Timed out waiting for the UI thread to revoke XAML handlers");
    return false;
}

void ResetComposition() {
    // The batch's handler is a delegate like any other: remove it before the
    // batch and the state it closes over go away. Takes the mutex itself, so it
    // runs before the lock below.
    RevokeDismissBatch();

    std::lock_guard lock(g_compositionMutex);

    // Invalidate a pending dismiss completion before stopping its animation,
    // so an old batch cannot touch a close in a newly opened menu. The recorded
    // settle curve belongs to the root that is going away.
    InvalidateDismissBatchLocked();
    g_settleFrames = 0;
    g_settleDurationMs = 0;

    try {
        if (g_state) {
            g_state.StopAnimation(L"Progress");
            g_coasting.store(false, std::memory_order_relaxed);
            g_state.InsertScalar(L"Progress", kProgressRest);
            g_progressNow.store(kProgressRest, std::memory_order_relaxed);
        }
        if (g_visual) {
            g_visual.StopAnimation(L"Translation");
            g_visual.Properties().InsertVector3(
                L"Translation", float3{0.0f, 0.0f, 0.0f});
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Composition cleanup failed: %08X", e.code());
    } catch (...) {
    }

    g_dismissBatch = nullptr;
    g_dismissBatchToken = {};
    g_translationExpression = nullptr;
    g_compositor = nullptr;
    g_state = nullptr;
    g_visual = nullptr;
    // The renderer is gone: every gate that asks whether gestures may start
    // reads this, so a gesture cannot run against a property set that no longer
    // exists.
    g_rendererGeneration.store(0, std::memory_order_release);
    // The old root's dispatcher dies with it; PrepareRootElement captures the
    // next root's before it registers anything on it.
    g_dispatcher = nullptr;

    // A root that is going away owns no geometry. The snapshot is emptied whole
    // and the epoch moves, so a request recorded for this root can never publish
    // into the next one. The measure mutex nests inside this one here, which is
    // the only place the two meet.
    CancelMeasureRequest();
    ClearScrollerRetry();
    g_dismissDistance = 0.0f;
    g_openGeneration.fetch_add(1, std::memory_order_relaxed);
    GestureGeometry empty;
    empty.generation = g_openGeneration.load(std::memory_order_relaxed);
    empty.layout.axis = g_dismissAxis;
    PublishGestureGeometry(empty);

    // Invalidate a stale Loaded handler from the previous root.
    g_setupGeneration.fetch_add(1, std::memory_order_relaxed);
}

// Drops the previous root's renderer before a new open installs its own
// property set, animation and input state. Runs on the UI thread.
void ResetRenderer() {
    RevokeHandlers();
    ResetComposition();
}

}  // namespace

BOOL Wh_ModInit() {
    FileLog(L"mod init");
    Wh_Log(L"Legacy Start Edge Swipe 0.24.0 initializing");
    LoadAnimationStyle();
    LoadGestureStartSpeed();
    // The stop event the input thread's wait watches. Created before the thread
    // exists, so the unload has a signal that does not depend on the window.
    g_hidStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hidStopEvent) {
        FileLog(L"hid stop event failed err=%u",
                static_cast<unsigned>(GetLastError()));
    }
    // The cross-process channel: without it a gesture close still closes (the
    // Windows tap is local) but the bridge cannot tell that Hide from a keyboard
    // close, and a keyboard close cannot be answered with its retract.
    OpenIpcChannel();
    RemoveLegacyChannelFiles();
    FileLog(L"settle plan speed=%.2f dip/ms dur=%llu..%llu segment=%.1f ms "
            L"frames=%d..%d",
            static_cast<double>(kSettleSpeedDipPerMs),
            static_cast<unsigned long long>(kSettleMinDuration.count()),
            static_cast<unsigned long long>(kSettleMaxDuration.count()),
            kSettleSegmentMs, kSettleMinSteps + 1, kSettleMaxFrames);
    FileLog(L"settle spring win10 zeta=%.2f", kWin10SpringDamping);
    FileLog(L"settle spring win11 zeta=%.2f", kWin11SpringDamping);

    // The open/closed signal comes from DWM. Resolve it at runtime so a build
    // without the dwmapi import library still links and loads.
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (dwmapi) {
        g_dwmGetWindowAttribute.store(
            reinterpret_cast<DwmGetWindowAttribute_t>(
                GetProcAddress(dwmapi, "DwmGetWindowAttribute")),
            std::memory_order_relaxed);
    }
    if (!g_dwmGetWindowAttribute.load(std::memory_order_relaxed)) {
        Wh_Log(L"DwmGetWindowAttribute not resolved; gestures stay disabled");
    }

    // The root hook's build gate reads the file version of StartUI_.dll. The
    // exports are resolved at runtime for the same reason as dwmapi: no
    // version.lib in the compiler options and no dangling import.
    HMODULE version = LoadLibraryW(L"version.dll");
    if (version) {
        g_getFileVersionInfoSize.store(
            reinterpret_cast<GetFileVersionInfoSizeW_t>(
                GetProcAddress(version, "GetFileVersionInfoSizeW")),
            std::memory_order_relaxed);
        g_getFileVersionInfo.store(
            reinterpret_cast<GetFileVersionInfoW_t>(
                GetProcAddress(version, "GetFileVersionInfoW")),
            std::memory_order_relaxed);
        g_verQueryValue.store(
            reinterpret_cast<VerQueryValueW_t>(
                GetProcAddress(version, "VerQueryValueW")),
            std::memory_order_relaxed);
    }
    if (!g_getFileVersionInfoSize.load(std::memory_order_relaxed) ||
        !g_getFileVersionInfo.load(std::memory_order_relaxed) ||
        !g_verQueryValue.load(std::memory_order_relaxed)) {
        Wh_Log(L"version.dll not resolved; the root hook is refused");
    }

    // The 1 ms timer period is what makes the release land 18-22 ms after the
    // last report instead of 31. Resolved at runtime; the module is kept loaded
    // for the process lifetime so the resolved pointers never dangle.
    HMODULE winmm = LoadLibraryW(L"winmm.dll");
    if (winmm) {
        g_timeBeginPeriod.store(
            reinterpret_cast<TimePeriod_t>(
                GetProcAddress(winmm, "timeBeginPeriod")),
            std::memory_order_relaxed);
        g_timeEndPeriod.store(
            reinterpret_cast<TimePeriod_t>(GetProcAddress(winmm, "timeEndPeriod")),
            std::memory_order_relaxed);
    }
    if (!g_timeBeginPeriod.load(std::memory_order_relaxed) ||
        !g_timeEndPeriod.load(std::memory_order_relaxed)) {
        Wh_Log(L"timeBeginPeriod not resolved; release keeps the default timer "
               L"quantum");
    }

    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibraryExW = kernelBase
                              ? reinterpret_cast<LoadLibraryExW_t>(
                                    GetProcAddress(kernelBase,
                                                   "LoadLibraryExW"))
                              : nullptr;
    if (!loadLibraryExW) {
        Wh_Log(L"LoadLibraryExW not found; relying on polling");
    } else if (!Wh_SetFunctionHook(
                   reinterpret_cast<void*>(loadLibraryExW),
                   reinterpret_cast<void*>(LoadLibraryExW_Hook),
                   reinterpret_cast<void**>(&LoadLibraryExW_Original))) {
        Wh_Log(L"Failed to hook LoadLibraryExW; relying on polling");
    }

    TryInstallFromLoadedModules(false);

    g_pollThread =
        CreateThread(nullptr, 0, PollModulesThread, nullptr, 0, nullptr);
    if (!g_pollThread) {
        Wh_Log(L"Failed to create the module polling thread");
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    g_afterInit = true;
    TryInstallFromLoadedModules(false);
    Wh_ApplyHookOperations();

    FileLog(L"hid init");
    if (!g_hidStopEvent) {
        // A gesture source that cannot be stopped is worse than no gesture:
        // unloading would have to hope the thread notices instead of signalling
        // it. The diagnostics above still name the reason.
        FileLog(L"hid thread not started (stop event unavailable)");
        return;
    }
    g_hidThread =
        CreateThread(nullptr, 0, HidInputThread, nullptr, 0, nullptr);
    if (!g_hidThread) {
        FileLog(L"hid thread fail err=%u",
                static_cast<unsigned>(GetLastError()));
    }
}

void Wh_ModBeforeUninit() {
    // Stop hook registration and code that can run on its own thread before
    // Windhawk removes the hooks (Wh_SetFunctionHook/Wh_ApplyHookOperations
    // must not be called after this function returns).
    g_disabled = true;
    g_unloading = true;
    g_stopPolling = true;

    // Stop the asynchronous dismiss-settle callback from touching state once
    // the module starts to unload. A callback already inside module code is
    // drained before anything it touches is freed.
    g_stopDismissCallback = true;

    if (g_pollThread) {
        if (WaitForSingleObject(g_pollThread, kUnloadThreadWaitMs) ==
            WAIT_OBJECT_0) {
            CloseHandle(g_pollThread);
        } else {
            // The loop watches the stop event every 10-50 ms, so this is a stuck
            // thread, not a slow one. Its handle stays open and every module
            // resource stays in place: the process must restart to finish the
            // unload.
            g_pollThreadLeaked.store(true, std::memory_order_relaxed);
            FileLog(L"unload poll thread still running");
            FileLog(L"unload poll thread wait=%u ms timed out",
                    static_cast<unsigned>(kUnloadThreadWaitMs));
        }
        g_pollThread = nullptr;
    }

    // 1. Stop the input thread. The event does not depend on the window: the
    //    thread may still be between CreateThread and publishing the HWND, and
    //    a WM_QUIT aimed at a window that does not exist yet reaches nobody.
    if (g_hidStopEvent) {
        SetEvent(g_hidStopEvent);
    }
    HWND hwnd = g_hidHwnd.load(std::memory_order_relaxed);
    if (hwnd) {
        // Belt to the event: a message already queued keeps the loop's
        // MsgWaitForMultipleObjects on the message branch, and this is what
        // ends it there.
        PostMessageW(hwnd, WM_QUIT, 0, 0);
    }

    // 2. Wait for it. Nothing it touches may be freed before this returns.
    if (g_hidThread) {
        const DWORD waitResult =
            WaitForSingleObject(g_hidThread, kUnloadThreadWaitMs);
        if (waitResult == WAIT_OBJECT_0) {
            CloseHandle(g_hidThread);
            FileLog(L"unload hid thread stopped");
        } else {
            // It is still inside module code: its window procedure, its globals
            // and the IPC view are all live. Nothing may be released under it.
            // Keep the handle open so the thread object is not freed either.
            g_hidThreadLeaked.store(true, std::memory_order_relaxed);
            FileLog(L"unload hid thread still running; ipc view leaked");
            FileLog(L"unload hid thread wait=%u ms timed out",
                    static_cast<unsigned>(kUnloadThreadWaitMs));
        }
        g_hidThread = nullptr;
    } else {
        FileLog(L"unload hid thread stopped");
    }
}

void Wh_ModUninit() {
    // A thread that BeforeUninit did not stop must be treated like a leaked one:
    // every resource it can touch stays alive, and the log names it. The module
    // is then not honestly gone; the process must restart.
    if (g_hidThread) {
        g_hidThreadLeaked.store(true, std::memory_order_relaxed);
        FileLog(L"unload hid thread still running; ipc view leaked");
    }
    if (g_pollThread) {
        // An uninit that skipped BeforeUninit still has a module-scan thread in
        // module code; it is a residual exactly like the input thread.
        g_pollThreadLeaked.store(true, std::memory_order_relaxed);
        FileLog(L"unload poll thread still running");
    }

    const bool hidLeaked =
        g_hidThreadLeaked.load(std::memory_order_relaxed);
    const bool pollLeaked =
        g_pollThreadLeaked.load(std::memory_order_relaxed);
    if (hidLeaked || pollLeaked) {
        FileLog(L"unload residual=%s%s; process must restart to finish "
                L"unloading",
                hidLeaked ? L"hid thread" : L"",
                pollLeaked ? (hidLeaked ? L",poll thread" : L"poll thread")
                           : L"");
        Wh_Log(L"Unload left resources in place: a worker thread is still "
               L"running; restart StartMenuExperienceHost to finish unloading");
        return;
    }

    // 3. The input thread can no longer post. Revoke every XAML delegate on the
    //    UI thread and wait for that turn: a queued revocation must be shown to
    //    have run before the objects it holds are dropped.
    if (!RevokeHandlersOnUiThread()) {
        FileLog(L"unload residual=xaml handlers; process must restart to finish "
                L"unloading");
        Wh_Log(L"Unload left resources in place: the UI thread did not revoke "
               L"the XAML handlers; restart StartMenuExperienceHost to finish "
               L"unloading");
        return;
    }

    // 4. Detach the settle batch's Completed handler, then drain a callback the
    //    compositor already dispatched. Only after both may the composition
    //    objects it touches be released.
    RevokeDismissBatch();
    if (!WaitForDismissCallbackDrain(kUnloadCallbackDrainMs)) {
        FileLog(L"unload residual=dismiss completion; process must restart to "
                L"finish unloading");
        Wh_Log(L"Unload left resources in place: a settle completion is still "
               L"running; restart StartMenuExperienceHost to finish unloading");
        return;
    }

    // 5. Only now free what the thread's leftovers would touch: the batch
    //    handler, the composition state and the shared section.
    ResetComposition();
    ClearScrollerRetry();

    SwipeIpcClose(&g_ipc);

    if (g_hidStopEvent) {
        CloseHandle(g_hidStopEvent);
        g_hidStopEvent = nullptr;
    }
    Wh_Log(L"Legacy Start Edge Swipe Dismiss uninitialized");
}

// A settings change only refreshes the atomics: the next settle reads them, no
// renderer state and no cached curve is touched.
void Wh_ModSettingsChanged() {
    LoadAnimationStyle();
    LoadGestureStartSpeed();
}

