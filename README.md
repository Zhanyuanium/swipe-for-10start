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

## The two mods

Two mods ship together, and both are needed for the full experience:

- **Start menu legacy swipe** (`start-menu-legacy-swipe`) — the gesture
  itself: edge-pull to open, follow the finger, drag to dismiss, and the
  animation.
- **Start menu touch gesture bridge** (`start-menu-touch-gesture-bridge`) —
  runs in Explorer and makes closing the menu with the Windows key or by
  clicking away play the same slide-out. Without it the menu still closes, but
  instantly.

## Install

Install both mods from the Windhawk UI, or from an elevated PowerShell:

```powershell
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod install --file .\mods\start-menu-legacy-swipe.wh.cpp
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod install --file .\mods\start-menu-touch-gesture-bridge.wh.cpp
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod enable local@start-menu-touch-gesture-bridge
```

Then restart `StartMenuExperienceHost.exe` (Task Manager → details →
`StartMenuExperienceHost.exe` → end task; it restarts itself) or sign out and
back in — the mod can only attach when the menu process creates its window.
`mod install` leaves a previously disabled mod disabled, so the `enable` line
is required for the Windows-key close animation; the bridge hooks Explorer
when it is enabled, without an Explorer restart on this build.

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
  menu follows the finger from the frame it appears. A pull that starts further
  in does not open it: only the taskbar edge does that.
- Mouse drag is deliberately not enabled.

## Try it

- Swipe in from the taskbar edge: the menu opens and follows your finger. A
  swipe that begins a little further in, about half a taskbar's height, keeps
  up with your hand too instead of snapping open. A short or reversed pull
  springs back; a longer pull or a flick keeps it open.
- With the menu open, drag it toward the taskbar edge to dismiss it — past a
  tenth of the way on a top or bottom taskbar (a fifth on a side one), or with
  a flick, it commits and slides out.
- Press the Windows key: the same animation plays on open and on close.

## If something looks wrong

- The mods log to their Windhawk storage folders:
  `C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\local@start-menu-legacy-swipe\renderer.log`
  and `...\local@start-menu-touch-gesture-bridge\bridge.log`.
- After disabling and re-enabling the mod, restart
  `StartMenuExperienceHost.exe` — gestures cannot attach to the already-open
  menu window.
- The mod requires `StartUI_.dll` build `10.0.22621.3733`; on any other build
  no gesture starts and the log shows `startui root hook skipped ...`.
- While the menu is being dragged, its contents still receive touches at their
  original positions — a known limit, along with the assumption that the touch
  panel covers the primary monitor.
- The full technical documentation — what was measured, what was falsified,
  and every design decision — lives in [docs/](docs/README.md), including the
  complete [known-limits list](docs/limits.md).
