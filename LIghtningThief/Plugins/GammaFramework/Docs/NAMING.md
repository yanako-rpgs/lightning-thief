# Gamma Framework — Naming Specification

The single source of truth for the Gamma Emerald → Gamma Framework extraction.
Every rename below was decided deliberately; nothing here is incidental.

**Rule:** Gamma Framework is a *genre* framework, not a Pokémon clone with the serial
numbers filed off. Mechanics (systems, formulas, rules) are kept — they are not
copyrightable subject matter. Expression (specific names, labels, flavour text)
is replaced wholesale.

---

## 1. Identity

| Old | New |
|---|---|
| Gamma Emerald (project) | **Gamma Framework** (plugin) |
| `PokemonEmerald` (module) | `GammaFrameworkWorld` + `GammaFrameworkCreatures` |
| *(new)* | `FGF_CreatureBridge` — the one-way valve between them |
| `PokemonEmeraldEditor` | `GammaFrameworkEditor` |
| `GESaveGuard` | `GammaFrameworkSaveGuard` |
| `GE_` class prefix | `GF_` class prefix |
| *(unprefixed classes)* | `GF_` prefix applied universally |

`GF_` is applied to **every** plugin type, not only the ones that already had `GE_`.
A plugin dropped into an arbitrary project must not collide with that project's
`UGridMovementComponent` or `ACore3D`. This is the one place the convention was
widened beyond a literal `GE`→`GF` swap.

---

## 2. Core vocabulary

| Pokémon term | Gamma Framework term | Notes |
|---|---|---|
| Pokemon | **Creature** | 2,555 sites |
| Shiny | **Unique** | incl. Shiny Charm → Unique Charm |
| Trainer | **Tamer** | OT → Original Tamer; `ATrainerMaster` → `AGF_TamerMaster` |
| Move | **Skill** | 869 sites; `AAttackMaster` → `AGF_SkillDefinition` |
| Ability (passive) | **Trait** | `EPokemonAbility` → `EGF_CreatureTrait` |
| Nature | **Temperament** | `EPokemonNature` → `EGF_Temperament` |
| Pokeball | **Core** | `APokeball3D` → `AGF_Core3D` |
| PP | **Uses** | `CurrentPP`/`MaxPP` → `CurrentUses`/`MaxUses` |
| IV (Individual Value) | **Potential** | `FPokemonIVs` → `FGF_CreaturePotential` |
| EV (Effort Value) | **Training** | `FPokemonEVs` → `FGF_CreatureTraining` |
| Pokedex | **Compendium** | |
| PC / Box | **Vault / Vault Page** | `UPokemonBoxSystem` → `UGF_VaultSystem` |
| Daycare | **Sanctuary** | |
| TM / HM | **Tome** | `bUniversalTM` → `bUniversalTome` |
| Gym / Badge | **Trial / Emblem** | mostly content-side flags |
| Egg Group | **Breeding Group** | `EEggGroup` → `EGF_BreedingGroup` |
| Friendship | **Bond** | 0–255 |
| Cry | **Call** | `UCryEnvelopeWidgetBase` → `UGF_CallEnvelopeWidget` |
| Faint | **Downed** | `bIsFainted` → `bIsDowned` |
| Struggle | **Last Resort** | fallback skill when all Uses are spent |
| Whiteout / Blackout | **Rout** | |
| PokéRus | *removed* | replaced by `Invigorated` status |
| Masuda method | **Foreign Pair** bonus | |

### Kept as-is (ordinary English / genre-universal)

Party · Level · Route · Nickname · Held Item · Evolution · Wild Encounter ·
Weather · Stat Stages · Species · Starter · EXP · Egg · Berry ·
Status conditions (Burn / Poison / Sleep / Freeze / Paralyze / Confuse)

Three shared enums were moved **into Core** so the genre-agnostic half can name
them without depending on the creature layer: `EGF_Element` and
`EGF_SkillCategory` (`GF_ElementTypes.h`), `EGF_STATUSEffect`
(`GF_StatusTypes.h` — items cure status, and items are Core), and
`EGF_PlayerGender` (`GF_CreatureBridge.h` — dialogue substitutes gendered text).

---

## 3. Stats

The Special split is reframed as magic. This is a **design change**, not a rename.

| Old | New |
|---|---|
| HP | HP |
| Attack | Attack |
| Defense | Defense |
| Special Attack | **Magic** |
| Special Defense | **Poise** |
| Speed | Speed |

Damage category follows: `ESplit { Physical, Special, Status }` →
`EGF_SkillCategory { Physical, Magic, Status }`.

- A **Physical** skill uses Attack vs Defense.
- A **Magic** skill uses Magic vs Poise.
- A **Status** skill deals no damage.

---

## 4. Elements

Eight duplicate 18-value type enums (`EPokemonPrimaryType`, `EPokemonSeconadryType`,
`EAttackPrimaryType`, `EAttackSeconadryType`, `EWeakness`, `EResistant`, `EImmune`,
`EPokemonType`) collapse into **one** `EGF_Element`. `UTypeConverterHelper` is deleted —
it existed only to convert between two copies of the same enum.

The roster is **Elemental Arcana**: 17 elements, Neutral included. `MAX` is a
sentinel, not an element.

| | | | |
|---|---|---|---|
| Neutral | Ember | Tide | Verdant |
| Gale | Terra | Spark | Sinew |
| Ferrous | Chitin | Stone | Frost |
| Venom | Umbra | Lumen | Aether |
| Wyrm | | | |

Structure of the effectiveness matrix (built fresh, not re-skinned) — 289 matchups:

- **Primal cycle:** Ember → Verdant → Tide → Ember
- **Kinetic cycle:** Gale → Terra → Spark → Gale
- **Attrition cycle:** Sinew → Ferrous → Chitin → Sinew — muscle bends metal,
  metal crushes carapace, the swarm outlasts the fighter
- **Opposed pair:** Lumen ↔ Umbra, mutually 2×
- **Terra and Stone are not interchangeable.** Terra is soil, so it grounds Spark
  and cannot touch Gale; Stone is hard mineral, so it knocks Gale out of the sky
- **Five immunities**, each doing a job: Neutral→Umbra, Terra→Gale, Spark→Terra,
  Venom→Ferrous, and Wyrm→Lumen, the one hard counter to the strongest element
- **The Ferrous rule:** Ferrous never both resists an element *and* hits it 2×.
  Before that rule it resisted 11 of 17, which made it the correct answer in any
  defensive slot. Seven resistances left — still the armoured element, no longer
  strictly the best one

The roster and the chart are hand-written in `GF_ElementTypes.h/.cpp` and are safe
to edit directly. They are **not** in `value_renames.py`.

---|---|---|---|
| Ember | Tide | Verdant | Gale |
| Stone | Ferrous | Spark | Frost |
| Venom | Umbra | Lumen | Aether |

Structure of the effectiveness matrix (built fresh, not re-skinned):

- **Primal cycle:** Ember → Verdant → Tide → Ember
- **Kinetic cycle:** Gale → Stone → Spark → Gale
- **Opposed pair:** Lumen ↔ Umbra, mutually 2×
- **Aether** is strong against everything except **Ferrous**, which resists it
- **Neutral** is 1× both ways, with no immunities

---

## 5. Enum values

The type renames above only changed the types. The **values** were the part that
was actually someone else's expression, and they are renamed too — 1:1 and
meaning-preserving, so the stat-modifier matrix and every trait effect
implementation keep working untouched.

- **25 temperaments**: Adamant → Ferocious, Jolly → Sprightly, Timid → Skittish, …
- **51 traits**: Intimidate → Menace, Levitate → Hover, Wonder Guard → Aegis,
  Static → Livewire, Chlorophyll → Sunfed, …

The full tables are in `Tools/value_renames.py`, with the effect of each trait in
a comment beside it.

These are applied **scoped** — inside the enum body and at `EnumName::Value` call
sites only. Several old values (`Static`, `Trace`, `Pickup`, `Damp`) are ordinary
tokens in an Unreal codebase, and a global word-boundary replacement corrupts
trace calls and item pickup code.

---

## 6. Cores

`EGF_CoreType` — the capture vessels. Renamed end to end 2026-09-11. The ladder is
named plainly so grade reads straight off the label; every situational core gets
one concrete word instead, so a glance at the bag separates tiers from tools.

| Was | Enum value | Name | Modifier | Rule |
|---|---|---|---|---|
| Poké Ball | `SimpleCore` | **Simple Core** | ×1.0 | The floor. Craftable anywhere |
| Great Ball | `GreaterCore` | **Greater Core** | ×1.5 | Second rung |
| Ultra Ball | `HyperCore` | **Hyper Core** | ×2.0 | Third rung. Freed the word *Warden* for the preserve core |
| Master Ball | `AbsoluteCore` | **Absolute Core** | always | Returns 255.0 — the formula never fails |
| Safari Ball | `WardenCore` | **Warden Core** | ×1.5 | Issued, never bought. Was `WildsCore` |
| Net Ball | `SnareCore` | **Snare Core** | ×3.5 | Against Chitin or Tide. Took the identifier off the repeat core |
| Dive Ball | `DepthCore` | **Depth Core** | ×3.5 | Underwater only |
| Nest Ball | `CradleCore` | **Cradle Core** | (41−Lvl)/10 | Clamped ×1.0–×4.0 |
| Repeat Ball | `EchoCore` | **Echo Core** | ×3.5 | Species already in the Compendium. The core remembers a shape it has held before |
| Timer Ball | `AeonCore` | **Aeon Core** | 1+0.3/rnd | Caps at ×4.0 on round 11 |
| Luxury Ball | `HearthCore` | **Hearth Core** | ×1.0 | Bond gain |
| Premier Ball | `EliteCore` | **Elite Core** | ×1.0 | Cosmetic. Given, not sold |
| Dusk Ball | `GloamCore` | **Gloam Core** | ×3.5 | Night or underground. `NightCore` had to go — nightcore is a music genre |
| Heal Ball | `MendCore` | **Mend Core** | ×1.0 | Full heal on claim |
| Quick Ball | `SuddenCore` | **Sudden Core** | ×5.0 | Round one only. Set against Aeon as the other end of one axis |
| Cherish Ball | `BoonCore` | **Boon Core** | ×1.0 | Event Creature |
| — | `OmenCore` | **Omen Core** | ×3.5 | On a unique Creature. A one-of-a-kind is read as a sign, not as sparkle |

**Order is load-bearing.** The enum serialises as `uint8`, so every value above was
renamed *in place* and none moved index. An item asset set to the old `MeshCore` (5)
still reads as Snare Core, and one set to the old `SnareCore` (8) still reads as Echo
Core. Append new cores at the end; never reorder.

**Three files move together.** The enum is in `GammaFrameworkWorld/GF_ItemEnums.h`,
the default in `GF_ItemData.h`, and the multipliers and display strings in
`GammaFrameworkCreatures/GF_CatchingLibrary.cpp`. Cores are **not** in
`value_renames.py` — they are hand-maintained.

**One implementation.** `UGF_CoreCatchingLibrary` (`GF_CaptureCoreLibrary.h/.cpp`)
was a dead duplicate — no C++ callers, no Blueprint references, and multipliers that
disagreed with the live ones. Deleted 2026-09-11; `GetCoreName` and `IsAbsoluteCore`
moved into `UGF_CatchingLibrary` first. The live path is
`UGF_CreatureManagerSubsystem::AttemptCatch` → `UGF_CatchingLibrary::CalculateCatchAttempt`.

### Still open

- `GetCoreName` returns `FText::FromString`, so no core name is localisable yet.
- The Absolute Core is detected from the asset (`CatchRateModifier >= 255`) rather
  than from the enum, so a misconfigured asset silently stops being absolute.
- `CalculateCatchAttempt` sets `ShakeCount = 3` on the Absolute and critical-capture
  branches, where the shake loop means 4. Anything that plays wobbles off `ShakeCount`
  will play a full three-wobble breakout build-up before a critical capture seals.

### Proposed, not built

Eight the existing formula could carry. The first four read the four-slot board or
the map — things this genre's capture items never had to think about.

| Name | Rule | Cost |
|---|---|---|
| Lone Core | ×3.5 when the target is the last enemy standing | one bool on `GetCoreModifier` |
| Choir Core | ×2.5 while 3+ enemies stand | one int on `GetCoreModifier` |
| Kindred Core | ×3.0 if an active Creature shares an element with the target | reads the board against `PrimaryElement` |
| March Core | ×4.0 inside Godsmarch, ×0.5 elsewhere | one bool, same shape as `bIsNightOrCave` |
| Ashen Core | ×2.0 *on top of status*, ×1.0 on a clean target | touches the status path |
| Riven Core | ×3.5 below 25% HP, ×0.5 above | reads HP the formula already has |
| Toll Core | ×2.0, not consumed on failure — costs the claimer 25% max HP instead | hook on the failure branch |
| Oath Core | ×1.0, +10% all stats, can never be released or traded | flag on `FGF_CreatureInstanceData` |

## 7. Deliberately deferred

- **Blueprint / UMG migration**: not in this pass. Plugin is C++ only for now.
- **Content**: no species, sprites, audio or data tables move into the plugin.
- **Trait effect balance**: the names changed, the implementations are a direct
  port and may deserve a design pass.

## 8. Not solved by renaming

Nintendo / The Pokémon Company sued Palworld in Japan (2024) over **patents**,
reportedly covering capture mechanics — throwing a device at a creature in the
field. Patents can reach mechanics in a way copyright cannot. The throw-a-Core-
to-capture loop is therefore the most exposed part of this design, and no amount
of renaming addresses it. Worth an actual IP lawyer's opinion before shipping
commercially.
