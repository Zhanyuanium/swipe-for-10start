// ==WindhawkMod==
// @id              start-menu-legacy-swipe
// @name            Legacy Start Edge Swipe
// @description     Touch/pen edge swipe to open and to dismiss the legacy Start menu
// @version         0.12.0
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

- Hooks `StartUI::App::PrepareRootElement` (`StartUI_.dll`, RVA `0xAD730`),
  captures the returned root after it loads and enables Composition
  translation.
- Hooks `StartUI::SplitViewFrame::OnGlobalAnimationRequested` (`StartUI_.dll`,
  RVA `0x4164C0`) and can drop the request whose `AnimationType` is 0 when the
  debug line `anim=0` sets the switch; the switch starts at `anim=1`, the
  original behaviour. The close request (`AnimationType` 1) always reaches the
  original unchanged. The animation type is read from the event arguments with
  the same three int getters the original calls, not from a call argument.
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
- Each device is decoded from its `RIDI_PREPARSEDDATA` blob, never by opening
  the device. Contact collections are the `0x0D/0x05` and `0x0D/0x22` link
  nodes. `contacts` is the top-level Contact Count (`0x0D/0x54`); X and Y come
  from the first contact collection with a non-zero position.
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
- Release commits when the axis travel exceeds 20% of `Distance` or the average
  velocity exceeds 0.45 DIP/ms. Both outcomes settle with the style the setting
  selects: a commit retracts `Progress` to 0, a cancel returns it to 1, and the
  Windows 10 spring's initial velocity is the release speed (scaled by the
  setting) in either direction, while the Windows 11 curve starts at rest. The
  tap that closes is armed for the end of that settle. Every Progress animation
  inserts the target as the base value before `StartAnimation`.
- A vertical dismiss competes with the app list's own scroll: dragging toward
  the taskbar is how the list scrolls back, and that drag used to close the
  menu. The element that scrolls is measured per open on the UI thread: the
  innermost `ScrollViewer` with `ScrollableWidth` 0 and `ScrollableHeight` above
  0, whose screen rect (XAML rect plus the client origin) and
  `VerticalOffset`/`ScrollableHeight` are published for the input thread. Inside
  that rect a `Top`/`Bottom` dismiss starts only when the list is already at the
  end the drag would scroll away from (offset 0 on a bottom taskbar, offset ==
  `ScrollableHeight` on a top one); otherwise the press is no dismiss and logs
  `dismiss deferred to scroll vo=... sh=...` at most once per 500 ms. Outside
  the rect, and when nothing was measured, the plain drag stays. The rejection
  starts no session, so the same contact becomes a dismiss as soon as its
  scrolling reaches the end; the contact latch is untouched and the next down
  starts from a clean state. `Left`/`Right` are unchanged.
- The settle has two styles, and one setting picks the style for all four
  outcomes: the dismiss commit and cancel, the reveal commit and cancel.
  - Windows 10 (the spring): open and close mirror exactly because it runs in
    normalized travel `y = (Progress - from) / (to - from)`: `y(0) = 0`,
    `y(inf) = 1`, and `y'(0)` is the release speed scaled by
    `GestureStartSpeedPercent` (default 100) divided by the travel left. The
    natural frequency is `2*pi / 0.25 s`, the damping ratio 0.9, and the
    duration is five time constants,
    `T = clamp(5 / (zeta*omega), 180, 400)` = 221 ms at those values. Thirteen
    keyframes (12 equal segments) sample
    `y(t) = 1 - exp(-zeta*omega*t) * (cos(wd*t) + ((zeta*omega - y0)/wd) *
    sin(wd*t))`, with every value clamped to 0..1: a flick faster than the
    remaining travel can absorb flattens at the rest position instead of
    translating past it. The keyframes are linear and no easing function is
    attached, because the samples are the curve.
  - Windows 11 (the default): one `ScalarKeyFrameAnimation` over 300 ms,
    `InsertKeyFrame(0, from)` and `InsertKeyFrame(1, to, easing)`, where the
    easing is the cached `cubic-bezier(0.1, 0.9, 0.2, 1)` that Setup builds
    once. It carries no release velocity: the motion starts at rest and
    decelerates into the target. `from` and `to` are clamped the same way.

  One settle-and-arm helper serves both gesture families: it plays the
  animation, opens the suppression window, stamps `close-mode.txt` and arms the
  tap for the end of the settle, which is 300 ms on Windows 11 and `T` on the
  spring. A close whose settle did not start arms nothing. One line records
  each settle: `settle style=win11|win10 dir=open|close vel=... dur=...`. The
  gesture's release line is written after the settle started: the file write
  costs time on the input thread, and before the animation it only made the
  seam longer.
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
- Two or more contacts cancel the gesture and are ignored until the panel is
  released. A lift report is not required: a session with no sample for 18 ms
  ends on its own.
- Reveal follow: a contact that starts within 12 DIP of the taskbar edge while
  the menu is closed is a candidate. EP opens the menu by itself once the
  contact passes its own edge threshold; the first report that sees the menu
  open takes `Progress` over. The follow works in absolute screen DIP against
  the panel edge that faces the taskbar (`panelTrail`), which the UI thread maps
  from the largest visible child of the root that does not fill it
  (`StartSizingFrame`) and translates by the XAML origin, the client origin of
  the host window in DIP (`ClientToScreen`), not the work-area origin:
  `Progress = 1 - (finger - panelTrail) / (dir * Distance)`. The edge is
  published at setup, on every root `SizeChanged`, on every menu open, on the
  second layout pass of that open and on every work-area or display change:
  setup runs while the shell's open
  storyboard still has the frame out of place (measured on a bottom taskbar:
  panel top -12 DIP, settled 200), so the open, layout and event refreshes are
  what publish the settled edge. It logs only when the value moves by more than
  1 DIP, and a value outside the monitor work area by more than 24 DIP is
  rejected in favour of the last good one (`reveal panelTrail rejected ...`).
  The panel edge therefore meets the finger at every sample, and the
  panel is exactly off screen at the screen edge (Progress 0.269 for a 1052 DIP
  trail on a 1440 DIP root). That mapping drives the visual only. The release
  decision mirrors the dismiss path: it judges the travel away from the edge at
  release, `(finger - edgeStart) * openingSign` clamped at 0, where
  `openingSign` is +1 for a left or top taskbar and -1 for right or bottom. A
  release past 20% of `Distance`, or one whose average velocity
  `travel / max(elapsed, 1)` exceeds 0.45 DIP/ms, settles `Progress` to 1 with
  the selected settle style and keeps the menu. Anything else is a cancel:
  `Progress` settles to 0 with that same style, so both close paths read the
  same from any finger position. The Windows tap that closes the menu is armed
  for the end of the settle, so the panel is already off screen when the shell
  hides it. `Progress`
  stays 0; a later
  open restores it, unless an edge contact is already driving that open. Pulling
  out and pushing back therefore cancels, as it does inside the open menu. The
  line `open follow travel=... max=... progress=... vel=... commit=... panel=... n=...`
  is written once per reveal; the cancel adds `close tap kind=reveal delay=0`,
  or `close tap dropped kind=reveal` when a new pull is already there at the tap.
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
  or tray open) is animated by the mod: `Progress` is set to 0 and eases to 1
  over 260 ms on `cubic-bezier(0.45, 0, 0.55, 1)`, so a keyboard open slides the
  same way a gesture open does. `PollRootVisibility` polls the cloaked attribute
  at most once per 20 ms on the 5 ms idle tick and sees the transition; a live
  edge candidate and the 600 ms suppression window both suppress the animation.
  A close the shell performs by itself logs `win close observed`, leaves
  `Progress` where the close left it, and lets the next open animate from 0.
  The Windows-key close is animated now: the explorer-side bridge delays
  `CStartExperienceManager::Hide` by 350 ms and writes `retract=1` to
  `debug.txt`, so the mod runs the selected settle style from rest (221 ms
  spring, 300 ms Windows 11) while the shell waits and the panel slides out
  instead of vanishing. One line per transition.
- Every gesture close stamps `close-mode.txt` at the release with
  `gesture=0`. The tap comes at the end of the settle, so the Hide it causes
  needs no wait; the stamp tells the bridge that this Hide is that tap's and not
  a keyboard close. A stamp older than the bridge's 800 ms window means a
  keyboard close and gets the fixed 350 ms lead instead.
- A new edge pull during the settle cancels the close before the shell is
  touched: the tap is dropped and the pull keeps driving the panel, so no Hide
  runs and the toggle is never advanced. The pull generation is sampled at the
  release and checked once when the tap would run; the drop overwrites the stamp
  with `cancelled`, so a later keyboard close uses its own lead.

What it does not do:

- It does not use the explorer-side bridge's shared memory; the bridge reaches
  it only through the `retract` line and the `close-mode.txt` stamp.
- It does not attach XAML pointer handlers and has no DirectManipulation
  hook.
- It does not remove the shell's open storyboard. Measured with `anim=0`:
  dropping the open request leaves the Start window uncloaked with no pane
  drawn, because the same request is the step that presents the content. The
  switch is therefore off and the open still slides from the shell.

Settings:

- `AnimationStyle` (default `Windows 11`) picks the settle mechanism. The value
  is the display label: exactly `Windows 10` selects the damped spring, and
  every other value (including the default) selects the Windows 11 Fluent
  curve. `Wh_ModInit` reads it once into an atomic enum and logs
  `settle style selected=... setting=...`; `Wh_ModSettingsChanged` refreshes
  the atomic, so a change applies to the next settle and no settle performs a
  setting lookup.
- `GestureStartSpeedPercent` (default 100, clamped to 0-300) is the initial
  velocity of the Windows 10 spring, as a percentage of the finger's release
  speed. `Wh_ModInit` reads it once into an atomic and logs
  `settle start speed=...`; `Wh_ModSettingsChanged` refreshes the atomic, so a
  change applies to the next settle and no settle performs a setting lookup.
  100 continues at exactly the finger's speed, so an open and a close leave the
  seam at the same velocity the finger had; higher values snap away faster. The
  Windows 11 curve starts at rest and ignores this value.

Debug channel (kept in the build):

- `debug.txt` in the mod storage directory
  (`C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\local@start-menu-legacy-swipe\`)
  is polled every 50 ms on the raw input thread and re-read only when its write
  time or size changes. One command per line:
  - `p=0.5` inserts `Progress`, clamped to 0..1.
  - `t=-300,0,0` sets `UIElement.Translation` on the UI thread.
  - `e=-300,0` sets `Properties["Offset"]` and `Visual.Offset` on the visual.
  - `anim=0` suppresses the shell's open storyboard; `anim=1` restores it. The
    default is `anim=1`, because suppression leaves the pane undrawn.
  - `retract` plays the committed-dismiss settle from the current `Progress`:
    `BeginGestureSettle(current, 0, 0)` in the selected style from rest (the
    221 ms spring or the 300 ms Windows 11 curve), and logs `debug retract`.
    The next open restores the scalar, so the command
    leaves the reuse flag clear. The explorer-side bridge writes this line 350 ms
    before `CStartExperienceManager::Hide` runs, which is what turns a
    Windows-key close into a slide-out. It applies only when the file was
    written within the last second: the file keeps the last command after the
    close, and a fresh Start process must not replay it on attach (that logs
    `debug retract ignored (stale)`).
  Empty lines and unknown lines are ignored. The channel compares the rendering
  mechanisms without a gesture; it does nothing before the first
  `attached root=...` line.
- `close-mode.txt` in the same directory is not a command file. A gesture close
  writes `gesture=0` at its release. The tap comes after the settle and causes
  a Hide that needs no wait; the stamp is what tells the bridge that Hide is not
  a keyboard close. A dropped close writes `cancelled`, which the bridge cannot
  parse and treats as a keyboard close. Only a file younger than 800 ms is read;
  a missing or older stamp means a keyboard close and the bridge uses its fixed
  350 ms lead. `CREATE_ALWAYS` creates and overwrites it, so no other setup is
  needed.

Diagnostics:

- `mod init`, `settle style selected=... setting=...`,
  `settle start speed=...%`,
  `settle spring period=... zeta=...`, `hid init`,
  `hid reg usage=... ok=... err=...`,
  `hid down x=... y=... axis=... dist=...`,
  `hid release src=idle|report|abort delta=... perp=... vel=... commit=...
  n=... relms=...`,
  `settle style=win11|win10 dir=open|close vel=... dur=...`,
  `win open`, `win close observed`,
  `close tap kind=dismiss|reveal delay=...`,
  `close tap dropped kind=dismiss|reveal`,
  `close tap skipped kind=... (already closed)`,
  `close mode write failed kind=...`, `close mode cancel write failed`,
  `open follow travel=... max=... progress=... vel=... commit=... panel=... n=...`,
  `reveal panelTrail=...` (or `reveal panelTrail=... (fallback)`, or
  `reveal panelTrail rejected ...` when the work-area guard kept the previous
  value),
  `hid decodefail dev=... err=...`,
  `attached root=...`, `dismiss axis=...`, `debug p=...`, `debug t=...`,
  `debug e=...`, `debug anim=...`, `debug retract`,
  `debug retract ignored (stale)`,
  `gesture geometry updated axis=... dist=...` (or
  `gesture geometry axis=... dist=... (unchanged)` once per open),
  `gesture geometry write failed ...`, `gesture geometry queue err=...`,
  `work area changed`,
  `scroller rect=...,...,...,... vo=... sh=...` (once per open, when the
  scrolling element was measured, and again only if the settled pass moved it),
  `dismiss deferred to scroll vo=... sh=...` (at most once per 500 ms),
  `startui anim hooked`,
  `startui anim hook skipped` and the first four
  `startui anim type=... edge=... trans=... suppress=...` calls go to
  `renderer.log`.
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
  that child is missing or unlaid when `Setup` and `SizeChanged` run, the trail
  falls back to the root extent and the follow lags the finger again (log
  `reveal panelTrail=... (fallback)`).
- The first trail of an open is measured too early to be trusted: `Setup` runs
  while the shell's open storyboard is still growing the frame
  into place, and on a bottom taskbar that read -12 DIP where the settled panel
  starts at 200. The open transition re-measures it, and the layout watch
  re-measures it again on the second `LayoutUpdated` of that open, so the settled
  anchor is what a follow started after the open uses; a gesture that begins
  inside those first two layout passes still maps against the earlier value.
  Setup's value is the one that survives when every later measurement is
  rejected by the work-area guard.
- The trail assumes the XAML coordinate space starts at the host window's client
  origin, which is what `ClientToScreen` returns. Measured on this build with a
  bottom taskbar: the CoreWindow sits at x=447 px while the work area starts at
  0, so the panel's frame right of 1005 DIP is drawn at 1228.5 DIP. A host that
  lays its content out somewhere else than its client area would move the trail
  by that difference.
- `panelTrail` is republished on every root `SizeChanged`, on every open and on
  every work-area or display change. A panel resize during a follow moves the
  mapping, so the panel edge jumps to the new trail under the same finger. The
  event refresh measures only when no gesture owns the panel, so a live follow
  never has its anchor moved.
- A taskbar move is noticed only through `WM_SETTINGCHANGE`
  (`SPI_SETWORKAREA`) or `WM_DISPLAYCHANGE`; an arrangement change that raises
  neither keeps the old axis until the next open, which always refreshes it.
- `Distance` is sampled at takeover. A layout change during the follow keeps
  the old extent until the contact lifts.
- Composition calls and the settle batch run from the input thread. The batch
  completion arrives on the compositor's dispatcher; if it never arrives,
  `Progress` stays at 0 and every later down is ignored until that batch
  completes.
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
- `close-mode.txt` is read only inside its 800 ms window. A gesture close stamps
  `gesture=0` at its release; the tap's Hide arrives at the end of the settle
  (300 ms on Windows 11, 221 ms on the spring) and runs
  at once. A dropped close stamps `cancelled`, which the bridge treats as a
  keyboard close, so a Win press within the window still gets the 350 ms lead.
- `DwmGetWindowAttribute` is resolved at init from `dwmapi.dll`. If the export
  is missing, no gesture starts.
- The `PrepareRootElement` and `OnGlobalAnimationRequested` RVAs are specific to
  `StartUI_.dll` 10.0.22621.3733. The animation hook checks its target prologue
  and the three argument getters before it installs; a different build logs
  `startui anim hook skipped` and keeps the original animation.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- AnimationStyle: Windows 11
  $name: Gesture animation style
  $description: >-
    Windows 11 uses the Fluent decelerate curve (cubic-bezier(0.1, 0.9, 0.2,
    1)) over 300 ms from rest. Windows 10 uses a damped spring that starts at
    the finger's release speed, scaled by GestureStartSpeedPercent.
  $options:
  - Windows 11: Fluent decelerate curve, 300 ms from rest
  - Windows 10: Damped spring that continues the release speed
- GestureStartSpeedPercent: 100
  $name: Gesture settle initial velocity (% of finger speed)
  $description: >-
    Initial velocity of the Windows 10 spring, as a percentage of the finger's
    speed at release. 100 continues at exactly the finger's speed; higher snaps
    away faster. The Windows 11 curve starts at rest and ignores this value.
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
 *   timeBeginPeriod and timeEndPeriod, resolved at init from winmm.dll so the
 *   process holds a 1 ms period while a contact is down; on the
 *   CREATE_WAITABLE_TIMER_HIGH_RESOLUTION waitable timer that drives the 5 ms
 *   idle tick (WM_TIMER stays on the 15.6 ms tick on this build); on
 *   HidP_GetCaps, HidP_GetLinkCollectionNodes, HidP_GetValueCaps and
 *   HidP_GetUsageValue from hid.dll; on the CompositionPropertySet, the
 *   expression and two cached cubic-bezier easings installed in Setup
 *   ((0.45, 0, 0.55, 1) for the keyboard open, (0.1, 0.9, 0.2, 1) for the
 *   Windows 11 settle), plus the damped-spring settle the gesture paths sample
 *   at `GestureStartSpeedPercent` of the release speed when the style setting
 *   selects `Windows 10`; on
 *   VisualTreeHelper, TransformToVisual and LayoutUpdated
 *   from Windows.UI.Xaml.Media to map the panel trail and to follow the layout;
 *   on ScrollViewer, its ScrollableWidth/ScrollableHeight/VerticalOffset and its
 *   ViewChanged event from Windows.UI.Xaml.Controls, which name the element the
 *   app list scrolls with; on
 *   ClientToScreen, which turns the host window's client origin into the screen
 *   offset of the XAML coordinate space; on the
 *   CoreDispatcher
 *   captured in Setup, which the input thread posts the open-time and
 *   work-area refresh to; on the `WM_SETTINGCHANGE` (`SPI_SETWORKAREA`) and
 *   `WM_DISPLAYCHANGE` broadcasts the hidden top-level input window receives
 *   (a message-only window would not, which is why the window is not
 *   message-only); on `debug.txt` in the
 *   mod storage directory, read by the debug channel; on the fixed
 *   `close-mode.txt` path in the same directory: each gesture close stamps
 *   `gesture=0` at its release, or `cancelled` when a pull drops its tap, and the
 *   bridge uses the stamp to tell the tap's Hide from a keyboard close.
 * [OUTPUT]: one hidden top-level window and one worker thread per process; the
 *   Composition `Progress` scalar plus the `Distance`/`DirX`/`DirY` inputs the
 *   expression reads, which the open-time and work-area refresh rewrites when
 *   the taskbar moves; the panel trail in absolute screen DIP, published for the
 *   reveal follow by Setup, SizeChanged, every menu open and every work-area or
 *   display change, never by a timer, with a value outside the monitor work area
 *   rejected; one `VK_LWIN` tap armed for the end of every
 *   committed dismiss or short reveal, the shell's own Start toggle that performs
 *   `CStartExperienceManager::Hide`; the tap is dropped when a new pull
 *   generation exists when it would run, and no request file or re-arm tap
 *   exists; one settle in the style `AnimationStyle` selects, and one
 *   settle-and-arm helper for all four gesture outcomes. `Windows 11` (the
 *   default) is one eased pair over 300 ms from rest on the cached
 *   cubic-bezier(0.1, 0.9, 0.2, 1); `Windows 10` is the spring (open and close
 *   mirror exactly: the initial velocity is `GestureStartSpeedPercent` of the
 *   finger's release speed, scaled by the travel left, with the natural period
 *   0.25 s, damping 0.9 and a 221 ms duration, sampled by 13 linear keyframes
 *   clamped to 0..1). The helper also owns the suppression window, the
 *   close-mode stamp and the
 *   armed tap, whose delay is the settle's own duration (300 ms or 221 ms), so
 *   neither gesture family can skip one; both settings are read once at init
 *   and refreshed on a settings change, logged as
 *   `settle style selected=... setting=...` and
 *   `settle start speed=...%`; a linear coast of
 *   `Progress` at the finger's smoothed speed while
 *   no report arrives, started by the idle tick before that tick decides the
 *   release, ended by the next report or by the release, which then
 *   settles from the value the coast reached; one cached scroller per
 *   open (the innermost vertically scrollable element the app list uses), with
 *   its screen rect and its `VerticalOffset`/`ScrollableHeight` published for the
 *   input thread and refreshed from its `ViewChanged` event, which rejects a
 *   vertical dismiss that would only scroll the list and logs
 *   `dismiss deferred to scroll`; a
 *   0-to-1 progress slide over 260 ms for a closed-to-open transition no reveal
 *   owns, plus a `win close observed` line when the shell hides the window by
 *   itself; a 1 ms timer period (timeBeginPeriod/timeEndPeriod) held exactly
 *   while a contact is down and one 5 ms high-resolution idle tick that detects
 *   the release; a suppression window, at most 600 ms, after either close in
 *   which a closed-to-open report does not restore `Progress` and an edge
 *   contact starts a new reveal rather than a drag of the settling panel; the
 *   window ends as soon as the shell reports the window cloaked; an optional
 *   suppressed open animation request
 *   (type 0, off by default because dropping it leaves the pane undrawn) while
 *   the close request (type 1) always passes through; five file-driven debug
 *   commands (`p`, `t`, `e`, `anim`, `retract`) that apply a Progress value, the
 *   XAML translation, the visual offset, the animation mode or the settle
 *   the explorer-side bridge asks for; no exported symbols.
 * [POS]: input and gesture layer of the legacy Start mod. It replaces the XAML
 *   pointer and DirectManipulation paths with raw HID decoding, owns two
 *   gestures (the dismiss drag inside the open menu and the reveal follow that
 *   takes the menu's own open over from the taskbar edge and pins the panel's
 *   trailing edge to the finger), and re-reads the taskbar edge behind both on
 *   every open and on every work-area broadcast. It also carries the switch that
 *   can drop the
 *   shell's open storyboard for experiments. It also owns the close decision:
 *   the settle runs first, then the tap at its end; a pull generation checked
 *   at the tap drops it, so a re-pull never touches the shell. The explorer-side
 *   bridge still calls the original Hide; it only skips its keyboard lead when
 *   the stamp names the tap.
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
// StartUI_.dll 10.0.22621.3733 (x86-64).
constexpr uintptr_t kPrepareRootElementVa = 0x1800AD730;

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
// The gesture settle: two selectable styles for all four outcomes
//
// The setting picks the style; every outcome uses the picked one: the dismiss
// commit and cancel, the reveal commit and cancel. Windows 10 is the damped
// spring this mod shipped from the start, Windows 11 is the Fluent decelerate
// curve.
//
// Spring (Windows 10). Open and close are exact mirrors. A cubic-bezier with a
// fixed duration cannot do that: its initial slope is only a multiple of the
// animation's own average speed, and covering the remaining travel in a fixed
// time forces the curve to accelerate, which contradicts an opening that should
// slow down. A spring's initial velocity is the finger's release speed (scaled
// by the setting) in either direction, and its damping supplies the
// deceleration.
//
// The spring works in normalized travel `y = (p - from) / (to - from)`, so
// y(0) = 0 and y(inf) = 1. With natural frequency omega, damping ratio zeta and
// damped frequency wd = omega*sqrt(1 - zeta^2):
//
//   y(t) = 1 - exp(-zeta*omega*t)
//               * (cos(wd*t) + ((zeta*omega - y0) / wd) * sin(wd*t))
//
// y0 = y'(0) is the scaled release speed divided by the travel left in DIP.
// The sign of the panel velocity cancels in the normalized slope, because
// (to - from) carries it too: the spring always relaxes from 0 toward 1. The
// panel velocity keeps the sign for the reader, so a close reports a negative
// DIP/s velocity and an open a positive one.
//
// The duration is five time constants, 1/(zeta*omega), which is ~221 ms at
// these values, clamped to 180-400 ms. The tail is below 1% by then, so the
// last samples sit on the target. The settle is a polyline of 12 equal
// segments sampled from that curve; the keyframes are linear and no easing
// function is attached, because the samples are the curve.
//
// Fluent (Windows 11). One eased pair: `from` at t=0 and `to` at t=1 on
// cubic-bezier(0.1, 0.9, 0.2, 1) over 300 ms. It carries no release velocity:
// the motion starts at rest and decelerates into the target, which is how the
// shell's own Fluent transitions move. The fixed duration is what the close
// tap is armed with as well.
// ---------------------------------------------------------------------------
constexpr double kSpringPeriodSeconds = 0.25;
constexpr double kSpringDamping = 0.9;
constexpr double kSettleTimeConstants = 5.0;
constexpr std::chrono::milliseconds kSettleMinDuration{180};
constexpr std::chrono::milliseconds kSettleMaxDuration{400};
// 12 segments, so 13 keyframes: both the release position and the target are
// sampled, and the animation cannot stop one segment short of its destination.
constexpr int kSettleSteps = 12;
constexpr int kSettleSampleCount = kSettleSteps + 1;

// The Windows 11 style: one eased pair over 300 ms on the Fluent decelerate
// curve. The four numbers are cubic-bezier's two control points; Setup builds
// the cached easing function from them once.
constexpr std::chrono::milliseconds kWin11SettleDuration{300};
constexpr float kWin11SettleControl1X = 0.1f;
constexpr float kWin11SettleControl1Y = 0.9f;
constexpr float kWin11SettleControl2X = 0.2f;
constexpr float kWin11SettleControl2Y = 1.0f;

// The spring setting: the settle's initial velocity as a percentage of the
// finger's release speed. 100 continues at exactly that speed. The Windows 11
// curve starts at rest, so it reads no value from here.
constexpr int kGestureStartSpeedDefault = 100;
constexpr int kGestureStartSpeedMin = 0;
constexpr int kGestureStartSpeedMax = 300;

// A closed-to-open transition that no gesture owns (Windows key, Start button,
// tray) gets a slide a reveal produces: Progress from 0 to 1 on
// cubic-bezier(0.45, 0, 0.55, 1). The keyboard open is not a gesture settle: it
// has no release speed to continue, so it keeps its fixed duration and curve.
constexpr std::chrono::milliseconds kOpenSlideDuration{260};

// Every gesture close ends in one Windows tap at the end of its settle. The tap
// is the shell's own Start toggle: the key handler performs Hide, so no
// explorer-side request and no re-arm tap exist. The settle runs first, so the
// panel is already off screen when the window hides; close-mode.txt tells the
// bridge that the Hide belongs to this tap and needs no further delay.
constexpr uint64_t kCloseTapDelayMs = 0;

// A close this mod started hides the menu a moment later. Until then the shell
// still reports the window open, so the reopen poll must not read that as a
// fresh open and restore Progress while the close is in flight.
constexpr uint64_t kRestoreSuppressMs = 600;

// The tap has to run inside that window, or the poll reads the still-open window
// as a fresh open and restores Progress while the close is in flight. Both
// styles must land inside it; the spring's clamp is the longer of the two.
static_assert(static_cast<uint64_t>(kSettleMaxDuration.count()) <=
                  kRestoreSuppressMs,
              "the close tap must run inside the restore suppression");
static_assert(static_cast<uint64_t>(kWin11SettleDuration.count()) <=
                  kRestoreSuppressMs,
              "the Windows 11 close tap must run inside the restore "
              "suppression");

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
constexpr UINT_PTR kDebugTimerId = 2;
constexpr UINT kDebugTimerMs = 50;
// The DWM cloaked query is a syscall. The report handler accepts one answer
// per interval and reuses it for every other report.
constexpr uint64_t kOpenCheckIntervalMs = 20;

// A trail outside the monitor work area cannot be the panel's edge. 24 DIP of
// tolerance keeps a panel border that overhangs the work area accepted, and a
// frame still mid-animation rejected.
constexpr float kPanelTrailToleranceDip = 24.0f;

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
// The reveal maps Progress from the panel trail, which the UI thread publishes
// when the root opens; for the first 50 ms that mapping is still settling.
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

std::mutex g_compositionMutex;
wuc::Compositor g_compositor{nullptr};
wuc::CompositionPropertySet g_state{nullptr};
wuc::ExpressionAnimation g_translationExpression{nullptr};
wuc::Visual g_visual{nullptr};
wuc::CompositionScopedBatch g_dismissBatch{nullptr};

// The two fixed curves, created in Setup and cleared with the property set.
// Every animation reads one under g_compositionMutex, so neither outlives the
// compositor that created it.
//
// - g_retractEasing: cubic-bezier(0.45, 0, 0.55, 1) starts gently and ends
//   gently; the keyboard open uses it.
// - g_win11SettleEasing: cubic-bezier(0.1, 0.9, 0.2, 1), the Fluent decelerate
//   curve; the Windows 11 settle uses it.
//
// The Windows 10 settle uses neither: its sampled spring is the curve.
wuc::CompositionEasingFunction g_retractEasing{nullptr};
wuc::CompositionEasingFunction g_win11SettleEasing{nullptr};

// The value the renderer currently shows. Composition has no read-back, so every
// write to `Progress` records it here; the debug `retract` command starts its
// animation from this value instead of from a gesture session that may be gone.
// A coast moves the value without a write, so it is not reflected here until
// StopCoast pins what it reached.
std::atomic<float> g_progressNow{1.0f};

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
// The five numbers belong together, so they are one POD behind a mutex instead
// of five atomics: a torn rect/offset pair could allow or reject the wrong
// press. The lock is taken once per down on the input side and only when the
// cache is measured or scrolled on the UI side, so it is never contended.
// ---------------------------------------------------------------------------
struct ScrollerCache {
    bool valid = false;
    bool settled = false;  // false until the post-storyboard layout pass
    float left = 0.0f;    // screen DIP, all four from the XAML origin
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
// rect that actually moved is worth a second line.
bool g_scrollerLogged = false;
float g_scrollerLoggedRect[4] = {};
// Coarse tick of the last `dismiss deferred to scroll` line. The digitizer
// reports at ~170 Hz, so the rejection is rate limited; input thread only.
uint64_t g_scrollerRejectLogTick = 0;

winrt::event_token g_loadedToken{};
winrt::event_token g_sizeToken{};

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
// would run. This is one scheduled tap, not a request file.
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

// Filled in at setup and on every PrepareRootElement.
int32_t g_dismissAxis = static_cast<int32_t>(DismissAxis::Left);
float g_dismissDistance = 0.0f;

// Published by the UI thread and read by the input thread. No XAML object is
// shared: only these plain values.
//
// g_hasRoot is set by Setup and never cleared: PrepareRootElement does not fire
// again for a reused XAML root, so it only answers "the renderer was installed
// once in this process". Whether the menu is open is decided per down by the
// DWM cloaked attribute, never by a latch.
std::atomic<bool> g_hasRoot{false};
std::atomic<int32_t> g_axisPub{static_cast<int32_t>(DismissAxis::Left)};
std::atomic<float> g_distancePub{0.0f};
// A one-shot settle deadline (see the open transition): the layout watch needs a
// second layout pass, and after the open storyboard there may not be one until
// the user touches the menu.
std::atomic<uint64_t> g_settleDueTick{0};
// Diagnostics for a dismiss that did not start: one line per second, not per
// report, because a deferred scroll produces a sample every ~6 ms.
uint64_t g_skipLogTick = 0;
void PostUiSettledMeasurement(bool trailIdle);
bool g_settleTrailIdle = false;
std::atomic<int64_t> g_winRectDip[4]{};  // left, top, right, bottom

// The panel edge that faces the taskbar, in absolute screen DIP. The root
// canvas spans the screen (1440 DIP here) while the visible panel is narrower
// (~993 DIP), so the reveal must be measured against this edge, not against the
// root extent. 0 means "not known yet"; the input thread then uses
// g_distancePub, which restores the old root-width travel.
//
// Setup publishes the first value, but it runs while the shell's open
// storyboard still has the frame out of place: on a bottom taskbar that read a
// panel top of -12 DIP where the settled panel starts at 200. The value is
// therefore re-measured on every menu open and on every work-area or display
// change the system announces, never on a timer: the layout only moves when one
// of those events says it did.
std::atomic<float> g_panelTrailDip{0.0f};

// UI thread only. Setup resets the open log; a failed mapping logs its fallback
// once per process so a missing panel does not flood the log every resize. A
// trail rejected by the work-area guard logs once, and the last good value
// stays published.
bool g_panelTrailLogged = false;
bool g_panelTrailFallbackLogged = false;
bool g_panelTrailRejectedLogged = false;

HANDLE g_hidThread = nullptr;
std::atomic<HWND> g_hidHwnd{nullptr};

// Defined with the unload helpers; PrepareRootElement uses them to reset on
// every open.
void RevokeHandlers();
void ResetComposition();
void ResetRenderer();
HWND FindStartCoreWindow();

// Defined with the UI-thread publication below. The input thread posts it when
// the menu opens and when the system announces a work-area or display change;
// `logOnChange` keeps a stable layout silent.
void PublishPanelTrailDip(bool logOnChange);

// Defined with the UI-thread publication below. The open transition arms this
// watch, which re-measures the panel edge on the second layout pass, when the
// shell's open storyboard has placed the panel. The close teardown disarms it.
void ArmSettledTrailWatch(bool trailIdle);
void StopSettledTrailWatch();

// Defined with the scroller cache below. Measures the scrolling element an open
// menu scrolls with, publishes its screen rect and offsets for the input thread,
// and drops the cached element and its ViewChanged handler on close/teardown.
void CacheScrollableElement(bool settled);
void ReleaseScrollableElement();

// Defined with the UI-thread publication below. Recomputes the dismiss axis and
// the translation the composition expression bakes in, and rewrites the
// composition scalars when either moved.
void RefreshGestureGeometry();

// Defined with the input-thread gesture state below. The debug `retract` command
// clears it: the shell is about to hide the window, so nothing needs to restore
// `Progress`, and the next open restores it anyway.
extern bool g_commitNeedsProgressReset;

// Defined with the file paths below. A close stamps its retract delay there
// immediately before the Windows tap, so the bridge can hold the shell's Hide
// for exactly that long.
bool WriteCloseMode(uint64_t delayMs);

// Defined with the input-thread gesture state below; the close tap needs the
// kind for its log line.
const wchar_t* CloseKindName(CloseKind kind);

// ---------------------------------------------------------------------------
// Composition helpers
// ---------------------------------------------------------------------------

bool ShowAtRest() {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state) {
        return false;
    }

    try {
        g_state.StopAnimation(L"Progress");
        g_coasting.store(false, std::memory_order_relaxed);
        g_state.InsertScalar(L"Progress", 1.0f);
        g_progressNow.store(1.0f, std::memory_order_relaxed);
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

// Stops the cancel animation so a new drag can drive the scalar directly.
bool StopProgressAnimation() {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state) {
        return false;
    }

    try {
        g_state.StopAnimation(L"Progress");
        // A new drag owns the scalar from here: a coast still running belongs to
        // the gesture that just ended.
        g_coasting.store(false, std::memory_order_relaxed);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Stopping Progress failed: %08X %s", e.code(),
               e.message().c_str());
        return false;
    } catch (...) {
        Wh_Log(L"Stopping Progress failed with an unknown exception");
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
        // A direct insert is the new base value, so a coast still animating over
        // it ends here: the sample it belongs to has already been replaced.
        if (g_coasting.exchange(false, std::memory_order_relaxed)) {
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

// The fixed-curve shape of AnimateProgressTo, when the caller has no sampled
// polyline to hand it.
enum class ProgressCurve : int32_t {
    Retract = 0,      // the keyboard open: cubic-bezier(0.45, 0, 0.55, 1)
    Win11Settle = 1,  // the Windows 11 settle: cubic-bezier(0.1, 0.9, 0.2, 1)
};

// The one Progress animation in this file. It has two shapes:
//
// - the fixed eased pair for the keyboard open and the Windows 11 settle. The
//   easing sits on the keyframe at t=1, the one that defines the motion into
//   the target; the t=0 frame carries the start value only. Which curve is
//   attached comes from the caller: the keyboard open takes the retract curve,
//   the Windows 11 settle the Fluent decelerate curve.
// - a sampled polyline for the Windows 10 spring. Every keyframe is linear and
//   no easing function is attached, because the spring samples are the curve.
//   An easing function on a segment would zero or reshape the initial slope the
//   spring exists to keep.
//
// Every shape inserts the target as the base value *before* StartAnimation, so a
// cancelled or interrupted animation still lands on the intended end state
// instead of snapping back to the drag position.
bool AnimateProgressTo(float from, float to,
                       std::chrono::milliseconds duration,
                       const float* settleSamples = nullptr,
                       int settleSampleCount = 0,
                       ProgressCurve curve = ProgressCurve::Retract) {
    std::lock_guard lock(g_compositionMutex);
    if (!g_state || !g_compositor) {
        return false;
    }

    const bool polyline = settleSamples != nullptr && settleSampleCount > 1;
    const wuc::CompositionEasingFunction& easing =
        curve == ProgressCurve::Win11Settle ? g_win11SettleEasing
                                            : g_retractEasing;
    if (!polyline && !easing) {
        return false;
    }

    try {
        wuc::ScalarKeyFrameAnimation animation =
            g_compositor.CreateScalarKeyFrameAnimation();
        if (polyline) {
            // Equal steps across the duration; the first sample is the release
            // position and the last is the target.
            const float last = static_cast<float>(settleSampleCount - 1);
            for (int i = 0; i < settleSampleCount; ++i) {
                animation.InsertKeyFrame(static_cast<float>(i) / last,
                                         std::clamp(settleSamples[i], 0.0f,
                                                    1.0f));
            }
        } else {
            animation.InsertKeyFrame(0.0f, std::clamp(from, 0.0f, 1.0f));
            animation.InsertKeyFrame(1.0f, std::clamp(to, 0.0f, 1.0f),
                                     easing);
        }
        animation.Duration(duration);

        g_state.StopAnimation(L"Progress");
        g_coasting.store(false, std::memory_order_relaxed);
        g_state.InsertScalar(L"Progress", std::clamp(to, 0.0f, 1.0f));
        g_state.StartAnimation(L"Progress", animation);
        g_progressNow.store(std::clamp(to, 0.0f, 1.0f),
                            std::memory_order_relaxed);
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
// release speed on the gesture axis in DIP/ms. `duration` is the animation's
// length, which is also what the close tap is armed with.
//
// The style setting picks the mechanism:
//
// - Windows 11: one eased pair on the Fluent decelerate curve over a fixed
//   300 ms. It carries no release velocity: the motion starts at rest and
//   decelerates into the target.
// - Windows 10: the damped spring. Open and close are exact mirrors because it
//   works in normalized travel: y(0) = 0, y(inf) = 1, and y'(0) is the scaled
//   release speed divided by the travel left. The sign of the settle direction
//   cancels there, because (to - from) carries it; the spring always relaxes
//   toward its target. A release at rest gets y0 = 0, the plain step response.
//   A release faster than the remaining travel can absorb overshoots the
//   target, and the 0..1 clamp turns that overshoot into a plateau at the rest
//   position instead of a translation past it.
bool BeginGestureSettle(float from, float to, float velocity,
                        std::chrono::milliseconds& duration) {
    const float start = std::clamp(from, 0.0f, 1.0f);
    const float target = std::clamp(to, 0.0f, 1.0f);

    if (g_settleStyle.load(std::memory_order_relaxed) == SettleStyle::Win11) {
        duration = kWin11SettleDuration;
        if (!AnimateProgressTo(start, target, duration, nullptr, 0,
                               ProgressCurve::Win11Settle)) {
            return false;
        }

        FileLog(L"settle style=win11 dir=%s vel=%.2f dur=%llu",
                target >= start ? L"open" : L"close", velocity,
                static_cast<unsigned long long>(duration.count()));
        return true;
    }

    const float distance = g_distancePub.load(std::memory_order_relaxed);
    const float remaining =
        std::fabs(target - start) * std::max(distance, 0.0f);
    const float startScale =
        static_cast<float>(g_gestureStartSpeedPercent.load(
            std::memory_order_relaxed)) /
        100.0f;

    // Work in milliseconds throughout: `velocity` is DIP/ms, the duration is in
    // ms, and y0 is 1/ms. The period constant is seconds, so omega converts it
    // once here; keeping y0 in seconds would make the initial slope 1000x too
    // small and leave the seam without the finger's velocity.
    constexpr double kPi = 3.14159265358979323846;
    const double omega = 2.0 * kPi / (kSpringPeriodSeconds * 1000.0);  // 1/ms
    const double decay = kSpringDamping * omega;                       // 1/ms
    const double damped = omega * std::sqrt(1.0 - kSpringDamping * kSpringDamping);
    const double settleMs = kSettleTimeConstants / decay;
    const int64_t durationMs = std::clamp(
        static_cast<int64_t>(std::llround(settleMs)),
        static_cast<int64_t>(kSettleMinDuration.count()),
        static_cast<int64_t>(kSettleMaxDuration.count()));
    duration = std::chrono::milliseconds(durationMs);

    // Normalized initial slope, 1/ms. The sign cancels against (to - from), so
    // this is always positive: the spring leaves the release position toward
    // the target in both directions.
    const double y0 =
        remaining > 0.0f
            ? static_cast<double>(startScale * velocity) /
                  static_cast<double>(remaining)
            : 0.0;

    float samples[kSettleSampleCount] = {};
    for (int i = 0; i < kSettleSampleCount; ++i) {
        const double t = static_cast<double>(durationMs) * i /
                         static_cast<double>(kSettleSampleCount - 1);
        const double y =
            1.0 - std::exp(-decay * t) *
                      (std::cos(damped * t) +
                       ((decay - y0) / damped) * std::sin(damped * t));
        samples[i] = std::clamp(
            start + (target - start) * static_cast<float>(y), 0.0f, 1.0f);
    }

    if (!AnimateProgressTo(start, target, duration, samples,
                           kSettleSampleCount)) {
        return false;
    }

    FileLog(L"settle style=win10 dir=%s vel=%.2f dur=%llu",
            target >= start ? L"open" : L"close", velocity,
            static_cast<unsigned long long>(duration.count()));
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
    Sleep(60);
    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
}

const wchar_t* CloseKindName(CloseKind kind) {
    return kind == CloseKind::Dismiss ? L"dismiss" : L"reveal";
}

// The value a close plans from. The coast carried the panel past the last
// report, so neither gesture family reads its own last sample: the settle has
// to start where the panel is, and a coast must never keep running under it.
// One call for both, and it reads the one value every write to `Progress`
// records, so a pinned coast or a restored rest value cannot be missed.
float CoastedProgress() {
    float value =
        std::clamp(g_progressNow.load(std::memory_order_relaxed), 0.0f, 1.0f);
    if (g_coasting.load(std::memory_order_relaxed)) {
        StopCoast(value);
    }
    return value;
}

// The tail every close shares: the suppression window, the close-mode stamp the
// bridge reads, and the tap armed for the end of the settle. The dismiss and
// the reveal cancel both come through here, so the two families cannot diverge.
void ArmGestureClose(CloseKind kind, std::chrono::milliseconds duration) {
    g_suppressRestoreUntil.store(GetTickCount64() + kRestoreSuppressMs,
                                 std::memory_order_relaxed);
    if (!WriteCloseMode(kCloseTapDelayMs)) {
        FileLog(L"close mode write failed kind=%s", CloseKindName(kind));
    }
    ScheduleCloseTap(kind, static_cast<uint64_t>(duration.count()));
}

// The close every gesture path runs, from the value the coast left. A close
// exists only once its settle started: the suppression, the stamp and the tap
// follow that animation, never a failed one. The tap is armed for the end of the
// settle, so the panel is off screen when the shell hides the window. The
// dismiss wraps this call in its settle batch; the reveal cancel calls it
// directly.
bool BeginGestureClose(float from, float velocity, CloseKind kind) {
    std::chrono::milliseconds duration{0};
    if (!BeginGestureSettle(from, 0.0f, velocity, duration)) {
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

// The close both gesture paths schedule. The settle runs first; the tap is
// armed for its end, where RunPendingCloseTap checks the pull generation and
// either taps or drops the close. No deferral file and no bridge request exist.
//
// Committed dismiss: the settle batch observes the settle, the shared close
// tail arms the tap. The settle itself, the log line, the suppression window,
// the close-mode stamp and the tap all come from BeginGestureClose, so this path
// and the reveal cancel cannot diverge.
bool BeginDismissSettle(float from, float velocity) {
    wuc::CompositionScopedBatch batch{nullptr};
    uint32_t serial = 0;

    try {
        {
            std::lock_guard lock(g_compositionMutex);
            if (!g_state || !g_compositor) {
                return false;
            }

            serial = g_dismissSerial.fetch_add(1) + 1;

            batch = g_compositor.CreateScopedBatch(
                wuc::CompositionBatchTypes::Animation);
            batch.Completed(
                [serial](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuc::CompositionBatchCompletedEventArgs const&) {
                    try {
                        std::lock_guard lock(g_compositionMutex);

                        // The stop flag and the serial check guard the state
                        // update against unload and a new open.
                        if (g_stopDismissCallback.load(
                                std::memory_order_relaxed) ||
                            serial != g_dismissSerial.load(
                                          std::memory_order_relaxed)) {
                            return;
                        }

                        // The retract has reached 0. Leave Progress there:
                        // resetting it here flashes the fully open menu for the
                        // frames between this reset and the shell hiding the
                        // window. The next session resets it instead. The hide
                        // itself is the Windows tap's job, so this compositor
                        // callback only ends the settle.
                        if (g_state) {
                            g_state.StopAnimation(L"Progress");
                        }
                        g_dismissSettling.store(
                            false, std::memory_order_relaxed);
                    } catch (...) {
                        g_dismissSettling.store(
                            false, std::memory_order_relaxed);
                    }
                });

            g_dismissSettling.store(true, std::memory_order_relaxed);
        }

        if (!BeginGestureClose(from, velocity, CloseKind::Dismiss)) {
            // No animation, so the batch holds nothing: invalidate the
            // completion first, or its callback would touch a close that never
            // started to settle.
            {
                std::lock_guard lock(g_compositionMutex);
                g_dismissSerial.fetch_add(1, std::memory_order_relaxed);
                g_dismissSettling.store(false, std::memory_order_relaxed);
            }
            batch.End();
            return false;
        }

        // End the batch outside the lock: its completion handler takes the
        // same mutex.
        batch.End();
        {
            std::lock_guard lock(g_compositionMutex);
            if (serial == g_dismissSerial.load(std::memory_order_relaxed)) {
                g_dismissBatch = batch;
            }
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Dismiss animation failed: %08X %s", e.code(),
               e.message().c_str());
        g_dismissSettling.store(false, std::memory_order_relaxed);
        return false;
    } catch (...) {
        Wh_Log(L"Dismiss animation failed with an unknown exception");
        g_dismissSettling.store(false, std::memory_order_relaxed);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Debug command channel
//
// Kept in the shipping build. It applies one rendering mechanism at a time so
// the candidates can be compared without a gesture. The file is
//   C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\
//   local@start-menu-legacy-swipe\debug.txt
// which is the directory Wh_GetModStoragePath returns. One command per line:
//   p=<progress>        InsertProgress, clamped to 0..1
//   t=<x>,<y>,<z>       UIElement.Translation, applied on the UI thread (DIP)
//   e=<x>[,<y>]         visual Properties["Offset"] and Visual.Offset (DIP)
// The raw input thread polls the file every 50 ms and re-reads it only when
// its write time or size changes. Nothing is logged per poll.
// ---------------------------------------------------------------------------

constexpr wchar_t kDebugFileName[] = L"debug.txt";
constexpr DWORD kDebugFileMaxBytes = 4096;

// The explorer-side bridge must know whether a Hide is the Start side's own tap
// or a keyboard close. A gesture close stamps `gesture=0` at its release; its tap
// comes after the retract, so the bridge passes that Hide at once. A close
// dropped before its tap stamps `cancelled`, which the bridge cannot parse and
// treats as a keyboard close, so that Hide still gets the 350 ms retract lead.
// Only a file younger than 800 ms is used; `CREATE_ALWAYS` creates and
// overwrites it.
constexpr wchar_t kCloseModeFileName[] = L"close-mode.txt";
// Written when a close is dropped before its tap: the stamp is fresh but names
// no gesture close, so the next Hide takes the keyboard path.
constexpr char kCloseModeCancelled[] = "cancelled\r\n";

// `retract` is a live signal from the explorer-side bridge, not a state: the
// bridge writes it right before it delays the hide. The file keeps the last
// command after the close, so a fresh Start process would otherwise replay it
// on attach and retract the menu that just opened. One second is far more than
// the 350 ms lead and the poll that reads it.
constexpr uint64_t kDebugCommandFreshMs = 1000;

struct DebugFileStamp {
    bool seen = false;
    DWORD writeLow = 0;
    DWORD writeHigh = 0;
    LONGLONG size = 0;
};

DebugFileStamp g_debugFileStamp;  // input thread only

// One path builder for both files in the mod storage directory.
bool StorageFilePath(const wchar_t* name, WCHAR* path, size_t count) {
    WCHAR storage[MAX_PATH] = {};
    if (!Wh_GetModStoragePath(storage, ARRAYSIZE(storage))) {
        return false;
    }

    return swprintf_s(path, count, L"%s\\%s", storage, name) > 0;
}

// Writes one marker into the mod storage directory with `CREATE_ALWAYS`, which
// creates the file and touches its last-write time. CloseHandle flushes the
// cached timestamp, which is what GetFileAttributesExW reports on the other
// side. The close mode's content is read by the bridge; the debug command is
// timestamp-only.
bool WriteStorageMarker(const wchar_t* name, const char* text, size_t length) {
    WCHAR path[MAX_PATH] = {};
    if (!StorageFilePath(name, path, ARRAYSIZE(path))) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(file, text, static_cast<DWORD>(length), &written,
                              nullptr);
    CloseHandle(file);
    return ok && written == length;
}

// Stamps the close mode for the explorer-side bridge. Called on the input thread
// right before a gesture close taps the Windows key; the write has to finish
// before the tap, because the shell can call Hide a few milliseconds after it.
// The value is the tap's own delay (0: the settle has already retracted the
// panel), so the bridge passes that Hide at once. A failed write only costs an
// early 350 ms keyboard lead on the bridge side, which still covers the settle.
bool WriteCloseMode(uint64_t delayMs) {
    char text[32] = {};
    const int length = std::snprintf(text, sizeof(text), "gesture=%llu\r\n",
                                     static_cast<unsigned long long>(delayMs));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(text)) {
        return false;
    }

    return WriteStorageMarker(kCloseModeFileName, text,
                              static_cast<size_t>(length));
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
    wucore::CoreDispatcher dispatcher = g_dispatcher;
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

void ApplyDebugLine(const char* line, bool fresh) {
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

    // The explorer-side bridge writes `retract=1` right before it delays
    // CStartExperienceManager::Hide. The close is already decided, so the panel
    // slides out on the same settle a committed dismiss uses while the shell
    // waits. The value after `=` is ignored today; the bridge may carry the lead
    // time there later. It only ever writes this for a close the mod did not
    // start itself (see close-mode.txt); a gesture close taps the Windows key
    // and stamps its own delay instead.
    if (std::strncmp(line, "retract", 7) == 0) {
        if (!fresh) {
            // A leftover command from an earlier close: the panel it belonged to
            // is already gone, and the menu that is on screen now must stay.
            FileLog(L"debug retract ignored (stale)");
            return;
        }

        const float from =
            std::clamp(g_progressNow.load(std::memory_order_relaxed), 0.0f,
                       1.0f);
        // The keyboard close has no release speed of its own, so it settles
        // from rest: the spring's plain step response, or the Windows 11 curve,
        // which always starts at rest.
        std::chrono::milliseconds duration{0};
        if (BeginGestureSettle(from, 0.0f, 0.0f, duration)) {
            // The window is about to be hidden: the next open restores Progress
            // by itself, so no restore is owed after this retract.
            g_commitNeedsProgressReset = false;
            FileLog(L"debug retract from=%.3f dur=%llu", from,
                    static_cast<unsigned long long>(duration.count()));
        }
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

void ApplyDebugCommands(char* text, bool fresh) {
    char* cursor = text;
    while (*cursor != '\0') {
        char* newline = std::strchr(cursor, '\n');
        if (newline) {
            *newline = '\0';
        }
        ApplyDebugLine(cursor, fresh);
        if (!newline) {
            return;
        }
        cursor = newline + 1;
    }
}

// Reads the command file only when its write time or size changed, and only
// after the renderer exists: before the first Setup neither mechanism has an
// object to act on.
void PollDebugFile() {
    if (g_unloading.load(std::memory_order_relaxed) ||
        !g_hasRoot.load(std::memory_order_relaxed)) {
        return;
    }

    WCHAR path[MAX_PATH] = {};
    if (!StorageFilePath(kDebugFileName, path, ARRAYSIZE(path))) {
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        // A missing file is not an error. Forget the stamp so a recreated file
        // is read even when it keeps the old size.
        g_debugFileStamp = DebugFileStamp{};
        return;
    }

    const LONGLONG size =
        (static_cast<LONGLONG>(attributes.nFileSizeHigh) << 32) |
        static_cast<LONGLONG>(attributes.nFileSizeLow);

    if (g_debugFileStamp.seen &&
        attributes.ftLastWriteTime.dwLowDateTime ==
            g_debugFileStamp.writeLow &&
        attributes.ftLastWriteTime.dwHighDateTime ==
            g_debugFileStamp.writeHigh &&
        size == g_debugFileStamp.size) {
        return;
    }

    g_debugFileStamp.seen = true;
    g_debugFileStamp.writeLow = attributes.ftLastWriteTime.dwLowDateTime;
    g_debugFileStamp.writeHigh = attributes.ftLastWriteTime.dwHighDateTime;
    g_debugFileStamp.size = size;

    // Age of this write, so a live `retract` is told apart from one the file
    // kept from an earlier close.
    ULARGE_INTEGER written{};
    written.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    written.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    ULARGE_INTEGER now{};
    now.LowPart = nowFileTime.dwLowDateTime;
    now.HighPart = nowFileTime.dwHighDateTime;
    const bool fresh =
        now.QuadPart >= written.QuadPart &&
        now.QuadPart - written.QuadPart <= kDebugCommandFreshMs * 10000ULL;

    if (size <= 0 || size > kDebugFileMaxBytes) {
        return;
    }

    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    char buffer[kDebugFileMaxBytes + 1] = {};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buffer, kDebugFileMaxBytes, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) {
        return;
    }
    buffer[read] = '\0';

    // An editor may prepend a UTF-8 BOM; skip it so the first line parses.
    char* text = buffer;
    if (read >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text += 3;
    }

    ApplyDebugCommands(text, fresh);
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
    USHORT contactNodes[kMaxContactCollections] = {};
    int contactNodeCount = 0;
    USHORT contactCountLink = 0;
    bool hasContactCount = false;
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

    // Contact collections are 0x0D/0x05 on classic digitizers and 0x0D/0x22
    // (Finger) on the ELAN2513. Both carry X/Y per contact.
    HIDP_LINK_COLLECTION_NODE nodes[kMaxLinkNodes]{};
    ULONG nodeCount = kMaxLinkNodes;
    if (HidP_GetLinkCollectionNodes(nodes, &nodeCount, device.preparsed) ==
        HIDP_STATUS_SUCCESS) {
        if (nodeCount > kMaxLinkNodes) {
            nodeCount = kMaxLinkNodes;
        }

        for (ULONG i = 0; i < nodeCount; ++i) {
            if (nodes[i].LinkUsagePage != kUsagePageDigitizer ||
                (nodes[i].LinkUsage != kUsageContact &&
                 nodes[i].LinkUsage != kUsageFinger)) {
                continue;
            }
            if (device.contactNodeCount < kMaxContactCollections) {
                device.contactNodes[device.contactNodeCount++] =
                    static_cast<USHORT>(i);
            }
        }
    }

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

bool ReadUsageValue(const HidDevice& device, USAGE usagePage,
                    USHORT collection, USAGE usage, const BYTE* report,
                    ULONG reportLength, ULONG* value) {
    if (!device.preparsed) {
        return false;
    }

    ULONG parsed = 0;
    const NTSTATUS status = HidP_GetUsageValue(
        HidP_Input, usagePage, collection, usage, &parsed, device.preparsed,
        reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)), reportLength);
    if (status != HIDP_STATUS_SUCCESS) {
        return false;
    }

    *value = parsed;
    return true;
}

struct HidAxisRange {
    long min = 0;
    long max = 0;
    bool valid = false;
};

// Selects the value cap that describes this report. reportId is the HID
// report ID from RAWINPUTHEADER.wParam; 0 means the device has no report IDs.
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
        if (cap.ReportID != 0 && reportId != 0 && cap.ReportID != reportId) {
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

// Whether a vertical dismiss may start here. A vertical drag competes with the
// content's own scrolling: dragging toward the taskbar is how the app list
// scrolls back, and a dismiss started on that drag closed the menu instead.
// Measured on this build, the list is at VerticalOffset 0 on every open and moves
// exactly with its content, so the rule is the scroll position, not a guess at
// the user's intent:
//
// - the press is outside the cached scroller (tiles, rail, panel background) or
//   nothing was measured: the drag has no content to scroll, allow it;
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
bool VerticalDismissAllowed(int32_t axis, float x, float y, uint64_t tickMs) {
    if (IsHorizontalAxis(axis)) {
        return true;
    }

    ScrollerCache scroller{};
    {
        std::lock_guard lock(g_scrollerMutex);
        if (!g_scroller.valid || !g_scroller.settled) {
            // Nothing measured for this open, or only the transitional rect
            // seen while the shell was still moving the window. Failing open
            // here let a swipe right after the open dismiss the menu while the
            // user was scrolling; wait for the measurement instead, but only
            // while the press is inside the panel band so a press on the
            // desktop still passes.
            const float trail = g_panelTrailDip.load(std::memory_order_relaxed);
            const float distance = g_distancePub.load(std::memory_order_relaxed);
            const bool insidePanel =
                trail > 0.0f && distance > 0.0f &&
                (axis == static_cast<int32_t>(DismissAxis::Bottom)
                     ? (y >= trail - kPanelBandSlopDip &&
                        y <= trail + distance)
                     : (y <= trail + kPanelBandSlopDip &&
                        y >= trail - distance));
            return !insidePanel;
        }
        scroller = g_scroller;
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
bool BeginEdgeCandidate(float x, float y, uint64_t tickMs) {
    if (!g_hasRoot.load(std::memory_order_relaxed)) {
        return false;
    }

    const float distance = g_distancePub.load(std::memory_order_relaxed);
    if (!(distance > 0.0f)) {
        return false;  // renderer not ready: no distance to follow
    }

    const int32_t axis = g_axisPub.load(std::memory_order_relaxed);
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

    // The new pull owns the panel from this report on. It starts a new pull
    // generation; a close tap checks it once and is skipped while this pull
    // exists, so the shell never hides the menu the finger is pulling out.
    g_pullSerial++;
    return true;
}

// One reveal step. While the menu is still closed the contact is only
// remembered; the moment it is open under the contact, the follow takes
// Progress over and every later report drives it.
void UpdateRevealSession(float x, float y, uint64_t tickMs) {
    HidRevealSession& reveal = g_reveal;
    const uint64_t sampleQpc = QpcMsNow();
    reveal.lastTick = tickMs;
    reveal.lastSampleQpc = sampleQpc;

    if (!reveal.active) {
        if (!StartWindowIsOpenThrottled(tickMs)) {
            return;
        }

        const float distance = g_distancePub.load(std::memory_order_relaxed);
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
        reveal.lastInserted = 1.0f;  // the menu is fully in right now
        reveal.samples = 0;

        // Diagnostic: what Progress the takeover replaces, so a reopen backlash
        // can be traced to a value left behind or to an animation still running.
        {
            float current = -1.0f;
            if (g_state) {
                g_state.TryGetScalar(L"Progress", current);
            }
            FileLog(L"reveal takeover from=%.3f", current);
        }

        // The takeover inserts the mapped progress (0.269 at the screen edge on
        // this layout) after the menu was at 1, so the first step of the follow
        // must not become the coast speed.
        ResetCoastVelocity();

        // A cancel or dismiss animation from a previous gesture may still own
        // Progress; the follow must drive the scalar from this sample on. A
        // close tap that is still ahead is not touched here: it re-checks the
        // pull generation once, so a pull that began first drops the close.
        StopProgressAnimation();
    }

    const int32_t axis = g_axisPub.load(std::memory_order_relaxed);
    const float finger = AxisScreenPosition(axis, x, y);
    // Decision quantity: how far the finger moved away from the taskbar edge
    // since the touch began. Signed by the opening direction and clamped at 0,
    // so a pull back toward the edge reads 0 and cannot commit.
    reveal.travel = std::max(
        0.0f, (finger - AxisScreenPosition(axis, reveal.edgeStartX,
                                           reveal.edgeStartY)) *
                  AxisOpeningSign(axis));
    reveal.maxTravel = std::max(reveal.maxTravel, reveal.travel);

    // The trail is published by the UI thread before the root opens. A zero
    // value means the renderer is between roots; the root extent keeps the
    // follow monotonic instead of dividing by a stale or missing edge.
    float panelTrail = g_panelTrailDip.load(std::memory_order_relaxed);
    if (!(panelTrail > 0.0f)) {
        panelTrail = reveal.distance;
    }
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
}

// Release of the edge contact. A candidate that never saw the menu open ends
// silently: it did not touch Progress. A reveal that ran logs exactly one line.
//
// The decision mirrors ReleaseHidSession: it judges the travel at the moment of
// release, not the mapped progress. Progress reads 0.269 at the screen edge on
// this layout, so a progress threshold would commit a pull-out that never left
// the edge and a pull out and back alike.
void ReleaseRevealSession() {
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

    // The release arrives from the idle timer, so elapsed stops at the last
    // observed sample; adding the idle wait would dilute a quick flick.
    const uint64_t elapsed = reveal.lastTick > reveal.startTick
                                 ? reveal.lastTick - reveal.startTick
                                 : 0;
    const float travel = std::max(0.0f, reveal.travel);
    const float velocity =
        travel / static_cast<float>(elapsed > 0 ? elapsed : 1);
    const float distance = reveal.distance;
    const float progress = std::clamp(reveal.progress, 0.0f, 1.0f);
    const float threshold = IsHorizontalAxis(
                                g_axisPub.load(std::memory_order_relaxed))
                                ? kCommitDistanceFraction
                                : kCommitDistanceFractionVertical;
    const bool commit = distance > 0.0f &&
                        (travel > threshold * distance ||
                         velocity > kCommitVelocity);

    // The release line is written after the animation starts: the file write
    // costs time on the input thread, and every millisecond before the
    // animation is a frame the panel stands still at the seam.
    EndContactTiming();

    // The coast carried the panel past the last report, so both outcomes start
    // from where it got to. `progress` above stays the mapped release position,
    // which is the measurement, not the point the animation starts from.
    const float from = CoastedProgress();

    if (commit) {
        // Let go past the commit point: the menu stays open and the follow
        // finishes the slide instead of snapping.
        const bool started = BeginGestureOpen(from, velocity);
        FileLog(L"open follow travel=%.0f max=%.0f progress=%.2f vel=%.2f "
                L"commit=%d panel=%.0f n=%d",
                travel, reveal.maxTravel, progress, velocity, commit ? 1 : 0,
                reveal.panelTrail, reveal.samples);
        if (!started) {
            ShowAtRest();
        }
        return;
    }

    // Short reveal: the same settle, the same log line, the same close-mode
    // stamp, the same suppression window and the same armed tap as the
    // committed dismiss, from the same release speed. Both close paths read
    // identically from any finger position.
    //
    // Progress stays at 0 afterwards and is not restored here: the hide takes a
    // moment, and restoring now would flash the fully open menu while the close
    // is in flight. The flag makes the next down restore the resting value once
    // the shell has finished hiding the reused root.
    const bool started =
        BeginGestureClose(from, velocity, CloseKind::RevealCancel);
    FileLog(L"open follow travel=%.0f max=%.0f progress=%.2f vel=%.2f "
            L"commit=%d panel=%.0f n=%d",
            travel, reveal.maxTravel, progress, velocity, commit ? 1 : 0,
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

void BeginHidSession(float x, float y, uint64_t tickMs) {
    // The previous commit is still sliding out; the shell's Hide for it is still
    // ahead, so this down belongs to the same menu.
    if (g_dismissSettling.load(std::memory_order_relaxed)) {
        if (tickMs > g_skipLogTick + 1000) {
            g_skipLogTick = tickMs;
            FileLog(L"dismiss skipped settling");
        }
        return;
    }

    const float distance = g_distancePub.load(std::memory_order_relaxed);
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
    if (!g_hasRoot.load(std::memory_order_relaxed) ||
        !StartWindowIsOpenThrottled(tickMs)) {
        if (tickMs > g_skipLogTick + 1000) {
            g_skipLogTick = tickMs;
            FileLog(L"dismiss skipped closed root=%d",
                    g_hasRoot.load(std::memory_order_relaxed) ? 1 : 0);
        }
        return;
    }

    const int32_t axis = g_axisPub.load(std::memory_order_relaxed);
    // A vertical drag on the app list belongs to the list while it can still
    // scroll that way. The rejection starts no session, and this function runs
    // again on the next sample of the same contact, so a drag that scrolls the
    // list to its end still turns into a dismiss without a new contact. The
    // contact latch itself is left untouched: it only tracks that a finger is
    // down, and the lift path clears it, so the next down is unaffected.
    if (!VerticalDismissAllowed(axis, x, y, tickMs)) {
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
    session.lastProgress = 1.0f;
    session.samples = 1;
    BeginContactTiming();
    // The speed of the previous gesture must not survive into this one.
    ResetCoastVelocity();

    // A cancel animation from the previous gesture may still be running;
    // stop it so this drag owns Progress. After a commit the base value can
    // still be 0, because the settle callback may not have run yet, so restore
    // the resting value before the first sample drives the scalar. The restore
    // is skipped while a close this mod started is still in flight: inserting 1
    // there flashed the fully open panel until the shell hid the window. The
    // flag stays set so the next open restores the scalar.
    StopProgressAnimation();
    if (g_commitNeedsProgressReset &&
        tickMs >= g_suppressRestoreUntil.load(std::memory_order_relaxed)) {
        g_commitNeedsProgressReset = false;
        InsertProgress(1.0f);
    }

    FileLog(L"hid down x=%.1f y=%.1f axis=%d dist=%.0f", x, y, axis,
            distance);
}

void ReleaseHidSession(bool allowCommit, const wchar_t* source) {
    HidGestureSession& session = g_hidSession;
    if (!session.active) {
        return;
    }

    // Measure the gesture up to the last observed sample. The release itself
    // arrives from an idle timer, so using it here would add the whole idle
    // wait to the elapsed time and dilute the velocity of a quick flick.
    const uint64_t lastSample = session.lastTick;
    const uint64_t elapsed =
        lastSample > session.startTick ? lastSample - session.startTick : 0;
    // Judge on where the finger actually let go, not on how far it once went:
    // dragging out and back in must cancel, not dismiss.
    const float finalDelta = std::max(0.0f, session.curDelta);
    const float velocity = finalDelta / static_cast<float>(elapsed > 0 ? elapsed : 1);
    const float distance = g_distancePub.load(std::memory_order_relaxed);
    const float threshold = IsHorizontalAxis(
                                g_axisPub.load(std::memory_order_relaxed))
                                ? kCommitDistanceFraction
                                : kCommitDistanceFractionVertical;
    const bool commit =
        allowCommit && distance > 0.0f &&
        (finalDelta > threshold * distance || velocity > kCommitVelocity);

    const uint64_t releaseQpc = QpcMsNow();
    EndContactTiming();

    // The coast continued the drag after the last report; the close has to start
    // from where it got to, not from the last report, or the panel jumps back
    // before it settles. Both gesture families read it through the same call.
    const float from = CoastedProgress();

    if (commit) {
        const bool started = BeginDismissSettle(from, velocity);
        // The release line is written after the settle started: the file write
        // costs time on the input thread, and every millisecond before the
        // animation is a frame the panel stands still at the seam.
        LogHidRelease(session, velocity, commit, releaseQpc, source);
        if (started) {
            g_commitNeedsProgressReset = true;
        } else {
            ShowAtRest();
        }
        session.active = false;
        return;
    }

    session.active = false;
    if (distance > 0.0f) {
        BeginGestureOpen(from, velocity);
    } else {
        ShowAtRest();
    }
    LogHidRelease(session, velocity, commit, releaseQpc, source);
}

// Scroll abort: the menu returns to its resting position at once; no
// animation and no dismissal.
void AbortHidSession(uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;
    if (!session.active) {
        return;
    }

    const uint64_t elapsed =
        tickMs > session.startTick ? tickMs - session.startTick : 0;
    const float velocity =
        session.maxDelta / static_cast<float>(elapsed > 0 ? elapsed : 1);

    LogHidRelease(session, velocity, false, QpcMsNow(), L"abort");
    session.active = false;
    EndContactTiming();
    ShowAtRest();
}

void UpdateHidSession(float x, float y, uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;

    const float distance = g_distancePub.load(std::memory_order_relaxed);
    if (!(distance > 0.0f)) {
        AbortHidSession(tickMs);
        return;
    }

    // A report ends the coast: pin where it got to, then insert the real value
    // below even when it is inside the 0.5 DIP filter, so the panel returns to
    // the finger at once. The correction is only the few DIP it drifted.
    float coastValue = session.lastProgress;
    const bool resumed =
        g_coasting.load(std::memory_order_relaxed) && StopCoast(coastValue);

    const int32_t axis = g_axisPub.load(std::memory_order_relaxed);
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
        AbortHidSession(tickMs);
        return;
    }

    const float progress =
        std::clamp(1.0f - delta / distance, 0.0f, 1.0f);
    SampleCoastVelocity(progress, sampleQpc);
    if ((delta > 0.0f || session.lastProgress < 1.0f) &&
        (resumed ||
         std::fabs(progress - session.lastProgress) * distance >=
             kProgressEpsilonDip)) {
        InsertProgress(progress);
        session.lastProgress = progress;
    }
}

// One pure state-machine step, called on the input thread only. It never
// touches a XAML object; composition goes through the mutex-guarded helpers.
void OnHidSample(int contacts, float xDip, float yDip, uint64_t tickMs) {
    HidGestureSession& session = g_hidSession;

    // Extra contacts (palm, ghost slot) are filtered when the report is
    // decoded; by the time a sample reaches here it always means "the tracked
    // finger is present".
    if (contacts <= 0) {
        if (session.active) {
            ReleaseHidSession(true, L"report");
        }
        session.suppress = false;
        ReleaseRevealSession();
        g_contactDown = false;
        g_openCheckValid = false;
        return;
    }

    if (session.suppress) {
        return;
    }

    if (session.active) {
        UpdateHidSession(xDip, yDip, tickMs);
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
        BeginEdgeCandidate(xDip, yDip, tickMs);
    }
    if (g_reveal.edgeCandidate) {
        UpdateRevealSession(xDip, yDip, tickMs);
        return;
    }

    if (!PointInStartWindow(xDip, yDip)) {
        return;
    }
    BeginHidSession(xDip, yDip, tickMs);
}

// The coast through the window between the last report and the release. Only the
// idle tick runs while the stream is silent, so it starts the coast, and it is
// also the tick that ends the session 10-15 ms later: that release stops the
// coast and plans the close from its value.
void MaybeCoast(uint64_t tickMs, uint64_t qpcNow) {
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

    const float distance = g_distancePub.load(std::memory_order_relaxed);
    if (!(distance > 0.0f)) {
        return;
    }

    // The reveal maps Progress from the panel trail, which the UI thread
    // publishes when the root opens. The first 50 ms of the follow settle.
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

    // First, before any release is decided. The coast has to be running when the
    // release is planned, and the release is the very next thing this tick does.
    // On a clean 5 ms tick the coast starts 8-13 ms after the last report and the
    // release lands 10-16 ms later; a coalesced tick, which a busy input thread
    // produces, could otherwise cross both thresholds at once and leave the
    // panel standing still for the whole gap. Starting it here makes that tick
    // start the coast and then hand it to the release, which stops it and settles
    // from its value, exactly as on the clean path.
    MaybeCoast(tickMs, qpcNow);

    // The one-shot settle: the layout watch needs a second layout pass, and one
    // may never come after the open storyboard finishes.
    const uint64_t settleDue = g_settleDueTick.load(std::memory_order_relaxed);
    if (settleDue != 0 && tickMs >= settleDue) {
        g_settleDueTick.store(0, std::memory_order_relaxed);
        PostUiSettledMeasurement(g_settleTrailIdle);
    }

    const bool sessionIdle =
        session.active && qpcNow >= session.lastSampleQpc &&
        qpcNow - session.lastSampleQpc >= kIdleReleaseMs;
    if (sessionIdle) {
        ReleaseHidSession(true, L"idle");
    }

    const bool revealIdle =
        g_reveal.edgeCandidate && qpcNow >= g_reveal.lastSampleQpc &&
        qpcNow - g_reveal.lastSampleQpc >= kIdleReleaseMs;
    if (revealIdle) {
        ReleaseRevealSession();
    }

    // A contact that holds no session is not tracked per report, so the same
    // silence that ends a gesture ends the "this contact already started"
    // latch. Without it a lost lift would swallow the next edge swipe.
    if (!session.active && !g_reveal.edgeCandidate && g_contactDown &&
        tickMs > g_contactTick && tickMs - g_contactTick >= kIdleReleaseMs) {
        g_contactDown = false;
        g_openCheckValid = false;
    }
}

// The tap both close paths arm. It runs on the input thread at the end of the
// retract. A pull that started after the release owns the menu again, so the tap
// is dropped and no Hide ever reaches the shell; the stamp is overwritten with a
// value the bridge cannot parse, so a later keyboard close takes its own lead.
void RunPendingCloseTap(uint64_t tickMs) {
    if (g_pendingTapTick == 0 || tickMs < g_pendingTapTick) {
        return;
    }

    const CloseKind kind = g_pendingTapKind;
    const uint32_t pull = g_pendingTapPull;
    g_pendingTapTick = 0;
    g_pendingTapKind = CloseKind::None;

    if (g_pullSerial != pull || g_reveal.active || g_reveal.edgeCandidate) {
        if (!WriteStorageMarker(kCloseModeFileName, kCloseModeCancelled,
                                sizeof(kCloseModeCancelled) - 1)) {
            FileLog(L"close mode cancel write failed");
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

    TapWindowsKey();
    FileLog(L"close tap kind=%s delay=%llu", CloseKindName(kind),
            static_cast<unsigned long long>(kCloseTapDelayMs));
}

// Posts the gesture-geometry refresh to the UI thread. The XAML root and the
// property set are UI-thread objects, so the input thread only dispatches the
// work and keeps reading the published atomics. The refresh is event driven: it
// runs when the menu opens and when the system announces a work-area or display
// change, never on a timer.
//
// `trailIdle` is decided here, on the thread that owns the gesture state: a live
// follow reads the panel edge every sample, so the edge must not be re-measured
// under the finger. A root that is not attached is skipped inside the refresh.
// One-shot, 400 ms after an open: XAML may not produce a second LayoutUpdated
// while nothing moves, so the layout watch alone would leave the scroller cache
// unarmed until the user touches the menu.
void MeasureSettledNow(bool trailIdle) {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return;
    }
    try {
        if (trailIdle) {
            RefreshGestureGeometry();
            PublishPanelTrailDip(true);
        }
        CacheScrollableElement(true);
    } catch (...) {
        // A closing menu must not break the settle.
    }
}

void PostUiSettledMeasurement(bool trailIdle) {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return;
    }
    wucore::CoreDispatcher dispatcher = g_dispatcher;
    if (!dispatcher) {
        return;
    }
    try {
        dispatcher.TryRunAsync(wucore::CoreDispatcherPriority::Normal,
                               [trailIdle]() { MeasureSettledNow(trailIdle); });
    } catch (...) {
    }
}
void PostUiGeometryRefresh(bool trailIdle) {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return;
    }

    wucore::CoreDispatcher dispatcher = g_dispatcher;
    if (!dispatcher) {
        return;
    }

    try {
        dispatcher.TryRunAsync(
            wucore::CoreDispatcherPriority::Normal,
            [trailIdle]() {
                try {
                    if (g_unloading.load(std::memory_order_relaxed)) {
                        return;
                    }
                    RefreshGestureGeometry();
                    if (trailIdle) {
                        PublishPanelTrailDip(true);
                    }
                    // The scroller cache must settle even when a reveal owns
                    // the open: without it every vertical dismiss waits for a
                    // measurement that never comes.
                    ArmSettledTrailWatch(trailIdle);
                    // The layout watch needs a second pass that may never come;
                    // arm a single 400 ms settle as the guaranteed path.
                    g_settleTrailIdle = trailIdle;
                    g_settleDueTick.store(GetTickCount64() + 400,
                                          std::memory_order_relaxed);
                    {
                        // The dismiss gate needs the scrolling element of this
                        // open; the settled pass measures it again, when the
                        // list has its final extent.
                        CacheScrollableElement(false);
                    }
                } catch (...) {
                    // Never let an exception escape the dispatcher callback.
                }
            });
    } catch (winrt::hresult_error const& e) {
        FileLog(L"gesture geometry queue err=%08X",
                static_cast<unsigned>(e.code()));
    } catch (...) {
        FileLog(L"gesture geometry queue err=unknown");
    }
}

// Posted by the input thread when the shell reports the window cloaked. Both
// watches belong to the open that just ended: the settled-trail watch would
// measure a layout of a menu that is no longer on screen, and a cached scroller
// would gate the next open against the old rect and offsets. The UI thread
// releases them.
void PostUiOpenWatchRelease() {
    if (g_unloading.load(std::memory_order_relaxed)) {
        return;
    }

    wucore::CoreDispatcher dispatcher = g_dispatcher;
    if (!dispatcher) {
        return;
    }

    try {
        dispatcher.TryRunAsync(
            wucore::CoreDispatcherPriority::Normal, []() {
                try {
                    if (g_unloading.load(std::memory_order_relaxed)) {
                        return;
                    }
                    StopSettledTrailWatch();
                    ReleaseScrollableElement();
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
// gesture open does. A close the shell performs by itself (Windows key) is not
// fought: nothing is inserted, and the next open starts from 0 again.
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
        if (!closeInFlight && !gestureOwnsProgress &&
            AnimateProgressTo(0.0f, 1.0f, kOpenSlideDuration)) {
            // The animation now owns Progress, so no later path needs to
            // restore the resting value for this open.
            g_commitNeedsProgressReset = false;
            FileLog(L"win open");
        }

        // One refresh per open, before the first dismiss of this open can start.
        // Re-measuring the trail is skipped while a gesture owns the panel: the
        // follow already reads the edge every sample.
        PostUiGeometryRefresh(!closeInFlight && !g_hidSession.active &&
                              !g_reveal.active);
    } else if (!open && g_rootWasOpen) {
        // The close has landed, so its suppression no longer applies. Without
        // this a Win press right after the hide opened the window with Progress
        // still at 0, and the panel was invisible until the next open.
        g_suppressRestoreUntil.store(0, std::memory_order_relaxed);
        FileLog(L"win close observed");
        // The layout watch and the cached scroller belong to the open that just
        // ended; the UI thread releases both.
        PostUiOpenWatchRelease();
    }

    g_rootWasOpen = open;
}

void FeedHidReport(HidDevice& device, USHORT reportId, const BYTE* report,
                   ULONG reportLength, uint64_t tickMs) {
    if (!device.ready || !device.preparsed) {
        LogDecodeFail(device, 0);
        return;
    }

    USHORT collections[kMaxContactCollections] = {};
    int collectionCount = 0;
    if (device.contactNodeCount > 0) {
        collectionCount = device.contactNodeCount;
        for (int i = 0; i < collectionCount; ++i) {
            collections[i] = device.contactNodes[i];
        }
    } else {
        collectionCount = 1;
        collections[0] = 0;
    }

    int nonZero = 0;
    int chosen = -1;
    long chosenX = 0;
    long chosenY = 0;
    long bestDistance = 0;

    // A precision touchscreen reports one collection per contact slot. Extra
    // contacts (palm, a ghost slot) must not cancel the gesture, so pick the
    // collection nearest the position the tracked finger last had instead of
    // just the first non-zero one.
    for (int i = 0; i < collectionCount; ++i) {
        ULONG x = 0;
        ULONG y = 0;
        const bool haveX = ReadUsageValue(device, kUsagePageGeneric,
                                          collections[i], kUsageX, report,
                                          reportLength, &x);
        const bool haveY = ReadUsageValue(device, kUsagePageGeneric,
                                          collections[i], kUsageY, report,
                                          reportLength, &y);
        if (!haveX || !haveY) {
            continue;
        }
        if (x == 0 && y == 0) {
            continue;
        }
        nonZero++;

        if (!g_hidSession.active) {
            if (chosen < 0) {
                chosen = i;
                chosenX = static_cast<long>(x);
                chosenY = static_cast<long>(y);
            }
            continue;
        }

        const long dx = static_cast<long>(x) - g_hidSession.lastRawX;
        const long dy = static_cast<long>(y) - g_hidSession.lastRawY;
        const long distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (chosen < 0 || distance < bestDistance) {
            chosen = i;
            chosenX = static_cast<long>(x);
            chosenY = static_cast<long>(y);
            bestDistance = distance;
        }
    }

    const bool primaryValid = chosen >= 0;
    const long primaryX = chosenX;
    const long primaryY = chosenY;

    ULONG topLevelCount = 0;
    bool haveCount = false;
    if (device.hasContactCount) {
        haveCount = ReadUsageValue(device, kUsagePageDigitizer,
                                   device.contactCountLink,
                                   kUsageContactCount, report, reportLength,
                                   &topLevelCount);
    }

    int contacts = 0;
    if (haveCount) {
        contacts = static_cast<int>(topLevelCount);
    } else {
        // No top-level Contact Count: contact buttons are authoritative,
        // otherwise count the collections that carry a position. A readable
        // but all-zero collection 0 is a lift, not a contact.
        ULONG tipSwitch = 0;
        ULONG inRange = 0;
        const bool haveTip =
            ReadUsageValue(device, kUsagePageDigitizer, collections[0],
                           kUsageTipSwitch, report, reportLength, &tipSwitch);
        const bool haveInRange =
            ReadUsageValue(device, kUsagePageDigitizer, collections[0],
                           kUsageInRange, report, reportLength, &inRange);
        if (haveInRange) {
            contacts = inRange ? 1 : 0;
        } else if (haveTip) {
            contacts = tipSwitch ? 1 : 0;
        } else {
            contacts = nonZero > 0 ? nonZero : 0;
        }
    }

    if (haveCount && contacts == 0) {
        // Contact Count is authoritative: on a lift report the position fields
        // often keep their last value, so trusting them would hide the lift and
        // force the idle timer to guess.
        OnHidSample(0, 0.0f, 0.0f, tickMs);
        return;
    }

    if (contacts <= 0) {
        // No Contact Count to trust: a report with no usable collection is a
        // lift, and a count above our tracked contact just means an extra
        // finger, which must not end the gesture.
        if (!primaryValid) {
            OnHidSample(0, 0.0f, 0.0f, tickMs);
            return;
        }
        contacts = 1;
    }

    if (!primaryValid) {
        LogDecodeFail(device, 0);
        return;
    }

    if (g_hidSession.active) {
        g_hidSession.lastRawX = primaryX;
        g_hidSession.lastRawY = primaryY;
    }

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if (screenWidth <= 1 || screenHeight <= 1) {
        LogDecodeFail(device, 0);
        return;
    }

    const HidAxisRange xRange = FindAxisRange(device, kUsageX, reportId);
    const HidAxisRange yRange = FindAxisRange(device, kUsageY, reportId);
    const float dpiScale = HidDpiScale();

    const int32_t pixelX = ToScreenPixel(primaryX, xRange, screenWidth);
    const int32_t pixelY = ToScreenPixel(primaryY, yRange, screenHeight);
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

    OnHidSample(contacts, xDip, yDip, tickMs);
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
        FeedHidReport(*device, static_cast<USHORT>(raw->header.wParam), report,
                      hid.dwSizeHid, tickMs);
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
            } else if (wParam == kDebugTimerId) {
                PollDebugFile();
            }
            return 0;
        // The taskbar moved or the display layout changed: the dismiss axis and
        // the translation baked into the composition expression are stale, and
        // the panel trail was measured against the old work area. This is the
        // event that replaces any polling for both. The message arrives on this
        // thread; the refresh is posted to the UI thread, which owns the XAML
        // root and the property set.
        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETWORKAREA) {
                FileLog(L"work area changed");
                PostUiGeometryRefresh(!g_hidSession.active && !g_reveal.active);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            FileLog(L"work area changed");
            PostUiGeometryRefresh(!g_hidSession.active && !g_reveal.active);
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
    // WM_TIMER tick stays as the fallback and is also the debug poll.
    HANDLE idleTimer = CreateIdleWaitableTimer();
    if (!idleTimer) {
        FileLog(L"hid timer fallback err=%u",
                static_cast<unsigned>(GetLastError()));
        SetTimer(hwnd, kIdleTimerId, kIdleTimerMs, nullptr);
    }
    SetTimer(hwnd, kDebugTimerId, kDebugTimerMs, nullptr);

    MSG msg;
    bool running = true;
    while (running) {
        // With the waitable timer, object 0 is the idle tick and the last index
        // is the message queue; without it only the queue is waited on.
        const DWORD objectCount = idleTimer ? 1 : 0;
        const DWORD wait = MsgWaitForMultipleObjects(objectCount, &idleTimer,
                                                     FALSE, INFINITE,
                                                     QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0 + objectCount) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            continue;
        }
        if (idleTimer && wait == WAIT_OBJECT_0) {
            const uint64_t now = GetTickCount64();
            RunPendingCloseTap(now);
            CheckHidIdleRelease(now);
            PollRootVisibility(now);
            continue;
        }
        break;  // WAIT_FAILED, or no timer and no input: leave the loop
    }
    if (running) {
        FileLog(L"hid wait failed err=%u",
                static_cast<unsigned>(GetLastError()));
    }

    KillTimer(hwnd, kIdleTimerId);
    KillTimer(hwnd, kDebugTimerId);
    if (idleTimer) {
        CancelWaitableTimer(idleTimer);
        CloseHandle(idleTimer);
    }
    UnregisterHidUsage(kUsageTouchScreen);
    UnregisterHidUsage(kUsagePen);

    g_hidSession = HidGestureSession{};
    g_reveal = HidRevealSession{};
    // A contact that is still down when the mod unloads must not leave its 1 ms
    // timer request behind.
    EndContactTiming();
    g_contactDown = false;
    g_openCheckValid = false;
    g_pendingTapTick = 0;
    g_pendingTapKind = CloseKind::None;
    g_coasting.store(false, std::memory_order_relaxed);
    ResetCoastVelocity();
    g_debugFileStamp = DebugFileStamp{};
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

// Publishes the plain values the input thread needs. Runs on the UI thread.
void PublishGestureGeometry() {
    g_axisPub.store(g_dismissAxis, std::memory_order_relaxed);
    g_distancePub.store(g_dismissDistance, std::memory_order_relaxed);

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

// Maps the panel's trailing edge into absolute screen DIP. The visible panel is
// the first visible child of the root with a width (StartSizingFrame); the rest
// of the root canvas is transparent and must not be part of the reveal travel.
// The edge that faces the taskbar is the right edge on a left taskbar, the
// bottom on top, the left on right and the top on bottom. Runs on the UI thread
// and returns false while that child is not laid out yet.
// The screen origin of the XAML coordinate space, in DIP. The content is laid
// out in the host window's client area, and that area does not start at the
// work-area origin: measured on a bottom taskbar, the CoreWindow sits at x=447
// px while the work area starts at 0, so the panel's frame right of 1005 DIP is
// drawn at 1228.5 DIP, not 1005. The work area only matched on a left taskbar
// because the island was inset by exactly the taskbar width; the client origin
// holds for every axis and every layout. Both the panel trail and the cached
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

bool ComputePanelTrailDip(float& trail) {
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
    // edge for whichever side the taskbar is on.
    float originX = 0.0f;
    float originY = 0.0f;
    if (!XamlOriginDip(originX, originY)) {
        return false;
    }

    switch (static_cast<DismissAxis>(g_dismissAxis)) {
        case DismissAxis::Left:
            trail = originX + frameRight;
            break;
        case DismissAxis::Top:
            trail = originY + frameBottom;
            break;
        case DismissAxis::Right:
            trail = originX + frameLeft;
            break;
        case DismissAxis::Bottom:
            trail = originY + frameTop;
            break;
    }

    return true;
}

// A trail has to land inside the monitor's work area, where the panel lives.
// The open storyboard starts the frame at a small, offset rect and grows it into
// place, so a measurement taken during it can name an edge above the work area
// (measured on a bottom taskbar: panel top -12 DIP while the settled panel
// starts at 200). Such a value is not the panel edge the follow maps against.
bool TrailInsideWorkArea(float trail) {
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

    // The trail is an x coordinate on a left or right taskbar and a y
    // coordinate on a top or bottom one, in the same DIP space as rcWork.
    const bool horizontal = IsHorizontalAxis(g_dismissAxis);
    const float low =
        (horizontal ? info.rcWork.left : info.rcWork.top) / scale;
    const float high =
        (horizontal ? info.rcWork.right : info.rcWork.bottom) / scale;
    return trail >= low - kPanelTrailToleranceDip &&
           trail <= high + kPanelTrailToleranceDip;
}

// Publishes the panel trail for the reveal follow. Falls back to the root
// extent, which reproduces the earlier root-width travel, and says so once.
//
// `logOnChange` is the refresh the input thread posts when the menu opens and
// when the system announces a work-area or display change: it speaks only when
// the value moved, so a layout that did not move does not repeat the line. Setup
// and SizeChanged keep the one line per open.
void PublishPanelTrailDip(bool logOnChange) {
    const float distance = g_dismissDistance;
    if (!(distance > 0.0f)) {
        return;
    }

    float trail = 0.0f;
    bool computed = false;
    try {
        computed = ComputePanelTrailDip(trail);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Panel trail mapping failed: %08X", e.code());
    } catch (...) {
        // A tree mid-teardown must not break Setup or SizeChanged.
    }

    if (computed && !TrailInsideWorkArea(trail)) {
        // Keep the value the last good measurement published: the follow maps
        // against it, and an edge outside the work area would place the panel
        // off screen at every finger position.
        if (!g_panelTrailRejectedLogged) {
            g_panelTrailRejectedLogged = true;
            FileLog(L"reveal panelTrail rejected %.0f", trail);
        }
        return;
    }

    if (!computed && logOnChange) {
        // A re-check never downgrades a measured trail to the root extent: the
        // tree can be mid-layout for a frame, and the follow would lag the
        // finger until the next open. The previous value stays.
        return;
    }

    const float previous = g_panelTrailDip.load(std::memory_order_relaxed);
    if (!computed) {
        trail = distance;
    }
    g_panelTrailDip.store(trail, std::memory_order_relaxed);

    if (!computed) {
        if (!g_panelTrailFallbackLogged) {
            g_panelTrailFallbackLogged = true;
            FileLog(L"reveal panelTrail=%.0f (fallback)", trail);
        }
        return;
    }

    if (logOnChange) {
        if (std::fabs(trail - previous) <= 1.0f) {
            return;
        }
        FileLog(L"reveal panelTrail=%.0f", trail);
        return;
    }

    if (!g_panelTrailLogged) {
        g_panelTrailLogged = true;
        FileLog(L"reveal panelTrail=%.0f", trail);
    }
}

// ---------------------------------------------------------------------------
// Settled panel trail (layout event, no timer)
//
// The open transition re-measures the panel edge once, but that measurement can
// land while the shell's own open storyboard is still growing the frame into
// place: on a bottom taskbar it read a panel top of -12 DIP where the settled
// panel starts at 200. The reveal follow maps `Progress` against this value, so
// an early measurement anchors the panel 212 DIP off for that whole open.
//
// LayoutUpdated is the one signal that arrives when XAML has placed the panel,
// and it costs nothing while no watch is armed. The watch skips the first pass
// (it usually precedes the placement), re-measures on the second, then revokes
// its own token, so at most one watch ever exists and no handler is left behind.
// The close path and the renderer teardown revoke it too, because a menu that
// closed before the second pass must not measure anything later.
// ---------------------------------------------------------------------------
bool g_layoutMeasureTrail = false;  // UI thread only

void StopSettledTrailWatch() {
    wux::FrameworkElement root = g_root;
    if (g_layoutToken.value && root) {
        try {
            root.LayoutUpdated(g_layoutToken);
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"LayoutUpdated revoke failed: %08X", e.code());
        } catch (...) {
            // A root mid-teardown cannot revoke; the generation guard in the
            // handler keeps a stale callback from measuring the next open.
        }
    }
    g_layoutToken = {};
    g_layoutTicksLeft = 0;
}

void ArmSettledTrailWatch(bool trailIdle) {
    wux::FrameworkElement root = g_root;
    if (!root) {
        return;  // no root, no layout to wait for
    }

    // One watch per open: a work-area broadcast re-posts the refresh, and the
    // previous watch would otherwise measure the same settled layout twice.
    StopSettledTrailWatch();
    g_layoutTicksLeft = 2;
    g_layoutMeasureTrail = trailIdle;

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
                if (g_layoutMeasureTrail) {
                    RefreshGestureGeometry();
                    PublishPanelTrailDip(true);
                }
                // The list has its final extent now, so the dismiss gate is
                // re-armed against the settled rect and offsets even when a
                // reveal owned the open and skipped the trail measurement.
                CacheScrollableElement(true);
            } catch (winrt::hresult_error const& e) {
                Wh_Log(L"Settled trail failed: %08X", e.code());
            } catch (...) {
                // Never let an exception escape into XAML.
            }
            // Revoked after the measurement, and also when one threw, so the
            // handler never outlives the open it belongs to.
            StopSettledTrailWatch();
        });
    } catch (winrt::hresult_error const& e) {
        g_layoutToken = {};
        g_layoutTicksLeft = 0;
        FileLog(L"layout watch failed err=%08X", static_cast<unsigned>(e.code()));
    } catch (...) {
        g_layoutToken = {};
        g_layoutTicksLeft = 0;
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
// panel trail, so the input thread can test a press point against it directly.
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

void FindScrollableElement(const wux::UIElement& element, uint32_t depth,
                           float originX, float originY,
                           ScrollerCandidate& best) {
    if (!element || depth > kScrollerScanMaxDepth) {
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
    } catch (...) {
        // A branch mid-teardown ends this branch only.
    }

    if (depth >= kScrollerScanMaxDepth) {
        return;
    }

    try {
        const int32_t count =
            wux::Media::VisualTreeHelper::GetChildrenCount(element);
        for (int32_t i = 0; i < count; ++i) {
            FindScrollableElement(
                wux::Media::VisualTreeHelper::GetChild(element, i)
                    .try_as<wux::UIElement>(),
                depth + 1, originX, originY, best);
        }
    } catch (...) {
        // A child being attached or removed ends this branch only.
    }
}

// Drops only the handler, so a re-measure can subscribe to the same or another
// element. Runs on the UI thread.
void RevokeScrollerWatch() {
    wuxc::ScrollViewer viewer = g_scrollerViewer;
    if (g_scrollerToken.value && viewer) {
        try {
            viewer.ViewChanged(g_scrollerToken);
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"ViewChanged revoke failed: %08X", e.code());
        } catch (...) {
            // The element is gone; its handler dies with it.
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
        if (g_scroller.valid) {
            g_scroller.verticalOffset = verticalOffset;
            g_scroller.scrollableHeight = scrollableHeight;
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Scroller offset read failed: %08X", e.code());
    } catch (...) {
        // A closed menu must not break the handler.
    }
}

// Finds and caches the element for the open that is being measured. Runs on the
// UI thread from the open transition and again from the settled layout pass; the
// second call replaces the first measurement, which can still belong to the
// storyboard.
void CacheScrollableElement(bool settled) {
    wux::FrameworkElement root = g_root;
    if (!root) {
        return;
    }

    float originX = 0.0f;
    float originY = 0.0f;
    if (!XamlOriginDip(originX, originY)) {
        return;
    }

    ScrollerCandidate best;
    try {
        FindScrollableElement(root, 0, originX, originY, best);
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Scroller search failed: %08X", e.code());
    } catch (...) {
        // An unreadable tree leaves the previous cache alone.
    }

    // One handler per measurement: the element must not be observed twice.
    RevokeScrollerWatch();

    if (!best.found) {
        // Nothing scrolls: every vertical press belongs to the dismiss. This is
        // also what the open transition can see while the storyboard still has
        // the list unmeasured, which is why the settled pass measures again
        // instead of trusting this answer.
        std::lock_guard lock(g_scrollerMutex);
        g_scroller = ScrollerCache{};
        return;
    }

    float verticalOffset = 0.0f;
    float scrollableHeight = 0.0f;
    try {
        verticalOffset = best.viewer.VerticalOffset();
        scrollableHeight = best.viewer.ScrollableHeight();
    } catch (...) {
        // A viewer that cannot be read is no better than no viewer.
        std::lock_guard lock(g_scrollerMutex);
        g_scroller = ScrollerCache{};
        return;
    }

    {
        std::lock_guard lock(g_scrollerMutex);
        g_scroller.valid = true;
        // Only the post-storyboard pass may arm the gate: the rect measured on
        // the open transition belongs to a window the shell is still moving.
        g_scroller.settled = settled;
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
// rect of the previous open.
void ReleaseScrollableElement() {
    RevokeScrollerWatch();
    g_scrollerLogged = false;
    g_scrollerLoggedRect[0] = 0.0f;
    g_scrollerLoggedRect[1] = 0.0f;
    g_scrollerLoggedRect[2] = 0.0f;
    g_scrollerLoggedRect[3] = 0.0f;
    std::lock_guard lock(g_scrollerMutex);
    g_scroller = ScrollerCache{};
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

    PublishGestureGeometry();
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
// A root that is not attached has no renderer to write: the refresh is a no-op
// then.
void RefreshGestureGeometry() {
    wux::FrameworkElement root = g_root;
    if (!root) {
        return;
    }

    {
        std::lock_guard lock(g_compositionMutex);
        if (!g_state) {
            return;
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
        return;  // the root has not been laid out yet
    }

    const bool changed = g_dismissAxis != previousAxis ||
                         std::fabs(distance - previousDistance) > 0.5f;
    g_dismissDistance = distance;

    {
        std::lock_guard lock(g_compositionMutex);
        if (!g_state) {
            return;
        }
        if (changed) {
            try {
                g_state.InsertScalar(L"Distance", distance);
                g_state.InsertScalar(L"DirX", dirX);
                g_state.InsertScalar(L"DirY", dirY);
            } catch (winrt::hresult_error const& e) {
                FileLog(L"gesture geometry write failed err=%08X",
                        static_cast<unsigned>(e.code()));
                return;
            } catch (...) {
                FileLog(L"gesture geometry write failed err=unknown");
                return;
            }
        }
    }

    // The published pair the input thread reads has to match the scalars, so
    // both happen after the write and never on their own.
    PublishGestureGeometry();
    FileLog(changed ? L"gesture geometry updated axis=%d dist=%.0f"
                    : L"gesture geometry axis=%d dist=%.0f (unchanged)",
            g_dismissAxis, distance);
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
    g_dispatcher = root.Dispatcher();
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
    g_dismissDistance = distance;

    wuc::CompositionPropertySet state = compositor.CreatePropertySet();
    state.InsertScalar(L"Progress", 1.0f);  // Normal Start is visible.
    state.InsertScalar(L"Distance", distance);
    state.InsertScalar(L"DirX", dirX);
    state.InsertScalar(L"DirY", dirY);

    // The fixed curve, the v0.1.0 one: cubic-bezier(0.45, 0, 0.55, 1) starts
    // gently and ends gently. The keyboard open uses it.
    const wuc::CompositionEasingFunction retractEasing =
        compositor.CreateCubicBezierEasingFunction(float2{0.45f, 0.0f},
                                                   float2{0.55f, 1.0f});

    // The Windows 11 settle curve: cubic-bezier(0.1, 0.9, 0.2, 1). It starts
    // almost flat and decelerates hard into the target, the Fluent motion the
    // Windows 11 style asks for. It is cached here, so no settle creates one.
    const wuc::CompositionEasingFunction win11SettleEasing =
        compositor.CreateCubicBezierEasingFunction(
            float2{kWin11SettleControl1X, kWin11SettleControl1Y},
            float2{kWin11SettleControl2X, kWin11SettleControl2Y});

    // The translation property must exist before the expression is started.
    visual.Properties().InsertVector3(L"Translation",
                                      float3{0.0f, 0.0f, 0.0f});

    wuc::ExpressionAnimation translation = compositor.CreateExpressionAnimation(
        L"Vector3((1 - state.Progress) * state.DirX * state.Distance, "
        L"(1 - state.Progress) * state.DirY * state.Distance, 0)");
    translation.SetReferenceParameter(L"state", state);
    visual.StartAnimation(L"Translation", translation);

    {
        std::lock_guard lock(g_compositionMutex);
        g_compositor = compositor;
        g_visual = visual;
        g_state = state;
        g_translationExpression = translation;
        g_retractEasing = retractEasing;
        g_win11SettleEasing = win11SettleEasing;
    }

    // A resize changes the extent the expression slides by, and it may be the
    // first real layout after Setup. The refresh owns the axis check as well, so
    // a root that is laid out again after a taskbar move is corrected here too.
    g_sizeToken = root.SizeChanged(
        [](winrt::Windows::Foundation::IInspectable const&,
           wux::SizeChangedEventArgs const&) {
            try {
                RefreshGestureGeometry();
                // The root was resized by the panel layout, so the trail is
                // remapped with the new extent in force.
                PublishPanelTrailDip(false);
            } catch (winrt::hresult_error const& e) {
                Wh_Log(L"SizeChanged update failed: %08X", e.code());
            } catch (...) {
                // Never let an exception escape into XAML.
            }
        });

    // Everything the input thread reads is published before the root opens.
    // This trail is the initial anchor: the frame can still be off its settled
    // position here, so the open transition re-measures it.
    PublishGestureGeometry();
    PublishPanelTrailDip(false);
    g_hasRoot.store(true, std::memory_order_release);

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
std::atomic<bool> g_animHookAttempted{false};
std::atomic<bool> g_afterInit{false};
std::atomic<bool> g_stopPolling{false};
HANDLE g_pollThread = nullptr;

void InstallStartUIHooks(HMODULE module) {
    if (!module || g_startUIHooked.exchange(true)) {
        return;
    }
    if (g_disabled.load(std::memory_order_relaxed)) {
        g_startUIHooked.store(false);
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
        if (!g_startUIHooked.load(std::memory_order_relaxed)) {
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

    if (module && !g_startUIHooked.load(std::memory_order_relaxed)) {
        if (module == GetModuleHandleW(L"StartUI_.dll")) {
            TryInstallFromLoadedModules(true);
        }
    }

    return module;
}

DWORD WINAPI PollModulesThread(LPVOID) {
    for (int i = 0; i < 400 && !g_stopPolling.load(std::memory_order_relaxed);
         i++) {
        TryInstallFromLoadedModules(true);
        if (g_startUIHooked.load(std::memory_order_relaxed)) {
            break;
        }
        Sleep(50);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------

// Runs on the UI thread.
void RevokeHandlers() {
    // The settled-trail watch and the cached scroller belong to this root, so
    // both are dropped before its reference goes away: their handlers hold XAML
    // objects and must never run against the next open.
    StopSettledTrailWatch();
    ReleaseScrollableElement();

    wux::FrameworkElement root = g_root;
    if (!root) {
        return;
    }

    try {
        if (g_sizeToken.value) {
            root.SizeChanged(g_sizeToken);
            g_sizeToken = {};
        }
        if (g_loadedToken.value) {
            root.Loaded(g_loadedToken);
            g_loadedToken = {};
        }
    } catch (winrt::hresult_error const& e) {
        Wh_Log(L"Failed to revoke XAML handlers: %08X %s", e.code(),
               e.message().c_str());
    } catch (...) {
        Wh_Log(L"Failed to revoke XAML handlers with an unknown exception");
    }

    // Release the XAML reference on the UI thread.
    g_root = nullptr;
}

void RevokeHandlersOnUiThread() {
    if (!g_root) {
        return;
    }

    if (g_uiThreadId.load(std::memory_order_relaxed) ==
        GetCurrentThreadId()) {
        RevokeHandlers();
        return;
    }

    wucore::CoreDispatcher dispatcher = g_dispatcher;
    if (!dispatcher) {
        Wh_Log(L"No CoreDispatcher captured; XAML handlers left attached");
        return;
    }

    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!done) {
        return;
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
        return;
    } catch (...) {
        CloseHandle(done);
        return;
    }

    if (WaitForSingleObject(done, 2000) == WAIT_OBJECT_0) {
        CloseHandle(done);
    } else {
        // The queued callback may still signal the event; leaking one handle
        // is safer than closing a handle it will use later.
        Wh_Log(L"Timed out waiting for the UI thread to revoke XAML handlers");
    }
}

void ResetComposition() {
    std::lock_guard lock(g_compositionMutex);

    // Invalidate a pending dismiss completion before stopping its animation,
    // so an old batch cannot touch a close in a newly opened menu.
    g_dismissSerial.fetch_add(1, std::memory_order_relaxed);
    g_dismissSettling.store(false, std::memory_order_relaxed);

    try {
        if (g_state) {
            g_state.StopAnimation(L"Progress");
            g_coasting.store(false, std::memory_order_relaxed);
            g_state.InsertScalar(L"Progress", 1.0f);
            g_progressNow.store(1.0f, std::memory_order_relaxed);
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
    g_translationExpression = nullptr;
    g_compositor = nullptr;
    g_state = nullptr;
    g_visual = nullptr;
    g_retractEasing = nullptr;
    g_win11SettleEasing = nullptr;

    g_distancePub.store(0.0f, std::memory_order_relaxed);
    g_panelTrailDip.store(0.0f, std::memory_order_relaxed);

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
    Wh_Log(L"Legacy Start Edge Swipe 0.12.0 initializing");
    LoadAnimationStyle();
    LoadGestureStartSpeed();
    FileLog(L"settle spring period=%.3f zeta=%.2f", kSpringPeriodSeconds,
            kSpringDamping);

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
    // the module starts to unload.
    g_stopDismissCallback = true;

    if (g_pollThread) {
        WaitForSingleObject(g_pollThread, 2000);
        CloseHandle(g_pollThread);
        g_pollThread = nullptr;
    }

    // The hidden input window and its raw input registration belong to the
    // input thread. Ask it to leave the message loop and wait for it before
    // the module's code can be unmapped.
    HWND hwnd = g_hidHwnd.load(std::memory_order_relaxed);
    if (hwnd) {
        PostMessageW(hwnd, WM_QUIT, 0, 0);
    }
    if (g_hidThread) {
        const DWORD waitResult = WaitForSingleObject(g_hidThread, 2000);
        CloseHandle(g_hidThread);
        g_hidThread = nullptr;
        if (waitResult != WAIT_OBJECT_0) {
            Wh_Log(L"Timed out waiting for the raw input thread");
        }
    }
}

void Wh_ModUninit() {
    RevokeHandlersOnUiThread();
    ResetComposition();
    Wh_Log(L"Legacy Start Edge Swipe Dismiss uninitialized");
}

// A settings change only refreshes the atomics: the next settle reads them, no
// renderer state and no cached curve is touched.
void Wh_ModSettingsChanged() {
    LoadAnimationStyle();
    LoadGestureStartSpeed();
}
