#requires -Version 7.0
<#
.SYNOPSIS
Build the single-file Windhawk store submission from the repo's two mod sources.

.DESCRIPTION
The mods here share `mods/swipe_ipc_shared.h` (and the bridge also includes
`mods/start_gesture_shared.h`), which the store cannot accept: a pull request adds
exactly one self-contained `mods/<id>.wh.cpp`, and `@compilerOptions` may not
contain a quoted `-I`. This script produces that one file per mod and changes
nothing else:

- each `#include "..."` of a local header is replaced by the header body, inside a
  `#pragma clang diagnostic push/ignored/pop` pair, because the contract's
  constants are used by one side or the other and the unused half would otherwise
  warn (the store's 2.0 compatibility check fails on any warning);
- the local `-I` path and `-Wall -Wextra` are dropped from `@compilerOptions`
  (neither is among the options the collection accepts, and its CI compiles
  without them);
- `@github`, `@license` and `@homepage` are added, and a companion-mod note is put
  under the README heading, because the two mods only work as a pair.

The output is generated, never hand-edited: `dist/` is ignored, and a diff against
the repo source is only the inlined headers and those metadata lines.

.PARAMETER OutDir
Where the two `.wh.cpp` files are written. Default `dist/store`.

.EXAMPLE
pwsh -File tools/make_store_mods.ps1
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\store')
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$mods = Join-Path $repo 'mods'
$repoUrl = 'https://github.com/Zhanyuanium/swipe-for-10start'

$modsToBuild = @(
    @{
        File = 'start-menu-legacy-swipe.wh.cpp'
        CompanionId = 'start-menu-touch-gesture-bridge'
        CompanionName = 'Start Menu Touch Gesture Bridge'
        CompanionWhere = 'explorer.exe'
    },
    @{
        File = 'start-menu-touch-gesture-bridge.wh.cpp'
        CompanionId = 'start-menu-legacy-swipe'
        CompanionName = 'Legacy Start Edge Swipe'
        CompanionWhere = 'StartMenuExperienceHost.exe'
    }
)

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

    $text = [regex]::Replace($text, '(?m)^(//\s+@compilerOptions\s+)(.*)$', {
        param($m)
        $opts = ($m.Groups[2].Value -split '\s+' | Where-Object {
            $_ -and $_ -ne '-Wall' -and $_ -ne '-Wextra' -and $_ -notmatch '^-I'
        }) -join ' '
        $m.Groups[1].Value + $opts + "`r"
    })

    # -- the pair note, under the README heading
    $note = @"
> **This is one half of a pair.** The gesture is decided in
> [$($mod.CompanionName)](https://windhawk.net/mods/$($mod.CompanionId)) (``$($mod.CompanionWhere)``);
> install both mods, or the menu opens without the swipe following the finger and
> closes without the settle.
>
> Requires ExplorerPatcher's legacy Windows 10 Start menu (``Start_ShowClassicMode=1``).
"@
    $text = [regex]::Replace($text,
        '(?m)^(// ==WindhawkModReadme==\r?\n/\*\r?\n)(#[^\r\n]*\r?\n)',
        { param($m) $m.Groups[1].Value + $m.Groups[2].Value + $note + "`n" })

    $out = Join-Path $OutDir $mod.File
    Set-Content -LiteralPath $out -Value $text -NoNewline -Encoding utf8

    $meta = & 'C:\Program Files\Windhawk\windhawk-cli.exe' source meta --strict --json $out 2>&1
    if ($LASTEXITCODE -ne 0) { throw "source meta rejected $($mod.File): $meta" }
    "{0}: {1} lines, {2} header(s) inlined, meta strict ok" -f `
        $mod.File, (Get-Content -LiteralPath $out | Measure-Object -Line).Lines, $inlined.Count
}
