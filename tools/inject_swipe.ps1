#requires -Version 7.0
<#
.SYNOPSIS
Inject a single-finger touch swipe using InjectTouchInput.

.DESCRIPTION
The contact comes from tools/touch_common.ps1: DOWN, one UPDATE per step at
-DelayMs, then UP as the last injection of the contact. Coordinates are physical
screen pixels. Use -DryRun to print the sequence without injecting anything.

.EXAMPLE
pwsh -File tools/inject_swipe.ps1 -StartX 1 -StartY 900 -EndX 900 -EndY 900
#>
[CmdletBinding()]
param(
    [int]$StartX = 1,
    [int]$StartY = 900,
    [int]$EndX = 900,
    [int]$EndY = 900,
    [int]$Steps = 60,
    [int]$DelayMs = 10,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'touch_common.ps1')

if ($DryRun) {
    Write-Host ("DRY RUN: ({0},{1}) -> ({2},{3}), steps={4}, delay={5}ms" -f `
        $StartX, $StartY, $EndX, $EndY, $Steps, $DelayMs)
    exit 0
}

try {
    Invoke-ContactSwipe -StartX $StartX -StartY $StartY -EndX $EndX -EndY $EndY `
        -Steps $Steps -DelayMs $DelayMs
    Write-Output 'OK'
} catch {
    Write-Output $_.Exception.Message
    exit 1
}
