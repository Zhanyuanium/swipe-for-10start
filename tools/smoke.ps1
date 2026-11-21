#requires -Version 7.0
<#
.SYNOPSIS
One-pass regression smoke for the shipping pair, on whichever taskbar edge is live.

.DESCRIPTION
Drives the four triggers through their real paths and checks the mods' own log
lines instead of pixels: a VK_LWIN tap opens and closes the menu, an injected
edge pull opens it, an injected panel drag closes it, and a click on the desktop
closes it. Every step is judged by the state report from start_menu.ps1 plus the
decision and executor lines the pair wrote during that step, and the last step
fails if the run produced any error-shaped line (a non-zero HRESULT, a timeout, a
refusal, a dropped press, a lost latch).

Each row requires the pair its claim rests on, not a substring: a touch open is
`reveal pull qualified` plus the executor's ack for the exact show sequence the
Start side published, a touch close is the method-1 dismiss with its ack, and the
click-away is `intent close trigger=clickaway ... method=7` in the bridge log,
the matching method-6 dismiss with its ack, and `win close observed`. A close
the shell performed natively cannot satisfy the click-away row.

The mod writes those lines to disk only while its DiagnosticTrace setting is on
(Wh_Log always receives them, but Windhawk's own debug logging is a separate
switch). The script checks that one of the two files was written during the run
and otherwise prints the one command that enables the trace and exits non-zero.
With the trace on it needs no other switch.

The injection geometry is read from the mod's own last `reveal panelTrail=...`
line — its axis, band and panel rectangle — so the same tool works on a bottom,
top, left or right taskbar; the coordinates it feeds are physical pixels, scaled
from the DIP values by the primary screen's factor.

Three details it encodes, each learned the hard way:
- The auto-hidden bar cannot be opened through UIA, so the open step uses the
  VK_LWIN tap the keyboard path uses; the Start button is a manual check.
- A click-away is only decided once the window is past its presentation
  (state 1/3); clicking earlier is swallowed on purpose, so the step waits for the
  executor's own `exec visible ... visible=visible` confirmation before it clicks.
- An injected contact is occasionally decoded as a hover rather than a touch; the
  pull and the drag are therefore retried before a step is called failed.

.PARAMETER Log
The Start half's `renderer.log`. Default: the standard Windhawk data root under
`%ProgramData%`; pass this when Windhawk keeps its data elsewhere.

.PARAMETER BridgeLog
The explorer half's `bridge.log`. Defaults to `bridge.log` beside `-Log`.

.PARAMETER SkipClickAway
Do not click the desktop. Use it when the desktop is covered by a window that
should not lose focus.

.EXAMPLE
pwsh -File tools/smoke.ps1
#>
[CmdletBinding()]
param(
    [string]$Log = (Join-Path $env:ProgramData 'Windhawk\Engine\ModsWritable\mod-storage\local@swipe-for-10start\renderer.log'),
    [string]$BridgeLog = '',
    [switch]$SkipClickAway
)

$ErrorActionPreference = 'Stop'
$runStart = Get-Date

if (-not $BridgeLog) {
    $BridgeLog = Join-Path (Split-Path -Parent $Log) 'bridge.log'
}

# The trace must be on for *this* run: a file left by an earlier session is
# stale, and every row would read old lines. The files are written by the mod,
# not by this script, so this runs after the first trigger has had a chance to
# write (see step 1). A missing file before that is not an error: enabling the
# trace at runtime does not write a line by itself, and the first trigger will.
function Test-TraceFresh {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return (Get-Item -LiteralPath $Path).LastWriteTime -ge $runStart
}
function Assert-TraceReady {
    if ((Test-TraceFresh $Log) -or (Test-TraceFresh $BridgeLog)) { return }
    Write-Host ("Neither log was written during this run (started " +
        "$($runStart.ToString('HH:mm:ss.fff'))), so the mod's DiagnosticTrace " +
        'setting is off.')
    Write-Host 'Turn it on once from an elevated shell, then run the smoke again:'
    Write-Host '  windhawk-cli mod settings set local@swipe-for-10start DiagnosticTrace=true'
    exit 1
}

$startMenu = Join-Path $PSScriptRoot 'start_menu.ps1'
$inject = Join-Path $PSScriptRoot 'inject_swipe.ps1'
$rows = New-Object System.Collections.Generic.List[object]

if (-not ('Smoke.Native' -as [type])) {
    Add-Type -Namespace Smoke -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, System.UIntPtr e);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(System.IntPtr context);
public static void SetDpiAware() {
    // PMv2: the injector takes physical pixels, so the metrics must not be virtualised.
    try { SetProcessDpiAwarenessContext(new System.IntPtr(-4)); } catch { }
}
public static void WinTap() {
    keybd_event(0x5B, 0, 0, System.UIntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    keybd_event(0x5B, 0, 2, System.UIntPtr.Zero);
}
public static void Click(int x, int y) {
    SetCursorPos(x, y);
    System.Threading.Thread.Sleep(60);
    mouse_event(0x0002, 0, 0, 0, System.UIntPtr.Zero);
    mouse_event(0x0004, 0, 0, 0, System.UIntPtr.Zero);
}
'@
}

[Smoke.Native]::SetDpiAware()

function Mark {
    # The trace rotates at its line cap, so a line index is not a stable cursor:
    # a read after the rotation would skip the new file's lines. Every line
    # carries `t=<GetTickCount64>`; a mark is that clock, and every read re-opens
    # the file and keeps the lines at or after it.
    return [Environment]::TickCount64
}
function New-Lines { param($fromTick, [string]$Path = $Log)
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    return @(Get-Content -LiteralPath $Path | Where-Object {
        $_ -match '^t=(\d+) ' -and [long]$Matches[1] -ge [long]$fromTick
    })
}
function State {
    $out = (& pwsh -NoProfile -File $startMenu 2>&1) -join ' '
    if ($out -match 'open=(\w+)') { return ($Matches[1] -eq 'True') }
    return $null
}
function Add-Row { param($step, $ok, $detail = '') $rows.Add([pscustomobject]@{ Step = $step; Ok = [bool]$ok; Detail = $detail }) }
function Wait-For { param($want, $timeoutMs = 3000)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        if ((State) -eq $want) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}
function Wait-Log { param($fromTick, $pattern, $timeoutMs = 2500, [string]$Path = $Log)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        if (((New-Lines $fromTick $Path) -join "`n") -match $pattern) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

# A row's evidence is a pair, not a substring: the sequence the Start side
# published and the executor's answer for exactly that sequence. A broad
# `exec show` would also accept a refused command or a later row's show.
function Test-ExecutedShow { param([string]$text)
    foreach ($match in [regex]::Matches($text, 'exec show request seq=(\d+)')) {
        $seq = $match.Groups[1].Value
        if ($text -match "exec ack seq=\d+ command=$seq state=1\b") { return $true }
    }
    return $false
}
function Test-ExecutedDismiss { param([string]$text, [int]$method)
    foreach ($match in [regex]::Matches($text, "exec dismiss request seq=(\d+) method=$method\b")) {
        $seq = $match.Groups[1].Value
        if ($text -match "exec ack seq=\d+ command=$seq state=(1|4)\b") { return $true }
    }
    return $false
}

# The geometry the mod itself last published: axis (0 left, 1 top, 2 right,
# 3 bottom), band in DIP, panel rectangle in DIP. Physical pixels are DIP times
# the primary screen's scale, which is what InjectTouchInput expects.
#
# The layout fields (axis, band, rectangle) must come from this run: `-Since` is
# the run's tick mark, so a rotated trace or a previous session cannot supply
# historical coordinates. The scale is the monitor DPI, not a layout fact, and the
# bar's thickness in DIP is constant, so its ratio may come from the last
# `dismiss` line in the file even when that line predates the run; every field is
# still reported separately and the caller refuses to inject unless all four were
# measured.
function Get-Layout {
    param([long]$Since = 0)
    $allLines = @()
    if (Test-Path -LiteralPath $Log) { $allLines = @(Get-Content -LiteralPath $Log) }
    $lines = @($allLines | Where-Object {
        $_ -match '^t=(\d+) ' -and [long]$Matches[1] -ge $Since
    })
    $layout = [pscustomobject]@{
        Axis = 3; Low = 804; High = 900; Left = 236; Top = 265; Right = 1229; Bottom = 840
        Scale = 2
        AxisKnown = $false; BandKnown = $false; RectKnown = $false; ScaleKnown = $false
        Complete = $false
    }
    # The axis survives even when the panel rectangle was rejected (a left or
    # right bar can put the panel off the edge the trail expects): the fallback
    # line has the axis, the dismissal line has it too, and the rejected line
    # still carries the measured rectangle the drag needs.
    $axisLine = $lines | Select-String 'panelTrail=.*axis=\d|dismiss axis=\d' | Select-Object -Last 1
    if ($axisLine -and $axisLine.Line -match 'axis=(\d)') {
        $layout.Axis = [int]$Matches[1]; $layout.AxisKnown = $true
    }
    $bandLine = $lines | Select-String 'band=(-?\d+),(-?\d+)' | Select-Object -Last 1
    if ($bandLine -and $bandLine.Line -match 'band=(-?\d+),(-?\d+)') {
        $layout.Low = [int]$Matches[1]; $layout.High = [int]$Matches[2]
        $layout.BandKnown = $true
    }
    $rectLine = $lines | Select-String 'reveal .*rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)' | Select-Object -Last 1
    if ($rectLine -and $rectLine.Line -match 'rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)') {
        $layout.Left = [int]$Matches[1]; $layout.Top = [int]$Matches[2]
        $layout.Right = [int]$Matches[3]; $layout.Bottom = [int]$Matches[4]
        $layout.RectKnown = $true
    }
    # The scale from the taskbar's own rect: the `dismiss` line prints it in
    # physical pixels, and the band is twice the bar in DIP. Their ratio is the
    # monitor scale without assuming a screen size or a docked bar. It stays
    # fractional: 125 % and 150 % are 1.25 and 1.5, and rounding them puts every
    # injected point off the band or off the panel.
    $barLine = $allLines | Select-String 'dismiss axis=\d.*rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)' | Select-Object -Last 1
    $layout.Scale = 0
    $bandDip = $layout.High - $layout.Low
    if ($barLine -and $layout.BandKnown -and $bandDip -gt 0 -and
        $barLine.Line -match 'rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)') {
        $barWidth = [int]$Matches[3] - [int]$Matches[1]
        $barHeight = [int]$Matches[4] - [int]$Matches[2]
        $barPhysical = if ($layout.Axis -eq 0 -or $layout.Axis -eq 2) { $barWidth } else { $barHeight }
        if ($barPhysical -gt 0) {
            $layout.Scale = $barPhysical / ($bandDip / 2.0)
            $layout.ScaleKnown = $true
        }
    }
    if ($layout.Scale -lt 1) { $layout.Scale = 1 }
    $layout.Complete = $layout.AxisKnown -and $layout.BandKnown -and
        $layout.RectKnown -and $layout.ScaleKnown
    return $layout
}

# The fields a refused layout is missing, for the row detail.
function Get-LayoutGap { param($layout)
    $missing = @()
    if (-not $layout.AxisKnown) { $missing += 'axis' }
    if (-not $layout.BandKnown) { $missing += 'band' }
    if (-not $layout.RectKnown) { $missing += 'rect' }
    if (-not $layout.ScaleKnown) { $missing += 'scale' }
    return ($missing -join ',')
}

# The pull: from just inside the band toward the desktop, ~450 DIP of travel.
function Get-PullPoints { param($layout)
    $w = [Smoke.Native]::GetSystemMetrics(0); $h = [Smoke.Native]::GetSystemMetrics(1)
    $s = $layout.Scale
    switch ($layout.Axis) {
        0 { return @([int](($layout.High - 2) * $s), [int]($h / 2), [int](($layout.Low + 450) * $s), [int]($h / 2)) }
        1 { return @([int]($w / 2), [int](($layout.Low + 2) * $s), [int]($w / 2), [int](($layout.High + 450) * $s)) }
        2 { return @([int](($layout.Low + 2) * $s), [int]($h / 2), [int](($layout.High + 450) * $s), [int]($h / 2)) }
        default { return @([int]($w / 2), [int](($layout.High - 2) * $s), [int]($w / 2), [int](($layout.Low - 450) * $s)) }
    }
}

# The dismiss: from the panel's middle toward the taskbar, ~300 DIP.
function Get-DragPoints { param($layout)
    $s = $layout.Scale
    $cx = [int]((($layout.Left + $layout.Right) / 2) * $s)
    $cy = [int]((($layout.Top + $layout.Bottom) / 2) * $s)
    switch ($layout.Axis) {
        0 { return @($cx, $cy, [int]($cx - 300 * $s), $cy) }
        1 { return @($cx, $cy, $cx, [int]($cy - 300 * $s)) }
        2 { return @($cx, $cy, [int]($cx + 300 * $s), $cy) }
        default { return @($cx, $cy, $cx, [int]($cy + 300 * $s)) }
    }
}

$runMark = Mark

# 0. clean start
if (State) { [Smoke.Native]::WinTap(); Start-Sleep -Milliseconds 900 }
Add-Row 'starts closed' (Wait-For $false 3000) "state=$(State)"

# 1. the keyboard path opens. This warm-up open is also what refreshes the mod's
# geometry, so the layout is read *after* it: the geometry snapshot a run starts
# with can belong to a rotated trace or a bar that moved since.
$m = Mark
[Smoke.Native]::WinTap()
$opened = Wait-For $true 3000
Assert-TraceReady
Start-Sleep -Milliseconds 400
$l = (New-Lines $m) -join "`n"
$winOpen = ($l -match 'intent recv seq=\d+ kind=open trigger=win\b') -and
    (Test-ExecutedShow $l)
Add-Row 'win open' ($opened -and $winOpen) ''

# The layout the injected steps will use, from this run's own lines only.
$layout = Get-Layout -Since $runMark
$layoutGap = Get-LayoutGap $layout
if (-not $layout.Complete) {
    Write-Host "layout incomplete for this run (missing: $layoutGap); injecting nothing"
}

# 2. and closes
$m = Mark
[Smoke.Native]::WinTap()
$closed = Wait-For $false 3000
Start-Sleep -Milliseconds 400
$l = (New-Lines $m) -join "`n"
$winClose = ($l -match 'intent recv seq=\d+ kind=close trigger=win\b') -and
    (Test-ExecutedDismiss $l 1)
Add-Row 'win close' ($closed -and $winClose) ''

# 3. an injected edge pull opens it (admission, takeover, show); retried because
# an injected contact is occasionally decoded as a hover. The wait is deliberate:
# a pull that starts inside the previous close's settle is tracked as the reopen
# that close owes, not as a fresh admission, and its log lines differ.
Start-Sleep -Milliseconds 1200
if (-not $layout.Complete) {
    Add-Row 'touch open' $false "layout incomplete: $layoutGap"
} else {
$pull = Get-PullPoints $layout
$opened = $false; $detail = ''
for ($i = 1; $i -le 3 -and -not $opened; $i++) {
    $m = Mark
    & pwsh -NoProfile -File $inject -StartX $pull[0] -StartY $pull[1] -EndX $pull[2] -EndY $pull[3] -Steps 40 -DelayMs 8 | Out-Null
    $opened = Wait-For $true 4000
    Start-Sleep -Milliseconds 400
    $l = (New-Lines $m) -join "`n"
    $opened = $opened -and ($l -match 'reveal pull qualified') -and
        (Test-ExecutedShow $l)
    if (-not $opened) { $detail = "attempt $i"; Start-Sleep -Milliseconds 400 }
}
Add-Row 'touch open' $opened $detail
}

# 4. an injected panel drag closes it
if (-not $layout.Complete) {
    Add-Row 'touch close' $false "layout incomplete: $layoutGap"
} else {
$drag = Get-DragPoints $layout
$closed = $false; $detail = ''
for ($i = 1; $i -le 3 -and -not $closed; $i++) {
    $m = Mark
    & pwsh -NoProfile -File $inject -StartX $drag[0] -StartY $drag[1] -EndX $drag[2] -EndY $drag[3] -Steps 30 -DelayMs 8 | Out-Null
    $closed = Wait-For $false 4000
    Start-Sleep -Milliseconds 400
    $l = (New-Lines $m) -join "`n"
    $bridgeLines = (New-Lines $m $BridgeLog) -join "`n"
    # An injected drag is delivered as a pointer, so the shell closes the panel
    # through its own click-away; a real digitizer takes the HID producer. Both
    # are the mod's own close only with the line that identifies the producer and
    # an executed command for the exact sequence it published.
    $hidClose = ($l -match 'close dismiss kind=') -and
        (Test-ExecutedDismiss $l 1)
    $clickClose = ($bridgeLines -match 'intent close trigger=clickaway[^\r\n]*method=7') -and
        (Test-ExecutedDismiss $l 6)
    $closed = $closed -and ($hidClose -or $clickClose)
    if (-not $closed) { $detail = "attempt $i"; Start-Sleep -Milliseconds 400 }
}
Add-Row 'touch close' $closed $detail
}

# 5. a click on the desktop closes it, once the window is past its presentation
if ($SkipClickAway) {
    Add-Row 'click-away' $true 'skipped'
} else {
    $m = Mark
    [Smoke.Native]::WinTap()
    if (-not (Wait-For $true 3000)) {
        Add-Row 'click-away' $false 'menu did not open'
    } else {
        # The window's presentation is ~300 ms and a click inside it is swallowed on
        # purpose. The executor's `visible=visible` line is in the bridge log, so
        # wait for it; a time-based wait could click inside the uncloak.
        $shown = Wait-Log $m 'exec visible seq=\d+ [^\r\n]*visible=visible' 3000 $BridgeLog
        if (-not $shown) { Start-Sleep -Milliseconds 900 }
        [Smoke.Native]::Click([int]([Smoke.Native]::GetSystemMetrics(0) * 0.85), [int]([Smoke.Native]::GetSystemMetrics(1) * 0.15))
        $closed = Wait-For $false 3000
        # The landing, not just the closed state: `win close observed` is the line
        # that says the shell hid the window and the controller saw it.
        $landed = Wait-Log $m 'win close observed' 2500
        $startLines = (New-Lines $m) -join "`n"
        $bridgeLines = (New-Lines $m $BridgeLog) -join "`n"
        # A native method-7 close is not this mod's close: it leaves no
        # `intent close trigger=clickaway` line and this row fails.
        $intent = $bridgeLines -match 'intent close trigger=clickaway[^\r\n]*method=7'
        $executed = Test-ExecutedDismiss $startLines 6
        Add-Row 'click-away' ($closed -and $landed -and $intent -and $executed) ''
    }
}

# 6. nothing error-shaped was written during this run, in either log: a failed
# command or a refused trigger is reported by the side that answered it.
$bad = @()
foreach ($path in @($Log, $BridgeLog)) {
    $bad += New-Lines $runMark $path | Where-Object {
        $_ -match 'hr=(?!0{8}\b)|timeout|refused|press .* lost|exec .* failed|executor not ready|latch replaced'
    }
}
Add-Row 'no error lines' ($bad.Count -eq 0) (($bad | Select-Object -First 2) -join ' | ')

if (State) { [Smoke.Native]::WinTap(); Start-Sleep -Milliseconds 900 }

$rows | Format-Table -AutoSize
if ($rows | Where-Object { -not $_.Ok }) { 'SMOKE FAILED'; exit 1 } else { 'SMOKE PASSED'; exit 0 }
