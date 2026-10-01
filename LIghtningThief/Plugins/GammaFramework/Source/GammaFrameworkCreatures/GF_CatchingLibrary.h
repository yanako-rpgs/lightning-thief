// CatchingLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureInstanceData.h"
#include "GF_ItemData.h"
#include "GF_CatchingLibrary.generated.h"

/**
 * Situational information the conditional Cores need.
 *
 * None of this can be read from the Creature or the item asset, so the battle
 * supplies it. Every field defaults to "no bonus", meaning an unfilled context
 * makes every conditional core behave like a Simple Core rather than
 * silently handing out a multiplier.
 */
USTRUCT(BlueprintType)
struct FGF_CatchContext
{
	GENERATED_BODY()

	/**
	 * True if this species is already registered as caught in the Compendium (Echo Core).
	 * UGF_CreatureManagerSubsystem::AttemptCatch fills this in for you - leave it alone
	 * unless you are calling UGF_CatchingLibrary directly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catching")
	bool bSpeciesAlreadyCaught = false;

	/**
	 * Turn number within this battle, 1-based (Sudden Core, Aeon Core).
	 * 0 means "not tracked" and gives neither core its bonus.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catching")
	int32 TurnCount = 0;

	/** True when surfing, fishing or underwater (Depth Core) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catching")
	bool bIsUnderwater = false;

	/** True in a cave or at night (Gloam Core) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catching")
	bool bIsNightOrCave = false;
};

/**
 * Catch result data for detailed feedback
 */
USTRUCT(BlueprintType)
struct FGF_CatchResult
{
	GENERATED_BODY()

	/** Was the Creature caught? */
	UPROPERTY(BlueprintReadWrite, Category = "Catching")
	bool bCaught = false;

	/** Number of shakes (0-3, 3 = caught) */
	UPROPERTY(BlueprintReadWrite, Category = "Catching")
	int32 ShakeCount = 0;

	/** Was this a critical capture? */
	UPROPERTY(BlueprintReadWrite, Category = "Catching")
	bool bCriticalCapture = false;

	/** Catch probability (0.0 - 1.0) */
	UPROPERTY(BlueprintReadWrite, Category = "Catching")
	float CatchProbability = 0.0f;

	/** Modified catch rate used in calculation */
	UPROPERTY(BlueprintReadWrite, Category = "Catching")
	float ModifiedCatchRate = 0.0f;
};

/**
 * Blueprint Function Library for Creature Catching Calculations
 * Contains pure math functions for catch rates, shake counts, etc.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CatchingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//--------------------
	// MAIN CATCH FUNCTIONS
	//--------------------

	/**
	 * Calculate complete catch attempt with all details
	 * Uses classic/4/5 catching formula
	 *
	 * @param WildCreature - The Creature being caught
	 * @param CoreData - The core being used (must have CatchRateModifier)
	 * @param StatusModifier - Status condition multiplier (1.0=none, 1.5=paralyzed/burned/poisoned, 2.5=asleep/frozen)
	 * @param CompendiumCaught - Number of species caught (for critical capture chance)
	 * @param Context - Situational info for the conditional cores (turn count, terrain, Compendium)
	 * @return Complete catch result with shake count, success, and probability
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static FGF_CatchResult CalculateCatchAttempt(
		const FGF_CreatureInstanceData& WildCreature,
		UGF_ItemData* CoreData,
		const FGF_CatchContext& Context,
		float StatusModifier = 1.0f,
		int32 CompendiumCaught = 0
	);

	/**
	 * Calculate catch probability as percentage (0-100)
	 * For UI display before throwing
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static float GetCatchProbabilityPercent(
		const FGF_CreatureInstanceData& WildCreature,
		UGF_ItemData* CoreData,
		const FGF_CatchContext& Context,
		float StatusModifier = 1.0f
	);

	/**
	 * Simple yes/no catch check without details
	 * For quick testing
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static bool IsCreatureCaught(
		const FGF_CreatureInstanceData& WildCreature,
		UGF_ItemData* CoreData,
		const FGF_CatchContext& Context,
		float StatusModifier = 1.0f
	);

	//--------------------
	// CATCH RATE MODIFIERS
	//--------------------

	/**
	 * Get core catch rate modifier
	 *
	 * Flat cores (Simple/Greater/Hyper/Absolute/Warden/Hearth/Elite/Mend/Boon) return
	 * ItemData->CatchRateModifier straight from the asset. The conditional cores
	 * (Snare, Depth, Cradle, Echo, Aeon, Gloam, Sudden, Omen) resolve their multiplier in
	 * code and IGNORE the asset value, so leave CatchRateModifier at 1.0 on those items.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static float GetCoreModifier(
		UGF_ItemData* CoreData,
		const FGF_CreatureInstanceData& WildCreature,
		const FGF_CatchContext& Context
	);

	/**
	 * Calculate HP-based catch modifier
	 * Formula: (3 * MaxHP - 2 * CurrentHP) / (3 * MaxHP)
	 * Lower HP = higher modifier
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static float CalculateHPModifier(const FGF_CreatureInstanceData& WildCreature);

	/**
	 * Get status condition catch modifier
	 * @param StatusType - Type of status (use your enum)
	 * @return Modifier: 1.0 (none), 1.5 (para/burn/poison), 2.5 (sleep/freeze)
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static float GetStatusModifier(uint8 StatusType);

	//--------------------
	// CORE IDENTITY
	//--------------------

	/**
	 * True for the one Core that cannot fail.
	 *
	 * The catch path itself detects it from the asset (CatchRateModifier >= 255),
	 * because that is what the maths reads. This is the enum-side answer, for UI
	 * and for anything holding a core type without its item asset.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static bool IsAbsoluteCore(EGF_CoreType CoreType);

	/**
	 * Display name for a Core, for UI that has the enum but not the item asset.
	 *
	 * These are the same strings as the enum's UMETA DisplayNames and have to be
	 * kept in step with them by hand.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static FText GetCoreName(EGF_CoreType CoreType);

	//--------------------
	// HELPER FUNCTIONS
	//--------------------

	/**
	 * Calculate shake probability for classic-5
	 * Used internally for shake count
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static int32 CalculateShakeProbability(float ModifiedCatchRate);

	/**
	 * Check if critical capture occurs
	 * Chance increases with Compendium completion
	 * Critical captures skip shakes and catch immediately
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static bool CheckCriticalCapture(int32 CompendiumCaught, float CatchRate);

	/**
	 * Clamp catch rate to valid range
	 * Prevents overflow in calculations
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static float ClampCatchRate(float CatchRate);

	/**
	 * How much more EXP a claim is worth than downing the same Creature.
	 *
	 * Takes the species' BASE catch rate, not the modified one. Rewarding a low
	 * modified rate would pay the player for skipping the things the claim is
	 * meant to encourage -- weakening the target, spending a better Core -- and
	 * would make a Hyper Core cost EXP to use. The species' own elusiveness is
	 * the thing being rewarded, and it is the same number whatever was thrown
	 * at it.
	 *
	 * (255 / Rate)^0.25 is the exponent CalculateShakeProbability already uses
	 * to decide how hard a Creature is to hold. Borrowing it means the reward
	 * and the difficulty cannot drift apart when one of them is retuned.
	 *
	 * Bounded by construction: 1.0x at rate 255, ~1.54x at the default 45,
	 * ~3.04x at 3, and 4.0x at rate 1. No cap needed.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static float GetClaimEXPMultiplier(int32 BaseCatchRate);

	/**
	 * GetClaimEXPMultiplier for a Creature, reading its species' base catch rate.
	 *
	 * The one to call from a battle graph: SpeciesData is a soft pointer, so the
	 * alternative is resolving it by hand at every call site just to reach one
	 * int. Returns 1.0 -- parity with a KO -- if the species will not load.
	 */
	UFUNCTION(BlueprintPure, Category = "Creature|Catching")
	static float GetClaimEXPMultiplierForCreature(const FGF_CreatureInstanceData& Creature);

private:
	/** Generate random shake check (0-65535) */
	static int32 GenerateShakeCheck();

	/** Calculate number of shakes before breaking free */
	static int32 CalculateShakeCount(int32 ShakeProbability);
};