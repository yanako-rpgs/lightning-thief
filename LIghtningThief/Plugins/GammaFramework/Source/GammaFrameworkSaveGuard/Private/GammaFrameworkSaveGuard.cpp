#include "CoreMinimal.h"
#include "PlatformFeatures.h"
#include "GF_SaveGuardSystem.h"

/**
 * This has to be a module of its own rather than something bolted onto
 * GammaFramework: IPlatformFeaturesModule::Get() does
 * FModuleManager::LoadModuleChecked<IPlatformFeaturesModule>(ModuleName), so
 * the module object it finds must itself derive from IPlatformFeaturesModule.
 * GammaFramework's is FDefaultGameModuleImpl and cannot be.
 *
 * It is picked up via Config/DefaultEngine.ini:
 *
 *     [PlatformFeatures]
 *     PlatformFeaturesModule=GammaFrameworkSaveGuard
 *
 * which FWindowsPlatformMisc::GetPlatformFeaturesModuleName() reads. Delete
 * that ini section and the engine falls straight back to the stock
 * FGenericSaveGameSystem with no code change needed.
 */
class FGF_GammaFrameworkSaveGuardModule : public IPlatformFeaturesModule
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		UE_LOG(LogGammaFrameworkSaveGuard, Log,
			TEXT("Editor build: saves stay as plain .sav in Saved/SaveGames. Only packaged builds use the vault."));
#else
		UE_LOG(LogGammaFrameworkSaveGuard, Log, TEXT("Saves route through the encrypted vault."));
#endif
	}

	virtual ISaveGameSystem* GetSaveGameSystem() override
	{
#if WITH_EDITOR
		// Deliberate split. In-editor runs keep writing plain .sav files to
		// Saved/SaveGames so the SaveFileEditor plugin keeps working: that plugin
		// reads bytes straight off a path with FFileHelper and never calls the
		// save system it asks for, so it cannot see vault files at all.
		//
		// The cost is that editor and packaged no longer share a save path, so a
		// bug in the vault will not surface until something is packaged. The
		// "gf.SaveVaultSelfTest" console command exercises the vault directly and
		// is the way to check it from here.
		static FGenericSaveGameSystem EditorSaveGame;
		return &EditorSaveGame;
#else
		static FGF_SaveGuardSystem SaveGameSystem;
		return &SaveGameSystem;
#endif
	}
};

IMPLEMENT_MODULE(FGF_GammaFrameworkSaveGuardModule, GammaFrameworkSaveGuard);
