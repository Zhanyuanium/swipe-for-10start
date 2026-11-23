# Geometry — the snapshot, the band, the axis, the scroll survey

> Part of [docs/README.md](README.md). The gestures that consume this geometry
> are in [gesture.md](gesture.md).

## The snapshot

Axis, `Distance`, the panel rectangle, the XAML origin, the work area and the
monitor are one `GestureGeometry` snapshot behind a short writer lock: the UI
thread is the only writer and the input thread answers its last whole copy on
contention (the first report of a new contact takes the lock blocking when its
cached copy has an obsolete layout identity). The snapshot carries the root and
open generation plus a layout identity token, so a cached copy from before a
resize is rejected for a new gesture.

- The trailing edge is measured on the UI thread from the root's visible child
  (`StartSizingFrame`) and translated by `ClientToScreen` of the host window's
  client origin, not the work-area origin. One function derives it from the rect,
  so the hit test, the follow, the release and the log line describe one panel.
- **Along** the gesture the trail edge is the boundary: a mapped progress at or
  above the panel's live one is on the surface the retract still shows.
  **Across** it there is no progress, so the cross range comes from the same rect
  (`top..bottom` on a horizontal axis, `left..right` on a vertical one,
  `PanelCrossContains`). On `60,151..1053,726`, a point below `y=728` is off the
  panel whatever its `x` maps to.
- A measurement is accepted only when the rect lies on the monitor and its
  taskbar-side edge lies inside the band: the bar's reserved strip extended by one
  bar thickness toward the desktop. The left docked 48 DIP bar gives `0..96`; the
  settled panel's left edge is `60`, 12 DIP inside the bar's inner edge; the open
  storyboard's rect is rejected whole (`reveal panelTrail rejected ...`, once per
  open). The same band is the reveal's candidate strip.
- The previous rect is carried over only when the whole layout identity (axis,
  travel, client origin, work area, monitor) matches (±0.5 DIP on travel); an
  axis-only change republishes the trail. Renderer scalars are written first and
  `g_dismissDistance` advances only when they took the new travel; a write that
  throws marks the renderer invalid until a full reapply.

## Deferred apply

The apply reservation (`LayoutApplyGuard`) and the admission claim
(`ClaimPanelForAdmission`) are `seq_cst`, so the layout is deferred whole or the
admission is refused and retried on the next report. Any layout change — an open,
`WM_DISPLAYCHANGE`, `SPI_SETWORKAREA`, `SizeChanged` — is deferred while a
contact owns the panel: renderer scalars, panel rect and scroller survey apply
together by one settled pass after the release
(`gesture geometry deferred reason=panel-owned`), so nothing moves under the
finger.

The first measurement of an open lands while the shell is animating and fails
acceptance; one request (the layout watch's second pass or its 400 ms deadline)
carries an id and an epoch. Its decision and its write are one section under
`g_measureMutex`; XAML measurement stays outside it, and the publish re-checks the
id, the epoch, the axis, the travel and panel ownership. A request a close or a
newer request replaced writes nothing.

## The axis probe

The dismiss axis and the band come from the taskbar, never the Start window's
rect: `SHAppBarMessage(ABM_GETTASKBARPOS)` is primary (its `uEdge` and rect are
reported unchanged while the bar is hidden), cached on the input thread and
refreshed on `WM_SETTINGCHANGE`/`WM_DISPLAYCHANGE`, so no XAML handler makes a
cross-process call. The `Shell_TrayWnd`/`Shell_SecondaryTrayWnd` rectangle is the
cross-check: the bar must be flush to the edge **and** thin across it. The
work-area inset names the axis only when it truly insets, because auto-hide makes
it zero. An inconclusive probe keeps the last axis and band, logs once per
undecided episode, and never silently means `Left`.

Measured: left docked 2026-11-21 `axis=0`, `rc=0,0,96,1800`,
`work=96,0,2880,1800`; bottom auto-hidden `axis=3`, bar `0,1798,2880,1894`,
`work=0,0,2880,1800`.

## The scroller gate

A vertical dismiss yields to the menu's own scrolling, which happens in more than
one place (the app list and the tile grid). The mod surveys every vertically
scrollable `ScrollViewer` under the panel once per open and keeps up to six of
them (`kMaxScrollRegions`), each with its screen rect, `VerticalOffset`,
`ScrollableHeight`, depth and label.

- At a press the gate picks the innermost containing region (depth first, then
  the smaller rect); no containing region means the drag belongs to the dismiss.
- Inside a region the drag belongs to the scroll unless the region is already at
  the end the drag would scroll away from (`VerticalOffset <= 24` for a bottom
  taskbar, mirrored for a top one); otherwise the press is refused
  (`dismiss deferred to scroll vo=... sh=... region=...`, once per 500 ms).
- This is a top/bottom rule: on a left or right bar the dismiss axis is
  horizontal, `VerticalDismissAllowed` answers true, and the gate is not
  consulted.
- Every remeasure publishes `Unknown` first; retries back off 100/200/400 ms,
  three attempts. A region whose offsets could not be read is dropped alone. A
  region that cannot be watched keeps the rect and offsets of the pass that read
  it instead of going `Unknown`.
- The watch has two slots per region: `owner` (whose `ViewChanged` token must
  still be revoked; a failed revoke keeps the slot occupied and a replacement is
  refused) and `viewer` (the element the region was measured from, the only one
  whose handler may write it, and only while the cache is `Scrollable` and
  belongs to the open and layout on screen).

Only a broadcast is honoured: `SendMessageTimeout(HWND_BROADCAST,
WM_SETTINGCHANGE, SPI_SETWORKAREA, ...)`, because `wParam` must be
`SPI_SETWORKAREA` (`0x002F`); a `PostMessageW` to the mod's window fails with
error 1159 on this host.
