#requires -Version 7.0
<#
.SYNOPSIS
Generate the local mod builds and install them into Windhawk.

.DESCRIPTION
Windhawk compiles an installed mod from stdin with no repository beside it, so
the shared headers must already be inlined and `@compilerOptions` must not carry
an `-I`. `tools/make_store_mods.ps1 -Profile local` produces exactly that into
`dist/local/`: the same inlined headers as the store submission, `-I` removed,
but `-Wall -Wextra` kept and no store metadata added. This script installs those
generated files, never `mods/` directly.

The shipping pair is always installed and enabled. `-All` adds the diagnostic
mods: `start-menu-touch-gesture-consumer` comes from `dist/local/` because it
includes `start_gesture_shared.h`; `inputprobe`, `msgprobe` and
`start-menu-touch-gesture-probe` install from `mods/` as they are. `inputprobe`
takes the Raw Input stream from the Start mod, so `-All` belongs to a probe
session, not to a normal install.

Each `mod install --file` also compiles the mod; a compile or metadata failure
fails the script. After the installs `StartMenuExperienceHost.exe` is restarted,
because a mod attaches only when the menu host creates its window; `-SkipRestart`
leaves the running host alone.

.PARAMETER All
Also install and enable the four diagnostic mods.

.PARAMETER SkipRestart
Do not restart `StartMenuExperienceHost.exe` after the install.

.PARAMETER Compiler
The `windhawk-cli` executable or command name. Default: `windhawk-cli` on PATH,
then `%ProgramFiles%\Windhawk\windhawk-cli.exe`.

.EXAMPLE
pwsh -File tools/install_mods.ps1

.EXAMPLE
pwsh -File tools/install_mods.ps1 -All -SkipRestart
#>
[CmdletBinding()]
param(
    [switch]$All,
    [switch]$SkipRestart,
    [string]$Compiler = 'windhawk-cli'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$generator = Join-Path $PSScriptRoot 'make_store_mods.ps1'
$localDir = Join-Path $repo 'dist\local'

# Explicit path, then PATH, then Windhawk's own install folder. The fallback is
# the default layout, tested before use, so a portable install still works when
# it is named by -Compiler or put on PATH.
function Resolve-WindhawkCli {
    param([string]$Requested)

    if ($Requested -and $Requested -ne 'windhawk-cli') {
        $command = Get-Command $Requested -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
        if (Test-Path -LiteralPath $Requested) { return (Resolve-Path -LiteralPath $Requested).Path }
        throw "-Compiler is neither a command nor a file: $Requested"
    }

    $command = Get-Command windhawk-cli -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $fallback = Join-Path $env:ProgramFiles 'Windhawk\windhawk-cli.exe'
    if (Test-Path -LiteralPath $fallback) { return $fallback }

    throw ("windhawk-cli was not found. Pass -Compiler, or put Windhawk's install " +
        "folder (the one holding windhawk-cli.exe) on PATH, or install it to the " +
        "default '$fallback'. To add it to PATH: Settings > System > About > " +
        "Advanced system settings > Environment Variables, edit Path, then reopen " +
        "the shell.")
}

function Get-ModVersion {
    param([string]$Path)
    $match = Select-String -LiteralPath $Path -Pattern '^\s*//\s+@version\s+(\S+)' |
        Select-Object -First 1
    if ($match) { return $match.Matches[0].Groups[1].Value }
    return 'unknown'
}

$cli = Resolve-WindhawkCli $Compiler

& $generator -Profile local

$targets = New-Object System.Collections.Generic.List[hashtable]
$targets.Add(@{
        Id = 'start-menu-legacy-swipe'
        File = Join-Path $localDir 'start-menu-legacy-swipe.wh.cpp'
    })
$targets.Add(@{
        Id = 'start-menu-touch-gesture-bridge'
        File = Join-Path $localDir 'start-menu-touch-gesture-bridge.wh.cpp'
    })
if ($All) {
    $targets.Add(@{
            Id = 'start-menu-touch-gesture-consumer'
            File = Join-Path $localDir 'start-menu-touch-gesture-consumer.wh.cpp'
        })
    foreach ($name in 'inputprobe', 'msgprobe', 'start-menu-touch-gesture-probe') {
        $targets.Add(@{
                Id = $name
                File = Join-Path (Join-Path $repo 'mods') "$name.wh.cpp"
            })
    }
}

$failed = New-Object System.Collections.Generic.List[string]
foreach ($target in $targets) {
    $id = "local@$($target.Id)"
    if (-not (Test-Path -LiteralPath $target.File)) {
        "$id unknown failed"
        $failed.Add($id)
        continue
    }

    $version = Get-ModVersion $target.File
    $installExit = 1
    $enableExit = 1

    & $cli mod install --file $target.File
    $installExit = $LASTEXITCODE
    if ($installExit -eq 0) {
        & $cli mod enable $id
        $enableExit = $LASTEXITCODE
    }

    $state = if ($installExit -eq 0 -and $enableExit -eq 0) { 'enabled' } else { 'failed' }
    '{0} {1} {2}' -f $id, $version, $state
    if ($state -eq 'failed') { $failed.Add($id) }
}

if ($failed.Count -gt 0) {
    [Console]::Error.WriteLine("install failed: " + ($failed -join ', '))
    exit 1
}

if (-not $SkipRestart) {
    $menuHost = Get-Process -Name StartMenuExperienceHost -ErrorAction SilentlyContinue
    if ($menuHost) {
        $menuHost | Stop-Process -Force
        'restarted StartMenuExperienceHost.exe; the next Start opens a fresh menu'
    } else {
        'StartMenuExperienceHost.exe is not running; it starts with the next Start press'
    }
}
