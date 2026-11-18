#requires -Version 7.0
<#
.SYNOPSIS
Inject a touch drag, hold the contact, capture the screen, then optionally lift.

.DESCRIPTION
Used to tell a real finger-following translation from an animation that keeps
running on its own: the screenshot is taken while the contact is still down at
the held position, so the content must be sitting at that position.

The hold is the shared keep-alive from tools/touch_common.ps1: an UPDATE every
5 ms, below the mod's 18 ms idle release. The contact therefore stays down for
the whole -HoldMs and for the capture, which is what makes the screenshot a
mid-gesture frame.

Coordinates are physical screen pixels (this machine is 2880x1800 at 200%).

.PARAMETER ReleaseAtEnd
Send the UP in this run, after the capture, so the gesture completes. Formerly
named -Lift, which was misleading: the script re-injected the whole drag before
lifting, and a later process cannot lift a contact this one owns. -Lift is kept
as an alias so existing calls still work. Without it the contact stays down
until this process exits.

.EXAMPLE
pwsh -File tools/touch_hold.ps1 -StartX 1700 -StartY 900 -HoldX 1000 -HoldY 900

.EXAMPLE
pwsh -File tools/touch_hold.ps1 -StartX 1700 -StartY 900 -HoldX 1000 -HoldY 900 -ReleaseAtEnd
#>
[CmdletBinding()]
param(
    [int]$StartX = 1700,
    [int]$StartY = 900,
    [int]$HoldX = 1000,
    [int]$HoldY = 900,
    [int]$Steps = 30,
    [int]$StepDelayMs = 10,
    [int]$HoldMs = 1200,
    [string]$ScreenshotPath = "$env:LOCALAPPDATA\Temp\opencode\hold.png",
    [Alias('Lift')]
    [switch]$ReleaseAtEnd
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'touch_common.ps1')

Start-Contact -X $StartX -Y $StartY
Glide-Contact -X $HoldX -Y $HoldY -Steps $Steps -DelayMs $StepDelayMs

try {
    Hold-Contact -Ms $HoldMs
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
    "SHOT path=$ScreenshotPath width=$($bounds.Width) height=$($bounds.Height)"
} finally {
    if ($ReleaseAtEnd) {
        # End-Contact stops, joins and lifts in one call; a throw in a separate
        # stop would skip the UP, so the release has a single owner.
        End-Contact
    } else {
        # The contact is meant to stay down: only the keep-alive is stopped.
        Stop-ContactHold
    }
}

if ($ReleaseAtEnd) {
    'LIFTED'
} else {
    'HELD (the contact stays down while this process runs; it cannot be lifted by a later run, so pass -ReleaseAtEnd to end the gesture in this one)'
}
