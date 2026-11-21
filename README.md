**English** | [简体中文](README.zh-CN.md)

# swipe-for-10start

Touch and pen swipe gestures for the legacy Windows 10 Start menu that
ExplorerPatcher restores on Windows 11. Swipe in from the taskbar edge and the
menu opens following your finger; drag the open menu back toward that edge and
it slides away and closes. Keyboard opens and closes get the same animation.
By Zhanyuanium, distributed as Windhawk mods.

## What you need

- **Windows 11** with **ExplorerPatcher** set to the Windows 10 (legacy) Start
  menu (`Start_ShowClassicMode=1`). Verified on Windows 11 build 26300.9974 and
  ExplorerPatcher 26100.9711.
- **Windhawk** 2.0 or later.
- A touchscreen or pen. (The mouse is deliberately not used for these
  gestures.)

## The mod

One Windhawk mod ships, and it runs in both processes it needs: the controller
in StartMenuExperienceHost and the executor in Explorer.

- **The controller** owns the gesture itself: edge-pull to open, follow the
  finger, drag to dismiss, and the animation. It also consumes the executor's
  intents and schedules the close's dismiss.
- **The executor** runs in Explorer and executes the shell's own
  `ShowStartView`/`Dismiss` on the launcher's thread. The Windows key, the Start
  button and a click away publish one intent; the controller decides open or
  close from its own published state, runs the one mirrored settle, and then
  asks the executor to dismiss. A close slides out with the same animation the
  gesture close uses.
  A press during a close is read as a reopen: while the window is still on
  screen the close is cancelled and the panel settles back from where the
  retract left it; a press inside the shell's hide is latched and served when
  the hide lands. The key never goes dead. A touch pull still down when a close
  lands reopens through the controller only if it is a real pull: a finger
  resting on the taskbar through the retract does not open the menu again.
  Win+Tab, the lock screen and a display
  change pass through untouched, so a lock never waits. If the executor half is
  refused — the Explorer build does not match, or the launcher's public symbols
  cannot be resolved — the menu still opens and closes, but the close is instant.
  The launcher hooks are found by public symbol, not by address. While the
  symbols are being fetched, Explorer keeps working and every press runs the
  shell's own path; with `DiagnosticTrace` on, the log shows
  `exec launcher symbols pending ...` and then
  `exec launcher symbols resolved ... match=1`.
  The Start half withdraws its interception whenever it has no usable renderer:
  while the menu's root is being replaced or a failed geometry write has
  invalidated it, a press runs the shell's own path exactly once, and the mod
  resumes when a usable root is attached.

## Install

Install the mod from the Windhawk UI, or from an elevated PowerShell in this
repository:

```powershell
pwsh -File .\tools\install_mods.ps1
```

The script runs `tools/make_store_mods.ps1 -Profile local` first: that writes the
single-file build to `dist\local\`, with the shared headers inlined and no `-I`
in `@compilerOptions`, because an installed mod is compiled from stdin with no
repository beside it. It then installs and enables the mod. Windhawk's install
folder must be on `PATH`; `-Compiler` names `windhawk-cli` explicitly, `-All`
also installs the diagnostic probes, and `-SkipRestart` leaves the menu host
alone.

The script restarts `StartMenuExperienceHost.exe` when it finishes; do it by hand
(Task Manager → details → `StartMenuExperienceHost.exe` → end task; it restarts
itself) or sign out and back in — the mod can only attach when the menu process
creates its window. `mod install` leaves a previously disabled mod disabled, so
the script enables it; the Explorer half hooks Explorer when the mod is enabled,
without an Explorer restart on this build.

## Settings

- **Menu animation style** (`AnimationStyle`, default `Windows 11`) — the
  shape of the slide. `Windows 11` is fitted to the motion of the system's own
  panels (the Quick Settings flyout); `Windows 10` is the feel the mod shipped
  with originally, a looser spring that can overshoot slightly. Both run the
  same spring — the choice changes the feel, never the mechanism — and it also
  decides how opening and closing with the Windows key looks.
- **Animation start speed (% of swipe speed)** (`GestureStartSpeedPercent`,
  default 100, range 0-300) — how fast the menu keeps moving the moment you
  lift your finger, as a percentage of the finger's speed in the direction the
  menu is going. 100 continues at finger speed, higher values snap away
  faster, 0 always starts the animation from a standstill. A finger that was
  held still starts from rest whatever this says, and Windows-key opens always
  start from rest — this setting does not affect them.
- **Write a diagnostic trace file** (`DiagnosticTrace`, default off) — turns on
  the decision log described below. Leave it off for everyday use; a bug report
  or a maintainer will ask for it.

## What works where

- Taskbar on the **left**, **top** or **bottom** — verified. The **right**
  side runs the same code path and was exercised earlier but is not verified on
  the current setup. **Auto-hide** is supported (it is the daily
  configuration).
- **Touch** works. **Pen** input is decoded (hover is ignored) but has not yet
  been exercised by hand.
- On a top or bottom taskbar, a vertical swipe inside the open menu scrolls
  what is under your finger — the app list, or the grid of pinned tiles — and
  becomes a dismiss only once that area reaches its end.
- A pull that starts just inside the screen edge — up to about half a taskbar's
  height, the strip the system's own swipe opens from — opens the menu, and the
  menu follows the finger from the frame it appears. A tap on the taskbar does
  not open it: the mod asks for the menu only after the finger has travelled
  24 DIP away from the edge, so a tap on a pinned app, the clock, the tray or
  the Start button stays a native press. A pull that starts further
  in does not open it: only the taskbar edge does that.
- Mouse drag is deliberately not enabled.

## Try it

- Swipe in from the taskbar edge: the menu opens and follows your finger. A
  swipe that begins a little further in, about half a taskbar's height, keeps
  up with your hand too instead of snapping open. A quick flick that ends
  before the menu appears still carries its speed into the opening motion, and
  the menu rises from the bottom edge instead of appearing part-way open. A
  short or reversed pull springs back; a longer pull or a flick keeps it open.
- With the menu open, drag it toward the taskbar edge to dismiss it — past a
  tenth of the way on a top or bottom taskbar (a fifth on a side one), or with
  a flick, it commits and slides out.
- Press the Windows key: the same animation plays on open and on close.

## If something looks wrong

- The mod can write a decision log into two files in its Windhawk storage
  folder, which Windhawk shows on the mod's page: `renderer.log` from the Start
  half and `bridge.log` from the Explorer half, both under
  `local@swipe-for-10start`. They are written only while **Write a diagnostic
  trace file** (`DiagnosticTrace`) is on; turn it on once from an elevated
  shell with
  `windhawk-cli mod settings set local@swipe-for-10start DiagnosticTrace=true`
  or from the mod's settings, then reproduce the problem.
- After disabling and re-enabling the mod, restart
  `StartMenuExperienceHost.exe` — gestures cannot attach to the already-open
  menu window.
- The root hook is found by public symbol, not by address. If `StartUI_.dll`
  does not carry `StartUI::App::PrepareRootElement` (or its symbols cannot be
  read), no gesture starts and the log shows `startui root symbol unresolved
  ...`; the shell then handles every press natively.
- While the menu is being dragged, its contents still receive touches at their
  original positions — a known limit, along with the assumption that the touch
  panel covers the primary monitor.
- The full technical documentation — what was measured, what was falsified,
  and every design decision — lives in [docs/](docs/README.md), including the
  complete [known-limits list](docs/limits.md).
- [CHANGELOG.md](CHANGELOG.md) lists each release tag with the version it
  shipped.
