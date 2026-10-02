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
 * - APs (rolled at birth, grow with affinity, max 50)
 * - EPs (1 per level, freely allocated; each EP counts like one AP)
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
     * HP = floor(((2 * Base + AP + EP) * Level) / 100) + Level + 10
     */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 CalculateHP(int32 BaseHP, int32 AP, int32 EP, int32 Level);

    /**
     * Calculate a single stat (Attack, Defense, etc.)
     * Stat = floor((floor(((2 * Base + AP + EP) * Level) / 100) + 5) * Temperament)
     *
     * @param BaseStat - Base stat value from species
     * @param AP - AP for this stat (0-50)
     * @param EP - EPs allocated to this stat
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
        int32 AP,
        int32 EP,
        int32 Level,
        EGF_Temperament Temperament,
        bool bIsAttack,
        bool bIsDefense,
        bool bIsMagic,
        bool bIsPoise,
        bool bIsSpeed
    );

    // --------------------------------------------------------
    // APs / EPs / AFFINITY (read-only helpers for the info screen;
    // change them through UGF_CreatureManagerSubsystem)
    // --------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 GetCreatureAP(const FGF_CreatureInstanceData& CreatureData, EGF_CreatureStat Stat) { return CreatureData.GetAP(Stat); }

    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 GetCreatureEP(const FGF_CreatureInstanceData& CreatureData, EGF_CreatureStat Stat) { return CreatureData.GetEP(Stat); }

    /** EPs earned so far: EPPerLevel x Level. */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 GetEPBudget(const FGF_CreatureInstanceData& CreatureData) { return CreatureData.GetEPBudget(); }

    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 GetUnspentEP(const FGF_CreatureInstanceData& CreatureData) { return CreatureData.GetUnspentEP(); }

    /** True at max affinity -- show the heart on the first info page. */
    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static bool IsMaxAffinity(const FGF_CreatureInstanceData& CreatureData) { return CreatureData.IsMaxAffinity(); }

    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 GetMaxAP() { return FGF_CreatureInstanceData::MaxAP; }

    UFUNCTION(BlueprintPure, Category = "Creature|Stats")
    static int32 GetMaxAffinity() { return FGF_CreatureInstanceData::MaxAffinity; }

    /** Move slots open at the Creature's level (2, 3 from Lv 7, 4 from Lv 12 by default). */
    UFUNCTION(BlueprintPure, Category = "Creature|Skills")
    static int32 GetSkillSlotCount(const FGF_CreatureInstanceData& CreatureData) { return CreatureData.GetSkillSlotCount(); }

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