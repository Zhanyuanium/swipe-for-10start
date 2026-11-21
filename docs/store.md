# Store submission

> L3 of `docs/`. How the one mod reaches the official Windhawk mods collection,
> and what the submission file differs from the repo source.

The collection (`ramensoftware/windhawk-mods`) accepts exactly one self-contained
file per pull request, `mods/<id>.wh.cpp`, and its CI rejects a `@compilerOptions`
that contains a double quote. The shipping source cannot be submitted as it sits
here: it includes `swipe_ipc_shared.h`.

`tools/make_store_mods.ps1` produces the submittable file. It is generated, never
hand-edited (`dist/` is ignored), and changes only what the collection requires:

| Change | Why |
|---|---|
| Each `#include "<local>.h"` replaced by the header body, inside `#pragma clang diagnostic push/ignored "-Wunused-const-variable"/pop` | One file per PR; the contract's constants are used by one side, so the unused half would warn, and the 2.0 compatibility check fails on any warning |
| `-I` (if a source ever carries one), `-Wall`, `-Wextra` removed from `@compilerOptions` in the store profile | A machine path must not survive and a quoted option breaks the compile job outright; `-Wall`/`-Wextra` are not among the options the collection accepts, and its CI compiles without them |
| `@github https://github.com/Zhanyuanium`, `@license MIT`, `@homepage <repo>` added | `@github` is required and must equal the PR author's profile exactly; the collection treats a missing licence as MIT |

Everything else — code, README body, version, `@include`, `@architecture` — is the
repo source verbatim, so the submitted file and `mods/` cannot drift apart.

## Local install

The same generator has a `local` profile, because an installed mod is also
compiled from a single file: Windhawk reads a mod's source from stdin and has no
repository beside it. `pwsh -File tools/install_mods.ps1` builds that profile into
`dist/local/` — headers inlined, `-I` removed, `-Wall -Wextra` kept, no store
metadata — then installs and enables `local@swipe-for-10start` and restarts
`StartMenuExperienceHost.exe`. `-All` adds the three diagnostic probes from
`mods/`; `-SkipRestart` leaves the menu host running. The CLI comes from
`-CliPath`/`-Compiler`, then `windhawk-cli` on `PATH`, then
`%ProgramFiles%\Windhawk\windhawk-cli.exe`, and the failure message names both
locations.

This flow is why `mods/` no longer carries an `-I`: the repository source is built
with `-I mods` on the compiler command line, and the installed source is always
the generated one.

## One pull request

The mod is one file that carries both halves; the controller decides and the
executor performs inside that file, and each half is inert in the other's
process. Submit `swipe-for-10start` as one PR, one file. The channel has no
version word: its object namespace is the ABI identity, so a user who installs
it beside an old development build gets the pass-through path rather than a
half-driven menu.

## Checks before opening the PR

1. `pwsh -File tools/make_store_mods.ps1` — regenerates the store file and runs
   `windhawk-cli source meta --strict` on it.
2. `pwsh -File tools/make_store_mods.ps1 -Profile local`, then compile the
   generated local file with `-Wall -Wextra -fsyntax-only` and no `-I`; it must
   be silent (the inlined-header pragma exists for exactly this).
3. Run the collection's own validator against each file, from a clone of
   `windhawk-mods`:
   `python .github/pr_validation.py mods/<id>.wh.cpp Zhanyuanium 92024923`
   It must report zero errors and zero review items.
4. The PR body must keep the template's `## Mod authorship` section, and the
   mod's `@github` must match the PR author's login (`Zhanyuanium`).

## After opening

The bot's flow is author-driven: comment `/ai-review`, fix what it reports,
repeat, then `/ready-for-reviewer` once the head commit's AI review is current.
The commit message on the branch becomes the mod's changelog entry.

[PROTOCOL]: Update this header on change, then check AGENTS.md
