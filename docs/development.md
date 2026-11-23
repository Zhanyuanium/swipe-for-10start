# Development — build, install, test

> Part of [docs/README.md](README.md). The user guide is
> [../README.md](../README.md) / [../README.zh-CN.md](../README.zh-CN.md).

Everything runs from the repository root in PowerShell 7. The install and store
commands compile through Windhawk itself; the local source is generated, never
hand-edited.

## Local build and install

Windhawk compiles an installed mod from stdin with no repository beside it, so
`swipe_ipc_shared.h` must be inlined and `@compilerOptions` must carry no `-I`.
`tools/make_store_mods.ps1 -Profile local` writes exactly that to
`dist/local/swipe-for-10start.wh.cpp`.

From an elevated PowerShell:

```powershell
pwsh -File .\tools\install_mods.ps1
```

It builds, installs and enables `local@swipe-for-10start`, and restarts the menu
host unless `-SkipRestart`; a restart is not required, because an enable or update
attaches to the running host's existing root. `-Compiler` names `windhawk-cli`
explicitly and `-All` installs the probes (`inputprobe` takes the Raw Input
stream from the Start half).

The repository-side compile check, without installing:

```powershell
& 'C:\Program Files\Windhawk\Compiler\bin\clang++.exe' `
  '@C:\Program Files\Windhawk\Compiler\compile_flags.txt' `
  -I mods -Wall -Wextra -fsyntax-only mods\swipe-for-10start.wh.cpp
```

## Store build

`pwsh -File .\tools\make_store_mods.ps1 -Profile store` writes the single-file
submission to `dist/store/`, adds the collection metadata and runs
`source meta --strict`. The submission rules are in [store.md](store.md).

## Tests

`tools/smoke.ps1` drives the four triggers and the three close interruptions
through their real paths and judges every step from the mods' own log lines. It
requires `DiagnosticTrace` and otherwise prints the one command that enables it.
The probes, injectors and captures are in [testing.md](testing.md); their roster
is [../tools/AGENTS.md](../tools/AGENTS.md).
