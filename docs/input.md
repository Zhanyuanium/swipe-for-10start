# Input layer — Raw HID, decode, identity, release

> Part of [docs/README.md](README.md). Member list: [../mods/AGENTS.md](../mods/AGENTS.md).

The legacy Start has no consumer for the shell's touch gesture. Every
in-process source was measured and is empty: XAML pointer events report
`PointerDeviceType.Mouse` with one move per drag, `GetCursorPos` stays
`CURSOR_SUPPRESSED`, `GetPointerInfo` returns a frozen down snapshot, the queue
closes after the down, and `RegisterPointerInputTarget` needs UIAccess. Raw
Input is the only source that works.

A worker in the `start` half registers Raw Input for the digitizer
(`0x0D/0x04` touch, `0x0D/0x02` pen, `RIDEV_INPUTSINK`) on a hidden **top-level**
window: a message-only window receives no `WM_SETTINGCHANGE`/`WM_DISPLAYCHANGE`,
which are the whole event-driven axis refresh.

## What wakes the thread

No period. One high-resolution one-shot is armed for the soonest deadline only:

| Deadline | Clock | Window |
|---|---|---|
| coast start | QPC | 8 ms, later for a fresh follow |
| idle release of a dismiss session | QPC | 18 ms |
| band candidate watchdog / contact latch | QPC + tick | 120 ms / 18 ms |
| close repair bound | tick | 800 ms after acceptance |
| executor budget (show / dismiss) | tick | 400 ms, or `executeAt + 400` |
| fallback under a transition / root probe | tick | 100 ms |

`NextInputDeadlineMs` returns the minimum or 0 (disarmed). A held release (a
touch show in flight, a close that owns the panel) contributes no deadline: its
hold ends on a cloak event or an executor answer.

The open/close state is pushed: `EVENT_OBJECT_CLOAKED`/`UNCLOAKED` on the
CoreWindow. The callback sets an auto-reset event and nothing else; the service
reads `DWMWA_CLOAKED` once, and a failed read is **unknown**, never hidden. A
requested or just-observed transition opens a bounded watch (1000 ms for a
request, 500 ms after an observation) with a slow 100 ms fallback poll.

The only idle wake is the 100 ms `peerTick` heartbeat, while the peer is
advertised. Measured with the menu closed: **190.6 wakes/s and 32 DWM queries/s
before, 9.2 wakes/s and 0 queries/s after**; Start host idle CPU 0.625 % →
0.026 % of one core.

## Decode

`GetRawInputDeviceInfoW(RIDI_PREPARSEDDATA)` + `HidP_GetUsageValue`: contact
collections `0x0D/0x22` (Finger) or `0x0D/0x05` carry `0x01/0x30` (X) and
`0x01/0x31` (Y); top-level `0x0D/0x54` is the Contact Count.

- A device whose caps declare report IDs carries the ID in the first raw byte
  (`RAWINPUTHEADER::wParam` is only `RIM_INPUT`/`RIM_INPUTSINK`); that byte
  selects the caps. A report ID with no contact field is ignored whole, which is
  distinct from a decode failure (`hid decodefail`).
- `TipSwitch` (`0x0D/0x42`) and `InRange` (`0x0D/0x32`) are button usages, read
  per collection and per report: in range with the tip up is a hover. Only the
  tracked slot's tip up (or its in-range bit clearing) ends the contact while
  coordinates keep arriving; a count-only lift report is not a lift.
- A tracked report with no coordinates refreshes the liveness clock only when it
  asserts contact. Raw values map to pixels through the collection's logical
  range then to DIP through the window DPI.

## Contact identity

The tracked contact is named by its Contact Identifier (`0x0D/0x51`) when both
device and contact carry one, otherwise by `(device, node index)`
(`identity=contactid|node`). A slot handed to another finger does not transfer
the drag; a new contact's candidates are its real contacts, nearest the last raw
position.

- The identity's evidence clock moves only on a report that proves the contact
  (asserted tip/in-range without a point, or a fed position); a name-only report
  updates the slot and identifier for diagnostics, then returns.
- With no session owning the contact, the 18 ms idle forgets an identity whose
  last evidence is that old (`hid identity expired ... latch=0`).
- A contact a close consumed is the exception: while a close is in flight neither
  the latch nor the identity expires on silence. A Contact Identifier ends on its
  lift or on a report naming another asserted identifier (`hid consumed ended ...
  reason=new-contact|device`); a node index and the coordinate-only path keep the
  idle fallbacks (120 ms, then 18 ms) once the close has ended.

## Release

The digitizer keeps reporting while the finger rests (255 reports in a 1.5 s
hold), so 18 ms without a report is a reliable lift for a dismiss session; a
band candidate uses 120 ms so a sparse or 20–30 ms injected stream does not lose
an admitted flick. A report gap alone never begins a new down.

- A tracked lift report ends the contact (`src=report`); the idle is the fallback
  for a silent device (`src=idle`).
- The idle check needs a `QueryPerformanceCounter` silence: `GetTickCount64`
  advances every 15.6 ms, so an 18 ms window would read as 31 ms. The deadline
  needs a `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` one-shot: `WM_TIMER` stayed on
  the 15.6 ms quantum even under `timeBeginPeriod(1)`. `SetTimer` is the
  fallback, at worst 15.6 ms late. Measured release latency: 31 ms with the old
  periodic tick, **18–19 ms** now.

The input layer keeps the last eight positioned reports with their QPC stamps,
duplicates included; the reveal takeover replays them so the release window spans
the open. Measured devices: ELAN2513 Col01 (five contact collections with
`Tip`), Col05 pen (tip and in-range), SYNA3580 (five with `Tip`), and the virtual
digitizer (four with `Tip` and `In Range`, report ID 1).
