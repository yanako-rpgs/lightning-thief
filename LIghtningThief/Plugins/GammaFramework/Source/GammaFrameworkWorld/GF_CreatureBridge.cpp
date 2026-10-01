#include "GF_CreatureBridge.h"

TFunction<float(const UObject*)>                          FGF_CreatureBridge::GetEncounterRateMultiplier;
TFunction<void(const UObject*, FGF_GridCoordinate)>       FGF_CreatureBridge::RegisterDungeonEntrance;
TFunction<void(const UObject*)>                           FGF_CreatureBridge::ClearDungeonState;
TFunction<void(const UObject*)>                           FGF_CreatureBridge::DeleteCreatureSave;
TFunction<bool(const UObject*)>                           FGF_CreatureBridge::LoadCreatureSave;
TFunction<uint8(const UObject*)>                          FGF_CreatureBridge::GetPlayerGender;
TFunction<int32(const UObject*)>                          FGF_CreatureBridge::GetEmblemCount;
TFunction<void(const UObject*, const FGF_SettingsMirror&)> FGF_CreatureBridge::PushSettingsMirror;
TFunction<bool(const UObject*, FName, int32, int32&)>     FGF_CreatureBridge::GiveCreature;
TFunction<FString(const UObject*, int32)>                 FGF_CreatureBridge::GetVaultPageName;

float FGF_CreatureBridge::EncounterRateMultiplier(const UObject* WorldContext)
{
	// 1.0 rather than 0.0: an unbound hook must leave the caller's arithmetic
	// untouched, not silently switch wild encounters off.
	return GetEncounterRateMultiplier ? GetEncounterRateMultiplier(WorldContext) : 1.0f;
}

uint8 FGF_CreatureBridge::PlayerGender(const UObject* WorldContext)
{
	return GetPlayerGender ? GetPlayerGender(WorldContext) : 0;
}

int32 FGF_CreatureBridge::EmblemCount(const UObject* WorldContext)
{
	return GetEmblemCount ? GetEmblemCount(WorldContext) : 0;
}

bool FGF_CreatureBridge::IsCreatureLayerPresent()
{
	// One representative hook stands in for the whole set: they are all bound
	// together in GammaFrameworkCreatures' module startup, or none are.
	return static_cast<bool>(GetEncounterRateMultiplier);
}
