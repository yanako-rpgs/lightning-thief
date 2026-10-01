// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_SanctuaryTypes.h"
#include "GF_ElementTypes.h"
#include "GF_BreedingLibrary.generated.h"

class UGF_CreatureManagerSubsystem;

/**
 * Pure breeding rules — breeding group compatibility and how an egg is built from its
 * two parents. Kept separate from UGF_SanctuarySubsystem so the same rules can be
 * reused anywhere (debug menu, a second sanctuary, a breeding-preview UI) without
 * dragging the sanctuary's save state along.
 *
 * Everything here follows the classic rules unless a comment says
 * otherwise. The one deliberate deviation is Potential inheritance: the original has a
 * well-known bug where it can pick the same stat twice, and this picks three
 * genuinely distinct stats instead.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_BreedingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Held item that lets the mother pass her nature down. Must match the ItemName on the ItemData asset. */
	static const FName AnchorStoneItemName;

	/** Key item that adds unique rolls to every hatch. Must match the ItemName on the ItemData asset. */
	static const FName UniqueCharmItemName;

	/**
	 * How many unique rolls an egg with these parents gets, before the Unique Charm.
	 * Exposed so debug UI and the sanctuary screen can show the player their odds.
	 */
	UFUNCTION(BlueprintPure, Category = "Breeding|Unique")
	static int32 GetForeignPairUniqueRolls(const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B);

	/** Rolls this egg would get right now, Unique Charm included. 1 roll = 1/3500. */
	UFUNCTION(BlueprintPure, Category = "Breeding|Unique", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEggUniqueRolls(const UObject* WorldContextObject, const FGF_CreatureInstanceData& Egg);

	//--------------------
	// COMPATIBILITY
	//--------------------

	/** True if the two species share at least one breeding group. Shifter is not handled here. */
	UFUNCTION(BlueprintPure, Category = "Creature|Breeding")
	static bool DoBreedingGroupsOverlap(const UGF_CreatureSpeciesData* A, const UGF_CreatureSpeciesData* B);

	/** True if this species belongs to the Shifter group. */
	UFUNCTION(BlueprintPure, Category = "Creature|Breeding")
	static bool IsUniversalBreeder(const UGF_CreatureSpeciesData* Species);

	/** True if this species can never breed (no breeding tags, or no groups set). */
	UFUNCTION(BlueprintPure, Category = "Creature|Breeding")
	static bool IsUnbreedable(const UGF_CreatureSpeciesData* Species);

	/**
	 * The classic compatibility score for a pair. This is the whole "do they like
	 * each other" answer — the value doubles as the percent chance of an egg
	 * appearing per 256 steps.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Breeding")
	static EGF_SanctuaryCompatibility GetCompatibility(const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B);

	/** The compatibility as a raw 0-100 percent, for progress bars or debug text. */
	UFUNCTION(BlueprintPure, Category = "Creature|Breeding")
	static int32 GetCompatibilityChance(EGF_SanctuaryCompatibility Compatibility);

	/**
	 * The line the Day-Care Man says when you talk to him outside, matching the
	 * four classic responses. Use this verbatim for the "do they like each other" NPC.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Breeding")
	static FText GetCompatibilityText(EGF_SanctuaryCompatibility Compatibility);

	/** Rolls the per-256-step egg check: an egg appears if Compatibility > Random(0-99). */
	UFUNCTION(BlueprintCallable, Category = "Creature|Breeding")
	static bool RollForEgg(EGF_SanctuaryCompatibility Compatibility);

	//--------------------
	// EGG SPECIES RESOLUTION
	//--------------------

	/**
	 * Walks the evolution chain backwards to find the lowest form of a species —
	 * breed a Rivergaunt and you get a Rivulet egg. Respects BabySpeciesOverride when
	 * the automatic answer is wrong. Never returns null for a valid input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Breeding", meta = (WorldContext = "WorldContextObject"))
	static UGF_CreatureSpeciesData* GetBaseEggSpecies(const UObject* WorldContextObject, UGF_CreatureSpeciesData* Species);

	/**
	 * Full egg-species resolution for a pair: base form of the mother, then the
	 * incense swap (Dewpaw -> Dewkit etc.) and the split-gender roll
	 * (Barbling-F -> Barbling-M, Glimmara -> Glimmer).
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Breeding", meta = (WorldContext = "WorldContextObject"))
	static UGF_CreatureSpeciesData* ResolveEggSpecies(const UObject* WorldContextObject,
		const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B);

	/**
	 * Which of the two parents supplies the species (mother, or the non-Shifter parent),
	 * and which supplies egg moves (father, or the non-Shifter parent when a Shifter is
	 * involved — that's why Shifter is the standard egg-move partner).
	 * Returns false if the pair is incompatible.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Breeding")
	static bool IdentifyParentRoles(const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B,
		int32& OutMotherIndex, int32& OutSkillParentIndex);

	//--------------------
	// EGG CREATION
	//--------------------

	/**
	 * Builds the actual egg from two parents: species, inherited Potentials, inherited
	 * nature (AnchorStone), and the four-stage egg moveset. The result is a level 5
	 * Creature with bIsEgg set and its egg cycles primed.
	 *
	 * Returns an invalid (empty SpeciesData) instance if the pair can't breed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Breeding", meta = (WorldContext = "WorldContextObject"))
	static FGF_CreatureInstanceData BuildEgg(const UObject* WorldContextObject,
		const FGF_CreatureInstanceData& A, const FGF_CreatureInstanceData& B,
		FName OTName, int32 OTID);

	/**
	 * Builds a GIFT egg of a specific species, with no parents involved.
	 *
	 * This deliberately ignores every breeding rule — breeding groups, compatibility,
	 * the no breeding tags. That's the point: a gift egg is handed over by an NPC
	 * or an event, so Bloomsprite, Aurelis and other unbreedable species are all fair game.
	 * BuildEgg would refuse them.
	 *
	 * Potentials and nature are rolled fresh (nothing to inherit from). The Creature inside
	 * knows whatever it naturally would at level 5, unless you pass StartingSkills.
	 *
	 * @param Species            What's inside. Pass the species itself, not a baby form —
	 *                           no evolution-chain walk happens here, so a Bloomsprite egg
	 *                           hatches a Bloomsprite.
	 * @param EggCyclesOverride  Steps-to-hatch in egg cycles. -1 uses the species value.
	 *                           Set it low (1-5) for a gift you want hatching quickly.
	 * @param UniqueRolls         Unique chances at hatch. 1 = normal, higher = kinder.
	 * @param EggAppearanceType  Which egg graphic to show. None resolves to the
	 *                           species' primary type via GetEggAppearanceType.
	 * @param StartingSkills      Optional explicit moveset, for a gift with a special move.
	 *                           LEAVE THE PIN UNCONNECTED for the natural level-5 moveset —
	 *                           AutoCreateRefTerm supplies an empty array.
	 *                           Do NOT wire a MakeArray with a blank entry: that's an array
	 *                           of length 1 containing null, which makes Initialize take the
	 *                           explicit-moveset branch, discard the null, and skip the
	 *                           natural learnset entirely — the Creature hatches with no moves.
	 *
	 * Returns an invalid (empty SpeciesData) instance if Species is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Breeding|Gift", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "StartingSkills", AdvancedDisplay = "4"))
	static FGF_CreatureInstanceData BuildGiftEgg(const UObject* WorldContextObject,
		UGF_CreatureSpeciesData* Species,
		FName OTName, int32 OTID,
		const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills,
		int32 EggCyclesOverride = -1,
		int32 UniqueRolls = 1,
		EGF_Element EggAppearanceType = EGF_Element::None);

	/**
	 * Which egg graphic this egg should use. Returns the explicit EggAppearanceType
	 * when one was set, otherwise the species' primary type. Feed the result into
	 * your egg icon lookup so one call covers both bred and gift eggs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Breeding|Gift", meta = (WorldContext = "WorldContextObject"))
	static EGF_Element GetEggAppearanceType(const UObject* WorldContextObject, const FGF_CreatureInstanceData& Egg);

	/**
	 * Turns an egg into the Creature inside it: clears the egg flags, restores the
	 * real display name, sets bond to the classic hatch value of 120 and fills
	 * in HP. Returns false if the entry wasn't an egg.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Breeding", meta = (WorldContext = "WorldContextObject"))
	static bool HatchEggInstance(const UObject* WorldContextObject, UPARAM(ref) FGF_CreatureInstanceData& Egg);

private:
	/** Resolve the manager subsystem from any world context object. */
	static UGF_CreatureManagerSubsystem* GetManager(const UObject* WorldContextObject);

	/** Three distinct stats inherited from a random parent each; the rest rolled fresh. */
	static void InheritPotentials(const FGF_CreatureInstanceData& Mother, const FGF_CreatureInstanceData& Father,
		FGF_CreatureInstanceData& Egg);

	/** classic AnchorStone rule: mother holding an AnchorStone has a 50% chance to pass her nature. */
	static void InheritTemperament(const FGF_CreatureInstanceData& Mother, FGF_CreatureInstanceData& Egg);

	/**
	 * The four inheritance passes, applied in classic's order on top of the baby's
	 * natural level-1 moves:
	 *   1. egg moves the move-parent knows that are in the baby's EggSkills list
	 *   2. Tome moves the move-parent knows that the baby is allowed to learn
	 *   3. moves BOTH parents know that appear in the baby's level-up learnset
	 * When the moveset is already full, the oldest move (slot 0) is pushed out.
	 */
	static void BuildEggSkillset(const FGF_CreatureInstanceData& SkillParent, const FGF_CreatureInstanceData& OtherParent,
		const UGF_CreatureSpeciesData* BabySpecies, FGF_CreatureInstanceData& Egg);

	/** Append a move, pushing out slot 0 if all four slots are taken. Ignores duplicates. */
	static void PushEggSkill(FGF_CreatureInstanceData& Egg, const TSoftClassPtr<AGF_SkillDefinition>& Skill);
};
