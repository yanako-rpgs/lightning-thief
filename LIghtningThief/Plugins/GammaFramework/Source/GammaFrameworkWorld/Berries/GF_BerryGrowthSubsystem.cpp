// GF_BerryGrowthSubsystem.cpp
#include "GF_BerryGrowthSubsystem.h"

#include "GameFramework/Actor.h"
#include "Engine/Level.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

const FString UGF_BerrySave::SlotName  = TEXT("GEBerrySlot");
const int32   UGF_BerrySave::UserIndex = 0;

namespace
{
    /** Seedling, Sprout, AlmostDone, ReadyToBeHarvested - matches ENUM_BerryTreeState. */
    constexpr int32 BerryStageCount = 4;

    /**
     * "UEDPIE_0_MAP_Route102" -> "MAP_Route102". PIE renames the package but not the
     * actor, so without this a tree harvested in PIE and one harvested in a packaged
     * build would be two different trees in the same save.
     */
    FString StripPIEPrefix(const FString& In)
    {
        static const FString Prefix = TEXT("UEDPIE_");
        if (!In.StartsWith(Prefix))
        {
            return In;
        }

        // UEDPIE_<instance>_<name> - drop the instance number too.
        const FString Rest = In.RightChop(Prefix.Len());
        int32 Underscore = INDEX_NONE;
        return Rest.FindChar(TEXT('_'), Underscore) ? Rest.RightChop(Underscore + 1) : In;
    }
}

void UGF_BerryGrowthSubsystem::ResetToBootState()
{
    BerryTrees.Reset();

    UE_LOG(LogTemp, Log, TEXT("GF_Berries: reset to boot state - every tree is ripe again."));

    OnBerryDataChanged.Broadcast();
}

//--------------------
// QUERIES
//--------------------

FName UGF_BerryGrowthSubsystem::MakeBerryTreeID(const AActor* TreeActor)
{
    if (!IsValid(TreeActor))
    {
        return NAME_None;
    }

    FString LevelName;
    if (const ULevel* Level = TreeActor->GetLevel())
    {
        LevelName = StripPIEPrefix(FPackageName::GetShortName(Level->GetOutermost()->GetName()));
    }

    return FName(*FString::Printf(TEXT("%s.%s"), *LevelName, *TreeActor->GetName()));
}

const FGF_BerryTreeState* UGF_BerryGrowthSubsystem::FindTree(FName TreeID) const
{
    return BerryTrees.FindByPredicate(
        [TreeID](const FGF_BerryTreeState& Tree) { return Tree.TreeID == TreeID; });
}

int32 UGF_BerryGrowthSubsystem::GetBerryTreeStage(FName TreeID, float GrowthHours) const
{
    const FGF_BerryTreeState* Tree = FindTree(TreeID);
    if (!Tree)
    {
        // Never harvested - the tree is as the level author placed it.
        return BerryStageCount - 1;
    }

    // A negative value means the system clock moved back; that reads as freshly
    // planted rather than ripe, which is the safe direction.
    const double ElapsedHours = (FDateTime::UtcNow() - Tree->CycleStartRealUtc).GetTotalHours();
    if (ElapsedHours <= 0.0)
    {
        return 0;
    }

    const double PerStage = FMath::Max(0.01f, GrowthHours) / (BerryStageCount - 1);
    return FMath::Clamp(FMath::FloorToInt(static_cast<float>(ElapsedHours / PerStage)), 0, BerryStageCount - 1);
}

bool UGF_BerryGrowthSubsystem::IsBerryTreeReady(FName TreeID, float GrowthHours) const
{
    return GetBerryTreeStage(TreeID, GrowthHours) >= BerryStageCount - 1;
}

float UGF_BerryGrowthSubsystem::GetBerryTreeHoursRemaining(FName TreeID, float GrowthHours) const
{
    const FGF_BerryTreeState* Tree = FindTree(TreeID);
    if (!Tree)
    {
        return 0.0f;
    }

    const double ElapsedHours = (FDateTime::UtcNow() - Tree->CycleStartRealUtc).GetTotalHours();
    const double Remaining    = FMath::Max(0.01f, GrowthHours) - ElapsedHours;

    return FMath::Max(0.0f, static_cast<float>(Remaining));
}

//--------------------
// MUTATION
//--------------------

void UGF_BerryGrowthSubsystem::HarvestBerryTree(FName TreeID, FName BerryName)
{
    if (TreeID.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("GF_Berries: HarvestBerryTree called with no TreeID - "
            "the tree will regrow instantly. Pass MakeBerryTreeID(self)."));
        return;
    }

    FGF_BerryTreeState* Tree = BerryTrees.FindByPredicate(
        [TreeID](const FGF_BerryTreeState& Existing) { return Existing.TreeID == TreeID; });

    if (!Tree)
    {
        Tree = &BerryTrees.AddDefaulted_GetRef();
        Tree->TreeID = TreeID;
    }

    Tree->BerryName         = BerryName;
    Tree->CycleStartRealUtc = FDateTime::UtcNow();
    ++Tree->TimesHarvested;

    UE_LOG(LogTemp, Log, TEXT("GF_Berries: harvested '%s' (%s) - regrowing from now (harvest #%d)."),
        *TreeID.ToString(), *BerryName.ToString(), Tree->TimesHarvested);

    // Broadcast here too, not just on the debug paths: Blueprints may call this
    // directly instead of going through UGF_BerryTreeComponent::Harvest, and then
    // nothing else would ever tell the tree to update its sprite.
    OnBerryDataChanged.Broadcast();
}

void UGF_BerryGrowthSubsystem::ForceBerryTreeReady(FName TreeID)
{
    FGF_BerryTreeState* Tree = BerryTrees.FindByPredicate(
        [TreeID](const FGF_BerryTreeState& Existing) { return Existing.TreeID == TreeID; });

    if (!Tree)
    {
        return; // Untracked trees already read as ready.
    }

    // Push the cycle start far enough into the past that any GrowthHours is satisfied,
    // rather than deleting the entry - that would lose the harvest count.
    Tree->CycleStartRealUtc = FDateTime::UtcNow() - FTimespan::FromDays(1000.0);

    UE_LOG(LogTemp, Log, TEXT("GF_Berries: forced '%s' ripe."), *TreeID.ToString());

    OnBerryDataChanged.Broadcast();
}

void UGF_BerryGrowthSubsystem::DebugAdvanceGrowthHours(float Hours)
{
    const FTimespan Shift = FTimespan::FromHours(Hours);
    for (FGF_BerryTreeState& Tree : BerryTrees)
    {
        Tree.CycleStartRealUtc -= Shift;
    }

    UE_LOG(LogTemp, Log, TEXT("GF_Berries: aged %d tree(s) by %.2f hour(s)."),
        BerryTrees.Num(), Hours);

    // Trees already standing in a loaded level are not polling - tell them to look again,
    // or the skip is invisible until each one's own refresh tick comes round.
    OnBerryDataChanged.Broadcast();
}

//--------------------
// PERSISTENCE
//--------------------

bool UGF_BerryGrowthSubsystem::SaveBerries()
{
    UGF_BerrySave* SaveObj = Cast<UGF_BerrySave>(
        UGameplayStatics::CreateSaveGameObject(UGF_BerrySave::StaticClass()));

    if (!SaveObj)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_Berries: could not create the berry save object!"));
        return false;
    }

    SaveObj->BerryTrees = BerryTrees;

    const bool bSuccess = UGameplayStatics::SaveGameToSlot(
        SaveObj, UGF_BerrySave::SlotName, UGF_BerrySave::UserIndex);

    UE_LOG(LogTemp, Log, TEXT("GF_Berries: saved %d tree(s) -> %s"),
        BerryTrees.Num(), bSuccess ? TEXT("OK") : TEXT("FAILED"));

    return bSuccess;
}

bool UGF_BerryGrowthSubsystem::LoadBerries()
{
    if (!UGameplayStatics::DoesSaveGameExist(UGF_BerrySave::SlotName, UGF_BerrySave::UserIndex))
    {
        // Normal for a save made before this system existed, and for New Game. Either
        // way the run must start clean rather than inheriting the previous session's.
        UE_LOG(LogTemp, Log, TEXT("GF_Berries: no berry save found - every tree starts ripe."));
        ResetToBootState();
        return false;
    }

    UGF_BerrySave* SaveObj = Cast<UGF_BerrySave>(
        UGameplayStatics::LoadGameFromSlot(UGF_BerrySave::SlotName, UGF_BerrySave::UserIndex));

    if (!SaveObj)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_Berries: berry save exists but failed to load - trees reset."));
        ResetToBootState();
        return false;
    }

    BerryTrees = SaveObj->BerryTrees;

    UE_LOG(LogTemp, Log, TEXT("GF_Berries: loaded %d tracked tree(s)."), BerryTrees.Num());

    // A load can happen with a level already up (Continue, soft reset), so any live
    // tree must re-read rather than keep showing the previous run's stage.
    OnBerryDataChanged.Broadcast();
    return true;
}

bool UGF_BerryGrowthSubsystem::DeleteBerrySave()
{
    if (!UGameplayStatics::DoesSaveGameExist(UGF_BerrySave::SlotName, UGF_BerrySave::UserIndex))
    {
        return false;
    }

    const bool bSuccess = UGameplayStatics::DeleteGameInSlot(
        UGF_BerrySave::SlotName, UGF_BerrySave::UserIndex);

    UE_LOG(LogTemp, Log, TEXT("GF_Berries: deleted berry save -> %s"),
        bSuccess ? TEXT("OK") : TEXT("FAILED"));

    return bSuccess;
}
