#include "GF_Follower.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AGF_Follower::AGF_Follower()
{
    PrimaryActorTick.bCanEverTick = false;

    // Scene root — actor origin sits at floor level; sprite is offset upward
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // Create sprite component and attach it to the scene root
    SpriteComponent = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("Sprite"));
    SpriteComponent->SetupAttachment(SceneRoot);

    // Create movement component
    MovementComponent = CreateDefaultSubobject<UGF_GridMovementComponent>(TEXT("GridMovement"));
    MovementComponent->SpriteComponent = SpriteComponent;

    // Mark as follower!
    MovementComponent->bIsFollower = true;
    MovementComponent->MaxHistorySize = 20;

    // SceneRoot sits ON the floor (see above), not at a capsule centre, so the component's
    // 88-unit capsule default — only auto-calibrated for ACharacter owners — would push
    // every floor trace underground and freeze the follower's height on slopes.
    MovementComponent->ActorHeightAboveGround = 0.0f;

    // Create AI component
    AIComponent = CreateDefaultSubobject<UGF_GridNPCAIComponent>(TEXT("NPCAI"));

    // CRITICAL: Set behavior to FollowPlayerPath
    AIComponent->Behavior = EGF_NPCBehavior::FollowPlayerPath;
    AIComponent->bCanMove = true;
    AIComponent->FollowDistance = 1;
    AIComponent->bAutoMoveWhenBlocking = true;
    AIComponent->TeleportThreshold = 3.0f;
}

void AGF_Follower::BeginPlay()
{
    // Before Super: the movement component's BeginPlay (floor snap + history seed) runs
    // inside AActor::BeginPlay, and this also beats any stale value saved in a BP child.
    if (MovementComponent)
        MovementComponent->ActorHeightAboveGround = 0.0f;

    Super::BeginPlay();

    // Push the sprite up so the Creature sits ON the floor tile instead of half-underground.
    SpriteComponent->SetRelativeLocation(FVector(0.0f, 0.0f, FollowerZOffset));

    // Hide until DeferredInitialization positions us correctly — avoids a one-frame
    // flash at world origin while we wait for the player's BeginPlay to finish.
    SpriteComponent->SetVisibility(false);

    // Delay by one tick so that the player's GridMovementComponent::BeginPlay has
    // run and seeded MovementHistory with the correct floor Z before we read it.
    GetWorldTimerManager().SetTimerForNextTick(this, &AGF_Follower::DeferredInitialization);
}

void AGF_Follower::DeferredInitialization()
{
    AActor* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (Player)
    {
        SetupFollower(Player);
        InitializeAnimations();
        SpawnBehindPlayer(Player);
    }

    // Reveal the sprite now that we're positioned correctly.
    SpriteComponent->SetVisibility(true);
}

void AGF_Follower::SpawnBehindPlayer(AActor* Player)
{
    if (!Player || !MovementComponent) return;

    UGF_GridMovementComponent* PlayerMovement = Player->FindComponentByClass<UGF_GridMovementComponent>();
    if (!PlayerMovement) return;

    FGF_GridCoordinate PlayerPos = PlayerMovement->GetCurrentGridPosition();
    EGF_PlayerDirection Facing   = PlayerMovement->GetCurrentFacing();

    // Tile directly behind the player (opposite of facing direction)
    FGF_GridCoordinate BehindOffset;
    switch (Facing)
    {
        case EGF_PlayerDirection::North: BehindOffset = { 1,  0}; break; // behind north = south
        case EGF_PlayerDirection::South: BehindOffset = {-1,  0}; break; // behind south = north
        case EGF_PlayerDirection::East:  BehindOffset = { 0, -1}; break; // behind east  = west
        case EGF_PlayerDirection::West:  BehindOffset = { 0,  1}; break; // behind west  = east
        default:                      BehindOffset = { 1,  0}; break;
    }

    FGF_GridCoordinate SpawnTile;
    SpawnTile.X = PlayerPos.X + BehindOffset.X;
    SpawnTile.Y = PlayerPos.Y + BehindOffset.Y;
    SpawnTile.Z = 0;

    // GridMovementComponent::BeginPlay seeds MovementHistory[0] with the player's
    // starting grid position using a downward ECC_WorldStatic probe — the same channel
    // ExecuteSkill uses for PendingTargetFloorZ.  That entry has the correct grid Z
    // (e.g. -1 when the floor is one tile below world Z=0) before any step is taken.
    // Read it directly instead of re-tracing at the spawn tile, which may have
    // unrelated WorldStatic geometry at a different height.
    if (PlayerMovement->MovementHistory.Num() > 0)
    {
        SpawnTile.Z = PlayerMovement->MovementHistory[0].Z;
        MovementComponent->PendingTargetFloorZ = PlayerMovement->PendingTargetFloorZ;
    }

    MovementComponent->TeleportToGrid(SpawnTile);

    // Face the same direction as the player so the follower looks natural on spawn
    MovementComponent->SetFacing(Facing);
}

void AGF_Follower::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Clear the tile before the actor is gone so it doesn't stay occupied
    if (MovementComponent)
    {
        UGF_GridWorldSubsystem* GridSubsystem = GetWorld()->GetSubsystem<UGF_GridWorldSubsystem>();
        if (GridSubsystem)
        {
            FGF_GridCoordinate CurrentTile = MovementComponent->GetCurrentGridPosition();
            FGF_GridCoordinate TargetTile  = MovementComponent->TargetGridPosition;

            // Clear both current and target tile in case we're mid-move
            GridSubsystem->UnregisterEntityOnTile(this, CurrentTile);
            GridSubsystem->UnregisterEntityOnTile(this, TargetTile);

            // Also clear bIsPlayerFollower flag which ClearActorFromTile misses
            FGF_TileProperties CurrentProps = GridSubsystem->GetTileProperties(CurrentTile);
            CurrentProps.OccupyingActor   = nullptr;
            CurrentProps.bIsPlayerFollower = false;
            GridSubsystem->SetTileProperties(CurrentTile, CurrentProps);

            FGF_TileProperties TargetProps = GridSubsystem->GetTileProperties(TargetTile);
            TargetProps.OccupyingActor   = nullptr;
            TargetProps.bIsPlayerFollower = false;
            GridSubsystem->SetTileProperties(TargetTile, TargetProps);
        }
    }

    Super::EndPlay(EndPlayReason);
}

void AGF_Follower::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // Empty - follower doesn't need per-frame updates
}

void AGF_Follower::SetupFollower(AActor* PlayerActor)
{
    if (!PlayerActor || !AIComponent)
        return;

    // Get player's movement component
    UGF_GridMovementComponent* PlayerMovement =
        PlayerActor->FindComponentByClass<UGF_GridMovementComponent>();

    if (PlayerMovement)
    {
        // Connect follower to player
        AIComponent->PlayerToFollow = PlayerMovement;

        UE_LOG(LogTemp, Warning, TEXT("Follower linked to player!"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Player doesn't have GridMovementComponent!"));
    }
}

void AGF_Follower::InitializeAnimations()
{
    if (FollowerAnimations.Num() > 0)
    {
		UPaperFlipbook** UpAnim = FollowerAnimations.Find(EGF_Animations::WalkUp);
    	UPaperFlipbook** DownAnim = FollowerAnimations.Find(EGF_Animations::WalkDown);
	    UPaperFlipbook** LeftAnim = FollowerAnimations.Find(EGF_Animations::WalkLeft);
	    UPaperFlipbook** RightAnim = FollowerAnimations.Find(EGF_Animations::WalkRight);

        MovementComponent->IdleNorth = *UpAnim;
        MovementComponent->WalkNorth = *UpAnim;
        MovementComponent->IdleEast = *RightAnim;
        MovementComponent->WalkEast = *RightAnim;
        MovementComponent->IdleWest = *LeftAnim;
        MovementComponent->WalkWest = *LeftAnim;
        MovementComponent->IdleSouth = *DownAnim;
        MovementComponent->WalkSouth = *DownAnim;
    }




}
