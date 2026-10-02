// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_SkillDefinition.h"

#include "GF_BattleBridge.h"
#include "GF_CreatureTraits.h"

EGF_StatStages FGF_SkillStatChange::ToStatStage() const
{
	const bool bUp = Stages > 0;
	switch (Stat)
	{
		case EGF_BattleStat::Attack:   return bUp ? EGF_StatStages::AttackUp   : EGF_StatStages::AttackDown;
		case EGF_BattleStat::Defense:  return bUp ? EGF_StatStages::DefenseUp  : EGF_StatStages::DefenseDown;
		case EGF_BattleStat::Magic:    return bUp ? EGF_StatStages::MagicUp    : EGF_StatStages::MagicDown;
		case EGF_BattleStat::Poise:    return bUp ? EGF_StatStages::PoiseUp    : EGF_StatStages::PoiseDown;
		case EGF_BattleStat::Speed:    return bUp ? EGF_StatStages::SpeedUp    : EGF_StatStages::SpeedDown;
		case EGF_BattleStat::Accuracy: return bUp ? EGF_StatStages::AccuracyUp : EGF_StatStages::AccuracyDown;
		case EGF_BattleStat::Evasion:  return bUp ? EGF_StatStages::EvasionUp  : EGF_StatStages::EvasionDown;
		default:                       return EGF_StatStages::None;
	}
}

// Sets default values
AGF_SkillDefinition::AGF_SkillDefinition()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AGF_SkillDefinition::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AGF_SkillDefinition::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

EGF_STATUSEffect AGF_SkillDefinition::ApplyStatus()
{
	int32 roll = FMath::RandRange(0, 100);

	EGF_STATUSEffect CurrentStatus = Status;

	if (roll > StatusChance)
	{
		return EGF_STATUSEffect::None;
	}

	switch (CurrentStatus)
	{
	case EGF_STATUSEffect::Burned:
		return EGF_STATUSEffect::Burned;
		break;

	case EGF_STATUSEffect::Paralyzed:
		return EGF_STATUSEffect::Paralyzed;
		break;

	case EGF_STATUSEffect::Poisoned:
		return EGF_STATUSEffect::Poisoned;
		break;

	case EGF_STATUSEffect::Sleeping:
		return EGF_STATUSEffect::Sleeping;
		break;

	case EGF_STATUSEffect::Frozen:
		return EGF_STATUSEffect::Frozen;
		break;

	case EGF_STATUSEffect::Confused:
		return EGF_STATUSEffect::Confused;
		break;
	}

	return EGF_STATUSEffect::None;
}

bool AGF_SkillDefinition::TryApplyStatStage()
{
	// Early out if this move doesn't affect stat stages
	if (!bAffectsStatStage || StatStages == EGF_StatStages::None)
	{
		return false;
	}

	// Roll for chance
	int32 Roll = FMath::RandRange(0, 100);

	if (Roll > StatStageChance)
	{
		// Failed the chance roll
		return false;
	}

	// Stat stage successfully applied - broadcast the delegate
	OnStatStageApplied.Broadcast(StatStages, StatStageAmount, bStatChangeAffectsSelf);

	return true;
}

bool AGF_SkillDefinition::TryApplyFlinch(bool bTargetHasMovedThisTurn, bool bTargetIsImmuneToFlinch)
{
	// Early out if this move can't cause flinch
	if (!bCanCauseFlinch || FlinchChance <= 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("%s cannot cause flinch"), *Name.ToString());
		return false;
	}

	// Flinch doesn't work if target already moved this turn
	// This is how it works in classic implementations - you can only flinch someone who's slower
	if (bTargetHasMovedThisTurn)
	{
		UE_LOG(LogTemp, Log, TEXT("%s: Target already moved this turn - flinch has no effect"), *Name.ToString());
		return false;
	}

	// Check for flinch immunity (Composed trait, etc.)
	if (bTargetIsImmuneToFlinch)
	{
		UE_LOG(LogTemp, Log, TEXT("%s: Target is immune to flinch!"), *Name.ToString());
		return false;
	}

	// Roll for flinch chance
	int32 Roll = FMath::RandRange(1, 100);

	if (Roll <= FlinchChance)
	{
		UE_LOG(LogTemp, Log, TEXT("%s caused FLINCH! (Rolled %d, needed <=%d)"),
			*Name.ToString(), Roll, FlinchChance);

		// Broadcast flinch applied delegate
		OnFlinchApplied.Broadcast();

		return true;
	}

	UE_LOG(LogTemp, Log, TEXT("%s: Flinch failed (Rolled %d, needed <=%d)"),
		*Name.ToString(), Roll, FlinchChance);

	return false;
}

int32 AGF_SkillDefinition::GetFlinchChanceFromClass(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
	if (!SkillClass)
	{
		return 0;
	}

	// Get the CDO (Class Default Object) to read properties without spawning
	const AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
	if (!SkillCDO)
	{
		return 0;
	}

	if (!SkillCDO->bCanCauseFlinch)
	{
		return 0;
	}

	return SkillCDO->FlinchChance;
}

bool AGF_SkillDefinition::CanSkillCauseFlinch(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
	if (!SkillClass)
	{
		return false;
	}

	const AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
	if (!SkillCDO)
	{
		return false;
	}

	return SkillCDO->bCanCauseFlinch && SkillCDO->FlinchChance > 0;
}

bool AGF_SkillDefinition::IsEscapeSkill(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
	if (!SkillClass)
	{
		return false;
	}

	const AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
	if (!SkillCDO)
	{
		return false;
	}

	return SkillCDO->bIsEscapeSkill;
}

bool AGF_SkillDefinition::CanEscapeWithSkill(TSubclassOf<AGF_SkillDefinition> SkillClass, AGF_Creature* User, bool bIsTamerBattle, bool bEscapeBlockedByScript)
{
	if (!IsEscapeSkill(SkillClass))
	{
		return false;
	}

	// "You can't run from a Tamer battle!" — Teleport is no exception.
	if (bIsTamerBattle)
	{
		return false;
	}

	// Scripted no-run battles, plus whatever escape blockers the battle BP knows about
	// (Mean Look, Ingrain, Arena Trap / Shadow Tag once those exist).
	if (bEscapeBlockedByScript)
	{
		return false;
	}

	// Ensnare / Ember Vortex / Bind hold the user in place — unless it has Fleetfoot,
	// which guarantees an escape from any wild battle.
	if (FGF_BattleBridge::EscapeBlocked(User))
	{
		return false;
	}

	// Wild, untrapped, unscripted — the escape is guaranteed. No speed roll, unlike Run.
	return true;
}

bool AGF_SkillDefinition::IsSelfKOSkill(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
	if (!SkillClass)
	{
		return false;
	}

	const AGF_SkillDefinition* SkillCDO = SkillClass->GetDefaultObject<AGF_SkillDefinition>();
	if (!SkillCDO)
	{
		return false;
	}

	return SkillCDO->bIsSelfKOSkill;
}