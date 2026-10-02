// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CreatureStatLibrary.h"
#include "GF_CreatureSpeciesData.h"
#include "Math/UnrealMathUtility.h"

FGF_CreatureCurrentStats UGF_CreatureStatLibrary::CalculateCreatureStats(const FGF_CreatureInstanceData& CreatureData)
{
    FGF_CreatureCurrentStats Stats;

    // Check if SpeciesData is valid
    if (CreatureData.SpeciesData.IsNull())
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateCreatureStats: SpeciesData is null"));
        return Stats;
    }

    // Load the species data if it's a soft reference
    UGF_CreatureSpeciesData* SpeciesData = CreatureData.SpeciesData.LoadSynchronous();
    if (!SpeciesData)
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateCreatureStats: Failed to load SpeciesData"));
        return Stats;
    }

    const FGF_CreatureBaseStats& BaseStats = SpeciesData->BaseStats;

    // Set Level
    Stats.Level = CreatureData.Level;

    // Calculate HP (both MaxHP and CurrentHP)
    int32 CalculatedHP = CalculateHP(
        BaseStats.HP,
        CreatureData.HP_AP,
        CreatureData.HP_EP,
        CreatureData.Level
    );
    // Husk: see UGF_CreatureSpeciesData::HasFixedOneHP.
    Stats.MaxHP = SpeciesData->HasFixedOneHP() ? 1.f : static_cast<float>(CalculatedHP);
    Stats.CurrentHP = CreatureData.CurrentHP; // Keep actual current HP from instance data

    // Calculate Attack
    Stats.Attack = static_cast<float>(CalculateStat(
        BaseStats.Attack,
        CreatureData.Attack_AP,
        CreatureData.Attack_EP,
        CreatureData.Level,
        CreatureData.Temperament,
        true, false, false, false, false // bIsAttack
    ));

    // Calculate Defense
    Stats.Defense = static_cast<float>(CalculateStat(
        BaseStats.Defense,
        CreatureData.Defense_AP,
        CreatureData.Defense_EP,
        CreatureData.Level,
        CreatureData.Temperament,
        false, true, false, false, false // bIsDefense
    ));

    // Calculate Special Attack
    Stats.Magic = static_cast<float>(CalculateStat(
        BaseStats.Magic,
        CreatureData.Magic_AP,
        CreatureData.Magic_EP,
        CreatureData.Level,
        CreatureData.Temperament,
        false, false, true, false, false // bIsMagic
    ));

    // Calculate Special Defense
    Stats.Poise = static_cast<float>(CalculateStat(
        BaseStats.Poise,
        CreatureData.Poise_AP,
        CreatureData.Poise_EP,
        CreatureData.Level,
        CreatureData.Temperament,
        false, false, false, true, false // bIsPoise
    ));

    // Calculate Speed
    Stats.Speed = static_cast<float>(CalculateStat(
        BaseStats.Speed,
        CreatureData.Speed_AP,
        CreatureData.Speed_EP,
        CreatureData.Level,
        CreatureData.Temperament,
        false, false, false, false, true // bIsSpeed
    ));

    return Stats;
}

int32 UGF_CreatureStatLibrary::CalculateHP(int32 BaseHP, int32 AP, int32 EP, int32 Level)
{
    // HP Formula: floor(((2 × Base + AP + EP) × Level) / 100) + Level + 10

    // Clamp inputs to valid ranges
    AP = FMath::Clamp(AP, 0, FGF_CreatureInstanceData::MaxAP);
    EP = FMath::Max(EP, 0);
    Level = FMath::Clamp(Level, 1, 100);

    float BasePart = (2.0f * BaseHP + AP + EP) * Level;
    int32 HP = FMath::FloorToInt(BasePart / 100.0f) + Level + 10;

    return FMath::Max(1, HP); // Minimum 1 HP
}

int32 UGF_CreatureStatLibrary::CalculateStat(
    int32 BaseStat,
    int32 AP,
    int32 EP,
    int32 Level,
    EGF_Temperament Temperament,
    bool bIsAttack,
    bool bIsDefense,
    bool bIsMagic,
    bool bIsPoise,
    bool bIsSpeed)
{
    // Stat Formula: floor((floor(((2 × Base + AP + EP) × Level) / 100) + 5) × Temperament)

    // Clamp inputs to valid ranges
    AP = FMath::Clamp(AP, 0, FGF_CreatureInstanceData::MaxAP);
    EP = FMath::Max(EP, 0);
    Level = FMath::Clamp(Level, 1, 100);

    // Calculate base stat before nature
    float BasePart = (2.0f * BaseStat + AP + EP) * Level;
    int32 BaseStatCalc = FMath::FloorToInt(BasePart / 100.0f) + 5;

    // Apply nature modifier
    float TemperamentModifier = GetTemperamentModifier(Temperament, bIsAttack, bIsDefense, bIsMagic, bIsPoise, bIsSpeed);

    int32 FinalStat = FMath::FloorToInt(BaseStatCalc * TemperamentModifier);
    return FMath::Max(1, FinalStat); // Minimum 1
}

float UGF_CreatureStatLibrary::GetTemperamentModifier(
    EGF_Temperament Temperament,
    bool bIsAttack,
    bool bIsDefense,
    bool bIsMagic,
    bool bIsPoise,
    bool bIsSpeed)
{
    // Temperament modifiers: +10% to one stat, -10% to another
    // Neutral natures don't modify anything

    switch (Temperament)
    {
        // Attack boosting natures (+Atk)
        case EGF_Temperament::Ferocious:  return bIsAttack ? 1.1f : (bIsMagic ? 0.9f : 1.0f);  // +Atk, -SpA
        case EGF_Temperament::Valiant:    return bIsAttack ? 1.1f : (bIsSpeed ? 0.9f : 1.0f);          // +Atk, -Spe
        case EGF_Temperament::Solitary:   return bIsAttack ? 1.1f : (bIsDefense ? 0.9f : 1.0f);        // +Atk, -Def
        case EGF_Temperament::Unruly:  return bIsAttack ? 1.1f : (bIsPoise ? 0.9f : 1.0f); // +Atk, -SpD

        // Defense boosting natures (+Def)
        case EGF_Temperament::Stalwart:     return bIsDefense ? 1.1f : (bIsAttack ? 0.9f : 1.0f);        // +Def, -Atk
        case EGF_Temperament::Wily:   return bIsDefense ? 1.1f : (bIsMagic ? 0.9f : 1.0f); // +Def, -SpA
        case EGF_Temperament::Slack:      return bIsDefense ? 1.1f : (bIsPoise ? 0.9f : 1.0f);// +Def, -SpD
        case EGF_Temperament::Languid:  return bIsDefense ? 1.1f : (bIsSpeed ? 0.9f : 1.0f);         // +Def, -Spe

        // Magic boosting natures (+SpA)
        case EGF_Temperament::Temperate:     return bIsMagic ? 1.1f : (bIsDefense ? 0.9f : 1.0f);        // +SpA, -Def
        case EGF_Temperament::Humble:   return bIsMagic ? 1.1f : (bIsAttack ? 0.9f : 1.0f);         // +SpA, -Atk
        case EGF_Temperament::Silent:    return bIsMagic ? 1.1f : (bIsSpeed ? 0.9f : 1.0f);          // +SpA, -Spe
        case EGF_Temperament::Reckless:     return bIsMagic ? 1.1f : (bIsPoise ? 0.9f : 1.0f); // +SpA, -SpD

        // Poise boosting natures (+SpD)
        case EGF_Temperament::Serene:     return bIsPoise ? 1.1f : (bIsAttack ? 0.9f : 1.0f);        // +SpD, -Atk
        case EGF_Temperament::Guarded:  return bIsPoise ? 1.1f : (bIsMagic ? 0.9f : 1.0f); // +SpD, -SpA
        case EGF_Temperament::Tender:   return bIsPoise ? 1.1f : (bIsDefense ? 0.9f : 1.0f);       // +SpD, -Def
        case EGF_Temperament::Brash:    return bIsPoise ? 1.1f : (bIsSpeed ? 0.9f : 1.0f);         // +SpD, -Spe

        // Speed boosting natures (+Spe)
        case EGF_Temperament::Rushed:    return bIsSpeed ? 1.1f : (bIsDefense ? 0.9f : 1.0f);        // +Spe, -Def
        case EGF_Temperament::Sprightly:    return bIsSpeed ? 1.1f : (bIsMagic ? 0.9f : 1.0f);  // +Spe, -SpA
        case EGF_Temperament::Innocent:    return bIsSpeed ? 1.1f : (bIsPoise ? 0.9f : 1.0f); // +Spe, -SpD
        case EGF_Temperament::Skittish:    return bIsSpeed ? 1.1f : (bIsAttack ? 0.9f : 1.0f);         // +Spe, -Atk

        // Neutral natures (no modifiers)
        case EGF_Temperament::Meek:
        case EGF_Temperament::Placid:
        case EGF_Temperament::Robust:
        case EGF_Temperament::Peculiar:
        case EGF_Temperament::Stoic:
        default:
            return 1.0f;
    }
}

FString UGF_CreatureStatLibrary::GetTemperamentBoostedStat(EGF_Temperament Temperament)
{
    switch (Temperament)
    {
        // Attack boosting
        case EGF_Temperament::Ferocious:
        case EGF_Temperament::Valiant:
        case EGF_Temperament::Solitary:
        case EGF_Temperament::Unruly:
            return TEXT("Attack");

        // Defense boosting
        case EGF_Temperament::Stalwart:
        case EGF_Temperament::Wily:
        case EGF_Temperament::Slack:
        case EGF_Temperament::Languid:
            return TEXT("Defense");

        // Magic boosting
        case EGF_Temperament::Temperate:
        case EGF_Temperament::Humble:
        case EGF_Temperament::Silent:
        case EGF_Temperament::Reckless:
            return TEXT("Magic");

        // Poise boosting
        case EGF_Temperament::Serene:
        case EGF_Temperament::Guarded:
        case EGF_Temperament::Tender:
        case EGF_Temperament::Brash:
            return TEXT("Poise");

        // Speed boosting
        case EGF_Temperament::Rushed:
        case EGF_Temperament::Sprightly:
        case EGF_Temperament::Innocent:
        case EGF_Temperament::Skittish:
            return TEXT("Speed");

        // Neutral natures
        default:
            return TEXT("None");
    }
}

FString UGF_CreatureStatLibrary::GetTemperamentHinderedStat(EGF_Temperament Temperament)
{
    switch (Temperament)
    {
        // Attack hindering
        case EGF_Temperament::Stalwart:
        case EGF_Temperament::Serene:
        case EGF_Temperament::Humble:
        case EGF_Temperament::Skittish:
            return TEXT("Attack");

        // Defense hindering
        case EGF_Temperament::Tender:
        case EGF_Temperament::Rushed:
        case EGF_Temperament::Solitary:
        case EGF_Temperament::Temperate:
            return TEXT("Defense");

        // Magic hindering
        case EGF_Temperament::Ferocious:
        case EGF_Temperament::Guarded:
        case EGF_Temperament::Wily:
        case EGF_Temperament::Sprightly:
            return TEXT("Magic");

        // Poise hindering
        case EGF_Temperament::Slack:
        case EGF_Temperament::Unruly:
        case EGF_Temperament::Reckless:
        case EGF_Temperament::Innocent:
            return TEXT("Poise");

        // Speed hindering
        case EGF_Temperament::Valiant:
        case EGF_Temperament::Silent:
        case EGF_Temperament::Languid:
        case EGF_Temperament::Brash:
            return TEXT("Speed");

        // Neutral natures
        default:
            return TEXT("None");
    }
}