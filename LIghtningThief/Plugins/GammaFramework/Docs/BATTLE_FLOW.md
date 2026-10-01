# Battle Flow — the 4v4 turn engine

`GammaFrameworkBattle/Turn/`. Six files, no timers.

This is the replacement for the single-battle port that sits beside it. The old
`AGF_BattleManager` still compiles; the damage formula in `UGF_BattleComponent`
is still the formula, and the resolver below calls into it.

| File | What it is |
|---|---|
| `GF_BattleTypes.h` | Slots, phases, actions, turn-order entries, turn context, step kinds |
| `GF_BattleBoard.h/.cpp` | Who is standing where. Seat queries only |
| `GF_TurnOrderLibrary.h/.cpp` | Speed, priority, sorting, target expansion. Pure functions |
| `GF_BattleFlowComponent.h/.cpp` | The state machine |
| `GF_BattleResolver.h/.cpp` | Plan and apply damage and every secondary effect; end-of-round ticks |
| `GF_BattleAI.h/.cpp` | Enemy decisions, four difficulty tiers |

---

## The one idea

**Everything addresses a slot, never "the player's creature".**

`FGF_BattleSlot` is `(Side, Index)`. The board maps slots to actors. The flow
component holds slots as state and looks the actor up when it needs one.

That is what makes four-a-side cheap. The old code had `PlayerCreature` and
`EnemyCreature` as fields, so widening the board meant touching every call site
that named them. Here, widening the board is `MaxActiveSlots = 4`.

---

## The round

```
Intro      one step
              |
Command    one OnCommandRequested per living player seat, in slot order
           answer each with Submit*Command
           CancelLastCommand backs out to re-issue an earlier one
              |
Resolving  commands lock, order is built and broadcast
           one OnTurnBegin + AcknowledgeTurn per action
              |
RoundEnd   one step: status damage, weather, held items, Uses
           send replacements out here
              |
           back to Command, or Victory / Defeat / Ended
```

Every action costs its creature its one turn — items included. Four a side means
up to **eight actions in a round**.

---

## Nothing advances on a timer

The flow stops at each presentation point, hands out a token, and waits.

```
OnStepAwaitingAcknowledgement (StepKind, StepToken)
        |
   ... you play the animation, the dialogue, the camera move ...
        |
AcknowledgeStep(StepToken)          or  AcknowledgeTurn(StepToken, Result)
```

A machine that takes 900 ms to play a hit acknowledges 900 ms later. A machine
that takes 200 ms acknowledges after 200 ms. Neither can start the next turn on
top of the last one, because the next turn is not scheduled — it is waiting.

**The token is not decoration.** A stale token is logged and ignored. That is what
stops a late animation callback — one that fires after the player skipped ahead —
from advancing the battle a second time.

`AcknowledgeTurn` takes an `EGF_TurnResult` so the layer that performed the turn
reports what actually happened: `Completed`, `SkippedIncapacitated` for a sleeping
creature, and so on. The flow does not guess. `AcknowledgeStep` reports `Completed`.

### Step kinds

| Kind | Raised when | You do |
|---|---|---|
| `Intro` | after `StartBattle` | send-outs, opening dialogue |
| `Turn` | a creature's turn came up | plan, present, apply |
| `Downed` | one or more creature went down | down animation, award EXP |
| `RoundEnd` | all actions spent | `ResolveRoundEndTicks`, play the messages |
| `Replacement` | seats emptied and a bench can fill them | `SendOutReserve`, or decline |
| `Outcome` | battle decided | victory/rout sequence |

---

## Events worth binding

| Event | For |
|---|---|
| `OnBattlePhaseChanged` | HUD state. Bind this instead of polling `GetPhase` |
| `OnCommandRequested` | open the action menu for that seat |
| `OnTurnOrderChanged` | **the order ribbon** — see below |
| `OnTurnBegin` / `OnTurnEnd` | dialogue triggers, stat callouts, camera |
| `OnCreatureDowned` | per creature, before the `Downed` step |
| `OnReplacementNeeded` | replacement menu |
| `OnBattleEnded` | outcome |

---

## The turn-order ribbon

`OnTurnOrderChanged` carries `TArray<FGF_TurnOrderEntry>`, and each entry already
holds the icon, the display name, the side and the position. **Bind it once and
the widget never reaches back into the component.**

It fires in both shapes:

- **During Command** — a live prediction. Every living seat appears; seats with an
  order use its real priority, seats still waiting are ranked on Speed alone. It
  fires again after every command, so the ribbon settles toward the real sequence
  as the player commits. That matters because at four slots the fourth order
  always depends on the first three.
- **From lock-in onward** — the committed sequence.

Entries are **marked, never removed**. A creature that goes down before its turn
gets `bCancelled`; one that has acted gets `bHasActed`. Deleting the entry would
make the whole ribbon shuffle left mid-round and the player would lose track of a
sequence they were reading a second ago.

### Names are disambiguated

Two unnicknamed creature of the same species on the same side come back as
`Ashling A` and `Ashling B`. This is in `FGF_TurnOrderEntry::DisplayName`,
`FGF_TurnContext::DisplayName` and `GetDisplayNameForSlot`.

Without it the ribbon shows four identical names, which is the confusion
four-a-side introduces and the reason the ribbon exists at all.

---

## Whose turn is it

| Getter | Valid |
|---|---|
| `GetCurrentCreature` / `GetCurrentSlot` / `GetCurrentSide` | during Resolving |
| `GetCommandingCreature` / `GetCommandingSlot` | during Command |
| **`GetSpotlightCreature`** / `GetSpotlightSlot` / `GetSpotlightName` | either |

`GetSpotlight*` returns whoever the screen is about: the creature being commanded
during Command, the creature acting during Resolving, null otherwise. Bind that
for the on-screen "who is going" indicator — binding the other two means writing
the same phase check in every widget.

`GetCurrentTurnContext` carries the whole turn — creature, name, action, targets,
round, position in the sequence — so a listener that runs a frame late still sees
the turn it was told about.

---

## Turn order

Priority bracket, then effective Speed, then a tiebreak **rolled once** when the
action was stamped.

The tiebreak is not an implementation detail. The obvious version flips a coin
inside the sort comparator, which makes the comparator answer differently for the
same pair and produces an order that is not merely random but incoherent — a
round that occasionally sequences wrong, at a rate too low to reproduce on
purpose.

Effective Speed = base Speed × stat stage × weather trait, **then** halved by
paralysis. Halving last means a +2 paralysed creature is still slowed relative to
itself.

Non-skill actions get their bracket from `PriorityConfig` on the component:
Flee 7, Swap/Item/Marque 6, Brace 4, Pass −7. Skills read `Priority` off their own
asset, from the **class default object** — no spawn.

---

## Resolving a turn

Damage is split into **plan** and **apply**, and the gap between them is where the
presentation goes.

```
OnTurnBegin
    |
PlanCurrentTurn()      -> FGF_ActionResolution. Touches nothing.
    |                     Accuracy and crits are already rolled: the numbers
    |                     are final.
   ... animation, dialogue, health bars tween to a number you already have ...
    |
ApplyResolution(Plan)  -> HP moves, Uses are spent, downs are flagged
    |
AcknowledgeTurn(Token, Result)
```

`PlanCurrentTurn` is the one-node form on the flow component — it already knows
the board, the action and the weather. `UGF_BattleResolver::PlanAction` is the
same thing when you need to plan a hypothetical.

**Plan once.** It rolls accuracy and crits, so calling it twice gives two
different battles. `ApplyResolution` refuses to run twice on the same resolution,
because a presentation retry must not subtract the same damage again.

The resolution carries an `FGF_TargetResolution` per seat: the formula's own
verdict (crit, effectiveness, immunity, miss, message), the damage actually
applied after the spread discount and Brace, HP before and after, and whether
that seat went down.

The damage formula itself is untouched — element effectiveness, the
Physical/Magic split, stat stages, crits, traits, held items and weather are still
`UGF_BattleComponent`'s. What the resolver adds is what four-a-side needs: looping
a spread skill over its targets, the multi-target discount, and Brace.

Multi-hit skills roll their hit count once for the whole action, then roll
accuracy, crit and damage independently per hit — which is what makes them swingy
by design. Mid-Fly and mid-Dig targets are caught here, because the damage
formula checks accuracy and evasion but not where the target physically is.

### Secondary effects

Everything a skill does besides damage is rolled in the plan and carried out in
the apply, for the same reason the damage is: **the plan has to be final** before
the presentation shows it.

`bConnected` on each `FGF_TargetResolution` is the gate — not missed, not immune,
not Protected — and every effect below hangs off it.

| | |
|---|---|
| **Status** | `FGF_StatusOutcome`: whether the chance roll passed, whether an element/trait/hardcoded immunity blocked it (with the framework's own message), whether it actually landed |
| **Stat stages** | `FGF_StatStageOutcome` per change: what was asked for, what actually moved after the ±6 clamp, whether a trait blocked it, and the ready-to-print message |
| **Flinch** | rolled from the skill class, applied through `ApplyFlinch` so Resolute still fires |
| **Contact traits** | Bramblehide, Livewire and the rest — read off the target, landed on the *attacker* |
| **Recoil / drain / self-heal** | including Foulsap, which turns a drain into damage (`bDrainBackfired`) |
| **Trapping, Protect, crit-stage, Refresh, Bide** | wired through the existing helpers |

Three details worth knowing, because each one is a bug if you get it backwards:

**A status skill never reaches the accuracy check in the damage formula** — it
short-circuits on the Status split and returns before `CheckSkillHit` runs. So the
resolver rolls accuracy itself for those. Without that, every status skill lands
for free.

**You do not roll accuracy against yourself.** A Self-shaped skill, or one aimed
at the actor's own seat, always connects. Otherwise Recover and Swords Dance can
miss — which is wrong, and nearly impossible to spot, because it looks like the
heal simply did not fire.

**Effects skip a creature that just went down.** Statusing a corpse produces
messages about a creature the player has already watched fall over.

`ActualChange == 0` with `bBlockedByTrait == false` means the stat was already at
+6 or −6 — a different message from "a trait stopped you", which is why both flags
are on the struct.

The skill asset names a stat direction (`AttackUp` / `AttackDown`) *and* carries a
signed `StatStageAmount`, so the two can disagree. The amount wins, because it is
what the stage component applies — but a mismatch is almost always an author
leaving the default `-1` on an Up skill, so it logs a warning naming the creature.

### End of round

`ResolveRoundEndTicks()` during the `RoundEnd` step, before acknowledging it.

Burn and poison, then weather, then traps, then trait ticks and held items — a
fixed order, so the same board always ticks the same way and a message sequence is
reproducible. Returns one `FGF_RoundEndTick` per creature that had anything happen,
each with its own ordered `Messages` array.

Traits and held items run **after** the chip, so a Sustain Charm can save a
creature the chip would otherwise have downed.

Chip fractions live in `ChipConfig` on the component. They are exposed because
eight creature ticking at 1/8 a round is twice the attrition the same numbers
produce one-on-one, and that is the first thing anyone tunes.

Sleep is **not** ticked here. `TickSleepAtTurnStart` is called at the start of the
sleeper's own turn instead — a creature that falls asleep and has its counter
ticked in the same round loses a turn it never got the chance to lose.

### Brace, and a name collision worth fixing

The action-menu **Brace** — halve incoming damage, +1 Poise, lasts the round — is
`AGF_Creature::bIsBracing`. It is set when a Brace order resolves and cleared at
the start of the next round, not at round end, so a creature that braced is still
protected against the end-of-round chip it braced for.

**This is not the Brace skill.** That one is the store-and-release move
(`bIsBiding`, `StartBide`, `GetBideReleaseDamage`). Two different mechanics wearing
the same word: one is a stance every creature can take from the menu, the other is
a skill a species has to learn. Worth renaming one of them before either gets
content.

---

## The enemy AI

`EnemyAIProfile` on the flow component. Four tiers, differing mostly in
**targeting** rather than skill choice — because that is where the difficulty of a
four-wide board actually lives. A weak AI spreads damage across four healthy
enemies and kills none; a strong one concentrates and removes a quarter of your
output permanently.

| Tier | Behaviour |
|---|---|
| `Random` | Any usable skill at any living seat. For early wild encounters |
| `Basic` | Highest base power, random target. Hits hard, aims nowhere |
| `Smart` | Scores every **(skill, target)** pair on estimated damage — matchup, STAB, the right defence, stat stages. Won't waste a status skill on an already-statused target |
| `Expert` | Smart, plus: focus-fires anything it can remove this round, values a spread skill by what it does to the whole line, heals, swaps out of bad matchups, respects the ace lock |

Per-tamer switches sit alongside the tier, because "as clever as a Trial Leader
but with no potions" is a real setup: `HealItems`, `HealThreshold`, `bMaySwap`,
`AceReserveIndex`, `LastResortSkill`.

`MistakeChance` (default 0.15) is the chance of taking the **second**-best option
instead of the best. A tier that always plays perfectly reads as unfair rather
than hard, and it is also predictable — a player who knows the AI is optimal can
plan around it exactly. It only ever drops one rank; an AI that occasionally plays
its worst option is not a difficulty setting, it is a different AI.

`ScoreAllOptions` returns the full ranking with a `Reason` string per option.
Reading that ranking is the only practical way to tell a bad decision from a bad
score.

Expert's swap threshold is **1.75×** — a candidate on the bench has to be that
much better than what is on the field, because a swap costs a whole action and
gives the player a free hit on the arrival.

### Why the AI does not use the real damage formula

`EstimateDamage` is the classic damage shape with element, STAB and stat stages,
returning a number in HP. It is an estimate on purpose: the real formula spawns a
skill actor per call, and scoring four skills against four targets is sixteen
spawns per enemy per round — 128 with a full enemy line. The estimate ranks
options in the same order at none of the cost, and being in HP units is what lets
the finishing-blow check compare it straight against the target's remaining HP.

---

## The presentation layer -- `Stage/`

`Turn/` is the engine. `Stage/` is the layer a game actually drives it with:
holding a turn open while things play, skills that say when they are done,
getting into a battle and back out. It is a copy of Godsmarch's game-module
battle code, renamed `GS_` -> `GF_`, so a new project starts with the same
Blueprint workflow Godsmarch uses.

| Godsmarch | Plugin | What it is |
|---|---|---|
| `AGS_BattleManager` | **`AGF_TurnBattleManager`** | Drives the flow. Presentation holds, `FinishTurnPresentation`, `AcknowledgePresentedStep`, claim/flee, menu helpers |
| `AGS_Skill` | `AGF_Skill` | Skill base: `OnPlayAttack` builds the attack, `NotifyImpact` / `NotifyFinished` report back, `OnImpact` / `OnFinished` to bind |
| `UGS_BattleLauncher` | `UGF_BattleLauncher` | Overworld -> battle -> overworld |
| `AGS_BattleArena` | `AGF_BattleArena` | What a battle needs from its level |
| `AGS_BattleAnchor` | `AGF_BattleAnchor` | One placeable seat marker |
| `UGS_BattleFX` | `UGF_BattleFX` | Spawning effects at the right place and facing |

The manager is `TurnBattleManager` only because `AGF_BattleManager` was already
taken by the old single-battle port. Every other name is a straight prefix swap.

Holding a turn, in short: `AddPresentationHold` / `ReleasePresentationHold` (or
the static `HoldTurn` / `ReleaseTurn` nodes) keep the turn open; holds are
counted, so overlapping effects are safe as long as every Add has a Release.

**Godsmarch does not use these.** It still runs its own `GS_` classes, and the
two copies are independent -- a fix in one does not reach the other. If a project
built on `Stage/` misbehaves where Godsmarch does not, diff the two first.

The one deliberate difference: the `gs.BattleDebug` overlay was not brought
across, so `bFeedDebugger` and its hooks are gone from the manager.

---

## What changed outside the Turn folder

### Skill targeting

**`EGF_SkillTargetShape`** in `GammaFrameworkWorld/GF_ElementTypes.h`, next to
`EGF_SkillCategory` — authored on the skill asset in Creatures, read by the flow
in Battle, so it belongs in the shared layer that both can name. Same reasoning
that put `EGF_SkillCategory` there.

**`AGF_SkillDefinition`** gains `TargetShape` (defaults to `Single`, so every skill
authored before this keeps behaving exactly as it did) and
`MultiTargetDamageMultiplier` (defaults to **0.65**).

0.65 rather than the usual 0.75 because the board is twice as wide: a spread skill
lands on four targets, and at 0.75 that is three full hits for one action, which
makes spreading strictly better than concentrating. The whole tactical question of
the format is focus-one-down versus spread, and this number is what keeps it a
real choice. It applies on the number of seats actually **struck**, so a spread
skill against one surviving enemy pays no penalty.

### Side-on animations

`EGF_CreatureFrontAnimations` and `EGF_CreatureBackAnimations` are gone, replaced
by **`EGF_CreatureAnimState`** — six values: `Idle`, `IdleLowHP`, `Call`, `Attack`,
`Hurt`, `Down`.

The stage is viewed side-on, both lines in profile, so there is no angle from
which you see a face on one side and a back on the other. A creature needs one set
of art, not two. The old layout was a 24-value Front enum plus an identical
24-value Back enum with gender and unique folded into the key names — 48 flipbook
slots per species to express six states, and double the art budget for a view that
no longer exists.

Variants are separate **maps** rather than more enum keys, so a species author
fills six entries and gets six-value dropdowns:

```
SideAnimations              <- required. Everything falls back to here
SideAnimationsUnique        <- the "shiny" set
SideAnimationsFemale        <- only for visibly different females
SideAnimationsFemaleUnique
```

`GetSideAnimation(State, bUnique, bFemale, OutVariant)` applies the fallbacks:
FemaleUnique → **Unique** → Female → Normal. Unique outranks Female deliberately —
the unique palette is what a player is looking for and will notice missing,
whereas a female silhouette on a species that never drew one is not a visible
loss. `IdleLowHP` falls back to `Idle` **within the chosen set** before dropping to
a less specific one, so a unique creature at low HP stays unique.

A species with nothing but the six base entries is fully playable.

On the creature actor: `GetAnimationForState`, `GetIdleAnimation` (picks Idle or
IdleLowHP for you), `IsAtLowHP`, and `LowHPAnimationThreshold` — default a third,
matching where the genre puts the low-health warning, so the art change and the
beeping health bar happen at the same moment.

The Compendium's asset-registry tag moved with it: `FrontIdleAnimation` →
`BattleIdleAnimation`, reading `SideAnimations[Idle]`. No species assets existed
yet, so nothing needed resaving.

Module boundaries are unchanged — `Tools/check_module_boundaries.sh` passes.

---

## Not done yet

- **Two-turn skills** (Fly, Dig) have their asset fields and their
  `SemiInvulnerableState` respected on defence, but the charge turn is not
  sequenced — that needs the flow to carry an order across a round boundary.
- **Marque resolution** lives in the Creatures module. When a Core seals, call
  `AbortBattle(EGF_BattleOutcome::Claimed)`.
- **Swap** is sequenced but not performed — call `ExecuteSwap` while the Swap turn
  is resolving, before acknowledging it.
- **Item effects** are not applied. The flow sequences an Item action and costs the
  creature its turn; what the item does is the inventory system's business.
- **Reflect and Veil** are passed as `false` — side-wide screens are not modelled
  on the new board yet.
- **Confusion self-hit** is not rolled. `ApplyStatusCondition` will set the
  volatile, but nothing makes a confused creature hurt itself.
- **Weight-based and fixed-level damage** (`bIsWeightBasedPower`,
  `bIsFixedLevelDamage`) fall through to the normal formula.
