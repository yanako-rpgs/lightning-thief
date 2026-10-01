# Gamma Framework

A creature-collector / beast-tamer framework for Unreal Engine 5.6, built as a
reusable plugin with genre-neutral naming.

~73,000 lines of C++ across five modules. Builds clean on both the editor and
the non-editor game target.

---

## Installing

Unreal Engine **5.6**. Clone into your project's `Plugins/` folder:

```bash
git clone https://github.com/UndreamedPanic/GammaFramework.git YourProject/Plugins/GammaFramework
```

Or as a submodule, to track changes:

```bash
git submodule add https://github.com/UndreamedPanic/GammaFramework.git Plugins/GammaFramework
```

Regenerate project files and build. Requires Paper2D, Niagara and UINavigation —
all three are declared in the `.uplugin`, so enabling this pulls them in.

To develop the plugin standalone, drop it into a blank C++ UE 5.6 project. That
is the same install path, and it builds on both the editor and non-editor game
targets.

## Status

Early. The C++ is complete, renamed and building, but **no content ships with
it** — no creatures, sprites, audio or data tables. The battle layer is a port
that is expected to be replaced; see the Creatures / Battle boundary below.

---

## Modules

### `GammaFrameworkWorld` — runtime, genre-agnostic

Nothing here knows what a Creature is. A project that wants the overworld tech
and nothing else can disable the Creatures module and this still works.

- **Grid overworld** — tile-based movement, pathfinding, tile scanning/blocking,
  world subsystem, per-tile properties, debug drawing
- **NPC AI** — patrols, following, scripted movement, vision cones
- **Dialogue** — asset format, parser, subsystem, rich-text effect decorator,
  widget base, and an editor preview tool
- **Quests / story flags**
- **Settings** — save, subsystem, project-settings page, difficulty profiles
- **Boot** — staged boot plan with PSO/Niagara precache progress
- **Items** — data assets, inventory, categories, item manager
- **Berries** — plantable growth with a real-time clock
- **Shared types** — `EGF_Element` and its effectiveness chart,
  `EGF_SkillCategory`, `EGF_STATUSEffect`, `EGF_PlayerGender`
- **Misc** — sprite billboarding, transform shake, OST manager, followers,
  widget mirroring, diagnostics logging, game version, reset interface

### `GammaFrameworkCreatures` — runtime, the creature DATA layer

Everything about what a creature *is*. Knows nothing about how a turn resolves.

- **Species and instances** — species data assets, per-creature instance data,
  stats, potential/training, temperaments, traits
- **Vault** — off-party storage, pages, cursor, screen widget
- **Breeding** — the Sanctuary, breeding groups, inheritance, hatching
- **Capture** — catch rates, capture Cores, shake resolution
- **Trading** — peer-to-peer trade over an HTTP relay, gift distribution, codec
- **Routes and town map** — route data, wild encounter tables, map widget
- **Party UI** — slot widgets, panels, stat radar

### `GammaFrameworkBattle` — runtime, combat

Separable on purpose: **disable or delete this module and the data layer keeps
working.** Nothing in Creatures references it.

- **Battle** — turn resolution, damage math, skills, held items, flinch,
  status, weather, AI decision-making
- **Tamers** — tamer actors, AI difficulty, teams, rematches
- **Runtime debugger** — in-game debug pages

This is the half meant to be replaced by a new battle system.

### `GammaFrameworkEditor` — editor only

A top-level **Gamma** menu in the Level Editor menu bar: the **Create Creature**
window (name + type -> a species data asset filed by element), the dialogue
editor and its reimport tools. Plus the dialogue asset factory and the
species-data thumbnail renderer. See `Docs/EDITOR_TOOLS.md`.

### `GammaFrameworkSaveGuard` — runtime, PreDefault

A `PlatformFeatures` module that swaps in an encrypted save-game system. It has
to be its own module because `IPlatformFeaturesModule::Get()` loads the module
by name and requires it to derive from `IPlatformFeaturesModule`.

Enabled via `Config/DefaultEngine.ini`:

```ini
[PlatformFeatures]
PlatformFeaturesModule=GammaFrameworkSaveGuard
```

Delete that section and the engine falls back to the stock save system with no
code change. In editor builds it deliberately still writes plain `.sav` files.

---

## The World / Creatures boundary

`GammaFrameworkCreatures` depends on `GammaFrameworkWorld`. **World must never
depend on Creatures** — UBT rejects a circular module reference, and the split
is what makes the overworld half reusable on its own.

Six places in the original code had World systems reaching into creature code.
Two were fixed by widening a type (`TSubclassOf<AActor>` instead of the concrete
class). The rest go through **`FGF_CreatureBridge`** — a set of function hooks that
`GammaFrameworkCreatures` binds in its module startup:

| Hook | Used by |
|---|---|
| `GetEncounterRateMultiplier` | grid movement — a lead creature's trait changes the wild rate |
| `RegisterDungeonEntrance` / `ClearDungeonState` | grid movement — escape-rope return point |
| `DeleteCreatureSave` / `LoadCreatureSave` | game reset library |
| `GetPlayerGender` | dialogue — gendered text substitution |
| `GetEmblemCount` | settings — level cap by progression |
| `PushSettingsMirror` | settings — options mirrored into the save |
| `GiveCreature` / `GetVaultPageName` | dialogue — the give-creature event |

When the Creatures module is absent every hook is unbound and the safe accessors
return neutral defaults. World keeps working; it simply behaves as though the
game has no creatures.

**If you add a World feature that needs the creature layer, add a hook here.**
Do not add a module dependency. Creatures may call into World freely — only the
reverse needs the bridge.

## The Creatures / Battle boundary

The same rule one layer down, and the reason the split exists: **Creatures must
not depend on Battle**, so the data model survives the battle system being
replaced.

Two data-layer call sites legitimately want a combat answer. They go through
**`FGF_BattleBridge`** (`GF_BattleBridge.h`, declared in Creatures, bound by
Battle at startup):

| Hook | Used by | Default when Battle is absent |
|---|---|---|
| `IsEscapeBlocked` | skill definitions — can this creature flee | `false`, nothing traps it |
| `ApplyHeldItemEXPBoost` | creature manager — held-item EXP modifier | identity, EXP passes through |

Battle also binds `FGF_CreatureBridge::GetEncounterRateMultiplier`, because that
one is a trait *effect* rather than creature data.

**What deliberately stayed behind** — this was a shallow split, and these are the
seam you will redesign when you build the new battle system:

- **`AGF_Creature`** — the in-battle actor. `UGF_CreatureManagerSubsystem` owns
  `SpawnWildCreature`, `SwitchBattleCreature`, `CurrentBattleCreature` and
  `ActiveCreatureActors`, so pulling the actor out means surgery on an
  8,000-line file that is itself a rewrite candidate.
- **`UGF_CreatureTraitLibrary`** — traits are a creature property the data layer
  reads constantly, and `InitializeTraitOnActor` is entangled with battle state.
  Battle depends on Creatures, so it calls the library freely.
- **`GF_CreatureStatStageComponent`** — owned by `AGF_Creature`, so it follows it.

---

## Developing

**The source is hand-maintained. Edit it directly.**

That is worth stating because it was not always true. This plugin began as
generated output: a migration script read the original codebase and rewrote
every file, so editing a `.cpp` here would have been erased on the next run.
That pipeline is now retired — see `Docs/migration/` for what it did and why it
was worth retiring. The script refuses to run, because it would delete this
tree.

Two invariants are worth preserving as you work, both of which the module split
depends on:

1. **`GammaFrameworkWorld` must never reference `GammaFrameworkCreatures`**, and
   **`GammaFrameworkCreatures` must never reference `GammaFrameworkBattle`.**
   Dependencies run one way. Anything needing to cross that line goes through
   `FGF_CreatureBridge` or `FGF_BattleBridge`.

2. **Enum display strings are separate from enum identifiers.** Renaming a value
   without its `UMETA(DisplayName)` leaves the UI printing the old name — this
   bit once already, with the enum reading `Dewshield` while the summary screen
   said "Water Veil".

The first invariant has a check script — it derives each module's header list
and greps the modules above it, so it stays correct as files are added:

```bash
Tools/check_module_boundaries.sh
```

Exits non-zero and prints the offending `#include` lines on a violation. Worth
running before a commit that moves code between modules; UBT will also reject a
genuine circular dependency, but this tells you *which line* caused it.

---

## Known follow-ups

- **No Blueprints ship with the plugin.** It is C++ only; build your game's
  Blueprints on top of these classes.
- **No content ships with the plugin** — no species, sprites, audio or tables.
- **The runtime debugger lives in Battle** because its widget hard-codes the
  battle and give-creature tabs. Making the page list registration-driven would
  let the grid/item/quest pages move to World.
- **Battle**: the 4v4 turn engine lives in `Source/GammaFrameworkBattle/Turn/` — see `Docs/BATTLE_FLOW.md`. The single-battle code beside it is being retired.
- **Trait and temperament value names** were renamed, but their effect logic is
  a direct port and may deserve a design pass of its own.
- **Aether hits everything for 2×** except Ferrous. It is deliberately the widest
  coverage in the chart; pay for it with low base power in skill data, not by
  editing the matrix.
