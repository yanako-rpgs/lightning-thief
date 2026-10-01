#include "GammaFrameworkCreatures.h"
#include "Modules/ModuleManager.h"

#include "GF_CreatureBridge.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_VaultSystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/GameInstanceSubsystem.h"

/**
 * Binds every FGF_CreatureBridge hook.
 *
 * GammaFrameworkWorld cannot reference this module -- UBT forbids the circular
 * dependency, and the split exists so a project can take the grid/dialogue/quest
 * tech on its own. So the creature layer registers itself here at module
 * startup, and World calls through function pointers that are simply unbound when
 * this module is absent.
 *
 * Every hook is a lazy lookup rather than a cached pointer: module startup runs
 * long before any GameInstance exists, so there is nothing to cache yet.
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
		// A GameInstanceSubsystem is not a world context object, so ask it
		// directly for the instance it belongs to.
		if (const UGameInstanceSubsystem* Sub = Cast<UGameInstanceSubsystem>(WorldContext))
		{
			return Sub->GetGameInstance();
		}
		return Cast<UGameInstance>(const_cast<UObject*>(WorldContext));
	}

	UGF_CreatureManagerSubsystem* ManagerFrom(const UObject* WorldContext)
	{
		UGameInstance* GI = GameInstanceFrom(WorldContext);
		return GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
	}

	UGF_VaultSystem* VaultFrom(const UObject* WorldContext)
	{
		UGF_CreatureManagerSubsystem* Manager = ManagerFrom(WorldContext);
		return Manager ? Manager->VaultSystem : nullptr;
	}

	void BindCreatureBridge()
	{
		// NOTE: GetEncounterRateMultiplier is deliberately NOT bound here.
		// It is a trait EFFECT, so GammaFrameworkBattle binds it instead -- this
		// module must not reference the trait effect library, which lives there.
		// With Battle disabled the hook stays unbound and the multiplier is 1.0,
		// which is correct: no battle layer, no trait modifying encounter rates.

		FGF_CreatureBridge::RegisterDungeonEntrance = [](const UObject* Ctx, FGF_GridCoordinate ReturnTile)
		{
			if (UGF_CreatureManagerSubsystem* Manager = ManagerFrom(Ctx))
			{
				Manager->RegisterDungeonEntrance(ReturnTile);
			}
		};

		FGF_CreatureBridge::ClearDungeonState = [](const UObject* Ctx)
		{
			if (UGF_CreatureManagerSubsystem* Manager = ManagerFrom(Ctx))
			{
				Manager->ClearDungeonState();
			}
		};

		FGF_CreatureBridge::DeleteCreatureSave = [](const UObject* Ctx)
		{
			if (UGF_CreatureManagerSubsystem* Manager = ManagerFrom(Ctx))
			{
				Manager->DeleteSave();
			}
		};

		FGF_CreatureBridge::LoadCreatureSave = [](const UObject* Ctx) -> bool
		{
			UGF_CreatureManagerSubsystem* Manager = ManagerFrom(Ctx);
			return Manager ? Manager->LoadGame() : false;
		};

		FGF_CreatureBridge::GetPlayerGender = [](const UObject* Ctx) -> uint8
		{
			const UGF_VaultSystem* Vault = VaultFrom(Ctx);
			return Vault ? static_cast<uint8>(Vault->PlayerGender) : 0;
		};

		FGF_CreatureBridge::GetEmblemCount = [](const UObject* Ctx) -> int32
		{
			const UGF_VaultSystem* Vault = VaultFrom(Ctx);
			if (!Vault)
			{
				return 0;
			}

			// The per-emblem bools are the real record of progress.
			// TotalEmblemAmount is a separate counter that is not kept in sync
			// when a trial is beaten, so trusting it alone reports 0 emblems for
			// a player who has earned several -- which pins the level cap at its
			// first-trial value and blocks all EXP.
			int32 Counted = 0;
			Counted += Vault->bHasStoneEmblem   ? 1 : 0;
			Counted += Vault->bHasKnuckleEmblem ? 1 : 0;
			Counted += Vault->bHasDynamoEmblem  ? 1 : 0;
			Counted += Vault->bHasHeatEmblem    ? 1 : 0;
			Counted += Vault->bHasBalanceEmblem ? 1 : 0;
			Counted += Vault->bHasFeatherEmblem ? 1 : 0;
			Counted += Vault->bHasMindEmblem    ? 1 : 0;
			Counted += Vault->bHasRainEmblem    ? 1 : 0;

			return FMath::Max(Counted, Vault->TotalEmblemAmount);
		};

		FGF_CreatureBridge::PushSettingsMirror = [](const UObject* Ctx, const FGF_SettingsMirror& Mirror)
		{
			// One-way copy, settings -> save. Loading a game never writes back
			// through here, so a save cannot clobber the player's current options.
			if (UGF_VaultSystem* Vault = VaultFrom(Ctx))
			{
				Vault->TextSpeed          = Mirror.TextSpeed;
				Vault->FrameID            = Mirror.FrameID;
				Vault->Difficulty         = Mirror.Difficulty;
				Vault->WindowedModes      = Mirror.WindowedModes;
				Vault->Resolution         = Mirror.Resolution;
				Vault->MasterVolume       = Mirror.MasterVolume;
				Vault->SoundEffectsVolume = Mirror.SoundEffectsVolume;
				Vault->MusicVolume        = Mirror.MusicVolume;
				Vault->bEXPShareEnabled   = Mirror.bEXPShareEnabled;
				Vault->bDOFEnabled        = Mirror.bDOFEnabled;
				Vault->bAutoRepelEnabled  = Mirror.bAutoRepelEnabled;
			}
		};

		FGF_CreatureBridge::GiveCreature =
			[](const UObject* Ctx, FName SpeciesName, int32 Level, int32& OutVaultPageIndex) -> bool
		{
			UGF_CreatureManagerSubsystem* Manager = ManagerFrom(Ctx);
			if (!Manager)
			{
				return false;
			}
			UGF_CreatureSpeciesData* Species = Manager->GetCreatureSpeciesData(SpeciesName);
			if (!Species)
			{
				return false;
			}
			return Manager->GiveCreatureAtLevelTracked(Species, Level, OutVaultPageIndex);
		};

		FGF_CreatureBridge::GetVaultPageName = [](const UObject* Ctx, int32 VaultPageIndex) -> FString
		{
			const UGF_VaultSystem* Vault = VaultFrom(Ctx);
			return Vault ? Vault->GetVaultPageName(VaultPageIndex) : FString();
		};
	}

	void UnbindCreatureBridge()
	{
		FGF_CreatureBridge::RegisterDungeonEntrance    = nullptr;
		FGF_CreatureBridge::ClearDungeonState          = nullptr;
		FGF_CreatureBridge::DeleteCreatureSave         = nullptr;
		FGF_CreatureBridge::LoadCreatureSave           = nullptr;
		FGF_CreatureBridge::GetPlayerGender            = nullptr;
		FGF_CreatureBridge::GetEmblemCount             = nullptr;
		FGF_CreatureBridge::PushSettingsMirror         = nullptr;
		FGF_CreatureBridge::GiveCreature               = nullptr;
		FGF_CreatureBridge::GetVaultPageName           = nullptr;
	}
}

class FGammaFrameworkCreaturesModule : public IModuleInterface
{
public:
	virtual void StartupModule() override  { BindCreatureBridge(); }
	virtual void ShutdownModule() override { UnbindCreatureBridge(); }
};

IMPLEMENT_MODULE(FGammaFrameworkCreaturesModule, GammaFrameworkCreatures);
