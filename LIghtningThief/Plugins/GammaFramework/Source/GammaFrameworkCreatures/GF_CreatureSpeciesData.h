// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "Engine/DataAsset.h"
#include "GF_CreatureTraitTypes.h"
#include "GF_ElementTypes.h"
#include "GF_CreatureSpeciesData.generated.h"


// Forward declarations
class UPaperFlipbook;
class UPaperSprite;
class USoundWave;
class AGF_SkillDefinition;
class AGF_SimpleFollower;

// State of a species in the player's Compendium
UENUM(BlueprintType)
enum class EGF_CompendiumEntryState : uint8
{
	Unknown		UMETA(DisplayName = "Unknown"),		// never encountered
	Seen		UMETA(DisplayName = "Seen"),		// spotted but not caught
	Caught		UMETA(DisplayName = "Caught"),		// owned at least once
};

// Creature Typing Enums
UENUM(BlueprintType)
enum class EGF_Temperament : uint8
{
	Ferocious		UMETA(DisplayName = "Ferocious"),
	Meek		UMETA(DisplayName = "Meek"),
	Stalwart		UMETA(DisplayName = "Stalwart"),
	Valiant		UMETA(DisplayName = "Valiant"),
	Serene		UMETA(DisplayName = "Serene"),
	Guarded		UMETA(DisplayName = "Guarded"),
	Placid		UMETA(DisplayName = "Placid"),
	Tender		UMETA(DisplayName = "Tender"),
	Robust		UMETA(DisplayName = "Robust"),
	Rushed		UMETA(DisplayName = "Rushed"),
	Wily		UMETA(DisplayName = "Wily"),
	Sprightly		UMETA(DisplayName = "Sprightly"),
	Slack			UMETA(DisplayName = "Slack"),
	Solitary		UMETA(DisplayName = "Solitary"),
	Temperate		UMETA(DisplayName = "Temperate"),
	Humble		UMETA(DisplayName = "Humble"),
	Innocent		UMETA(DisplayName = "Innocent"),
	Unruly		UMETA(DisplayName = "Unruly"),
	Silent		UMETA(DisplayName = "Silent"),
	Peculiar		UMETA(DisplayName = "Peculiar"),
	Reckless		UMETA(DisplayName = "Reckless"),
	Languid		UMETA(DisplayName = "Languid"),
	Brash		UMETA(DisplayName = "Brash"),
	Stoic		UMETA(DisplayName = "Stoic"),
	Skittish		UMETA(DisplayName = "Skittish"),
	MAX			UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EGF_CreatureGender : uint8
{
	Male		UMETA(DisplayName = "Male"),
	Female		UMETA(DisplayName = "Female"),
	Genderless	UMETA(DisplayName = "Genderless")
};

// Breeding groups. Two Creature can produce an egg only if they share at least
// one group (Shifter is the universal exception, untagged can never breed).
// Names follow the classic internal groups — the wiki names for a few of these
// differ (Field = Ground, Grass = Plant, Humanoid = Humanshape,
// Amorphous = Indeterminate).
// Breeding groups are FName tags rather than an enum: every project defines
// its own, and adding one must not mean editing plugin C++. Two species can
// breed when their tag sets overlap. See BreedingGroups below.

UENUM(BlueprintType)
enum class EGF_EXPCurves : uint8
{
	Volatile			UMETA(DisplayName = "Volatile"),
	Swift			UMETA(DisplayName = "Swift"),
	Steady		UMETA(DisplayName = "Steady"),
	Measured		UMETA(DisplayName = "Measured"),
	Gradual			UMETA(DisplayName = "Gradual"),
	Uneven		UMETA(DisplayName = "Uneven")
};

/**
 * The six states a creature is ever in on the battle stage.
 *
 * ---------------------------------------------------------------------------
 * Why there is no Front and no Back
 * ---------------------------------------------------------------------------
 *
 * The stage is viewed SIDE-ON, both lines in profile. There is no camera angle
 * from which you see a creature's face on one side and its back on the other,
 * so a creature needs ONE set of art, not two.
 *
 * The previous layout carried a 24-value Front enum and an identical 24-value
 * Back enum, with gender and unique folded into the key names. That was 48
 * flipbook slots per species to express six states, and it doubled the art
 * budget for a view that no longer exists.
 *
 * Now: six states, and the variants are separate MAPS rather than more keys.
 * A species author fills six entries and, if the creature has a unique form,
 * six more. Six-value dropdowns instead of a 24-value one.
 */
UENUM(BlueprintType)
enum class EGF_CreatureAnimState : uint8
{
	/** Standing on the line, healthy. */
	Idle      UMETA(DisplayName = "Idle"),

	/** Standing on the line, hurt. Swapped in below the low-HP threshold. */
	IdleLowHP UMETA(DisplayName = "Idle (Low HP)"),

	/** The send-out cry. Called Call, not Cry -- see Docs/NAMING.md. */
	Call      UMETA(DisplayName = "Call"),

	/** Playing an offensive action. */
	Attack    UMETA(DisplayName = "Attack"),

	/** Taking a hit. */
	Hurt      UMETA(DisplayName = "Hurt"),

	/** Downed. Called Down, not Faint -- see Docs/NAMING.md. */
	Down      UMETA(DisplayName = "Down"),
};

/**
 * Which of the four flipbook sets a lookup resolved to.
 *
 * Returned alongside the flipbook so a caller can tell "this species has proper
 * female unique art" from "it fell back to the normal set", which is the
 * difference between a finished creature and one still waiting on art.
 */
UENUM(BlueprintType)
enum class EGF_CreatureAnimVariant : uint8
{
	Normal       UMETA(DisplayName = "Normal"),
	Unique       UMETA(DisplayName = "Unique"),
	Female       UMETA(DisplayName = "Female"),
	FemaleUnique UMETA(DisplayName = "Female Unique"),
	None         UMETA(DisplayName = "None"),
};

// Base stats that don't change per species
USTRUCT(BlueprintType)
struct FGF_CreatureBaseStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Level = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 HP = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Attack = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Defense = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Magic = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Poise = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Speed = 50;
};

USTRUCT(BlueprintType)
struct FGF_LearnableSkill
{
    GENERATED_BODY()

    // The move that can be learned
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftClassPtr<AGF_SkillDefinition> Skill;

    // Level at which this move is learned (0 = knows from birth)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 LearnLevel = 1;

    FGF_LearnableSkill()
        : LearnLevel(1)
    {}
};

UENUM(BlueprintType)
enum class EGF_EvolutionTrigger : uint8
{
    Level       UMETA(DisplayName = "Level"),
    Item        UMETA(DisplayName = "Item"),
    Trade       UMETA(DisplayName = "Trade"),
    Bond  UMETA(DisplayName = "Bond"),
    TradeItem   UMETA(DisplayName = "Trade with Item")
};

UENUM(BlueprintType)
enum class EGF_EvolutionCondition : uint8
{
    None            UMETA(DisplayName = "None"),
    PersonalityLow  UMETA(DisplayName = "Personality < 50%"),
    PersonalityHigh UMETA(DisplayName = "Personality >= 50%"),
    TimeDay         UMETA(DisplayName = "Daytime"),
    TimeNight       UMETA(DisplayName = "Nighttime"),
    GenderMale      UMETA(DisplayName = "Male Only"),
    GenderFemale    UMETA(DisplayName = "Female Only"),
    LocationSpecific UMETA(DisplayName = "Specific Location")
};

USTRUCT(BlueprintType)
struct FGF_EvolutionMethod
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
    FName EvolvedSpecies;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
    EGF_EvolutionTrigger Trigger;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
    int32 RequiredLevel = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
    FName RequiredItem;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
    int32 RequiredBond = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
    FName RequiredHeldItem; // For trade evolutions with held items

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
    EGF_EvolutionCondition AdditionalCondition = EGF_EvolutionCondition::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
    FName RequiredLocation;
};



/**
 * Data Asset containing all static species information
 * This is shared across all Creature of the same species
 */
UCLASS(BlueprintType, meta=(ThumbnailRenderer="Class'/Script/GammaFrameworkEditor.CreatureSpeciesDataThumbnailRenderer'"))
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureSpeciesData : public UPrimaryDataAsset
{
	GENERATED_BODY()



public:

	// Species Information
	//
	// AssetRegistrySearchable on these two is load-bearing, not a nicety: it writes
	// the value into the asset registry so UGF_CreatureManagerSubsystem can answer
	// "which asset is Cindling?" from registry metadata alone. Without it the only
	// way to match a name to an asset is to LOAD every species -- and each of these
	// assets hard-references its full sprite set (~35 MB average, ~4.18 GB across
	// all 119), so that one question used to cost the entire sprite library.
	//
	// Adding the tag only affects assets saved AFTER the change. The existing 118
	// need one resave pass over /Game/BPS/CreatureData/Creature, otherwise the
	// subsystem falls back to the old load-everything path (and logs a warning
	// saying exactly that).
	//
	// NOTE: AssetRegistrySearchable is a UPROPERTY *specifier* and must sit in the
	// specifier list. Putting it in meta=() compiles and generates clean-looking
	// metadata, but the property never gets CPF_AssetRegistrySearchable and no tag
	// is ever emitted -- a silent no-op.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Species")
	FName SpeciesName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Species")
	int32 CompendiumNumber;

	/**
	 * Husk is the one species whose HP is NOT the Gen-3 formula: it is always
	 * exactly 1, at every level, with any Potentials or Training.
	 *
	 * This exists because six separate places compute MaxHP -- the subsystem's
	 * RecalculateStats, the level-up loop, UGF_CreatureStatLibrary, the battle actor's
	 * GenerateStats/RefreshStats, HP Up vitamins, and breeding -- and each one had
	 * to remember to hand-check the species name. Half of them didn't, which is why
	 * a Husk was born with 1 HP and then grew a real HP bar the first time it
	 * was sent into battle (the actor recalculated, then wrote the result back to
	 * the party). Every HP calculation asks this question instead of spelling the
	 * name out, so adding another fixed-HP species is a one-line change here.
	 */
	UFUNCTION(BlueprintPure, Category = "Species")
	bool HasFixedOneHP() const;

	/**
	 * The same rule, answerable from the NAME alone.
	 *
	 * Exists for sweeps over stored Creature: resolving a species name out of the asset
	 * registry costs nothing, while LoadSynchronous-ing every party and box entry to ask
	 * the asset directly would drag the whole ~4 GB sprite library back into memory. The
	 * member version above forwards to this, so there is still exactly one place that
	 * knows which species this applies to.
	 */
	// Parameter is InSpeciesName, not SpeciesName: UHT rejects a parameter that shadows a
	// member of the same class, and this class has a SpeciesName property.
	UFUNCTION(BlueprintPure, Category = "Species")
	static bool SpeciesNameHasFixedOneHP(FName InSpeciesName);

	// Compendium Entry Data
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species|Compendium")
	FText Category;  // e.g. "Chick Creature"

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species|Compendium", meta = (MultiLine = "true"))
	FText CompendiumDescription;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species|Compendium", meta = (ClampMin = "0.0"))
	float HeightMeters = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species|Compendium", meta = (ClampMin = "0.0"))
	float WeightKg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species|Compendium")
	UTexture2D* Footprint;


	// Locations where this Creature can be found, shown on the AREA tab
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species|Compendium")
	TArray<FText> Areas;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species")
	FGF_CreatureBaseStats BaseStats;

	// Tagged so the Compendium grid can show a species' typing without loading the
	// species. See FGF_SpeciesSummary.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Species")
	EGF_Element PrimaryElement;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Species")
	EGF_Element SecondaryElement;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species")
	TArray<EGF_Element> Weaknesses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species")
	TArray<EGF_Element> SuperWeaknesses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species")
	TArray<EGF_Element> Resistances;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species")
	TArray<EGF_Element> Immunities;

	//--------------------
	// ABILITIES
	//--------------------

	// The species' first (default) trait. Every Creature of this species that isn't
	// rolled into slot 2 gets this one.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	EGF_CreatureTrait Trait1 = EGF_CreatureTrait::None;

	// Optional second trait. Leave as None when the species only has one — the
	// 50/50 slot roll is skipped entirely in that case.
	// classic has no Hidden Traits, so there is deliberately no third slot.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traits")
	EGF_CreatureTrait Trait2 = EGF_CreatureTrait::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evolution")
	TArray<FGF_EvolutionMethod> Evolutions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category= "Extras")
	TSubclassOf<AGF_SimpleFollower> Follower;

	// Visual Assets
	//
	// DisplayIcon1 is tagged: an AssetRegistrySearchable object property stores the
	// referenced asset's PATH as a string, so the Compendium can build a soft ref to
	// just this one sprite and load it (a few KB) instead of the whole species
	// (~35 MB of front/back/unique sprites and a cry) to draw a grid cell.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Visuals")
	UPaperSprite* DisplayIcon1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
	UPaperSprite* DisplayIcon2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
	UPaperSprite* UniqueDisplayIcon1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
	UPaperSprite* UniqueDisplayIcon2;

	/**
	 * The side-on set. Six entries, and this is the only one that is required.
	 *
	 * Every lookup falls back to here, so a species with nothing but these six
	 * flipbooks is fully playable -- unique and female forms just reuse the
	 * normal animation until their art exists.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|Animations")
	TMap<EGF_CreatureAnimState, UPaperFlipbook*> SideAnimations;

	/** The unique ("shiny") palette. Leave empty and unique creature use the normal set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|Animations")
	TMap<EGF_CreatureAnimState, UPaperFlipbook*> SideAnimationsUnique;

	/** Only for species whose females are visibly different. Usually empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|Animations")
	TMap<EGF_CreatureAnimState, UPaperFlipbook*> SideAnimationsFemale;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals|Animations")
	TMap<EGF_CreatureAnimState, UPaperFlipbook*> SideAnimationsFemaleUnique;

	/**
	 * The flipbook to play, with the variant fallbacks applied.
	 *
	 * Fallback order for a female unique creature is FemaleUnique -> Unique ->
	 * Female -> Normal. Unique outranks Female deliberately: the unique palette
	 * is the thing a player is looking for and will notice missing, whereas a
	 * female silhouette on a species that never drew one is not a visible loss.
	 *
	 * IdleLowHP falls back to Idle within whichever set is chosen, so low-HP art
	 * is genuinely optional per species.
	 *
	 * @param OutVariant  Which set actually answered. None when nothing did.
	 */
	UFUNCTION(BlueprintPure, Category = "Visuals|Animations")
	UPaperFlipbook* GetSideAnimation(EGF_CreatureAnimState State, bool bUnique, bool bFemale, EGF_CreatureAnimVariant& OutVariant) const;

	/** GetSideAnimation without the variant readback. */
	UFUNCTION(BlueprintPure, Category = "Visuals|Animations")
	UPaperFlipbook* FindSideAnimation(EGF_CreatureAnimState State, bool bUnique = false, bool bFemale = false) const;

public:
	/**
	 * Writes two extra values into this asset's registry data at save time.
	 *
	 * AssetRegistrySearchable can only tag a plain UPROPERTY -- it cannot reach inside
	 * a TMap or a TArray of structs. These two things live in exactly those places, and
	 * both are needed WITHOUT loading the species:
	 *
	 *  - BattleIdleAnimation: the Compendium preview needs one flipbook (~7.7 MB). Loading
	 *    the species to get it drags every variant set and the cry, which is a multi-second
	 *    stall per entry while browsing.
	 *  - EvolutionTargets: breeding walks the evolution chain backwards to find a base
	 *    form, which used to mean loading all 118 species to read their Evolutions array.
	 *
	 * Read back in UGF_CreatureManagerSubsystem::BuildSpeciesIndex. Values only appear in
	 * assets saved AFTER this was added -- the existing ones need a ResavePackages pass.
	 */
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	/** Tag names written above. Use these rather than retyping the strings. */
	static const FName BattleIdleAnimationTagName;
	static const FName EvolutionTargetsTagName;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	USoundWave* Call;

	// Growth
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth")
	EGF_EXPCurves ExpCurve;



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth")
	float BaseEXP;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0", ClampMax = "255"))
	int32 CatchRate = 45;

	// Gender Ratio
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species", meta = (ClampMin = "0", ClampMax = "100"))
	float MaleRatio = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Species")
	bool bIsGenderless = false;

	//--------------------
	// BREEDING
	//--------------------

	// Primary breeding group. Two parents need at least one group in common to breed
	// (Shifter ignores this rule). untagged = this species can never breed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breeding")
	TArray<FName> BreedingGroups;

	// Breeds with any species that can breed at all, ignoring group overlap.
	// Replaces the old magic universal-breeder group value, which was a
	// species name masquerading as a taxonomy entry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breeding")
	bool bUniversalBreeder = false;

	// (the second group folded into the BreedingGroups array above)

	// How many egg cycles a freshly laid egg of this species starts with.
	// One cycle is burned every 256 steps, so 20 cycles = 5120 steps to hatch.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breeding", meta = (ClampMin = "1"))
	int32 EggCycles = 20;

	// Species the egg actually hatches into. Leave blank and the breeding library
	// walks the evolution chain backwards to find the lowest form automatically.
	// Only set this when the automatic answer is wrong.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breeding")
	FName BabySpeciesOverride;

	// BreedingCharm babies (classic rule): if the mother is holding CharmItem, the egg
	// hatches as CharmBabySpecies instead of this species.
	// Set on Dewpaw (Dewkit / Tide Charm) and Wardling (Wardkit / Idle Charm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breeding")
	FName CharmBabySpecies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breeding")
	FName CharmItem;

	// For the split-gender species whose two halves are separate entries:
	// the egg has a 50% chance to become this species instead.
	// Set on Barbling-F (-> Barbling-M) and Glimmara (-> Glimmer).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breeding")
	FName AlternateGenderSpecies;

	// Egg moves — moves this species can only be born knowing, inherited from
	// the father when he knows them. Not part of the level-up learnset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breeding")
	TArray<TSoftClassPtr<AGF_SkillDefinition>> EggSkills;

	// Learnable Skills
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skills")
	TArray<FGF_LearnableSkill> LearnableSkills;

	// Tome/HM Compatibility — the moves this species is ALLOWED to learn from a Tome.
	// This is separate from the level-up learnset above (LearnableSkills). Populate from the
	// classic compatibility table (see Content/Python/populate_tm_compatibility.py).
	// A move also counts as learnable if its bUniversalTM flag is set, even if not listed here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skills")
	TArray<TSoftClassPtr<AGF_SkillDefinition>> LearnableTomeMoves;


};

