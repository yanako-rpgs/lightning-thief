#include "GF_GridMovementComponent.h"
#include "GF_GridNPCAIComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "PaperFlipbook.h"
#include "GF_GridTileMarker.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/LevelStreaming.h"
#include "Engine/CoreSettings.h"
#include "GF_CreatureBridge.h"

namespace
{
    /**
     * How far the actor's origin may sit from the floor it last measured before the idle
     * guard in TickComponent re-seats it. Comfortably above the tolerance ExecuteSkill
     * already allows per step, so a settled actor never re-traces every frame, and well
     * under a tile so a real sink into ramp geometry is always caught.
     */
    constexpr float FloorDriftHealThreshold = 12.0f;

    /**
     * Can ActorHeightAboveGround be treated as "the actual distance from the floor to this
     * actor's origin"? Only two owners guarantee it: an ACharacter, which BeginPlay
     * calibrates from the capsule half-height, and a follower Creature, which forces the
     * value to 0 in both its constructor and its BeginPlay because its root sits on the
     * floor. Every other owner may still be carrying the 88 default that describes neither,
     * so nothing may reposition them from it.
     */
    bool HasTrustedFloorOffset(const AActor* Owner, bool bIsFollower)
    {
        return bIsFollower || (Owner && Owner->IsA<ACharacter>());
    }

    /**
     * Is this actor the player's follower Creature? Asks the actor's own movement component
     * instead of trusting FGF_TileProperties::bIsPlayerFollower, which is a tile flag and can
     * describe a follower that has already walked away.
     */
    bool IsFollowerActor(const AActor* Actor)
    {
        if (!Actor)
            return false;

        const UGF_GridMovementComponent* Movement = Actor->FindComponentByClass<UGF_GridMovementComponent>();
        return Movement && Movement->bIsFollower;
    }
}

UGF_GridMovementComponent::UGF_GridMovementComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UGF_GridMovementComponent::BeginPlay()
{
    Super::BeginPlay();
    //UGF_GridWorldSubsystem::RegisterEntityOnTile
    // Get grid subsystem
    UWorld* World = GetWorld();
    if (World)
    {
        GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
        if (!GridSubsystem)
        {
          //  UE_LOG(LogTemp, Error, TEXT("GridMovementComponent: Could not find GridWorldSubsystem!"));
        }
    }

    // Find sprite component if not set.
    //
    // Skipped entirely when bDriveSpriteAnimation is false. This component drives the
    // owner's PaperFlipbookComponent directly (SetFlipbook in UpdateSprite /
    // SetIdleAnimation) and only knows about Idle* and Walk* flipbooks -- it has no
    // concept of running. On an actor animated by PaperZD that is a second writer to the
    // same sprite: PaperZD sets the run animation, this overwrites it with the walk
    // flipbook (UpdateSprite is called from 15 places, including every queued input while
    // a direction is held), and the run cycle visibly restarts. Leaving SpriteComponent
    // null makes every animation function here early-out, handing the sprite to PaperZD.
    if (!SpriteComponent && bDriveSpriteAnimation)
    {
        SpriteComponent = GetOwner()->FindComponentByClass<UPaperFlipbookComponent>();
        if (!SpriteComponent)
        {
          //  UE_LOG(LogTemp, Warning, TEXT("GridMovementComponent: No PaperFlipbookComponent found!"));
        }
    }
    // Calibrate the ground offset to the actual character capsule. Everywhere the
    // component positions the pawn it uses FloorZ + ActorHeightAboveGround, but an
    // ACharacter's CharacterMovementComponent settles the capsule at its own resting
    // height (CapsuleHalfHeight + the floor gap CMC keeps in walking mode). A fixed
    // ActorHeightAboveGround that doesn't match makes the pawn spawn/teleport a few
    // units off the floor and then visibly snap when gravity resolves — on BeginPlay,
    // and at the end of a door transition when RestoreDoorGravity() runs. Matching the
    // resting offset here means CMC finds the pawn already inside its floor band and
    // never adjusts, so there is no snap.
    if (bAutoDetectGroundZ)
    {
        if (const ACharacter* Char = Cast<ACharacter>(GetOwner()))
        {
            if (const UCapsuleComponent* Capsule = Char->GetCapsuleComponent())
            {
                ActorHeightAboveGround = Capsule->GetScaledCapsuleHalfHeight()
                    + UCharacterMovementComponent::MAX_FLOOR_DIST;
            }
        }
    }

    //
    // Initialize grid position
    if (GridSubsystem)
    {
        FVector StartLocation = GetOwner()->GetActorLocation();
        CurrentGridPosition = GridSubsystem->WorldToGrid(StartLocation);
        if (bAutoDetectGroundZ)
        {
            FHitResult HitResult;
            FCollisionQueryParams QueryParams;
            QueryParams.AddIgnoredActor(GetOwner());
            QueryParams.bTraceComplex = false;
            FVector TraceStart = FVector(StartLocation.X, StartLocation.Y, StartLocation.Z + GroundTraceDistance);
            FVector TraceEnd   = FVector(StartLocation.X, StartLocation.Y, StartLocation.Z - GroundTraceDistance);
            if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, GroundTraceChannel, QueryParams))
            {
                CurrentGridPosition.Z = FMath::RoundToInt(HitResult.ImpactPoint.Z / GridSubsystem->TileSize);
            }
        }
        else
        {
            CurrentGridPosition.Z = 0;
        }

        // Seed the "target" state to match where we start. ExecuteSkill is the only
        // place these normally get assigned, so an entity that has never moved would
        // otherwise leave them at (0,0,0). The stale-snap guard in ScriptedMoveTo
        // compares TargetGridPosition against CurrentGridPosition and, if they differ,
        // snaps the actor to TargetWorldPosition — which for a never-moved NPC would
        // teleport it to the origin (e.g. when BeginPushedBack is the first move).
        TargetGridPosition  = CurrentGridPosition;
        TargetWorldPosition = GetOwner()->GetActorLocation();

        // Register on tile (always Z=0)
		FGF_GridCoordinate FlatPosition = CurrentGridPosition;
		FlatPosition.Z = 0;
        if (bIsFollower)
        {
			GridSubsystem->RegisterFollowerOnTile(GetOwner(), FlatPosition);
        }
        else
        {
			GridSubsystem->RegisterEntityOnTile(GetOwner(), FlatPosition);
        }


        // Snap to grid
               FVector GridWorldPos = GridSubsystem->GridToWorld(CurrentGridPosition);
        bool bBeginPlayFloorFound = false;
        if (bAutoDetectGroundZ)
        {
            FHitResult HitResult;
            FCollisionQueryParams QueryParams;
            QueryParams.AddIgnoredActor(GetOwner());
            QueryParams.bTraceComplex = false;
            FVector TraceStart = FVector(GridWorldPos.X, GridWorldPos.Y, GridWorldPos.Z + GroundTraceDistance);
            FVector TraceEnd   = FVector(GridWorldPos.X, GridWorldPos.Y, GridWorldPos.Z - GroundTraceDistance);
            if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, GroundTraceChannel, QueryParams))
            {
                GridWorldPos.Z = HitResult.ImpactPoint.Z + ActorHeightAboveGround;
                bBeginPlayFloorFound = true;
            }
        }
        GetOwner()->SetActorLocation(GridWorldPos);

        // If the floor trace missed (sub-levels may not be loaded yet), lock movement
        // and poll in Tick until the floor appears — prevents falling through the ground
        // on PIE start or after level loads.
        // Only do this for the player or the follower Creature — NPCs are placed on already-
        // loaded geometry and locking them would block any NPCMoveRelative calls made from
        // their own BeginPlay before the floor confirmation resolves.
        const APawn* OwnerPawn = Cast<APawn>(GetOwner());
        const bool bOwnerIsPlayer = OwnerPawn && OwnerPawn->IsPlayerControlled();
        if (bAutoDetectGroundZ && !bBeginPlayFloorFound && (bOwnerIsPlayer || bIsFollower))
        {
            LockMovement();
            SuppressDoorGravity();
            SeedStreamingForCurrentLocation();
            bAwaitingFloorConfirmation  = true;
            bFloorConfirmSkipDoorEvents = true;
            FloorConfirmFacing          = CurrentFacing;
            FloorConfirmAutoSteps       = 0;
            FloorConfirmDestCoord       = CurrentGridPosition;
            FloorConfirmElapsed         = 0.0f;
            bFloorConfirmStallLogged    = false;
        }
    }

    // Set initial sprite
    UpdateSprite();
    CurrentFacing = EGF_PlayerDirection::North;
    SetFacing(CurrentFacing);

    // -----------------------------------------------------------------------
    // Seed MovementHistory with the starting position so that follower Creature
    // spawned before the first player step have a valid history entry to read.
    //
    // We do a downward ECC_WorldStatic probe (the same channel ExecuteSkill uses
    // for PendingTargetFloorZ) starting just above the actor's world Z.
    // Starting from ActorLoc.Z+1 — NOT from ActorLoc.Z+GroundTraceDistance —
    // means we are already below any overhead WorldStatic geometry and will
    // only hit the actual floor beneath us.
    // -----------------------------------------------------------------------
    if (GridSubsystem)
    {
        FVector ActorLoc = GetOwner()->GetActorLocation();

        FHitResult SeedHit;
        FCollisionQueryParams SeedParams;
        SeedParams.AddIgnoredActor(GetOwner());
        SeedParams.bTraceComplex = false;

        FVector SeedStart(ActorLoc.X, ActorLoc.Y, ActorLoc.Z + 1.f);
        FVector SeedEnd  (ActorLoc.X, ActorLoc.Y, ActorLoc.Z - GroundTraceDistance);

        if (GetWorld()->LineTraceSingleByChannel(SeedHit, SeedStart, SeedEnd, ECC_WorldStatic, SeedParams))
        {
            PendingTargetFloorZ    = SeedHit.ImpactPoint.Z;
            CurrentGridPosition.Z  = FMath::RoundToInt(PendingTargetFloorZ / GridSubsystem->TileSize);
        }

        // Insert exactly one seed entry so the follower has a guaranteed coordinate.
        if (MovementHistory.Num() == 0)
            MovementHistory.Insert(CurrentGridPosition, 0);
    }
}

bool UGF_GridMovementComponent::IsMovingForAnimation() const
{
    if (IsMovingContinuous())
    {
        return true;
    }

    // Cover the one-tick Idle dip between consecutive grid steps (see the header comment).
    if (LastMovingTime < 0.0f || MoveContinuityGrace <= 0.0f)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    return World && (World->GetTimeSeconds() - LastMovingTime) < MoveContinuityGrace;
}

namespace
{
    // ── Streaming budget while the player is behind a fade ───────────────────────────
    // Config sets the per-frame streaming budget deliberately low (s.LevelStreamingActors-
    // UpdateTimeLimit=1.85 ms and friends) so that streaming a level in never hitches
    // gameplay. During a floor-confirmation wait that trade is backwards: the player is
    // holding on a black screen or a loading widget, cannot see a single frame of it, and
    // the only thing the small budget buys is a longer wait - Voltmere took 5-10 seconds
    // of it. Spend frames freely while nobody is looking, then put the shipped values back.
    //
    // These globals ARE the s.* CVars (FAutoConsoleVariableRef binds each CVar to the
    // global), so writing them is the same as typing the command - which matters because
    // the console is not always available in a test build.
    // The shipped values, captured ONCE before the first boost.
    //
    // Restoring straight off UStreamingSettings would be nicer, but does not compile: in 5.6
    // those UPROPERTYs are protected with no public getter. Re-reading the s.* CVars does not
    // work either, for the reason noted above -- these globals ARE the CVars, so once boosted
    // they no longer remember what they were.
    //
    // Capture-once keeps the property the config read was there for: because the snapshot is
    // only ever taken while the budget is pristine, a wait interrupted by a level hide (which
    // routes EndPlay without destroying the actor) still restores the shipped numbers on the
    // next release, instead of latching a previous transition's boosted ones.
    struct FGF_StreamingBudget
    {
        float ActorsUpdateTimeLimit             = 0.0f;
        float AsyncLoadingTimeLimit             = 0.0f;
        float UnregisterComponentsTimeLimit     = 0.0f;
        int32 RegistrationGranularity           = 0;
        int32 UnregistrationGranularity         = 0;
        bool  bCaptured                         = false;
    };

    FGF_StreamingBudget ShippedStreamingBudget;

    // Called every tick of the wait, so the capture has to be idempotent.
    void ApplyLoadingScreenStreamingBudget()
    {
        if (!ShippedStreamingBudget.bCaptured)
        {
            ShippedStreamingBudget.ActorsUpdateTimeLimit         = GLevelStreamingActorsUpdateTimeLimit;
            ShippedStreamingBudget.AsyncLoadingTimeLimit         = GAsyncLoadingTimeLimit;
            ShippedStreamingBudget.UnregisterComponentsTimeLimit = GLevelStreamingUnregisterComponentsTimeLimit;
            ShippedStreamingBudget.RegistrationGranularity       = GLevelStreamingComponentsRegistrationGranularity;
            ShippedStreamingBudget.UnregistrationGranularity     = GLevelStreamingComponentsUnregistrationGranularity;
            ShippedStreamingBudget.bCaptured                     = true;
        }

        GLevelStreamingActorsUpdateTimeLimit               = 12.0f;
        GAsyncLoadingTimeLimit                             = 12.0f;
        GLevelStreamingUnregisterComponentsTimeLimit       = 12.0f;
        GLevelStreamingComponentsRegistrationGranularity   = 40;
        GLevelStreamingComponentsUnregistrationGranularity = 40;
    }

    void RestoreConfiguredStreamingBudget()
    {
        // Never boosted (a release with no matching wait) - leave the live values alone
        // rather than stamping zeroes over them.
        if (!ShippedStreamingBudget.bCaptured)
        {
            return;
        }

        GLevelStreamingActorsUpdateTimeLimit               = ShippedStreamingBudget.ActorsUpdateTimeLimit;
        GAsyncLoadingTimeLimit                             = ShippedStreamingBudget.AsyncLoadingTimeLimit;
        GLevelStreamingUnregisterComponentsTimeLimit       = ShippedStreamingBudget.UnregisterComponentsTimeLimit;
        GLevelStreamingComponentsRegistrationGranularity   = ShippedStreamingBudget.RegistrationGranularity;
        GLevelStreamingComponentsUnregistrationGranularity = ShippedStreamingBudget.UnregistrationGranularity;
    }
}

void UGF_GridMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Stamped BEFORE this tick can complete a step and drop MovementState to Idle, so
    // IsMovingContinuous() can tell "between two steps" apart from "actually stopped".
    if (MovementState == EGF_GridMovementState::Moving)
    {
        if (const UWorld* World = GetWorld())
        {
            LastMovingTime = World->GetTimeSeconds();
        }
    }

    // ── Floor confirmation: stay gravity-suppressed until sub-level floor appears ──
    if (bAwaitingFloorConfirmation)
    {
        FloorConfirmElapsed += DeltaTime;
        const bool bTimedOut = (FloorConfirmElapsed >= FloorConfirmTimeout);

        // Nobody can see these frames - buy the level with them. Restored on release below.
        ApplyLoadingScreenStreamingBudget();

        // Wait for BOTH: floor geometry present under the player AND every
        // streaming sub-level fully shown.  Requiring both also defeats the
        // 1-frame volume race — if the floor is found on the base level before
        // the destination sub-level reports visible, we keep waiting.
        // Not const: the blocking flush below re-tests both.
        bool bFloorReady     = TrySnapToFloor();
        bool bStreamingReady = AreStreamingLevelsReady();

        // "Nothing is pending" is NOT "the destination is ready".
        //
        // AreStreamingLevelsReady() only waits on levels that already report
        // ShouldBeVisible(). The streaming volumes are what set that, and the engine
        // re-evaluates them once per tick from the PLAYER CAMERA - which lags a teleport.
        // So for the first frames after arriving, the level we just landed in has not been
        // requested by anyone: the check answers a question nobody asked and says "ready".
        // That is how a save loaded in Voltmere released the player into an empty city,
        // standing on one of the persistent level's blocking volumes (which is a perfectly
        // good floor as far as the trace is concerned) while the city took another 5-10
        // seconds to arrive.
        //
        // Two parts to closing it: keep re-seeding the volumes from where the player
        // ACTUALLY is, so the request appears regardless of where the camera has got to,
        // and refuse to accept a "ready" answer until that has had a beat to happen.
        SeedStreamingForCurrentLocation();

        constexpr float MinFloorConfirmHoldSeconds = 0.35f;
        if (FloorConfirmElapsed < MinFloorConfirmHoldSeconds)
        {
            bStreamingReady = false;
        }

        // The timeout may bypass STREAMING, never the FLOOR.
        //
        // This used to release on bTimedOut alone. On a Steam Deck the opening sub-level takes
        // longer than the timeout to stream, so the valve fired with no floor under the player,
        // gravity came back, and they dropped straight through the world — softlocked before
        // the forced cutscene into their house could run, with no way out but quitting. A log
        // from that session shows it firing twice.
        //
        // Releasing a player onto geometry that does not exist is never a recovery; it is the
        // failure it was meant to prevent. So the escape hatch now only gives up on waiting for
        // every sub-level to report visible — the floor under our own feet stays mandatory.
        // Waiting past the timeout with no floor is the case that used to drop the player
        // through the world. It is now survivable, but it still means the level is streaming
        // far slower than the game expects, and it must not be silent -- a player staring at
        // a frozen character needs to leave a trace in the log. One-shot, not per frame.
        if (bTimedOut && !bFloorConfirmStallLogged && (!bFloorReady || !bStreamingReady))
        {
            bFloorConfirmStallLogged = true;
            UE_LOG(LogTemp, Warning,
                TEXT("GridMovementComponent: %s after %.1fs (floor:%s streaming:%s) - forcing a blocking ")
                TEXT("stream flush rather than releasing the player into a half-loaded level."),
                *GetNameSafe(GetOwner()), FloorConfirmTimeout,
                bFloorReady ? TEXT("ok") : TEXT("MISSING"),
                bStreamingReady ? TEXT("ok") : TEXT("INCOMPLETE"));

            // Stop waiting and MAKE it load, once.
            //
            // Holding indefinitely is safe but it is still an unplayable game: the player stares at
            // a character that will not move and cannot tell that from a crash. A blocking flush
            // costs one long hitch on a screen where the player is already frozen, and turns "maybe
            // it resolves" into "it has resolved". Bounded on purpose -- it drains the pending
            // requests and returns; it cannot spin forever on a sub-level that was never requested.
            //
            // One-shot, gated behind the same flag as the log: if the floor is STILL missing after
            // a full flush then the level is not merely slow, it is absent, and hammering the flush
            // every frame would turn a diagnosable stall into a hang.
            if (UWorld* World = GetWorld(); World && GEngine)
            {
                GEngine->BlockTillLevelStreamingCompleted(World);

                // Re-test both. The flush drained every pending request, so anything still missing
                // is absent rather than slow.
                bFloorReady     = TrySnapToFloor();
                bStreamingReady = AreStreamingLevelsReady();

                if (bFloorReady && bStreamingReady)
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("GridMovementComponent: flush completed the level under %s - releasing."),
                        *GetNameSafe(GetOwner()));
                }
                else
                {
                    UE_LOG(LogTemp, Error,
                        TEXT("GridMovementComponent: after a blocking flush %s still has floor:%s streaming:%s. ")
                        TEXT("That is absent, not slow - check the sub-level is loaded and cooked. Holding the ")
                        TEXT("player rather than releasing them into it."),
                        *GetNameSafe(GetOwner()),
                        bFloorReady ? TEXT("ok") : TEXT("MISSING"),
                        bStreamingReady ? TEXT("ok") : TEXT("INCOMPLETE"));
                }
            }
        }

        // BOTH, always. The timeout no longer bypasses either one -- it triggers the flush above
        // and then we re-test.
        //
        // Releasing on "floor is present, streaming can catch up" looked harmless and was not: on a
        // Steam Deck the player was handed control while sub-levels were still arriving, so they saw
        // the bare skybox, could walk, and could outrun the streamer entirely. Worse, the collision
        // volumes that drive cutscenes were not there yet -- leaving Fernhollow Trial after its leader is
        // supposed to trap the player the instant they step out of the door, and that trigger simply
        // did not exist for the first few steps. A cutscene that silently does not fire is far more
        // damaging than a loading screen that lasts a second longer.
        if (bFloorReady && bStreamingReady)
        {
            // Cheap and once per transition. Without it, "the screen opened too early" and
            // "the level really was ready" look identical in a log, which is exactly the
            // ambiguity that hid the Voltmere case.
            UE_LOG(LogTemp, Log,
                TEXT("GridMovementComponent: %s floor confirmed after %.2fs - releasing."),
                *GetNameSafe(GetOwner()), FloorConfirmElapsed);

            // Gameplay is about to be visible again - hitch-free budget from here.
            RestoreConfiguredStreamingBudget();

            bAwaitingFloorConfirmation = false;
            RestoreDoorGravity();
            OnFloorConfirmed.Broadcast();

            if (bFloorConfirmSkipDoorEvents)
            {
                // BeginPlay or WaitForFloor() case — just unlock, no door flow
                bFloorConfirmSkipDoorEvents = false;
                UnlockMovement();
            }
            else
            {
                // Door transition case — start auto-steps or fire OnDoorExited
                if (FloorConfirmAutoSteps > 0)
                {
                    RemainingAutoSteps         = FloorConfirmAutoSteps;
                    AutoStepDirection          = FloorConfirmFacing;
                    bIsProcessingDoorAutoSteps = true;
                    MovementState              = EGF_GridMovementState::Idle;

                    FGF_GridCoordinate StepOffset = UGF_GridWorldSubsystem::GetDirectionOffset(FloorConfirmFacing);
                    FGF_GridCoordinate StepTarget = CurrentGridPosition + StepOffset;
                    StepTarget.Z = 0;
                    ExecuteSkill(StepTarget, FloorConfirmFacing);
                }
                else
                {
                    UnlockMovement();
                    OnDoorExited.Broadcast(FloorConfirmDestCoord);
                }
            }
        }
        // Movement stays locked while waiting — fall through so TickMovement is skipped
    }

    if (MovementState == EGF_GridMovementState::Moving)
    {
        // Continuously advance walk time - this never resets so the cycle always continues
        WalkAnimationTime += DeltaTime;
        TickMovement(DeltaTime);
    }
    // Idle only, never Locked: a lock means a door transition, cutscene or battle owns this
    // actor's position, and re-seating it there is what the floor confirmation path above
    // exists to do properly.
    else if (MovementState == EGF_GridMovementState::Idle
             && bAutoDetectGroundZ && !bFreezeVerticalMovement && !bIsFollowingNPC
             && !bAwaitingFloorConfirmation && !bIsProcessingDoorAutoSteps && !bPendingDoorEntryStep
             && HasTrustedFloorOffset(GetOwner(), bIsFollower))
    {
        // Idle drift guard. PendingTargetFloorZ is the floor this actor last measured under
        // its own feet, so comparing it to the actual origin costs nothing — no trace — and
        // detects exactly the state that used to be unrecoverable: standing somewhere other
        // than on the floor. Only then do we pay for a trace and re-seat.
        //
        // Anything can land here: a step that fell back to flat over geometry the trace
        // missed, a swap or scripted move that started mid-lerp, a blocking volume that
        // moved. Without it, the follower's first sink into a ramp is permanent and every
        // subsequent pass deepens it until it drops through the floor entirely.
        // Deliberately one-directional: only BELOW is healed. Sunk into the floor is always a
        // bug, whereas above it can be legitimate (a surfing player, a scripted lift, a floor
        // the WorldStatic trace cannot see), and dragging those down would be a new bug.
        const float ExpectedZ = PendingTargetFloorZ + ActorHeightAboveGround;
        if (GetOwner()->GetActorLocation().Z < ExpectedZ - FloorDriftHealThreshold)
        {
            TrySnapToFloor();
        }
    }
}

//====================================================================================
// PUBLIC API
//====================================================================================

bool UGF_GridMovementComponent::TryMove(EGF_PlayerDirection Direction)
{
    if (!GridSubsystem || MovementState != EGF_GridMovementState::Idle)
    {
        // Queue the new direction so it fires on the next step complete —
        // but never during a door sequence, the player shouldn't be able to
        // redirect or change facing while auto-walking through an entrance.
        if (MovementState == EGF_GridMovementState::Moving && !bPendingDoorEntryStep && !bIsProcessingDoorAutoSteps)
        {
            bHasQueuedSkill = true;
            QueuedMoveDirection = Direction;
            CurrentFacing = Direction;  // Face the new direction immediately
            UpdateSprite();
        }
        return false;
    }



    if (bStoppingAfterCurrentMove)
        return false;

    if (!CanMove())
        return false;

    // Update facing - if we're currently showing walk anim from last step,
    // switching direction will be handled cleanly in SetWalkAnimation
    CurrentFacing = Direction;


    // Always use Z=0 for tile type data — CurrentGridPosition.Z is world height, not a tile key
    FGF_GridCoordinate FlatCurrentPos = CurrentGridPosition;
    FlatCurrentPos.Z = 0;
    FGF_TileProperties CurrentProps = GridSubsystem->GetTileProperties(FlatCurrentPos);

    FGF_GridCoordinate DirectionOffset = UGF_GridWorldSubsystem::GetDirectionOffset(Direction);

    // Check if this is a ledge jump (current tile is ledge AND moving in jump direction)
    bool bIsLedgeJump = CurrentProps.bIsLedge && (Direction == CurrentProps.LedgeJumpDirection);

    // Calculate target: 2 tiles for ledge jump, 1 tile for normal movement
    FGF_GridCoordinate TargetCoord;
    if (bIsLedgeJump)
    {
        TargetCoord = CurrentGridPosition + (DirectionOffset * 2);  // Jump 2 tiles
    }
    else
    {
        TargetCoord = CurrentGridPosition + DirectionOffset;  // Normal 1 tile
    }

    // Validate the target position (now checking the correct distance)
    if (!GridSubsystem->CanMoveToTile(CurrentGridPosition, TargetCoord, bHasSurf))
    {
        // Always look up tile data at Z=0 — followers register at flat Z regardless of
        // slope height, so a non-zero TargetCoord.Z (e.g. after descending a slope) would
        // miss the follower record and skip the swap entirely.
        FGF_GridCoordinate FlatTargetCoord = TargetCoord;
        FlatTargetCoord.Z = 0;
        FGF_TileProperties TargetProps = GridSubsystem->GetTileProperties(FlatTargetCoord);

        // If the blocking tile is occupied by the player's follower, swap positions
        // instead of bumping — prevents the player from getting stuck in tight spaces.
        //
        // Only the PLAYER may start a swap. SwapPositionsWithActor animates the other actor
        // backwards, so a follower that ran this on its own blocked step dragged the player
        // with it — at the foot of a ramp the follower's step up is refused, its target tile
        // is the one the player is standing on, and the player gets walked back down the
        // slope with no input. Identity is taken from the occupant's own movement component:
        // the bIsPlayerFollower tile flag survives the follower walking away, so the player's
        // tile carries a stale one almost permanently (the follower has stood on nearly every
        // tile the player has).
        if (!bIsFollower && IsFollowerActor(TargetProps.OccupyingActor))
        {
            SwapPositionsWithActor(TargetProps.OccupyingActor, TargetCoord);
            return true;
        }

        OnBumpIntoWall.Broadcast(Direction);

        return false;
    }

	    FGF_GridCoordinate FlatTargetCoord = TargetCoord;
		FlatTargetCoord.Z = 0;  // Always check at ground level

	if (GridSubsystem->IsTileOccupied(FlatTargetCoord))
	{
	    AActor* OccupyingActor = GridSubsystem->GetActorOnTile(FlatTargetCoord);
	    if (OccupyingActor && OccupyingActor != GetOwner())
	    {
	        return false;
	    }
	}

    // Check for item pickup at target
    FGF_TileProperties TargetProps = GridSubsystem->GetTileProperties(TargetCoord);
    if (TargetProps.bHasItem && !TargetProps.bItemPickedUp)
    {
        OnItemEncountered(TargetProps.ItemID);
    }

    // Check for tall grass encounter
    if (TargetProps.bCanTriggerEncounter)
    {
        float EncounterChance = TargetProps.EncounterChance;

        // Beacon on the LEAD party Creature doubles the wild encounter rate.
        // Read from party data rather than a battle actor — the lead is walking
        // around the overworld, it has no AGF_Creature.
        // Returns 1.0 when the Creatures module is absent, leaving the rate alone.
        EncounterChance *= FGF_CreatureBridge::EncounterRateMultiplier(this);

        if (FMath::FRand() < EncounterChance)
        {
            OnWildEncounter();
        }
    }

    // Horizontal geometry trace - catches wall meshes not registered in the grid.
    // Checks the hit normal to distinguish real walls (vertical) from walkable slopes (angled up).
    // Horizontal geometry trace — catches wall meshes and blocking volumes not registered
    // in the tile system. Skipped only for ledge jumps (the cliff IS a wall).
    if (!bIsLedgeJump)
    {
        FVector CurrentWorldPos = GetOwner()->GetActorLocation();

        // Always strip the grid Z before converting to world space.
        // CurrentGridPosition.Z encodes terrain height as a tile-unit integer; baking it
        // into GridToWorld shifts the XY target position when Z != 0.
        FGF_GridCoordinate FlatTarget = TargetCoord;
        FlatTarget.Z = 0;
        FVector TargetWorldPos = GridSubsystem->GridToWorld(FlatTarget);

        // Height to run the horizontal probe at. The player's origin is its capsule centre,
        // well clear of the ground, but a follower Creature's origin sits ON the floor — a
        // trace at exactly that height grazes the ground mesh, so a ramp riser or a tile
        // seam at the destination comes back as a vertical hit and reads as a wall (and a
        // near-horizontal normal also skips the floor probe below, making it a hard block).
        // Lift it to half a tile above the floor, clamped so it can never rise more than one
        // tile above the actor: for the player the capsule centre already wins and nothing
        // about wall detection changes.
        const float LiftedTraceZ = PendingTargetFloorZ + (GridSubsystem->TileSize * 0.5f);
        const float TraceZ = FMath::Clamp(LiftedTraceZ,
                                          (float)CurrentWorldPos.Z,
                                          (float)CurrentWorldPos.Z + GridSubsystem->TileSize);

        TargetWorldPos.Z = TraceZ;   // keep trace horizontal at the actor's probe height

        FVector TraceStart = FVector(CurrentWorldPos.X, CurrentWorldPos.Y, TraceZ);
        FVector TraceEnd   = FVector(TargetWorldPos.X,  TargetWorldPos.Y,  TraceZ);

        FHitResult WallHit;
        FCollisionQueryParams WallParams;
        WallParams.AddIgnoredActor(GetOwner());
        WallParams.bTraceComplex = false;
        // Ignore pawns (the player and other characters). This trace exists to catch STATIC
        // level geometry not registered as non-walkable tiles — character-vs-character
        // blocking is already handled by the grid occupancy check (IsTileOccupied) above.
        // Without this, a pawn physically overlapping the target tile (e.g. the player while
        // being pushed) is mistaken for a wall and blocks scripted moves through tight spaces.
        for (TActorIterator<APawn> PawnIt(GetWorld()); PawnIt; ++PawnIt)
            WallParams.AddIgnoredActor(*PawnIt);

        if (GetWorld()->LineTraceSingleByChannel(WallHit, TraceStart, TraceEnd, ECC_WorldStatic, WallParams))
        {
            // Normal mostly upward → slope face, not a wall.  Mostly horizontal → wall.
            bool bIsWall = WallHit.ImpactNormal.Z <= 0.5f;

            // Secondary floor probe: only used for borderline cases where steep slope
            // geometry clips into the trace path and LOOKS like a wall even though the
            // tile is reachable (e.g. ramp risers, slight incline edges).
            //
            // Skipped when the normal is nearly horizontal (Z < 0.1) — that is a true
            // 90-degree wall and no floor probe should override it.  Without this guard
            // the probe would find the floor on the far side of a real wall and wrongly
            // allow the player to walk through it.
            if (bIsWall && WallHit.ImpactNormal.Z >= 0.1f)
            {
                FHitResult FloorProbe;
                FCollisionQueryParams ProbeParams;
                ProbeParams.AddIgnoredActor(GetOwner());
                // Ignore all pawns so their capsules don't shadow the floor.
                for (TActorIterator<APawn> It(GetWorld()); It; ++It)
                    ProbeParams.AddIgnoredActor(*It);

                FVector ProbeTop = FVector(TargetWorldPos.X, TargetWorldPos.Y, CurrentWorldPos.Z + GroundTraceDistance);
                FVector ProbeBot = FVector(TargetWorldPos.X, TargetWorldPos.Y, CurrentWorldPos.Z - GroundTraceDistance);
                // WorldStatic: same channel as ExecuteSkill floor traces — ramp meshes block
                // WorldStatic but are often Overlap for Visibility.
                if (GetWorld()->LineTraceSingleByChannel(FloorProbe, ProbeTop, ProbeBot, ECC_WorldStatic, ProbeParams))
                {
                    bIsWall = false;   // Walkable ground exists at the target - slope clip, not a true wall
                }
            }

            if (bIsWall)
            {
                // Verbose: this fires on EVERY bump into wall geometry, by the player and
                // by every patrolling NPC, so at Warning it drowns the log. Turn it on with
                //     log LogTemp Verbose
                // when you're actually chasing a movement problem.
                UE_LOG(LogTemp, Verbose, TEXT("TryMove [%s]: blocked by wall geometry '%s' (normal Z=%.2f). Tile (%d,%d)->(%d,%d). Add a non-walkable tile marker at the destination to suppress this check."),
                    *GetOwner()->GetName(),
                    WallHit.GetActor() ? *WallHit.GetActor()->GetName() : TEXT("(unknown)"),
                    WallHit.ImpactNormal.Z,
                    CurrentGridPosition.X, CurrentGridPosition.Y,
                    TargetCoord.X, TargetCoord.Y);
                OnBumpIntoWall.Broadcast(Direction);
                SetIdleAnimation(CurrentFacing);
                bPendingIdleAfterStep = false;
                return false;
            }
        }
    }

    // Execute movement (pass the ledge jump flag)
    ExecuteSkill(TargetCoord, Direction);
    return true;
}

bool UGF_GridMovementComponent::TryInteract()
{
    if (!GridSubsystem)
        return false;

    // Get tile in front of player
    FGF_GridCoordinate DirectionOffset = UGF_GridWorldSubsystem::GetDirectionOffset(CurrentFacing);
    FGF_GridCoordinate InteractCoord = CurrentGridPosition + DirectionOffset;

    // Check for actor on tile (NPC, tamer, etc.)
    AActor* ActorOnTile = GridSubsystem->GetActorOnTile(InteractCoord);
    if (ActorOnTile)
    {
        OnActorInteraction(ActorOnTile);
        return true;
    }

    // Check for item
    if (GridSubsystem->HasItemAtTile(InteractCoord))
    {
        FName ItemID = GridSubsystem->GetItemIDAtTile(InteractCoord);
        if (GridSubsystem->PickUpItem(InteractCoord))
        {
            OnItemPickedUp(ItemID);
            return true;
        }
    }

    // Check for door/interactable
    FGF_TileProperties Props = GridSubsystem->GetTileProperties(InteractCoord);
    if (Props.bHasDoor)
    {
        OnDoorInteraction(InteractCoord);
        return true;
    }

    if (Props.bHasInteractable)
    {
        OnObjectInteraction(InteractCoord);
        return true;
    }

    return false;
}

void UGF_GridMovementComponent::LockMovement()
{
    MovementState = EGF_GridMovementState::Locked;

    UpdateSprite();
}

void UGF_GridMovementComponent::UnlockMovement()
{
    // Don't let external systems (dialogue, etc.) break an active follow cutscene
    if (bIsFollowingNPC)
        return;

    MovementState = EGF_GridMovementState::Idle;
    bStoppingAfterCurrentMove = false;
    UpdateSprite();
}

void UGF_GridMovementComponent::ForceUnlockMovement()
{
    const bool bWasLocked    = (MovementState == EGF_GridMovementState::Locked);
    const bool bWasFollowing = bIsFollowingNPC;

    // Clear the flag FIRST -- it is the thing that makes UnlockMovement() a no-op.
    bIsFollowingNPC          = false;
    MovementState            = EGF_GridMovementState::Idle;
    bStoppingAfterCurrentMove = false;
    UpdateSprite();

    if (bWasLocked || bWasFollowing)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("ForceUnlockMovement: control restored (was %s%s). Something failed to unlock normally."),
            bWasLocked ? TEXT("Locked") : TEXT("unlocked"),
            bWasFollowing ? TEXT(", bIsFollowingNPC=true") : TEXT(""));
    }
}

void UGF_GridMovementComponent::StopWalking()
{
    // Call this from Blueprint when movement input is released.
    // This is the ONLY place idle animation should be set.
    if (MovementState != EGF_GridMovementState::Moving)
    {
        SetIdleAnimation(CurrentFacing);
    }
    // If still moving, idle will be set next time TryMove returns false
}

void UGF_GridMovementComponent::ForceStopMovement()
{

    bKeepWalkingAnimation = false;

    // Complete current movement, then lock
    if (MovementState == EGF_GridMovementState::Moving)
    {
        CompleteMovement();
        bStoppingAfterCurrentMove = true;
    }

    else
    {
    	MovementState = EGF_GridMovementState::Locked;

		UpdateSprite();

    }


}

void UGF_GridMovementComponent::TeleportToGrid(FGF_GridCoordinate Coord)
{
    if (!GridSubsystem)
        return;

    // Always use Z=0 for tile registration (matching ExecuteSkill and BeginPlay)
    FGF_GridCoordinate FlatOldPos = CurrentGridPosition;
    FlatOldPos.Z = 0;

    GridSubsystem->ClearActorFromTile(FlatOldPos);
    GridSubsystem->UnregisterEntityOnTile(GetOwner(), FlatOldPos);

    // Update position
    CurrentGridPosition = Coord;
    FVector NewWorldPos = GridSubsystem->GridToWorld(Coord);
    GetOwner()->SetActorLocation(NewWorldPos);

    // GridToWorld quantises height to whole tiles (Z * TileSize), so anywhere the floor is
    // not an exact multiple of TileSize — ramps, the Route 110 cycling road — the actor
    // lands buried in or floating above the geometry. Buried is the damaging case: every
    // horizontal wall trace in TryMove then starts inside the mesh and comes back as a
    // penetrating hit with a horizontal normal, which reads as a wall in all four
    // directions, so a follower teleported onto raised ground freezes in place until it is
    // teleported back to Z=0. Snap onto the real floor and re-anchor the height bookkeeping.
    if (bAutoDetectGroundZ)
    {
        FCollisionQueryParams FloorParams;
        FloorParams.AddIgnoredActor(GetOwner());
        FloorParams.bTraceComplex = false;
        // Pawns are never ground (see ExecuteSkill) — a capsule blocks WorldStatic and would
        // be read as a raised floor when we teleport in right next to the player.
        for (TActorIterator<APawn> It(GetWorld()); It; ++It)
        {
            FloorParams.AddIgnoredActor(*It);
        }

        // The real floor can only be within half a tile of the quantised height, so start
        // one tile above it: high enough to always catch it, low enough that a bridge
        // overhead is still ignored.
        FHitResult FloorHit;
        const FVector FloorTraceTop   (NewWorldPos.X, NewWorldPos.Y, NewWorldPos.Z + GridSubsystem->TileSize);
        const FVector FloorTraceBottom(NewWorldPos.X, NewWorldPos.Y, NewWorldPos.Z - GroundTraceDistance);

        if (GetWorld()->LineTraceSingleByChannel(FloorHit, FloorTraceTop, FloorTraceBottom, ECC_WorldStatic, FloorParams))
        {
            NewWorldPos.Z = FloorHit.ImpactPoint.Z + ActorHeightAboveGround;
            GetOwner()->SetActorLocation(NewWorldPos);

            PendingTargetFloorZ   = FloorHit.ImpactPoint.Z;
            CurrentGridPosition.Z = FMath::RoundToInt(PendingTargetFloorZ / GridSubsystem->TileSize);
        }
        // Trace missed (destination sub-level not streamed in yet) — keep the quantised
        // position; the floor-confirmation pass in TickComponent resolves it.
    }

    // Register on new tile (Z=0)
    FGF_GridCoordinate FlatNewPos = CurrentGridPosition;
    FlatNewPos.Z = 0;

    if (bIsFollower)
    {
        GridSubsystem->RegisterFollowerOnTile(GetOwner(), FlatNewPos);
    }
    else
    {
        GridSubsystem->RegisterEntityOnTile(GetOwner(), FlatNewPos);
    }

    // Keep the scripted-move target state in sync with the teleport destination. Otherwise the
    // stale-snap guard in ScriptedMoveTo sees TargetGridPosition != CurrentGridPosition, assumes
    // we're mid-step, and yanks us back to the pre-teleport TargetWorldPosition on the next
    // scripted move (e.g. BeginPushedBack right after a warp).
    TargetGridPosition  = CurrentGridPosition;
    TargetWorldPosition = GetOwner()->GetActorLocation();

    MovementState = EGF_GridMovementState::Idle;
    UpdateSprite();
}

void UGF_GridMovementComponent::SetFacing(EGF_PlayerDirection Direction)
{
    CurrentFacing = Direction;
    UpdateSprite();
}

void UGF_GridMovementComponent::UnregisterFromGrid()
{
    if (GridSubsystem && GetOwner())
        GridSubsystem->ClearActorFromAllTiles(GetOwner());
}

void UGF_GridMovementComponent::SetHasSurf(bool bNewHasSurf)
{
    bHasSurf = bNewHasSurf;
}

//====================================================================================
// INTERNAL MOVEMENT LOGIC
//====================================================================================



void UGF_GridMovementComponent::ExecuteSkill(FGF_GridCoordinate TargetCoord, EGF_PlayerDirection Direction)
{
    if (!GridSubsystem)
        return;

    // Always use Z=0 for tile type data (same reason as TryMove)
    FGF_GridCoordinate FlatCurrentForLedge = CurrentGridPosition;
    FlatCurrentForLedge.Z = 0;
    FGF_TileProperties CurrentProps = GridSubsystem->GetTileProperties(FlatCurrentForLedge);

    // Check if this is a ledge jump (for animation and sound effects)
    bool bIsLedgeJump = CurrentProps.bIsLedge && (Direction == CurrentProps.LedgeJumpDirection);

    if (bIsLedgeJump)
    {


        bIsJumpingLedge = true;
        LedgeJumpSpeed = MovementSpeed * 2.0f;

        // Play ledge jump sound
        if (LedgeJumpSound)
        {
            UGameplayStatics::PlaySoundAtLocation(this, LedgeJumpSound, GetOwner()->GetActorLocation());
        }
    }
    else
    {

        bIsJumpingLedge = false;
    }

    TargetGridPosition = TargetCoord;
    CurrentFacing = Direction;

    FVector GridTargetPosition = GridSubsystem->GridToWorld(TargetCoord);

    // StartWorldPosition is exactly where the actor already is - no trace needed.
    // Recalculating it from a trace + ActorHeightAboveGround causes cumulative Z drift
    // because the result only matches reality if ActorHeightAboveGround is pixel-perfect.
    StartWorldPosition = GetOwner()->GetActorLocation();
    LastTileWalked = StartWorldPosition;

    // Default target: same Z as start (flat terrain)
    TargetWorldPosition = FVector(GridTargetPosition.X, GridTargetPosition.Y, StartWorldPosition.Z);

    if (bAutoDetectGroundZ)
    {
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(GetOwner());
        QueryParams.bTraceComplex = false;

        // Ignore ALL pawns in the floor traces. A character's collision capsule blocks
        // WorldStatic, so walking near/through another pawn (e.g. the player) would let the
        // downward floor trace hit their capsule and read it as a high floor — the per-step
        // height delta then walks this actor up onto their "head" (the sky-climb bug).
        // Pawns are never ground. (Previously only done for followers; the same applies to
        // any NPC passing close to the player, e.g. a grunt fleeing past.)
        for (TActorIterator<APawn> It(GetWorld()); It; ++It)
        {
            QueryParams.AddIgnoredActor(*It);
        }

        // Always use WorldStatic for floor detection:
        //   - Terrain, slopes, and floors block WorldStatic  → traced correctly.
        //   - TriggerBoxes and overlap-only volumes do NOT block WorldStatic
        //     → the trace passes through them, preventing the "ramp over trigger" bug
        //     where a blocking-Visibility box causes a false FloorDelta and walks the
        //     actor up over it like a ramp.
        //   - Decorative/overhead geometry (tree trunks, props) is typically WorldDynamic
        //     or only blocks Visibility → also excluded, preventing the sky-climb bug.
        ECollisionChannel FloorChannel = ECC_WorldStatic;

        // Where this actor's feet actually are. ActorHeightAboveGround only describes an
        // ACharacter's capsule (BeginPlay auto-calibrates it from the capsule half-height);
        // a plain AActor — every follower Creature — has its origin ON the floor, so
        // subtracting the 88-unit default put this anchor ~88 units UNDERGROUND. Both floor
        // traces then ran entirely below the surface: they either missed (flat fallback) or
        // returned the same penetrating start point, so FloorDelta was always 0 and the
        // follower never changed height — it ignored every slope in the game.
        //
        // Measure the floor instead of assuming it. Starting the probe at the actor's own
        // origin + 1 (not + GroundTraceDistance) means overhead geometry — a bridge the
        // actor is walking UNDER — can never be mistaken for the floor.
        //
        // Take whichever estimate is HIGHER. The probe is a single line, so an actor whose
        // origin overhangs a gap (a capsule resting on a platform edge) can probe straight
        // past its own floor; the offset form covers that. The offset form is the one that
        // is wrong for non-Character actors; the probe covers that. Neither can drag the
        // anchor below the surface the actor is standing on.
        const float CurrentFloorZ = [&]()
        {
            const float OffsetFloorZ = StartWorldPosition.Z - ActorHeightAboveGround;

            FHitResult FeetHit;
            const FVector FeetTraceTop   (StartWorldPosition.X, StartWorldPosition.Y, StartWorldPosition.Z + 1.0f);
            const FVector FeetTraceBottom(StartWorldPosition.X, StartWorldPosition.Y, StartWorldPosition.Z - GroundTraceDistance);
            if (GetWorld()->LineTraceSingleByChannel(FeetHit, FeetTraceTop, FeetTraceBottom, FloorChannel, QueryParams))
            {
                return FMath::Max(OffsetFloorZ, (float)FeetHit.ImpactPoint.Z);
            }
            return OffsetFloorZ;
        }();

        // Cap how far ABOVE the current floor the downward trace may start. Because the
        // trace takes the first (highest) hit, starting it high over the actor's head
        // grabs the top of overhead geometry — a bridge/overpass the player should pass
        // UNDER — and treats it as a raised floor (the "giant ramp" bug). Measuring from
        // the actor's feet (current floor) and only searching MaxStepUpHeight upward means
        // anything clearing the head is ignored, while a one-tile step-up still registers.
        //
        // The +2 margin matters: MaxStepUpHeight (64) equals TileSize (64), so a 45-degree
        // ramp rises EXACTLY the height of the window and the target trace would start
        // precisely on the surface it is meant to find — a coin-flip hit. When it lost that
        // flip the step fell through to the flat fallback below: the actor walked
        // horizontally onto a tile whose floor is a full tile higher and ended up buried
        // inside the ramp.
        const float TraceTopZ = CurrentFloorZ + MaxStepUpHeight + 2.0f;

        // Both traces run the same window, anchored on the floor rather than on the actor
        // origin, so a step DOWN is found from the same reference the step up uses.
        const float TraceBottomZ = CurrentFloorZ - GroundTraceDistance;

        // Trace start floor Z
        FHitResult StartHit;
        FVector StartTraceTop    = FVector(StartWorldPosition.X, StartWorldPosition.Y, TraceTopZ);
        FVector StartTraceBottom = FVector(StartWorldPosition.X, StartWorldPosition.Y, TraceBottomZ);

        // Trace target floor Z
        FHitResult TargetHit;
        FVector TargetTraceTop    = FVector(GridTargetPosition.X, GridTargetPosition.Y, TraceTopZ);
        FVector TargetTraceBottom = FVector(GridTargetPosition.X, GridTargetPosition.Y, TraceBottomZ);

        bool bStartHit  = GetWorld()->LineTraceSingleByChannel(StartHit,  StartTraceTop,  StartTraceBottom,  FloorChannel, QueryParams);
        bool bTargetHit = GetWorld()->LineTraceSingleByChannel(TargetHit, TargetTraceTop, TargetTraceBottom, FloorChannel, QueryParams);

        if (bIsFollowingNPC || bFreezeVerticalMovement)
        {
            // Skip ALL vertical adjustment for this step.
            //
            // bIsFollowingNPC: player trails an NPC through tiles that may have tree trunks
            //   or overhead props directly above them — the downward trace hits those instead
            //   of the real floor, accumulating height (sky-climb bug).
            //
            // bFreezeVerticalMovement: same problem on any other scripted/cutscene actor
            //   (e.g. NPC during PushPlayerBack push-follow).  Set this flag for the duration
            //   of any movement where overhead geometry could corrupt the floor trace.
            //
            // Fix: anchor to the START tile's floor Z so CompleteMovement records the correct
            // ground height.  The actor stays perfectly flat for the whole scripted sequence;
            // normal floor detection resumes as soon as the flag is cleared.
            if (bStartHit)
                PendingTargetFloorZ = StartHit.ImpactPoint.Z;
            // TargetWorldPosition.Z already = StartWorldPosition.Z — no change needed.
        }
        else if (bStartHit && bTargetHit)
        {
            // Normal movement: apply the HEIGHT DIFFERENCE between floors, so a slightly
            // imprecise ActorHeightAboveGround never shows up as a visible pop on a slope.
            float FloorDelta = TargetHit.ImpactPoint.Z - StartHit.ImpactPoint.Z;
            TargetWorldPosition.Z = StartWorldPosition.Z + FloorDelta;

            // Self-heal: the delta form carries whatever vertical error the actor already
            // had straight into the next step, so an error can never be worked off — it is
            // permanent, and every later mistake stacks on top of it. That is the ramp
            // sinking bug: a step that fell back to flat (missed target trace), or a step
            // started mid-lerp by SwapPositionsWithActor (running back and forth into the
            // follower swaps them every time, and ExecuteSkill is called there without
            // waiting for CanMove()), leaves the actor a fraction of a tile low. Repeat and
            // it walks down through the floor.
            //
            // The landing tile's floor was just measured, so pin the result to it: keep the
            // delta result only while it stays within FloorErrorTolerance of "standing on
            // that floor". The tolerance is what stops this from re-introducing the reason
            // the delta form exists — ActorHeightAboveGround is a calibrated estimate, not a
            // pixel-exact value, so small honest offsets survive and only drift is corrected.
            //
            // Only where that estimate is actually known (see HasTrustedFloorOffset): a
            // plain-AActor NPC still carrying the 88 default has its origin on the floor, and
            // pinning it to floor+88 would hang it in mid-air. Those actors get the lower
            // bound only, which can never lift anything — it just refuses to bury it.
            // double, not float: FVector components are double in UE5 and FMath::Clamp
            // deduces a single T from all three arguments.
            const double FloorErrorTolerance = 4.0;
            const double GroundedZ = TargetHit.ImpactPoint.Z + ActorHeightAboveGround;

            if (HasTrustedFloorOffset(GetOwner(), bIsFollower))
            {
                TargetWorldPosition.Z = FMath::Clamp(TargetWorldPosition.Z,
                                                     GroundedZ - FloorErrorTolerance,
                                                     GroundedZ + FloorErrorTolerance);
            }
            else
            {
                TargetWorldPosition.Z = FMath::Max(TargetWorldPosition.Z, TargetHit.ImpactPoint.Z);
            }

            PendingTargetFloorZ = TargetHit.ImpactPoint.Z;
        }
        else if (bStartHit)
        {
            // Target floor unknown (trace missed) — stay flat, but still record the floor we
            // are actually standing on. Leaving PendingTargetFloorZ stale here poisoned two
            // things downstream: TryMove's horizontal wall probe height, and the grid Z that
            // CompleteMovement derives from it.
            PendingTargetFloorZ = StartHit.ImpactPoint.Z;
        }
        // If both traces miss, TargetWorldPosition.Z stays = StartWorldPosition.Z (safe flat fallback)
    }


    // Change state and sprite
    MovementState = EGF_GridMovementState::Moving;
    MovementAlpha = 0.0f;
    UpdateSprite();

	// Unregister from current tile (always use Z=0 for registration)
	FGF_GridCoordinate FlatCurrentPos = CurrentGridPosition;
	FlatCurrentPos.Z = 0;
	GridSubsystem->UnregisterEntityOnTile(GetOwner(), FlatCurrentPos);

	// Register on target tile (always use Z=0 for registration).
	// Follower vs entity matters here as well as in CompleteMovement: RegisterEntityOnTile
	// clears the tile's follower flag, so registering a follower through it would leave the
	// tile mislabelled for the whole step animation.
	FGF_GridCoordinate FlatTargetPos = TargetCoord;
	FlatTargetPos.Z = 0;
	if (bIsFollower)
	{
		GridSubsystem->RegisterFollowerOnTile(GetOwner(), FlatTargetPos);
	}
	else
	{
		GridSubsystem->RegisterEntityOnTile(GetOwner(), FlatTargetPos);
	}
}

void UGF_GridMovementComponent::TickMovement(float DeltaTime)
{
    if (!GridSubsystem)
        return;

    // Calculate lerp speed
    float Distance = FVector::Dist(StartWorldPosition, TargetWorldPosition);
    float CurrentSpeed = bIsJumpingLedge ? LedgeJumpSpeed : MovementSpeed;
    float LerpSpeed = (Distance > 0) ? (CurrentSpeed / Distance) : 1.0f;

    // Update alpha
    MovementAlpha += DeltaTime * LerpSpeed;

    if (MovementAlpha >= 1.0f)
    {
        // Movement complete
        CompleteMovement();
    }
    else
    {
        // Lerp position (with arc for ledge jumps)
        FVector NewPosition;
        if (bIsJumpingLedge)
        {
            // Add arc to ledge jump
            NewPosition = FMath::Lerp(StartWorldPosition, TargetWorldPosition, MovementAlpha);
            float ArcHeight = 50.0f * FMath::Sin(MovementAlpha * PI);  // Parabolic arc
            NewPosition.Z += ArcHeight;
        }
        else
        {
            NewPosition = FMath::Lerp(StartWorldPosition, TargetWorldPosition, MovementAlpha);
        }
        GetOwner()->SetActorLocation(NewPosition);
    }
}

void UGF_GridMovementComponent::CompleteMovement()
{
    GetOwner()->SetActorLocation(TargetWorldPosition);
    CurrentGridPosition = TargetGridPosition;
    CurrentGridPosition.Z = FMath::RoundToInt(PendingTargetFloorZ / GridSubsystem->TileSize);

    MovementState = EGF_GridMovementState::Idle;
    MovementAlpha = 0.0f;


    FGF_GridCoordinate FlatPosition = CurrentGridPosition;
    FlatPosition.Z = 0;


    if (bIsFollower)
        {
			GridSubsystem->RegisterFollowerOnTile(GetOwner(), FlatPosition);
        }
        else
        {
			GridSubsystem->RegisterEntityOnTile(GetOwner(), FlatPosition);
        }

    MovementState = EGF_GridMovementState::Idle;
    MovementAlpha = 0.0f;

    MovementHistory.Insert(CurrentGridPosition, 0);

    if (MovementHistory.Num() > MaxHistorySize)
    {
        MovementHistory.SetNum(MaxHistorySize);
    }


    if (!bIsJumpingLedge && FootstepSound)
	    {
	        UGameplayStatics::PlaySoundAtLocation(this, FootstepSound, GetOwner()->GetActorLocation(), 0.5f);
	    }

    if (bIsJumpingLedge)
		{
		    OnLedgeLanded.Broadcast();
		}



    if (bOverrideFacingOnComplete)
{
    CurrentFacing = OverrideFacingDirection;
    bOverrideFacingOnComplete = false;

        bIsJumpingLedge = false;


        if (bStoppingAfterCurrentMove)
    {
        MovementState = EGF_GridMovementState::Locked;
        bStoppingAfterCurrentMove = false;

    }
    else
    {
        MovementState = EGF_GridMovementState::Idle;
    }

}

    // Do NOT switch animation here. If the player is still holding input, the walk
    // flipbook should keep playing uninterrupted. Idle is set by StopWalking() instead.

    // Capture BEFORE broadcast — OnStepCompleted listeners (e.g. boundary triggers) may call
    // ScriptedMoveTo(), which sets bIsProcessingScriptedMove = true mid-CompleteMovement.
    // Using the pre-broadcast value means the scripted-move advancement block below only
    // reacts to moves that were ALREADY in progress, not ones just initiated by a listener.
    const bool bWasProcessingScriptedMove = bIsProcessingScriptedMove;

    OnStepCompleted.Broadcast();

    // --- Post-arrival auto-steps (after CompleteEnterDoor teleport) ---
    if (bIsProcessingDoorAutoSteps)
    {
        RemainingAutoSteps--;
        if (RemainingAutoSteps <= 0)
        {
            bIsProcessingDoorAutoSteps = false;
            bSuppressDoorDetection = false;  // Auto-steps done - player can use doors again immediately
            UnlockMovement();
            UpdateSprite();
            OnDoorExited.Broadcast(CurrentGridPosition);
        }
        else
        {
            // Walk one more step - force-execute bypassing collision so the
            // player smoothly clears the entrance mesh
            FGF_GridCoordinate AutoStepOffset = UGF_GridWorldSubsystem::GetDirectionOffset(AutoStepDirection);
            FGF_GridCoordinate AutoStepTarget = CurrentGridPosition + AutoStepOffset;
            AutoStepTarget.Z = 0;
            ExecuteSkill(AutoStepTarget, AutoStepDirection);
        }
        return;
    }

    // --- Pre-entry steps: tick down, keep walking, fire delegate when done ---
    if (bPendingDoorEntryStep)
    {
        RemainingEntrySteps--;
        if (RemainingEntrySteps <= 0)
        {
            // All entry steps done
            bPendingDoorEntryStep = false;
            bHasQueuedSkill = false;
            SetIdleAnimation(CurrentFacing);

            // Two-phase delay:
            //   Phase 1 (DoorTransitionDelay) — wait for fade-out to finish, THEN teleport.
            //                                   Player is still at the entry side, screen is black.
            //   Phase 2 (ArrivalDelay)        — player is at destination, streaming level loads.
            //                                   OnDoorEntered fires when both phases are done.

            // Lambda that starts phase 2 (fires OnDoorEntered, optionally after ArrivalDelay)
            auto FireOnDoorEntered = [this]()
            {
                if (PendingDoorArrivalDelay > 0.0f)
                {
                    GetWorld()->GetTimerManager().SetTimer(
                        DoorArrivalTimerHandle,
                        [this]() { OnDoorEntered.Broadcast(PendingDoorTileCoord, PendingDoorDestCoord, PendingDoorArrivalFacing); },
                        PendingDoorArrivalDelay,
                        false
                    );
                }
                else
                {
                    OnDoorEntered.Broadcast(PendingDoorTileCoord, PendingDoorDestCoord, PendingDoorArrivalFacing);
                }
            };

            if (PendingDoorTransitionDelay > 0.0f)
            {
                // Phase 1: wait for fade to go fully black, then teleport and start phase 2
                GetWorld()->GetTimerManager().SetTimer(
                    DoorTransitionTimerHandle,
                    [this, FireOnDoorEntered]()
                    {
                        PerformDoorTeleport(PendingDoorDestCoord, PendingDoorArrivalFacing);
                        FireOnDoorEntered();
                    },
                    PendingDoorTransitionDelay,
                    false
                );
            }
            else
            {
                // No fade delay — teleport immediately then start phase 2
                PerformDoorTeleport(PendingDoorDestCoord, PendingDoorArrivalFacing);
                FireOnDoorEntered();
            }
        }
        else
        {
            // More steps to take — keep walking forward
            FGF_GridCoordinate EntryOffset = UGF_GridWorldSubsystem::GetDirectionOffset(PendingDoorFacing);
            FGF_GridCoordinate EntryTarget = CurrentGridPosition + EntryOffset;
            EntryTarget.Z = 0;
            ExecuteSkill(EntryTarget, PendingDoorFacing);
        }
        return;
    }

    // --- NPC train-follow: player steps to each position the NPC vacated ---
    if (bIsFollowingNPC)
    {
        if (NPCFollowQueue.Num() > 0)
        {
            TakeNextFollowStep();
        }
        else if (bNPCFollowPathFinished)
        {
            StopFollowingNPC();
        }
        // Queue empty but NPC still moving — player idles in place until next NPC step arrives
        return;
    }

    // --- Scripted player movement: tick path one step at a time ---
    if (bWasProcessingScriptedMove)
    {
        ScriptedMovePathIndex++;

        if (ScriptedMovePathIndex >= ScriptedMovePath.Num() - 1)
        {
            // Arrived at destination. ExecuteSkill overwrote CurrentFacing with the
            // direction of travel on the final step. If a facing override was active
            // (e.g. an NPC pushed backward while still facing the player), restore it
            // now — before UnlockMovement()/StopWalking() set the idle pose — so the
            // entity ends up facing the override direction, not the way it walked.
            if (bScriptedFacingOverride)
                CurrentFacing = ScriptedFacingOverrideDirection;

            // Arrived at destination
            bIsProcessingScriptedMove = false;
            bScriptedMovementActive   = false;
            bScriptedFacingOverride   = false;
            ScriptedMovePath.Empty();
            ScriptedMovePathIndex = 0;

            RestoreScriptedMoveSpeed();

            UnlockMovement();
            StopWalking();
            OnScriptedMoveComplete.Broadcast();
        }
        else
        {
            // Take the next step
            FGF_GridCoordinate NextTile = ScriptedMovePath[ScriptedMovePathIndex + 1];
            FGF_GridCoordinate Diff     = NextTile - CurrentGridPosition;
            EGF_PlayerDirection Dir     = CurrentFacing;
            if (FMath::Abs(Diff.X) >= FMath::Abs(Diff.Y))
                Dir = (Diff.X > 0) ? EGF_PlayerDirection::South : EGF_PlayerDirection::North;
            else
                Dir = (Diff.Y > 0) ? EGF_PlayerDirection::West : EGF_PlayerDirection::East;

            EGF_PlayerDirection FacingDir = bScriptedFacingOverride ? ScriptedFacingOverrideDirection : Dir;
            CurrentFacing = FacingDir;
            SetWalkAnimation(FacingDir);
            ExecuteSkill(NextTile, Dir);
            // ExecuteSkill reset CurrentFacing to the travel direction and repainted via
            // UpdateSprite(); reassert the override facing and repaint for this step.
            if (bScriptedFacingOverride)
            {
                CurrentFacing = FacingDir;
                SetWalkAnimation(FacingDir);
            }
        }
        return;
    }

    // --- Normal door detection: player just stepped onto a door tile ---
    {
        FGF_GridCoordinate FlatLandedPos = CurrentGridPosition;
        FlatLandedPos.Z = 0;
        FGF_TileProperties LandedProps = GridSubsystem->GetTileProperties(FlatLandedPos);
        if (LandedProps.bHasDoor && !bSuppressDoorDetection)
        {
            bHasQueuedSkill = false;

            // Store door data before auto-steps change CurrentGridPosition
            PendingDoorTileCoord       = FlatLandedPos;
            PendingDoorDestCoord       = LandedProps.DoorDestinationCoord;
            PendingDoorFacing          = CurrentFacing;
            // Arrival facing: keep the entry direction unless this door overrides it.
            PendingDoorArrivalFacing   = LandedProps.bDoorOverrideStepDirection
                                            ? LandedProps.DoorOverrideStepDirection
                                            : CurrentFacing;
            RemainingEntrySteps        = LandedProps.EntryStepsBeforeTeleport;
            PendingDoorTransitionDelay = LandedProps.DoorTransitionDelay;
            PendingDoorArrivalDelay    = LandedProps.DoorArrivalDelay;
            bPendingDoorEntryStep      = true;

            // Lock immediately so input is blocked from this frame onward,
            // not after the last entry step. ExecuteSkill bypasses the lock.
            LockMovement();

            // Escape Rope / Dig dungeon tracking, driven by flags on the door marker:
            //  - Entrance: remember the tile just outside (the one the player stepped in
            //    from) as the return point. Non-door tile, so the rope can't re-trigger
            //    the entrance and send the player back inside.
            //  - Exit: clear the "in dungeon" state so the rope greys out again.
            if (LandedProps.bIsDungeonEntrance || LandedProps.bIsDungeonExit)
            {
                if (LandedProps.bIsDungeonEntrance)
                {
                    if (FGF_CreatureBridge::RegisterDungeonEntrance)
                    {
                        FGF_GridCoordinate OutdoorOffset = UGF_GridWorldSubsystem::GetDirectionOffset(CurrentFacing);
                        FGF_GridCoordinate ReturnTile = FlatLandedPos - OutdoorOffset;
                        ReturnTile.Z = 0;
                        FGF_CreatureBridge::RegisterDungeonEntrance(this, ReturnTile);
                    }
                }
                else // bIsDungeonExit
                {
                    if (FGF_CreatureBridge::ClearDungeonState)
                    {
                        FGF_CreatureBridge::ClearDungeonState(this);
                    }
                }
            }

            // Play door sound (enter or exit depending on which marker this is)
            if (LandedProps.DoorSound)
            {
                UGameplayStatics::PlaySoundAtLocation(this, LandedProps.DoorSound, GetOwner()->GetActorLocation());
            }

            // Fire immediately so Blueprint can start fade-out over the walk-in.
            // StepsLengthOfTime tells Blueprint exactly how long the walk-in will take
            // so it can match the fade duration to the animation.
            //float StepDistance = GridSubsystem->TileSize;
            //float TimePerStep = (MovementSpeed > 0.0f) ? (StepDistance / MovementSpeed) : 0.0f;
            float StepsLengthOfTime = RemainingEntrySteps;
            // Fire immediately so Blueprint can start fade-out over the walk-in
            OnDoorEnterStarted.Broadcast(FlatLandedPos, StepsLengthOfTime, LandedProps.bDoorOverrideStepDirection, PendingDoorArrivalFacing);

            // Take the first entry step immediately
            FGF_GridCoordinate EntryOffset = UGF_GridWorldSubsystem::GetDirectionOffset(CurrentFacing);
            FGF_GridCoordinate EntryTarget = CurrentGridPosition + EntryOffset;
            EntryTarget.Z = 0;
            ExecuteSkill(EntryTarget, CurrentFacing);
            return;
        }
        bSuppressDoorDetection = false;  // Clear after one step
    }

    if (bHasQueuedSkill)
    {
        bHasQueuedSkill = false;
        EGF_PlayerDirection Queued = QueuedMoveDirection;
        TryMove(Queued);
    }
}

//====================================================================================
// NPC TRAIN FOLLOW
//====================================================================================

void UGF_GridMovementComponent::StartFollowingNPC(UGF_GridNPCAIComponent* NPC)
{
    if (!NPC) return;

    FollowedNPC             = NPC;
    bIsFollowingNPC         = true;
    bNPCFollowPathFinished  = false;
    NPCFollowQueue.Empty();

    NPC->EnablePlayerFollowMode();
    NPC->OnNPCStepTaken.AddDynamic(this, &UGF_GridMovementComponent::HandleNPCStepTaken);
    NPC->OnCutsceneMovementComplete.AddDynamic(this, &UGF_GridMovementComponent::HandleNPCMoveComplete);

    bScriptedMovementActive = true;
    LockMovement();
}

void UGF_GridMovementComponent::StopFollowingNPC()
{
    if (IsValid(FollowedNPC))
    {
        FollowedNPC->DisablePlayerFollowMode();
        FollowedNPC->OnNPCStepTaken.RemoveDynamic(this, &UGF_GridMovementComponent::HandleNPCStepTaken);
        FollowedNPC->OnCutsceneMovementComplete.RemoveDynamic(this, &UGF_GridMovementComponent::HandleNPCMoveComplete);
    }
    FollowedNPC            = nullptr;
    bIsFollowingNPC        = false;
    bNPCFollowPathFinished = false;
    NPCFollowQueue.Empty();

    bKeepWalkingAnimation   = false;
    bScriptedMovementActive = false;

    // Force-clean tile registration.
    // During the follow, rapid ExecuteSkill calls can leave a stale OccupyingActor on a
    // tile the player already left (if a later registration overwrote the slot before
    // UnregisterEntityOnTile was called for the earlier tile).
    // Clear both CurrentGridPosition and TargetGridPosition, then re-register once cleanly.
    if (GridSubsystem)
    {
        FGF_GridCoordinate FlatCurrent = CurrentGridPosition;
        FlatCurrent.Z = 0;
        FGF_GridCoordinate FlatTarget = TargetGridPosition;
        FlatTarget.Z = 0;

        // Clear any ghost on the target tile (might differ from current if mid-move)
        if (FlatTarget != FlatCurrent)
            GridSubsystem->ClearActorFromTile(FlatTarget);

        // Re-register cleanly on the actual resting tile
        GridSubsystem->ClearActorFromTile(FlatCurrent);
        GridSubsystem->RegisterEntityOnTile(GetOwner(), FlatCurrent);
    }

    // Bypass the follow-guard so we actually unlock here
    MovementState = EGF_GridMovementState::Idle;
    bStoppingAfterCurrentMove = false;
    UpdateSprite();
    StopWalking();

    OnNPCFollowComplete.Broadcast();
}

void UGF_GridMovementComponent::HandleNPCStepTaken(FGF_GridCoordinate PreviousPosition)
{
    NPCFollowQueue.Add(PreviousPosition);

    // If the player is already idle, kick off the step immediately
    if (MovementState != EGF_GridMovementState::Moving)
        TakeNextFollowStep();
}

void UGF_GridMovementComponent::HandleNPCMoveComplete()
{
    bNPCFollowPathFinished = true;

    // If the queue already drained before this callback, finish now
    if (NPCFollowQueue.Num() == 0 && MovementState != EGF_GridMovementState::Moving)
        StopFollowingNPC();
}

void UGF_GridMovementComponent::TakeNextFollowStep()
{
    if (NPCFollowQueue.Num() == 0) return;

    FGF_GridCoordinate QueuedPos = NPCFollowQueue[0];
    NPCFollowQueue.RemoveAt(0);

    // Derive direction from where the NPC was relative to us.
    // Ignore NPC's exact coordinates (they may have non-zero Z from elevated tiles such as
    // healing machines / PCs, which GridToWorld multiplies into world Z and causes the
    // player to be launched into the air). Instead, compute the step target from OUR
    // current position + the direction — keeps the player on the ground plane always.
    FGF_GridCoordinate Diff = QueuedPos - CurrentGridPosition;

    // Already on the queued tile — skip silently and try the next one
    if (Diff.X == 0 && Diff.Y == 0)
    {
        if (NPCFollowQueue.Num() > 0)
            TakeNextFollowStep();
        return;
    }

    // Resolve direction from the diff (same convention as ExecuteSinglePathStep)
    EGF_PlayerDirection Dir = CurrentFacing;
    if      (FMath::Abs(Diff.X) >= FMath::Abs(Diff.Y))
        Dir = (Diff.X > 0) ? EGF_PlayerDirection::South : EGF_PlayerDirection::North;
    else
        Dir = (Diff.Y > 0) ? EGF_PlayerDirection::West : EGF_PlayerDirection::East;

    // Build OUR target tile by applying the direction offset to OUR current position.
    // This is always at ground level (Z=0 offset) regardless of what Z the NPC was at.
    FGF_GridCoordinate OurTarget = CurrentGridPosition + GridSubsystem->GetDirectionOffset(Dir);
    OurTarget.Z = 0;

    // Update CurrentFacing so the Paper ZD blueprint reads the correct direction
    CurrentFacing = Dir;
    bScriptedMovementActive  = true;
    bKeepWalkingAnimation    = (NPCFollowQueue.Num() > 0);

    SetWalkAnimation(Dir);
    ExecuteSkill(OurTarget, Dir);
}

//====================================================================================
// SCRIPTED PLAYER MOVEMENT
//====================================================================================

void UGF_GridMovementComponent::ScriptedMoveTo(FGF_GridCoordinate TargetCoord, bool bRun)
{
    if (!GridSubsystem)
        return;

    // If called while a move is already in flight, CurrentGridPosition is stale — it still holds
    // the tile we LEFT. ExecuteSkill already unregistered us from CurrentGridPosition and registered
    // us on TargetGridPosition, so tile data is ahead of CurrentGridPosition.
    //
    // We check TargetGridPosition != CurrentGridPosition rather than MovementState == Moving because
    // cutscene code commonly calls LockMovement() BEFORE ScriptedMoveTo, changing state to Locked
    // while the player is still physically mid-step. Checking state would silently skip the snap,
    // leaving TargetGridPosition ghost-registered (CanMoveToTile has no != GetOwner() exception,
    // so ANY actor on a tile blocks it — causing a permanent invisible wall on that tile).
    {
        FGF_GridCoordinate FlatTarget  = TargetGridPosition; FlatTarget.Z  = 0;
        FGF_GridCoordinate FlatCurrent = CurrentGridPosition; FlatCurrent.Z = 0;

        if (FlatTarget != FlatCurrent)
        {
            CurrentGridPosition = TargetGridPosition;
            GetOwner()->SetActorLocation(TargetWorldPosition);
            MovementState = EGF_GridMovementState::Idle;
            MovementAlpha = 0.0f;
        }
    }

    // Find path — try respecting occupants first, then force through them if needed
    // (the 5th arg, bIgnoreOccupants, lets a scripted move pass through actors when boxed in).
    TArray<FGF_GridCoordinate> Path = GridSubsystem->FindPath(CurrentGridPosition, TargetCoord, false);
    if (Path.Num() == 0)
        Path = GridSubsystem->FindPath(CurrentGridPosition, TargetCoord, false, false, /*bIgnoreOccupants*/ true);

    if (Path.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ScriptedMoveTo: No path found to (%d,%d)"), TargetCoord.X, TargetCoord.Y);
        RestoreScriptedMoveSpeed();          // never started - don't leak a temporary speed change
        OnScriptedMoveComplete.Broadcast();  // Fire anyway so callers don't hang
        return;
    }

    // Already there?
    if (Path.Num() == 1)
    {
        RestoreScriptedMoveSpeed();          // never started - don't leak a temporary speed change
        StopWalking();
        OnScriptedMoveComplete.Broadcast();
        return;
    }

    bIsProcessingScriptedMove = true;
    bScriptedMovementActive   = true;
    ScriptedMovePath           = Path;
    ScriptedMovePathIndex      = 0;
    LockMovement();

    if (bRun)
    {
        ScriptedMoveOriginalSpeed = MovementSpeed;
        MovementSpeed *= 2.0f;
    }

    // Kick off the first step
    FGF_GridCoordinate NextTile = ScriptedMovePath[1];
    FGF_GridCoordinate Diff     = NextTile - CurrentGridPosition;
    EGF_PlayerDirection Dir     = CurrentFacing;
    if (FMath::Abs(Diff.X) >= FMath::Abs(Diff.Y))
        Dir = (Diff.X > 0) ? EGF_PlayerDirection::South : EGF_PlayerDirection::North;
    else
        Dir = (Diff.Y > 0) ? EGF_PlayerDirection::West : EGF_PlayerDirection::East;

    // bScriptedFacingOverride: use the override direction for sprite/animation only.
    // Dir is still passed to ExecuteSkill so tile registration and movement are correct.
    EGF_PlayerDirection FacingDir = bScriptedFacingOverride ? ScriptedFacingOverrideDirection : Dir;
    CurrentFacing = FacingDir;
    SetWalkAnimation(FacingDir);
    ExecuteSkill(NextTile, Dir);
    // ExecuteSkill set CurrentFacing = Dir (the travel direction) AND called UpdateSprite(),
    // repainting the walk anim in the travel direction. Reassert the override facing and
    // repaint so the sprite faces the override direction for the whole slide.
    if (bScriptedFacingOverride)
    {
        CurrentFacing = FacingDir;
        SetWalkAnimation(FacingDir);
    }
}

void UGF_GridMovementComponent::BeginPushedBack(EGF_PlayerDirection PushDirection, int32 NumTiles, bool bFacePushDirection, float SpeedMultiplier)
{
    if (!GridSubsystem) return;

    // Resolve facing override BEFORE ScriptedMoveTo so the very first step uses it
    if (bFacePushDirection)
    {
        // Bouncer style: player turns and walks away — no override needed, ScriptedMoveTo handles it
        bScriptedFacingOverride = false;
    }
    else
    {
        // Default: keep facing TOWARD the NPC (opposite of push direction) while sliding back —
        // the walk cycle plays pointed at the player, so it reads as a shove, not a walk-away.
        bScriptedFacingOverride = true;
        switch (PushDirection)
        {
            case EGF_PlayerDirection::North: ScriptedFacingOverrideDirection = EGF_PlayerDirection::South; break;
            case EGF_PlayerDirection::South: ScriptedFacingOverrideDirection = EGF_PlayerDirection::North; break;
            case EGF_PlayerDirection::East:  ScriptedFacingOverrideDirection = EGF_PlayerDirection::West;  break;
            case EGF_PlayerDirection::West:  ScriptedFacingOverrideDirection = EGF_PlayerDirection::East;  break;
            default:                      ScriptedFacingOverrideDirection = PushDirection;           break;
        }
    }

    // Temporarily scale movement speed for the push. ScriptedMoveOriginalSpeed is restored
    // automatically when the scripted move completes (or is cancelled), so normal speed resumes.
    if (SpeedMultiplier > 0.0f && !FMath::IsNearlyEqual(SpeedMultiplier, 1.0f))
    {
        ScriptedMoveOriginalSpeed = MovementSpeed;
        MovementSpeed *= SpeedMultiplier;
    }

    FGF_GridCoordinate Offset     = UGF_GridWorldSubsystem::GetDirectionOffset(PushDirection);
    FGF_GridCoordinate TargetCoord = CurrentGridPosition + (Offset * NumTiles);
    ScriptedMoveTo(TargetCoord);
}

void UGF_GridMovementComponent::StepBackKeepFacing()
{
    if (!GridSubsystem) return;

    // Keep facing exactly where we are; move into the tile directly behind us.
    const EGF_PlayerDirection KeepFacing = CurrentFacing;
    const EGF_PlayerDirection MoveDir    = UGF_GridWorldSubsystem::GetOppositeDirection(KeepFacing);

    // Lock the sprite to KeepFacing for the whole slide and the final idle pose.
    // ScriptedMoveTo passes the real MoveDir to ExecuteSkill (so tile registration is
    // correct) but uses this override for animation only.
    bScriptedFacingOverride         = true;
    ScriptedFacingOverrideDirection = KeepFacing;

    FGF_GridCoordinate Offset      = UGF_GridWorldSubsystem::GetDirectionOffset(MoveDir);
    FGF_GridCoordinate TargetCoord = CurrentGridPosition + Offset;
    ScriptedMoveTo(TargetCoord);
}

void UGF_GridMovementComponent::RestoreScriptedMoveSpeed()
{
    if (ScriptedMoveOriginalSpeed > 0.0f)
    {
        MovementSpeed             = ScriptedMoveOriginalSpeed;
        ScriptedMoveOriginalSpeed = 0.0f;
    }
}

void UGF_GridMovementComponent::CancelScriptedMove()
{
    if (!bIsProcessingScriptedMove)
        return;

    bIsProcessingScriptedMove = false;
    bScriptedMovementActive   = false;
    bScriptedFacingOverride   = false;
    ScriptedMovePath.Empty();
    ScriptedMovePathIndex = 0;

    RestoreScriptedMoveSpeed();

    UnlockMovement();
    StopWalking();
}

//====================================================================================
// DOOR SYSTEM
//====================================================================================

bool UGF_GridMovementComponent::TrySnapToFloor()
{
    if (!GetWorld() || !GridSubsystem) return false;

    FVector ActorLoc = GetOwner()->GetActorLocation();
    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(GetOwner());
    QueryParams.bTraceComplex = false;

    // Start the trace just above the feet (see ExecuteSkill) so an overhead bridge the
    // actor is standing UNDER isn't grabbed as the floor and snapped onto.
    FVector TraceStart(ActorLoc.X, ActorLoc.Y, (ActorLoc.Z - ActorHeightAboveGround) + MaxStepUpHeight);
    FVector TraceEnd  (ActorLoc.X, ActorLoc.Y, ActorLoc.Z - GroundTraceDistance);

    if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
    {
        FVector SnappedPos = ActorLoc;
        SnappedPos.Z = HitResult.ImpactPoint.Z + ActorHeightAboveGround;
        GetOwner()->SetActorLocation(SnappedPos);
        PendingTargetFloorZ   = HitResult.ImpactPoint.Z;
        CurrentGridPosition.Z = FMath::RoundToInt(PendingTargetFloorZ / GridSubsystem->TileSize);
        return true;
    }
    return false;
}

bool UGF_GridMovementComponent::AreStreamingLevelsReady() const
{
    UWorld* World = GetWorld();
    if (!World) return true;

    for (ULevelStreaming* LS : World->GetStreamingLevels())
    {
        // Only wait on levels the streaming system actually wants visible.  A
        // level that should be visible but isn't yet is still running AddToWorld.
        if (LS && LS->ShouldBeVisible() && !LS->IsLevelVisible())
        {
            return false;
        }
    }
    return true;
}

void UGF_GridMovementComponent::SeedStreamingForCurrentLocation()
{
    if (UWorld* World = GetWorld())
    {
        // Re-evaluate streaming volumes at the actor's (post-teleport) location so
        // the destination sub-level is flagged ShouldBeVisible() this frame.
        FVector ViewLoc = GetOwner()->GetActorLocation();
        World->ProcessLevelStreamingVolumes(&ViewLoc);
    }
}

void UGF_GridMovementComponent::WaitForFloor()
{
    LockMovement();
    SuppressDoorGravity();
    SeedStreamingForCurrentLocation();
    bAwaitingFloorConfirmation  = true;
    bFloorConfirmSkipDoorEvents = true;
    FloorConfirmFacing          = CurrentFacing;
    FloorConfirmAutoSteps       = 0;
    FloorConfirmDestCoord       = CurrentGridPosition;
    FloorConfirmElapsed         = 0.0f;
    bFloorConfirmStallLogged    = false;
}

void UGF_GridMovementComponent::SuppressDoorGravity()
{
    // ACharacter path — most common for player pawns
    if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
        {
            SavedGravityScale = CMC->GravityScale;
            CMC->GravityScale = 0.0f;
            CMC->Velocity = FVector::ZeroVector;
            CMC->StopMovementImmediately();
            bDoorSuppressedGravity = true;
            return;
        }
    }

    // Fallback: physics-simulating root component
    if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
    {
        if (RootPrim->IsSimulatingPhysics())
        {
            RootPrim->SetPhysicsLinearVelocity(FVector::ZeroVector);
            RootPrim->SetEnableGravity(false);
            bDoorSuppressedGravity = true;
        }
    }
}

void UGF_GridMovementComponent::RestoreDoorGravity()
{
    if (!bDoorSuppressedGravity) return;
    bDoorSuppressedGravity = false;

    if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
        {
            CMC->GravityScale = SavedGravityScale;
        }
        return;
    }

    if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
    {
        RootPrim->SetEnableGravity(true);
    }
}

void UGF_GridMovementComponent::PerformDoorTeleport(FGF_GridCoordinate DestCoord, EGF_PlayerDirection Facing)
{
    if (!GridSubsystem) return;

    // Unregister from current tile
    FGF_GridCoordinate FlatOldPos = CurrentGridPosition;
    FlatOldPos.Z = 0;
    GridSubsystem->UnregisterEntityOnTile(GetOwner(), FlatOldPos);
    GridSubsystem->ClearActorFromTile(FlatOldPos);

    // Get destination XY from the grid (authoritative for XY, but not Z)
    FVector NewWorldPos = GridSubsystem->GridToWorld(DestCoord);

    // Find the live destination GridTileMarker to get the authoritative floor Z.
    // The cached DoorDestinationWorldLocation can be stale if the destination sub-level
    // wasn't loaded when the source door registered (common on first visit or after reload).
    // TActorIterator is fine here — this runs once per door transition, not per frame.
    bool bFoundMarker = false;
    for (TActorIterator<AGF_GridTileMarker> It(GetWorld()); It; ++It)
    {
        if (It->GetGridCoordinate() == DestCoord)
        {
            NewWorldPos.Z = It->GetActorLocation().Z;
            bFoundMarker = true;
            break;
        }
    }

    // Fall back to cached location if the marker isn't loaded yet
    if (!bFoundMarker)
    {
        FGF_TileProperties DoorProps = GridSubsystem->GetTileProperties(PendingDoorTileCoord);
        if (!DoorProps.DoorDestinationWorldLocation.IsZero())
            NewWorldPos.Z = DoorProps.DoorDestinationWorldLocation.Z;
    }

    NewWorldPos.Z += ActorHeightAboveGround;
    PendingTargetFloorZ = NewWorldPos.Z - ActorHeightAboveGround;

    // Verbose: five lines on every single door transition is a lot of noise for
    // something that only matters when door Z placement is actually wrong.
    UE_LOG(LogTemp, Verbose, TEXT("=== PerformDoorTeleport ==="));
    UE_LOG(LogTemp, Verbose, TEXT("  bFoundMarker: %s"), bFoundMarker ? TEXT("YES") : TEXT("NO (used fallback)"));
    UE_LOG(LogTemp, Verbose, TEXT("  ActorHeightAboveGround: %.1f"), ActorHeightAboveGround);
    UE_LOG(LogTemp, Verbose, TEXT("  Setting actor Z to: %.1f"), NewWorldPos.Z);

    GetOwner()->SetActorLocation(NewWorldPos);

    UE_LOG(LogTemp, Verbose, TEXT("  Actual actor Z after SetActorLocation: %.1f"), GetOwner()->GetActorLocation().Z);

    // Register on new tile
    CurrentGridPosition = DestCoord;
    FGF_GridCoordinate FlatNewPos = DestCoord;
    FlatNewPos.Z = 0;
    if (bIsFollower)
        GridSubsystem->RegisterFollowerOnTile(GetOwner(), FlatNewPos);
    else
        GridSubsystem->RegisterEntityOnTile(GetOwner(), FlatNewPos);

    CurrentFacing = Facing;
    UpdateSprite();

    // Sync scripted-move target state to the door destination so a later scripted move
    // (e.g. BeginPushedBack) doesn't get snapped back to the pre-warp tile by ScriptedMoveTo.
    TargetGridPosition  = CurrentGridPosition;
    TargetWorldPosition = GetOwner()->GetActorLocation();

    // Prevent the arrival tile re-triggering the door system immediately
    bSuppressDoorDetection = true;
    bDoorTeleportComplete  = true;

    // Re-lock movement. CompleteMovement resets MovementState to Idle unconditionally
    // before it reaches the door-step handling, so we have to lock again here to keep
    // the player frozen during the ArrivalDelay / DoorTransitionDelay timer.
    LockMovement();

    // Zero gravity and velocity so the player doesn't fall during the delay if the
    // destination floor hasn't loaded yet (CharacterMovementComponent keeps applying
    // gravity even while movement is locked).
    SuppressDoorGravity();
}

void UGF_GridMovementComponent::CompleteEnterDoor(FGF_GridCoordinate DestinationCoord, EGF_PlayerDirection FacingOnArrival)
{
    if (!GridSubsystem)
        return;

    UE_LOG(LogTemp, Verbose, TEXT("=== CompleteEnterDoor ==="));
    UE_LOG(LogTemp, Verbose, TEXT("  bDoorTeleportComplete: %s"), bDoorTeleportComplete ? TEXT("YES (pre-teleported)") : TEXT("NO (will teleport now)"));
    UE_LOG(LogTemp, Verbose, TEXT("  Actor Z on entry: %.1f"), GetOwner()->GetActorLocation().Z);

    // Teleport is normally done before OnDoorEntered fires (inside PerformDoorTeleport).
    // This fallback handles any legacy Blueprint that calls CompleteEnterDoor without
    // the new pre-teleport flow (e.g. if delays are both 0 and the timer path was skipped).
    if (!bDoorTeleportComplete)
    {
        PerformDoorTeleport(DestinationCoord, FacingOnArrival);
    }
    bDoorTeleportComplete = false;

    // Force the streaming volumes to evaluate at the new location so the
    // destination sub-level is flagged ShouldBeVisible() before we test readiness.
    SeedStreamingForCurrentLocation();

    FGF_TileProperties DoorProps = GridSubsystem->GetTileProperties(PendingDoorTileCoord);
    const int32 StepsToTake = DoorProps.DoorAutoSteps;

    // Stay in the polling path until BOTH the floor geometry is present AND the
    // destination sub-level has fully streamed in.  The streaming check matters
    // even when the trace hits immediately — e.g. the player is standing on the
    // persistent/base floor while a decorative sub-level is still being added to
    // the world; without it the door flow would continue early and the sub-level
    // would visibly pop in.
    if (!TrySnapToFloor() || !AreStreamingLevelsReady())
    {
        SuppressDoorGravity();
        bAwaitingFloorConfirmation  = true;
        bFloorConfirmSkipDoorEvents = false;
        FloorConfirmFacing          = FacingOnArrival;
        FloorConfirmAutoSteps       = StepsToTake;
        FloorConfirmDestCoord       = DestinationCoord;
        FloorConfirmElapsed         = 0.0f;
        bFloorConfirmStallLogged    = false;
        return;
    }

    // Floor found immediately — restore gravity and continue normally
    RestoreDoorGravity();

    if (StepsToTake > 0)
    {
        RemainingAutoSteps         = StepsToTake;
        AutoStepDirection          = FacingOnArrival;
        bIsProcessingDoorAutoSteps = true;

        // Unlock just enough for ExecuteSkill; CompleteMovement re-locks via the flag
        MovementState = EGF_GridMovementState::Idle;

        FGF_GridCoordinate StepOffset = UGF_GridWorldSubsystem::GetDirectionOffset(FacingOnArrival);
        FGF_GridCoordinate StepTarget = CurrentGridPosition + StepOffset;
        StepTarget.Z = 0;
        ExecuteSkill(StepTarget, FacingOnArrival);
    }
    else
    {
        UnlockMovement();
        OnDoorExited.Broadcast(DestinationCoord);
    }
}
//====================================================================================

void UGF_GridMovementComponent::SetAnimationSet(
    UPaperFlipbook* NewIdleNorth, UPaperFlipbook* NewIdleSouth,
    UPaperFlipbook* NewIdleEast,  UPaperFlipbook* NewIdleWest,
    UPaperFlipbook* NewWalkNorth, UPaperFlipbook* NewWalkSouth,
    UPaperFlipbook* NewWalkEast,  UPaperFlipbook* NewWalkWest)
{
    if (NewIdleNorth) IdleNorth = NewIdleNorth;
    if (NewIdleSouth) IdleSouth = NewIdleSouth;
    if (NewIdleEast)  IdleEast  = NewIdleEast;
    if (NewIdleWest)  IdleWest  = NewIdleWest;
    if (NewWalkNorth) WalkNorth = NewWalkNorth;
    if (NewWalkSouth) WalkSouth = NewWalkSouth;
    if (NewWalkEast)  WalkEast  = NewWalkEast;
    if (NewWalkWest)  WalkWest  = NewWalkWest;

    // Force the sprite to update immediately, bypassing the equality check
    // in SetIdleAnimation / SetWalkAnimation so the new flipbook always applies.
    if (SpriteComponent)
    {
        UPaperFlipbook* Desired = nullptr;
        if (MovementState == EGF_GridMovementState::Moving)
        {
            switch (CurrentFacing)
            {
                case EGF_PlayerDirection::North: Desired = WalkNorth; break;
                case EGF_PlayerDirection::South: Desired = WalkSouth; break;
                case EGF_PlayerDirection::East:  Desired = WalkEast;  break;
                case EGF_PlayerDirection::West:  Desired = WalkWest;  break;
            }
        }
        else
        {
            switch (CurrentFacing)
            {
                case EGF_PlayerDirection::North: Desired = IdleNorth; break;
                case EGF_PlayerDirection::South: Desired = IdleSouth; break;
                case EGF_PlayerDirection::East:  Desired = IdleEast;  break;
                case EGF_PlayerDirection::West:  Desired = IdleWest;  break;
            }
        }
        if (Desired)
            SpriteComponent->SetFlipbook(Desired);
    }
}

void UGF_GridMovementComponent::UpdateSprite()
{
    if (!SpriteComponent)
        return;

    if (MovementState == EGF_GridMovementState::Moving || bKeepWalkingAnimation)
    {
        SetWalkAnimation(CurrentFacing);
    }
    else
    {
        SetIdleAnimation(CurrentFacing);
    }
}

void UGF_GridMovementComponent::SetIdleAnimation(EGF_PlayerDirection Direction)
{
     if (!SpriteComponent)
        return;

    UPaperFlipbook* TargetFlipbook = nullptr;

    switch (Direction)
    {
        case EGF_PlayerDirection::North:
            TargetFlipbook = IdleNorth;
            break;
        case EGF_PlayerDirection::South:
            TargetFlipbook = IdleSouth;
            break;
        case EGF_PlayerDirection::East:
            TargetFlipbook = IdleEast;
            break;
        case EGF_PlayerDirection::West:
            TargetFlipbook = IdleWest;
            break;
    }


    if (TargetFlipbook && SpriteComponent->GetFlipbook() != TargetFlipbook)
    {
        SpriteComponent->SetFlipbook(TargetFlipbook);
    }
}

void UGF_GridMovementComponent::SetWalkAnimation(EGF_PlayerDirection Direction)
{
     if (!SpriteComponent)
        return;

    UPaperFlipbook* TargetFlipbook = nullptr;

    switch (Direction)
    {
        case EGF_PlayerDirection::North:
            TargetFlipbook = WalkNorth;
            break;
        case EGF_PlayerDirection::South:
            TargetFlipbook = WalkSouth;
            break;
        case EGF_PlayerDirection::East:
            TargetFlipbook = WalkEast;
            break;
        case EGF_PlayerDirection::West:
            TargetFlipbook = WalkWest;
            break;
    }


    if (TargetFlipbook && SpriteComponent->GetFlipbook() != TargetFlipbook)
    {
        // Flipbook changed (direction change or idle->walk transition).
        // Sync to WalkAnimationTime so the cycle continues instead of restarting from 0.
        SpriteComponent->SetFlipbook(TargetFlipbook);
        float Duration = TargetFlipbook->GetTotalDuration();
        if (Duration > 0.0f)
        {
            SpriteComponent->SetPlaybackPosition(FMath::Fmod(WalkAnimationTime, Duration), false);
        }
    }
    // If flipbook did NOT change: do nothing. Let it play freely.
    // Forcing SetPlaybackPosition every step was what caused the restart.
}

//====================================================================================
// FOLLOWER SYSTEM
//====================================================================================

void UGF_GridMovementComponent::SwapPositionsWithActor(AActor* OtherActor, FGF_GridCoordinate OtherActorTile)
{
    if (!GridSubsystem || !OtherActor)
    {
        return;
    }

    UGF_GridMovementComponent* OtherMovement = OtherActor->FindComponentByClass<UGF_GridMovementComponent>();
    if (!OtherMovement)
    {
        return;
    }

    // Store player's current position (where follower will move to)
    FGF_GridCoordinate PlayerOldPos = CurrentGridPosition;

    // Unregister both from their current tiles before either starts moving
    FGF_GridCoordinate FlatPlayerPos = PlayerOldPos;
    FlatPlayerPos.Z = 0;
    FGF_GridCoordinate FlatOtherPos = OtherActorTile;
    FlatOtherPos.Z = 0;

    GridSubsystem->UnregisterEntityOnTile(GetOwner(), FlatPlayerPos);
    GridSubsystem->UnregisterEntityOnTile(OtherActor, FlatOtherPos);

    // The follower moves the opposite direction to the player
    EGF_PlayerDirection ReverseDir;
    switch (CurrentFacing)
    {
        case EGF_PlayerDirection::North: ReverseDir = EGF_PlayerDirection::South; break;
        case EGF_PlayerDirection::South: ReverseDir = EGF_PlayerDirection::North; break;
        case EGF_PlayerDirection::East:  ReverseDir = EGF_PlayerDirection::West;  break;
        case EGF_PlayerDirection::West:  ReverseDir = EGF_PlayerDirection::East;  break;
        default:                      ReverseDir = EGF_PlayerDirection::South; break;
    }

    // Player animates to follower's tile
    ExecuteSkill(OtherActorTile, CurrentFacing);

    // Follower animates to player's old tile simultaneously.
    // ExecuteSkill uses StartWorldPosition = current actor location for Z, so height
    // is preserved naturally — no manual Z fix needed.
    OtherMovement->ExecuteSkill(PlayerOldPos, ReverseDir);

    // Suppress the follower's follow-AI for the duration of the swap animation.
    // Without this, the overlap case (DistXY==0 while player CurrentGridPosition
    // hasn't updated mid-animation) chases the follower back onto the player.
    UGF_GridNPCAIComponent* FollowerAI = OtherActor->FindComponentByClass<UGF_GridNPCAIComponent>();
    if (FollowerAI)
        FollowerAI->NotifyPositionSwapped();

}

void UGF_GridMovementComponent::DisplaceToTile(FGF_GridCoordinate NewTile)
{
    if (!GridSubsystem)
        return;

    // Unregister from current tile
    FGF_GridCoordinate FlatOldPos = CurrentGridPosition;
    FlatOldPos.Z = 0;
    GridSubsystem->UnregisterEntityOnTile(GetOwner(), FlatOldPos);

    // Update position
    CurrentGridPosition = NewTile;
    FVector NewWorldPos = GridSubsystem->GridToWorld(NewTile);

    // Auto-detect ground Z if enabled
    if (bAutoDetectGroundZ)
    {
        FVector TraceStart = NewWorldPos + FVector(0, 0, GroundTraceDistance);
        FVector TraceEnd = NewWorldPos - FVector(0, 0, GroundTraceDistance);

        FHitResult HitResult;
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(GetOwner());
        //QueryParams.AddIgnoredActors(GetWorld()->GetPawnIterator());
        QueryParams.bTraceComplex = true;

        if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
        {
            NewWorldPos.Z = HitResult.ImpactPoint.Z + ActorHeightAboveGround;
        }
    }

    GetOwner()->SetActorLocation(NewWorldPos);

    // Register on new tile
    FGF_GridCoordinate FlatNewPos = NewTile;
    FlatNewPos.Z = 0;

    //Use appropriate registration based on follower status
    if (bIsFollower)
    {
        GridSubsystem->RegisterFollowerOnTile(GetOwner(), FlatNewPos);
    }
    else
    {
        GridSubsystem->RegisterEntityOnTile(GetOwner(), FlatNewPos);
    }

    // Sync scripted-move target state to the displaced tile so the stale-snap guard in
    // ScriptedMoveTo doesn't later yank us back to the pre-displacement position.
    TargetGridPosition  = CurrentGridPosition;
    TargetWorldPosition = GetOwner()->GetActorLocation();

    // Update sprite
    UpdateSprite();
}


void UGF_GridMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

    if (GridSubsystem && GetOwner())
    {
        // Free every tile we occupy — not just CurrentGridPosition. A mid-move actor is
        // registered on its TARGET tile (ExecuteSkill registers there at move start), and
        // force-through/teleport paths can leave registrations elsewhere too. The sweep
        // guarantees no tile is left occupied no matter which one we were on.
        GridSubsystem->ClearActorFromAllTiles(GetOwner());
    }
}
//====================================================================================
// STEP LISTENER DEBUG
//====================================================================================

int32 UGF_GridMovementComponent::GetStepListenerCount() const
{
    return OnStepCompleted.GetAllObjects().Num();
}

void UGF_GridMovementComponent::DebugLogStepListeners() const
{
    const TArray<UObject*> Listeners = OnStepCompleted.GetAllObjects();

    // Unique IDs matter as much as the count: if these differ between two dumps you are
    // looking at two different components, and an empty list means nothing.
    UE_LOG(LogTemp, Warning, TEXT("[StepListeners] comp %s (id %u) on %s (id %u): %d bound"),
        *GetName(),
        GetUniqueID(),
        *GetNameSafe(GetOwner()),
        GetOwner() ? GetOwner()->GetUniqueID() : 0,
        Listeners.Num());

    // ToString spells out the function name each object bound, which is what tells
    // apart "the sanctuary is missing" from "the tamer never unbound".
    UE_LOG(LogTemp, Warning, TEXT("[StepListeners]   %s"), *OnStepCompleted.ToString<UObject>());

    for (UObject* Listener : Listeners)
    {
        UE_LOG(LogTemp, Warning, TEXT("[StepListeners]   - %s (id %u)"),
            *GetNameSafe(Listener),
            Listener ? Listener->GetUniqueID() : 0);
    }
}
