// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GF_ElementTypes.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureSpeciesData.h"

// Forward declarations
class AGF_SkillDefinition;

/**
 * Battle Management Functions for Gamma Framework
 * Implements Generation 5-6 damage calculation mechanics
 */
class GAMMAFRAMEWORKBATTLE_API BattleManagementFunctions
{
public:
    
    //====================================================================================
    // MAIN DAMAGE CALCULATION
    //====================================================================================
    
    /**
     * Calculate damage dealt from attacker to defender using classic formula
     * @param Attacker - The attacking Creature's instance data
     * @param Defender - The defending Creature's instance data
     * @param Skill - The move being used
     * @param bIsCritical - Whether this is a critical hit
     * @param WeatherModifier - Weather effect (1.5 for boosted, 0.5 for reduced, 1.0 for none)
     * @param bBurnHalvesAttack - Whether attacker is burned (halves physical damage)
     * @return Final damage value
     */
    static float CalculateDamage(
        const FGF_CreatureInstanceData& Attacker,
        const FGF_CreatureInstanceData& Defender,
        AGF_SkillDefinition* Skill,
        bool bIsCritical = false,
        float WeatherModifier = 1.0f,
        bool bBurnHalvesAttack = false
    );

    //====================================================================================
    // TYPE EFFECTIVENESS
    //====================================================================================
    
    /**
     * Calculate total type effectiveness multiplier
     * Handles dual-type defenders properly (multiplies effectiveness)
     * @return Multiplier: 0 (immune), 0.25 (4x resist), 0.5 (2x resist), 1 (neutral), 2 (2x effective), 4 (4x effective)
     */
    static float GetTypeEffectiveness(
        EGF_Element SkillElement,
        EGF_Element DefenderType1,
        EGF_Element DefenderType2 = EGF_Element::None
    );

    //====================================================================================
    // STAT CALCULATIONS
    //====================================================================================
    
    /**
     * Calculate actual stat from base stat, AP, EP, level, and nature
     */
    static int32 CalculateStat(
        int32 BaseStat,
        int32 AP,
        int32 EP,
        int32 Level,
        EGF_Temperament Temperament,
        bool bIsAttack = false,
        bool bIsDefense = false,
        bool bIsMagic = false,
        bool bIsPoise = false,
        bool bIsSpeed = false,
        bool bIsHP = false
    );

    /**
     * Calculate all stats for a Creature at once
     */
    static void CalculateAllStats(
        const FGF_CreatureInstanceData& Creature,
        UGF_CreatureSpeciesData* SpeciesData,
        int32& OutHP,
        int32& OutAttack,
        int32& OutDefense,
        int32& OutMagic,
        int32& OutPoise,
        int32& OutSpeed
    );

    //====================================================================================
    // HELPER FUNCTIONS
    //====================================================================================
    
    /**
     * Get STAB (Same Type Attack Bonus) multiplier
     * @return the dual- or single-element STAB from UGF_CreatureRulesSettings
     *         (plus the Adaptability bonus), 1.0 when the element doesn't match
     */
    static float GetSTABBonus(
        EGF_Element SkillElement,
        EGF_Element CreatureType1,
        EGF_Element CreatureType2 = EGF_Element::None,
        bool bHasAdaptability = false
    );

    /**
     * Get nature modifier for a specific stat
     * @return 1.1 (boosted), 0.9 (reduced), or 1.0 (neutral)
     */
    static float GetTemperamentModifier(
        EGF_Temperament Temperament,
        bool bIsAttack,
        bool bIsDefense,
        bool bIsMagic,
        bool bIsPoise,
        bool bIsSpeed
    );

    /**
     * Check if a type matchup results in immunity
     */
    static bool IsImmune(EGF_Element AttackType, EGF_Element DefenseType);

private:
    
    /**
     * Get single type matchup effectiveness
     * @return 0 (immune), 0.5 (not very effective), 1 (neutral), 2 (super effective)
     */
    static float GetSingleTypeMatchup(EGF_Element AttackType, EGF_Element DefenseType);
};
