// FlinchHelperLibrary.cpp

#include "GF_FlinchHelperLibrary.h"
#include "GF_ElementTypes.h"
#include "GF_BattleManagementFunctions.h"
#include "GF_CreatureTraits.h"
bool UGF_FlinchHelperLibrary::IsTargetImmuneToSkillElement(AGF_SkillDefinition* Skill, AGF_Creature* TargetCreature)
{
    if (!Skill || !TargetCreature)
    {
        return false;
    }

    const EGF_Element SkillElement = (Skill->Type);
    if (SkillElement == EGF_Element::None)
    {
        return false;
    }

    // Species data is the source of truth; the actor caches these on init, so fall
    // back to the cached copies for Creature that were built without species data.
    EGF_Element DefenderType1 = TargetCreature->PrimaryElement;
    EGF_Element DefenderType2 = TargetCreature->SecondaryElement;
    if (TargetCreature->SpeciesData)
    {
        DefenderType1 = TargetCreature->SpeciesData->PrimaryElement;
        DefenderType2 = TargetCreature->SpeciesData->SecondaryElement;

        if (TargetCreature->SpeciesData->Immunities.Contains(SkillElement))
        {
            return true;
        }
    }
    else if (TargetCreature->Immune.Contains(SkillElement))
    {
        return true;
    }

    const float Effectiveness = BattleManagementFunctions::GetTypeEffectiveness(
        SkillElement, DefenderType1, DefenderType2);

    if (Effectiveness == 0.0f)
    {
        return true;
    }

    // Same extra immunities the damage path honours - Hover blocks Ground outright,
    // Aegis blocks anything that isn't super effective.
    const EGF_CreatureTrait DefenderTrait = UGF_CreatureTraitLibrary::GetActorTrait(TargetCreature);

    return UGF_CreatureTraitLibrary::DoesTraitGrantTypeImmunity(DefenderTrait, SkillElement)
        || UGF_CreatureTraitLibrary::DoesWonderGuardBlock(DefenderTrait, Effectiveness);
}

FGF_FlinchResult UGF_FlinchHelperLibrary::ProcessSkillFlinch(AGF_SkillDefinition* Skill, AGF_Creature* TargetCreature)
{
    FGF_FlinchResult Result;

    // Validate inputs
    if (!Skill)
    {
        Result.FailureReason = TEXT("Skill is null");
        UE_LOG(LogTemp, Warning, TEXT("ProcessSkillFlinch: Skill is null!"));
        return Result;
    }

    if (!TargetCreature)
    {
        Result.FailureReason = TEXT("Target Creature is null");
        UE_LOG(LogTemp, Warning, TEXT("ProcessSkillFlinch: Target Creature is null!"));
        return Result;
    }

    // Check if move can cause flinch
    if (!Skill->bCanCauseFlinch || Skill->FlinchChance <= 0)
    {
        Result.FailureReason = TEXT("Skill cannot cause flinch");
        return Result;
    }

    // A move the target is immune to never lands, so it can't flinch either.
    // Astonish on a Normal type does nothing at all - no damage, no flinch.
    // No BattleMessage here: the damage path already printed "It doesn't affect X...".
    if (IsTargetImmuneToSkillElement(Skill, TargetCreature))
    {
        Result.bWasTypeImmune = true;
        Result.FailureReason = TEXT("Target is immune to the move's type");
        UE_LOG(LogTemp, Log, TEXT("%s: %s is immune to this move's type - no flinch"),
            *Skill->Name.ToString(), *TargetCreature->Name.ToString());
        return Result;
    }

    // Check if target already moved this turn (flinch is useless)
    if (TargetCreature->bHasMovedThisTurn)
    {
        Result.bTargetAlreadyMoved = true;
        Result.FailureReason = TEXT("Target already moved this turn");
        UE_LOG(LogTemp, Log, TEXT("%s: %s already moved - flinch would have no effect"),
            *Skill->Name.ToString(), *TargetCreature->Name.ToString());
        return Result;
    }

    // Check for flinch immunity
    if (TargetCreature->bIsImmuneToFlinch)
    {
        Result.bWasImmuneToFlinch = true;
        Result.FailureReason = TEXT("Target is immune to flinch");

        // Build immunity message (for traits like Composed)
        if (TargetCreature->isPlayerCreature)
        {
            Result.BattleMessage = FString::Printf(TEXT("%s's Composed prevents flinching!"),
                *TargetCreature->Name.ToString());
        }
        else if (TargetCreature->isWildCreature)
        {
            Result.BattleMessage = FString::Printf(TEXT("The wild %s's Composed prevents flinching!"),
                *TargetCreature->Name.ToString());
        }
        else
        {
            Result.BattleMessage = FString::Printf(TEXT("The foe's %s's Composed prevents flinching!"),
                *TargetCreature->Name.ToString());
        }

        UE_LOG(LogTemp, Log, TEXT("%s is immune to flinch!"), *TargetCreature->Name.ToString());
        return Result;
    }

    // Roll for flinch chance
    int32 Roll = FMath::RandRange(1, 100);

    if (Roll <= Skill->FlinchChance)
    {
        // Flinch success!
        Result.bFlinchApplied = true;
        TargetCreature->bIsFlinched = true;

        UE_LOG(LogTemp, Log, TEXT("%s caused %s to FLINCH! (Rolled %d, needed <=%d)"),
            *Skill->Name.ToString(), *TargetCreature->Name.ToString(), Roll, Skill->FlinchChance);

        // Don't set battle message here - it will be shown when the Creature tries to move
        // The message is "X flinched and couldn't move!" which makes more sense during their turn
    }
    else
    {
        Result.FailureReason = FString::Printf(TEXT("Flinch roll failed (Rolled %d, needed <=%d)"),
            Roll, Skill->FlinchChance);
        UE_LOG(LogTemp, Log, TEXT("%s: Flinch failed (Rolled %d, needed <=%d)"),
            *Skill->Name.ToString(), Roll, Skill->FlinchChance);
    }

    return Result;
}

bool UGF_FlinchHelperLibrary::ShouldSkipTurnDueToFlinch(AGF_Creature* Creature, FString& OutFlinchMessage)
{
    if (!Creature)
    {
        return false;
    }

    if (!Creature->bIsFlinched)
    {
        return false;
    }

    // Creature is flinched - they skip their turn!
    OutFlinchMessage = Creature->GetFlinchMessage();

    UE_LOG(LogTemp, Log, TEXT("%s is flinched and cannot move!"), *Creature->Name.ToString());

    return true;
}

void UGF_FlinchHelperLibrary::ResetTurnFlagsForBothCreature(AGF_Creature* PlayerCreature, AGF_Creature* EnemyCreature)
{
    if (PlayerCreature)
    {
        PlayerCreature->ResetTurnFlags();
    }

    if (EnemyCreature)
    {
        EnemyCreature->ResetTurnFlags();
    }

    UE_LOG(LogTemp, Verbose, TEXT("Turn flags reset for both Creature"));
}

void UGF_FlinchHelperLibrary::MarkCreatureAsActed(AGF_Creature* Creature)
{
    if (!Creature)
    {
        return;
    }

    Creature->MarkAsMovedThisTurn();
}

int32 UGF_FlinchHelperLibrary::GetFlinchChanceFromSkillClass(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
    return AGF_SkillDefinition::GetFlinchChanceFromClass(SkillClass);
}

bool UGF_FlinchHelperLibrary::CanSkillClassCauseFlinch(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
    return AGF_SkillDefinition::CanSkillCauseFlinch(SkillClass);
}
