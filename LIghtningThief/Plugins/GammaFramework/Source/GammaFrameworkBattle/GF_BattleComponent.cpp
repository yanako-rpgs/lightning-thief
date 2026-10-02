#include "GF_BattleComponent.h"
#include "GF_ElementTypes.h"
#include "GF_DiagLog.h"
#include "GF_BattleManagementFunctions.h"
#include "GF_CreatureStatLibrary.h"
#include "GF_CreatureTraits.h"
#include "GF_Creature.h"
#include "GF_ItemData.h"
#include "GF_ItemDataManager.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// Which trait to use for a damage calculation. The override wins when it's set —
// that's how Trace gets its copied trait into the formula, since the instance data
// still holds the Creature's real one. None means "just read the instance data".
static EGF_CreatureTrait ResolveBattleTrait(const FGF_CreatureInstanceData& Data, EGF_CreatureTrait Override)
{
    return (Override != EGF_CreatureTrait::None)
        ? Override
        : UGF_CreatureTraitLibrary::GetInstanceTrait(Data);
}

// Resolve an item's data asset by name via the ItemDataManager subsystem.
// Returns nullptr if the name is None, the world/game instance is unavailable,
// or no item with that name exists.
static const UGF_ItemData* ResolveHeldItemData(const UWorld* World, FName ItemName)
{
    if (ItemName.IsNone() || !World)
    {
        return nullptr;
    }

    const UGameInstance* GameInstance = World->GetGameInstance();
    if (!GameInstance)
    {
        return nullptr;
    }

    const UGF_ItemDataManager* ItemManager = GameInstance->GetSubsystem<UGF_ItemDataManager>();
    return ItemManager ? ItemManager->GetItemByName(ItemName) : nullptr;
}

// Legacy items whose damage effect is still hardcoded below. Data-driven
// damage/super-effective fields are skipped for these to avoid double-applying.
static bool IsLegacyHardcodedHeldItem(FName ItemName)
{
    return ItemName == TEXT("BloodOrb")
        || ItemName == TEXT("ResolveBand")
        || ItemName == TEXT("ChoiceSpecs")
        || ItemName == TEXT("AdeptBelt");
}

UGF_BattleComponent::UGF_BattleComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UGF_BattleComponent::BeginPlay()
{
    Super::BeginPlay();
}

// Helper function to convert EGF_Element to EGF_Element


// A Creature may only use a move it actually owns. A mismatch here means the move was
// chosen against a DIFFERENT Creature than the one attacking -- a stale queue, or an index
// that points at another team member -- which is how a Thornwood ends up firing Surf.
// LastResort is exempt: it is deliberately not in anyone's moveset.
static void VerifySkillBelongsToAttacker(
    const FGF_CreatureInstanceData& Attacker,
    TSubclassOf<AGF_SkillDefinition> SkillUsed,
    const TCHAR* CallSite)
{
    if (!SkillUsed)
    {
        return;
    }

    const FSoftObjectPath UsedPath(SkillUsed.Get());
    if (UsedPath.GetAssetName().Contains(TEXT("LastResort")))
    {
        return;
    }

    FString Known;
    for (const TSoftClassPtr<AGF_SkillDefinition>& Owned : Attacker.Skills)
    {
        if (Owned.IsNull())
        {
            continue;
        }
        if (Owned.ToSoftObjectPath() == UsedPath)
        {
            return;
        }
        if (!Known.IsEmpty())
        {
            Known += TEXT(", ");
        }
        Known += Owned.ToSoftObjectPath().GetAssetName();
    }

    GF_DIAG("ILLEGAL MOVE | %s (Lv%d) is using '%s', which is NOT in its moveset [%s] | site=%s",
        *Attacker.GetDisplayName().ToString(), Attacker.Level,
        *UsedPath.GetAssetName(),
        Known.IsEmpty() ? TEXT("<empty>") : *Known,
        CallSite);
}

FGF_BattleDamageResult UGF_BattleComponent::CalculateDamage(
    const FGF_CreatureInstanceData& Attacker,
    const FGF_CreatureInstanceData& Defender,
    TSubclassOf<AGF_SkillDefinition> SkillUsed,
    FName AttackerHeldItem,
    FName DefenderHeldItem,
    EGF_WeatherType WeatherType,
    bool bHasReflect,
    bool bHasLightScreen,
    float AttackerAttackMult,
    float AttackerDefenseMult,
    float AttackerMagicMult,
    float AttackerPoiseMult,
    float AttackerSpeedMult,
    float DefenderAttackMult,
    float DefenderDefenseMult,
    float DefenderMagicMult,
    float DefenderPoiseMult,
    float DefenderSpeedMult,
    int32 AttackerCritStage,
    EGF_CreatureTrait AttackerTraitOverride,
    EGF_CreatureTrait DefenderTraitOverride)

{
    FGF_BattleDamageResult Result;

    VerifySkillBelongsToAttacker(Attacker, SkillUsed, TEXT("CalculateDamage"));

    // Spawn move to get its data
    AGF_SkillDefinition* Skill = SpawnSkillActor(SkillUsed);
    if (!Skill)
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateDamage: Failed to spawn move!"));
        Result.DamageMessage = "Error: Invalid move";
        return Result;
    }

    const EGF_CreatureTrait AttackerTrait = ResolveBattleTrait(Attacker, AttackerTraitOverride);
    const EGF_CreatureTrait DefenderTrait = ResolveBattleTrait(Defender, DefenderTraitOverride);

    //====================================================================================
    // STEP 0: TRAIT MOVE IMMUNITY (Muffled)
    //====================================================================================

    {
        FString BlockMessage;
        if (UGF_CreatureTraitLibrary::DoesTraitBlockMove(
                DefenderTrait, SkillUsed, Defender.GetDisplayName().ToString(), BlockMessage))
        {
            Result.bImmune = true;
            Result.DamageMessage = BlockMessage;
            CleanupSkillActor(Skill);
            return Result;
        }
    }

    //====================================================================================
    // STEP 1: CHECK IF MOVE HITS
    //====================================================================================

    if (!CheckSkillHit(SkillUsed, Attacker, Defender))
    {
        Result.bMissed = true;
        Result.DamageMessage = FString::Printf(TEXT("%s's attack missed!"),
            *Attacker.GetDisplayName().ToString());

        CleanupSkillActor(Skill);
        return Result;
    }

    //====================================================================================
    // STEP 2: CHECK TYPE EFFECTIVENESS
    //====================================================================================

    UGF_CreatureSpeciesData* DefenderSpecies = Defender.SpeciesData.LoadSynchronous();
    if (!DefenderSpecies)
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateDamage: Failed to load defender species!"));
        CleanupSkillActor(Skill);
        return Result;
    }

    // ✅ FIX: Use Skill->Type (not SkillElement) and convert to EGF_Element
    EGF_Element SkillElement = (Skill->Type);

    Result.TypeEffectiveness = BattleManagementFunctions::GetTypeEffectiveness(
        SkillElement,
        DefenderSpecies->PrimaryElement,
        DefenderSpecies->SecondaryElement
    );

    // Check for immunity — type chart first, then the traits that add their own.
    // Hover blocks Ground outright; Aegis blocks anything not super effective.
    if (Result.TypeEffectiveness == 0.0f
        || UGF_CreatureTraitLibrary::DoesTraitGrantTypeImmunity(DefenderTrait, SkillElement)
        || UGF_CreatureTraitLibrary::DoesWonderGuardBlock(DefenderTrait, Result.TypeEffectiveness))
    {
        Result.bImmune = true;
        Result.DamageMessage = FString::Printf(TEXT("It doesn't affect %s..."),
            *Defender.GetDisplayName().ToString());

        CleanupSkillActor(Skill);
        return Result;
    }

    // Determine effectiveness flags
    if (Result.TypeEffectiveness > 1.0f)
    {
        Result.bSuperEffective = true;
    }
    else if (Result.TypeEffectiveness < 1.0f)
    {
        Result.bNotEffective = true;
    }

    //====================================================================================
    // STEP 3: ROLL FOR CRITICAL HIT
    //====================================================================================

    Result.bWasCritical = RollCriticalHit(Attacker, Skill, AttackerCritStage);

    //====================================================================================
    // STEP 4: GET WEATHER MODIFIER
    //====================================================================================

    float WeatherModifier = GetWeatherModifier(SkillElement, WeatherType);

    //====================================================================================
    // STEP 5: CALCULATE ACTUAL STATS FROM APs/EPs/LEVEL
    //====================================================================================

    // This is the root fix: FGF_CreatureInstanceData only stores raw APs/EPs/Level.
    // We MUST calculate the real stats before using them in the damage formula.
    FGF_CreatureCurrentStats AttackerStats = UGF_CreatureStatLibrary::CalculateCreatureStats(Attacker);
    FGF_CreatureCurrentStats DefenderStats = UGF_CreatureStatLibrary::CalculateCreatureStats(Defender);

    bool bIsPhysical = (Skill->Split == EGF_SkillCategory::Physical);
    bool bIsBurned   = (Attacker.StatusCondition == EGF_STATUSEffect::Burned);

    // Pick the relevant attack and defense stats for this move's split
    float AttackStat  = bIsPhysical ? AttackerStats.Attack        : AttackerStats.Magic;
    float DefenseStat = bIsPhysical ? DefenderStats.Defense       : DefenderStats.Poise;

    // Apply stat stage multipliers (battle boosts/drops passed in from Blueprint)
    float AttackerStageMult = bIsPhysical ? AttackerAttackMult  : AttackerMagicMult;
    float DefenderStageMult = bIsPhysical ? DefenderDefenseMult : DefenderPoiseMult;

    // classic crit: ignore the attacker's negative stages and defender's positive stages
    if (Result.bWasCritical)
    {
        if (AttackerStageMult < 1.0f) AttackerStageMult = 1.0f;
        if (DefenderStageMult > 1.0f) DefenderStageMult = 1.0f;
    }

    AttackStat  *= AttackerStageMult;
    DefenseStat *= DefenderStageMult;

    // classic Selfdestruct / Explosion halve the target's Defense. Applied here rather than
    // through the DefenderDefenseMult pin so it survives the crit clamp above — a target
    // at +2 Defense taking a crit would otherwise come out at 1.0x instead of 0.5x.
    if (bIsPhysical && Skill->bIsSelfKOSkill)
    {
        DefenseStat *= Skill->SelfKODefenseMultiplier;
    }

    // Trait Attack modifiers: Titanstrength / Innerforce double it, Guts adds 50%
    AttackStat *= UGF_CreatureTraitLibrary::GetAttackStatMultiplier(
        AttackerTrait, bIsPhysical, Attacker.StatusCondition);

    // Burn halves physical Attack in classic — unless the attacker has Guts, which
    // skips the drop entirely on top of its own boost.
    if (bIsBurned && bIsPhysical
        && !UGF_CreatureTraitLibrary::DoesTraitIgnoreBurnAttackDrop(AttackerTrait))
    {
        AttackStat *= 0.5f;
    }

    // Prevent division by zero
    DefenseStat = FMath::Max(1.0f, DefenseStat);

    //====================================================================================
    // STEP 6: GEN 3 DAMAGE FORMULA
    //====================================================================================
    // Formula: floor((floor(2*Level/5 + 2) * BasePower * (Atk/Def)) / 50 + 2)
    // Then multiply by all modifiers.

    float Level       = static_cast<float>(Attacker.Level);
    float LevelFactor = FMath::FloorToFloat((2.0f * Level) / 5.0f) + 2.0f;
    float RawDamage   = FMath::FloorToFloat((LevelFactor * Skill->Power * (AttackStat / DefenseStat)) / 50.0f) + 2.0f;

    // STAB (Same Type Attack Bonus) — stronger for single-element attackers
    UGF_CreatureSpeciesData* AttackerSpecies = Attacker.SpeciesData.IsNull()
        ? nullptr : Attacker.SpeciesData.LoadSynchronous();
    float STAB = 1.0f;
    if (AttackerSpecies)
    {
        STAB = BattleManagementFunctions::GetSTABBonus(
            SkillElement, AttackerSpecies->PrimaryElement, AttackerSpecies->SecondaryElement);
    }

    // Critical hit is 2x in classic
    float CritMod = Result.bWasCritical ? 2.0f : 1.0f;

    // Random damage roll (85–100%)
    float RandomFactor = FMath::FRandRange(0.85f, 1.0f);

    float BaseDamage = RawDamage * CritMod * STAB * Result.TypeEffectiveness * WeatherModifier * RandomFactor;

    UE_LOG(LogTemp, Log, TEXT("--- Damage Debug ---"));
    UE_LOG(LogTemp, Log, TEXT("Attacker Lv%d | AtkStat=%.1f | DefStat=%.1f"), Attacker.Level, AttackStat, DefenseStat);
    UE_LOG(LogTemp, Log, TEXT("Power=%.1f | LevelFactor=%.1f | RawDamage=%.1f"), Skill->Power, LevelFactor, RawDamage);
    UE_LOG(LogTemp, Log, TEXT("STAB=%.2f | Crit=%.1f | TypeEff=%.2f | Weather=%.2f | Rand=%.2f"), STAB, CritMod, Result.TypeEffectiveness, WeatherModifier, RandomFactor);

    //====================================================================================
    // STEP 7: APPLY HELD ITEM MODIFIERS
    //====================================================================================

    float AttackerItemMod = GetHeldItemModifier(AttackerHeldItem, SkillElement, Result.TypeEffectiveness, true);
    float DefenderItemMod = GetHeldItemModifier(DefenderHeldItem, SkillElement, Result.TypeEffectiveness, false);
    BaseDamage *= AttackerItemMod * DefenderItemMod;

    //====================================================================================
    // STEP 7.5: APPLY TRAIT DAMAGE MODIFIERS
    //====================================================================================
    // Rootsurge / Cinderrage / Tidesurge / Hivecall on the attacker, Insulated on the defender.

    BaseDamage *= UGF_CreatureTraitLibrary::GetLowHPTypeBoost(
        AttackerTrait, SkillElement, Attacker.CurrentHP, AttackerStats.MaxHP);
    BaseDamage *= UGF_CreatureTraitLibrary::GetDefenderDamageMultiplier(DefenderTrait, SkillElement);

    //====================================================================================
    // STEP 8: APPLY SCREEN MODIFIERS
    //====================================================================================

    float ScreenMod = GetScreenModifier(bIsPhysical, bHasReflect, bHasLightScreen);
    BaseDamage *= ScreenMod;

    //====================================================================================
    // STEP 9: FINAL DAMAGE & MESSAGE
    //====================================================================================

    Result.Damage = FMath::Max(1.0f, FMath::FloorToFloat(BaseDamage));

    FString Message = FString::Printf(TEXT("%s dealt %.0f damage!"),
        *Attacker.GetDisplayName().ToString(),
        Result.Damage);

    if (Result.bWasCritical)
    {
        Message += TEXT(" Critical hit!");
    }

    if (Result.bSuperEffective)
    {
        Message += TEXT(" It's super effective!");
    }
    else if (Result.bNotEffective)
    {
        Message += TEXT(" It's not very effective...");
    }

    Result.DamageMessage = Message;

    UE_LOG(LogTemp, Log, TEXT("=== BATTLE CALCULATION ==="));
    UE_LOG(LogTemp, Log, TEXT("%s"), *Result.DamageMessage);
    UE_LOG(LogTemp, Log, TEXT("Type Effectiveness: %.2fx"), Result.TypeEffectiveness);
    UE_LOG(LogTemp, Log, TEXT("Final Damage: %.0f"), Result.Damage);

    CleanupSkillActor(Skill);
    return Result;
}

//====================================================================================
// HELPER FUNCTIONS
//====================================================================================

bool UGF_BattleComponent::CheckSkillHitActor(
    TSubclassOf<AGF_SkillDefinition> SkillClass,
    const FGF_CreatureInstanceData& Attacker,
    AGF_Creature* DefenderActor,
    const FGF_CreatureInstanceData& DefenderData,
    float AttackerAccuracyMult,
    float DefenderEvasionMult)
{
    if (!DefenderActor || !SkillClass)
        return false;

    AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
    if (!SkillCDO)
        return false;

    // Semi-invulnerability check — move must explicitly opt in to hitting this state
    const EGF_SemiInvulnerableState DefenderState = DefenderActor->SemiInvulnerableState;
    if (DefenderState != EGF_SemiInvulnerableState::None)
    {
        bool bCanHit = false;
        switch (DefenderState)
        {
            case EGF_SemiInvulnerableState::InAir:
                bCanHit = SkillCDO->bCanHitInAir;       // Thunder, Gust, Sky Uppercut, etc.
                break;
            case EGF_SemiInvulnerableState::Underground:
                bCanHit = SkillCDO->bCanHitUnderground;  // Earthquake, Magnitude, Fissure
                break;
            case EGF_SemiInvulnerableState::Underwater:
                bCanHit = SkillCDO->bCanHitUnderwater;   // Surf, Whirlpool
                break;
            case EGF_SemiInvulnerableState::PhaseShifted:
                bCanHit = false;                        // Nothing pierces Phase Strike
                break;
            default:
                bCanHit = false;
                break;
        }

        if (!bCanHit)
        {
            UE_LOG(LogTemp, Log, TEXT("CheckSkillHitActor: %s missed - defender is semi-invulnerable (%d)"),
                *SkillCDO->Name.ToString(), (int32)DefenderState);
            return false;
        }
    }

    // Fall through to normal accuracy check
    return CheckSkillHit(SkillClass, Attacker, DefenderData, AttackerAccuracyMult, DefenderEvasionMult);
}

bool UGF_BattleComponent::CheckSkillHit(
    TSubclassOf<AGF_SkillDefinition> SkillClass,
    const FGF_CreatureInstanceData& Attacker,
    const FGF_CreatureInstanceData& Defender,
    float AttackerAccuracyMult,
    float DefenderEvasionMult,
    EGF_WeatherType Weather)
{
    if (!SkillClass)
    {
        return false;
    }

    // Get the Class Default Object (CDO) to read the move's properties
    AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
    if (!SkillCDO)
    {
        return false;
    }

    // Use SkillCDO->Accuracy (already a float, 0-100)
    float BaseAccuracy = SkillCDO->Accuracy;

    // Perfect accuracy moves never miss (Accuracy <= 0 means "always hit")
    if (BaseAccuracy <= 0.0f || BaseAccuracy >= 100.0f)
    {
        return true;
    }

    // Apply accuracy and evasion stage multipliers
    // Formula: Final Accuracy = Base Accuracy × (Accuracy Mult / Evasion Mult)
    // Higher accuracy stages increase hit chance
    // Higher evasion stages decrease hit chance
    float FinalAccuracy = BaseAccuracy * (AttackerAccuracyMult / DefenderEvasionMult);

    // Manyeyes multiplies accuracy by 1.3
    FinalAccuracy *= UGF_CreatureTraitLibrary::GetAccuracyMultiplier(
        UGF_CreatureTraitLibrary::GetInstanceTrait(Attacker));

    // Duststep cuts it by 20% while a sandstorm is up
    FinalAccuracy *= UGF_CreatureTraitLibrary::GetEvasionAccuracyMultiplier(
        UGF_CreatureTraitLibrary::GetInstanceTrait(Defender), Weather);

    // Clamp to reasonable range (can't go below 0% or above 100%)
    FinalAccuracy = FMath::Clamp(FinalAccuracy, 0.0f, 100.0f);

    // Roll for hit
    float Roll = FMath::FRandRange(0.0f, 100.0f);
    return Roll <= FinalAccuracy;
}

bool UGF_BattleComponent::RollCriticalHit(
    const FGF_CreatureInstanceData& Attacker,
    AGF_SkillDefinition* Skill,
    int32 ExtraCritStage)
{
    if (!Skill)
        return false;

    if (bForceCriticalHit)
        return true;

    // Crit stage table (classic):
    //   Stage 0 → 1/16  (6.25%)   — normal move
    //   Stage 1 → 1/8   (12.5%)   — high crit move OR Focus Energy
    //   Stage 2 → 1/4   (25%)     — high crit move + Focus Energy
    //   Stage 3 → 1/2   (50%)
    //   Stage 4 → always crit
    const int32 Stage = (Skill->isHighCritRatio ? 1 : 0) + FMath::Clamp(ExtraCritStage, 0, 4);

    if (Stage >= 4)
        return true;

    // Each stage halves the denominator: 16 >> Stage
    const int32 CritChance = 16 >> Stage;   // 16, 8, 4, 2
    return FMath::RandRange(1, CritChance) == 1;
}

float UGF_BattleComponent::GetWeatherModifier(EGF_Element SkillElement, EGF_WeatherType Weather) const
{
    switch (Weather)
    {
        case EGF_WeatherType::HarshSun:
            if (SkillElement == EGF_Element::Fire)
                return 1.5f;
            if (SkillElement == EGF_Element::Water)
                return 0.5f;
            break;

        case EGF_WeatherType::Rain:
            if (SkillElement == EGF_Element::Water)
                return 1.5f;
            if (SkillElement == EGF_Element::Fire)
                return 0.5f;
            break;

        case EGF_WeatherType::Sandstorm:
        case EGF_WeatherType::Hail:
        default:
            break;
    }

    return 1.0f;
}

float UGF_BattleComponent::GetHeldItemModifier(
    FName ItemName,
    EGF_Element SkillElement,
    float TypeEffectiveness,
    bool bIsAttacker) const
{
    if (ItemName.IsNone())
    {
        return 1.0f;
    }

    float Modifier = 1.0f;

    //--------------------------------------------------------------------------
    // Legacy hardcoded items (kept for back-compat)
    //--------------------------------------------------------------------------
    if (bIsAttacker)
    {
        if (ItemName == TEXT("BloodOrb"))
            Modifier *= 1.3f;
        else if (ItemName == TEXT("ResolveBand"))
            Modifier *= 1.5f;
        else if (ItemName == TEXT("ChoiceSpecs"))
            Modifier *= 1.5f;
        else if (ItemName == TEXT("AdeptBelt") && TypeEffectiveness > 1.0f)
            Modifier *= 1.2f;
    }

    //--------------------------------------------------------------------------
    // Data-driven modifiers, read straight from the item's data asset.
    // Fill these fields on the ItemData asset — no code changes needed per item.
    //--------------------------------------------------------------------------
    if (const UGF_ItemData* Item = ResolveHeldItemData(GetWorld(), ItemName))
    {
        if (bIsAttacker)
        {
            // Type boost (Silk Scarf = Normal, Ember Charm = Fire, Mystic Water = Water, ...)
            // No legacy overlap, so always applied.
            if (Item->BoostedType != EGF_Element::None
                && Item->BoostedType == SkillElement
                && Item->TypeBoostMultiplier != 1.0f)
            {
                Modifier *= Item->TypeBoostMultiplier;
            }

            // Overall damage boost (Blood Orb) and super-effective boost (Adept Belt)
            // are only applied here for non-legacy items to avoid double-stacking
            // with the hardcoded values above.
            if (!IsLegacyHardcodedHeldItem(ItemName))
            {
                Modifier *= Item->DamageMultiplier;

                if (TypeEffectiveness > 1.0f)
                {
                    Modifier *= Item->SuperEffectiveBoost;
                }
            }
        }
    }

    return Modifier;
}

float UGF_BattleComponent::GetScreenModifier(
    bool bIsPhysical,
    bool bHasReflect,
    bool bHasLightScreen) const
{
    if (bIsPhysical && bHasReflect)
        return 0.5f;
    if (!bIsPhysical && bHasLightScreen)
        return 0.5f;

    return 1.0f;
}

bool UGF_BattleComponent::DoesAttackingCreatureGoFirst(
    AGF_Creature* AttackingCreature,
    TSubclassOf<AGF_SkillDefinition> AttackingSkill,
    AGF_Creature* TargetCreature,
    TSubclassOf<AGF_SkillDefinition> TargetSkill,
    EGF_WeatherType Weather)
{
    int32 AttackingPriority = AttackingSkill ? AttackingSkill.GetDefaultObject()->Priority : 0;
    int32 TargetPriority    = TargetSkill    ? TargetSkill.GetDefaultObject()->Priority    : 0;

    // Higher priority bracket always goes first
    if (AttackingPriority != TargetPriority)
        return AttackingPriority > TargetPriority;

    // Same priority — faster Creature goes first
    // Paralysis halves speed automatically
    float AttackingSpeed = AttackingCreature ? AttackingCreature->CurrentStats.Speed : 0.f;
    float TargetSpeed    = TargetCreature    ? TargetCreature->CurrentStats.Speed    : 0.f;

    if (AttackingCreature && AttackingCreature->Status == EGF_STATUS::Paralyzed) AttackingSpeed *= 0.5f;
    if (TargetCreature    && TargetCreature->Status    == EGF_STATUS::Paralyzed) TargetSpeed    *= 0.5f;

    // Currentborne doubles Speed in rain, Sunfed doubles it in harsh sun.
    // Applied after paralysis, matching classic ordering.
    AttackingSpeed *= UGF_CreatureTraitLibrary::GetSpeedMultiplier(
        UGF_CreatureTraitLibrary::GetActorTrait(AttackingCreature), Weather);
    TargetSpeed    *= UGF_CreatureTraitLibrary::GetSpeedMultiplier(
        UGF_CreatureTraitLibrary::GetActorTrait(TargetCreature), Weather);

    if (AttackingSpeed != TargetSpeed)
        return AttackingSpeed > TargetSpeed;

    // Same speed — coin flip (50/50)
    return FMath::RandBool();
}

AGF_SkillDefinition* UGF_BattleComponent::SpawnSkillActor(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
    if (!SkillClass)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnSkillActor: SkillClass is null!"));
        return nullptr;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnSkillActor: World is null!"));
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AGF_SkillDefinition* Skill = World->SpawnActor<AGF_SkillDefinition>(
        SkillClass,
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParams
    );

    return Skill;
}

void UGF_BattleComponent::CleanupSkillActor(AGF_SkillDefinition* Skill)
{
    if (Skill)
    {
        Skill->Destroy();
    }
}

// Low Kick / Grass Knot weight->power table (kg buckets from Bulbapedia).
static int32 WeightToPower(float Kg)
{
    if (Kg < 10.0f)   return 20;
    if (Kg < 25.0f)   return 40;
    if (Kg < 50.0f)   return 60;
    if (Kg < 100.0f)  return 80;
    if (Kg < 200.0f)  return 100;
    return 120;
}

FGF_BattleDamageResult UGF_BattleComponent::CalculateDamageWithoutSpawning(
    const FGF_CreatureInstanceData& Attacker,
    const FGF_CreatureInstanceData& Defender,
    TSubclassOf<AGF_SkillDefinition> SkillUsed,
    FName AttackerHeldItem,
    FName DefenderHeldItem,
    EGF_WeatherType WeatherType,
    bool bHasReflect,
    bool bHasLightScreen,
    float AttackerAttackMult,
    float AttackerDefenseMult,
    float AttackerMagicMult,
    float AttackerPoiseMult,
    float AttackerSpeedMult,
    float DefenderAttackMult,
    float DefenderDefenseMult,
    float DefenderMagicMult,
    float DefenderPoiseMult,
    float DefenderSpeedMult,
    int32 AttackerCritStage,
    int32 BasePowerOverride,
    EGF_CreatureTrait AttackerTraitOverride,
    EGF_CreatureTrait DefenderTraitOverride)


{
    FGF_BattleDamageResult Result;

    VerifySkillBelongsToAttacker(Attacker, SkillUsed, TEXT("CalculateDamageWithoutSpawning"));

    // Temporarily spawn move to read its data (will destroy immediately)
    AGF_SkillDefinition* Skill = SpawnSkillActor(SkillUsed);
    if (!Skill)
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateDamageWithoutSpawning: Failed to spawn move!"));
        Result.DamageMessage = "Error: Invalid move";
        return Result;
    }

    // Status moves deal no damage — skip all damage calculation entirely
    if (Skill->Split == EGF_SkillCategory::Status)
    {
        Result.Damage = 0.f;
        Result.DamageMessage = TEXT("");
        CleanupSkillActor(Skill);
        return Result;
    }

    const EGF_CreatureTrait AttackerTrait = ResolveBattleTrait(Attacker, AttackerTraitOverride);
    const EGF_CreatureTrait DefenderTrait = ResolveBattleTrait(Defender, DefenderTraitOverride);

    //====================================================================================
    // STEP 0: TRAIT MOVE IMMUNITY (Muffled)
    //====================================================================================
    // Runs before the accuracy roll — a blocked move never gets a chance to miss.

    {
        FString BlockMessage;
        if (UGF_CreatureTraitLibrary::DoesTraitBlockMove(
                DefenderTrait, SkillUsed, Defender.GetDisplayName().ToString(), BlockMessage))
        {
            Result.bImmune = true;
            Result.DamageMessage = BlockMessage;
            CleanupSkillActor(Skill);
            return Result;
        }
    }

    // Store move info for logging
    FName SkillName = Skill->Name;
    float SkillDuration = Skill->DurationOfAttack;

    //====================================================================================
    // STEP 1: CHECK IF MOVE HITS
    //====================================================================================

    if (!CheckSkillHit(SkillUsed, Attacker, Defender))
    {
        Result.bMissed = true;
        Result.DamageMessage = FString::Printf(TEXT("%s's attack missed!"),
            *Attacker.GetDisplayName().ToString());

        CleanupSkillActor(Skill);
        return Result;
    }

    //====================================================================================
    // STEP 2: CHECK TYPE EFFECTIVENESS
    //====================================================================================

    UGF_CreatureSpeciesData* DefenderSpecies = Defender.SpeciesData.LoadSynchronous();
    if (!DefenderSpecies)
    {
        UE_LOG(LogTemp, Error, TEXT("CalculateDamageWithoutSpawning: Failed to load defender species!"));
        CleanupSkillActor(Skill);
        return Result;
    }

    EGF_Element SkillElement = static_cast<EGF_Element>(Skill->Type);

    Result.TypeEffectiveness = BattleManagementFunctions::GetTypeEffectiveness(
        SkillElement,
        DefenderSpecies->PrimaryElement,
        DefenderSpecies->SecondaryElement
    );

    // Check for immunity — type chart first, then the traits that add their own.
    // Hover blocks Ground outright; Aegis blocks anything not super effective.
    if (Result.TypeEffectiveness == 0.0f
        || UGF_CreatureTraitLibrary::DoesTraitGrantTypeImmunity(DefenderTrait, SkillElement)
        || UGF_CreatureTraitLibrary::DoesWonderGuardBlock(DefenderTrait, Result.TypeEffectiveness))
    {
        Result.bImmune = true;
        Result.DamageMessage = FString::Printf(TEXT("It doesn't affect %s..."),
            *Defender.GetDisplayName().ToString());

        CleanupSkillActor(Skill);
        return Result;
    }

    //====================================================================================
    // STEP 2.5: FIXED-DAMAGE MOVES (Seismic Toss / Night Shade)
    //====================================================================================
    // Damage equals the user's level, bypassing stats/STAB/crit/weather/random.
    // Runs after the immunity check above so type immunity is still honored
    // (Seismic Toss is Fighting -> can't hit Ghost; Night Shade is Ghost -> can't hit Normal).
    // Effectiveness flags are intentionally left off — fixed damage isn't super/not effective.
    if (Skill->bIsFixedLevelDamage)
    {
        Result.Damage = static_cast<float>(FMath::Max(1, Attacker.Level));
        CleanupSkillActor(Skill);
        return Result;
    }

    // Set effectiveness flags
    if (Result.TypeEffectiveness > 1.0f)
        Result.bSuperEffective = true;
    else if (Result.TypeEffectiveness < 1.0f)
        Result.bNotEffective = true;

    //====================================================================================
    // STEP 3: ROLL FOR CRITICAL HIT
    //====================================================================================

    Result.bWasCritical = RollCriticalHit(Attacker, Skill, AttackerCritStage);

    //====================================================================================
    // STEP 4: GET WEATHER MODIFIER
    //====================================================================================

    float WeatherModifier = GetWeatherModifier(SkillElement, WeatherType);

    //====================================================================================
    // STEP 5: CALCULATE ACTUAL STATS FROM APs/EPs/LEVEL
    //====================================================================================

    FGF_CreatureCurrentStats AttackerStats = UGF_CreatureStatLibrary::CalculateCreatureStats(Attacker);
    FGF_CreatureCurrentStats DefenderStats = UGF_CreatureStatLibrary::CalculateCreatureStats(Defender);

    bool bIsPhysical = (Skill->Split == EGF_SkillCategory::Physical);
    bool bIsBurned   = (Attacker.StatusCondition == EGF_STATUSEffect::Burned);

    float AttackStat  = bIsPhysical ? AttackerStats.Attack        : AttackerStats.Magic;
    float DefenseStat = bIsPhysical ? DefenderStats.Defense       : DefenderStats.Poise;

    float AttackerStageMult = bIsPhysical ? AttackerAttackMult  : AttackerMagicMult;
    float DefenderStageMult = bIsPhysical ? DefenderDefenseMult : DefenderPoiseMult;

    // classic crit: ignore attacker's negative stages and defender's positive stages
    if (Result.bWasCritical)
    {
        if (AttackerStageMult < 1.0f) AttackerStageMult = 1.0f;
        if (DefenderStageMult > 1.0f) DefenderStageMult = 1.0f;
    }

    AttackStat  *= AttackerStageMult;
    DefenseStat *= DefenderStageMult;

    // classic Selfdestruct / Explosion halve the target's Defense. Applied here rather than
    // through the DefenderDefenseMult pin so it survives the crit clamp above — a target
    // at +2 Defense taking a crit would otherwise come out at 1.0x instead of 0.5x.
    if (bIsPhysical && Skill->bIsSelfKOSkill)
    {
        DefenseStat *= Skill->SelfKODefenseMultiplier;
    }

    // Trait Attack modifiers: Titanstrength / Innerforce double it, Guts adds 50%
    AttackStat *= UGF_CreatureTraitLibrary::GetAttackStatMultiplier(
        AttackerTrait, bIsPhysical, Attacker.StatusCondition);

    // Burn halves physical Attack in classic — unless the attacker has Guts, which
    // skips the drop entirely on top of its own boost.
    if (bIsBurned && bIsPhysical
        && !UGF_CreatureTraitLibrary::DoesTraitIgnoreBurnAttackDrop(AttackerTrait))
    {
        AttackStat *= 0.5f;
    }

    DefenseStat = FMath::Max(1.0f, DefenseStat);

    //====================================================================================
    // STEP 6: GEN 3 DAMAGE FORMULA
    //====================================================================================

    // Priority: explicit override (Rising Slash) > weight-based (Low Kick) > move's flat Power.
    float EffectivePower;
    if (BasePowerOverride >= 0)
    {
        EffectivePower = static_cast<float>(BasePowerOverride);
    }
    else if (Skill->bIsWeightBasedPower)
    {
        // DefenderSpecies is already loaded & validated above (STEP 2).
        EffectivePower = static_cast<float>(WeightToPower(DefenderSpecies->WeightKg));
    }
    else
    {
        EffectivePower = Skill->Power;
    }

    float Level       = static_cast<float>(Attacker.Level);
    float LevelFactor = FMath::FloorToFloat((2.0f * Level) / 5.0f) + 2.0f;
    float RawDamage   = FMath::FloorToFloat((LevelFactor * EffectivePower * (AttackStat / DefenseStat)) / 50.0f) + 2.0f;

    // STAB
    UGF_CreatureSpeciesData* AttackerSpecies = Attacker.SpeciesData.IsNull()
        ? nullptr : Attacker.SpeciesData.LoadSynchronous();
    float STAB = 1.0f;
    if (AttackerSpecies)
    {
        STAB = BattleManagementFunctions::GetSTABBonus(
            SkillElement, AttackerSpecies->PrimaryElement, AttackerSpecies->SecondaryElement);
    }

    float CritMod      = Result.bWasCritical ? 2.0f : 1.0f;
    float RandomFactor = FMath::FRandRange(0.85f, 1.0f);

    float BaseDamage = RawDamage * CritMod * STAB * Result.TypeEffectiveness * WeatherModifier * RandomFactor;

    UE_LOG(LogTemp, Log, TEXT("--- Damage Debug (Pre-Spawn) ---"));
    UE_LOG(LogTemp, Log, TEXT("Attacker Lv%d | AtkStat=%.1f | DefStat=%.1f"), Attacker.Level, AttackStat, DefenseStat);
    UE_LOG(LogTemp, Log, TEXT("Power=%.1f | LevelFactor=%.1f | RawDamage=%.1f"), EffectivePower, LevelFactor, RawDamage);
    UE_LOG(LogTemp, Log, TEXT("STAB=%.2f | Crit=%.1f | TypeEff=%.2f | Weather=%.2f | Rand=%.2f"), STAB, CritMod, Result.TypeEffectiveness, WeatherModifier, RandomFactor);

    //====================================================================================
    // STEP 7: APPLY HELD ITEM MODIFIERS
    //====================================================================================

    float AttackerItemMod = GetHeldItemModifier(AttackerHeldItem, SkillElement, Result.TypeEffectiveness, true);
    float DefenderItemMod = GetHeldItemModifier(DefenderHeldItem, SkillElement, Result.TypeEffectiveness, false);
    BaseDamage *= AttackerItemMod * DefenderItemMod;

    //====================================================================================
    // STEP 7.5: APPLY TRAIT DAMAGE MODIFIERS
    //====================================================================================
    // Rootsurge / Cinderrage / Tidesurge / Hivecall on the attacker, Insulated on the defender.

    const float TraitAttackMod  = UGF_CreatureTraitLibrary::GetLowHPTypeBoost(
        AttackerTrait, SkillElement, Attacker.CurrentHP, AttackerStats.MaxHP);
    const float TraitDefenseMod = UGF_CreatureTraitLibrary::GetDefenderDamageMultiplier(
        DefenderTrait, SkillElement);

    BaseDamage *= TraitAttackMod * TraitDefenseMod;

    UE_LOG(LogTemp, Log, TEXT("Trait mods: attacker=%.2f | defender=%.2f"),
        TraitAttackMod, TraitDefenseMod);

    //====================================================================================
    // STEP 8: APPLY SCREEN MODIFIERS
    //====================================================================================

    float ScreenMod = GetScreenModifier(bIsPhysical, bHasReflect, bHasLightScreen);
    BaseDamage *= ScreenMod;

    //====================================================================================
    // STEP 9: FINAL DAMAGE & MESSAGE
    //====================================================================================

    Result.Damage = FMath::Max(1.0f, FMath::FloorToFloat(BaseDamage));

    FString Message = FString::Printf(TEXT("%s dealt %.0f damage!"),
        *Attacker.GetDisplayName().ToString(), Result.Damage);

    if (Result.bWasCritical) Message += TEXT(" Critical hit!");
    if (Result.bSuperEffective) Message += TEXT(" It's super effective!");
    else if (Result.bNotEffective) Message += TEXT(" It's not very effective...");

    Result.DamageMessage = Message;

    UE_LOG(LogTemp, Log, TEXT("=== DAMAGE CALCULATED (NOT SPAWNED YET) ==="));
    UE_LOG(LogTemp, Log, TEXT("Skill: %s (Duration: %.2f seconds)"), *SkillName.ToString(), SkillDuration);
    UE_LOG(LogTemp, Log, TEXT("%s"), *Result.DamageMessage);

    // Clean up temporary move
    CleanupSkillActor(Skill);

    return Result;
}

//====================================================================================
// SPAWN MOVE SEPARATELY (AFTER DIALOGUE/CAMERA)
//====================================================================================

AGF_SkillDefinition* UGF_BattleComponent::SpawnSkillForBattle(
    TSubclassOf<AGF_SkillDefinition> SkillClass,
    FVector SpawnLocation,
    FRotator SpawnRotation)
{
    if (!SkillClass)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnSkillForBattle: SkillClass is null!"));
        return nullptr;
    }

    AGF_SkillDefinition* SpawnedSkill = SpawnSkillActorAt(SkillClass, SpawnLocation, SpawnRotation);

    if (SpawnedSkill)
    {
        UE_LOG(LogTemp, Log, TEXT("Spawned move: %s at %s (Duration: %.2f seconds)"),
            *SpawnedSkill->Name.ToString(),
            *SpawnLocation.ToString(),
            SpawnedSkill->DurationOfAttack);
    }

    return SpawnedSkill;
}

// Heal percentages are authored as fractions (0.5 = 50%). Skills set up with a whole
// number ("50") would heal 50x the creature's HP, which FMath::Min quietly turns into
// "always a full heal" — so treat anything above 1 as a percent and shout about it.
static float NormalizeHealPercentage(float RawPercentage, const AGF_SkillDefinition* Skill, const TCHAR* FieldName)
{
    if (RawPercentage <= 1.0f)
    {
        return RawPercentage;
    }

    UE_LOG(LogTemp, Warning, TEXT("%s on %s is %.2f - expected a fraction (0.5 = 50%%). Treating it as %.2f%%; fix the move asset."),
        FieldName, Skill ? *Skill->Name.ToString() : TEXT("<null>"), RawPercentage, RawPercentage);

    return RawPercentage / 100.0f;
}

FGF_DrainingSkillHealResult UGF_BattleComponent::ApplyDrainingSkillHealing(
    FGF_CreatureInstanceData& Attacker,
    const FGF_BattleDamageResult& DamageResult,
    AGF_SkillDefinition* Skill,
    EGF_CreatureTrait DefenderTrait)
{
    FGF_DrainingSkillHealResult Result;

    // Same contract as ApplySelfHealing: OldHP/NewHP always describe the attacker's real
    // HP, so a caller tweening between them gets a no-op instead of a down when no
    // draining happens.
    Result.AttackerOldHP = Attacker.CurrentHP;
    Result.AttackerNewHP = Attacker.CurrentHP;

    // Only drain moves (Siphon, Mega Drain, Giga Drain, Leech Life, etc.) heal here
    if (!Skill || !Skill->bIsDrainingSkill || DamageResult.Damage <= 0)
    {
        return Result; // No healing
    }

    // Calculate healing
    const float HealAmount = DamageResult.Damage * NormalizeHealPercentage(Skill->DrainPercentage, Skill, TEXT("DrainPercentage"));

    // Foulsap turns the drain around: the attacker takes that HP as damage
    // instead of recovering it.
    if (UGF_CreatureTraitLibrary::DoesDrainHurtInstead(DefenderTrait))
    {
        Attacker.CurrentHP = FMath::Max(0.0f, Attacker.CurrentHP - HealAmount);

        Result.AttackerNewHP    = Attacker.CurrentHP;
        // Actual HP lost, so the tween and the text agree with the health bar.
        Result.HealAmount       = Result.AttackerOldHP - Result.AttackerNewHP;
        Result.bDidHeal         = false;
        Result.bWasHurtInstead  = true;

        UE_LOG(LogTemp, Log, TEXT("%s was hurt by Foulsap for %f HP! (%f -> %f)"),
            *Attacker.GetDisplayName().ToString(), Result.HealAmount, Result.AttackerOldHP, Result.AttackerNewHP);

        return Result;
    }

    // Apply healing (capped at max HP)
    Attacker.CurrentHP = FMath::Min(Attacker.CurrentHP + HealAmount, Attacker.MaxHP);

    // Store new HP and heal amount (what was actually restored, after the MaxHP cap)
    Result.AttackerNewHP = Attacker.CurrentHP;
    Result.HealAmount = Result.AttackerNewHP - Result.AttackerOldHP;
    Result.bDidHeal = true;

    UE_LOG(LogTemp, Log, TEXT("%s healed %f HP from draining! (%f -> %f)"),
        *Attacker.GetDisplayName().ToString(), Result.HealAmount, Result.AttackerOldHP, Result.AttackerNewHP);

    return Result;
}

FGF_DrainingSkillHealResult UGF_BattleComponent::ApplySelfHealing(
    FGF_CreatureInstanceData& User,
    AGF_SkillDefinition* Skill)
{
    FGF_DrainingSkillHealResult Result;

    // Describe the user's real HP up front, so every path out of here — including the
    // ones that heal nothing — reports OldHP == NewHP == current HP. Callers tween the
    // health bar between these two; leaving them at 0 animates a healthy creature into
    // a down. This macro runs for every move, so the no-heal paths are the common case.
    Result.AttackerOldHP = User.CurrentHP;
    Result.AttackerNewHP = User.CurrentHP;

    if (!Skill)
    {
        UE_LOG(LogTemp, Warning, TEXT("ApplySelfHealing: Skill pin is NULL - healing 0. Is the move actor spawned/still alive when this is called?"));
        return Result;
    }

    if (!Skill->bIsSelfHealingSkill)
    {
        UE_LOG(LogTemp, Warning, TEXT("ApplySelfHealing: %s has bIsSelfHealingSkill = false - healing 0."), *Skill->Name.ToString());
        return Result;
    }

    if (User.CurrentHP >= User.MaxHP)
    {
        UE_LOG(LogTemp, Warning, TEXT("ApplySelfHealing: %s is already at full HP (%.0f/%.0f) - healing 0."),
            *User.GetDisplayName().ToString(), User.CurrentHP, User.MaxHP);
        return Result; // Already full, nothing to do
    }

    // SelfHealPercentage is a fraction (0.5 = 50%). A move authored with a whole
    // number ("50") would otherwise heal MaxHP * 50 — always a full heal.
    const float HealFraction = NormalizeHealPercentage(Skill->SelfHealPercentage, Skill, TEXT("SelfHealPercentage"));

    Result.AttackerOldHP = User.CurrentHP;
    User.CurrentHP       = FMath::Min(User.CurrentHP + User.MaxHP * HealFraction, User.MaxHP);
    Result.AttackerNewHP = User.CurrentHP;
    // Report what was ACTUALLY restored, not the uncapped roll — callers use this
    // for the HP tween and the "restored X HP" text.
    Result.HealAmount    = Result.AttackerNewHP - Result.AttackerOldHP;
    Result.bDidHeal      = true;

    UE_LOG(LogTemp, Log, TEXT("ApplySelfHealing: %s restored %.0f HP (%.0f -> %.0f)"),
        *User.GetDisplayName().ToString(), Result.HealAmount, Result.AttackerOldHP, Result.AttackerNewHP);

    return Result;
}

//====================================================================================
// GET MOVE DATA WITHOUT SPAWNING (TEMPORARILY SPAWN TO READ, THEN DESTROY)
//====================================================================================

float UGF_BattleComponent::GetSkillDurationFromClass(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
    if (!SkillClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetSkillDurationFromClass: SkillClass is null!"));
        return 1.0f;
    }

    // Get CDO (Class Default Object) - no need to spawn!
    AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
    if (!SkillCDO)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetSkillDurationFromClass: Failed to get CDO!"));
        return 1.0f;
    }

    return SkillCDO->DurationOfAttack;
}

float UGF_BattleComponent::GetTimeTillCameraMovesToEnemyFromClass(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
    if (!SkillClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetTimeTillCameraMovesToEnemyFromClass: SkillClass is null!"));
        return 0.0f;
    }

    AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
    if (!SkillCDO)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetTimeTillCameraMovesToEnemyFromClass: Failed to get CDO!"));
        return 0.0f;
    }

    return SkillCDO->TimeTillCameraMovesToEnemy;
}

float UGF_BattleComponent::GetSkillDuration(AGF_SkillDefinition* Skill)
{
    if (!Skill)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetSkillDuration: Skill is null!"));
        return 1.0f;
    }

    return Skill->DurationOfAttack;
}

float UGF_BattleComponent::GetTimeTillCameraMovesToEnemy(AGF_SkillDefinition* Skill)
{
    if (!Skill)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetTimeTillCameraMovesToEnemy: Skill is null!"));
        return 0.0f;
    }

    return Skill->TimeTillCameraMovesToEnemy;
}

void UGF_BattleComponent::CleanupSkill(AGF_SkillDefinition* Skill)
{
    CleanupSkillActor(Skill);
}

//====================================================================================
// PRIVATE HELPERS
//====================================================================================

bool UGF_BattleComponent::CanStatusSkillAffect(
    TSubclassOf<AGF_SkillDefinition> SkillClass,
    const FGF_CreatureInstanceData& Defender,
    FString& OutFailMessage) const
{
    OutFailMessage.Reset();

    if (!SkillClass)
        return false;

    AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
    if (!SkillCDO)
        return false;

    UGF_CreatureSpeciesData* DefenderSpecies = Defender.SpeciesData.LoadSynchronous();
    if (!DefenderSpecies)
        return true; // Can't determine - allow by default

    // Generic "nothing happened" line, used for every reason the player shouldn't be
    // told the mechanics of. Trait blocks overwrite it with something specific.
    const FString GenericFailure = FString::Printf(TEXT("It doesn't affect %s..."),
        *Defender.GetDisplayName().ToString());

    //====================================================================================
    // CHECK 1: Type chart immunity
    // e.g. Static Pulse (Electric) vs Ground type → 0x → immune
    //====================================================================================
    if (SkillCDO->Type != EGF_Element::None)
    {
        EGF_Element SkillElement = (SkillCDO->Type);
        float TypeEff = BattleManagementFunctions::GetTypeEffectiveness(
            SkillElement,
            DefenderSpecies->PrimaryElement,
            DefenderSpecies->SecondaryElement
        );

        if (TypeEff == 0.0f)
        {
            UE_LOG(LogTemp, Log, TEXT("CanStatusSkillAffect: %s blocked by type immunity (%.1fx)"),
                *SkillCDO->Name.ToString(), TypeEff);
            OutFailMessage = GenericFailure;
            return false;
        }
    }

    //====================================================================================
    // CHECK 1.5: Trait immunities
    // Muffled blocks the whole move (Bluster, Shriek, Discord, Sing, Roar);
    // Dewshield and Emberhide block the specific status it would inflict.
    //====================================================================================
    {
        const EGF_CreatureTrait DefenderTrait = UGF_CreatureTraitLibrary::GetInstanceTrait(Defender);
        const FString DefenderName = Defender.GetDisplayName().ToString();
        FString BlockMessage;

        if (UGF_CreatureTraitLibrary::DoesTraitBlockMove(DefenderTrait, SkillClass, DefenderName, BlockMessage))
        {
            UE_LOG(LogTemp, Log, TEXT("CanStatusSkillAffect: %s"), *BlockMessage);
            OutFailMessage = BlockMessage;
            return false;
        }

        if (UGF_CreatureTraitLibrary::DoesTraitBlockStatus(DefenderTrait, SkillCDO->Status, DefenderName, BlockMessage))
        {
            UE_LOG(LogTemp, Log, TEXT("CanStatusSkillAffect: %s"), *BlockMessage);
            OutFailMessage = BlockMessage;
            return false;
        }

        // Hover also blocks Ground-type STATUS moves, e.g. Grit Fling.
        if (SkillCDO->Type != EGF_Element::None
            && UGF_CreatureTraitLibrary::DoesTraitGrantTypeImmunity(
                    DefenderTrait, (SkillCDO->Type)))
        {
            UE_LOG(LogTemp, Log, TEXT("CanStatusSkillAffect: %s is immune via Hover"), *DefenderName);
            OutFailMessage = GenericFailure;
            return false;
        }
    }

    //====================================================================================
    // CHECK 2: Hardcoded status immunities (classic)
    // These apply regardless of move type.
    //====================================================================================
    const EGF_Element Primary   = DefenderSpecies->PrimaryElement;
    const EGF_Element Secondary = DefenderSpecies->SecondaryElement;

    auto HasType = [&](EGF_Element T) {
        return Primary == T || Secondary == T;
    };

    switch (SkillCDO->Status)
    {
        case EGF_STATUSEffect::Burned:
            // Fire types cannot be burned
            if (HasType(EGF_Element::Fire))
            {
                UE_LOG(LogTemp, Log, TEXT("CanStatusSkillAffect: %s immune to Burn (Fire type)"),
                    *Defender.GetDisplayName().ToString());
                OutFailMessage = GenericFailure;
                return false;
            }
            break;

        case EGF_STATUSEffect::Poisoned:
            // Poison types cannot be poisoned
            if (HasType(EGF_Element::Poison))
            {
                UE_LOG(LogTemp, Log, TEXT("CanStatusSkillAffect: %s immune to Poison (Poison type)"),
                    *Defender.GetDisplayName().ToString());
                OutFailMessage = GenericFailure;
                return false;
            }
            break;

        default:
            break;
    }

    return true;
}

//====================================================================================
// MOVE USABILITY / STRUGGLE
//====================================================================================

bool UGF_BattleComponent::IsSkillUsable(const AGF_Creature* Creature, int32 SlotIndex, EGF_SkillUnusableReason& OutReason)
{
    OutReason = EGF_SkillUnusableReason::Usable;

    if (!Creature || !Creature->Skills.IsValidIndex(SlotIndex) || !Creature->Skills[SlotIndex])
    {
        OutReason = EGF_SkillUnusableReason::NoSkillInSlot;
        return false;
    }

    // SkillUses is appended in lockstep with Skills by InitializeFromInstanceData, so a
    // missing entry means the two desynced somewhere. Fail OPEN: letting a move
    // through with untracked Uses is a far smaller problem than wrongly reporting
    // "no usable moves" and locking the battle into permanent LastResort.
    if (!Creature->SkillUses.IsValidIndex(SlotIndex))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("IsSkillUsable: %s has %d moves but only %d Uses entries - slot %d has no Uses data. "
                 "Treating it as usable; check InitializeFromInstanceData."),
            *Creature->Name.ToString(), Creature->Skills.Num(), Creature->SkillUses.Num(), SlotIndex);
        return true;
    }

    if (Creature->SkillUses[SlotIndex].CurrentUses <= 0)
    {
        OutReason = EGF_SkillUnusableReason::OutOfUses;
        return false;
    }

    return true;
}

bool UGF_BattleComponent::HasAnyUsableSkill(const AGF_Creature* Creature)
{
    return GetFirstUsableSkillIndex(Creature) != INDEX_NONE;
}

int32 UGF_BattleComponent::GetFirstUsableSkillIndex(const AGF_Creature* Creature)
{
    if (!Creature)
    {
        return INDEX_NONE;
    }

    // Iterate Skills, not SkillUses — a short Uses array must not read as "out of moves".
    for (int32 i = 0; i < Creature->Skills.Num(); ++i)
    {
        EGF_SkillUnusableReason Reason;
        if (IsSkillUsable(Creature, i, Reason))
        {
            return i;
        }
    }

    return INDEX_NONE;
}

EGF_SkillTurnAction UGF_BattleComponent::DecideTurnAction(const AGF_Creature* Creature, int32 SelectedSlotIndex, int32& OutSkillSlotIndex)
{
    OutSkillSlotIndex = INDEX_NONE;

    if (!Creature)
    {
        UE_LOG(LogTemp, Warning, TEXT("DecideTurnAction: null Creature - rejecting the click."));
        return EGF_SkillTurnAction::RejectSelection;
    }

    // LastResort is decided from the WHOLE moveset before a single Uses is spent.
    // Doing it here, and only here, is what stops the last Uses of a move from
    // being deducted and then read back as "no moves left -> LastResort".
    const int32 FirstUsable = GetFirstUsableSkillIndex(Creature);

    if (FirstUsable == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("DecideTurnAction: %s has no usable move (slot %d clicked, %d moves) -> STRUGGLE, no Uses spent."),
            *Creature->Name.ToString(), SelectedSlotIndex, Creature->Skills.Num());

        // Say WHY, per slot. "No usable move" has two very different causes -- an empty move
        // slot and zero Uses -- and they point at completely different bugs. Without this the
        // only way to tell them apart is guessing.
        for (int32 i = 0; i < Creature->Skills.Num(); ++i)
        {
            EGF_SkillUnusableReason SlotReason;
            IsSkillUsable(Creature, i, SlotReason);

            const int32 SlotUses = Creature->SkillUses.IsValidIndex(i) ? Creature->SkillUses[i].CurrentUses : -1;

            UE_LOG(LogTemp, Warning,
                TEXT("   slot %d: move=%s actorUses=%d reason=%d"),
                i,
                Creature->Skills[i] ? *Creature->Skills[i]->GetName() : TEXT("NULL"),
                SlotUses,
                static_cast<int32>(SlotReason));
        }
        return EGF_SkillTurnAction::UseLastResort;
    }

    EGF_SkillUnusableReason Reason;
    if (!IsSkillUsable(Creature, SelectedSlotIndex, Reason))
    {
        // Other moves still have Uses, so this is a bad click, not a LastResort turn.
        // Bouncing it keeps the menu open instead of burning the turn on nothing.
        UE_LOG(LogTemp, Warning,
            TEXT("DecideTurnAction: %s clicked slot %d which is unusable (reason %d) but slot %d still is -> REJECT."),
            *Creature->Name.ToString(), SelectedSlotIndex, static_cast<int32>(Reason), FirstUsable);
        return EGF_SkillTurnAction::RejectSelection;
    }

    OutSkillSlotIndex = SelectedSlotIndex;

    const int32 UsesLeft = Creature->SkillUses.IsValidIndex(SelectedSlotIndex)
        ? Creature->SkillUses[SelectedSlotIndex].CurrentUses
        : -1;

    UE_LOG(LogTemp, Warning,
        TEXT("DecideTurnAction: %s uses slot %d (Uses %d before this turn) -> USE SELECTED MOVE."),
        *Creature->Name.ToString(), SelectedSlotIndex, UsesLeft);

    return EGF_SkillTurnAction::UseSelectedSkill;
}

bool UGF_BattleComponent::ApplyProtect(AGF_Creature* User)
{
    if (!User)
        return false;

    // Each consecutive use halves the success chance (classic mechanic).
    // Count=0 → 100%, Count=1 → 50%, Count=2 → 25%, etc.
    const int32 Count = User->ConsecutiveProtectCount;
    if (Count > 0)
    {
        // Probability = 100 / 2^Count, minimum 1 out of 65536 (treat as never for simplicity)
        float SuccessChance = 100.0f / FMath::Pow(2.0f, static_cast<float>(Count));
        float Roll = FMath::FRandRange(0.0f, 100.0f);
        if (Roll > SuccessChance)
        {
            UE_LOG(LogTemp, Log, TEXT("ApplyProtect: Protect failed (consecutive use %d, chance %.1f%%)"), Count, SuccessChance);
            return false;
        }
    }

    User->bIsProtected = true;
    User->ConsecutiveProtectCount = Count + 1;

    UE_LOG(LogTemp, Log, TEXT("ApplyProtect: %s is now protected (streak: %d)"),
        *User->GetName(), User->ConsecutiveProtectCount);

    return true;
}

void UGF_BattleComponent::ResetProtectStreak(AGF_Creature* User)
{
    if (!User)
        return;

    User->ConsecutiveProtectCount = 0;
}

bool UGF_BattleComponent::ShouldAbortSwapPrompt(AGF_Creature* EnemyActive, bool bEnemySideHasUsableCreature)
{
    // Both conditions matter. The active being down is not enough — a tamer whose only
    // Creature is still standing also reports "no others", and aborting there would swallow
    // a legitimate swap prompt.
    const bool bEnemyDown = (EnemyActive != nullptr) && EnemyActive->IsDowned();
    const bool bBattleAlreadyWon = bEnemyDown && !bEnemySideHasUsableCreature;

    if (bBattleAlreadyWon)
    {
        UE_LOG(LogTemp, Log, TEXT("ShouldAbortSwapPrompt: opposing side is wiped - "
            "abandoning the swap prompt so the WinBattle timer can resolve the battle."));
    }

    return bBattleAlreadyWon;
}

EGF_BattleBoardResult UGF_BattleComponent::ResolveBoardAfterAction(
    AGF_Creature* PlayerActive,
    AGF_Creature* EnemyActive,
    int32 PlayerUsableRemaining,
    int32 EnemyUsableRemaining,
    TArray<AGF_Creature*>& OutDowned)
{
    OutDowned.Reset();

    // Read the board. Never ask "who did I just damage?" — one action can put both
    // actives down and that question only ever has one answer.
    const bool bPlayerDown = (PlayerActive != nullptr) && PlayerActive->IsDowned();
    const bool bEnemyDown  = (EnemyActive  != nullptr) && EnemyActive->IsDowned();

    if (bEnemyDown)  OutDowned.Add(EnemyActive);
    if (bPlayerDown) OutDowned.Add(PlayerActive);

    if (!bPlayerDown && !bEnemyDown)
    {
        return EGF_BattleBoardResult::Continue;
    }

    UE_LOG(LogTemp, Log, TEXT("ResolveBoardAfterAction: playerDown=%d enemyDown=%d "
        "playerLeft=%d enemyLeft=%d"),
        bPlayerDown, bEnemyDown, PlayerUsableRemaining, EnemyUsableRemaining);

    // A wipe outranks a replacement. If the last opposing Creature went down on the
    // same action that took yours, the battle is already won and must not stop to
    // ask for a replacement first — that is exactly what hangs a Selfdestruct KO
    // against a tamer's last Creature.
    if (EnemyUsableRemaining <= 0)
    {
        return EGF_BattleBoardResult::PlayerWins;
    }

    if (PlayerUsableRemaining <= 0)
    {
        return EGF_BattleBoardResult::PlayerLoses;
    }

    // Both sides can still field something, so somebody has to swap in.
    if (bPlayerDown && bEnemyDown)
    {
        return EGF_BattleBoardResult::BothMustReplace;
    }

    return bPlayerDown
        ? EGF_BattleBoardResult::PlayerMustReplace
        : EGF_BattleBoardResult::EnemyMustReplace;
}

int32 UGF_BattleComponent::GetRevengePower(AGF_Creature* User)
{
    if (!User)
        return 60;

    // Revenge (and Avalanche) hit for 120 if the user was damaged this turn, else 60.
    return User->bTookDamageThisTurn ? 120 : 60;
}

int32 UGF_BattleComponent::GetFuryCutterPower(AGF_Creature* User)
{
    if (!User)
        return 40;

    // 40, 80, 160, 160, ... capped at 160.
    const float Power = 40.0f * FMath::Pow(2.0f, static_cast<float>(User->FuryCutterCount));
    return FMath::Min(static_cast<int32>(Power), 160);
}

void UGF_BattleComponent::IncrementFuryCutter(AGF_Creature* User)
{
    if (!User)
        return;

    // Stop counting once we've reached the cap so the exponent can't run away.
    if (GetFuryCutterPower(User) < 160)
    {
        User->FuryCutterCount++;
    }
}

void UGF_BattleComponent::ResetFuryCutterStreak(AGF_Creature* User)
{
    if (!User)
        return;

    User->FuryCutterCount = 0;
}

bool UGF_BattleComponent::ApplyPartialTrap(AGF_Creature* Target, FName SkillName, int32 MinTurns, int32 MaxTurns)
{
    if (!Target)
        return false;

    // Partial traps don't stack or refresh — if already trapped, the new one fails.
    if (Target->bIsPartiallyTrapped)
        return false;

    const int32 Turns = FMath::RandRange(FMath::Min(MinTurns, MaxTurns), FMath::Max(MinTurns, MaxTurns));

    Target->bIsPartiallyTrapped       = true;
    Target->PartialTrapTurnsRemaining = Turns;
    Target->PartialTrapMoveName       = SkillName;

    UE_LOG(LogTemp, Log, TEXT("ApplyPartialTrap: %s trapped by %s for %d turns"),
        *Target->GetName(), *SkillName.ToString(), Turns);

    return true;
}

bool UGF_BattleComponent::IsPartiallyTrapped(AGF_Creature* Target)
{
    return Target ? Target->bIsPartiallyTrapped : false;
}

int32 UGF_BattleComponent::GetPartialTrapTurnsRemaining(AGF_Creature* Target)
{
    return (Target && Target->bIsPartiallyTrapped) ? Target->PartialTrapTurnsRemaining : 0;
}

int32 UGF_BattleComponent::GetPartialTrapDamage(AGF_Creature* Target)
{
    if (!Target || !Target->bIsPartiallyTrapped)
        return 0;

    // 1/8 of max HP per turn, never less than 1.
    return FMath::Max(1, FMath::FloorToInt(Target->CurrentStats.MaxHP / 8.0f));
}

void UGF_BattleComponent::TickPartialTrap(AGF_Creature* Target, bool& bJustEnded)
{
    bJustEnded = false;

    if (!Target || !Target->bIsPartiallyTrapped)
        return;

    Target->PartialTrapTurnsRemaining--;

    if (Target->PartialTrapTurnsRemaining <= 0)
    {
        ClearPartialTrap(Target);
        bJustEnded = true;
    }
}

void UGF_BattleComponent::ClearPartialTrap(AGF_Creature* Target)
{
    if (!Target)
        return;

    Target->bIsPartiallyTrapped       = false;
    Target->PartialTrapTurnsRemaining = 0;
    Target->PartialTrapMoveName       = NAME_None;
}

int32 UGF_BattleComponent::GetMultiHitCount(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
    AGF_SkillDefinition* CDO = SkillClass ? SkillClass->GetDefaultObject<AGF_SkillDefinition>() : nullptr;
    if (!CDO || !CDO->isLoopingSkill)
        return 1;

    const int32 Min = FMath::Max(1, CDO->MinHits);
    const int32 Max = FMath::Max(Min, CDO->MaxHits);

    // Fixed count: Double Kick, Twineedle, Bonemerang, etc.
    if (Min == Max)
        return Min;

    // Standard 2-5 hit distribution (classic+): 2 and 3 hits are most common.
    if (Min == 2 && Max == 5)
    {
        const int32 Roll = FMath::RandRange(0, 7); // 0-7
        if (Roll < 3) return 2;   // 3/8
        if (Roll < 6) return 3;   // 3/8
        if (Roll < 7) return 4;   // 1/8
        return 5;                 // 1/8
    }

    // Any other custom range: uniform.
    return FMath::RandRange(Min, Max);
}

void UGF_BattleComponent::StartBide(AGF_Creature* User, int32 StoreTurns)
{
    if (!User)
        return;

    User->bIsBiding         = true;
    User->BideTurnsRemaining = FMath::Max(1, StoreTurns);
    User->BideDamageStored   = 0.0f;

    UE_LOG(LogTemp, Log, TEXT("StartBide: %s began biding for %d turns"),
        *User->GetName(), User->BideTurnsRemaining);
}

bool UGF_BattleComponent::IsBiding(AGF_Creature* User)
{
    return User ? User->bIsBiding : false;
}

void UGF_BattleComponent::AddBideDamage(AGF_Creature* User, float Amount)
{
    if (!User || !User->bIsBiding || Amount <= 0.0f)
        return;

    User->BideDamageStored += Amount;

    UE_LOG(LogTemp, Log, TEXT("AddBideDamage: %s stored +%.0f (total %.0f)"),
        *User->GetName(), Amount, User->BideDamageStored);
}

void UGF_BattleComponent::TickBide(AGF_Creature* User, bool& bIsReleaseTurn)
{
    bIsReleaseTurn = false;

    if (!User || !User->bIsBiding)
        return;

    User->BideTurnsRemaining--;

    if (User->BideTurnsRemaining <= 0)
    {
        // Release turn — caller applies GetBideReleaseDamage then calls ClearBide.
        bIsReleaseTurn = true;
    }
}

int32 UGF_BattleComponent::GetBideReleaseDamage(AGF_Creature* User)
{
    if (!User || !User->bIsBiding)
        return 0;

    return FMath::FloorToInt(User->BideDamageStored * 2.0f);
}

void UGF_BattleComponent::ClearBide(AGF_Creature* User)
{
    if (!User)
        return;

    User->bIsBiding          = false;
    User->BideTurnsRemaining = 0;
    User->BideDamageStored   = 0.0f;
}

AGF_SkillDefinition* UGF_BattleComponent::SpawnSkillActorAt(
    TSubclassOf<AGF_SkillDefinition> SkillClass,
    FVector Location,
    FRotator Rotation)
{
    if (!SkillClass)
    {
        return nullptr;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    return World->SpawnActor<AGF_SkillDefinition>(SkillClass, Location, Rotation, SpawnParams);
}