#requires -Version 7.0
<#
.SYNOPSIS
Inject a touch drag, keep the contact reporting while the screen is captured,
then lift.

.DESCRIPTION
The virtual digitizer stops reporting after the last injected report, while the
real panel reports at ~170 Hz. A plain hold therefore goes silent for longer
than the mod's 18 ms idle release and the capture shows the post-release state.
This script starts the shared keep-alive (tools/touch_common.ps1, one UPDATE
every 5 ms) for as long as the screenshot takes, so the reveal follow is still
live when the pixels are read. End-Contact lifts after the capture.

Coordinates are physical screen pixels (this machine is 2880x1800 at 200%).

.EXAMPLE
pwsh -File tools/follow_hold.ps1 -StartX 0 -StartY 900 -HoldX 900 -HoldY 900
#>
[CmdletBinding()]
param(
    [int]$StartX = 0,
    [int]$StartY = 900,
    [int]$HoldX = 900,
    [int]$HoldY = 900,
    [int]$Steps = 30,
    [int]$StepDelayMs = 10,
    [string]$ScreenshotPath = "$env:LOCALAPPDATA\Temp\opencode\follow.png"
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'touch_common.ps1')

Start-Contact -X $StartX -Y $StartY
Glide-Contact -X $HoldX -Y $HoldY -Steps $Steps -DelayMs $StepDelayMs

try {
    Start-ContactHold -X $HoldX -Y $HoldY

    # Capture inline: spawning screenshot.ps1 as a child process took long
    # enough that the mod's idle-release timer fired before the pixels were read.
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    } finally {
        $g.Dispose()
    }
    $bmp.Save($ScreenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "SHOT path=$ScreenshotPath width=$($bounds.Width) height=$($bounds.Height) hold=$HoldX,$HoldY"
} finally {
    Stop-ContactHold
    End-Contact
}

'LIFTED'
