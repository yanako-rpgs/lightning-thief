// ItemData.h - UPDATED with Actor Reference and Held Items
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GF_ItemEnums.h"
// Skill classes are referenced loosely here -- see TeachableSkill below.
#include "PaperSprite.h"
#include "GF_ElementTypes.h"  // EGF_Element
#include "GF_ElementTypes.h"
#include "GF_ItemData.generated.h"

// Forward declarations
class ACore3D;
class UPaperFlipbook;
class UNiagaraSystem;
class USoundBase;

/**
 * Base data asset for all items
 * NOW WITH ACTOR CLASS REFERENCES!
 */
UCLASS(BlueprintType, meta=(ThumbnailRenderer="Class'/Script/GammaFrameworkEditor.CreatureSpeciesDataThumbnailRenderer'"))
class GAMMAFRAMEWORKWORLD_API UGF_ItemData : public UDataAsset
{
    GENERATED_BODY()

public:
    //--------------------
    // BASIC INFO
    //--------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    FName ItemName;  // Unique identifier (e.g., "Core", "MajorSalve")

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    FText DisplayName;  // What shows in UI (e.g., "Pok� Core", "Major Salve")

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    UTexture2D* Icon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    int32 ItemID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    EGF_ItemCategory Category = EGF_ItemCategory::Items;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    EGF_ItemType ItemType = EGF_ItemType::None;

    //--------------------
    // USAGE
    //--------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    bool bConsumable = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    bool bUsableInBattle = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    bool bUsableOutsideBattle = false;

    //--------------------
    // VALUE
    //--------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    int32 BuyPrice = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
    int32 SellPrice = 0;

    //--------------------
    // HEALING PROPERTIES
    //--------------------

    // Flat HP restored (e.g. Potion = 20, Major Salve = 50). Ignored when bRestorePercentage is true.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Healing")
    int32 HPRestoreAmount = 0;

    // When true, restore HPRestorePercentage % of max HP instead of a flat amount.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Healing")
    bool bRestorePercentage = false;

    // Percentage of max HP to restore (0-100). Only used when bRestorePercentage is true.
    // Revive = 50, Max Revive = 100, Max Potion = 100.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Healing", meta = (EditCondition = "bRestorePercentage", ClampMin = "0.0", ClampMax = "100.0"))
    float HPRestorePercentage = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Healing")
    int32 UsesRestoreAmount = 0;

    //--------------------
    // VITAMIN PROPERTIES
    //--------------------

    /** Which TrainingValue this vitamin raises. Only used when ItemType == Vitamin. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Vitamin")
    EGF_VitaminStat VitaminStat = EGF_VitaminStat::HP;

    /** How many TrainingValue points one dose adds (10 in classic). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Vitamin", meta = (ClampMin = "1", ClampMax = "252"))
    int32 TrainingBoostAmount = 10;

    //--------------------
    // CORE PROPERTIES
    //--------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core")
    EGF_CoreType CoreType = EGF_CoreType::SimpleCore;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core")
    float CatchRateModifier = 1.0f;

    /** CRITICAL: Which actor to spawn when this core is thrown */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core")
	TSubclassOf<AActor> CoreActorClass;

    //--------------------
    // CORE VISUALS
    //--------------------
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Visuals")
    UPaperSprite* IdleCoreSprite;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Visuals")
    UPaperFlipbook* ThrowFlipbook;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Visuals")
    UPaperFlipbook* OpenFlipbook;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Visuals")
    UPaperFlipbook* ShakeFlipbook;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Visuals")
    UPaperFlipbook* CaptureFlipbook;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Visuals")
    UPaperFlipbook* BreakoutFlipbook;

    //--------------------
    // CORE VFX
    //--------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core VFX")
    UNiagaraSystem* ThrowTrailEffect;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core VFX")
    UNiagaraSystem* OpenEffect;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core VFX")
    UNiagaraSystem* CaptureEffect;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core VFX")
    UNiagaraSystem* BreakoutEffect;

    //--------------------
    // CORE AUDIO
    //--------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Audio")
    USoundBase* ThrowSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Audio")
    USoundBase* HitSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Audio")
    USoundBase* OpenSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Audio")
    USoundBase* ShakeSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Audio")
    USoundBase* CaptureSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Core Audio")
    USoundBase* BreakoutSound;

    //--------------------
    // Tome PROPERTIES
    //--------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Tome")
    TSoftClassPtr<AActor> TeachableSkill;   // AGF_SkillDefinition subclass.
    // Deliberately loose: the World module owns items, Creatures owns
    // skills, and World cannot name a Creatures type. Cast on that side.

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Tome")
    bool bIsHM = false;

    //--------------------
    // HELD ITEM PROPERTIES
    //--------------------

    /** Can this item be held by a Creature? */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item")
    bool bCanBeHeld = false;

    /** Type of held item effect */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item")
    EGF_HeldItemEffect HeldItemEffect = EGF_HeldItemEffect::None;

    /** Is this held item consumed when activated? */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item")
    bool bConsumedOnUse = false;

    /** Berry trigger condition (for berries) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Berry")
    EGF_BerryTrigger BerryTrigger = EGF_BerryTrigger::None;

    //--------------------
    // STAT MODIFIERS
    //--------------------

    /** Attack multiplier (1.5 = +50%) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Stats", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float AttackMultiplier = 1.0f;

    /** Defense multiplier */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Stats", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float DefenseMultiplier = 1.0f;

    /** Special Attack multiplier */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Stats", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float MagicMultiplier = 1.0f;

    /** Special Defense multiplier */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Stats", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float PoiseMultiplier = 1.0f;

    /** Speed multiplier */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Stats", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float SpeedMultiplier = 1.0f;

    /** Accuracy multiplier (Wide Lens = 1.1 = +10%) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Stats", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float AccuracyMultiplier = 1.0f;

    /** Critical hit rate boost (0 = normal, 1 = +1 stage, 2 = +2 stages) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Stats")
    int32 CriticalHitBoost = 0;

    //--------------------
    // DAMAGE MODIFIERS
    //--------------------

    /** Overall damage multiplier (Blood Orb = 1.3 = +30%) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Damage", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float DamageMultiplier = 1.0f;

    /** Boosts super-effective moves (Adept Belt = 1.2 = +20%) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Damage", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float SuperEffectiveBoost = 1.0f;

    /** Which Creature type does this item boost? */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Type Boost")
    EGF_Element BoostedType = EGF_Element::None;

    /** How much does it boost? (1.2 = +20% damage) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Type Boost", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float TypeBoostMultiplier = 1.0f;

    /** HP recoil after dealing damage (Blood Orb = 10.0 = 10% recoil) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Damage", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float RecoilPercentage = 0.0f;

    //--------------------
    // BERRY EFFECTS
    //--------------------

    /** HP restored when berry activates */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Berry")
    int32 BerryHPRestore = 0;

    /**
     * Picked-to-ripe time for a tree bearing this berry, in REAL hours: 8 means eight
     * hours of wall clock, whether the game is running or not.
     *
     * Growth time belongs to the berry, not to the tree - an Oran takes as long in
     * Fernhollow as it does on Route 102 - so this is the one place it is authored.
     * UGF_BerryTreeComponent reads it from here; a tree can still override it for a
     * one-off. NOT in-game hours: BP_StylizedSky's day cycle is a visual speed, and a
     * berry aged off it would ripen while the player crosses a route.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Berry", meta = (ClampMin = "0.01"))
    float BerryGrowthHours = 8.0f;

    /** HP threshold percentage to trigger (50.0 = activate at 50% HP) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Berry", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float BerryActivationThreshold = 50.0f;

    /** Status conditions this berry cures */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Berry")
    TArray<EGF_STATUSEffect> CuredStatusConditions;

    /** Stat raised by this berry (for Liechi, Ganlon, etc) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Berry")
    FString StatToRaise; // "Attack", "Defense", "Speed", etc.

    /** How many stages to raise the stat */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Berry")
    int32 StatRaiseStages = 0;

    /** Type resistance (for type-resist berries like Occa, Passho, etc) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Berry")
    EGF_Element ResistType = EGF_Element::None;

    //--------------------
    // SPECIAL EFFECTS
    //--------------------

    /** EXP multiplier (Fortune Egg = 1.5 = +50% EXP) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float EXPMultiplier = 1.0f;

    /** Money multiplier (Coin Charm = 2.0 = double money) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special", meta = (ClampMin = "0.0", ClampMax = "3.0"))
    float MoneyMultiplier = 1.0f;

    /** Prevents downing from full HP with 1 HP (Last Stand Sash/Band) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special")
    bool bPreventsOHKO = false;

    /** Chance to prevent OHKO (Grit Band = 10%) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float OHKOPreventChance = 0.0f;

    /** Restores HP each turn (percentage, Sustain Charm = 6.25 = 1/16) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float HPRestorePerTurn = 0.0f;

    /** Prevents trait changes (Trait Shield) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special")
    bool bProtectsTrait = false;

    /** Makes holder immune to Ground-type moves (Lift Balloon) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special")
    bool bGrantsGroundImmunity = false;

    /** Prevents use of status moves (Bulwark Vest) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special")
    bool bPreventsStatusSkills = false;

    /** Locks holder into one move but boosts power (Choice items) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special")
    bool bLocksSkill = false;

    /** Protects from weather effects (Warding Parasol) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Held Item|Special")
    bool bProtectsFromWeather = false;
};


/**
 * Runtime instance of an item with quantity
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKWORLD_API FGF_ItemInstance
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FName ItemName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 ItemID = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 Quantity = 1;

    FGF_ItemInstance()
        : ItemName(NAME_None), ItemID(0), Quantity(1)
    {}

    FGF_ItemInstance(FName InItemName, int32 InQuantity = 1)
        : ItemName(InItemName), ItemID(0), Quantity(InQuantity)
    {}

    FGF_ItemInstance(int32 InItemID, int32 InQuantity = 1)
        : ItemName(NAME_None), ItemID(InItemID), Quantity(InQuantity)
    {}

    bool IsValid() const
    {
        return (ItemName != NAME_None || ItemID > 0) && Quantity > 0;
    }

    bool operator==(const FGF_ItemInstance& Other) const
    {
        if (ItemName != NAME_None && Other.ItemName != NAME_None)
        {
            return ItemName == Other.ItemName;
        }
        return ItemID == Other.ItemID;
    }
};

/**
 * Wrapper for items in a category - used for save/load
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKWORLD_API FGF_CategoryItemData
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    EGF_ItemCategory Category;

    UPROPERTY(SaveGame)
    TArray<FGF_ItemInstance> Items;

    FGF_CategoryItemData()
        : Category(EGF_ItemCategory::Items)
    {}

    FGF_CategoryItemData(EGF_ItemCategory InCategory)
        : Category(InCategory)
    {}
};