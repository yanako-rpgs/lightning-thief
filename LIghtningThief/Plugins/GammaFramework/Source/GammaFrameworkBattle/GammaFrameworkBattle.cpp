#include "GammaFrameworkBattle.h"
#include "Modules/ModuleManager.h"

#include "GF_BattleBridge.h"          // Creatures -> Battle hooks
#include "GF_CreatureBridge.h"        // World -> creature layer hooks
#include "GF_BattleComponent.h"
#include "GF_HeldItemBattleHelper.h"
#include "GF_CreatureTraits.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_CreatureInstanceData.h"
#include "GF_Creature.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/GameInstanceSubsystem.h"

/**
 * Binds the hooks that only the battle layer can answer.
 *
 * Two bridges meet here:
 *
 *   FGF_BattleBridge   (declared in Creatures) -- the data layer asking a
 *                      combat question: can this creature flee, does a held
 *                      item change EXP.
 *
 *   FGF_CreatureBridge (declared in World) -- specifically the encounter-rate
 *                      hook, which is a TRAIT EFFECT and so lives here rather
 *                      than in Creatures. Everything else on that bridge is
 *                      bound by GammaFrameworkCreatures.
 *
 * Both bridges tolerate this module being absent: their accessors fall back to
 * neutral values, so a project with no battle system still walks around, opens
 * dialogue, manages a party and saves.
 */
namespace
{
	UGameInstance* GameInstanceFrom(const UObject* WorldContext)
	{
		if (!WorldContext || !GEngine)
		{
			return nullptr;
		}
		if (const UWorld* World = GEngine->GetWorldFromContextObject(
				WorldContext, EGetWorldErrorMode::ReturnNull))
		{
			return World->GetGameInstance();
		}
		if (const UGameInstanceSubsystem* Sub = Cast<UGameInstanceSubsystem>(WorldContext))
		{
			return Sub->GetGameInstance();
		}
		return Cast<UGameInstance>(const_cast<UObject*>(WorldContext));
	}

	void BindBattleHooks()
	{
		FGF_BattleBridge::IsEscapeBlocked = [](AGF_Creature* User) -> bool
		{
			// Trapping skills hold the user in place -- unless its trait
			// guarantees an escape from any wild battle.
			return UGF_BattleComponent::IsPartiallyTrapped(User)
				&& !UGF_CreatureTraitLibrary::CanAlwaysFleeFromWild(
						UGF_CreatureTraitLibrary::GetActorTrait(User));
		};

		FGF_BattleBridge::ApplyHeldItemEXPBoost =
			[](UObject* Ctx, const FGF_CreatureInstanceData& Creature, int32 BaseEXP) -> int32
		{
			return UGF_HeldItemBattleHelper::ApplyEXPBoostFromData(Ctx, Creature, BaseEXP);
		};

		// Trait effect, so it belongs to Battle rather than Creatures.
		FGF_CreatureBridge::GetEncounterRateMultiplier = [](const UObject* Ctx) -> float
		{
			UGameInstance* GI = GameInstanceFrom(Ctx);
			const UGF_CreatureManagerSubsystem* Manager =
				GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
			if (Manager)
			{
				FGF_CreatureInstanceData Lead;
				if (Manager->GetPartyCreatureData(0, Lead))
				{
					return UGF_CreatureTraitLibrary::GetEncounterRateMultiplier(
						UGF_CreatureTraitLibrary::GetInstanceTrait(Lead));
				}
			}
			return 1.0f;
		};
	}

	void UnbindBattleHooks()
	{
		FGF_BattleBridge::IsEscapeBlocked          = nullptr;
		FGF_BattleBridge::ApplyHeldItemEXPBoost    = nullptr;
		FGF_CreatureBridge::GetEncounterRateMultiplier = nullptr;
	}
}

class FGammaFrameworkBattleModule : public IModuleInterface
{
public:
	virtual void StartupModule() override  { BindBattleHooks(); }
	virtual void ShutdownModule() override { UnbindBattleHooks(); }
};

IMPLEMENT_MODULE(FGammaFrameworkBattleModule, GammaFrameworkBattle);
