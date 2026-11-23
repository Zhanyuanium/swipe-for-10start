# Store submission

> How the one mod reaches `ramensoftware/windhawk-mods`, and what the submitted
> file differs from the repo source.

The collection accepts exactly one self-contained `mods/<id>.wh.cpp` per PR and
rejects a `@compilerOptions` containing a double quote. The repo source includes
`swipe_ipc_shared.h`, so `tools/make_store_mods.ps1` generates the submittable
file (`dist/` is ignored, never hand-edited):

| Change | Why |
|---|---|
| each `#include "<local>.h"` replaced by the header body, inside a `-Wunused-const-variable` pragma pair | one file per PR; the constants are used by one side, and the 2.0 check fails on a warning |
| `-I` (and, in `store`, `-Wall`/`-Wextra`) removed from `@compilerOptions` | a machine path must not survive; a quoted option breaks the compile job, and the collection compiles without the warnings |
| `==WindhawkModReadme==` body replaced by `README.md` + `---` + `README.zh-CN.md` | the store page and the installed mod show the same bilingual guides; the condensed block in `mods/` never ships |
| `@github`, `@license MIT`, `@homepage` added | `@github` is required and must equal the PR author's login |

Everything else — code, version, `@include`, `@architecture` — is the repo source
verbatim, so the submitted file and `mods/` cannot drift apart.

Two source constraints come from the collection's scripts, not Windhawk:
`extract_mod_symbols.py` reads a hook name only where the entry carries it as a
literal (a `constexpr wchar_t` fails with `Symbol name is not a string`), and
`pr_validation.py` maps a `<base>DllHooks` variable to `<base>.dll` in the Windows
file-name index, so the Start array is `startuiDllHooks` and the launcher array
`symbolHooks` with `// twinui.pcshell.dll` above it.

The `local` profile is the same generator without store metadata and with
`-Wall -Wextra` kept, because an installed mod is also compiled from a single
file. `tools/install_mods.ps1` builds it and installs `local@swipe-for-10start`.
The mod is one PR, one file: the controller and the executor live in it, and each
half is inert in the other's process. The namespace is the ABI identity, so a
staggered install cannot cross-pair two layouts, but it does not stop two images
from loading into the same hosts: enable one copy only.

Checks before opening the PR:

1. `pwsh -File tools/make_store_mods.ps1` — regenerates and runs
   `windhawk-cli source meta --strict`.
2. `pwsh -File tools/make_store_mods.ps1 -Profile local`, then compile that file
   with `-Wall -Wextra -fsyntax-only` and no `-I`; it must be silent.
3. `get_mod_symbols(...)` from `.github/extract_mod_symbols.py` must print one
   name under `startui.dll` and thirteen under `twinui.pcshell.dll`.
4. The collection's validator must report zero errors and zero review items.
5. The PR body keeps the `## Mod authorship` section, and `@github` matches the PR
   author's login.

After opening, comment `/ai-review`, fix what it reports, then
`/ready-for-reviewer` once the head commit's review is current; the branch's
commit message becomes the changelog entry.

[PROTOCOL]: Update this header on change, then check AGENTS.md
