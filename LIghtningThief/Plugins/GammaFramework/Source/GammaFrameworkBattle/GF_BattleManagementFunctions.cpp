// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_BattleManagementFunctions.h"
#include "GF_ElementTypes.h"
#include "GF_SkillDefinition.h"
#include "Math/UnrealMathUtility.h"

//====================================================================================
// MAIN DAMAGE CALCULATION - GEN 5-6 FORMULA
//====================================================================================

float BattleManagementFunctions::CalculateDamage(const FGF_CreatureInstanceData& Attacker, const FGF_CreatureInstanceData& Defender,AGF_SkillDefinition* Skill,bool bIsCritical, float WeatherModifier, bool bBurnHalvesAttack)
{
    if (!Skill)
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateDamage: Skill is null!"));
        return 0.0f;
    }

    // Load species data
    UGF_CreatureSpeciesData* AttackerSpecies = Attacker.SpeciesData.LoadSynchronous();
    UGF_CreatureSpeciesData* DefenderSpecies = Defender.SpeciesData.LoadSynchronous();

    if (!AttackerSpecies || !DefenderSpecies)
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateDamage: Failed to load species data!"));
        return 0.0f;
    }

    // Calculate actual stats
    int32 AttackerHP, AttackerAtk, AttackerDef, AttackerMagic, AttackerPoise, AttackerSpeed;
    int32 DefenderHP, DefenderAtk, DefenderDef, DefenderMagic, DefenderPoise, DefenderSpeed;

    CalculateAllStats(Attacker, AttackerSpecies, AttackerHP, AttackerAtk, AttackerDef, AttackerMagic, AttackerPoise, AttackerSpeed);
    CalculateAllStats(Defender, DefenderSpecies, DefenderHP, DefenderAtk, DefenderDef, DefenderMagic, DefenderPoise, DefenderSpeed);

    // Determine if move is physical or special (you'll need to add this to AGF_SkillDefinition)
    // For now, assuming you have a boolean or enum on the move
    bool bIsPhysical = true; // TODO: Get from Skill->bIsPhysical or Skill->Category

    // Get appropriate offensive and defensive stats
    float A = bIsPhysical ? (float)AttackerAtk : (float)AttackerMagic;
    float D = bIsPhysical ? (float)DefenderDef : (float)DefenderPoise;

    // Get move power (you'll need to add this to AGF_SkillDefinition)
    float Power = Skill->Power;

    int32 Level = Attacker.Level;

    //====================================================================================
    // BASE DAMAGE CALCULATION: ((2×Level/5+2)×Power×A/D/50+2)
    //====================================================================================

    if (Skill->Name == FName("Acrobatics") && Attacker.HasHeldItem())
    {

	    Power *= 2;

        UE_LOG(LogTemp, Log, TEXT("Power is: %f And it was multiplied"), Power);
    }

    float BaseDamage = ((2.0f * Level / 5.0f + 2.0f) * Power * A / D / 50.0f + 2.0f);
    BaseDamage = FMath::FloorToFloat(BaseDamage);

    //====================================================================================
    // APPLY MODIFIERS IN ORDER
    //====================================================================================

    // 1. Targets modifier (0.75 for multi-target moves in doubles/triples, 1.0 for single target)
    // TODO: Implement if you support double battles
    float Targets = 1.0f;

    // 2. Weather modifier (1.5 for boosted, 0.5 for reduced)
    float Weather = WeatherModifier;

    // 3. Critical hit (1.5x in classic+, 2.0x in classic)
    float Critical = bIsCritical ? 1.5f : 1.0f;

    // 4. Random factor (0.85 to 1.0)
    float Random = FMath::FRandRange(0.85f, 1.0f);

    // 5. STAB (Same Type Attack Bonus)
    EGF_Element SkillElement = EGF_Element::Neutral; // TODO: Get from Skill->Type
    float STAB = GetSTABBonus(SkillElement, AttackerSpecies->PrimaryElement, AttackerSpecies->SecondaryElement, false);

    // 6. Type Effectiveness (0, 0.25, 0.5, 1, 2, or 4)
    float Type = GetTypeEffectiveness(SkillElement, DefenderSpecies->PrimaryElement, DefenderSpecies->SecondaryElement);

    // 7. Burn modifier (0.5 if burned and physical move, 1.0 otherwise)
    float Burn = (bBurnHalvesAttack && bIsPhysical) ? 0.5f : 1.0f;

    // 8. Other modifiers (Reflect, Veil, traits, items, etc.)
    float Other = 1.0f;
    // TODO: Implement Reflect/Veil and other modifiers as needed

    //====================================================================================
    // FINAL CALCULATION
    //====================================================================================

    float FinalDamage = BaseDamage * Targets * Weather * Critical * Random * STAB * Type * Burn * Other;

    // Round to nearest integer (rounding down at 0.5 per classic rules)
    FinalDamage = FMath::FloorToFloat(FinalDamage + 0.5f);

    // Minimum damage of 1 (unless immune)
    if (FinalDamage < 1.0f && Type > 0.0f)
    {
        FinalDamage = 1.0f;
    }

    // Logging for debugging
    UE_LOG(LogTemp, Log, TEXT("=== DAMAGE CALCULATION ==="));
    UE_LOG(LogTemp, Log, TEXT("Base Damage: %.2f"), BaseDamage);
    UE_LOG(LogTemp, Log, TEXT("Critical: %.2f, Random: %.2f, STAB: %.2f"), Critical, Random, STAB);
    UE_LOG(LogTemp, Log, TEXT("Type Effectiveness: %.2f"), Type);
    UE_LOG(LogTemp, Log, TEXT("Final Damage: %.2f"), FinalDamage);

    return FinalDamage;
}

//====================================================================================
// TYPE EFFECTIVENESS SYSTEM
//====================================================================================

float BattleManagementFunctions::GetTypeEffectiveness(
    EGF_Element SkillElement,
    EGF_Element DefenderType1,
    EGF_Element DefenderType2)
{
    return UGF_ElementLibrary::GetEffectiveness(SkillElement, DefenderType1, DefenderType2);
}

float BattleManagementFunctions::GetSingleTypeMatchup(EGF_Element AttackType, EGF_Element DefenseType)
{
    return UGF_ElementLibrary::GetMatchup(AttackType, DefenseType);
}

bool BattleManagementFunctions::IsImmune(EGF_Element AttackType, EGF_Element DefenseType)
{
    return GetSingleTypeMatchup(AttackType, DefenseType) == 0.0f;
}

//====================================================================================
// STAT CALCULATIONS
//====================================================================================

int32 BattleManagementFunctions::CalculateStat(
    int32 BaseStat,
    int32 Potential,
    int32 TrainingValue,
    int32 Level,
    EGF_Temperament Temperament,
    bool bIsAttack,
    bool bIsDefense,
    bool bIsMagic,
    bool bIsPoise,
    bool bIsSpeed,
    bool bIsHP)
{
    // Clamp Potential and TrainingValue values
    Potential = FMath::Clamp(Potential, 0, 31);
    TrainingValue = FMath::Clamp(TrainingValue, 0, 255);

    if (bIsHP)
    {
        // HP formula: ((2 * Base + Potential + (TrainingValue / 4)) * Level / 100) + Level + 10
        int32 HP = ((2 * BaseStat + Potential + (TrainingValue / 4)) * Level / 100) + Level + 10;
        return HP;
    }
    else
    {
        // Other stats formula: (((2 * Base + Potential + (TrainingValue / 4)) * Level / 100) + 5) * Temperament
        int32 Stat = ((2 * BaseStat + Potential + (TrainingValue / 4)) * Level / 100) + 5;

        // Apply nature modifier
        float TemperamentMod = GetTemperamentModifier(Temperament, bIsAttack, bIsDefense, bIsMagic, bIsPoise, bIsSpeed);
        Stat = FMath::FloorToInt(Stat * TemperamentMod);

        return Stat;
    }
}

void BattleManagementFunctions::CalculateAllStats(
    const FGF_CreatureInstanceData& Creature,
    UGF_CreatureSpeciesData* SpeciesData,
    int32& OutHP,
    int32& OutAttack,
    int32& OutDefense,
    int32& OutMagic,
    int32& OutPoise,
    int32& OutSpeed)
{
    if (!SpeciesData)
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateAllStats: SpeciesData is null!"));
        OutHP = 100;
        OutAttack = OutDefense = OutMagic = OutPoise = OutSpeed = 50;
        return;
    }

    const FGF_CreatureBaseStats& BaseStats = SpeciesData->BaseStats;
    int32 Level = Creature.Level;
    EGF_Temperament Temperament = Creature.Temperament;

    OutHP = CalculateStat(BaseStats.HP, Creature.HP_Potential, Creature.HP_Training, Level, Temperament, false, false, false, false, false, true);
    OutAttack = CalculateStat(BaseStats.Attack, Creature.Attack_Potential, Creature.Attack_Training, Level, Temperament, true, false, false, false, false, false);
    OutDefense = CalculateStat(BaseStats.Defense, Creature.Defense_Potential, Creature.Defense_Training, Level, Temperament, false, true, false, false, false, false);
    OutMagic = CalculateStat(BaseStats.Magic, Creature.Magic_Potential, Creature.Magic_Training, Level, Temperament, false, false, true, false, false, false);
    OutPoise = CalculateStat(BaseStats.Poise, Creature.Poise_Potential, Creature.Poise_Training, Level, Temperament, false, false, false, true, false, false);
    OutSpeed = CalculateStat(BaseStats.Speed, Creature.Speed_Potential, Creature.Speed_Training, Level, Temperament, false, false, false, false, true, false);

    UE_LOG(LogTemp, Log, TEXT("Stats calculated for %s (Level %d):"), *Creature.GetDisplayName().ToString(), Level);
    UE_LOG(LogTemp, Log, TEXT("  HP: %d, Atk: %d, Def: %d, Magic: %d, Poise: %d, Speed: %d"),
        OutHP, OutAttack, OutDefense, OutMagic, OutPoise, OutSpeed);
}

//====================================================================================
// HELPER FUNCTIONS
//====================================================================================

float BattleManagementFunctions::GetSTABBonus(
    EGF_Element SkillElement,
    EGF_Element CreatureType1,
    EGF_Element CreatureType2,
    bool bHasAdaptability)
{
    // Check if move type matches either of the Creature's types
    bool bHasSTAB = (SkillElement == CreatureType1) || (SkillElement == CreatureType2);

    if (!bHasSTAB)
    {
        return 1.0f;
    }

    // STAB is 2.0 with Adaptability, 1.5 normally
    return bHasAdaptability ? 2.0f : 1.5f;
}

float BattleManagementFunctions::GetTemperamentModifier(
    EGF_Temperament Temperament,
    bool bIsAttack,
    bool bIsDefense,
    bool bIsMagic,
    bool bIsPoise,
    bool bIsSpeed)
{
    //====================================================================================
    // TEMPERAMENT MODIFIERS (1.1 for boosted stat, 0.9 for reduced stat)
    //====================================================================================

    switch (Temperament)
    {
        // Hardy, Docile, Meek, Peculiar, Serious - No effect
        case EGF_Temperament::Robust:
        case EGF_Temperament::Placid:
        case EGF_Temperament::Meek:
        case EGF_Temperament::Peculiar:
        case EGF_Temperament::Stoic:
            return 1.0f;

        // Attack boosting
        case EGF_Temperament::Solitary:  // +Atk, -Def
            if (bIsAttack) return 1.1f;
            if (bIsDefense) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Valiant:   // +Atk, -Spe
            if (bIsAttack) return 1.1f;
            if (bIsSpeed) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Ferocious: // +Atk, -Magic
            if (bIsAttack) return 1.1f;
            if (bIsMagic) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Unruly: // +Atk, -Poise
            if (bIsAttack) return 1.1f;
            if (bIsPoise) return 0.9f;
            return 1.0f;

        // Defense boosting
        case EGF_Temperament::Stalwart:    // +Def, -Atk
            if (bIsDefense) return 1.1f;
            if (bIsAttack) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Languid: // +Def, -Spe
            if (bIsDefense) return 1.1f;
            if (bIsSpeed) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Wily:  // +Def, -Magic
            if (bIsDefense) return 1.1f;
            if (bIsMagic) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Slack:     // +Def, -Poise
            if (bIsDefense) return 1.1f;
            if (bIsPoise) return 0.9f;
            return 1.0f;

        // Speed boosting
        case EGF_Temperament::Skittish:   // +Spe, -Atk
            if (bIsSpeed) return 1.1f;
            if (bIsAttack) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Rushed:   // +Spe, -Def
            if (bIsSpeed) return 1.1f;
            if (bIsDefense) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Sprightly:   // +Spe, -Magic
            if (bIsSpeed) return 1.1f;
            if (bIsMagic) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Innocent:   // +Spe, -Poise
            if (bIsSpeed) return 1.1f;
            if (bIsPoise) return 0.9f;
            return 1.0f;

        // Special Attack boosting
        case EGF_Temperament::Humble:  // +Magic, -Atk
            if (bIsMagic) return 1.1f;
            if (bIsAttack) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Temperate:    // +Magic, -Def
            if (bIsMagic) return 1.1f;
            if (bIsDefense) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Silent:   // +Magic, -Spe
            if (bIsMagic) return 1.1f;
            if (bIsSpeed) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Reckless:    // +Magic, -Poise
            if (bIsMagic) return 1.1f;
            if (bIsPoise) return 0.9f;
            return 1.0f;

        // Special Defense boosting
        case EGF_Temperament::Serene:    // +Poise, -Atk
            if (bIsPoise) return 1.1f;
            if (bIsAttack) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Tender:  // +Poise, -Def
            if (bIsPoise) return 1.1f;
            if (bIsDefense) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Brash:   // +Poise, -Spe
            if (bIsPoise) return 1.1f;
            if (bIsSpeed) return 0.9f;
            return 1.0f;

        case EGF_Temperament::Guarded: // +Poise, -Magic
            if (bIsPoise) return 1.1f;
            if (bIsMagic) return 0.9f;
            return 1.0f;

        default:
            return 1.0f;
    }
}
