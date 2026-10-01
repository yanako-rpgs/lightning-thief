#pragma once

#include "CoreMinimal.h"

struct FGF_CreatureInstanceData;
class AGF_Creature;

/**
 * The one-way valve between GammaFrameworkCreatures and GammaFrameworkBattle.
 *
 * Same rule as FGF_CreatureBridge one layer up: Battle depends on Creatures,
 * never the reverse, so the data layer can survive the battle system being
 * replaced. Creatures is the data model -- species, instances, party, vault,
 * breeding, trading. It must not name a battle type.
 *
 * Two places in the data layer legitimately want a battle answer, and both go
 * through here. Every hook has a neutral default, so with GammaFrameworkBattle
 * disabled the data layer behaves as though no battle is in progress -- which
 * is exactly true.
 *
 * GammaFrameworkBattle binds these in its module startup.
 */
struct GAMMAFRAMEWORKCREATURES_API FGF_BattleBridge
{
	/**
	 * Is this actor prevented from fleeing a wild battle right now?
	 *
	 * Covers trapping skills and the traits that ignore them. Default false:
	 * with no battle layer nothing can trap anything, so escape always works.
	 */
	static TFunction<bool(AGF_Creature* User)> IsEscapeBlocked;

	/**
	 * Let a held item modify EXP gained. Default is the identity -- EXP passes
	 * through untouched rather than being zeroed.
	 */
	static TFunction<int32(UObject* WorldContext,
	                       const FGF_CreatureInstanceData& Creature,
	                       int32 BaseEXP)> ApplyHeldItemEXPBoost;

	// ── Safe accessors ───────────────────────────────────────────────────────

	static bool  EscapeBlocked(AGF_Creature* User);
	static int32 HeldItemEXPBoost(UObject* WorldContext,
	                              const FGF_CreatureInstanceData& Creature,
	                              int32 BaseEXP);
	static bool  IsBattleLayerPresent();
};
