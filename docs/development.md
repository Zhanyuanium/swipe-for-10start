# Development — build, install, and test

> Part of [docs/README.md](README.md). The user guide is
> [../README.md](../README.md) / [../README.zh-CN.md](../README.zh-CN.md).

Everything here runs from the repository root in PowerShell 7. The install and
store commands compile through Windhawk itself; the local build is generated,
never hand-edited.

## Local build and install

The shipping mod is one source with two process halves and one shared header.
Windhawk compiles an installed mod from stdin with no repository beside it, so
the header must be inlined and `@compilerOptions` must carry no `-I`.
`tools/make_store_mods.ps1 -Profile local` writes exactly that to
`dist/local/swipe-for-10start.wh.cpp`: headers inlined, `-I` removed, `-Wall
-Wextra` kept, no store metadata added.

From an elevated PowerShell:

```powershell
pwsh -File .\tools\install_mods.ps1
```

The script runs the local generator, installs and enables
`local@swipe-for-10start`, and restarts `StartMenuExperienceHost.exe` when it
finishes. A restart is not required for the mod to work: an enable or update
attaches to the running menu host's existing root. Windhawk's install folder
must be on `PATH`; `-Compiler` names `windhawk-cli` explicitly, `-All` also
installs the diagnostic probes (`inputprobe` takes the Raw Input stream from the
Start half), and `-SkipRestart` leaves the menu host alone.

`mod install` leaves a previously disabled mod disabled, so the script enables
it explicitly. The Explorer half hooks Explorer when the mod is enabled, with
no Explorer restart on this build.

The repository-side compile check, without installing:

```powershell
& 'C:\Program Files\Windhawk\Compiler\bin\clang++.exe' `
  '@C:\Program Files\Windhawk\Compiler\compile_flags.txt' `
  -I mods -Wall -Wextra -fsyntax-only mods\swipe-for-10start.wh.cpp
```

## Store build

```powershell
pwsh -File .\tools\make_store_mods.ps1 -Profile store
```

The store profile writes the single-file submission to `dist/store/` and adds
the collection metadata (`@github`, `@license`, `@homepage`). It runs
`windhawk-cli source meta --strict` on the generated file. The submission rules
are in [store.md](store.md).

## Tests

`tools/smoke.ps1` drives the four triggers and the three close interruptions
through their real paths and judges every step from the mods' own log lines. It
requires `DiagnosticTrace` and otherwise prints the one command that enables
it. The probes, injectors and captured measurements are listed in
[testing.md](testing.md); their member list is
[../tools/AGENTS.md](../tools/AGENTS.md).
