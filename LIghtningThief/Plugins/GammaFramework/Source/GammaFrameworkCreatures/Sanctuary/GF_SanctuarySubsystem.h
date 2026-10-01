// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_CreatureInstanceData.h"
#include "GF_SanctuaryTypes.h"
#include "GF_ResettableState.h"
#include "GF_SanctuarySubsystem.generated.h"

class UGF_CreatureManagerSubsystem;
class UGF_VaultSystem;
class UGF_GridMovementComponent;

/** The sanctuary man is outside holding an egg. Show his "we found an egg!" dialogue. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnSanctuaryEggAvailable);

/** An egg in the party ran out of cycles. Play the hatch cutscene, then call HatchEgg. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnEggReadyToHatch, int32, PartyIndex);

/** A boarded Creature gained a level while walking. Only useful for debug readouts. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnSanctuaryLevelGained, int32, SlotIndex, int32, NewLevel);

/**
 * Creature Day Care, following the classic rules.
 *
 * The rules it implements:
 *   - Two slots. Each boarded Creature earns exactly 1 EXP per step the player walks.
 *   - EXP is banked, not applied — the Creature only actually levels up when you
 *     collect it, which is why the sanctuary lady can quote a price up front.
 *   - Collecting costs 100 + 100 per level gained.
 *   - On collection the Creature levels up, auto-learns its level-up moves (pushing
 *     out its oldest move when all four slots are full — it does NOT ask you), and
 *     comes back fully healed with full Uses.
 *   - Every 256 steps the game rolls for an egg using the pair's compatibility
 *     score as a straight percent chance.
 *   - Every 256 steps each egg in the party burns one egg cycle; at zero it hatches.
 *     Two cycles instead of one if a non-egg party member has Emberhide or
 *     Scorchhide (a classic effect, kept here as a quality-of-life change).
 *
 * STEP DRIVER: call SanctuaryStep() once per overworld step, or call
 * BindStepSource() with the player's UGF_GridMovementComponent and it will hook
 * OnStepCompleted itself.
 */
UCLASS(BlueprintType)
class GAMMAFRAMEWORKCREATURES_API UGF_SanctuarySubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Steps between egg rolls and egg-cycle ticks. 256 in every classic game. */
	static constexpr int32 StepsPerCycle = 256;

	//--------------------
	// EVENTS
	//--------------------

	UPROPERTY(BlueprintAssignable, Category = "Sanctuary|Events")
	FGF_OnSanctuaryEggAvailable OnSanctuaryEggAvailable;

	UPROPERTY(BlueprintAssignable, Category = "Sanctuary|Events")
	FGF_OnEggReadyToHatch OnEggReadyToHatch;

	UPROPERTY(BlueprintAssignable, Category = "Sanctuary|Events")
	FGF_OnSanctuaryLevelGained OnSanctuaryLevelGained;

	//--------------------
	// STEP DRIVER
	//--------------------

	/**
	 * Advance the sanctuary by one overworld step: bank EXP for both boarded
	 * Creature, tick the 256-step cycle, roll for an egg and count down party eggs.
	 * Safe to call when the sanctuary is empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Steps")
	void SanctuaryStep();

	/**
	 * Hook the player's grid movement so every completed step drives the sanctuary
	 * automatically. Call this once after the player pawn exists (BeginPlay is
	 * fine) — re-binding the same component is harmless.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Steps")
	void BindStepSource(UGF_GridMovementComponent* MovementComponent);

	/** Stop driving the sanctuary from a previously bound movement component. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Steps")
	void UnbindStepSource(UGF_GridMovementComponent* MovementComponent);

	//--------------------
	// DEPOSIT / WITHDRAW
	//--------------------

	/** True if there's a free sanctuary slot. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary")
	bool HasFreeSlot() const;

	/** Index of the first free slot, or -1 when both are taken. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary")
	int32 GetFirstFreeSlot() const;

	/** How many Creature are currently boarded (0-2). */
	UFUNCTION(BlueprintPure, Category = "Sanctuary")
	int32 GetOccupiedSlotCount() const;

	/**
	 * Can this party member be handed over? Fails for eggs, for a full sanctuary,
	 * and when it would leave the player without a Creature to battle with.
	 * OutReason is player-facing text you can put straight into a dialogue box.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary")
	bool CanDepositPartyCreature(int32 PartyIndex, FText& OutReason) const;

	/**
	 * Hand a party Creature over to the sanctuary. Removes it from the party and
	 * records the level it arrived at for the fee calculation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary")
	bool DepositPartyCreature(int32 PartyIndex, int32& OutSlotIndex);

	/** What the player owes to get this one back: 100 + 100 per level gained. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary")
	int32 GetWithdrawCost(int32 SlotIndex) const;

	/** True if the player can afford the fee and has a free party slot. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary")
	bool CanWithdraw(int32 SlotIndex, FText& OutReason) const;

	/**
	 * Take a Creature back: charges the fee, applies every banked EXP point,
	 * auto-learns level-up moves, fully heals it and puts it in the party.
	 * OutLearnedSkills lists everything it picked up while boarded, and
	 * OutForgottenSkills the moves that got pushed out to make room — show both
	 * in the "while it was away..." dialogue.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary")
	bool WithdrawCreature(int32 SlotIndex, FGF_CreatureInstanceData& OutCreature,
		TArray<TSoftClassPtr<AGF_SkillDefinition>>& OutLearnedSkills,
		TArray<TSoftClassPtr<AGF_SkillDefinition>>& OutForgottenSkills);

	//--------------------
	// UI QUERIES
	//--------------------

	/** Everything the sanctuary UI needs for one slot in a single call. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|UI")
	bool GetSlotPreview(int32 SlotIndex, FGF_SanctuarySlotPreview& OutPreview) const;

	/** The raw stored Creature for a slot, unmodified by banked EXP. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|UI")
	bool GetSlotCreature(int32 SlotIndex, FGF_CreatureInstanceData& OutCreature) const;

	/** The level a boarded Creature would come out at right now. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary|UI")
	int32 GetProjectedLevel(int32 SlotIndex) const;

	/** Total EXP banked so far for a slot — one point per step walked. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary|UI")
	int32 GetBankedEXP(int32 SlotIndex) const;

	//--------------------
	// BREEDING
	//--------------------

	/** How well the current pair gets along. Incompatible when fewer than two are boarded. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary|Breeding")
	EGF_SanctuaryCompatibility GetCompatibility() const;

	/**
	 * The Day-Care Man's line about the pair — the "do they like each other"
	 * answer. Returns his no-pair line when fewer than two Creature are boarded.
	 */
	UFUNCTION(BlueprintPure, Category = "Sanctuary|Breeding")
	FText GetCompatibilityText() const;

	/** True while the sanctuary man is holding an egg for the player. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary|Breeding")
	bool IsEggWaiting() const;

	/**
	 * Accept the waiting egg. Builds it from the two current parents and puts it
	 * in the party — the player needs a free party slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Breeding")
	bool CollectEgg(FGF_CreatureInstanceData& OutEgg);

	/** Turn the egg down. The sanctuary man keeps looking after the parents. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Breeding")
	void RejectEgg();

	//--------------------
	// EGG HATCHING
	//--------------------

	/** Party index of the first egg that's ready to hatch, or -1 if none is. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary|Eggs")
	int32 GetEggReadyToHatch() const;

	/**
	 * Hatch a party egg into the Creature inside. Call this at the end of your
	 * hatch cutscene; OutHatched is the finished Creature so you can show
	 * "<name> hatched from the egg!" and offer a nickname.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Eggs")
	bool HatchEgg(int32 PartyIndex, FGF_CreatureInstanceData& OutHatched);

	/** How many egg cycles this party egg has left, or -1 if the slot isn't an egg. */
	UFUNCTION(BlueprintPure, Category = "Sanctuary|Eggs")
	int32 GetEggCyclesRemaining(int32 PartyIndex) const;

	//--------------------
	// DEBUG
	//--------------------

	/** Skip the wait: put an egg in the sanctuary man's hands right now. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Debug")
	void DebugForceEgg();

	/** Skip the wait: bring every party egg down to one cycle from hatching. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Debug")
	void DebugFastHatch();

	/** Bank EXP as if the player had walked this many steps. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Debug")
	void DebugAddSteps(int32 Steps);

	/** Dump the sanctuary's whole state to the log. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Debug")
	void DebugPrintSanctuary() const;

	/** DEBUG: wipe the sanctuary clean — empties both slots (boarded Creature are LOST),
	 *  clears any waiting egg and the step counter. For un-wedging test saves only. */
	UFUNCTION(BlueprintCallable, Category = "Sanctuary|Debug")
	void DebugResetSanctuary();

private:
	/** Resolve the manager subsystem lazily — subsystem init order isn't guaranteed. */
	UGF_CreatureManagerSubsystem* GetManager() const;

	/** The save object the sanctuary lives on, or null if the save isn't up yet. */
	UGF_VaultSystem* GetVaultSystem() const;

	/** Mutable sanctuary save data, with the slot array sized correctly. Null if no save. */
	FGF_SanctuaryData* GetSanctuaryData() const;

	/** Fires when the bound movement component finishes a step. */
	UFUNCTION()
	void HandleStepCompleted();

	/** Level a boarded Creature would reach after its banked EXP is applied. */
	int32 CalculateLevelAfterBankedEXP(const FGF_CreatureInstanceData& Mon, int32 BankedEXP) const;

	/**
	 * Apply banked EXP for real: level up, auto-learn level-up moves (dropping the
	 * oldest move when the moveset is full), then fully restore HP, Uses and status.
	 */
	void ApplySanctuaryGrowth(int32 SlotIndex, FGF_CreatureInstanceData& Mon, int32 BankedEXP,
		TArray<TSoftClassPtr<AGF_SkillDefinition>>& OutLearnedSkills,
		TArray<TSoftClassPtr<AGF_SkillDefinition>>& OutForgottenSkills) const;

	/** Roll for an egg, and burn one cycle off every egg in the party. */
	void RunCycleTick();

	/** Movement components we're currently listening to. */
	UPROPERTY()
	TArray<TWeakObjectPtr<UGF_GridMovementComponent>> BoundStepSources;

public:
	// IGF_ResettableState - the sanctuary's DATA lives on UGF_VaultSystem::Sanctuary, so
	// UGF_CreatureManagerSubsystem's reset covers it. What is ours is the bookkeeping of
	// which movement components we bound to; those belong to the outgoing world and
	// are about to be destroyed.
	//
	// Note we only forget the list - we do NOT unbind. OnStepCompleted is shared with
	// egg hatching and step counting, and unbinding it here would kill both.
	virtual void ResetToBootState() override
	{
		BoundStepSources.Empty();
	}
};
