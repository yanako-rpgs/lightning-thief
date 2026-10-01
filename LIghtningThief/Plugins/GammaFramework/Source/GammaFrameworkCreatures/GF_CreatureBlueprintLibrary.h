// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_CreatureBlueprintLibrary.generated.h"

/**
 * Blueprint Function Library for Creature operations
 * Provides helper functions to create and initialize Creature from Blueprint
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a wild Creature instance data with random stats
	 * This is the EASIEST way to create Creature - just species and level!
	 * Automatically learns moves based on level.
	 *
	 * @param SpeciesData - The Creature species (e.g., DA_Budling)
	 * @param Level - The Creature's level (1-100)
	 * @return Fully initialized wild Creature
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature")
	static FGF_CreatureInstanceData CreateWildCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level, bool ForceUnique = false, int32 UniqueOdds = 3500, int32 EliteUniqueOdds = 7000);

	/**
	 * Create a Creature with custom starting moves
	 * Use this if you need to specify exact moves (like for starter Creature)
	 *
	 * @param SpeciesData - The Creature species
	 * @param Level - The Creature's level
	 * @param StartingSkills - Array of move classes to give the Creature
	 * @return Fully initialized Creature instance data
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature")
	static FGF_CreatureInstanceData CreateCreatureWithSkills(
		UGF_CreatureSpeciesData* SpeciesData,
		int32 Level,
		const TArray<TSubclassOf<AGF_SkillDefinition>>& StartingSkills
	);

	/**
	 * Get display name from Creature Instance Data
	 * Returns nickname if set, otherwise species name
	 *
	 * @param CreatureData - The Creature instance data struct
	 * @return Display name (nickname or species name)
	 */
	UFUNCTION(BlueprintPure, Category = "Creature", meta = (DisplayName = "Get Creature Display Name"))
	static FName GetCreatureDisplayName(const FGF_CreatureInstanceData& CreatureData);

	/**
	 * Get display icon for Creature (handles unique variants automatically)
	 * Returns Icon1 by default
	 *
	 * @param CreatureData - The Creature instance data struct
	 * @return Display icon sprite (unique if Creature is unique)
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Display", meta = (DisplayName = "Get Creature Icon"))
	static UPaperSprite* GetCreatureDisplayIcon(const FGF_CreatureInstanceData& CreatureData);

	/**
	 * Get display icon 1 for Creature (handles unique variants)
	 *
	 * @param CreatureData - The Creature instance data struct
	 * @return Display icon 1 sprite (unique if Creature is unique)
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Display", meta = (DisplayName = "Get Creature Icon 1"))
	static UPaperSprite* GetCreatureDisplayIcon1(const FGF_CreatureInstanceData& CreatureData);

	/**
	 * Get display icon 2 for Creature (handles unique variants)
	 *
	 * @param CreatureData - The Creature instance data struct
	 * @return Display icon 2 sprite (unique if Creature is unique)
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Display", meta = (DisplayName = "Get Creature Icon 2"))
	static UPaperSprite* GetCreatureDisplayIcon2(const FGF_CreatureInstanceData& CreatureData);

	/**
	 * The egg icon for an egg entry, or null if this isn't an egg.
	 *
	 * Both Get Creature Icon nodes call this first, so any screen that already asks for a
	 * Creature's icon draws an egg automatically — no per-widget IsEgg branch needed.
	 * Call it directly only when you specifically want "egg or nothing".
	 *
	 * @param Frame  0 or 1 — the two frames of the egg's bob animation.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Display", meta = (DisplayName = "Get Egg Icon"))
	static UPaperSprite* GetEggDisplayIcon(const FGF_CreatureInstanceData& CreatureData, int32 Frame = 0);

	/**
	 * Get species data from Creature instance data
	 * Loads synchronously if not already loaded
	 *
	 * @param CreatureData - The Creature instance data struct
	 * @return Loaded species data, or nullptr if invalid
	 */
	UFUNCTION(BlueprintPure, Category = "Creature", meta = (DisplayName = "Get Creature Species Data"))
	static UGF_CreatureSpeciesData* GetCreatureSpeciesData(const FGF_CreatureInstanceData& CreatureData);

	/**
	 * Check if a Creature Instance Data struct has valid data (i.e. a Creature actually occupies this slot)
	 * Use this instead of IsValid on the struct — structs don't support IsValid in Blueprint
	 *
	 * @param CreatureData - The Creature instance data struct to check
	 * @return True if the struct contains a real Creature (SpeciesData path is set)
	 */
	UFUNCTION(BlueprintPure, Category = "Creature", meta = (DisplayName = "Is Valid Creature Data"))
	static bool IsCreatureDataValid(const FGF_CreatureInstanceData& CreatureData);

	// =========================================================
	// Compendium Formatting
	// =========================================================

	/**
	 * Converts a height in meters to a Compendium-style feet/inches string.
	 * e.g. 0.5 -> "1'08\""   |   14.5 -> "47'07\""  (Leviath)
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Formatting")
	static FString MetersToFeetInches(float Meters);

	/**
	 * Converts a weight in kilograms to a Compendium-style pounds string.
	 * e.g. 2.9 -> "6.4 lbs"   |   398.0 -> "877.4 lbs"  (Leviath)
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Formatting")
	static FString KgToPounds(float Kg);

	/**
	 * Formats height for metric display.
	 * e.g. 0.5 -> "0.5 m"
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Formatting")
	static FString FormatHeightMetric(float Meters);

	/**
	 * Formats weight for metric display.
	 * e.g. 2.9 -> "2.9 kg"
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Formatting")
	static FString FormatWeightMetric(float Kg);
};
