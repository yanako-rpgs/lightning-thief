#include "GF_SimpleFollower.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AGF_SimpleFollower::AGF_SimpleFollower()
{
    PrimaryActorTick.bCanEverTick = true;

    SpriteComponent = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("Sprite"));
    RootComponent = SpriteComponent;

    MovementComponent = CreateDefaultSubobject<UGF_GridMovementComponent>(TEXT("GridMovement"));
    MovementComponent->SpriteComponent = SpriteComponent;
    MovementComponent->bIsFollower = true;

    // This actor's root sits ON the floor (the visible sprite is a child offset upward),
    // unlike an ACharacter whose root is the centre of its capsule. GridMovementComponent's
    // 88-unit default is a capsule half-height and is only auto-calibrated for Characters,
    // so leaving it here would place every floor calculation ~88 units underground.
    MovementComponent->ActorHeightAboveGround = 0.0f;

    NPCAIComponent = CreateDefaultSubobject<UGF_GridNPCAIComponent>(TEXT("NPCAI"));
    NPCAIComponent->Behavior = EGF_NPCBehavior::FollowPlayerPath;
    NPCAIComponent->FollowDistance = 1;
}

void AGF_SimpleFollower::BeginPlay()
{
    // Re-assert the floor offset BEFORE Super — the movement component's own BeginPlay
    // (floor snap + movement-history seed) runs inside AActor::BeginPlay, and a Blueprint
    // child that saved the old 88.0 default would otherwise keep the follower's floor
    // maths underground.
    if (MovementComponent)
        MovementComponent->ActorHeightAboveGround = 0.0f;

    Super::BeginPlay();

    if (!MovementComponent || !NPCAIComponent)
        return;

    MovementComponent->bIsFollower = true;
    // Find FollowerSprite
    TArray<UActorComponent*> AllComponents;
    GetComponents(AllComponents);

    NPCAIComponent->bAutoMoveWhenBlocking = false;

    for (UActorComponent* Comp : AllComponents)
    {
        UPaperFlipbookComponent* FlipComp = Cast<UPaperFlipbookComponent>(Comp);
        if (FlipComp && FlipComp != SpriteComponent)
        {
            FollowerSprite = FlipComp;
            break;
        }
    }

    // Setup player
    AActor* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (Player)
    {
        SetupFollower(Player);
    }
}

void AGF_SimpleFollower::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!MovementComponent || !FollowerSprite)
        return;

    // Calculate direction
    FGF_GridCoordinate Current = MovementComponent->CurrentGridPosition;
    FGF_GridCoordinate Target = MovementComponent->TargetGridPosition;

    int32 DiffX = Target.X - Current.X;
    int32 DiffY = Target.Y - Current.Y;

    EGF_PlayerDirection Direction;
    if (FMath::Abs(DiffX) > FMath::Abs(DiffY))
        Direction = (DiffX > 0) ? EGF_PlayerDirection::East : EGF_PlayerDirection::West;
    else if (DiffY != 0)
        Direction = (DiffY > 0) ? EGF_PlayerDirection::North : EGF_PlayerDirection::South;
    else
        Direction = MovementComponent->GetCurrentFacing();

    // Convert to array index
    int32 DirIndex = (int32)Direction;

    // Get animation
    bool bIsMoving = MovementComponent->IsMoving();
    UPaperFlipbook* NewFlipbook = nullptr;

    if (bIsMoving && WalkAnimations.IsValidIndex(DirIndex))
    {
        NewFlipbook = WalkAnimations[DirIndex];
    }
    else if (!bIsMoving && IdleAnimations.IsValidIndex(DirIndex))
    {
        NewFlipbook = IdleAnimations[DirIndex];
    }

    if (NewFlipbook && FollowerSprite->GetFlipbook() != NewFlipbook)
    {
        FollowerSprite->SetFlipbook(NewFlipbook);
    }
}

void AGF_SimpleFollower::SetupFollower(AActor* PlayerActor)
{
    if (!PlayerActor || !NPCAIComponent)
        return;

    UGF_GridMovementComponent* PlayerMovement = PlayerActor->FindComponentByClass<UGF_GridMovementComponent>();

    if (PlayerMovement)
    {
        NPCAIComponent->PlayerToFollow = PlayerMovement;
        NPCAIComponent->FollowDistance = FollowDistance;
    }
}

bool AGF_SimpleFollower::SpawnFollower(AActor* PlayerActor)
{
    if (!PlayerActor || !MovementComponent)
        return false;

    UGF_GridMovementComponent* PlayerMovement = PlayerActor->FindComponentByClass<UGF_GridMovementComponent>();
    if (!PlayerMovement)
        return false;

    // Get grid subsystem
    UWorld* World = GetWorld();
    if (!World)
        return false;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return false;

    // Free every tile this follower still holds before asking for a new one. The old code
    // unregistered a single un-flattened coordinate, which missed the tile map entirely and
    // left the previous spot marked as occupied forever.
    GridSubsystem->ClearActorFromAllTiles(this);

    FVector SpawnLocation;
    FGF_GridCoordinate SpawnTile;
    if (!GridSubsystem->GetFollowerSpawnLocation(PlayerActor, GetActorLocation(), SpawnLocation, SpawnTile))
    {
        // Every neighbour is blocked or taken. Not moving is deliberate: placing the follower
        // anyway is what nudged NPCs off their tiles in the first place.
        return false;
    }

    // TeleportToGrid resolves the real floor height and re-registers us on the tile (as a
    // follower, because MovementComponent->bIsFollower is set).
    MovementComponent->TeleportToGrid(SpawnTile);
    MovementComponent->SetFacing(PlayerMovement->GetCurrentFacing());
    SetupFollower(PlayerActor);
    return true;
}


