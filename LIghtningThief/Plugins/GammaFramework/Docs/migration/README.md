# Migration tooling — RETIRED

These scripts performed the one-time extraction that produced this plugin.
**They are no longer part of the workflow and must not be run.**

`migrate_from_gamma_emerald.py` *deletes* the plugin's `Source/` tree and
regenerates it from the original project. That was correct while the plugin was
generated output; it is destructive now that the source is hand-maintained.
The script refuses to run for that reason — it exits with an explanation unless
given an explicit override flag.

They are committed because the reasoning matters. Every table here documents not
just what changed but why, including the cases where the obvious rename would
have been wrong.

## Why it was retired

The pipeline was the right tool for a bulk rename of ~73,000 lines: renames are
order-dependent and easy to get subtly wrong, and a script made every decision
reviewable, reproducible, and re-runnable when a bug turned up. It was re-run
roughly forty times.

It stopped being the right tool once the work became *design* rather than
renaming — a new element roster, breeding groups moving to tags, hardcoded
content being removed. None of that is derivable from the original source, so
each change had to be encoded as a regex patch against code it was never meant
to describe. Several broke before landing, on rename ordering, on CRLF, and on a
patch quoting a comment that an earlier pass had already rewritten.

The premise is also gone: the source project is frozen, so there is no upstream
left to re-pull from.

## What each file is

| File | Contents |
|---|---|
| `migrate_from_gamma_emerald.py` | The pipeline. Copies source, applies every pass in order, reports anything that stopped matching. |
| `value_renames.py` | 25 temperaments and 51 traits, with each trait's effect in a comment. |
| `enum_value_renames.py` | Held-item effects, catch-device tiers, growth curves. Named by function, not by item. |
| `item_name_renames.py` | The item long tail, plus identifier fixups. |
| `example_cast.py` | The invented cast used in code comments as worked examples. |
| `decouple_patches.py` | The structural edits: module decoupling, and content that had leaked into the framework. |

## Why they will not run as-is

The paths at the top of `migrate_from_gamma_emerald.py` point at a specific
machine and at a source project that is not part of this repository. The scripts
are here to be **read**, not executed.

## Lessons worth keeping

Every one of these cost a broken build before it was understood:

1. **Rename order is load-bearing.** A rule keyed on a pre-rename spelling
   silently matches nothing. `Pokemon -> Creature` running before the
   `FPokemonIVs` rule meant the IV rename quietly did nothing at all.

2. **Never blanket-replace a short or common word.** `Move` in this codebase is
   overwhelmingly *movement*, so a naive replacement turns `RemoveAt` into
   `ReskillAt`. `IV`/`EV`/`PP` are substrings of EVENT, LEVEL and APPLY. `Ball`
   is inside Balloon, `Cry` inside Crystal, `ability` inside capability.

3. **Engine symbols get caught in the blast radius.** `IFileManager::Move`,
   `ESlateBrushDrawType::Box`, and `UPaperSprite` — the last one because an
   auto-prefixer swept up *forward declarations*. Collect definitions only.

4. **Renaming an enum is half the job.** The display strings are what players
   read. The enum said `Dewshield` while the UI printed "Water Veil" for a whole
   session before anyone noticed, because the label lived in a separate table.

5. **Patterns must not quote text that another pass rewrites.** Anchor a patch
   on code, never on a comment — a prose rename elsewhere will break it silently.

6. **CRLF is invisible until it is not.** A regex written with `\n` fails on
   CRLF source, and testing it in Python text mode *passes*, because the read
   normalises line endings first. Use `\s*` between lines.
