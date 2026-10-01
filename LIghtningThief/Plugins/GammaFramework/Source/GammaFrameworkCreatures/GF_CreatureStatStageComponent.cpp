// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CreatureStatStageComponent.h"
#include "GF_CreatureTraits.h"

UGF_CreatureStatStageComponent::UGF_CreatureStatStageComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGF_CreatureStatStageComponent::BeginPlay()
{
	Super::BeginPlay();
	OwningCreature = Cast<AGF_Creature>(GetOwner());
	ResetAllStatStages();
}

EGF_StatStages  UGF_CreatureStatStageComponent::GetRandomStage() const
{
		TArray<EGF_StatStages> Stats = {
			EGF_StatStages::AccuracyUp,
			EGF_StatStages::EvasionUp,
			EGF_StatStages::AttackUp,
			EGF_StatStages::DefenseUp,
			EGF_StatStages::MagicUp,
			EGF_StatStages::PoiseUp,
			EGF_StatStages::SpeedUp
		};

		//EGF_StatStages RandomStat = static_cast<EGF_StatStages>(FMath::RandRange(0, Stats.Num() - 1));

		//StatType = RandomStat;

		return Stats[FMath::RandRange(0, Stats.Num() - 1)];
}




int32 UGF_CreatureStatStageComponent::ApplyStatStageChange(EGF_StatStages StatType, int32 Change, bool bSelfInflicted)
{
	bBlockedByTrait = false;
	LastTraitBlockMessage.Reset();

	int32* TargetStage = GetStatStagePointer(StatType);
	if (!TargetStage)
	{
		return 0;
	}

	// Unyielding / Hazeform block every opponent-inflicted drop; Sharpsight blocks
	// accuracy drops only. Self-inflicted drops always go through.
	if (!OwningCreature)
	{
		OwningCreature = Cast<AGF_Creature>(GetOwner());
	}

	if (OwningCreature)
	{
		// Match the "The wild X" / "The foe's X" convention used by the trait messages
		// and GetFlinchMessage, so a mirror match doesn't produce two identical lines.
		FString DisplayName = OwningCreature->Name.ToString();
		if (!OwningCreature->isPlayerCreature)
		{
			DisplayName = (OwningCreature->isWildCreature ? TEXT("The wild ") : TEXT("The foe's ")) + DisplayName;
		}

		FString BlockMessage;
		const bool bAllowed = UGF_CreatureTraitLibrary::CanStatStageBeLowered(
			UGF_CreatureTraitLibrary::GetActorTrait(OwningCreature),
			StatType,
			bSelfInflicted,
			DisplayName,
			BlockMessage);

		if (!bAllowed)
		{
			bBlockedByTrait = true;
			LastTraitBlockMessage = BlockMessage;
			bStatAtLimit = false;

			UE_LOG(LogTemp, Log, TEXT("%s"), *BlockMessage);
			return 0;
		}
	}

	int32 OldValue = *TargetStage;
	*TargetStage = FMath::Clamp(*TargetStage + Change, -6, 6);
	int32 ActualChange = *TargetStage - OldValue;

	// Track whether the stat hit its limit (no change occurred despite trying)
	bStatAtLimit = (ActualChange == 0);

	// Log the change
	if (ActualChange != 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Stat stage changed: %d -> %d (Change: %d)"), OldValue, *TargetStage, ActualChange);
	}

	OnStatStageUpdated.Broadcast(StatType, *TargetStage, ActualChange);

	return ActualChange;




}

float UGF_CreatureStatStageComponent::GetStatMultiplier(EGF_StatStages StatType) const
{

	const int32* Stage = GetStatStagePointerConst(StatType);
	if (!Stage)
	{
		return 1.0f;
	}

	return CalculateMultiplier(*Stage);
}

float UGF_CreatureStatStageComponent::GetModifiedStat(EGF_StatStages StatType, float BaseStat) const
{
	float Multiplier = GetStatMultiplier(StatType);
	return BaseStat * Multiplier;
}

void UGF_CreatureStatStageComponent::ResetAllStatStages()
{
	AttackStage = 0;
	DefenseStage = 0;
	MagicStage = 0;
	PoiseStage = 0;
	SpeedStage = 0;
	AccuracyStage = 0;
	EvasionStage = 0;


	UE_LOG(LogTemp, Log, TEXT("All stat stages reset to 0"));
}

int32 UGF_CreatureStatStageComponent::GetCurrentStage(EGF_StatStages StatType) const
{
	const int32* Stage = GetStatStagePointerConst(StatType);
	return Stage ? *Stage : 0;
}

bool UGF_CreatureStatStageComponent::IsAtMaxStage(EGF_StatStages StatType) const
{
	return GetCurrentStage(StatType) >= 6;
}

bool UGF_CreatureStatStageComponent::IsAtMinStage(EGF_StatStages StatType) const
{
	return GetCurrentStage(StatType) <= -6;
}

FString UGF_CreatureStatStageComponent::GetStatStageMessage(EGF_StatStages StatType, int32 ActualChange, bool bAffectedSelf, const FString& CreatureName) const
{
	// Get the stat name
	FString StatName;
	switch (StatType)
	{
	case EGF_StatStages::AttackUp:
	case EGF_StatStages::AttackDown:
		StatName = TEXT("Attack");
		break;
	case EGF_StatStages::DefenseUp:
	case EGF_StatStages::DefenseDown:
		StatName = TEXT("Defense");
		break;
	case EGF_StatStages::MagicUp:
	case EGF_StatStages::MagicDown:
		StatName = TEXT("Special Attack");
		break;
	case EGF_StatStages::PoiseUp:
	case EGF_StatStages::PoiseDown:
		StatName = TEXT("Special Defense");
		break;
	case EGF_StatStages::SpeedUp:
	case EGF_StatStages::SpeedDown:
		StatName = TEXT("Speed");
		break;
	case EGF_StatStages::AccuracyUp:
	case EGF_StatStages::AccuracyDown:
		StatName = TEXT("Accuracy");
		break;
	case EGF_StatStages::EvasionUp:
	case EGF_StatStages::EvasionDown:
		StatName = TEXT("Evasiveness");
		break;
	default:
		return TEXT("");
	}

	// Determine target prefix based on Creature ownership
	FString Target;

	if (!OwningCreature)
	{
		// Fallback if we can't get the owning Creature
		Target = CreatureName;
	}
	else
	{
		// Get the Creature's actual display name (nickname or species)
		FString DisplayName = OwningCreature->Name.ToString();

		if (OwningCreature->isPlayerCreature)
		{
			// Player's Creature - no prefix, just the name/nickname
			Target = DisplayName;
		}
		else if (OwningCreature->isWildCreature)
		{
			// Wild Creature - "The wild {species}"
			Target = FString::Printf(TEXT("The wild %s"), *DisplayName);
		}
		else
		{
			// Enemy tamer's Creature - "The foe's {name/nickname}"
			Target = FString::Printf(TEXT("The foe's %s"), *DisplayName);
		}
	}

	// Determine intensity
	FString IntensityText;
	int32 AbsChange = FMath::Abs(ActualChange);

	if (AbsChange == 1)
	{
		IntensityText = TEXT("");
	}
	else if (AbsChange == 2)
	{
		IntensityText = TEXT(" sharply");
	}
	else if (AbsChange >= 3)
	{
		IntensityText = TEXT(" drastically");
	}

	// Build message
	if (ActualChange > 0)
	{
		return FString::Printf(TEXT("%s's %s%s rose!"), *Target, *StatName, *IntensityText);
	}
	else if (ActualChange < 0)
	{
		return FString::Printf(TEXT("%s's %s%s fell!"), *Target, *StatName, *IntensityText);
	}
	else
	{
		// At max/min stage - no change occurred
		bool bTryingToIncrease = (StatType == EGF_StatStages::AttackUp || StatType == EGF_StatStages::DefenseUp ||
			StatType == EGF_StatStages::MagicUp || StatType == EGF_StatStages::PoiseUp || StatType == EGF_StatStages::SpeedUp ||
			StatType == EGF_StatStages::AccuracyUp || StatType == EGF_StatStages::EvasionUp);



		if (bTryingToIncrease)
		{
			return FString::Printf(TEXT("%s's %s won't go any higher!"), *Target, *StatName);



		}
		else
		{
			return FString::Printf(TEXT("%s's %s won't go any lower!"), *Target, *StatName);
		}


	}
}

bool UGF_CreatureStatStageComponent::GetIsAtLimit() const
{
	return bStatAtLimit;
}


int32* UGF_CreatureStatStageComponent::GetStatStagePointer(EGF_StatStages StatType)
{
	switch (StatType)
	{
	case EGF_StatStages::AttackUp:
	case EGF_StatStages::AttackDown:
		return &AttackStage;
	case EGF_StatStages::DefenseUp:
	case EGF_StatStages::DefenseDown:
		return &DefenseStage;
	case EGF_StatStages::MagicUp:
	case EGF_StatStages::MagicDown:
		return &MagicStage;
	case EGF_StatStages::PoiseUp:
	case EGF_StatStages::PoiseDown:
		return &PoiseStage;
	case EGF_StatStages::SpeedUp:
	case EGF_StatStages::SpeedDown:
		return &SpeedStage;
	case EGF_StatStages::AccuracyUp:
	case EGF_StatStages::AccuracyDown:
		return &AccuracyStage;
	case EGF_StatStages::EvasionUp:
	case EGF_StatStages::EvasionDown:
		return &EvasionStage;
	default:
		return nullptr;
	}
}

const int32* UGF_CreatureStatStageComponent::GetStatStagePointerConst(EGF_StatStages StatType) const
{
	switch (StatType)
	{
	case EGF_StatStages::AttackUp:
	case EGF_StatStages::AttackDown:
		return &AttackStage;
	case EGF_StatStages::DefenseUp:
	case EGF_StatStages::DefenseDown:
		return &DefenseStage;
	case EGF_StatStages::MagicUp:
	case EGF_StatStages::MagicDown:
		return &MagicStage;
	case EGF_StatStages::PoiseUp:
	case EGF_StatStages::PoiseDown:
		return &PoiseStage;
	case EGF_StatStages::SpeedUp:
	case EGF_StatStages::SpeedDown:
		return &SpeedStage;
	case EGF_StatStages::AccuracyUp:
	case EGF_StatStages::AccuracyDown:
		return &AccuracyStage;
	case EGF_StatStages::EvasionUp:
	case EGF_StatStages::EvasionDown:
		return &EvasionStage;
	default:
		return nullptr;
	}
}

float UGF_CreatureStatStageComponent::CalculateMultiplier(int32 Stage)
{
	if (Stage >= 0)
	{
		// Positive stages: (2 + Stage) / 2
		// Stage 0 = 1.0x, +1 = 1.5x, +2 = 2.0x, +3 = 2.5x, +4 = 3.0x, +5 = 3.5x, +6 = 4.0x
		return (2.0f + Stage) / 2.0f;
	}
	else
	{
		// Negative stages: 2 / (2 - Stage)
		// Stage -1 = 0.67x, -2 = 0.5x, -3 = 0.4x, -4 = 0.33x, -5 = 0.29x, -6 = 0.25x
		return 2.0f / (2.0f - Stage);
	}
}

bool UGF_CreatureStatStageComponent::IsAtLimitForChange(EGF_StatStages StatType, int32 Change) const
{
    if (Change > 0) return IsAtMaxStage(StatType);
    if (Change < 0) return IsAtMinStage(StatType);
    return false;
}