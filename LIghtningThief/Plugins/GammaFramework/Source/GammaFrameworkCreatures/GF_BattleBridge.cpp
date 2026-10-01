#include "GF_BattleBridge.h"
#include "GF_Creature.h"

TFunction<bool(AGF_Creature*)> FGF_BattleBridge::IsEscapeBlocked;
TFunction<int32(UObject*, const FGF_CreatureInstanceData&, int32)>
	FGF_BattleBridge::ApplyHeldItemEXPBoost;

bool FGF_BattleBridge::EscapeBlocked(AGF_Creature* User)
{
	// false, not true: an unbound hook must not accidentally trap the player in
	// a battle that the battle layer is not even running.
	return IsEscapeBlocked ? IsEscapeBlocked(User) : false;
}

int32 FGF_BattleBridge::HeldItemEXPBoost(UObject* WorldContext,
                                         const FGF_CreatureInstanceData& Creature,
                                         int32 BaseEXP)
{
	// Identity by default. Returning 0 here would silently stop all levelling.
	return ApplyHeldItemEXPBoost ? ApplyHeldItemEXPBoost(WorldContext, Creature, BaseEXP)
	                             : BaseEXP;
}

bool FGF_BattleBridge::IsBattleLayerPresent()
{
	return static_cast<bool>(IsEscapeBlocked);
}
