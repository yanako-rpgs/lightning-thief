#include "GF_RouteData.h"

#include "GF_CreatureManagerSubsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

UGF_CreatureSpeciesData* UGF_RouteData::ResolveEncounterSpecies(const UObject* WorldContextObject, const FGF_WildEncounterSlot& Slot)
{
    if (Slot.Species.IsNull())
    {
        return nullptr;
    }

    // Prefer the manager: it keeps one resident copy per species, so repeated
    // encounters on the same route do not re-resolve the path every time.
    if (const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
    {
        if (const UGameInstance* GI = World->GetGameInstance())
        {
            if (const UGF_CreatureManagerSubsystem* Manager = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
            {
                // The slot stores a path; the manager is keyed by species name, and
                // the asset name is not guaranteed to match it. Load through the
                // soft pointer, then let the manager cache what came back.
                if (UGF_CreatureSpeciesData* Loaded = Slot.Species.LoadSynchronous())
                {
                    return Manager->GetCreatureSpeciesData(Loaded->SpeciesName);
                }
                return nullptr;
            }
        }
    }

    return Slot.Species.LoadSynchronous();
}

FText UGF_RouteData::GetFormattedDisplayName(const FString& PlayerName) const
{
    FString Raw = DisplayName.ToString();

    // Replace {PlayerName} token if present
    if (Raw.Contains(TEXT("{PlayerName}")))
    {
        Raw = Raw.Replace(TEXT("{PlayerName}"), *PlayerName, ESearchCase::CaseSensitive);
        return FText::FromString(Raw);
    }

    return DisplayName;
}

FGF_WildEncounterSlot UGF_RouteData::PickFromTable(const TArray<FGF_WildEncounterSlot>& Table) const
{
    if (Table.Num() == 0) return FGF_WildEncounterSlot();

    // Sum all weights
    int32 TotalWeight = 0;
    for (const FGF_WildEncounterSlot& Slot : Table)
        TotalWeight += FMath::Max(Slot.Weight, 1);

    // Roll
    int32 Roll = FMath::RandRange(0, TotalWeight - 1);
    int32 Cumulative = 0;

    for (const FGF_WildEncounterSlot& Slot : Table)
    {
        Cumulative += FMath::Max(Slot.Weight, 1);
        if (Roll < Cumulative)
            return Slot;
    }

    return Table.Last();
}

FGF_WildEncounterSlot UGF_RouteData::PickRandomGrassEncounter() const
{
    return PickFromTable(GrassEncounters);
}

FGF_WildEncounterSlot UGF_RouteData::PickRandomSurfEncounter() const
{
    return PickFromTable(SurfEncounters);
}

FGF_WildEncounterSlot UGF_RouteData::PickRandomFishingEncounter(EGF_FishingRod Rod) const
{
    switch (Rod)
    {
        case EGF_FishingRod::SimpleRod:   return PickFromTable(OldRodEncounters);
        case EGF_FishingRod::KeenRod:  return PickFromTable(GoodRodEncounters);
        case EGF_FishingRod::DeepRod: return PickFromTable(SuperRodEncounters);
        default:                    return FGF_WildEncounterSlot();
    }
}

FGF_WildEncounterSlot UGF_RouteData::PickRandomRockSmashEncounter() const
{
    return PickFromTable(RockSmashEncounters);
}

bool UGF_RouteData::AllowsTeleportOut() const
{
    switch (TeleportRule)
    {
        case EGF_AreaTeleportRule::AlwaysAllow: return true;
        case EGF_AreaTeleportRule::AlwaysBlock: return false;
        default: break; // Auto - fall through to the AreaType default
    }

    switch (AreaType)
    {
        case EGF_AreaType::Route:
        case EGF_AreaType::City:
        case EGF_AreaType::Forest:
        case EGF_AreaType::Water:
            return true;

        case EGF_AreaType::Cave:      // Escape Rope / Dig territory
        case EGF_AreaType::Building:
        default:
            return false;
    }
}
