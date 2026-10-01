#include "GF_RouteVolume.h"
#include "GF_RouteSubsystem.h"
#include "Kismet/GameplayStatics.h"

AGF_RouteVolume::AGF_RouteVolume()
{
    PrimaryActorTick.bCanEverTick = false;

    Vault = CreateDefaultSubobject<UBoxComponent>(TEXT("Vault"));
    RootComponent = Vault;

    // Default size — resize per area in the editor
    Vault->SetBoxExtent(FVector(500.f, 500.f, 300.f));
    Vault->SetCollisionProfileName(TEXT("Trigger"));
}

void AGF_RouteVolume::BeginPlay()
{
    Super::BeginPlay();

    // Bind for future entries (walking into the volume)
    Vault->OnComponentBeginOverlap.AddDynamic(this, &AGF_RouteVolume::OnBoxBeginOverlap);

    // OnComponentBeginOverlap never fires for actors that were already inside the
    // volume when it spawned. After one tick (physics is initialised by then),
    // manually check if the player is already inside and activate immediately.
    //
    // Bound as a member function, not a lambda capturing `this`: only the (Obj, MemFunc)
    // overload stores a weak reference the timer manager can cancel when the actor dies.
    // One tick is a narrow window, but a sub-level that streams in and back out inside it
    // closes it -- and the callback below dereferences both `Vault` and `AreaData`.
    GetWorld()->GetTimerManager().SetTimerForNextTick(this, &AGF_RouteVolume::CheckAlreadyInsideOnSpawn);
}

void AGF_RouteVolume::CheckAlreadyInsideOnSpawn()
{
    if (!AreaData) return;

    AActor* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;

    TArray<AActor*> Overlapping;
    Vault->GetOverlappingActors(Overlapping);
    if (Overlapping.Contains(Player))
    {
        UGameInstance* GI = UGameplayStatics::GetGameInstance(this);
        if (!GI) return;

        UGF_RouteSubsystem* RouteSys = GI->GetSubsystem<UGF_RouteSubsystem>();
        if (RouteSys)
            RouteSys->SetCurrentRoute(AreaData);
    }
}

void AGF_RouteVolume::OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    // Only react to the local player pawn
    if (OtherActor != UGameplayStatics::GetPlayerPawn(this, 0)) return;
    if (!AreaData) return;

    UGameInstance* GI = UGameplayStatics::GetGameInstance(this);
    if (!GI) return;

    UGF_RouteSubsystem* RouteSubsystem = GI->GetSubsystem<UGF_RouteSubsystem>();
    if (!RouteSubsystem) return;

    RouteSubsystem->SetCurrentRoute(AreaData);
}
