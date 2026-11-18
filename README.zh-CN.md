[English](README.md) | **简体中文**

# swipe-for-10start

为 ExplorerPatcher 在 Windows 11 上恢复的经典 Windows 10 开始菜单提供触摸
和手写笔滑动手势。从任务栏所在的屏幕边缘划入，菜单会跟随手指打开；把已
打开的菜单往该边缘拖回，菜单会滑出并关闭。用键盘打开或关闭菜单时播放同
一套动画。作者 Zhanyuanium，以 Windhawk 模组的形式发布。

## 你需要什么

- **Windows 11**，且 **ExplorerPatcher** 已设置为 Windows 10（经典）开始
  菜单（`Start_ShowClassicMode=1`）。已在 Windows 11 build 26300.9974 和
  ExplorerPatcher 26100.9711 上验证。
- **Windhawk** 2.0 或更高版本。
- 触摸屏或手写笔。（本模组刻意不响应鼠标拖动。）

## 两个模组

两个模组一起发布，完整体验需要两个都装：

- **Start menu legacy swipe**（`start-menu-legacy-swipe`）——手势本体：
  边缘划入打开、跟随手指、拖动关闭，以及滑动动画。
- **Start menu touch gesture bridge**（`start-menu-touch-gesture-bridge`）——
  运行在 Explorer 中，让按 Windows 键或点击别处关闭菜单时同样播放滑出
  动画。没有它菜单照常关闭，但是瞬间消失。

## 安装

在 Windhawk 界面中安装两个模组，或在以管理员身份运行的 PowerShell 中执行：

```powershell
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod install --file .\mods\start-menu-legacy-swipe.wh.cpp
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod install --file .\mods\start-menu-touch-gesture-bridge.wh.cpp
& 'C:\Program Files\Windhawk\windhawk-cli.exe' mod enable local@start-menu-touch-gesture-bridge
```

然后重启 `StartMenuExperienceHost.exe`（任务管理器 → 详细信息 →
`StartMenuExperienceHost.exe` → 结束任务，它会自动重启），或者注销并重新
登录——模组只能在菜单进程创建窗口时挂载。`mod install` 会让先前被禁用
的模组保持禁用，所以上述 `enable` 一行是 Windows 键关闭动画所必需的；
bridge 在启用时挂钩 Explorer，在当前系统版本上不需要重启 Explorer。

## 设置项

- **菜单动画样式**（`AnimationStyle`，默认 `Windows 11`）——滑动动画的
  形状。`Windows 11` 按系统自带面板（快速设置弹出面板）的动作拟合；
  `Windows 10` 是本模组最初发布时的手感，弹簧更松，可能略微过冲。两者跑
  的是同一条弹簧——这个选择只改手感，不改机制——它同时决定用 Windows
  键打开或关闭菜单时的样子。
- **动画起步速度（占滑动速度的百分比）**（`GestureStartSpeedPercent`，
  默认 100，范围 0–300）——松手瞬间菜单继续滑动的速度，以手指朝菜单目
  标方向的速度分量的百分比计。100 表示按手指速度继续；更大的值甩得更
  快；0 总是从静止起步。手指停住再抬起时无论设多少都从静止起步；用
  Windows 键打开也总是从静止起步——这个设置不影响它们。

## 支持的环境

- 任务栏在**左侧**、**顶部**或**底部**——已验证。**右侧**走的是同一
  代码路径，早前验证过，但未在当前环境上验证。支持**自动隐藏**（这是
  日常使用的配置）。
- **触摸**可用。**手写笔**输入已解码（悬停被忽略），但尚未在真机上
  实测过。
- 任务栏在顶部或底部时，在已打开的菜单内上下滑动会先滚动手指下方的
  内容——应用列表，或固定磁贴的网格——该区域滚到底之后同一滑动才会
  变成关闭手势。
- 从屏幕边缘稍微靠里一点起手——大约半个任务栏高，也就是系统自带滑动
  的起手范围——菜单会打开，并且从出现的那一帧起就跟随手指。起手位置
  更靠里则不会打开菜单：只有任务栏边缘能打开它。
- 鼠标拖动刻意未启用。

## 上手一试

- 从任务栏边缘划入：菜单打开并跟随手指。从稍微靠里一点（约半个任务
  栏高）起手也会跟随手指，不会在手底下直接弹开。拉得短或往回推会弹
  回去；拉得更长或快速一甩则保持打开。
- 菜单打开时，把它往任务栏边缘拖即可关闭——任务栏在顶部或底部时拖过约
  十分之一行程（在侧边时约五分之一），或快速一甩，手势即成立，菜单滑出。
- 按 Windows 键：打开和关闭时播放同一套动画。

## 如果看起来不对

- 模组日志写在各自的 Windhawk 存储目录：
  `C:\ProgramData\Windhawk\Engine\ModsWritable\mod-storage\local@start-menu-legacy-swipe\renderer.log`
  和 `...\local@start-menu-touch-gesture-bridge\bridge.log`。
- 禁用再启用模组后要重启 `StartMenuExperienceHost.exe`——手势无法挂到
  已经存在的菜单窗口上。
- 本模组要求 `StartUI_.dll` 的版本正好是 `10.0.22621.3733`；其他版本
  不会启动任何手势，日志里会看到 `startui root hook skipped ...`。
- 拖动菜单期间，菜单内容仍在原位置接收触摸——这是已知限制；另一条
  已知限制是假设触摸面板覆盖主显示器。
- 完整的技术文档——测量数据、被证伪的方案、每项设计决定的理由——在
  [docs/](docs/README.md)（英文），包括完整的[已知限制列表](docs/limits.md)。
