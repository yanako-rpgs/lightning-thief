// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureInstanceData.h"
#include "GF_Creature.h"  // Use existing FGF_CreatureCurrentStats from Creature.h
#include "GF_CreatureStatLibrary.generated.h"

/**
 * Blueprint library for calculating Creature stats
 * Uses classic stat formulas
 *
 * Stats are calculated from:
 * - Base Stats (from species)
 * - Potentials (Individual Values)
 * - Training (Effort Values)
 * - Level
 * - Temperament modifiers
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureStatLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Calculate all current stats for a Creature
     * This is deterministic - same inputs always give same outputs
     *
     * @param CreatureData - The Creature instance data
     * @return Struct containing all calculated stats
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats", meta = (CompactNodeTitle = "Calculate Stats"))
    static FGF_CreatureCurrentStats CalculateCreatureStats(const FGF_CreatureInstanceData& CreatureData);

    /**
     * Calculate HP stat using classic formula
     * HP = floor(((2 � Base + Potential + floor(TrainingValue/4)) � Level) / 100) + Level + 10
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 CalculateHP(int32 BaseHP, int32 Potential, int32 TrainingValue, int32 Level);

    /**
     * Calculate a single stat (Attack, Defense, etc.) using classic formula
     * Stat = floor((floor(((2 � Base + Potential + floor(TrainingValue/4)) � Level) / 100) + 5) � Temperament)
     *
     * @param BaseStat - Base stat value from species
     * @param Potential - Individual Value (0-31)
     * @param TrainingValue - Effort Value (0-252)
     * @param Level - Creature's level
     * @param Temperament - Creature's nature
     * @param bIsAttack - Set to true if calculating Attack stat
     * @param bIsDefense - Set to true if calculating Defense stat
     * @param bIsMagic - Set to true if calculating Special Attack stat
     * @param bIsPoise - Set to true if calculating Special Defense stat
     * @param bIsSpeed - Set to true if calculating Speed stat
     * @return The calculated stat value
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 CalculateStat(
        int32 BaseStat,
        int32 Potential,
        int32 TrainingValue,
        int32 Level,
        EGF_Temperament Temperament,
        bool bIsAttack,
        bool bIsDefense,
        bool bIsMagic,
        bool bIsPoise,
        bool bIsSpeed
    );

    /**
     * Get nature modifier for a specific stat
     *
     * @return 1.1 for boosted stat, 0.9 for hindered stat, 1.0 for neutral
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static float GetTemperamentModifier(
        EGF_Temperament Temperament,
        bool bIsAttack,
        bool bIsDefense,
        bool bIsMagic,
        bool bIsPoise,
        bool bIsSpeed
    );

    /**
     * Get the name of the stat that this nature boosts
     * Returns "None" for neutral natures
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static FString GetTemperamentBoostedStat(EGF_Temperament Temperament);

    /**
     * Get the name of the stat that this nature hinders
     * Returns "None" for neutral natures
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static FString GetTemperamentHinderedStat(EGF_Temperament Temperament);
};