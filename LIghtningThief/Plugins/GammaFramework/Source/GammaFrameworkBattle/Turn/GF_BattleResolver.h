#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_BattleTypes.h"
#include "GF_BattleComponent.h"
#include "GF_SkillDefinition.h"
#include "GF_StatusTypes.h"
#include "GF_WeatherTypes.h"
#include "GF_BattleResolver.generated.h"

class AGF_Creature;
class AGF_SkillDefinition;
class UGF_BattleBoard;
class UGF_BattleComponent;

/**
 * ---------------------------------------------------------------------------
 * Turning an order into numbers, and then into HP.
 * ---------------------------------------------------------------------------
 *
 * Split in two on purpose:
 *
 *   PlanAction(...)       works out what WOULD happen. Touches nothing.
 *   ApplyResolution(...)  makes it happen.
 *
 * Between those two calls is where the animation plays, the dialogue prints and
 * the camera moves -- with the numbers already known, so a health bar can tween
 * to a value the game has already decided on. That is the workflow the existing
 * CalculateDamageWithoutSpawning was built for; this keeps it and extends it to
 * a board four seats wide.
 *
 * The damage formula itself is untouched. Element effectiveness, the
 * Physical/Magic split, stat stages, crits, traits, held items and weather are
 * all still UGF_BattleComponent's. What is new here is everything four-a-side
 * adds: looping a spread skill over its targets, the multi-target discount, and
 * Brace.
 */

//======================================================================================
// EFFECT OUTCOMES
//======================================================================================

/**
 * Whether a status landed on one creature, and why not when it did not.
 *
 * Rolled during PlanAction, carried out during ApplyResolution. The roll has to
 * happen in the plan so the plan is final; the messages come out of the apply
 * because they describe something that has already been done.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_StatusOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	EGF_STATUSEffect Status = EGF_STATUSEffect::None;

	/** The chance roll passed. Says nothing about whether it was allowed to land. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bRolled = false;

	/**
	 * Element immunity, a protective trait, or a hardcoded immunity refused it.
	 * BlockMessage says which -- print that rather than a generic failure, or the
	 * player has no idea a trait stopped them.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bBlocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText BlockMessage;

	/** It actually landed. False when the target already had a status. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bApplied = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText Message;
};

/** One stat stage change on one creature. */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_StatStageOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	EGF_StatStages Stat = EGF_StatStages::None;

	/** What the skill asked for, signed. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 Requested = 0;

	/**
	 * What actually moved, after the -6..+6 clamp and any trait block.
	 * Zero with bAtLimit set is "it won't go any higher" -- a different message
	 * from "nothing happened".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 ActualChange = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bAtLimit = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bBlockedByTrait = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bSelfInflicted = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText Message;
};

//======================================================================================
// RESULTS
//======================================================================================

/** What one action did to one seat. */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_TargetResolution
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_BattleSlot Target;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TObjectPtr<AGF_Creature> Creature = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText DisplayName;

	/** The formula's own verdict: crit, effectiveness, immunity, miss, message. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_BattleDamageResult Damage;

	/** Damage after the spread discount and Brace. This is what ApplyResolution subtracts. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float DamageToApply = 0.0f;

	/** How many times the skill struck. 1 for everything that is not a multi-hit skill. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 HitCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float HPBefore = 0.0f;

	/** Only meaningful after ApplyResolution. Before that it equals HPBefore. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float HPAfter = 0.0f;

	/** True once applied and this seat's HP hit zero. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bDowned = false;

	/** The 0.65 spread discount was applied because the action hit more than one seat. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bSpreadDiscounted = false;

	/** The target was Bracing, so incoming damage was halved. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bReducedByBrace = false;

	/** The target was mid-Fly / mid-Dig and could not be hit. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bSemiInvulnerable = false;

	/** The target was Protected and the skill does not ignore it. Nothing landed. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bBlockedByProtect = false;

	/**
	 * The skill reached this seat: not missed, not immune, not protected.
	 * Every secondary effect below is gated on this.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bConnected = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_StatusOutcome Status;

	/** Stat changes inflicted ON THIS TARGET. Self-buffs live on the action instead. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TArray<FGF_StatStageOutcome> StatStages;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bFlinched = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText FlinchMessage;

	/** A trapping skill took hold this hit. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bTrapped = false;
};

/** What one action did, across every seat it reached. */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_ActionResolution
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_BattleSlot Actor;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TObjectPtr<AGF_Creature> ActorCreature = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText ActorDisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	EGF_BattleActionType ActionType = EGF_BattleActionType::None;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TSubclassOf<AGF_SkillDefinition> SkillClass;

	/** Which slot's Uses to spend. INDEX_NONE for Last Resort and non-skill actions. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 SkillIndexToSpend = INDEX_NONE;

	/** Nothing happened. Print FailMessage rather than a generic "But it failed!". */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bFailed = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText FailMessage;

	/**
	 * A Flee's verdict. Rolled once by the flow when the turn begins and copied
	 * here, so the message and the battle ending read the same roll -- a second
	 * roll at either point could print an escape the flow then refuses.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bFleeEscaped = false;

	/** The odds that roll was made against, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float FleeChance = 0.0f;

	/** The other side's fastest creature, which ran the fleeing side down. Null on an escape. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TObjectPtr<AGF_Creature> FleeBlocker = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText FleeBlockerDisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TArray<FGF_TargetResolution> Targets;

	/** Recoil. Subtracted from the actor by ApplyResolution. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float SelfDamage = 0.0f;

	/** Drain and self-heal. Added to the actor by ApplyResolution. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float SelfHeal = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float ActorHPBefore = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float ActorHPAfter = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bActorDowned = false;

	/**
	 * Stat changes the skill puts on its USER -- Swords Dance, Growl's own drop.
	 * Separate from the per-target list so a self-targeting buff is applied once
	 * for the action rather than once per seat it nominally reaches.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TArray<FGF_StatStageOutcome> SelfStatStages;

	/** Bramblehide, Livewire and the rest, fired by touching the target. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TArray<FText> ContactMessages;

	/** Status a contact trait put back on the attacker. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_StatusOutcome ContactStatusOnSelf;

	/** The user's own status was cleared (Refresh). */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bSelfStatusCleared = false;

	/** Protect went up this round. False when the consecutive-use roll failed. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bProtectSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bCritStageRaised = false;

	/** Drain turned into damage because the target has Foulsap. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bDrainBackfired = false;

	/** Guards against a plan being applied twice. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bApplied = false;
};

/** One creature's end-of-round tick. */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_RoundEndTick
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_BattleSlot Slot;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TObjectPtr<AGF_Creature> Creature = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float StatusDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float WeatherDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float TrapDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float TraitHeal = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bTrapEnded = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bStatusCured = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float HPBefore = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float HPAfter = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bDowned = false;

	/** Lines to print, in order. Empty when this creature had a quiet round. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TArray<FText> Messages;

	/** False when nothing at all happened -- skip this entry entirely. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bAnythingHappened = false;
};

/**
 * Chip damage fractions, as a share of MaxHP.
 *
 * Here rather than hardcoded because these are balance numbers, and the first
 * thing anyone tunes on a four-a-side board is chip damage -- eight creature
 * ticking at 1/8 a round is twice the attrition the same numbers produce in a
 * one-on-one fight.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_ChipDamageConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BurnFraction = 0.125f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PoisonFraction = 0.125f;

	/** Sandstorm and Hail. Stone and Ferrous ignore sand; Frost ignores hail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WeatherFraction = 0.0625f;
};

//======================================================================================
// THE RESOLVER
//======================================================================================

UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_BattleResolver : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//==================================================================================
	// PLAN
	//==================================================================================

	/**
	 * Work out what this action would do, without changing anything.
	 *
	 * Rolls accuracy and crits, so the answer is final -- calling it twice gives
	 * two different battles. Plan once, show it, apply it.
	 *
	 * @param DamageComponent  The component that owns the damage formula. Get it
	 *                         from UGF_BattleFlowComponent::GetDamageComponent.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	static FGF_ActionResolution PlanAction(
		UGF_BattleComponent* DamageComponent,
		const UGF_BattleBoard* Board,
		const FGF_BattleAction& Action,
		EGF_WeatherType Weather = EGF_WeatherType::None);

	/**
	 * Commit a plan: HP moves, Uses are spent, downs are flagged.
	 *
	 * Refuses to run twice on the same resolution. A second application would
	 * subtract the same damage again, and the usual way that happens is a retry
	 * after a presentation hiccup.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	static bool ApplyResolution(UPARAM(ref) FGF_ActionResolution& Resolution);

	/**
	 * Plan and apply in one call, for anything that does not need the gap --
	 * the enemy's turn in a fast-forwarded battle, or a headless test.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	static FGF_ActionResolution ResolveActionImmediately(
		UGF_BattleComponent* DamageComponent,
		const UGF_BattleBoard* Board,
		const FGF_BattleAction& Action,
		EGF_WeatherType Weather = EGF_WeatherType::None);

	//==================================================================================
	// END OF ROUND
	//==================================================================================

	/**
	 * Status damage, weather, traps, trait ticks and held items for every seat.
	 *
	 * Applies as it goes and hands back the per-creature story so the round-end
	 * sequence can play one message at a time. Order is fixed: status, weather,
	 * trap, then traits and held items, so the same board always ticks the same
	 * way regardless of seat order.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	/**
	 * One creature's end-of-round tick, computed and applied on its own.
	 *
	 * Exposed separately so a presentation layer can walk the board a creature
	 * at a time and let each burn drain its own bar before the next begins.
	 * Resolving all eight at once is correct but unreadable.
	 *
	 * The per-creature order is unchanged -- chip, then the downed check, then
	 * traits and held items -- because that chain is what lets a Sustain Charm
	 * save a creature the chip would otherwise have taken down.
	 *
	 * bAnythingHappened is false when there was nothing to report.
	 */
	static FGF_RoundEndTick ResolveRoundEndForSlot(
		UObject* WorldContext,
		const UGF_BattleBoard* Board,
		const FGF_BattleSlot& Slot,
		EGF_WeatherType Weather,
		const FGF_ChipDamageConfig& Config);

	static TArray<FGF_RoundEndTick> ResolveRoundEnd(
		UObject* WorldContext,
		const UGF_BattleBoard* Board,
		EGF_WeatherType Weather,
		const FGF_ChipDamageConfig& Config);

	//==================================================================================
	// PIECES
	//==================================================================================

	/** Put a creature into the Brace stance: half damage in, +1 Poise. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	static bool ApplyBrace(AGF_Creature* Creature);

	/** Spend one Use. No-op for an invalid slot, so Last Resort costs nothing. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	static void SpendSkillUses(AGF_Creature* Creature, int32 SkillIndex);

	/**
	 * Tick sleep at the START of the sleeper's turn and report whether it woke.
	 *
	 * Deliberately not part of the end-of-round pass: a creature that falls
	 * asleep and has its counter ticked in the same round loses a turn it never
	 * got the chance to lose.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	static bool TickSleepAtTurnStart(AGF_Creature* Creature, bool& bWokeUp);

	/**
	 * Apply one stat stage change and describe what happened.
	 *
	 * Wraps the stage component so callers get the trait block, the at-limit case
	 * and the message in one struct instead of three separate reads afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	static FGF_StatStageOutcome ApplyStatStage(AGF_Creature* Creature, EGF_StatStages Stat, int32 Amount, bool bSelfInflicted);

	/** A random stat to move, for the skills that do not name one. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	static EGF_StatStages PickRandomStatStage(bool bRaise);

	/** Damage after the spread discount and the target's Brace. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Resolve")
	static float ApplyDamageModifiers(float RawDamage, const AGF_Creature* Target, int32 TargetCount,
		float MultiTargetMultiplier, bool& bOutSpreadDiscounted, bool& bOutBraced);

	/** Does this element ignore the current weather's chip damage? */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Resolve")
	static bool IsImmuneToWeatherChip(EGF_Element Element, EGF_WeatherType Weather);

private:
	/** Stat-stage multipliers off the creature's stage component, or 1.0 when it has none. */
	static void GatherStatMultipliers(const AGF_Creature* Creature,
		float& OutAttack, float& OutDefense, float& OutMagic, float& OutPoise, float& OutSpeed);

	static void ApplyHPDelta(AGF_Creature* Creature, float Delta, float& OutHPAfter, bool& bOutDowned);
};
