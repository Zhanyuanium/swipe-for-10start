# Input layer — why Raw HID, and how reports are decoded

> 语言：英文；用户指南有中文版：[../README.zh-CN.md](../README.zh-CN.md)。
> Part of [docs/README.md](README.md). Code member list: [../mods/AGENTS.md](../mods/AGENTS.md).

ExplorerPatcher replaces Windows 11's Start UI with the legacy `StartUI_.dll`,
which has no consumer for the shell's touch gesture. Every in-process source
for per-frame finger data on that UI was measured on this build and found
empty — the digitizer itself, through Raw Input, is the only source that works.

## Data sources that do not work

Each of these was measured on this build, not assumed:

| Source | Result |
|---|---|
| XAML pointer events | `PointerDeviceType.Mouse` (0), one `move` per drag, `axisDelta ≈ 0` |
| `CancelDirectManipulations` | returns false |
| UDK `ShellViewGestureAnimationPropertySet` | `AnimationProps.Translation` resolves but is constant 0; `Width`/`Height`/`VisibilityProgress` return `0x80070057` |
| XAML `DManipDataBase` cache (`CUIElement+0x118 → +0x138 → +0x58`) | empty during a drag |
| `DManipDataBase::SetContentOffsetsHelper` hook | installed, **0 calls** across three real drags |
| System cursor (`GetCursorPos`) | `CURSOR_SUPPRESSED` for the whole contact, position frozen |
| Queue (`GetMessageW`/`PeekMessageW`) | `WM_POINTERENTER` + `WM_POINTERDOWN`, then `WM_POINTERCAPTURECHANGED` and silence |
| `GetPointerInfo(id)` / `GetPointerFrameInfoHistory` | valid id, but a frozen down snapshot |
| `RegisterPointerInputTarget` | requires UIAccess, unavailable to an injected mod |
| Composition `IDCompositionVisual` read-back | write-only, no getter |

The short version: the touch never reaches XAML on this UI, and the window queue
is closed after the down, so the only in-process source is the digitizer itself.

## Capturing reports

A worker thread in the `start` namespace of `mods/swipe-for-10start.wh.cpp`
registers Raw Input for the digitizer (`usUsagePage 0x0D / usUsage 0x04` for
touch, `0x0D / 0x02` for pen, `RIDEV_INPUTSINK`) on a hidden top-level window. A message-only window
would not receive the `WM_SETTINGCHANGE` (`SPI_SETWORKAREA`) and
`WM_DISPLAYCHANGE` broadcasts that move the taskbar, and those broadcasts are
the whole event-driven axis refresh (see [geometry.md](geometry.md)).

## Decoding a report

Each report is decoded with `GetRawInputDeviceInfoW(RIDI_PREPARSEDDATA)` +
`HidP_GetUsageValue`: contact collections `0x0D/0x22` (Finger) or `0x0D/0x05`
carry `0x01/0x30` (X) and `0x01/0x31` (Y); the top-level `0x0D/0x54` is the
Contact Count. A device whose input caps declare report IDs carries the ID in
the first byte of every raw report (`RAWINPUTHEADER::wParam` is only
`RIM_INPUT`/`RIM_INPUTSINK`), and that byte selects the caps; a device without
report IDs takes the previous path. A report whose ID carries no contact field
at all — no slot field and no contact count — is an unrelated report ID: it is
ignored whole, which is distinct from a decode failure that logs
`hid decodefail`. Otherwise every candidate slot's `TipSwitch`, `InRange`,
Contact Identifier and X/Y are decoded before a contact is chosen.

`TipSwitch` (`0x0D/0x42`) and `InRange` (`0x0D/0x32`) are button usages, so they
are read per collection and per report: in range with the tip up is a hover and
drives nothing, and only the tracked slot's tip going up (or its in-range bit
clearing) ends the contact while coordinates keep arriving. A count-only lift
report is not a lift; the idle window ends the contact instead (18 ms for a
dismiss session, 120 ms for a band candidate). A tracked report
that carries no coordinates refreshes the liveness clock only when it asserts
contact (`TipSwitch`/`InRange`, or a slot state of Contact), never on
identifier presence alone; the liveness clock is separate from the position
clock, so that report cannot feed a position, and the idle cannot end a contact
the device is still asserting. When several slots carry the tracked identifier,
the one that asserts contact wins, and one
`hid contact id=... declared down in ... slots; using=...` line records the
ambiguity.

A device with no tip and no in-range usage keeps the count/position path; its
Contact Count and `(0,0)` rules apply in the tracked branch too, because it
cannot assert a tip for the tracked slot, and it is the only device family that
needs those rules.

Raw values are mapped to pixels through the logical range of the collection the
point was read from (`cap.LinkCollection`) and then to DIP through the window
DPI. `hid usages dev=... tip=... inrange=... count=... buttons=...
reportids=... coordpath=... identity=contactid|node` records once per device
which usages it exposes, whether it is on the count/position path, and which
name its tracked contact uses.

## Contact identity

The tracked contact is followed by its name: the Contact Identifier
(`0x0D/0x51`) when both the device and the contact carry one
(`identity=contactid`), the `(device, node index)` pair otherwise
(`identity=node`). A slot the device hands to another finger therefore does not
transfer the drag. When a report names a new contact, its real contacts are the
candidates and the one nearest the last tracked raw position is chosen; once
tracked, only that contact can move or end the gesture, and a report from
another slot or another device is ignored. The identity carries its own
evidence clock: `lastEvidenceTick` moves only when the report proves the
contact — the tracked slot's `TipSwitch`/`InRange` asserts it with no point, or
the report carries a position this path feeds. A name-only report updates the
slot and the identifier for diagnostics, then returns: no position, no liveness
and no clock write. The same 18 ms idle tick, once no session owns the contact,
forgets an identity whose last evidence is that old — admitted or not — and
logs `hid identity expired slot=... latch=0` when no contact latch held it, so
a refused admission cannot strand a name and make every later finger be
rejected as another contact.

## Release detection

The digitizer keeps reporting while the finger rests (measured 255 reports in a
1.5 s hold), so an 18 ms window without a report is a reliable lift for a
dismiss session. A band candidate is measured against a longer window
(`kCandidateIdleHoldMs`, 120 ms): a touch panel that samples sparsely, or an
injected test with 20–30 ms steps, must not lose an admitted 25 DIP flick. A
report gap alone never begins a new contact — the latch is cleared by a lift
report or by the candidate's own watchdog — so the admission origin survives the
gap and the crossing point cannot replace it. On a
device whose reports carry `TipSwitch`, the tracked slot's lift report ends the
contact instead: the release line says `src=report`, and the idle window is
only the fallback for a device that goes silent or a contact that leaves
without a lift report (`src=idle`). A report that carries only a contact count,
or no contact field for the tracked slot, says nothing about it, so a
count-only lift no longer ends a tracked contact; the idle expiry does, with
`src=idle`, and once no session owns the contact the same 18 ms latch clears
the contact flag and forgets the tracked identity. A report that names the
tracked contact and asserts it (`TipSwitch` down or `InRange`, or a slot state
of Contact) but carries no point is not silence: it refreshes the liveness
clock and the identity's evidence clock, and moves nothing.

The input layer also keeps the tracked contact's last eight positioned reports
with their `QueryPerformanceCounter` stamps, duplicates included and in report
order. The reveal's takeover replays them through its own trail mapping, so the
settle's release window spans the open instead of starting at the takeover
([gesture.md](gesture.md)). A duplicate is kept on purpose: the window, not the
buffer, is what collapses a resting finger to one value. A release that ends
before its menu opens replays the same buffer at the release, into a record the
same input thread reads when its visibility poll sees the open, so nothing is
published between them.

Measured layouts on this machine: ELAN2513 Col01 touch is five contact
collections with `Tip` on each, Col05 pen carries tip and in-range, SYNA3580 is
five with `Tip` each, and the virtual digitizer is four with `Tip` and
`In Range` plus a contact count, report ID 1. Both halves of the timing matter
on this build, and each was measured separately:

- the idle check needs a **`QueryPerformanceCounter`** silence: `GetTickCount64`
  only advances every 15.6 ms, so an 18 ms window read as 31 ms;
- the idle tick needs a **`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` waitable
  timer**: `WM_TIMER` stayed on the 15.6 ms quantum even while the process held
  a 1 ms `timeBeginPeriod(1)` request. The request is still made, paired through
  `timeEndPeriod`, for the time a contact is down.

Measured release latency (`relms` on the release line): 31 ms before, 19-23 ms
after. `vel=` on the `hid release`, `open follow` and `settle` lines is always
DIP/ms toward that settle's target, so a finger held still reads `vel=0.00` on
all three.
