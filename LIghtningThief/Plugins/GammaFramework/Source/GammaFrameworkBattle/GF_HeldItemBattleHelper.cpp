// Helditembattlehelper.cpp - FINAL CORRECTED VERSION
#include "GF_HeldItemBattleHelper.h"
#include "GF_ElementTypes.h"
#include "GF_ItemDataManager.h"
#include "Kismet/GameplayStatics.h"

//=============================================================================
// HELPER: Get Held Item Data
//=============================================================================

UGF_ItemData* UGF_HeldItemBattleHelper::GetCreatureHeldItemData(UObject* WorldContext, const FGF_CreatureInstanceData& CreatureData)
{
    if (CreatureData.HeldItem.IsNone())
    {
        return nullptr;
    }

    UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContext);
    if (!GameInstance)
    {
        return nullptr;
    }

    UGF_ItemDataManager* ItemManager = GameInstance->GetSubsystem<UGF_ItemDataManager>();
    if (!ItemManager)
    {
        return nullptr;
    }

    return ItemManager->GetItemByName(CreatureData.HeldItem);
}

//=============================================================================
// 1. STAT CALCULATION
//=============================================================================

void UGF_HeldItemBattleHelper::ApplyHeldItemStatModifiers(UObject* WorldContext, AGF_Creature* Creature)
{
    if (!Creature)
    {
        return;
    }

    // Get the Creature's instance data
    FGF_CreatureInstanceData InstanceData = Creature->ExportToInstanceData();

    UGF_ItemData* HeldItem = GetCreatureHeldItemData(WorldContext, InstanceData);
    if (!HeldItem || !HeldItem->bCanBeHeld)
    {
        return;
    }

    // Light Core: only doubles stats for Voltkit (Compendium #25)
    if (HeldItem->ItemName == FName("LightCore") && Creature->CompendiumNumber != 25)
    {
        return;
    }

    // Apply stat multipliers to CurrentStats
    if (HeldItem->AttackMultiplier != 1.0f)
    {
        Creature->CurrentStats.Attack *= HeldItem->AttackMultiplier;
        UE_LOG(LogTemp, Log, TEXT("%s: Attack modified by held item (x%.2f)"),
            *Creature->Name.ToString(), HeldItem->AttackMultiplier);
    }

    if (HeldItem->DefenseMultiplier != 1.0f)
    {
        Creature->CurrentStats.Defense *= HeldItem->DefenseMultiplier;
    }

    if (HeldItem->MagicMultiplier != 1.0f)
    {
        Creature->CurrentStats.Magic *= HeldItem->MagicMultiplier;
    }

    if (HeldItem->PoiseMultiplier != 1.0f)
    {
        Creature->CurrentStats.Poise *= HeldItem->PoiseMultiplier;
    }

    if (HeldItem->SpeedMultiplier != 1.0f)
    {
        Creature->CurrentStats.Speed *= HeldItem->SpeedMultiplier;
    }
}

//=============================================================================
// 2. DAMAGE CALCULATION
//=============================================================================

float UGF_HeldItemBattleHelper::GetHeldItemDamageMultiplier(
    UObject* WorldContext,
    AGF_Creature* Attacker,
    AGF_Creature* Defender,
    EGF_Element SkillElement,
    bool bIsSuperEffective)
{
    if (!Attacker)
    {
        return 1.0f;
    }

    FGF_CreatureInstanceData AttackerData = Attacker->ExportToInstanceData();
    UGF_ItemData* AttackerItem = GetCreatureHeldItemData(WorldContext, AttackerData);

    if (!AttackerItem || !AttackerItem->bCanBeHeld)
    {
        return 1.0f;
    }

    float FinalMultiplier = 1.0f;

    // Base damage multiplier (Blood Orb = 1.3)
    if (AttackerItem->DamageMultiplier != 1.0f)
    {
        FinalMultiplier *= AttackerItem->DamageMultiplier;
    }

    // Type boost (Ember Charm boosts Fire moves)
    if (AttackerItem->BoostedType == SkillElement && AttackerItem->TypeBoostMultiplier != 1.0f)
    {
        FinalMultiplier *= AttackerItem->TypeBoostMultiplier;
        UE_LOG(LogTemp, Log, TEXT("%s's held item boosted %s-type move! (x%.2f)"),
            *Attacker->Name.ToString(),
            *UEnum::GetValueAsString(SkillElement),
            AttackerItem->TypeBoostMultiplier);
    }

    // Super effective boost (Adept Belt)
    if (bIsSuperEffective && AttackerItem->SuperEffectiveBoost != 1.0f)
    {
        FinalMultiplier *= AttackerItem->SuperEffectiveBoost;
        UE_LOG(LogTemp, Log, TEXT("%s's Adept Belt boosted super effective move!"),
            *Attacker->Name.ToString());
    }

    return FinalMultiplier;
}

//=============================================================================
// 3. AFTER DAMAGE
//=============================================================================

void UGF_HeldItemBattleHelper::HandleAfterDamageEffects(
    UObject* WorldContext,
    AGF_Creature* Attacker,
    AGF_Creature* Defender,
    float DamageDealt)
{
    if (!Attacker || !Defender)
    {
        return;
    }

    // Handle attacker's held item (Blood Orb recoil)
    FGF_CreatureInstanceData AttackerData = Attacker->ExportToInstanceData();
    UGF_ItemData* AttackerItem = GetCreatureHeldItemData(WorldContext, AttackerData);

    if (AttackerItem && AttackerItem->RecoilPercentage > 0.0f)
    {
        float RecoilDamage = Attacker->CurrentStats.MaxHP * (AttackerItem->RecoilPercentage / 100.0f);
        Attacker->CurrentStats.CurrentHP -= RecoilDamage;

        UE_LOG(LogTemp, Warning, TEXT("%s took %.1f recoil damage from %s!"),
            *Attacker->Name.ToString(), RecoilDamage, *AttackerItem->ItemName.ToString());

        if (Attacker->CurrentStats.CurrentHP <= 0)
        {
            Attacker->CurrentStats.CurrentHP = 0;
            Attacker->isDowned = true;
        }
    }

    // Handle defender's HP restore berries (Vigor Berry, Mend Berry)
    FGF_CreatureInstanceData DefenderData = Defender->ExportToInstanceData();
    UGF_ItemData* DefenderItem = GetCreatureHeldItemData(WorldContext, DefenderData);

    if (DefenderItem && DefenderItem->bConsumedOnUse)
    {
        float HPPercentage = (Defender->CurrentStats.CurrentHP / Defender->CurrentStats.MaxHP) * 100.0f;

        // Check if HP is below activation threshold
        if (HPPercentage <= DefenderItem->BerryActivationThreshold)
        {
            float HPRestored = DefenderItem->BerryHPRestore;

            // Vigor Berry restores 25% of max HP
            if (DefenderItem->ItemName == FName("VigorBerry"))
            {
                HPRestored = Defender->CurrentStats.MaxHP * 0.25f;
            }

            if (HPRestored > 0)
            {
                Defender->CurrentStats.CurrentHP = FMath::Min(
                    Defender->CurrentStats.CurrentHP + HPRestored,
                    Defender->CurrentStats.MaxHP
                );

                UE_LOG(LogTemp, Log, TEXT("%s restored %.1f HP from %s!"),
                    *Defender->Name.ToString(), HPRestored, *DefenderItem->ItemName.ToString());

                // Consume the berry
                ConsumeHeldItem(Defender);
            }
        }
    }
}

//=============================================================================
// 4. PREVENT FAINTING
//=============================================================================

bool UGF_HeldItemBattleHelper::CheckPreventDowning(
    UObject* WorldContext,
    AGF_Creature* Creature,
    float DamageTaken)
{
    if (!Creature)
    {
        return false;
    }

    FGF_CreatureInstanceData CreatureData = Creature->ExportToInstanceData();
    UGF_ItemData* HeldItem = GetCreatureHeldItemData(WorldContext, CreatureData);

    if (!HeldItem || !HeldItem->bPreventsOHKO)
    {
        return false;
    }

    // Last Stand Sash: Prevents OHKO when at full HP
    if (HeldItem->ItemName == FName("LastStandSash"))
    {
        float HPBeforeHit = Creature->CurrentStats.CurrentHP + DamageTaken;
        if (HPBeforeHit >= Creature->CurrentStats.MaxHP)
        {
            Creature->CurrentStats.CurrentHP = 1.0f;

            UE_LOG(LogTemp, Warning, TEXT("%s hung on with Last Stand Sash!"),
                *Creature->Name.ToString());

            ConsumeHeldItem(Creature);
            return true;
        }
    }

    // Grit Band: 10% chance to survive any fatal hit
    if (HeldItem->ItemName == FName("GritBand"))
    {
        if (HeldItem->OHKOPreventChance > 0.0f)
        {
            float Roll = FMath::FRandRange(0.0f, 100.0f);
            if (Roll <= HeldItem->OHKOPreventChance)
            {
                Creature->CurrentStats.CurrentHP = 1.0f;

                UE_LOG(LogTemp, Warning, TEXT("%s hung on with Grit Band!"),
                    *Creature->Name.ToString());

                // Grit Band is NOT consumed
                return true;
            }
        }
    }

    return false;
}

//=============================================================================
// 5. END OF TURN
//=============================================================================

void UGF_HeldItemBattleHelper::HandleEndOfTurnEffects(UObject* WorldContext, AGF_Creature* Creature)
{
    if (!Creature)
    {
        return;
    }

    FGF_CreatureInstanceData CreatureData = Creature->ExportToInstanceData();
    UGF_ItemData* HeldItem = GetCreatureHeldItemData(WorldContext, CreatureData);

    if (!HeldItem || !HeldItem->bCanBeHeld)
    {
        return;
    }

    // Foul Sludge: Damages non-Poison types, heals Poison types
    if (HeldItem->ItemName == FName("FoulSludge"))
    {
        if (Creature->PrimaryElement == EGF_Element::Venom ||
            Creature->SecondaryElement == EGF_Element::Venom)
        {
            // Heal Poison-type Creature
            float HPRestored = Creature->CurrentStats.MaxHP * (HeldItem->HPRestorePerTurn / 100.0f);
            Creature->CurrentStats.CurrentHP = FMath::Min(
                Creature->CurrentStats.CurrentHP + HPRestored,
                Creature->CurrentStats.MaxHP
            );

            UE_LOG(LogTemp, Log, TEXT("%s restored HP from Foul Sludge!"),
                *Creature->Name.ToString());
        }
        else
        {
            // Damage non-Poison types
            float Damage = Creature->CurrentStats.MaxHP * (HeldItem->HPRestorePerTurn / 100.0f);
            Creature->CurrentStats.CurrentHP -= Damage;

            UE_LOG(LogTemp, Log, TEXT("%s was hurt by Foul Sludge!"),
                *Creature->Name.ToString());

            if (Creature->CurrentStats.CurrentHP <= 0)
            {
                Creature->CurrentStats.CurrentHP = 0;
                Creature->isDowned = true;
            }
        }
    }
    // Sustain Charm / Siphon Shell: Restore HP
    else if (HeldItem->HPRestorePerTurn > 0.0f)
    {
        float HPRestored = Creature->CurrentStats.MaxHP * (HeldItem->HPRestorePerTurn / 100.0f);
        Creature->CurrentStats.CurrentHP = FMath::Min(
            Creature->CurrentStats.CurrentHP + HPRestored,
            Creature->CurrentStats.MaxHP
        );

        UE_LOG(LogTemp, Log, TEXT("%s restored %.1f HP from %s!"),
            *Creature->Name.ToString(), HPRestored, *HeldItem->ItemName.ToString());
    }

    // Status cure berries (Cleanse Berry, Cheri Berry, etc.)
    // Check if Creature has a status and item can cure statuses
    if (Creature->Status != EGF_STATUS::None && HeldItem->CuredStatusConditions.Num() > 0)
    {
        // Convert Creature's EGF_STATUS to EGF_STATUSEffect for comparison
        EGF_STATUSEffect StatusEffect = static_cast<EGF_STATUSEffect>(static_cast<uint8>(Creature->Status));

        if (HeldItem->CuredStatusConditions.Contains(StatusEffect))
        {
            UE_LOG(LogTemp, Log, TEXT("%s was cured of its status by %s!"),
                *Creature->Name.ToString(), *HeldItem->ItemName.ToString());

            Creature->Status = EGF_STATUS::None;
            Creature->SleepCounter = 0;

            ConsumeHeldItem(Creature);
        }
    }
}

//=============================================================================
// 6. EXP BOOST
//=============================================================================

int32 UGF_HeldItemBattleHelper::ApplyEXPBoost(UObject* WorldContext, AGF_Creature* Creature, int32 BaseEXP)
{
    if (!Creature)
    {
        return BaseEXP;
    }

    FGF_CreatureInstanceData CreatureData = Creature->ExportToInstanceData();
    UGF_ItemData* HeldItem = GetCreatureHeldItemData(WorldContext, CreatureData);

    if (!HeldItem || HeldItem->EXPMultiplier <= 1.0f)
    {
        return BaseEXP;
    }

    int32 BoostedEXP = FMath::RoundToInt(BaseEXP * HeldItem->EXPMultiplier);

    UE_LOG(LogTemp, Log, TEXT("%s gained extra EXP from %s! (%d -> %d)"),
        *Creature->Name.ToString(), *HeldItem->ItemName.ToString(), BaseEXP, BoostedEXP);

    return BoostedEXP;
}

int32 UGF_HeldItemBattleHelper::ApplyEXPBoostFromData(UObject* WorldContext, const FGF_CreatureInstanceData& CreatureData, int32 BaseEXP)
{
    UGF_ItemData* HeldItem = GetCreatureHeldItemData(WorldContext, CreatureData);

    if (!HeldItem || HeldItem->EXPMultiplier <= 1.0f)
    {
        return BaseEXP;
    }

    int32 BoostedEXP = FMath::RoundToInt(BaseEXP * HeldItem->EXPMultiplier);
    return BoostedEXP;
}

//=============================================================================
// 7. MONEY BOOST
//=============================================================================

int32 UGF_HeldItemBattleHelper::ApplyMoneyBoost(UObject* WorldContext, TArray<AGF_Creature*> PartyCreature, int32 BaseMoney)
{
    float HighestMultiplier = 1.0f;

    for (AGF_Creature* Creature : PartyCreature)
    {
        if (!Creature)
        {
            continue;
        }

        FGF_CreatureInstanceData CreatureData = Creature->ExportToInstanceData();
        UGF_ItemData* HeldItem = GetCreatureHeldItemData(WorldContext, CreatureData);

        if (HeldItem && HeldItem->MoneyMultiplier > HighestMultiplier)
        {
            HighestMultiplier = HeldItem->MoneyMultiplier;
        }
    }

    if (HighestMultiplier > 1.0f)
    {
        int32 BoostedMoney = FMath::RoundToInt(BaseMoney * HighestMultiplier);
        UE_LOG(LogTemp, Log, TEXT("Money boosted by held item! (%d -> %d)"), BaseMoney, BoostedMoney);
        return BoostedMoney;
    }

    return BaseMoney;
}

//=============================================================================
// 8. ACCURACY BOOST
//=============================================================================

float UGF_HeldItemBattleHelper::GetAccuracyMultiplier(UObject* WorldContext, AGF_Creature* Attacker, bool bMovedSecond)
{
    if (!Attacker)
    {
        return 1.0f;
    }

    FGF_CreatureInstanceData AttackerData = Attacker->ExportToInstanceData();
    UGF_ItemData* HeldItem = GetCreatureHeldItemData(WorldContext, AttackerData);

    if (!HeldItem)
    {
        return 1.0f;
    }

    float Multiplier = HeldItem->AccuracyMultiplier;

    // Quick Claw: Boost accuracy when moving second
    if (bMovedSecond && HeldItem->ItemName == FName("QuickClaw"))
    {
        Multiplier *= 1.2f;
    }

    return Multiplier;
}

//=============================================================================
// 9. CRITICAL HIT BOOST
//=============================================================================

int32 UGF_HeldItemBattleHelper::GetCriticalHitBoost(UObject* WorldContext, AGF_Creature* Attacker)
{
    if (!Attacker)
    {
        return 0;
    }

    FGF_CreatureInstanceData AttackerData = Attacker->ExportToInstanceData();
    UGF_ItemData* HeldItem = GetCreatureHeldItemData(WorldContext, AttackerData);

    if (!HeldItem)
    {
        return 0;
    }

    return HeldItem->CriticalHitBoost;
}

//=============================================================================
// PRIVATE: Consume Held Item
//=============================================================================

void UGF_HeldItemBattleHelper::ConsumeHeldItem(AGF_Creature* Creature)
{
    if (!Creature)
    {
        return;
    }

    // Export data, modify it, then re-import
    FGF_CreatureInstanceData Data = Creature->ExportToInstanceData();

    UE_LOG(LogTemp, Log, TEXT("%s's %s was consumed!"),
        *Creature->Name.ToString(), *Data.HeldItem.ToString());

    Data.HeldItem = NAME_None;

    // Re-import the modified data
    Creature->InitializeFromInstanceData(Data);
}