#!/usr/bin/env python3
"""
Gamma Emerald  ->  Gamma Framework migration.

Copies Source/PokemonEmerald, Source/PokemonEmeraldEditor and Source/GESaveGuard
into the plugin's four modules, applying the rename spec in Docs/NAMING.md.

Re-runnable: wipes the module source trees (but not the .Build.cs / module .cpp
files) and regenerates them from the pristine Gamma Emerald source every time.
That matters, because the first pass will not compile clean and iterating means
editing THIS FILE and re-running, never hand-patching the output.

Design notes on why this is not a sed script:

  * "Move" in this codebase overwhelmingly means *movement* (grid, cursor, NPC),
    not a battle move. Blind replacement produces RemoveAt -> ReskillAt. So the
    Move -> Skill rename is driven by an explicit allow-list of identifiers.
  * "IV"/"EV"/"PP" are two-letter substrings of EVENT, LEVEL, EVisibility,
    APPLY, HPPercent... so those are exact-identifier renames only.
  * "Ball" is a substring of Balloon, "Cry" of Crystal, "ability" of capability.
    Those use lookahead guards instead of plain replacement.
"""

import os
import re
import shutil
import sys

# ── Paths ─────────────────────────────────────────────────────────────────────

SRC_ROOT = r"X:\UnrealProjects\GammaEmerald\GammaEmerald\Source"
PLUGIN   = r"X:\UnrealProjects\GammaFramework\Plugins\GammaFramework"
DST_ROOT = os.path.join(PLUGIN, "Source")

MOD_WORLD      = "GammaFrameworkWorld"
MOD_CREATURES = "GammaFrameworkCreatures"
MOD_EDITOR    = "GammaFrameworkEditor"
MOD_BATTLE    = "GammaFrameworkBattle"
MOD_SAVEGUARD = "GammaFrameworkSaveGuard"

# Files that carry each module's own identity; never delete on re-run.
MODULE_OWN_FILES = {
    "GammaFrameworkWorld.Build.cs", "GammaFrameworkWorld.h", "GammaFrameworkWorld.cpp",
    "GammaFrameworkCreatures.Build.cs", "GammaFrameworkCreatures.h", "GammaFrameworkCreatures.cpp",
    "GammaFrameworkEditor.Build.cs",
    "GammaFrameworkSaveGuard.Build.cs",
    "GammaFrameworkBattle.Build.cs", "GammaFrameworkBattle.h", "GammaFrameworkBattle.cpp",
    "GF_BattleBridge.h", "GF_BattleBridge.cpp",
    "GF_ElementTypes.h", "GF_ElementTypes.cpp",
    "GF_CreatureBridge.h", "GF_CreatureBridge.cpp",
    "GF_StatusTypes.h", "GF_WeatherTypes.h",
}

# ── Module assignment ─────────────────────────────────────────────────────────
# Anything under Source/PokemonEmerald not listed here lands in Core.

CREATURE_PREFIXES = (
    "Pokemon", "AttackMaster", "BattleManager", "BattleManagementFunctions",
    "GE_BattleComponent", "GE_BattleParticipantInterface", "Catchinglibrary",
    "PokeballCatchingLibrary", "Helditembattlehelper", "FlinchHelperLibrary",
    "GE_FaintBurstLibrary", "TrainerMaster", "CryEnvelopeWidgetBase",
    "Debugmenusubsystem",
    # Routes hold the wild encounter tables, which reference species.
    "RouteData", "RouteSubsystem", "RouteVolume",
)
# Whole folders that go to Creatures. Debugger/ goes here because
# SGE_DebuggerWidget hard-codes its battle and give-creature tabs; making the
# page list registration-driven is a follow-up, not part of the rename.
# The battle layer. Checked before CREATURE_PREFIXES so these win.
#
# The trait library (PokemonAbilities) deliberately stays in Creatures. Traits
# are a property of a creature, the data layer reads them constantly, and
# InitializeTraitOnActor is tangled with battle state in a way that does not
# split cleanly. Battle depends on Creatures, so it calls them freely.
BATTLE_PREFIXES = (
    "GE_BattleComponent", "GE_BattleParticipantInterface",
    "BattleManager", "BattleManagementFunctions",
    "Helditembattlehelper", "FlinchHelperLibrary", "GE_FaintBurstLibrary",
    "TrainerMaster", "Debugmenusubsystem",
)
BATTLE_DIRS = ("Debugger",)

CREATURE_DIRS = ("Daycare", "Trading", "TownMap")

# Dropped entirely.
SKIP_FILES = {
    "MyClass.h", "MyClass.cpp",                              # empty engine boilerplate
    "AssetDefinition_PokemonSpeciesData.h",                  # 100% commented out
    "TypeConverterHelper.h", "TypeConverterHelper.cpp",      # converts between two
                                                             # copies of one enum;
                                                             # both collapse to EGF_Element
    "PokemonEmerald.h", "PokemonEmerald.cpp",                # replaced by module .cpp
}

API_MACRO = {
    MOD_WORLD:      "GAMMAFRAMEWORKWORLD_API",
    MOD_CREATURES: "GAMMAFRAMEWORKCREATURES_API",
    MOD_EDITOR:    "GAMMAFRAMEWORKEDITOR_API",
    MOD_BATTLE:    "GAMMAFRAMEWORKBATTLE_API",
    MOD_SAVEGUARD: "GAMMAFRAMEWORKSAVEGUARD_API",
}

# ── Stage 1: ordered plain replacements ───────────────────────────────────────
# Applied longest-first. Order within the list is significant.

PLAIN = [
    # -- module / API identity (must precede the bare "Pokemon" rule) ----------
    ("POKEMONEMERALDEDITOR_API", "GAMMAFRAMEWORKEDITOR_API"),
    ("PokemonEmeraldEditor",     "GammaFrameworkEditor"),
    ("FPokemonEmeraldEditorModule", "FGammaFrameworkEditorModule"),
    ("PokemonEmerald/",          ""),      # module root is an include path now
    ("PokemonEmerald",           "GammaFramework"),
    ("GESaveGuardSystem",        "GF_SaveGuardSystem"),
    ("GESaveGuard",              "GammaFrameworkSaveGuard"),
    ("LogGESaveGuard",           "LogGFSaveGuard"),

    # -- element enums: eight duplicates collapse to one ----------------------
    ("EPokemonPrimaryType",   "EGF_Element"),
    ("EPokemonSeconadryType", "EGF_Element"),
    ("EPokemonSecondaryType", "EGF_Element"),
    ("EAttackPrimaryType",    "EGF_Element"),
    ("EAttackSeconadryType",  "EGF_Element"),
    ("EAttackSecondaryType",  "EGF_Element"),
    ("EPokemonType",          "EGF_Element"),
    ("EWeakness",             "EGF_Element"),
    ("EResistant",            "EGF_Element"),
    ("EImmune",               "EGF_Element"),
    ("PrimaryType",           "PrimaryElement"),
    ("SeconadryType",         "SecondaryElement"),
    ("SecondaryType",         "SecondaryElement"),
    ("AttackTypeToPokemonType", "ELEMENT_CONVERTER_DELETED"),
    ("PokemonTypeToAttackType", "ELEMENT_CONVERTER_DELETED"),

    # -- damage category -------------------------------------------------------
    ("ESplit", "EGF_SkillCategory"),

    # -- stat rename: Special becomes Magic / Poise ---------------------------
    ("SpecialAttackMultiplier",  "MagicMultiplier"),
    ("SpecialDefenseMultiplier", "PoiseMultiplier"),
    ("SpecialAttackStage",       "MagicStage"),
    ("SpecialDefenseStage",      "PoiseStage"),
    ("bIsSpecialAttack",         "bIsMagic"),
    ("bIsSpecialDefense",        "bIsPoise"),
    ("SpecialAttack",            "Magic"),
    ("SpecialDefense",           "Poise"),
    ("SpAtkDown", "MagicDown"), ("SpAtkUp", "MagicUp"),
    ("SpDefDown", "PoiseDown"), ("SpDefUp", "PoiseUp"),
    ("bIsSpAtk", "bIsMagic"), ("bIsSpDef", "bIsPoise"),
    ("SpAtk", "Magic"), ("SpDef", "Poise"),
    ("SPATK", "MAGIC"), ("SPDEF", "POISE"),

    # -- vault (was PC / box) -------------------------------------------------
    # MUST precede the Pokemon -> Creature rule below, or these never match.
    ("UPokemonBoxSystem",      "UGF_VaultSystem"),
    ("PokemonBoxSystem",       "GF_VaultSystem"),
    ("UPokemonPCController",   "UGF_VaultController"),
    ("PokemonPCController",    "GF_VaultController"),
    ("UPokemonPCScreenWidget", "UGF_VaultScreenWidget"),
    ("PokemonPCScreenWidget",  "GF_VaultScreenWidget"),
    ("FPokemonBox",            "FGF_VaultPage"),

    # -- core vocabulary -------------------------------------------------------
    ("Poke Ball", "Core"), ("Poke ball", "Core"), ("Poké Ball", "Core"),
    ("Pokeball", "Core"), ("PokeBall", "Core"),
    ("POKEBALL", "CORE"), ("pokeball", "core"),
    ("Pokedex", "Compendium"), ("POKEDEX", "COMPENDIUM"), ("pokedex", "compendium"),
    ("PokeRus", "Invigorated"), ("PokeMgr", "CreatureMgr"),
    ("Pokemon", "Creature"), ("POKEMON", "CREATURE"), ("pokemon", "creature"),
    ("Shiny", "Unique"), ("SHINY", "UNIQUE"), ("shiny", "unique"),
    ("Trainer", "Tamer"), ("TRAINER", "TAMER"), ("trainer", "tamer"),
    ("Daycare", "Sanctuary"), ("DAYCARE", "SANCTUARY"), ("daycare", "sanctuary"),
    ("WhiteOut", "Rout"), ("Whiteout", "Rout"), ("WHITEOUT", "ROUT"),
    ("BlackOut", "Rout"), ("Blackout", "Rout"),
    ("Friendship", "Bond"), ("friendship", "bond"),
    ("Struggle", "LastResort"), ("struggle", "lastResort"),
    ("Masuda", "ForeignPair"),
    ("Fainted", "Downed"), ("Fainting", "Downing"), ("Faints", "Downs"),
    ("Faint", "Down"), ("fainted", "downed"), ("faint", "down"),
    ("EggGroup", "BreedingGroup"), ("EEggGroup", "EGF_BreedingGroup"),
    ("Badge", "Emblem"), ("badge", "emblem"),
    ("Gym", "Trial"), ("gym", "trial"), ("GYM", "TRIAL"),
    ("Nature", "Temperament"), ("natureMod", "temperamentMod"),
    ("NATURE", "TEMPERAMENT"),

    # -- the GE_ prefix becomes GF_ -------------------------------------------
    ("GE_", "GF_"), ("SGE_", "SGF_"), ("FGE_", "FGF_"), ("UGE_", "UGF_"),
    ("LogGE", "LogGF"),
]

# ── Stage 2: guarded regex replacements ───────────────────────────────────────
# Lookaheads stop Balloon/Crystal/capability from being mangled.

GUARDED = [
    # Console command prefix (ge.debugger, ge.SaveVaultSelfTest). Word-bounded,
    # because as a plain substring "ge." also matches inside Page.h -> Pagf.h,
    # Damage. -> Damagf. and every other word ending in "ge".
    (r"\bge\.", "gf."),
    (r"\bBalls\b",       "Cores"),
    (r"Ball(?![a-z])",   "Core"),      # BallType -> CoreType, Balloon untouched
    (r"Cry(?![a-z])",    "Call"),      # CrySubmix -> CallSubmix, Crystal untouched
    (r"\bAbilities\b",   "Traits"),
    (r"\babilities\b",   "traits"),
    (r"Abilities(?![a-z])", "Traits"),
    (r"Ability(?![a-z])",   "Trait"),
    (r"\bability\b",     "trait"),     # word-bounded: capability/durability safe
    (r"\bABILITY\b",     "TRAIT"),
    (r"\bTMs\b",         "Tomes"),
    (r"\bTM\b",          "Tome"),
    (r"TM(?=[A-Z])",     "Tome"),      # TMMove -> TomeMove
]

# ── Stage 3: exact-identifier renames ─────────────────────────────────────────
# Everything here is matched with \b...\b. Nothing in this table is safe as a
# substring.

IDENT = {
    # -- IVs become Potential -------------------------------------------------
    # NOTE the spellings: IDENT runs AFTER the PLAIN pass, so by the time these
    # match, Pokemon has already become Creature and SpecialAttack has already
    # become Magic. Keying these on "FPokemonIVs" silently matches nothing.
    "FCreatureIVs": "FGF_CreaturePotential",
    "FCreatureEVs": "FGF_CreatureTraining",
    "FCreaturePP": "FGF_SkillUses",
    "FCreatureMoveData": "FGF_SkillEntry",
    "FCreatureLevelUpMoves": "FGF_LevelUpSkills",
    "FCreatureNatureMods": "FGF_TemperamentMods",
    "Magic_IV": "Magic_Potential", "Poise_IV": "Poise_Potential",
    "Magic_EV": "Magic_Training", "Poise_EV": "Poise_Training",
    "GetVaultCreatureMovesWithPP": "GetVaultCreatureSkillsWithUses",
    "GetPartyCreatureMovesWithPP": "GetPartyCreatureSkillsWithUses",
    "GetVaultCreatureMoves": "GetVaultCreatureSkills",
    "GetPartyCreatureLoadedMoves": "GetPartyCreatureLoadedSkills",
    "TeachMoveToPartyCreature": "TeachSkillToPartyCreature",
    "SwapPartyCreatureMoveSlots": "SwapPartyCreatureSkillSlots",
    "CreateCreatureWithMoves": "CreateCreatureWithSkills",
    "Creature_DecideOnNewMove": "Creature_DecideOnNewSkill",
    "FPokemonIVs": "FGF_CreaturePotential",
    "HPIV": "HP_Potential", "ATKIV": "Attack_Potential", "DEFIV": "Defense_Potential",
    "SPATKIV": "Magic_Potential", "SPDEFIV": "Poise_Potential", "SPDIV": "Speed_Potential",
    "HP_IV": "HP_Potential", "Attack_IV": "Attack_Potential",
    "Defense_IV": "Defense_Potential", "Speed_IV": "Speed_Potential",
    "SpecialAttack_IV": "Magic_Potential", "SpecialDefense_IV": "Poise_Potential",
    "SpAtk_IV": "Magic_Potential", "SpDef_IV": "Poise_Potential",
    "IV_HP": "Potential_HP", "IV_Attack": "Potential_Attack",
    "IV_Defense": "Potential_Defense", "IV_Speed": "Potential_Speed",
    "IV_SpAtk": "Potential_Magic", "IV_SpDef": "Potential_Poise",
    "IV": "Potential", "IVs": "Potentials", "IVS": "POTENTIALS",
    "CreateIVs": "CreatePotentials", "SetIVs": "SetPotentials",
    "GetIVByIndex": "GetPotentialByIndex", "InheritIVs": "InheritPotentials",
    "MotherIVs": "MotherPotentials", "FatherIVs": "FatherPotentials",
    "MaxIV": "MaxPotential", "MaxIVValue": "MaxPotentialValue",
    "bPresetIVs": "bPresetPotentials", "bShowIVs": "bShowPotentials",
    "bIsIV": "bIsPotential", "IVValue": "PotentialValue", "IVText": "PotentialText",
    "IVSize": "PotentialSize", "IVFillColor": "PotentialFillColor",
    "IVLineColor": "PotentialLineColor",

    # -- EVs become Training --------------------------------------------------
    "FPokemonEVs": "FGF_CreatureTraining",
    "HPEV": "HP_Training", "ATKEV": "Attack_Training", "DEFEV": "Defense_Training",
    "SPATKEV": "Magic_Training", "SPDEFEV": "Poise_Training", "SPDEV": "Speed_Training",
    "HP_EV": "HP_Training", "Attack_EV": "Attack_Training",
    "Defense_EV": "Defense_Training", "Speed_EV": "Speed_Training",
    "SpecialAttack_EV": "Magic_Training", "SpecialDefense_EV": "Poise_Training",
    "EV": "TrainingValue", "EVs": "Training",
    "EVBonus": "TrainingBonus", "EVBoostAmount": "TrainingBoostAmount",
    "EVMember": "TrainingMember", "GetEVMemberForVitamin": "GetTrainingMemberForVitamin",
    "MaxEVPerStat": "MaxTrainingPerStat", "MaxEVTotal": "MaxTrainingTotal",
    "TotalEV": "TotalTraining", "TotalEVs": "TotalTraining",
    "TotalEVCap": "TotalTrainingCap", "VitaminEVCap": "VitaminTrainingCap",
    "DumpEVsCommand": "DumpTrainingCommand", "DumpPartyEVs": "DumpPartyTraining",

    # -- PP becomes Uses ------------------------------------------------------
    "FPokemonPP": "FGF_SkillUses",
    "PP": "Uses", "CurrentPP": "CurrentUses", "MaxPP": "MaxUses",
    "NewMaxPP": "NewMaxUses", "MovePP": "SkillUses", "SlotPP": "SlotUses",
    "actorPP": "actorUses", "PPLeft": "UsesLeft", "PPState": "UsesState",
    "OutOfPP": "OutOfUses", "bHasPP": "bHasUses",
    "OutCurrentPP": "OutCurrentUses", "OutMaxPP": "OutMaxUses",
    "OldCurrentPPNum": "OldCurrentUsesNum",
    "DummyCurrentPP": "DummyCurrentUses", "DummyMaxPP": "DummyMaxUses",
    "bNeedsCurrentPP": "bNeedsCurrentUses", "bNeedsMaxPP": "bNeedsMaxUses",
    "NormalizePP": "NormalizeUses", "RestoreMovePP": "RestoreSkillUses",
    "PPRestore": "UsesRestore", "PPRestoreAmount": "UsesRestoreAmount",
    "PPToRestore": "UsesToRestore",
    "MovesWithPP": "SkillsWithUses",
    "GetBoxPokemonMovesWithPP": "GetVaultCreatureSkillsWithUses",
    "GetPartyPokemonMovesWithPP": "GetPartyCreatureSkillsWithUses",
    "PushPPToBattleActors": "PushUsesToBattleActors",

    # -- Move becomes Skill: ALLOW-LIST ONLY ----------------------------------
    # Anything containing "Move" and absent from this table stays "Move",
    # because it means movement.
    "AAttackMaster": "AGF_SkillDefinition",
    "AttackMaster": "GF_SkillDefinition",
    "Move": "Skill", "Moves": "Skills", "NewMove": "NewSkill", "NewMoves": "NewSkills",
    "MoveClass": "SkillClass", "MoveType": "SkillElement", "MoveCDO": "SkillCDO",
    "MoveIndex": "SkillIndex", "MoveName": "SkillName", "MoveData": "SkillData",
    "MoveCount": "SkillCount", "MoveDefaults": "SkillDefaults",
    "MoveOptions": "SkillOptions", "MoveUsed": "SkillUsed", "MoveDuration": "SkillDuration",
    "MoveInQueue": "SkillInQueue", "MoveEvaluation": "SkillEvaluation",
    "MoveLearn": "SkillLearn", "MoveSlot": "SkillSlot", "MoveScore": "SkillScore",
    "MoveWrapper": "SkillWrapper", "MoveParent": "SkillParent",
    "MoveParentIndex": "SkillParentIndex", "MoveVarNames": "SkillVarNames",
    "MoveComboBoxes": "SkillComboBoxes", "MoveOverrideRow": "SkillOverrideRow",
    "MoveDisplayInfo": "SkillDisplayInfo", "MoveTurnAction": "SkillTurnAction",
    "MoveUnusableReason": "SkillUnusableReason",
    "FMoveOption": "FGF_SkillOption", "FMoveEvaluation": "FGF_SkillEvaluation",
    "FMoveDisplayInfo": "FGF_SkillDisplayInfo", "FLearnableMove": "FGF_LearnableSkill",
    "FMoveLearnQueueEntry": "FGF_SkillLearnQueueEntry",
    "FPokemonMoveData": "FGF_SkillEntry", "FPokemonLevelUpMoves": "FGF_LevelUpSkills",
    "FDrainingMoveHealResult": "FGF_DrainingSkillHealResult",
    "EMoveTurnAction": "EGF_SkillTurnAction", "EMoveUnusableReason": "EGF_SkillUnusableReason",
    "ExecuteMove": "ExecuteSkill", "UseMove": "UseSkill", "LearnMove": "LearnSkill",
    "ForgetMove": "ForgetSkill", "TeachMove": "TeachSkill", "KnowsMove": "KnowsSkill",
    "SetMoveSlot": "SetSkillSlot", "GetMoves": "GetSkills", "OutMove": "OutSkill",
    "OutMoves": "OutSkills", "BestMove": "BestSkill", "AllMoves": "AllSkills",
    "TempMove": "TempSkill", "TargetMove": "TargetSkill", "ValidMoves": "ValidSkills",
    "SortedMoves": "SortedSkills", "EmptyMoves": "EmptySkills",
    "LoadedMove": "LoadedSkill", "LoadedMoves": "LoadedSkills",
    "SelectedMove": "SelectedSkill", "SelectedMoves": "SelectedSkills",
    "SelectedWildMoves": "SelectedWildSkills", "ForcedMoves": "ForcedSkills",
    "StartingMoves": "StartingSkills", "LearnableMove": "LearnableSkill",
    "LearnableMoves": "LearnableSkills", "LearnableTMMoves": "LearnableTomeSkills",
    "EvolutionMoves": "EvolutionSkills", "EggMoves": "EggSkills",
    "PushEggMove": "PushEggSkill", "BuildEggMoveset": "BuildEggSkillset",
    "MovesToLearn": "SkillsToLearn", "LearnedMove": "LearnedSkill",
    "ExistingMove": "ExistingSkill", "AttackingMove": "AttackingSkill",
    "SpawnedMove": "SpawnedSkill", "SpawnMoveActor": "SpawnSkillActor",
    "SpawnMoveActorAt": "SpawnSkillActorAt", "SpawnMoveForBattle": "SpawnSkillForBattle",
    "CleanupMove": "CleanupSkill", "CleanupMoveActor": "CleanupSkillActor",
    "ResolveMoveClass": "ResolveSkillClass", "ResolveMoveLearn": "ResolveSkillLearn",
    "GetMoveCDO": "GetSkillCDO", "GetMoveDuration": "GetSkillDuration",
    "GetMoveDurationFromClass": "GetSkillDurationFromClass",
    "GetMoveDisplayInfo": "GetSkillDisplayInfo", "GetBestMove": "GetBestSkill",
    "GetMovesToLearnAtLevel": "GetSkillsToLearnAtLevel",
    "GetAvailableLevelUpMoves": "GetAvailableLevelUpSkills",
    "GetAvailableTMMoves": "GetAvailableTomeSkills",
    "GetAllLevelUpMoves": "GetAllLevelUpSkills",
    "GetEarliestLearnableMoves": "GetEarliestLearnableSkills",
    "GetFirstUsableMoveIndex": "GetFirstUsableSkillIndex",
    "GetBoxPokemonMoves": "GetVaultCreatureSkills",
    "GetPartyPokemonLoadedMoves": "GetPartyCreatureLoadedSkills",
    "IsMoveUsable": "IsSkillUsable", "HasAnyUsableMove": "HasAnyUsableSkill",
    "IsMoveExcludedFromPool": "IsSkillExcludedFromPool",
    "bExcludeFromMovePool": "bExcludeFromSkillPool",
    "EvaluateAllMoves": "EvaluateAllSkills", "CalculateMoveScore": "CalculateSkillScore",
    "CheckMoveHit": "CheckSkillHit", "CheckMoveHitActor": "CheckSkillHitActor",
    "CheckLevelUpMoves": "CheckLevelUpSkills", "CheckPartyLevelUpMoves": "CheckPartyLevelUpSkills",
    "CheckEvolutionMoves": "CheckEvolutionSkills",
    "TryLearnLevelUpMoves": "TryLearnLevelUpSkills",
    "CanLearnMoreMoves": "CanLearnMoreSkills",
    "TeachMoveToPartyPokemon": "TeachSkillToPartyCreature",
    "SwapPartyPokemonMoveSlots": "SwapPartyCreatureSkillSlots",
    "CreatePokemonWithMoves": "CreateCreatureWithSkills",
    "ApplyMoves": "ApplySkills", "MaxMoves": "MaxSkills", "NoMoveInSlot": "NoSkillInSlot",
    "OutMoveSlotIndex": "OutSkillSlotIndex", "OutMoveParentIndex": "OutSkillParentIndex",
    "OutLearnedMoves": "OutLearnedSkills", "OutForgottenMoves": "OutForgottenSkills",
    "PendingMoveToLearn": "PendingSkillToLearn",
    "PendingMoveLearnQueue": "PendingSkillLearnQueue",
    "PendingMoveLearnPartyIndex": "PendingSkillLearnPartyIndex",
    "GetPendingMoveLearnPartyIndex": "GetPendingSkillLearnPartyIndex",
    "HasPendingMoveLearn": "HasPendingSkillLearn",
    "ProcessNextMoveLearn": "ProcessNextSkillLearn",
    "MoveLearnDeferElapsed": "SkillLearnDeferElapsed",
    "MoveLearnQueueEmptyDeferHandle": "SkillLearnQueueEmptyDeferHandle",
    "AnnounceMoveLearnQueueEmptyWhenIdle": "AnnounceSkillLearnQueueEmptyWhenIdle",
    "bMoveLearnQueueEmptyAnnounced": "bSkillLearnQueueEmptyAnnounced",
    "bMoveLearnedNotificationPending": "bSkillLearnedNotificationPending",
    "bMoveLearnedThisDrain": "bSkillLearnedThisDrain",
    "ConsumeMoveLearnedNotification": "ConsumeSkillLearnedNotification",
    "OnMoveLearned": "OnSkillLearned", "OnMoveChanged": "OnSkillChanged",
    "OnWildMoveChanged": "OnWildSkillChanged",
    "OnMoveLearnRequired": "OnSkillLearnRequired",
    "OnMoveLearnQueueEmpty": "OnSkillLearnQueueEmpty",
    "FOnMoveLearned": "FGF_OnSkillLearned",
    "FOnMoveLearnRequired": "FGF_OnSkillLearnRequired",
    "FOnMoveLearnQueueEmpty": "FGF_OnSkillLearnQueueEmpty",
    "MakeMoveWidget": "MakeSkillWidget", "UseSelectedMove": "UseSelectedSkill",
    "LastSelectedMove": "LastSelectedSkill", "LastMoveSelected": "LastSkillSelected",
    "PartyMoveClass": "PartySkillClass", "TeachableMove": "TeachableSkill",
    "TMMove": "TomeSkill", "ParentMove": "ParentSkill",
    "bReplaceMove": "bReplaceSkill", "bAllMoves": "bAllSkills",
    "bHasQueuedMove": "bHasQueuedSkill", "bHasAnyMoveOverride": "bHasAnySkillOverride",
    "bHasAtLeastNeutralMove": "bHasAtLeastNeutralSkill",
    "bLocksMove": "bLocksSkill", "bPreventsStatusMoves": "bPreventsStatusSkills",
    "bIsProtectMove": "bIsProtectSkill", "bIsSoundMove": "bIsSoundSkill",
    "bIsTrappingMove": "bIsTrappingSkill", "bIsDrainingMove": "bIsDrainingSkill",
    "bIsSelfHealingMove": "bIsSelfHealingSkill", "bIsSelfKOMove": "bIsSelfKOSkill",
    "IsSelfKOMove": "IsSelfKOSkill", "bIsEscapeMove": "bIsEscapeSkill",
    "IsEscapeMove": "IsEscapeSkill", "CanEscapeWithMove": "CanEscapeWithSkill",
    "bIsTwoTurnMove": "bIsTwoTurnSkill", "bIsChargingTwoTurnMove": "bIsChargingTwoTurnSkill",
    "PendingTwoTurnMove": "PendingTwoTurnSkill", "bIsBideMove": "bIsBideSkill",
    "isLoopingMove": "isLoopingSkill", "StatusMoveChance": "StatusSkillChance",
    "CanStatusMoveAffect": "CanStatusSkillAffect",
    "DoesAbilityBlockMove": "DoesTraitBlockSkill",
    "DoesDampBlockMove": "DoesDampBlockSkill", "DoesOHKOMoveFail": "DoesOHKOSkillFail",
    "IsTargetImmuneToMoveType": "IsTargetImmuneToSkillElement",
    "CanMoveCauseFlinch": "CanSkillCauseFlinch",
    "CanMoveClassCauseFlinch": "CanSkillClassCauseFlinch",
    "ProcessMoveFlinch": "ProcessSkillFlinch",
    "GetFlinchChanceFromMoveClass": "GetFlinchChanceFromSkillClass",
    "ApplyDrainingMoveHealing": "ApplyDrainingSkillHealing",
    "VerifyMoveBelongsToAttacker": "VerifySkillBelongsToAttacker",
    "LoadMoveAssets": "LoadSkillAssets",
    "DebugWildMove1": "DebugWildSkill1", "DebugWildMove2": "DebugWildSkill2",
    "DebugWildMove3": "DebugWildSkill3", "DebugWildMove4": "DebugWildSkill4",
    "WildMoveComboBoxes": "WildSkillComboBoxes",
    "Pokemon_DecideOnNewMove": "Creature_DecideOnNewSkill",

    # -- vault (was PC / box) --------------------------------------------------
    # These run AFTER the PLAIN pass, so they match post-rename spellings.
    # A blanket Box -> Vault rule is impossible: UBoxComponent, SVerticalBox,
    # SComboBox, SCheckBox and ECheckBoxState are all engine types.
    "FPCCursorState": "FGF_VaultCursorState",
    "EPCCursorPanel": "EGF_VaultCursorPanel",
    "EPCCursorSource": "EGF_VaultCursorSource",
    "MovePartyToBox": "MovePartyToVault", "MoveBoxToParty": "MoveVaultToParty",
    "GetBoxSystem": "GetVaultSystem", "BoxSystem": "VaultSystem",
    "LoadedBoxSystem": "LoadedVaultSystem", "NewBoxSystem": "NewVaultSystem",
    "SyncToBoxSystem": "SyncToVaultSystem",
    # Bare "Box" is nearly always the system pointer (UGF_VaultSystem* Box),
    # not a page, so it maps to Vault rather than VaultPage.
    "Box": "Vault", "Boxes": "VaultPages",
    "BoxA": "VaultPageA", "BoxB": "VaultPageB",
    "BoxIdx": "VaultPageIdx", "BoxIndex": "VaultPageIndex",
    "BoxName": "VaultPageName", "BoxNames": "VaultPageNames",
    "BoxSlot": "VaultSlot", "BoxSlots": "VaultSlots",
    "BoxRows": "VaultRows", "BoxCols": "VaultCols",
    "MaxBoxes": "MaxVaultPages", "NumBoxes": "NumVaultPages",
    "MaxBoxSize": "MaxVaultPageSize", "InitializeBoxes": "InitializeVaultPages",
    "CurrentBoxIndex": "CurrentVaultPageIndex", "NewBox": "NewVaultPage",
    "NextBox": "NextVaultPage", "PreviousBox": "PreviousVaultPage",
    "DestBox": "DestVaultPage", "SourceBox": "SourceVaultPage",
    "AddToBox": "AddToVault", "DropToBox": "DropToVault",
    "GrabFromBox": "GrabFromVault", "RemoveFromBox": "RemoveFromVault",
    "GetBoxCount": "GetVaultPageCount", "GetBoxName": "GetVaultPageName",
    "SetBoxName": "SetVaultPageName", "SetCurrentBox": "SetCurrentVaultPage",
    "GetCurrentBoxIndex": "GetCurrentVaultPageIndex",
    "GetCurrentBoxName": "GetCurrentVaultPageName",
    "IsValidBoxIndex": "IsValidVaultPageIndex", "IsValidBoxSlot": "IsValidVaultSlot",
    "SwapPartyWithBox": "SwapPartyWithVault",
    "OnBoxChanged": "OnVaultChanged", "OnBoxSwitched": "OnVaultSwitched",
    "FGF_OnBoxChanged": "FGF_OnVaultChanged",
    "bFromBox": "bFromVault", "bSendToBox": "bSendToVault",
    "bOutgoingFromBox": "bOutgoingFromVault",
    "OutBoxIndex": "OutVaultPageIndex", "NewBoxIndex": "NewVaultPageIndex",
    "OutgoingBoxIndex": "OutgoingVaultPageIndex",
    "ReceivedBoxIndex": "ReceivedVaultPageIndex",
    "UnusedBoxIndex": "UnusedVaultPageIndex",
    "LastCaughtBoxIndex": "LastCaughtVaultPageIndex",
    "GetLastCaughtBoxIndex": "GetLastCaughtVaultPageIndex",
    "GetLastCaughtBoxName": "GetLastCaughtVaultPageName",
    "GetGiftBoxIndex": "GetGiftVaultPageIndex",
    "BoxCreature": "VaultCreature", "GetBoxCreature": "GetVaultCreature",
    "GetBoxCreatureCount": "GetVaultCreatureCount",
    "GetBoxCreatureData": "GetVaultCreatureData",
    "GetBoxCreatureMoves": "GetVaultCreatureSkills",
    "UpdateBoxCreature": "UpdateVaultCreature",
    "OfferBoxCreature": "OfferVaultCreature",
    "EvolveBoxCreature": "EvolveVaultCreature",
    "SwapCreatureInBox": "SwapCreatureInVault",
    "SwapCreatureBetweenBoxes": "SwapCreatureBetweenVaultPages",
    "GetAllCreatureInBox": "GetAllCreaturesInVaultPage",
    "TransferBoxCreatureToNewCore": "TransferVaultCreatureToNewCore",
    # PC screen -> vault screen. Bare "PC" is deliberately NOT mapped: it appears
    # in prose comments, and in UINavPC / UINavPCComponent.
    "PCController": "VaultController", "PCScreen": "VaultScreen",
    "GetPCController": "GetVaultController", "InPCController": "InVaultController",
    "ClosePCScreen": "CloseVaultScreen", "InitializePCScreen": "InitializeVaultScreen",
    "OnPCBoxChanged": "OnVaultPageChanged",
    "OnPCCreatureDropped": "OnVaultCreatureDropped",
    "OnPCCreatureGrabbed": "OnVaultCreatureGrabbed",

    # -- misc ------------------------------------------------------------------
    "EPokedexEntryState": "EGF_CompendiumEntryState",
    "EVitaminStat": "EGF_VitaminStat",
    "MyClass": "GF_Unused",
}

from decouple_patches import PATCHES as DECOUPLE  # noqa: E402
from decouple_patches import OPTIONAL_PATCHES  # noqa: E402

DECOUPLE_APPLIED = set()


# ── Engine words that must never be touched ───────────────────────────────────
# Applied as a protective pass: these are stashed before the rename and restored
# after, so a rule can never reach inside them.
PROTECTED = [
    "MoveTemp", "MoveTempIfPossible", "RemoveAt", "RemoveAll", "RemoveSingle",
    "Remove", "MoveIgnoreActors", "AddMovementInput", "GetCharacterMovement",
    "UCharacterMovementComponent", "CharacterMovementComponent",
    "EVisibility", "ESlateVisibility", "EVerticalAlignment",
    "PPF_DeepComparison", "PPF_None", "LOCALAPPDATA", "UE_BUILD_SHIPPING",
    "SSplitter", "SplitIntoSections", "ParseIntoArray",
]


# Multi-token engine expressions a rename would otherwise break. PROTECTED
# matches whole words only, which cannot express "the Move that belongs to
# IFileManager" -- and IFileManager::Get().Move() is a real call in the save
# vault that Move -> Skill happily renamed into oblivion.
PROTECTED_LITERALS = [
    "IFileManager::Get().Move",
    "IFileManager::Get().Delete",
    # Box -> Vault renamed the Slate nine-slice draw mode into oblivion.
    "ESlateBrushDrawType::Box",
    "ESlateBrushDrawType::Image",
]


def build_protection(text):
    for i, lit in enumerate(PROTECTED_LITERALS):
        text = text.replace(lit, f"\x00L{i}\x00")

    """Swap engine words for sentinels so no rule can touch them."""
    for i, word in enumerate(sorted(PROTECTED, key=len, reverse=True)):
        text = re.sub(r"\b" + re.escape(word) + r"\b", f"\x00P{i}\x00", text)
    return text


def restore_protection(text):
    for i, lit in enumerate(PROTECTED_LITERALS):
        text = text.replace(f"\x00L{i}\x00", lit)

    for i, word in enumerate(sorted(PROTECTED, key=len, reverse=True)):
        text = text.replace(f"\x00P{i}\x00", word)
    return text


# ── GF_ prefixing ─────────────────────────────────────────────────────────────

# Only DEFINITIONS, never forward declarations.
#
# This distinction is load-bearing. Headers here forward-declare a pile of engine
# types -- class UPaperSprite; class UTexture2D; class UNiagaraSystem; -- and an
# earlier version of this regex swept those into the GF_ prefix pass, producing
# UGF_PaperSprite and a codebase that referenced engine classes that do not
# exist. Requiring a following ':' or '{' keeps forward declarations out.
TYPE_DECL_RE = re.compile(
    r"^\s*(?:class|struct)\s+(?:[A-Z_]+_API\s+)?([UAFIS][A-Za-z0-9_]+)\s*(?=[:{]|\s*\n\s*[:{])"
    r"|^\s*enum\s+class\s+([EU][A-Za-z0-9_]+)\s*(?=[:{])",
    re.MULTILINE,
)
DELEGATE_DECL_RE = re.compile(r"DECLARE_[A-Z_]*DELEGATE[A-Za-z_]*\s*\(\s*([FA-Za-z0-9_]+)")


def collect_type_names(all_text):
    """Every type this codebase declares, so GF_ can be inserted into each."""
    names = set()
    for m in TYPE_DECL_RE.finditer(all_text):
        for g in m.groups():
            if g:
                names.add(g)
    for m in DELEGATE_DECL_RE.finditer(all_text):
        names.add(m.group(1))
    # Types that already carry the prefix, or that we renamed by hand above.
    return {n for n in names if "GF_" not in n and "GE_" not in n}


def gf_prefix(name):
    """UCreatureManagerSubsystem -> UGF_CreatureManagerSubsystem"""
    if len(name) > 1 and name[0] in "UAFISE" and name[1].isupper():
        return name[0] + "GF_" + name[1:]
    return "GF_" + name


# ── The rename ────────────────────────────────────────────────────────────────

def apply_renames(text, module, type_names):
    text = build_protection(text)

    # Before anything else: the PLAIN pass contains POKEMON -> CREATURE, which
    # would otherwise turn POKEMONEMERALD_API into CREATUREEMERALD_API and leave
    # every exported class annotated with an undefined macro.
    text = text.replace("POKEMONEMERALD_API", API_MACRO[module])

    for old, new in PLAIN:
        text = text.replace(old, new)

    for pattern, repl in GUARDED:
        text = re.sub(pattern, repl, text)

    # Exact identifiers, longest first so MoveClass wins over Move.
    for old in sorted(IDENT, key=len, reverse=True):
        text = re.sub(r"\b" + re.escape(old) + r"\b", IDENT[old], text)

    # GF_ prefix on every declared type.
    for old in sorted(type_names, key=len, reverse=True):
        renamed = apply_simple(old)
        if "GF_" in renamed:
            continue
        text = re.sub(r"\b" + re.escape(renamed) + r"\b", gf_prefix(renamed), text)

    text = text.replace("POKEMONEMERALD_API", API_MACRO[module])
    text = text.replace("GAMMAFRAMEWORK_API", API_MACRO[module])
    text = restore_protection(text)
    return text


# ── Stage 4: element collapse ─────────────────────────────────────────────────
# Gamma Emerald declared the same 18-value type list eight times. The PLAIN pass
# renames all eight to EGF_Element, which leaves eight duplicate *definitions*.
# Strip every one; the canonical enum is the hand-written GF_ElementTypes.h.

ENUM_BLOCK_RE = re.compile(
    r"(?:UENUM\s*\([^)]*\)\s*)?enum\s+class\s+(EGF_Element|EGF_SkillCategory|EGF_CreatureTemperament|EGF_WeatherType)\s*:\s*uint8\s*\{[^}]*\};\s*",
    re.MULTILINE,
)

# The 18 old values mapped onto the 17-element Elemental Arcana roster.
# Only Ghost and Dark still merge (both -> Umbra); everything else is 1:1.
#
# Note Ground and Rock are NOT interchangeable any more: Terra is soil (grounds
# Spark, cannot touch Gale), Stone is hard mineral (knocks Gale down). Mapping
# Rock to Terra would silently invert both of those matchups.
ELEMENT_VALUE_MAP = {
    "Normal": "Neutral",  "Fighting": "Sinew",   "Fire": "Ember",
    "Water": "Tide",      "Grass": "Verdant",    "Bug": "Chitin",
    "Flying": "Gale",     "Ground": "Terra",     "Rock": "Stone",
    "Electric": "Spark",  "Steel": "Ferrous",    "Ice": "Frost",
    "Poison": "Venom",    "Ghost": "Umbra",      "Dark": "Umbra",
    "Fairy": "Lumen",     "Psychic": "Aether",   "Dragon": "Wyrm",
}


def collapse_elements(text, is_header):
    """Remove duplicate enum definitions and repoint old element values."""
    had_enum = bool(ENUM_BLOCK_RE.search(text))
    text = ENUM_BLOCK_RE.sub("", text)

    def fix_value(m):
        return "EGF_Element::" + ELEMENT_VALUE_MAP.get(m.group(1), m.group(1))

    text = re.sub(r"EGF_Element::(\w+)", fix_value, text)
    text = text.replace("EGF_SkillCategory::Special", "EGF_SkillCategory::Magic")

    needs = had_enum or "EGF_Element" in text or "EGF_SkillCategory" in text
    if needs and '#include "GF_ElementTypes.h"' not in text:
        # .generated.h must stay the last include, so insert before it.
        gen = re.search(r'^#include\s+"[^"]*\.generated\.h"', text, re.MULTILINE)
        if gen:
            text = text[: gen.start()] + '#include "GF_ElementTypes.h"\n' + text[gen.start():]
        else:
            first = re.search(r'^#include\s+"[^"]+"', text, re.MULTILINE)
            if first:
                text = text[: first.end()] + '\n#include "GF_ElementTypes.h"' + text[first.end():]
    return text


# ── Stage 5: functions the element collapse invalidates ───────────────────────
# Folding 18 values into 13 merges pairs (Ground+Rock -> Stone), which turns every
# 18-way switch over elements into duplicate case labels -- a hard compile error.
# Each such function is replaced outright by a call into UGF_ElementLibrary.

FUNCTION_BODIES = {
    ("GF_BattleManagementFunctions.cpp", "GetSingleTypeMatchup"):
        "\n    return UGF_ElementLibrary::GetMatchup(AttackType, DefenseType);\n",
    ("GF_BattleManagementFunctions.cpp", "GetTypeEffectiveness"):
        "\n    return UGF_ElementLibrary::GetEffectiveness(SkillElement, DefenderType1, DefenderType2);\n",
    ("GF_TamerMaster.cpp", "GetSingleTypeEffectiveness"):
        "\n    return UGF_ElementLibrary::GetMatchup(AttackType, DefenseType);\n",
    ("GF_TamerMaster.cpp", "GetTypeEffectiveness"):
        "\n    return UGF_ElementLibrary::GetEffectiveness(SkillElement, DefenderType1, DefenderType2);\n",
    ("GF_BattleDebuggerPage.cpp", "TypeToColor"):
        "\n    return UGF_ElementLibrary::GetElementColor(T);\n",
    ("GF_GiveCreatureDebuggerPage.cpp", "TypeToColor"):
        "\n    return UGF_ElementLibrary::GetElementColor(T);\n",
}


def replace_function_body(text, func_name, new_body):
    """Swap one function's body, matching braces so nested blocks are safe."""
    m = re.search(r"^[^\n]*\b" + re.escape(func_name) + r"\s*\([^;{]*\)\s*(?:const\s*)?\{",
                  text, re.MULTILINE)
    if not m:
        return text, False
    open_idx = text.index("{", m.start())
    depth = 0
    for i in range(open_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[: open_idx + 1] + new_body + text[i:], True
    return text, False


def rewrite_collapsed_functions(text, filename):
    for (fname, func), body in FUNCTION_BODIES.items():
        if filename == fname:
            text, ok = replace_function_body(text, func, body)
            if not ok:
                print(f"  !! could not rewrite {func} in {fname}")

    # The egg-appearance switch in the creature library has the same duplicate
    # label problem, but it is inline rather than a whole function.
    if filename == "GF_CreatureBlueprintLibrary.cpp":
        text = re.sub(
            r"const TCHAR\* TypeToken = TEXT\(\"000\"\);\s*switch\s*\(Appearance\)\s*\{[^}]*\}",
            "const FString TypeTokenStr = UGF_ElementLibrary::GetElementAssetToken(Appearance);\n"
            "\tconst TCHAR* TypeToken = *TypeTokenStr;",
            text,
        )

    # The two element enums are now one type, so every Attack<->Creature type
    # converter is an identity function.
    #
    # Definitions and call sites are told apart by PARAMETER SHAPE, not by the
    # surrounding text. A definition always takes a typed parameter:
    #     static EGF_Element ConvertXxx(EGF_Element AttackType)
    # while a call site always passes an expression:
    #     ConvertXxx(SkillCDO->Type)
    # Trying to match "...(anything) {" instead lets the regex backtrack across
    # a line break and swallow an unrelated block -- which is how the
    # CanStatusSkillAffect body got deleted twice.
    DEF_HEAD = r"^[^\n]*ELEMENT_CONVERTER_DELETED\s*\(\s*EGF_Element\s+\w+\s*\)\s*(?:const\s*)?\{"
    DECL     = r"^[^\n]*ELEMENT_CONVERTER_DELETED\s*\(\s*EGF_Element\s+\w+\s*\)\s*;[ \t]*\r?\n"

    while True:
        m = re.search(DEF_HEAD, text, re.MULTILINE)
        if not m:
            break
        open_idx = text.index("{", m.start())
        depth, end = 0, None
        for i in range(open_idx, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break
        if end is None:
            break
        text = text[: m.start()] + text[end:]

    text = re.sub(DECL, "", text, flags=re.MULTILINE)

    # Whatever is left is a call site: drop the name, keep the parentheses.
    text = re.sub(r"(?:UTypeConverterHelper::)?(?:Convert)?ELEMENT_CONVERTER_DELETED\s*\(",
                  "(", text)
    text = re.sub(r'^\s*#include\s+"[^"]*TypeConverterHelper\.h"\s*\n', "", text,
                  flags=re.MULTILINE)
    return text


def apply_decouple(text, relpath):
    """Break the few Core -> Creatures references the rename cannot fix."""
    rel = relpath.replace(chr(92), '/')
    for idx, (target, old, new) in enumerate(list(DECOUPLE) + list(OPTIONAL_PATCHES)):
        if not rel.endswith(target):
            continue
        # A patch may be a literal, or a regex when the block is long enough
        # that transcribing its exact whitespace by hand is not reliable.
        if old.startswith('RE:'):
            pattern = old[3:]
            if re.search(pattern, text, re.DOTALL):
                text = re.sub(pattern, new.replace(chr(92), chr(92)*2), text, flags=re.DOTALL)
                DECOUPLE_APPLIED.add(idx)
        elif old in text:
            text = text.replace(old, new)
            DECOUPLE_APPLIED.add(idx)
    return text

# -- Stage 6: enum VALUE renames -----------------------------------------------
from value_renames import TEMPERAMENTS, TRAITS, display_name  # noqa: E402
from value_renames import rename_in_prose  # noqa: E402
from enum_value_renames import rename_enum_value_labels  # noqa: E402
from item_name_renames import rename_item_names  # noqa: E402
from example_cast import rename_examples  # noqa: E402

VALUE_MAPS = (("EGF_Temperament", TEMPERAMENTS), ("EGF_CreatureTrait", TRAITS))

# Files where trait/temperament names appear as user-facing string literals.
VALUE_STRING_FILES = {
    "GF_Creature.cpp", "GF_Creature.h",
    "GF_CreatureTraits.cpp", "GF_CreatureTraits.h",
    "GF_CreatureMemoLibrary.cpp", "GF_CreatureMemoLibrary.h",
    "GF_CreatureSpeciesData.h",
}


def _rewrite_enum_body(text, enum_name, mapping):
    """Rename the members inside one enum definition, and nowhere else."""
    m = re.search(r"enum class " + enum_name + r"\s*:\s*uint8\s*\{", text)
    if not m:
        return text
    start = text.index("{", m.start())
    depth = 0
    end = None
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    if end is None:
        return text
    body = text[start:end]
    for old, new in mapping.items():
        body = re.sub(r"\b" + old + r"\b", new, body)
        body = re.sub(r'"' + display_name(old) + r'"', '"' + display_name(new) + '"', body)
        body = re.sub(r'"' + old + r'"', '"' + display_name(new) + '"', body)
    return text[:start] + body + text[end:]


def rename_enum_values(text, filename=""):
    """
    Scoped rename of temperament and trait values.

    Scoped on purpose: several old value names -- Static, Trace, Pickup, Damp --
    are ordinary tokens in an Unreal codebase. A bare word-boundary replacement
    turns LineTraceSingleByChannel's neighbours and the item pickup code into
    nonsense. Only the enum body and EnumName::Value call sites are touched.
    """
    for enum_name, mapping in VALUE_MAPS:
        text = _rewrite_enum_body(text, enum_name, mapping)
        for old, new in mapping.items():
            text = re.sub(r"\b" + enum_name + r"::" + old + r"\b",
                          enum_name + "::" + new, text)

    # The enum identifiers are only half the job. Trait and temperament display
    # names also live as STRING literals -- an NSLOCTEXT table in the trait
    # library, a modifier table in GF_Creature.cpp -- and those are what the
    # player actually reads. Renaming the enum but not these leaves the UI
    # cheerfully printing "Water Veil" for Dewshield, which defeats the point of
    # the whole exercise.
    #
    # Restricted to the files that hold display names, because several old value
    # names are ordinary string literals elsewhere -- "Bold" in particular is the
    # rich-text style tag used by the dialogue decorator and every debugger page.
    if filename in VALUE_STRING_FILES:
        for _, mapping in VALUE_MAPS:
            for old, new in mapping.items():
                text = text.replace('"' + display_name(old) + '"',
                                    '"' + display_name(new) + '"')
                text = text.replace('"' + old + '"', '"' + new + '"')
    return text


def apply_simple(name):
    """The rename pipeline for a bare identifier (used for type names/filenames)."""
    for old, new in PLAIN:
        name = name.replace(old, new)
    for pattern, repl in GUARDED:
        name = re.sub(pattern, repl, name)
    for old in sorted(IDENT, key=len, reverse=True):
        name = re.sub(r"\b" + re.escape(old) + r"\b", IDENT[old], name)
    return name


# Filenames the generic pipeline gets wrong or leaves in the original's sloppy
# casing (Catchinglibrary, Itemdatamanager, Helditembattlehelper...).
FILE_RENAME = {
    "PokemonBoxSystem":                    "GF_VaultSystem",
    "PokemonPCController":                 "GF_VaultController",
    "PokemonPCScreenWidget":               "GF_VaultScreenWidget",
    "PokemonPanelWidgets":                 "GF_CreaturePanelWidgets",
    "PokeballCatchingLibrary":             "GF_CaptureCoreLibrary",
    "Catchinglibrary":                     "GF_CatchingLibrary",
    "Itemdatamanager":                     "GF_ItemDataManager",
    "Helditembattlehelper":                "GF_HeldItemBattleHelper",
    "Debugmenusubsystem":                  "GF_DebugMenuSubsystem",
    "SGE_DialoguePreviewWidget":           "SGF_DialoguePreviewWidget",
    "PokemonSpeciesDataThumbnailRenderer": "GF_CreatureSpeciesDataThumbnailRenderer",
    "GE_FaintBurstLibrary":                "GF_DownedBurstLibrary",
    "CryEnvelopeWidgetBase":               "GF_CallEnvelopeWidget",
    "AttackMaster":                        "GF_SkillDefinition",
    "Pokemon":                             "GF_Creature",
    # Module implementation files keep the module's own name -- UBT requires it.
    "PokemonEmeraldEditor":                "GammaFrameworkEditor",
    "GESaveGuardModule":                   "GammaFrameworkSaveGuard",
}


def rename_file(basename):
    stem, ext = os.path.splitext(basename)
    if stem in FILE_RENAME:
        return FILE_RENAME[stem] + ext
    stem = apply_simple(stem)
    # A stem that already carries the prefix anywhere (SGF_DialoguePreview...)
    # must not pick up a second one.
    if "GF_" not in stem:
        stem = "GF_" + stem
    return stem + ext


# ── Walk / copy ───────────────────────────────────────────────────────────────

def module_for(relpath):
    parts = relpath.replace("\\", "/").split("/")
    top, rest = parts[0], parts[1:]
    if top == "GESaveGuard":
        return MOD_SAVEGUARD
    if top == "PokemonEmeraldEditor":
        return MOD_EDITOR
    if rest and rest[0] in BATTLE_DIRS:
        return MOD_BATTLE
    if rest and rest[0] in CREATURE_DIRS:
        return MOD_CREATURES
    leaf = rest[-1] if rest else ""
    if leaf.startswith(BATTLE_PREFIXES):
        return MOD_BATTLE
    if leaf.startswith(CREATURE_PREFIXES):
        return MOD_CREATURES
    return MOD_WORLD


def target_subdir(relpath, module):
    """Preserve the Dialogue/ Boot/ Settings/ grouping; flatten module roots."""
    parts = relpath.replace("\\", "/").split("/")
    inner = parts[1:-1]
    if module in (MOD_EDITOR, MOD_SAVEGUARD):
        inner = [p for p in inner if p in ("Public", "Private")]
    # Folder names go through the rename as well, or the code says
    # "Sanctuary/GF_SanctuaryTypes.h" while the directory on disk is
    # still called Daycare.
    inner = [apply_simple(part) for part in inner]
    return os.path.join(*inner) if inner else ""


def main():
    sources = []
    for top in ("PokemonEmerald", "PokemonEmeraldEditor", "GESaveGuard"):
        base = os.path.join(SRC_ROOT, top)
        for dirpath, _, files in os.walk(base):
            for f in files:
                if not f.endswith((".h", ".cpp")):
                    continue
                if f in SKIP_FILES:
                    continue
                full = os.path.join(dirpath, f)
                sources.append((os.path.relpath(full, SRC_ROOT), full))

    # One combined read, to learn every type name before rewriting anything.
    blob = []
    for _, full in sources:
        with open(full, "rb") as fh:
            blob.append(fh.read().decode("utf-8-sig", errors="replace"))
    type_names = collect_type_names("\n".join(blob))
    print(f"discovered {len(type_names)} declared types to GF_-prefix")

    # Clean the module trees, keeping each module's own identity files.
    for mod in (MOD_WORLD, MOD_CREATURES, MOD_BATTLE, MOD_EDITOR, MOD_SAVEGUARD):
        d = os.path.join(DST_ROOT, mod)
        if not os.path.isdir(d):
            continue
        for dirpath, dirnames, files in os.walk(d, topdown=False):
            for f in files:
                if f not in MODULE_OWN_FILES:
                    os.remove(os.path.join(dirpath, f))
            for sub in dirnames:
                p = os.path.join(dirpath, sub)
                if not os.listdir(p):
                    os.rmdir(p)

    # Filename map, so #include lines can be rewritten to match.
    include_map = {}
    plan = []
    for rel, full in sources:
        mod = module_for(rel)
        old_name = os.path.basename(rel)
        new_name = rename_file(old_name)
        include_map[old_name] = new_name
        stem_old, ext = os.path.splitext(old_name)
        stem_new = os.path.splitext(new_name)[0]
        include_map[stem_old + ".generated.h"] = stem_new + ".generated.h"
        # apply_renames runs first and rewrites the include TEXT, so by the time
        # fix_include looks at it the line already says "CreatureInstanceData.h",
        # not "PokemonInstanceData.h". Register that spelling too.
        renamed_stem = apply_simple(stem_old)
        include_map[renamed_stem + ext] = new_name
        include_map[renamed_stem + ".generated.h"] = stem_new + ".generated.h"
        plan.append((rel, full, mod, target_subdir(rel, mod), new_name))

    include_map_ci = {k.lower(): v for k, v in include_map.items()}

    counts = {}
    for rel, full, mod, sub, new_name in plan:
        with open(full, "rb") as fh:
            raw = fh.read()
        had_bom = raw.startswith(b"\xef\xbb\xbf")
        text = raw.decode("utf-8-sig", errors="replace")

        text = apply_renames(text, mod, type_names)
        text = collapse_elements(text, new_name.endswith('.h'))
        text = rewrite_collapsed_functions(text, new_name)
        text = rename_enum_values(text, new_name)
        text = rename_enum_value_labels(text)
        text = rename_item_names(text)
        text = rename_examples(text)
        text = rename_in_prose(text)

        # Rewrite #include targets to the new filenames.
        def fix_include(m):
            path = m.group(1)
            leaf = path.replace("\\", "/").split("/")[-1]
            # Case-insensitive: Gamma Emerald had files named Catchinglibrary.h
            # whose own include line said "CatchingLibrary.generated.h". Windows
            # did not care; an exact-match lookup does.
            hit = include_map.get(leaf) or include_map_ci.get(leaf.lower())
            if hit:
                prefix = path[: len(path) - len(leaf)]
                prefix = prefix.replace("../", "")
                return f'#include "{prefix}{hit}"'
            return m.group(0)

        text = re.sub(r'#include\s+"([^"]+)"', fix_include, text)
        # AFTER fix_include: the decouple patches match on final include
        # spellings, which fix_include is what produces.
        text = apply_decouple(text, os.path.join(sub, new_name))

        outdir = os.path.join(DST_ROOT, mod, sub) if sub else os.path.join(DST_ROOT, mod)
        os.makedirs(outdir, exist_ok=True)
        data = text.encode("utf-8")
        # Source files with non-ASCII need a UTF-8 BOM: this build has no /utf-8
        # flag, so MSVC otherwise reads them as the local codepage.
        if had_bom or any(b > 127 for b in data):
            data = b"\xef\xbb\xbf" + data
        with open(os.path.join(outdir, new_name), "wb") as fh:
            fh.write(data)
        counts[mod] = counts.get(mod, 0) + 1

    for mod in (MOD_WORLD, MOD_CREATURES, MOD_BATTLE, MOD_EDITOR, MOD_SAVEGUARD):
        print(f"  {mod:28s} {counts.get(mod, 0):4d} files")
    print(f"  {'TOTAL':28s} {sum(counts.values()):4d} files")


    # Hand-written docs that ship with the plugin. They describe the dialogue
    # token syntax and the settings screen, so they have to speak the new
    # vocabulary too -- a doc still saying {Box} when the code emits {Vault}
    # is worse than no doc.
    DOCS = [
        (os.path.join(SRC_ROOT, 'PokemonEmerald', 'Dialogue', 'DIALOGUE_SYNTAX.md'),
         'DIALOGUE_SYNTAX.md'),
        (os.path.join(SRC_ROOT, 'PokemonEmerald', 'Settings', 'SETTINGS_SCREEN.md'),
         'SETTINGS_SCREEN.md'),
    ]
    docs_dir = os.path.join(PLUGIN, 'Docs')
    os.makedirs(docs_dir, exist_ok=True)
    for src_doc, out_name in DOCS:
        if not os.path.exists(src_doc):
            print('  !! missing doc: ' + src_doc)
            continue
        text = open(src_doc, 'rb').read().decode('utf-8-sig', errors='replace')
        text = apply_simple(text)
        open(os.path.join(docs_dir, out_name), 'wb').write(text.encode('utf-8'))
    print('  docs regenerated: ' + ', '.join(n for _, n in DOCS))

    missed = [DECOUPLE[i][0] + ": " + DECOUPLE[i][1].strip().split(chr(10))[0][:70]
              for i in range(len(DECOUPLE)) if i not in DECOUPLE_APPLIED]
    if missed:
        print("")
        print("!! decouple patches that did NOT match "
              "(Core may still reference Creatures):")
        for m in missed:
            print("   " + m)

    leftovers = []
    for mod in (MOD_WORLD, MOD_CREATURES, MOD_BATTLE, MOD_EDITOR, MOD_SAVEGUARD):
        for dirpath, _, files in os.walk(os.path.join(DST_ROOT, mod)):
            for f in files:
                if not f.endswith((".h", ".cpp")):
                    continue
                p = os.path.join(dirpath, f)
                t = open(p, "rb").read().decode("utf-8-sig", errors="replace")
                for word in ("Pokemon", "Pokeball", "Shiny", "Trainer", "Daycare",
                             "Pokedex", "PokeRus", "AttackMaster"):
                    if word in t:
                        leftovers.append((os.path.relpath(p, DST_ROOT), word))
    # Display STRINGS are a separate failure mode from identifiers, and a much
    # quieter one: the enum said Dewshield while the UI printed 'Water Veil'
    # for a whole session before anyone noticed. Check both, always.
    ignore_literals = {'Bold'}   # rich-text style tag, not the temperament
    old_labels = set()
    for _, mapping in VALUE_MAPS:
        for old_value in mapping:
            old_labels.add(old_value)
            old_labels.add(display_name(old_value))
    old_labels -= ignore_literals
    string_leftovers = []
    for mod in (MOD_WORLD, MOD_CREATURES, MOD_BATTLE, MOD_EDITOR, MOD_SAVEGUARD):
        for dirpath, _, files in os.walk(os.path.join(DST_ROOT, mod)):
            for f in files:
                if not f.endswith(('.h', '.cpp')):
                    continue
                fp = os.path.join(dirpath, f)
                body = open(fp, 'rb').read().decode('utf-8-sig', errors='replace')
                pattern = chr(34) + '([^' + chr(34) + chr(92) + 'n]{2,40})' + chr(34)
                for lit in re.findall(pattern, body):
                    if lit in old_labels:
                        string_leftovers.append((os.path.relpath(fp, DST_ROOT), lit))
    if string_leftovers:
        print('')
        print('!! old trait/temperament DISPLAY STRINGS still present:')
        for fp, lit in string_leftovers[:30]:
            print('   ' + lit + '  in  ' + fp)
    if leftovers:
        print(f"\n!! {len(leftovers)} leftover Pokemon-term hits:")
        for p, w in leftovers[:40]:
            print(f"   {w:14s} {p}")
    else:
        print("\nno leftover Pokemon vocabulary found")


# ── RETIRED ───────────────────────────────────────────────────────────────────
#
# This script is no longer part of the workflow. The plugin's Source/ tree is
# hand-maintained now, and this script DELETES and regenerates it -- running it
# would destroy every hand-written change since the migration.
#
# It is kept as the record of what the migration did. Read it; do not run it.
#
# If you genuinely need to re-run a rename from the original source, you almost
# certainly want to write a targeted script for that one rename instead. If you
# really do want this whole thing, pass --i-know-this-wipes-the-plugin.

def _refuse_to_run():
    import sys
    if "--i-know-this-wipes-the-plugin" in sys.argv:
        print("Override accepted. Regenerating -- hand-written changes will be lost.")
        return
    print(__doc__.strip().splitlines()[0])
    print()
    print("RETIRED: this script wipes and regenerates the plugin's Source/ tree.")
    print("The plugin is hand-maintained now, so running it would destroy work.")
    print()
    print("It is kept as documentation of the migration. See Docs/migration/README.md.")
    print("To override anyway: --i-know-this-wipes-the-plugin")
    sys.exit(1)


if __name__ == "__main__":
    _refuse_to_run()
    main()
