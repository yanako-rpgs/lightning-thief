// ItemEnums.h
#pragma once

#include "CoreMinimal.h"
#include "GF_ItemEnums.generated.h"

// Held item effect types
UENUM(BlueprintType)
enum class EGF_HeldItemEffect : uint8
{
    None                UMETA(DisplayName = "None"),

    // STAT MODIFIERS
    FlatStatBoost           UMETA(DisplayName = "Flat Stat Boost"),
    ElementBoost           UMETA(DisplayName = "Element Boost"),

    // HP/STATUS RESTORATION
    HPRestore           UMETA(DisplayName = "HP Restore"),
    StatusCure          UMETA(DisplayName = "Status Cure"),
    RegenEachTurn           UMETA(DisplayName = "Regenerate Each Turn"),
    RegenIfVenom         UMETA(DisplayName = "Regenerate If Venom, Else Harm"),

    // DEFENSIVE
    SurviveLethalOnce           UMETA(DisplayName = "Survive Lethal Hit (once, from full)"),
    SurviveLethalChance           UMETA(DisplayName = "Survive Lethal Hit (chance)"),
    PoiseUpNoStatus         UMETA(DisplayName = "Poise Up, No Status Skills"),
    FloatAboveTerra          UMETA(DisplayName = "Float (immune to Terra)"),

    // OFFENSIVE
    PowerAtHPCost             UMETA(DisplayName = "Power Boost With Recoil"),
    LockSkillPowerUp          UMETA(DisplayName = "Lock Skill, Power Up"),
    SuperEffectiveBoost          UMETA(DisplayName = "Super-Effective Boost"),

    // UTILITY
    EXPBoost            UMETA(DisplayName = "EXP Boost"),
    MoneyBoost          UMETA(DisplayName = "Money Boost"),
    EXPShare            UMETA(DisplayName = "EXP Share"),
    CritBoost          UMETA(DisplayName = "Critical Hit Boost"),

    // WEATHER/TERRAIN
    ExtendRain            UMETA(DisplayName = "Extend Rain"),
    ExtendSun            UMETA(DisplayName = "Extend Harsh Sun"),
    ExtendHail             UMETA(DisplayName = "Extend Hail"),
    ExtendSandstorm          UMETA(DisplayName = "Extend Sandstorm"),

    // GEMS (One-time 30% boost)
    OneShotElementBoost                 UMETA(DisplayName = "One-Time Element Boost"),

    // PLATES (a signature species type changing + 20% boost)
    HeldElementBoost               UMETA(DisplayName = "Held Element Boost"),

    // MEGA EVOLUTION
    FormChangeStone           UMETA(DisplayName = "Form Change Stone"),

    // Z-CRYSTALS
    BurstCharge            UMETA(DisplayName = "One-Time Burst Charge"),

    // TRAIT MODIFIERS
    TraitShield       UMETA(DisplayName = "Trait Shield"),

    // SPECIAL
    InheritMorePotential         UMETA(DisplayName = "Inherit More Potential"),
    PreventEvolution           UMETA(DisplayName = "Prevent Evolution"),
    WeatherImmunity     UMETA(DisplayName = "Weather Immunity"),
    BoostOnSuperEffective      UMETA(DisplayName = "Boost When Hit Super-Effectively")
};

// Berry activation conditions
UENUM(BlueprintType)
enum class EGF_BerryTrigger : uint8
{
    None                UMETA(DisplayName = "None"),
    HPLow               UMETA(DisplayName = "HP Below Threshold"),
    StatusCondition     UMETA(DisplayName = "Has Status Condition"),
    SuperEffective      UMETA(DisplayName = "Hit by Super-Effective"),
    Confusion           UMETA(DisplayName = "Becomes Confused"),
    StatDrop            UMETA(DisplayName = "Stat is Lowered")
};


/**
 * Item categories matching Creature games
 */
UENUM(BlueprintType)
enum class EGF_ItemCategory : uint8
{
    Items       UMETA(DisplayName = "Items"),          // Potions, healing items
    Cores   UMETA(DisplayName = "Cores"),     // All types of Cores
    Tomes         UMETA(DisplayName = "Tomes & HMs"),      // Technical/Hidden Machines
    Berries     UMETA(DisplayName = "Berries"),        // All berries
    KeyItems    UMETA(DisplayName = "Key Items"),      // Story items, bikes, rods
    Other       UMETA(DisplayName = "Other Items"),    // Mail, evolution stones
    MAX         UMETA(Hidden)                          // For iteration - not a real category
};

/**
 * Item types for special functionality
 */
UENUM(BlueprintType)
enum class EGF_ItemType : uint8
{
    None            UMETA(DisplayName = "None"),
    HPRestore       UMETA(DisplayName = "HP Restore"),
    StatusHeal      UMETA(DisplayName = "Status Heal"),
    Revive          UMETA(DisplayName = "Revive"),
    UsesRestore       UMETA(DisplayName = "Uses Restore"),
    Core        UMETA(DisplayName = "Core"),
    Tome              UMETA(DisplayName = "Tome/HM"),
    Berry           UMETA(DisplayName = "Berry"),
    EvolutionStone  UMETA(DisplayName = "Evolution Stone"),
    KeyItem         UMETA(DisplayName = "Key Item"),
    BattleItem      UMETA(DisplayName = "Battle Item"),
    Vitamin         UMETA(DisplayName = "Vitamin (TrainingValue Boost)"),
    LevelBoost       UMETA(DisplayName = "Level Boost")
};

/**
 * Which TrainingValue a vitamin raises (Protein, Iron, etc.)
 */
UENUM(BlueprintType)
enum class EGF_VitaminStat : uint8
{
    HP              UMETA(DisplayName = "HP (HP Up)"),
    Attack          UMETA(DisplayName = "Attack (Protein)"),
    Defense         UMETA(DisplayName = "Defense (Iron)"),
    Magic   UMETA(DisplayName = "Sp. Attack (Calcium)"),
    Poise  UMETA(DisplayName = "Sp. Defense (Zinc)"),
    Speed           UMETA(DisplayName = "Speed (Carbos)")
};

/**
 * Core types with their catch rate modifiers.
 * Serialised as uint8 -- append new cores at the end, never reorder these.
 */
UENUM(BlueprintType)
enum class EGF_CoreType : uint8
{
    SimpleCore   UMETA(DisplayName = "Simple Core"),     // 1.0x
    GreaterCore  UMETA(DisplayName = "Greater Core"),    // 1.5x
    HyperCore    UMETA(DisplayName = "Hyper Core"),      // 2.0x
    AbsoluteCore UMETA(DisplayName = "Absolute Core"),   // Always catches
    WardenCore   UMETA(DisplayName = "Warden Core"),     // 1.5x, preserve-issued
    SnareCore    UMETA(DisplayName = "Snare Core"),      // 3.5x against Chitin or Tide
    DepthCore    UMETA(DisplayName = "Depth Core"),      // 3.5x underwater
    CradleCore   UMETA(DisplayName = "Cradle Core"),     // (41 - Level) / 10, min 1.0x
    EchoCore     UMETA(DisplayName = "Echo Core"),       // 3.5x if registered in the Compendium
    AeonCore     UMETA(DisplayName = "Aeon Core"),       // 1.0x -> 4.0x over turns 1-11
    HearthCore   UMETA(DisplayName = "Hearth Core"),     // 1.0x, boosts bond
    EliteCore    UMETA(DisplayName = "Elite Core"),      // 1.0x, cosmetic
    GloamCore    UMETA(DisplayName = "Gloam Core"),      // 3.5x at night or underground
    MendCore     UMETA(DisplayName = "Mend Core"),       // 1.0x, heals on claim
    SuddenCore   UMETA(DisplayName = "Sudden Core"),     // 5.0x on the first turn
    BoonCore     UMETA(DisplayName = "Boon Core"),       // 1.0x, event Creature
    OmenCore     UMETA(DisplayName = "Omen Core")        // 3.5x on a unique Creature, 1.0x otherwise
};


