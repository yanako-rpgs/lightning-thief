// GF_BerryTreeComponent.cpp
#include "GF_BerryTreeComponent.h"
#include "GF_BerryGrowthSubsystem.h"

#include "GF_ItemDataManager.h"
#include "UObject/UnrealType.h"
#include "GF_ItemData.h"

#include "PaperFlipbookComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

UGF_BerryTreeComponent::UGF_BerryTreeComponent()
{
    // Nothing to do per frame. Ripeness is derived from timestamps on demand, and the
    // only reason to re-check at all is so the sprite changes while someone watches -
    // a slow timer covers that for a fraction of a tick's cost.
    PrimaryComponentTick.bCanEverTick = false;
}

void UGF_BerryTreeComponent::BeginPlay()
{
    Super::BeginPlay();

    TreeID = UGF_BerryGrowthSubsystem::MakeBerryTreeID(GetOwner());

    if (TreeID.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("GF_BerryTree: '%s' could not build a tree ID - it will "
            "always look ripe and harvests will not persist."),
            *GetNameSafe(GetOwner()));
        return;
    }

    ResolveBerryName();
    ResolveFlipbookComponent();

    // Streaming a sublevel back in runs this again, which is the point: the tree is
    // rebuilt from the save every time the player walks back into the route.
    RefreshNow();

    UE_LOG(LogTemp, Log, TEXT("GF_BerryTree: '%s' bears %s, %.1f h to ripen, currently stage %d."),
        *TreeID.ToString(), *BerryItemName.ToString(), GetGrowthHours(), GetStage());

    // Growth data can change under a level that is already up - a debug skip, a load,
    // a New Game reset. Without this the tree keeps showing its old sprite until the
    // timer below happens to come round, which is what made Debug Advance Growth Hours
    // look like it did nothing.
    if (UGF_BerryGrowthSubsystem* Growth = GetGrowthSubsystem())
    {
        Growth->OnBerryDataChanged.AddDynamic(this, &UGF_BerryTreeComponent::RefreshNow);
    }

    if (RefreshIntervalSeconds > 0.0f)
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(
                RefreshTimer, this, &UGF_BerryTreeComponent::RefreshNow,
                RefreshIntervalSeconds, /*bLoop=*/true);
        }
    }
}

void UGF_BerryTreeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(RefreshTimer);
    }

    // The subsystem outlives the level, so a tree that streamed out must unsubscribe
    // or its stale binding accumulates on every visibility cycle.
    if (UGF_BerryGrowthSubsystem* Growth = GetGrowthSubsystem())
    {
        Growth->OnBerryDataChanged.RemoveDynamic(this, &UGF_BerryTreeComponent::RefreshNow);
    }

    Super::EndPlay(EndPlayReason);
}

UGF_BerryGrowthSubsystem* UGF_BerryTreeComponent::GetGrowthSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GI   = World ? World->GetGameInstance() : nullptr;

    return GI ? GI->GetSubsystem<UGF_BerryGrowthSubsystem>() : nullptr;
}

void UGF_BerryTreeComponent::ResolveBerryName()
{
    if (!BerryItemName.IsNone())
    {
        return; // Explicitly set in the Details panel - respect it.
    }

    const AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    // BP_BerryTree already carries a per-instance "BerryName", set on each placed tree.
    // Reading it here rather than asking for it a second time on this component is what
    // stops the two from silently disagreeing - the failure mode being a tree that
    // hands out one berry while growing on another berry's schedule.
    if (const FNameProperty* Prop = FindFProperty<FNameProperty>(Owner->GetClass(), TEXT("BerryName")))
    {
        BerryItemName = Prop->GetPropertyValue_InContainer(Owner);

        UE_LOG(LogTemp, Log, TEXT("GF_BerryTree: '%s' adopted BerryName '%s' from its Blueprint."),
            *GetNameSafe(Owner), *BerryItemName.ToString());
    }
}

void UGF_BerryTreeComponent::ResolveFlipbookComponent()
{
    if (TargetFlipbookComponent)
    {
        return; // Explicitly set in the Details panel - respect it.
    }

    if (const AActor* Owner = GetOwner())
    {
        TargetFlipbookComponent = Owner->FindComponentByClass<UPaperFlipbookComponent>();
    }

    if (!TargetFlipbookComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("GF_BerryTree: '%s' has no PaperFlipbookComponent - growth "
            "is still tracked, but the sprite will never change."),
            *GetNameSafe(GetOwner()));
    }
}

float UGF_BerryTreeComponent::GetGrowthHours() const
{
    // A deliberate per-tree exception wins outright.
    if (GrowthHoursOverride > 0.0f)
    {
        return GrowthHoursOverride;
    }

    if (CachedGrowthHours > 0.0f)
    {
        return CachedGrowthHours;
    }

    // Growth time lives on the berry, so an Oran takes as long everywhere. Resolved
    // once per tree; the fallback below is what an unconfigured berry gets.
    constexpr float FallbackHours = 8.0f;

    const UWorld* World = GetWorld();
    UGameInstance* GI   = World ? World->GetGameInstance() : nullptr;

    if (const UGF_ItemDataManager* DataMgr = GI ? GI->GetSubsystem<UGF_ItemDataManager>() : nullptr)
    {
        if (const UGF_ItemData* Berry = DataMgr->GetItemByName(BerryItemName))
        {
            CachedGrowthHours = Berry->BerryGrowthHours > 0.0f ? Berry->BerryGrowthHours : FallbackHours;
            return CachedGrowthHours;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("GF_BerryTree: '%s' could not resolve berry '%s' - falling back "
        "to %.1f hours. Check Berry Item Name against the item asset."),
        *GetNameSafe(GetOwner()), *BerryItemName.ToString(), FallbackHours);

    CachedGrowthHours = FallbackHours;
    return CachedGrowthHours;
}

int32 UGF_BerryTreeComponent::GetStage() const
{
    const UGF_BerryGrowthSubsystem* Growth = GetGrowthSubsystem();

    // No subsystem is a broken install, not an empty tree: report ripe rather than
    // leaving a tree the player can never harvest.
    return Growth ? Growth->GetBerryTreeStage(TreeID, GetGrowthHours()) : 3;
}

bool UGF_BerryTreeComponent::IsReady() const
{
    const UGF_BerryGrowthSubsystem* Growth = GetGrowthSubsystem();
    return Growth ? Growth->IsBerryTreeReady(TreeID, GetGrowthHours()) : true;
}

float UGF_BerryTreeComponent::GetHoursRemaining() const
{
    const UGF_BerryGrowthSubsystem* Growth = GetGrowthSubsystem();
    return Growth ? Growth->GetBerryTreeHoursRemaining(TreeID, GetGrowthHours()) : 0.0f;
}

void UGF_BerryTreeComponent::ApplyStageVisual(int32 Stage)
{
    if (Stage == LastAppliedStage)
    {
        return;
    }
    LastAppliedStage = Stage;

    if (TargetFlipbookComponent)
    {
        UPaperFlipbook* Book = nullptr;
        switch (Stage)
        {
            case 0:  Book = SeedlingFlipbook;           break;
            case 1:  Book = SproutFlipbook;             break;
            case 2:  Book = AlmostDoneFlipbook;         break;
            default: Book = ReadyToBeHarvestedFlipbook; break;
        }

        if (Book)
        {
            TargetFlipbookComponent->SetFlipbook(Book);
        }
        else
        {
            // A missing flipbook leaves the previous sprite up rather than blanking the
            // tree, which looks exactly like "growth is broken" - so say so out loud.
            UE_LOG(LogTemp, Warning, TEXT("GF_BerryTree: '%s' reached stage %d but has no flipbook "
                "assigned for it - the sprite will not change. Fill in the four flipbook slots "
                "on the GE Berry Tree component."),
                *GetNameSafe(GetOwner()), Stage);
        }
    }

    OnStageChanged.Broadcast(Stage);
}

void UGF_BerryTreeComponent::RefreshNow()
{
    ApplyStageVisual(GetStage());
}

void UGF_BerryTreeComponent::Harvest()
{
    UGF_BerryGrowthSubsystem* Growth = GetGrowthSubsystem();
    if (!Growth)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_BerryTree: no growth subsystem - '%s' was picked but "
            "will be full again on the next load."), *TreeID.ToString());
        return;
    }

    Growth->HarvestBerryTree(TreeID, BerryItemName);

    // Straight to Seedling rather than waiting for the timer, so the tree empties on
    // the same frame the player takes the berries.
    RefreshNow();
}
