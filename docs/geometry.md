# Geometry — where the panel is, which way is out, and what may scroll

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). The gestures that consume this geometry are in [gesture.md](gesture.md).

## The snapshot

Axis, `Distance`, the whole panel rectangle, the XAML origin, the work area and
the monitor are one `GestureGeometry` snapshot behind a short writer lock: the
UI thread is the only writer and holds an `SRWLOCK` exclusively for one struct
assignment; the input thread tries a shared acquisition per sample and answers
its last whole copy on contention, because a seqlock's counters do not
legalize reading a plain struct while the writer assigns it. The first report
of a new contact takes the lock blocking when its cached copy has an obsolete
layout identity, and that read has no time bound if the writer is descheduled.
The snapshot also carries the root/open generation and a layout identity token,
so a cached copy from before a resize is rejected for a new gesture instead of
starting from an old rectangle.

The panel's trailing edge — the side facing away from the taskbar — is measured
on the UI thread from the root's visible child (`StartSizingFrame`; ~1053 DIP
on a left taskbar, the panel's top edge at about 277-289 DIP on this
bottom-taskbar layout, 289 measured on one host and 277 on another), translated
by `ClientToScreen` of the host window's client origin — not by the work-area
origin. The trail is derived from the rect by one function, so a rect and its
edge cannot disagree, and the hit test, the follow, the release and that
report's log line all describe one panel. The legacy `reveal panelTrail=...`
line prints that derived edge with the axis, the rect, the work area, the band
it was tested against and the generation.

A measurement is accepted only when the rect lies on the monitor and its
taskbar-side edge lies inside the band the taskbar itself defines: the bar's
reserved strip at the screen edge, extended by one bar thickness toward the
desktop. On the current bottom-taskbar layout the auto-hidden bar's own 48 DIP
thickness gives the band `804..900` even though the work area is the whole
screen; the settled panel sits 12 DIP inside the bar's inner edge (`840` against
`852`), inside the band, and the open storyboard's rect is rejected whole. The
docked layout had the same band from the reserved strip `852..900`. The same
band is the reveal's candidate strip on the input
thread, so the panel the snapshot accepts and the strip a pull may start in
cannot disagree about where the taskbar ends. A rejection publishes nothing
and logs `reveal panelTrail rejected ... axis=... rect=... work=... mon=...
band=...` once per open, from the first pass that sees a rejected rect; the
last whole snapshot stays. The previous rect is carried over only when the
whole layout identity — axis, travel, client origin, work area and monitor —
matches (±0.5 DIP on travel). The axis is part of that identity, so an
axis-only change bumps the token, drops the carried rect and republishes the
trail.

Renderer scalars are written first and `g_dismissDistance` advances only when
they took the new travel, so a failed or skipped write leaves the applied
distance where the compositor holds it; a scalar write that throws after an
earlier one landed marks the renderer invalid (`gesture renderer invalid until
a successful reapply`) and refuses gestures until a full reapply. The apply
reservation (`LayoutApplyGuard`) and the admission claim
(`ClaimPanelForAdmission`) are `seq_cst`, so either the layout is deferred
whole or that admission is refused and retried on the next report. A layout
change of any kind — an open, `WM_DISPLAYCHANGE`, `SPI_SETWORKAREA` or
`SizeChanged` — is deferred whole while a contact owns the panel: renderer
scalars, panel rect and scroller survey are applied together by one settled pass
after the release (`gesture geometry deferred reason=panel-owned gen=...`), so
the edge, the axis, the travel and the scroller rects never move under the
finger.

## The axis probe

The dismiss axis and the panel-acceptance band come from the taskbar itself,
never the Start window's rect. `SHAppBarMessage(ABM_GETTASKBARPOS)` is the
primary source (`uEdge` and its docked rectangle, reported unchanged while the
bar is hidden); the `Shell_TrayWnd`/`Shell_SecondaryTrayWnd` rectangle is the
cross-check (the bar must be flush to the edge **and** thin across it, so a
full-width bottom bar is not mistaken for a side one); the work-area inset
names the axis only when it truly insets, because auto-hide makes it zero by
design. An inconclusive probe keeps the last axis and band, logs once per
undecided episode with what each source said (`dismiss axis=... kept, no
source named an edge ...`), and retries on the next open or broadcast; it never
silently means `Left`. Measured on this layout:

```
dismiss axis=3 (0=left,1=top,2=right,3=bottom) src=appbar appbar=3
rc=0,1704,2880,1800 bar=3 rect=0,1798,2880,1894 work=0,0,2880,1800
mon=0,0,2880,1800
```

## The measurement request

The first measurement of an open lands while the shell is still animating the
panel in and fails the acceptance rule; one request — the layout watch's second
pass or its 400 ms deadline, whichever runs first — carries a request id and an
epoch. Its decision and its write are one critical section under
`g_measureMutex`, the commit lock every epoch advance also takes, so a close, a
newer request or a resize cannot slip between them; all XAML measurement stays
outside that section. The panel publish re-checks the id, the epoch, the axis
and travel, and panel ownership at the moment it writes; its scroller commit
re-checks the id, the epoch, the layout version and panel ownership; and a
request that a close or a newer request replaced writes nothing. An idle
`SizeChanged` also re-measures the scroll survey, not only the panel, so a press
cannot be judged against the list rects of the old layout.

## The scroller gate

A vertical dismiss yields to the menu's own scrolling, and the menu scrolls in
more than one place: the app list and the tile grid. The mod therefore surveys
every vertically scrollable `ScrollViewer` under the panel once per open and
keeps up to six of them (`kMaxScrollRegions`) — a region is a viewer that
scrolls vertically and not horizontally — each with its screen rect,
`VerticalOffset`, `ScrollableHeight`, visual-tree depth and a label. The survey
replaces the single cached viewer of earlier builds, which made a press in the
tile grid "outside the scrolling content" and turned it into a dismiss.

Measured on this layout, the menu scrolls in two places. The rail is not one of
them: its `ScrollableHeight` is 0. One line per region per open records each
region, and again only when a settled pass moved its rect:

```
scroller rect=...,...,...,... vo=87 sh=98 name=TileListView depth=...
```

A viewer beyond the sixth is dropped: the deepest six are kept, because the
innermost is the one a press lands on, and the drop is logged once
(`scroller overflow (kept 6 deepest regions)`).

At a press the gate picks the innermost region whose rect contains the point —
depth first, then the smaller rect — so a list inside a scroll host wins over
the host. No region containing the press means the drag has no content to
scroll and belongs to the dismiss, which is what a press on the panel
background, the rail or the taskbar strip gets. Inside a region the drag
belongs to the scroll unless that region is already at the end the drag would
scroll away from (`VerticalOffset <= 24` for a bottom taskbar, mirrored for a
top one — the allowance a fling needs); otherwise the press is rejected with
`dismiss deferred to scroll vo=... sh=... region=...` at most once per 500 ms,
naming the region that took the drag. Measured on the tiles:
`vo=87 sh=98 region=TileListView` kept the menu open, and the same contact
became the dismiss once they reached their end (`commit=1`,
`close dismiss kind=dismiss`); the app list behaves as before, and a press in the
panel margin dismisses with no deferral. The rejection starts no session, so
the contact latch is untouched and the next down starts from a clean state.

The survey, not a single element, carries the state. Every remeasure publishes
`Unknown` first, so a pass that fails or stops short cannot leave a previous
`Nothing` or `Scrollable` speaking for a tree it no longer describes, and the
cached answer carries the layout version the gate checks beside the open
generation. Unknown means no measurement speaks for this open — nothing measured
yet, a failed measurement, or only the pass taken while the shell still moved
the window — and it defers a press inside the panel while a press outside it
still passes. Nothing scrollable means a settled, complete search found no
region, so the press dismisses; a search that threw or hit its depth limit stays
Unknown and retries with 100/200/400 ms backoff, three attempts, logging
`scroller retry why=origin|incomplete|not-allowed|offsets|commit gen=...
try=.../...` once per open and `scroller retry exhausted gen=...` when the
budget is spent; a search that ran under a contact stays Unknown and is posted
again when that contact ends. A completed negative result revokes every watch,
so no handler keeps refreshing offsets the gate can never use. A region whose
offsets could not be read is dropped alone: the others were measured whole, and
refusing the survey would leave the gate Unknown for the rest of that open.

The watch has two roles per region, one slot each in `g_scrollerWatches[6]`.
`owner` is the element whose `ViewChanged` token must still be revoked: a failed
revoke keeps it for a retry and keeps the slot occupied, so a replacement
registers no watch while it survives (`xaml viewchanged handler left attached;
new registration refused`). `viewer` is the element the committed region was
measured from, the only one whose handler may write that region: the handler
resolves its slot by the captured element on the UI thread, before it takes
`g_scrollerMutex`, and writes the offsets only under that mutex while the cache
is `Scrollable` and still belongs to the open and the layout on screen. The
cache identity moves in the same uninterrupted UI-thread step as the commit, so
no handler can see the new rect under the old element's name. A region that
cannot be watched keeps the rect and the offsets of the pass that read it — the
correct list, possibly with stale end-of-list numbers — instead of going
`Unknown`, which would defer every press inside the panel for the rest of that
open. Scrollable also carries the region list itself, so a positive candidate
commits even when another branch was unreadable.

## Work-area broadcasts

The mod honours only a broadcast: `SendMessageTimeout(HWND_BROADCAST,
WM_SETTINGCHANGE, SPI_SETWORKAREA, ...)`, because the `wParam` must be
`SPI_SETWORKAREA` (`0x002F`; `0x0029` is ignored) and a `PostMessageW` to the
mod's hidden window fails with error 1159 on this host. A work-area broadcast
during a held drag defers the whole turn (`gesture geometry deferred
reason=panel-owned gen=5`), and the release keeps the axis and travel it
started with. A burst of work-area changes leaves the scroller gate `Unknown`
for that open: every remeasure publishes `Unknown` first and a pass whose
request a newer one replaced commits nothing, so presses inside the panel are
deferred there rather than dismissed.

## Measured layouts

The current layout is **auto-hidden** (auto-hide on): `dismiss axis=3`,
`Distance` 852, work `0,0,1440,900`, the bar off screen, panel
`236,265..1229,840`, so the trail is the panel's top edge at **265 DIP** and the
band it is tested against is `804..900` (`reveal panelTrail=265 axis=3 ...
work=0,0,1440,900 band=804,900`). The docked layout (work `0,0,1440,852`, tray
`0,852..1440,900`, the same panel and band, the bar 48 DIP thick) is historical.
The earlier auto-hidden variant (panel `236,167..1229,840`, trail 167, hidden
window at `0,1798,2880,1894`) is also historical and its numbers are not these.

The survey on this layout measured two regions: the app list
(`AllAppsGridListView`, `sh=4065`) and the tile grid's internal viewer
(`TileListView`, `sh=98`). Pressing in the tiles at `vo=87` deferred the dismiss
to that region and kept the menu open; the same contact closed the menu once the
tiles reached their end.

The vertical path was exercised most fully on the earlier top-taskbar layout:
`dismiss axis=1`, panel about `236,36..1229,587`, scroller about
`309,66..569,586` with `vo=0 sh=4077`. There an upward drag inside the panel
whose list cannot scroll commits and taps. A drag in the long list scrolls
first (`dismiss deferred to scroll vo=0 sh=4077 region=...`; the line gained
`region=` in 0.30.0), the region's offsets follow `ViewChanged` (vo
277/1214/2194/3135), and the same swipe turns into a dismiss once the list
reaches its end.
