#requires -Version 7.0
<#
.SYNOPSIS
Capture the whole screen to a PNG, in physical pixels.

.DESCRIPTION
This machine is 2880x1800 physical at 200% scaling. A DPI-unaware pwsh process
sees a 1440x900 virtualized screen, and CopyFromScreen then grabs only the
top-left quarter at 1:1 physical pixels. The process is therefore switched to
per-monitor-v2 DPI awareness before any screen measurement happens, so
Screen.PrimaryScreen.Bounds reports 2880x1800 and the capture is the full screen.

.PARAMETER Path
Output PNG path. Defaults to a timestamped file under the opencode temp folder.

.PARAMETER Region
Optional "X,Y,W,H" rectangle in physical pixels. Without it the whole screen is
captured.

.EXAMPLE
pwsh -File tools/screenshot.ps1
#>
[CmdletBinding()]
param(
    [string]$Path,
    [string]$Region
)

$ErrorActionPreference = 'Stop'

if (-not ('ScreenCapture.Dpi' -as [type])) {
    Add-Type -Namespace ScreenCapture -Name Dpi -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetProcessDpiAwarenessContext(System.IntPtr value);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern System.IntPtr SetThreadDpiAwarenessContext(System.IntPtr value);
public static void Enable() {
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
    System.IntPtr v = new System.IntPtr(-4);
    System.IntPtr perThread = SetThreadDpiAwarenessContext(v);
    if (perThread == System.IntPtr.Zero) {
        SetProcessDpiAwarenessContext(v);
    }
}
'@
}

[ScreenCapture.Dpi]::Enable()

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
Write-Verbose "screen=$($screen.Width)x$($screen.Height)"

if (-not $Path) {
    $stamp = Get-Date -Format 'HHmmss-fff'
    $Path = Join-Path $env:LOCALAPPDATA "Temp\opencode\shot-$stamp.png"
}

if ($Region) {
    $parts = $Region -split ','
    if ($parts.Count -ne 4) { throw "Region must be X,Y,W,H (got '$Region')" }
    $rect = [System.Drawing.Rectangle]::new([int]$parts[0], [int]$parts[1], [int]$parts[2], [int]$parts[3])
} else {
    $rect = $screen
}

$bmp = [System.Drawing.Bitmap]::new($rect.Width, $rect.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
try {
    $g.CopyFromScreen($rect.Location, [System.Drawing.Point]::Empty, $rect.Size)
} finally {
    $g.Dispose()
}

$dir = Split-Path -Parent $Path
if ($dir -and -not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}
$bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

"SHOT path={0} width={1} height={2} bytes={3}" -f $Path, $rect.Width, $rect.Height, (Get-Item -LiteralPath $Path).Length
