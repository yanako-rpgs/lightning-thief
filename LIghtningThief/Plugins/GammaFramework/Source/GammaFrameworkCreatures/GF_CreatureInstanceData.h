// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_SkillDefinition.h"
#include "GF_ElementTypes.h"
#include "GF_CreatureInstanceData.generated.h"

// Forward declarations
class UGF_CreatureSpeciesData;
class AGF_SkillDefinition;

USTRUCT(BlueprintType)
struct FGF_SkillEntry
{
    GENERATED_BODY()

    // The move class
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<AGF_SkillDefinition> SkillClass;

    // Current Uses for this move
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 CurrentUses = 10;

    // Max Uses for this move
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxUses = 10;

    FGF_SkillEntry()
        : CurrentUses(10), MaxUses(10)
    {}


};

USTRUCT(BlueprintType)
struct FGF_LevelUpSkills
{
    GENERATED_BODY()

    // Array of moves that can be learned
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<TSoftClassPtr<AGF_SkillDefinition>> LearnableSkills;

    FGF_LevelUpSkills()
    {}
};

/**
 * A single selectable move entry for the "Change Skills" screen.
 * Used to populate BOTH the "Available Skills" (level-up) tab and the "Available Tomes" tab.
 */
USTRUCT(BlueprintType)
struct FGF_SkillOption
{
    GENERATED_BODY()

    // The move this entry would teach. Resolve for name / type / power / Uses display.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftClassPtr<AGF_SkillDefinition> Skill;

    // The Tome item this entry came from (for the icon/label). NAME_None for level-up moves.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName SourceTomeItem = NAME_None;

    // Level this move is learned at (level-up moves only; 0 for Tome entries). Use for the
    // "Lv. X" label, and compare against the Creature's level to grey out not-yet moves.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 LearnLevel = 0;

    // True if the Creature already knows this move -> grey the entry out and show "Known".
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bAlreadyKnown = false;

    FGF_SkillOption() {}
};

/**
 * Flat, ready-to-display copy of a move's stats, resolved from its (soft) class in C++.
 * Lets a widget show a move entry with a single pure node — no async class loading in BP.
 */
USTRUCT(BlueprintType)
struct FGF_SkillDisplayInfo
{
    GENERATED_BODY()

    // False if the move class couldn't be resolved (null/failed load).
    UPROPERTY(BlueprintReadOnly)
    bool bValid = false;

    UPROPERTY(BlueprintReadOnly)
    FName Name = NAME_None;

    // Prose, so FText -- mirrors AGF_SkillDefinition::Description. Name above
    // stays FName because it is an identifier that gets compared.
    UPROPERTY(BlueprintReadOnly)
    FText Description;

    UPROPERTY(BlueprintReadOnly)
    EGF_Element Type = EGF_Element::None;

    // Physical / Special (status moves included via the enum).
    UPROPERTY(BlueprintReadOnly)
    EGF_SkillCategory Category = EGF_SkillCategory::Physical;

    UPROPERTY(BlueprintReadOnly)
    float Power = 0.f;

    UPROPERTY(BlueprintReadOnly)
    float Accuracy = 0.f;

    UPROPERTY(BlueprintReadOnly)
    int32 MaxUses = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Priority = 0;

    FGF_SkillDisplayInfo() {}
};




/**
 * How this Creature came to be owned. Drives the wording of the tamer memo line
 * ("Met at Lv. 5." vs "Egg hatched.") and nothing else — it is display data.
 *
 * Unknown means the Creature predates the memo system, so its origin was never
 * recorded. Treat that as "no memo", never as a bug.
 */
UENUM(BlueprintType)
enum class EGF_CreatureMetType : uint8
{
	Unknown		UMETA(DisplayName = "Unknown"),
	Caught		UMETA(DisplayName = "Caught"),
	Gift		UMETA(DisplayName = "Received as a gift"),
	Hatched		UMETA(DisplayName = "Hatched from an egg"),
	EggReceived	UMETA(DisplayName = "Egg received")
};

/**
 * Serializable structure containing all instance-specific Creature data
 * This is what gets saved/loaded and stored in boxes
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKCREATURES_API FGF_CreatureInstanceData
{
	GENERATED_BODY()

	// Reference to species data (shared across all Creature of same species)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TSoftObjectPtr<UGF_CreatureSpeciesData> SpeciesData;

	// Unique Creature Data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 CreatureID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FName Nickname; // Optional custom name

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Level = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float CurrentEXP = 0;

	// Current Battle Stats
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float CurrentHP = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	float MaxHP = 100;

	// Potentials (Individual Values)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 HP_Potential = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Attack_Potential = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Defense_Potential = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Magic_Potential = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Poise_Potential = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Speed_Potential = 0;

	// Training (Effort Values)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 HP_Training = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Attack_Training = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Defense_Training = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Magic_Training = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Poise_Training = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Speed_Training = 0;

	// Temperament & Gender
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	EGF_Temperament Temperament = EGF_Temperament::Robust;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	EGF_CreatureGender Gender = EGF_CreatureGender::Genderless;

	//--------------------
	// TRAIT
	//--------------------

	// The trait this individual Creature was born/caught with. Baked in at creation
	// so it never changes when the species asset is edited later.
	// None on a Creature saved before traits existed — UGF_CreatureTraitLibrary::
	// GetInstanceTrait() resolves that back to the species so old saves still work.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Trait")
	EGF_CreatureTrait Trait = EGF_CreatureTrait::None;

	// Which species slot the trait came from (0 = Trait1, 1 = Trait2).
	// Kept so evolution can hand the evolved form the matching slot instead of
	// re-rolling, exactly like the games.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Trait")
	int32 TraitSlot = 0;

	// Learned Skills (store as soft class references)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<TSoftClassPtr<AGF_SkillDefinition>> Skills;

	// Skill Uses tracking
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<int32> CurrentUses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<int32> MaxUses;

	// Status Conditions
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	EGF_STATUSEffect StatusCondition = EGF_STATUSEffect::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 SleepCounter = 0;

	// Tamer Information
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FName OriginalTamerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FName CurrentTamerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 OriginalTamerID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 CurrentTamerID = 0;

	// Capture Information
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TSoftObjectPtr<UTexture2D> CaughtCore;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool bIsFollowerOut = false;




	 /**
     * Name of the Core used to catch this Creature
     * Used to display correct ball icon and animations
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Capture")
    FName CaughtCoreName = "Core";  // Default to Core


	//--------------------
	// ORIGIN / TAMER MEMO
	//--------------------
	// Everything below is stamped ONCE, when the Creature first becomes the player's,
	// and then never touched again — a Creature that changes hands in a trade carries
	// its original memo with it, exactly like the games. UGF_CreatureMemoLibrary owns all
	// the reading and writing; don't set these by hand from Blueprint.

	// Route the Creature was caught / received / hatched on, as an asset path.
	// Stored as a path rather than a display string so renaming a route's display
	// text updates every Creature that came from there. Invalid = unknown place.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Origin")
	FSoftObjectPath MetRoutePath;

	// Location text to show INSTEAD of the route, for origins that aren't a place
	// the player was standing ("Sanctuary Couple", "Traded", event gifts). Empty means
	// "use MetRoutePath".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Origin")
	FString MetLocationOverride;

	// Level the Creature was at when it was obtained. 0 = never recorded.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Origin")
	int32 MetLevel = 0;

	// Real-world date the Creature was obtained, same as the classic RTC memo.
	// A zero FDateTime is the "never stamped" sentinel — check that, not the route,
	// because a Creature can legitimately be obtained somewhere with no route set.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Origin")
	FDateTime MetDate = FDateTime(0);

	// Caught / gift / hatched. Only changes the memo wording.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Origin")
	EGF_CreatureMetType MetType = EGF_CreatureMetType::Unknown;

	/**
	 * Extra memo line, for a Creature whose story the standard lines cannot tell.
	 *
	 * Written for event distributions: a Lodemote that cannot evolve looks like
	 * a bug unless something says otherwise, and "Its magnets are damaged. It
	 * cannot evolve." turns a support ticket into flavour.
	 *
	 * Empty is normal. When empty and bCannotEvolve is set, the memo falls back
	 * to a plain generic line rather than saying nothing -- silence is what makes
	 * players think it is broken.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Origin")
	FString MemoNote;




	//--------------------
	// EGG / FRIENDSHIP
	//--------------------

	// True while this is still an unhatched egg. Egg entries occupy a party slot
	// but can't battle, be deposited in the sanctuary, or be sent out as a follower.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Egg")
	bool bIsEgg = false;

	// Egg cycles left before hatching. One is burned every 256 steps while the
	// egg is in the party. Meaningless once bIsEgg is false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Egg")
	int32 EggCyclesRemaining = 0;

	// Species this egg will hatch into. Set when the egg is created so the egg
	// can display as "Egg" while still remembering what's inside.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Egg")
	FName EggSpeciesName;

	// Which egg graphic to draw for this egg — the "type" of egg (Grass, Fire, Water...).
	// None means "decide from the species", which UGF_BreedingLibrary::GetEggAppearanceType
	// resolves to the species' primary type. Set it explicitly on a gift egg when you
	// want a specific look regardless of what's inside, e.g. a mysterious plain egg.
	// Meaningless once bIsEgg is false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Egg")
	EGF_Element EggAppearanceType = EGF_Element::None;

	// How many unique rolls this egg gets when it hatches. 1 = no bonus, 6 = ForeignPair.
	// Captured when the egg is created because it depends on the parents, who may be
	// withdrawn or traded away long before the egg hatches. The Unique Charm is NOT
	// baked in here — that's checked at hatch time, so getting the charm helps eggs
	// you are already carrying. Meaningless once bIsEgg is false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Egg")
	int32 EggUniqueRolls = 1;

	// Bond / happiness, 0-255. Starts at 70 for a caught Creature and at
	// 120 for one that hatched from an egg, matching classic.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "0", ClampMax = "255"))
	int32 Bond = 70;

	// Special Properties
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool bIsUnique = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool bIsDowned = false;

	/**
	 * This Creature never evolves, by any trigger, forever.
	 *
	 * For event distributions that are meant to stay in their given form -- a
	 * level 5 Lodemote handed out at a giveaway is supposed to still be a
	 * Lodemote. Both CheckForEvolution and EvolveCreature refuse it, so no
	 * in-game route can evolve it: not level-up, not a stone, not a Blueprint
	 * calling EvolveCreature directly.
	 *
	 * Deliberately a property of the Creature rather than a held AnchorStone, which
	 * the player could simply take off. It is NOT cheat-proof -- it lives in a
	 * save file the player owns, and someone editing that save could flip it or
	 * just hand themselves the evolved form outright. Nothing short of
	 * server-owned saves can stop that, and this game does not have those.
	 *
	 * Travels through the trade/gift codec for free: it is SaveGame, and the
	 * codec serializes tagged SaveGame properties, so an older build simply
	 * leaves it false rather than rejecting the blob.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool bCannotEvolve = false;

	// Held Item (optional)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FName HeldItem;

	// Stable per-Creature identity. MUST stay SaveGame: without it the SaveGame archive
	// skips the field, the struct is default-constructed on load, and every Creature gets
	// a fresh GUID on every load — which silently breaks anything keyed on identity
	// (link-trade tracking, dupe detection). Creature saved before this was tagged will
	// come back with a one-time new GUID and then stay stable from that load on.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FGuid UniqueID;




	// Helper Functions
	FGF_CreatureInstanceData()
	{
		CreatureID = FMath::RandRange(0, 999999);
		UniqueID = FGuid::NewGuid();
	}

FGF_CreatureInstanceData(const FGF_CreatureInstanceData& Other)
{
    // Species and basic info
    SpeciesData = Other.SpeciesData;
    CreatureID = Other.CreatureID;
    UniqueID = Other.UniqueID;  // <- ADD THIS!
    Nickname = Other.Nickname;
    Level = Other.Level;
    CurrentEXP = Other.CurrentEXP;

    // Current stats
    CurrentHP = Other.CurrentHP;
    MaxHP = Other.MaxHP;

    // Potentials
    HP_Potential = Other.HP_Potential;
    Attack_Potential = Other.Attack_Potential;
    Defense_Potential = Other.Defense_Potential;
    Magic_Potential = Other.Magic_Potential;
    Poise_Potential = Other.Poise_Potential;
    Speed_Potential = Other.Speed_Potential;

    // Training
    HP_Training = Other.HP_Training;
    Attack_Training = Other.Attack_Training;
    Defense_Training = Other.Defense_Training;
    Magic_Training = Other.Magic_Training;
    Poise_Training = Other.Poise_Training;
    Speed_Training = Other.Speed_Training;

    // Temperament & Gender
    Temperament = Other.Temperament;
    Gender = Other.Gender;

    // Trait
    Trait = Other.Trait;
    TraitSlot = Other.TraitSlot;

    // Skills
    Skills = Other.Skills;
    CurrentUses = Other.CurrentUses;
    MaxUses = Other.MaxUses;

    // Status
    StatusCondition = Other.StatusCondition;
    SleepCounter = Other.SleepCounter;

    // Tamer info - CRITICAL!
    OriginalTamerName = Other.OriginalTamerName;
    OriginalTamerID = Other.OriginalTamerID;  // <- ADD THIS!
    CurrentTamerName = Other.CurrentTamerName;
    CurrentTamerID = Other.CurrentTamerID;    // <- ADD THIS!

    // Capture info
    CaughtCore = Other.CaughtCore;
    CaughtCoreName = Other.CaughtCoreName;

    // Origin / tamer memo — drop any of these and a Creature loses its memo the
    // first time it's copied, which is every party add, box move and trade.
    MetRoutePath = Other.MetRoutePath;
    MetLocationOverride = Other.MetLocationOverride;
    MetLevel = Other.MetLevel;
    MetDate = Other.MetDate;
    MetType = Other.MetType;
    MemoNote = Other.MemoNote;

    // Special properties
    bIsUnique = Other.bIsUnique;
    bIsDowned = Other.bIsDowned;
    bCannotEvolve = Other.bCannotEvolve;

    // Egg / bond — NOTE: this copy constructor is hand-written, so any new
    // field added to this struct MUST be copied here or it silently resets.
    bIsEgg = Other.bIsEgg;
    EggCyclesRemaining = Other.EggCyclesRemaining;
    EggSpeciesName = Other.EggSpeciesName;
    EggUniqueRolls = Other.EggUniqueRolls;
    EggAppearanceType = Other.EggAppearanceType;
    Bond = Other.Bond;


    // Held item
    HeldItem = Other.HeldItem;

    UE_LOG(LogTemp, VeryVerbose, TEXT("Copy constructor: Copied all fields including UniqueID"));
}


	// Initialize from Species Data
	void Initialize(UGF_CreatureSpeciesData* InSpeciesData, int32 InLevel,
	    const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills = TArray<TSubclassOf<AGF_SkillDefinition>>(),
	    FName TamerName = NAME_None, int32 TamerID = 0);

	/**
	 * Ensures CurrentUses and MaxUses arrays match the Skills array.
	 * Fills MISSING entries from the move CDO and clamps CurrentUses to [0, MaxUses].
	 * Safe to call at any time, including on every read.
	 *
	 * It does NOT restore spent Uses: a CurrentUses of 0 is a real, meaningful value
	 * (the move is out of Uses and LastResort logic depends on it staying that way).
	 * To actually heal Uses — Ethers, Creature Centers, the sanctuary — write MaxUses into
	 * CurrentUses at that call site instead.
	 */
	void NormalizeUses();

	/**
	 * Keeps an egg flagged as downed.
	 *
	 * An egg fills a party slot but can never battle, and every "is this Creature out of
	 * action" test in the game — including the Blueprint ones, which only ever look at
	 * bIsDowned — has to skip it. Rather than teach each of those about eggs, an egg
	 * simply always carries bIsDowned, so a check that knows nothing about eggs still
	 * does the right thing: the swap prompt won't offer it, the send-out picker won't
	 * choose it, and a party of one Creature plus one egg can't soft-lock the battle.
	 *
	 * HP is deliberately left alone — the party screen divides CurrentHP by MaxHP.
	 *
	 * Safe to call at any time, including on every read. Once bIsEgg clears at hatch
	 * this does nothing, and HatchEggInstance clears the downed flag itself.
	 */
	void NormalizeEggState();

	/**
 * Learn a new move (adds to moves array if space available)
 * @param NewSkill - The move to learn
 * @param bReplaceSkill - If true and party is full, replaces the move at ReplaceIndex
 * @param ReplaceIndex - Which move to replace if move list is full
 * @return True if move was learned
 */
bool LearnSkill(TSoftClassPtr<AGF_SkillDefinition> NewSkill, bool bReplaceSkill = false, int32 ReplaceIndex = 0);

/**
 * Forget a move by index
 */
bool ForgetSkill(int32 SkillIndex);

/**
 * Check if Creature already knows a move
 */
bool KnowsSkill(TSoftClassPtr<AGF_SkillDefinition> Skill) const;

/**
 * Get moves that should be learned at a specific level
 */
TArray<TSoftClassPtr<AGF_SkillDefinition>> GetSkillsToLearnAtLevel(int32 AtLevel) const;

	/**
	 * The move(s) at the LOWEST learn level this species defines, ignoring the Creature's
	 * own level.
	 *
	 * A floor, so nothing can ever end up with zero moves. Seven species currently have
	 * no learnable move at or below level 5 -- Cindercrawl's earliest is Ember at 8, Duskwing's
	 * is level 6, Ironmote has none at any level -- usually because the low-level move has
	 * no Blueprint yet and the populate script skips it. Without this, a level-5 hatchling
	 * of those species comes out unable to battle at all.
	 */
	TArray<TSoftClassPtr<AGF_SkillDefinition>> GetEarliestLearnableSkills() const;

/**
 * Check if Creature can learn more moves (has less than 4)
 */
bool CanLearnMoreSkills() const { return Skills.Num() < 4; }

	// Check if this data is valid (has a species reference path set)
	// Uses IsNull() rather than IsValid() - IsValid() requires the asset to be in memory,
	// which can fail right after save/load. IsNull() just checks the path exists.
	bool IsValid() const
	{
		return !SpeciesData.IsNull();
	}

	// Get display name (nickname if set, otherwise species name).
	// Eggs always come back as "Egg" so the species inside stays a surprise.
	FName GetDisplayName() const;

	// True if this entry is an unhatched egg.
	bool IsEgg() const { return bIsEgg; }

	// Adjust bond, clamped to 0-255.
	void AddBond(int32 Delta) { Bond = FMath::Clamp(Bond + Delta, 0, 255); }

	/**
 * Give this Creature a held item
 */
void GiveHeldItem(FName ItemName);

/**
 * Remove held item from this Creature
 */
FName TakeHeldItem();

/**
 * Check if Creature has a held item
 */
bool HasHeldItem() const { return !HeldItem.IsNone(); }

/**
 * Get the name of the held item
 */
FName GetHeldItem() const { return HeldItem; }

};