#requires -Version 7.0
<#
.SYNOPSIS
Inject a left-edge touch swipe that pauses at several x positions.

.DESCRIPTION
This is used to distinguish a real finger-following animation (content stays at
the paused position) from a time-based synthetic animation (content keeps moving
while the finger is held).

Every pause is a real hold through tools/touch_common.ps1: one UPDATE every 5 ms,
below the mod's 18 ms idle release. The earlier version repeated a frame every
50 ms, which was not a hold at all: 50 ms of silence is a released contact, and
the mod had already ended the gesture in the gap.

Example:
  pwsh -File tools/inject_swipe_holds.ps1 -HoldMs 600
#>
[CmdletBinding()]
param(
    [int]$StartX = 1,
    [int]$Y = 900,
    [int]$HoldMs = 600,
    [int]$StepsPerSegment = 20,
    [int]$StepDelayMs = 10,
    [int[]]$Waypoints = @(300, 600, 900)
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'touch_common.ps1')

Start-Contact -X $StartX -Y $Y

try {
    foreach ($x in $Waypoints) {
        Glide-Contact -X $x -Y $Y -Steps $StepsPerSegment -DelayMs $StepDelayMs
        Hold-Contact -Ms $HoldMs
    }
    End-Contact
} catch {
    $failure = $_.Exception.Message
    try { End-Contact } catch { }
    Write-Output $failure
    exit 1
}

Write-Output 'OK'
