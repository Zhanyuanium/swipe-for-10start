#requires -Version 7.0
<#
.SYNOPSIS
Build self-contained Windhawk mod sources from the repo's mod sources, for the
store submission or for a local install.

.DESCRIPTION
The mod ships as one source and shares `mods/swipe_ipc_shared.h`, which neither
the store nor a Windhawk install can resolve: a pull request adds exactly one
self-contained `mods/<id>.wh.cpp`, and Windhawk compiles the installed file from
stdin with no repository beside it. This script produces that one file and
changes nothing else. Two profiles:

- `store` (the default) is the `ramensoftware/windhawk-mods` submission. Each
  `#include "..."` of a local header is replaced by the header body, inside a
  `#pragma clang diagnostic push/ignored/pop` pair, because the contract's
  constants are used by one side or the other and the unused half would
  otherwise warn (the store's 2.0 compatibility check fails on any warning).
  `-I`, `-Wall` and `-Wextra` are dropped from `@compilerOptions` (a quoted
  option breaks the collection's compile job, and the warning flags are not
  among the options it accepts), `@github`, `@license` and `@homepage` are
  added, and the output is checked with `windhawk-cli source meta --strict`.
- `local` is the file `tools/install_mods.ps1` installs. It inlines the same
  headers, removes the `-I` option, and changes nothing else: `-Wall -Wextra`
  stay so a local build is warning-clean, and no store metadata is added.

The output is generated, never hand-edited: `dist/` is ignored, and a diff
against the repo source is only the inlined headers and the metadata the store
profile requires.

.PARAMETER Profile
`store` or `local`. Default `store`.

.PARAMETER CliPath
The `windhawk-cli` executable used for `source meta --strict`. Default: a
`windhawk-cli` on PATH, then `%ProgramFiles%\Windhawk\windhawk-cli.exe`. Only the
`store` profile needs it.

.PARAMETER OutDir
Where the generated `.wh.cpp` files are written. Default `dist/<profile>`.

.EXAMPLE
pwsh -File tools/make_store_mods.ps1

.EXAMPLE
pwsh -File tools/make_store_mods.ps1 -Profile local
#>
[CmdletBinding()]
param(
    [ValidateSet('store', 'local')]
    [string]$Profile = 'store',
    [string]$CliPath,
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$mods = Join-Path $repo 'mods'
$repoUrl = 'https://github.com/Zhanyuanium/swipe-for-10start'
$isStore = $Profile -eq 'store'

if (-not $OutDir) { $OutDir = Join-Path $repo "dist\$Profile" }

# Explicit path, then PATH, then Windhawk's own install folder. Never a guess:
# the fallback stays a test, not an assumption about where Windhawk lives.
function Resolve-WindhawkCli {
    if ($CliPath) {
        if (-not (Test-Path -LiteralPath $CliPath)) {
            throw "-CliPath does not name a file: $CliPath"
        }
        return (Resolve-Path -LiteralPath $CliPath).Path
    }

    $command = Get-Command windhawk-cli -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $fallback = Join-Path $env:ProgramFiles 'Windhawk\windhawk-cli.exe'
    if (Test-Path -LiteralPath $fallback) { return $fallback }

    throw ("windhawk-cli was not found. Pass -CliPath, or put Windhawk's install " +
        "folder (the one holding windhawk-cli.exe) on PATH, or install it to the " +
        "default '$fallback'. To add it to PATH: Settings > System > About > " +
        "Advanced system settings > Environment Variables, edit Path, then reopen " +
        "the shell.")
}

$modsToBuild = New-Object System.Collections.Generic.List[hashtable]
$modsToBuild.Add(@{ File = 'swipe-for-10start.wh.cpp' })

$cli = $null
if ($isStore) { $cli = Resolve-WindhawkCli }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

foreach ($mod in $modsToBuild) {
    $source = Join-Path $mods $mod.File
    $text = Get-Content -LiteralPath $source -Raw

    # -- inlined headers, silenced for the half of the contract this side ignores
    $inlined = New-Object System.Collections.Generic.List[string]
    foreach ($m in [regex]::Matches($text, '(?m)^#include\s+"([^"]+)"\s*$')) {
        $name = $m.Groups[1].Value
        $path = Join-Path $mods $name
        if (-not (Test-Path $path)) { throw "missing header $name beside $source" }
        $body = (Get-Content -LiteralPath $path -Raw) -replace '(?m)^#pragma once\r?\n', ''
        $block = @"
// ---- begin $name (inlined: the store takes one self-contained file) ----
// The shared contract defines the vocabulary for both sides. This file is one
// side of it, so the unused half of these constants is deliberate.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
$($body.TrimEnd())
#pragma clang diagnostic pop
// ---- end $name ----
"@
        $inlined.Add($block)
        $text = $text.Replace($m.Value, "@@INLINE_$($inlined.Count - 1)@@")
    }
    for ($i = 0; $i -lt $inlined.Count; $i++) { $text = $text.Replace("@@INLINE_$i@@", $inlined[$i]) }

    # -- @compilerOptions: no machine path in either profile. The store also
    #    drops the warning flags it does not accept; a local build keeps them.
    $text = [regex]::Replace($text, '(?m)^(//\s+@compilerOptions\s+)(.*)$', {
            param($m)
            $drop = if ($isStore) { @('-Wall', '-Wextra') } else { @() }
            $opts = ($m.Groups[2].Value -split '\s+' | Where-Object {
                    $_ -and $_ -notmatch '^-I' -and $drop -notcontains $_
                }) -join ' '
            if ($opts -match '"') { throw "quoted compiler option left in $($mod.File): $opts" }
            if (-not $opts) { return '' }
            $m.Groups[1].Value + $opts + "`r"
        }.GetNewClosure())

    if ($isStore) {
        # -- metadata the collection requires and this repo does not need locally
        $extra = foreach ($pair in @(
                @('@github', 'https://github.com/Zhanyuanium'),
                @('@license', 'MIT'),
                @('@homepage', $repoUrl))) {
            '// ' + $pair[0].PadRight(16) + ' ' + $pair[1]
        }
        $text = [regex]::Replace($text, '(?m)^(//\s+@author\s+.*)$', {
                param($m) $m.Groups[1].Value.TrimEnd() + "`r`n" + ($extra -join "`r`n")
            }.GetNewClosure())
    }

    $out = Join-Path $OutDir $mod.File
    Set-Content -LiteralPath $out -Value $text -NoNewline -Encoding utf8

    $summary = "{0}: {1} lines, {2} header(s) inlined" -f `
        $mod.File, (Get-Content -LiteralPath $out | Measure-Object -Line).Lines, $inlined.Count
    if ($isStore) {
        $meta = & $cli source meta --strict --json $out 2>&1
        if ($LASTEXITCODE -ne 0) { throw "source meta rejected $($mod.File): $meta" }
        $summary += ', meta strict ok'
    }
    $summary
}
