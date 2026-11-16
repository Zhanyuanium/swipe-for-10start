#requires -Version 7.0
<#
.SYNOPSIS
Report or force the legacy Start menu's open/closed state.

.DESCRIPTION
The Start window keeps existing (and IsWindowVisible stays TRUE) while the menu
is hidden, so neither window enumeration nor visibility tells whether it is on
screen. DWMWA_CLOAKED does: 0 = shown, non-zero = cloaked/hidden. The window is
found through `winapp ui list-windows` because plain EnumWindows does not
reliably return it on this build.

.PARAMETER EnsureOpen
Open the menu if it is closed.

.PARAMETER EnsureClosed
Close the menu if it is open (global VK_LWIN tap, which toggles it).

.EXAMPLE
pwsh -File tools/start_menu.ps1 -EnsureOpen
#>
[CmdletBinding()]
param(
    [switch]$EnsureOpen,
    [switch]$EnsureClosed
)

$ErrorActionPreference = 'Stop'

if (-not ('StartMenuState.Native' -as [type])) {
    Add-Type -Namespace StartMenuState -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("dwmapi.dll")]
public static extern int DwmGetWindowAttribute(System.IntPtr h, int attr, out int value, int size);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
public static int Cloaked(System.IntPtr h) {
    int v = -1;
    int hr = DwmGetWindowAttribute(h, 14, out v, 4);
    return hr == 0 ? v : -999;
}
public static void WinTap() {
    keybd_event(0x5B, 0, 0, System.UIntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    keybd_event(0x5B, 0, 2, System.UIntPtr.Zero);
}
'@
}

function Get-StartHwnd {
    $out = & winapp ui list-windows -a StartMenuExperienceHost 2>&1
    foreach ($line in $out) {
        # winapp colorizes the HWND with ANSI escapes, so strip them first.
        $plain = $line -replace "`e\[[0-9;]*m", ''
        $m = [regex]::Match($plain, 'HWND\s+(\d+)')
        if ($m.Success) { return [int64]$m.Groups[1].Value }
    }
    return 0
}

function Get-State {
    $hwnd = Get-StartHwnd
    if ($hwnd -eq 0) { return [pscustomobject]@{ Hwnd = 0; Cloaked = -1; Open = $false } }
    $cloaked = [StartMenuState.Native]::Cloaked([System.IntPtr]$hwnd)
    return [pscustomobject]@{ Hwnd = $hwnd; Cloaked = $cloaked; Open = ($cloaked -eq 0) }
}

$state = Get-State

if ($EnsureOpen -and -not $state.Open) {
    for ($i = 0; $i -lt 3 -and -not $state.Open; $i++) {
        & winapp ui invoke "开始" -a explorer --type Button 2>&1 | Out-Null
        Start-Sleep -Milliseconds 1800
        $state = Get-State
    }
}

if ($EnsureClosed -and $state.Open) {
    [StartMenuState.Native]::WinTap()
    Start-Sleep -Milliseconds 900
    $state = Get-State
}

"START hwnd={0} cloaked={1} open={2}" -f $state.Hwnd, $state.Cloaked, $state.Open
if ($EnsureOpen -and -not $state.Open) { exit 1 }
if ($EnsureClosed -and $state.Open) { exit 1 }
