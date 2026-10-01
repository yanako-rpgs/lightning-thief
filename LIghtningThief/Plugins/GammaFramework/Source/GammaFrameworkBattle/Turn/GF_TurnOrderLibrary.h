#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_BattleTypes.h"
#include "GF_WeatherTypes.h"
#include "GF_TurnOrderLibrary.generated.h"

class AGF_Creature;
class AGF_SkillDefinition;
class UGF_BattleBoard;

/**
 * Turn order, in one place, as pure functions.
 *
 * Nothing here holds state or touches the board except to read it, which means
 * the ordering rules can be reasoned about — and changed — without opening the
 * state machine. Priority bracket first, then effective Speed, then a tiebreak
 * that was rolled ONCE when the action was stamped.
 *
 * That last part matters more than it looks. The obvious implementation flips a
 * coin inside the sort comparator, which makes the comparator answer differently
 * for the same pair of actions and produces an order that is not merely random
 * but incoherent. It shows up as a round that occasionally sequences wrong, at a
 * rate too low to reproduce on purpose.
 */
UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_TurnOrderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//==================================================================================
	// SPEED
	//==================================================================================

	/**
	 * Speed this creature actually acts on: base Speed, times its stat stage,
	 * times a weather trait, halved by paralysis.
	 *
	 * Halving last is deliberate and matches the genre — a paralysed creature with
	 * +2 Speed is still slowed relative to itself.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Turn Order")
	static float GetEffectiveSpeed(const AGF_Creature* Creature, EGF_WeatherType Weather = EGF_WeatherType::None);

	/** Highest effective Speed among a side's living creature. 0 when the side is empty. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Turn Order")
	static float GetFastestSpeedOnSide(const UGF_BattleBoard* Board, EGF_BattleSide Side, EGF_WeatherType Weather = EGF_WeatherType::None);

	//==================================================================================
	// PRIORITY
	//==================================================================================

	/**
	 * Priority off the skill asset, read from the class default object.
	 *
	 * No spawn. The old path spawned the skill actor just to read a number off it
	 * and destroyed it again, once per creature per turn.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Turn Order")
	static int32 GetSkillPriority(TSubclassOf<AGF_SkillDefinition> SkillClass);

	/** Priority for any action: the skill's own for Skill, the config bracket otherwise. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Turn Order")
	static int32 GetActionPriority(const FGF_BattleAction& Action, const FGF_ActionPriorityConfig& Config);

	//==================================================================================
	// BUILDING THE ORDER
	//==================================================================================

	/**
	 * Fill in Priority, ResolvedSpeed and TieBreak on an action.
	 *
	 * Called once per action at lock-in. After this the action carries everything
	 * the sort needs, so the sort itself reads no live state and cannot change its
	 * mind halfway through a round.
	 *
	 * @param TieBreakOverride  Pass a value to make the order reproducible (tests,
	 *                          replays). -1 rolls one. Written as a literal
	 *                          because UHT cannot parse INDEX_NONE as a default.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Turn Order")
	static void StampActionForOrdering(
		UPARAM(ref) FGF_BattleAction& Action,
		const UGF_BattleBoard* Board,
		EGF_WeatherType Weather,
		const FGF_ActionPriorityConfig& Config,
		int32 TieBreakOverride = -1);

	/**
	 * Sort stamped actions into resolution order, in place.
	 *
	 * The flow keeps its own copy in this order so entry N of the ribbon and
	 * action N of the round are the same turn. Sorting one and rebuilding the
	 * other independently is how those two drift apart.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Turn Order")
	static void SortActions(UPARAM(ref) TArray<FGF_BattleAction>& Actions);

	/**
	 * The committed sequence for this round, one entry per action, ready for the
	 * ribbon across the top of the screen.
	 *
	 * Expects actions that have already been stamped.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Turn Order")
	static TArray<FGF_TurnOrderEntry> BuildTurnOrder(const UGF_BattleBoard* Board, const TArray<FGF_BattleAction>& StampedActions);

	/**
	 * Best guess at the order while the player is still choosing.
	 *
	 * Every living seat appears. Seats with an order already issued use its real
	 * priority; seats still waiting are ranked on Speed alone at priority 0. Call
	 * it again after each command and the ribbon settles toward the real sequence
	 * as the player commits — which is the point, because at four slots the fourth
	 * order always depends on the first three.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Turn Order")
	static TArray<FGF_TurnOrderEntry> PreviewTurnOrder(
		const UGF_BattleBoard* Board,
		const TArray<FGF_BattleAction>& IssuedActions,
		EGF_WeatherType Weather,
		const FGF_ActionPriorityConfig& Config);

	//==================================================================================
	// TARGETING
	//==================================================================================

	/** Expand a target shape into the seats it actually reaches, right now. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Turn Order")
	static TArray<FGF_BattleSlot> ExpandTargets(
		const UGF_BattleBoard* Board,
		const FGF_BattleSlot& ActorSlot,
		EGF_SkillTargetShape Shape,
		const FGF_BattleSlot& PrimaryTarget);

	//==================================================================================
	// BLUEPRINT CONVENIENCE
	//==================================================================================

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle", meta = (DisplayName = "Equal (Battle Slot)", CompactNodeTitle = "==", ScriptOperator = "=="))
	static bool EqualBattleSlot(const FGF_BattleSlot& A, const FGF_BattleSlot& B);

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle", meta = (DisplayName = "Not Equal (Battle Slot)", CompactNodeTitle = "!=", ScriptOperator = "!="))
	static bool NotEqualBattleSlot(const FGF_BattleSlot& A, const FGF_BattleSlot& B);

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle")
	static bool IsValidBattleSlot(const FGF_BattleSlot& Slot);

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle")
	static FGF_BattleSlot MakeBattleSlot(EGF_BattleSide Side, int32 Index);

	/** "Player[2]". For logs and the debugger, not for the player-facing HUD. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle")
	static FString BattleSlotToString(const FGF_BattleSlot& Slot);

	/** Index of this slot's entry in a turn order array, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Turn Order")
	static int32 FindEntryForSlot(const TArray<FGF_TurnOrderEntry>& TurnOrder, const FGF_BattleSlot& Slot);

private:
	/**
	 * The one comparator. Priority, then Speed, then the pre-rolled tiebreak.
	 *
	 * It compares ACTIONS rather than ribbon entries on purpose: the tiebreak
	 * lives on the action, so the sort runs over the stamped orders and the
	 * display entries are built afterwards, in the order the sort produced.
	 */
	static bool CompareActions(const FGF_BattleAction& A, const FGF_BattleAction& B);

	static FGF_TurnOrderEntry MakeEntry(const UGF_BattleBoard* Board, const FGF_BattleAction& Action, int32 OrderIndex);
};
