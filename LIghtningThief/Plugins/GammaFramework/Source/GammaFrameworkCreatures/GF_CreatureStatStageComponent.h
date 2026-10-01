// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GF_SkillDefinition.h"
#include "GF_Creature.h"
#include "GF_CreatureStatStageComponent.generated.h"



DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGF_OnStatStageUpdated,
    EGF_StatStages, StatType,
    int32, NewStage,
    int32, ActualChange);


/**
 * ActorComponent that tracks stat stage modifiers for a Creature in battle
 * Add this component to your Blueprint Creature actor
 * Stat stages range from -6 to +6, with 0 being neutral
 */
UCLASS(ClassGroup=(Creature), meta=(BlueprintSpawnableComponent))
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureStatStageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGF_CreatureStatStageComponent();

protected:
	virtual void BeginPlay() override;

public:
	// Current stat stages (-6 to +6)
	UPROPERTY(BlueprintReadWrite, Category = "Stat Stages", meta = (ClampMin = "-6", ClampMax = "6"))
	int32 AttackStage = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Stat Stages", meta = (ClampMin = "-6", ClampMax = "6"))
	int32 DefenseStage = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Stat Stages", meta = (ClampMin = "-6", ClampMax = "6"))
	int32 MagicStage = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Stat Stages", meta = (ClampMin = "-6", ClampMax = "6"))
	int32 PoiseStage = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Stat Stages", meta = (ClampMin = "-6", ClampMax = "6"))
	int32 SpeedStage = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Stat Stages", meta = (ClampMin = "-6", ClampMax = "6"))
	int32 AccuracyStage = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Stat Stages", meta = (ClampMin = "-6", ClampMax = "6"))
	int32 EvasionStage = 0;


	/**
	 * Apply a stat stage change and return the actual change that occurred (accounting for clamping)
	 * @param StatType - Which stat to modify (AttackUp, DefenseDown, etc.)
	 * @param Change - Amount to change by (positive or negative)
	 * @param bSelfInflicted - True when the Creature is lowering its OWN stats (Meltdown,
	 *        Onslaught, Blood Rite). Wire the move's bStatChangeAffectsSelf into this.
	 *        Unyielding / Hazeform / Sharpsight only block drops caused by the OPPONENT,
	 *        so leaving this false on a self-drop would wrongly cancel it.
	 * @return The actual change that was applied after clamping to -6/+6. Returns 0 when an
	 *         trait blocked the drop — check bBlockedByTrait / LastTraitBlockMessage
	 *         to tell that apart from "already at the limit".
	 */
	UFUNCTION(BlueprintCallable, Category = "Stat Stages")
	int32 ApplyStatStageChange(EGF_StatStages StatType, int32 Change, bool bSelfInflicted = false);

	/**
	 * True when the most recent ApplyStatStageChange was cancelled by an trait
	 * (Unyielding, Hazeform, Sharpsight) rather than by hitting the -6/+6 cap.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Stat Stages")
	bool bBlockedByTrait = false;

	/** Ready-to-print message for the block above. Empty when nothing was blocked. */
	UPROPERTY(BlueprintReadOnly, Category = "Stat Stages")
	FString LastTraitBlockMessage;

	/**
	 * Get the multiplier for a stat based on its current stage
	 * @param StatType - Which stat to get the multiplier for
	 * @return Multiplier value (0.25x to 4.0x)
	 */
	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	float GetStatMultiplier(EGF_StatStages StatType) const;

	/**
	 * Get a modified stat value by applying the current stage multiplier
	 * @param StatType - Which stat stage to check
	 * @param BaseStat - The base stat value before modification
	 * @return The stat value after applying stage multiplier
	 */
	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	float GetModifiedStat(EGF_StatStages StatType, float BaseStat) const;

	/**
	 * Reset all stat stages to 0 (called at battle end or switch out)
	 */
	UFUNCTION(BlueprintCallable, Category = "Stat Stages")
	void ResetAllStatStages();

	/**
	 * Get the current stage value for a specific stat
	 * @param StatType - Which stat to check
	 * @return Current stage value (-6 to +6)
	 */
	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	int32 GetCurrentStage(EGF_StatStages StatType) const;

	/**
	 * Check if a stat is at maximum stage (+6)
	 */
	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	bool IsAtMaxStage(EGF_StatStages StatType) const;

	/**
	 * Check if a stat is at minimum stage (-6)
	 */
	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	bool IsAtMinStage(EGF_StatStages StatType) const;

	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	EGF_StatStages GetRandomStage() const;

	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	bool IsAtLimitForChange(EGF_StatStages StatType, int32 Change) const;



	/**
	 * Get a user-friendly message for a stat stage change
	 * @param StatType - Which stat changed
	 * @param ActualChange - The actual change that occurred (from ApplyStatStageChange)
	 * @param bAffectedSelf - True if this Creature was affected, false if opponent
	 * @param CreatureName - Name of the Creature
	 * @return Formatted message string (e.g., "Voltkit's Attack rose!")
	 */
	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	FString GetStatStageMessage(EGF_StatStages StatType, int32 ActualChange, bool bAffectedSelf, const FString& CreatureName) const;

	UFUNCTION(BlueprintPure, Category = "Stat Stages")
	bool GetIsAtLimit() const;

	UPROPERTY(BlueprintAssignable, Category = "Stat Stages")
	FGF_OnStatStageUpdated OnStatStageUpdated;

private:
	UPROPERTY()
	AGF_Creature* OwningCreature;

	UPROPERTY()
	bool bStatAtLimit = false;

	// Helper to get pointer to the appropriate stat stage
	int32* GetStatStagePointer(EGF_StatStages StatType);
	const int32* GetStatStagePointerConst(EGF_StatStages StatType) const;

	// Calculate multiplier from stage value
	static float CalculateMultiplier(int32 Stage);




};