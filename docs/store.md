# Store submission

> L3 of `docs/`. How the two mods reach the official Windhawk mods collection,
> and what the submission file differs from the repo source.

The collection (`ramensoftware/windhawk-mods`) accepts exactly one self-contained
file per pull request, `mods/<id>.wh.cpp`, and its CI rejects a `@compilerOptions`
that contains a double quote. Neither mod can be submitted as it sits here: the
Start side includes `swipe_ipc_shared.h` and the bridge includes it plus
`start_gesture_shared.h`.

`tools/make_store_mods.ps1` produces the submittable file. It is generated, never
hand-edited (`dist/` is ignored), and changes only what the collection requires:

| Change | Why |
|---|---|
| Each `#include "<local>.h"` replaced by the header body, inside `#pragma clang diagnostic push/ignored "-Wunused-const-variable"/pop` | One file per PR; the contract's constants are used by one side, so the unused half would warn, and the 2.0 compatibility check fails on any warning |
| `-I"<repo>"`, `-Wall`, `-Wextra` removed from `@compilerOptions` | A quoted option breaks the compile job outright; `-Wall`/`-Wextra` are not among the options the collection accepts, and its CI compiles without them |
| `@github https://github.com/Zhanyuanium`, `@license MIT`, `@homepage <repo>` added | `@github` is required and must equal the PR author's profile exactly; the collection treats a missing licence as MIT |
| A "one half of a pair" note under the README heading, naming the companion mod's `windhawk.net/mods/<id>` page | The two mods only work together; the pair precedent is `audioswap`/`microswap` |

Everything else — code, README body, version, `@include`, `@architecture` — is the
repo source verbatim, so the submitted file and `mods/` cannot drift apart.

## Two pull requests, never one

The bridge decides and the Start side animates; each is useless alone (the Start
side falls back to the shell's own show/dismiss and the gesture's settle is not
driven). Submit `start-menu-legacy-swipe` and `start-menu-touch-gesture-bridge` as
two PRs, one file each, cross-linked in both README blocks. The channel's version
word refuses a mismatched pair, so a user who installs only one gets the pass-
through path rather than a half-driven menu.

## Checks before opening the PR

1. `pwsh -File tools/make_store_mods.ps1` — regenerates both files and runs
   `windhawk-cli source meta --strict` on each.
2. Compile each generated file with `-Wall -Wextra -fsyntax-only`; both must be
   silent (the inlined-header pragma exists for exactly this).
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
