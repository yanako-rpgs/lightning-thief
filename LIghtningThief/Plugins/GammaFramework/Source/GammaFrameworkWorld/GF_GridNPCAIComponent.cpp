#include "GF_GridNPCAIComponent.h"
#include "GF_GridWorldSubsystem.h"
#include "GF_GridMovementComponent.h"
#include "GameFramework/Actor.h"
// UGameplayStatics::GetPlayerCharacter returns ACharacter*, which several sites here assign
// to AActor*. That needs the complete ACharacter type, not just a forward declaration --
// without it MSVC reports "cannot convert from 'ACharacter *' to 'AActor *'". This compiled
// only because GridMovementComponent.cpp includes it and the two shared a unity blob; any
// edit that splits them (adaptive non-unity uses 'git status' for the working set) breaks it.
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

UGF_GridNPCAIComponent::UGF_GridNPCAIComponent()
{
    PrimaryComponentTick.bCanEverTick = true;


    MoveCooldown = 1.0f;
    bCanMove = false; // Disabled by default until BeginPlay

    // NOTE: there used to be a PreCalculatePatrolPaths() call here. It was dead in two
    // ways — the function was declared but never defined (a link error on any full
    // rebuild), and the branch could never be true anyway: UPROPERTYs are deserialized
    // from the archetype AFTER the constructor runs, so Behavior and PatrolWaypoints
    // are always at their defaults at this point. If patrol paths ever do get cached,
    // do it from BeginPlay/TryInitialize, where the world and the waypoints exist.
}

void UGF_GridNPCAIComponent::BeginPlay()
{
    Super::BeginPlay();
    // Initialization is handled lazily in TryInitialize() on first Tick
    // so Blueprint-added components work correctly regardless of BeginPlay order

    // BeginPlay runs AGAIN every time a streamed sublevel is made visible. The actor is not
    // destroyed on the way out (ULevel::RemoveFromWorld only routes EndPlay), so on the way
    // back in every member of this component still holds the value it froze with — including
    // a half-finished path and an unrestored run speed. bInitialized being true already is
    // exactly what separates "player walked back into the building" from "first load".
    if (bInitialized)
        bPendingLevelReentryReset = true;
}

void UGF_GridNPCAIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Remember what we were doing before tearing it down — HandleLevelReentryReset needs to
    // know whether this NPC was actually running a route, so an NPC a story event moved on
    // purpose (idle, no waypoints) is never dragged back to its placement tile.
    bWasRoutedWhenHidden = bIsInCutsceneMovement
                        || bRunningCircleLoop
                        || bUsesRelativeRoute
                        || Behavior == EGF_NPCBehavior::Patrol
                        || PatrolWaypoints.Num() > 0;

    // Restore the base walk speed. NPCMoveTo(bRunToTarget) doubles MovementSpeed and only
    // puts it back when the move COMPLETES — a move interrupted by the level being hidden
    // never completes, so the doubled speed survives into the next visit and the next
    // run-move doubles it again: 2x, 4x, 8x, until the NPC is a blur crossing the room in
    // one frame. Clearing OriginalMoveSpeed here is the other half of that fix.
    if (MovementComponent && OriginalMoveSpeed > 0.0f)
        MovementComponent->MovementSpeed = OriginalMoveSpeed;
    OriginalMoveSpeed = 0.0f;

    // Drop the in-flight path. Its tiles belong to a level that is going away, and on the way
    // back in the Blueprint restarts the route from BeginPlay anyway.
    bIsInCutsceneMovement          = false;
    bCutsceneForceThroughOccupants = false;
    CurrentPath.Empty();
    CurrentPathIndex     = 0;
    TimeSinceLastMove    = 0.0f;
    bPendingFollowerStep = false;

    // Unbind the per-step delegates by hand rather than via StopCircleLoop/FinishPushFollow:
    // those broadcast completion events and stop animations, which has no business running
    // while the level is being torn down. bRunningCircleLoop is deliberately left set so the
    // re-entry reset can rebuild the ring from its anchor.
    if (MovementComponent)
    {
        MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandleCircleStepCompleted);

        if (bPushFollowStepBound)
        {
            MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandlePushFollowStepCompleted);
            bPushFollowStepBound = false;
        }
    }
    if (IsValid(CircleFollower) && CircleFollower->MovementComponent)
    {
        CircleFollower->MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandleFollowerStepCompleted);
    }

    PushFollowRemainingSteps = 0;
    bPushFollowStepPending   = false;
    PushFollowRetryCount     = 0;

    Super::EndPlay(EndPlayReason);
}

void UGF_GridNPCAIComponent::HandleLevelReentryReset()
{
    bPendingLevelReentryReset = false;

    if (!bResetToStartOnLevelReentry || !MovementComponent || !GridSubsystem)
        return;

    if (!bWasRoutedWhenHidden)
        return;

    // Route bookkeeping: start the loop from the top rather than resuming a leg that was
    // cut in half.
    CurrentPath.Empty();
    CurrentPathIndex     = 0;
    CurrentWaypointIndex = 0;
    WaypointWaitTimer    = 0.0f;
    TimeSinceLastMove    = 0.0f;

    if (bRunningCircleLoop)
    {
        // Rebuilds the ring from CircleLoopAnchor and re-places leader + follower on it.
        // Safe to call even if the Blueprint also calls StartCircleLoop from BeginPlay:
        // both land on the same anchored ring.
        StartCircleLoop(CircleFollower, CircleFirstDirection, CircleStepsPerSide, bCircleClockwise, FollowerTilesBehind);
        return;
    }

    const FGF_GridCoordinate Now = MovementComponent->GetCurrentGridPosition();
    if (Now.X != StartingPosition.X || Now.Y != StartingPosition.Y)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("NPCAI [%s]: level re-entry - returning to placement tile (%d,%d) from (%d,%d)"),
            *GetNameSafe(GetOwner()), StartingPosition.X, StartingPosition.Y, Now.X, Now.Y);

        MovementComponent->TeleportToGrid(StartingPosition);
    }
}

bool UGF_GridNPCAIComponent::TryInitialize()
{
    if (bInitialized) return true;

    UWorld* World = GetWorld();
    if (!World) return false;

    if (!GridSubsystem)
        GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem) return false;

    if (!MovementComponent)
        MovementComponent = GetOwner()->FindComponentByClass<UGF_GridMovementComponent>();
    if (!MovementComponent) return false;

    OriginalPosition  = MovementComponent->GetCurrentGridPosition();
    StartingPosition  = OriginalPosition;
    bCanMove          = true;
    bInitialized      = true;

    return true;
}

void UGF_GridNPCAIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bInitialized && !TryInitialize())
        return;

    if (!MovementComponent || !GridSubsystem)
        return;

    // Deferred to here rather than done in BeginPlay: the sublevel's collision is not
    // guaranteed to be registered on the frame BeginPlay runs, and the reset floor-traces.
    if (bPendingLevelReentryReset)
        HandleLevelReentryReset();

    // Circle-loop safety net: advance the lockstep loop whenever both are idle (the first
    // step, a blocked retry once the player clears the tile, or resuming after a pause).
    // Continuity comes from the OnStepCompleted handlers; this covers the rest.
    if (bRunningCircleLoop)
        AdvanceLoop();

    // Push-follow deferred step: fires one tick after HandlePushFollowStepCompleted so
    // the player's CompleteMovement has fully resolved (scripted next-step ExecuteSkill
    // already ran and unregistered the player from the tile the NPC wants to enter).
    if (bPushFollowStepPending && MovementComponent->CanMove())
    {
        bPushFollowStepPending = false;
        TakePushFollowStep();
    }

    // Tamer vision always runs — including during patrol (cutscene movement) so the
    // NPC can detect the player while walking its route.
    if (bIsTamer && !bHasBeatenTamer)
    {
        CheckTamerVision();
    }

    // Cutscene movement must run regardless of bCanMove — NPCMoveTo sets bCanMove=false
    // when a previous cutscene ends, and the next NPCMoveTo call won't work otherwise.
    if (bIsInCutsceneMovement)
    {
        HandleCutsceneMovement();
        return;
    }

    if (!bCanMove)
        return;

    // NPC-to-NPC follow: train-style — follower moves the instant the target moves,
    // not one step later. Two cases handled each tick when CanMove() is true:
    //   1. Follower is behind target's settled position  → catch up normally.
    //   2. Follower is AT target's settled position and target is mid-step
    //      → mirror target's direction immediately (true train behaviour).
    if (bIsFollowingNPCActor)
    {
        if (IsValid(FollowTargetNPC) && FollowTargetNPC->MovementComponent && MovementComponent->CanMove())
        {
            UGF_GridMovementComponent* TargetMC = FollowTargetNPC->MovementComponent;

            FGF_GridCoordinate MyPos      = MovementComponent->GetCurrentGridPosition();
            FGF_GridCoordinate NPCCurrent = TargetMC->GetCurrentGridPosition();  // settled tile
            FGF_GridCoordinate Diff       = NPCCurrent - MyPos;

            if (bFollowTargetRun)
                MovementComponent->MovementSpeed = TargetMC->MovementSpeed;

            if (Diff.X != 0 || Diff.Y != 0)
            {
                // Case 1: we're behind — step toward where the NPC currently is
                MovementComponent->TryMove(GetDirectionFromDiff(Diff));
            }
            else if (TargetMC->IsMoving())
            {
                // Case 2: we're on the same tile as the NPC's settled position and
                // the NPC has already started stepping to the next tile — mirror that
                // direction so we move simultaneously instead of waiting for arrival.
                FGF_GridCoordinate NPCTarget  = TargetMC->GetTargetGridPosition();
                FGF_GridCoordinate NPCDiff    = NPCTarget - NPCCurrent;
                if (NPCDiff.X != 0 || NPCDiff.Y != 0)
                {
                    MovementComponent->TryMove(GetDirectionFromDiff(NPCDiff));
                }
            }
        }
        return;
    }

    if (Behavior == EGF_NPCBehavior::FleeFromPlayer)
    {
        HandleFleeBehavior();  // Called every frame
        return;
    }

    if (Behavior == EGF_NPCBehavior::ChasePlayer)
    {
        HandleChaseBehavior();
        return;
    }

    if (Behavior == EGF_NPCBehavior::FollowPlayerPath)
    {
        HandleFollowPlayerPath();
        return;
    }

    if (CurrentPath.Num() > 0 &&
        CurrentPathIndex >= CurrentPath.Num() - 1 &&
        !MovementComponent->IsMoving())
    {
        UE_LOG(LogTemp, Verbose, TEXT("Path completed - firing OnMovementStopped"));

        CurrentPath.Empty();
        CurrentPathIndex = 0;
        TimeSinceLastMove = 0.0f;
        MovementComponent->bKeepWalkingAnimation = false;

        OnMovementStopped.Broadcast();
        OnPositionReached.Broadcast(MovementComponent->GetCurrentGridPosition());
        return;  // Exit early, don't process behaviors this frame
    }


    // Normal behaviors use cooldown
    if (MovementComponent->CanMove())
    {
        TimeSinceLastMove += DeltaTime;
        if (TimeSinceLastMove < MoveCooldown)
            return;
        TimeSinceLastMove = 0.0f;
    }



    // Handle normal behaviors
    switch (Behavior)
    {
        case EGF_NPCBehavior::Idle:
            if (bIdleCanLookAround)
            {
	            HandleLookAroundBehavior();
            }

            break;
        case EGF_NPCBehavior::Patrol:
            HandlePatrolBehavior();
            break;
        case EGF_NPCBehavior::LookAround:
            HandleLookAroundBehavior();
            break;
        case EGF_NPCBehavior::ChasePlayer:
            HandleChaseBehavior();
            break;
        case EGF_NPCBehavior::FleeFromPlayer:
            // Already handled above
            break;
        case EGF_NPCBehavior::FollowPlayerPath:
            HandleFollowPlayerPath();
            break;
    }

    // Check if follower is blocking player
    if (Behavior == EGF_NPCBehavior::FollowPlayerPath && bAutoMoveWhenBlocking)
    {
        CheckIfBlockingPlayer();
    }
}

//====================================================================================
// SIMPLIFIED BEHAVIOR HANDLERS
//====================================================================================

void UGF_GridNPCAIComponent::HandleCutsceneMovement()
{

    if (CurrentPath.Num() == 0)
    {
        bIsInCutsceneMovement = false;
        bCutsceneForceThroughOccupants = false;
        bCanMove = false;   // Freeze NPC - let Blueprint decide what to do next
        TimeSinceLastMove = 0.0f;

        if (OriginalMoveSpeed > 0.0f)
        {
            MovementComponent->MovementSpeed = OriginalMoveSpeed;
            OriginalMoveSpeed = 0.0f;
        }

        if (!bLoopAnimation)
            MovementComponent->bKeepWalkingAnimation = false;

        UE_LOG(LogTemp, Warning,
            TEXT("NPCAI [%s]: cutscene movement FAILED mid-path (abandoned). "
                 "OnCutsceneMovementComplete will NOT fire. Failed listeners: %s"),
            *GetNameSafe(GetOwner()),
            OnCutsceneMovementFailed.IsBound() ? TEXT("bound") : TEXT("NONE"));

        OnCutsceneMovementFailed.Broadcast();
        return;
    }


    if (CurrentPathIndex >= CurrentPath.Num() - 1 && !MovementComponent->IsMoving())
    {
        if (bShouldFacePlayerOnCutsceneComplete)
	        {

	        AActor* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	        if (Player && GridSubsystem)
	        {
	            FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());
	            FaceTowardsCoordinate(PlayerPos);

	            bIdleCanLookAround = false;

	            UE_LOG(LogTemp, Verbose, TEXT("Facing player at (%d, %d)"), PlayerPos.X, PlayerPos.Y);
	        }
        }

        bIsInCutsceneMovement = false;
        bCutsceneForceThroughOccupants = false;
        bCanMove = false;   // Freeze NPC after cutscene - Blueprint re-enables if needed
        CurrentPath.Empty();
        CurrentPathIndex = 0;
        TimeSinceLastMove = 0.0f;

        if (OriginalMoveSpeed > 0.0f)
        {
            MovementComponent->MovementSpeed = OriginalMoveSpeed;
            OriginalMoveSpeed = 0.0f;
        }

        if (!bLoopAnimation)
        {
            MovementComponent->bKeepWalkingAnimation = false;
            MovementComponent->StopWalking();
        }

        OnMovementStopped.Broadcast();
        OnPositionReached.Broadcast(MovementComponent->GetCurrentGridPosition());

        // IsBound() alone is ambiguous: it returns false BOTH when nothing was ever
        // bound AND when entries exist whose bound object has been destroyed (the
        // invocation list holds weak references). Those have completely different
        // causes, so log the raw entry count alongside it, plus the full path name
        // of this actor - actor display names are reused, so the path is the only
        // way to tell "same actor lost its binding" from "this is a different actor".
        {
            TArray<UObject*> Listeners = OnCutsceneMovementComplete.GetAllObjects();
            int32 DeadEntries = 0;
            for (UObject* Obj : Listeners)
            {
                if (!IsValid(Obj)) { ++DeadEntries; }
            }

            UE_LOG(LogTemp, Warning,
                TEXT("NPCAI COMPLETE | actor=%s | component=%s | listeners=%d (dead=%d) | IsBound=%s"),
                *GetPathNameSafe(GetOwner()),
                *GetPathNameSafe(this),
                Listeners.Num(),
                DeadEntries,
                OnCutsceneMovementComplete.IsBound() ? TEXT("true") : TEXT("FALSE"));

            for (UObject* Obj : Listeners)
            {
                UE_LOG(LogTemp, Warning, TEXT("NPCAI COMPLETE |   listener: %s (valid=%s)"),
                    *GetPathNameSafe(Obj), IsValid(Obj) ? TEXT("yes") : TEXT("NO - destroyed"));
            }
        }

        OnCutsceneMovementComplete.Broadcast();
        return;
    }

    ExecuteSinglePathStep();
}
void UGF_GridNPCAIComponent::HandlePatrolBehavior()
{
    // Circle loop is driven by OnStepCompleted + the safety net in TickComponent, not by
    // the cooldown-gated patrol tick — so just skip normal waypoint patrol while it runs.
    if (bRunningCircleLoop)
        return;

    // No waypoints? Nothing to do
    if (PatrolWaypoints.Num() == 0)
        return;

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate TargetWaypoint = PatrolWaypoints[CurrentWaypointIndex];

    // Check if close to waypoint
    int32 Distance = MyPos.ManhattanDistance(TargetWaypoint);
    if (Distance <= 1)
    {
        // Reached waypoint
        OnReachedWaypoint(CurrentWaypointIndex);

        CurrentWaypointIndex++;
        if (CurrentWaypointIndex >= PatrolWaypoints.Num())
        {
            if (bLoopPatrol)
            {
                CurrentWaypointIndex = 0;
            }
            else
            {
                return; // End of patrol
            }
        }

        TimeSinceLastMove = 0.0f;
        return;
    }

    // Need to path to waypoint
    if (CurrentPath.Num() == 0)
    {
        // bPreferOpenSpace is the inverse of bHugWalls: hugging walls means we do NOT
        // apply the wall-proximity penalty, so tight loops around blocking tiles work.
        CurrentPath = GridSubsystem->FindPath(MyPos, TargetWaypoint, false, !bHugWalls);
        CurrentPathIndex = 0;

        if (CurrentPath.Num() == 0)
        {
            UE_LOG(LogTemp, Verbose, TEXT("No path to waypoint! Skipping."));
            CurrentWaypointIndex++;
            if (CurrentWaypointIndex >= PatrolWaypoints.Num())
            {
                CurrentWaypointIndex = 0;
            }
            TimeSinceLastMove = 0.0f;
            return;
        }
    }

    // Skill along path
    ExecuteSinglePathStep();
}

EGF_PlayerDirection UGF_GridNPCAIComponent::RotateDirection(EGF_PlayerDirection Dir, bool bClockwise)
{
    // CW order:  North -> East -> South -> West -> North
    // CCW order: North -> West -> South -> East -> North
    switch (Dir)
    {
        case EGF_PlayerDirection::North: return bClockwise ? EGF_PlayerDirection::East  : EGF_PlayerDirection::West;
        case EGF_PlayerDirection::East:  return bClockwise ? EGF_PlayerDirection::South : EGF_PlayerDirection::North;
        case EGF_PlayerDirection::South: return bClockwise ? EGF_PlayerDirection::West  : EGF_PlayerDirection::East;
        case EGF_PlayerDirection::West:  return bClockwise ? EGF_PlayerDirection::North : EGF_PlayerDirection::South;
        default:                      return Dir;
    }
}

void UGF_GridNPCAIComponent::StartCircleLoop(UGF_GridNPCAIComponent* FollowerNPC, EGF_PlayerDirection FirstDirection, int32 StepsPerSide, bool bClockwise, int32 TilesBehind)
{
    // Self-initialize so this works when called on the spawn frame from BeginPlay — the AI
    // component otherwise caches MovementComponent/GridSubsystem lazily on the first Tick.
    if (!TryInitialize() || !MovementComponent)
        return;

    // Make sure the follower is initialized too, so its MovementComponent is available for
    // placement below (it is also spawned and would otherwise init on its own first Tick).
    if (IsValid(FollowerNPC))
        FollowerNPC->TryInitialize();

    StepsPerSide = FMath::Max(1, StepsPerSide);

    // Remember the shape so HandleLevelReentryReset can rebuild the identical ring without
    // the Blueprint having to call this again.
    CircleFirstDirection = FirstDirection;
    CircleStepsPerSide   = StepsPerSide;
    bCircleClockwise     = bClockwise;

    bRunningCircleLoop  = true;
    bCircleLeaderPaused = false;
    bFollowerHeld       = false;
    Behavior            = EGF_NPCBehavior::Patrol;   // routed through HandlePatrolBehavior
    bCanMove            = true;
    LoopClock           = 0;
    CircleFollower      = FollowerNPC;
    FollowerTilesBehind = FMath::Max(1, TilesBehind);
    FollowerClock       = -FollowerTilesBehind;   // starts FollowerTilesBehind tiles behind

    // Build the ring of ABSOLUTE tiles from the leader's current tile as P[0], walking the
    // square: StepsPerSide tiles per side, turning 90 degrees at each corner. Every entry is
    // a real, walkable loop tile — this is the single source of truth for both NPCs.
    //
    // The ring is anchored to the tile the loop was FIRST started from, not to wherever the
    // leader happens to be standing now. This function gets called again on every sublevel
    // visibility cycle (Blueprint BeginPlay, or HandleLevelReentryReset), and by then the
    // leader is frozen part-way round — re-anchoring to that tile slides the entire loop by
    // however far it got, so walking in and out repeatedly marches the NPC off across the map.
    LoopTiles.Empty();
    if (!bHasCircleLoopAnchor)
    {
        CircleLoopAnchor     = MovementComponent->GetCurrentGridPosition();
        bHasCircleLoopAnchor = true;
    }
    const FGF_GridCoordinate Start = CircleLoopAnchor;
    FGF_GridCoordinate P(Start.X, Start.Y, 0);
    EGF_PlayerDirection Dir = FirstDirection;
    for (int32 Side = 0; Side < 4; ++Side)
    {
        for (int32 Step = 0; Step < StepsPerSide; ++Step)
        {
            LoopTiles.Add(P);
            P = P + UGF_GridWorldSubsystem::GetDirectionOffset(Dir);
        }
        Dir = RotateDirection(Dir, bClockwise);
    }

    // Put the leader on P[0]. On a fresh start it is already standing there and this does
    // nothing; on a re-entry it is wherever the level hide froze it, and both clocks below
    // are about to be reset to index 0 — so its actual tile has to match or the first step
    // is taken in the wrong direction.
    if (LoopTiles.Num() > 0)
    {
        const FGF_GridCoordinate LeaderPos = MovementComponent->GetCurrentGridPosition();
        if (LeaderPos.X != LoopTiles[0].X || LeaderPos.Y != LoopTiles[0].Y)
        {
            MovementComponent->TeleportToGrid(FGF_GridCoordinate(LoopTiles[0].X, LoopTiles[0].Y, Start.Z));
        }
    }

    // Place the follower FollowerTilesBehind tiles behind the leader ON THE RING (a real loop
    // tile, so never a wall — and always behind, so it never sits on the leader's next tile).
    if (IsValid(FollowerNPC) && FollowerNPC->MovementComponent && LoopTiles.Num() > 0)
    {
        FollowerNPC->bRunningCircleLoop   = false;   // it's a follower, not a leader
        FollowerNPC->bIsFollowingNPCActor = false;   // we drive it directly
        FollowerNPC->Behavior             = EGF_NPCBehavior::Idle;
        FollowerNPC->bIdleCanLookAround   = false;
        FollowerNPC->FollowTargetNPC      = this;
        FollowerNPC->bCanMove             = true;
        FollowerNPC->bLoopAnimation       = true;

        const int32 L        = LoopTiles.Num();
        const int32 FIdx     = ((0 - FollowerTilesBehind) % L + L) % L;
        const FGF_GridCoordinate FTile = LoopTiles[FIdx];
        const int32 FloorZ   = Start.Z;   // keep the floor height so it isn't dropped under the floor
        FollowerNPC->MovementComponent->TeleportToGrid(FGF_GridCoordinate(FTile.X, FTile.Y, FloorZ));

        FollowerNPC->MovementComponent->OnStepCompleted.AddUniqueDynamic(this, &UGF_GridNPCAIComponent::HandleFollowerStepCompleted);
    }

    // Continuity: advance the moment either NPC finishes a tile.
    MovementComponent->OnStepCompleted.AddUniqueDynamic(this, &UGF_GridNPCAIComponent::HandleCircleStepCompleted);

    AdvanceLoop();   // kick the first step
}

void UGF_GridNPCAIComponent::PauseCircleLoop(bool bHoldFollowerInPlace)
{
    // Leader stops. The follower keeps going to close right up behind the leader (as in the
    // original game) — UNLESS bHoldFollowerInPlace (the player is talking to the follower
    // itself), in which case it also stops where it is. Follower motion is driven in
    // AdvanceLoop; only settle the leader's walk animation here.
    bCircleLeaderPaused = true;
    bFollowerHeld       = bHoldFollowerInPlace;
    if (MovementComponent)
        MovementComponent->bKeepWalkingAnimation = false;
}

void UGF_GridNPCAIComponent::ResumeCircleLoop()
{
    bCircleLeaderPaused = false;
    bFollowerHeld       = false;
    AdvanceLoop();   // restart the lockstep
}

void UGF_GridNPCAIComponent::StopCircleLoop()
{
    bRunningCircleLoop  = false;
    bCircleLeaderPaused = false;
    bFollowerHeld       = false;
    LoopTiles.Empty();
    LoopClock     = 0;
    FollowerClock = 0;

    // Release the ring anchor: an explicit stop means "this loop is over", so a later
    // StartCircleLoop is free to anchor a new ring wherever the NPC is then standing. Note
    // that a sublevel being hidden deliberately does NOT come through here — that has to keep
    // the anchor so the loop comes back in exactly the same place.
    bHasCircleLoopAnchor = false;
    if (MovementComponent)
    {
        MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandleCircleStepCompleted);
        MovementComponent->bKeepWalkingAnimation = false;
    }

    // Detach and halt the follower.
    if (IsValid(CircleFollower))
    {
        CircleFollower->FollowTargetNPC = nullptr;
        CircleFollower->bLoopAnimation  = false;
        if (CircleFollower->MovementComponent)
        {
            CircleFollower->MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandleFollowerStepCompleted);
            CircleFollower->MovementComponent->bKeepWalkingAnimation = false;
            CircleFollower->MovementComponent->StopWalking();
        }
    }
    CircleFollower = nullptr;
}

void UGF_GridNPCAIComponent::AdvanceLoop()
{
    if (!bRunningCircleLoop || !MovementComponent || LoopTiles.Num() == 0)
        return;

    static int32 BlockedLogThrottle = 0;   // diagnostic rate-limit only (not per-instance state)

    const int32 L = LoopTiles.Num();

    // A ring tile is clear if it's walkable and not held by anyone OTHER than our own two
    // loop NPCs (they never truly block each other — the follower is always strictly behind).
    // If the PLAYER (or anything external) is on a next tile, that NPC simply waits there.
    auto TileClear = [&](const FGF_GridCoordinate& Tile) -> bool
    {
        if (!GridSubsystem || !GridSubsystem->IsTileWalkable(Tile))
            return false;
        AActor* Occ = GridSubsystem->GetActorOnTile(Tile);
        if (Occ == nullptr || Occ == GetOwner())
            return true;
        return IsValid(CircleFollower) && Occ == CircleFollower->GetOwner();
    };

    // --- Leader: advance one ring tile unless paused for dialogue. ---
    if (!bCircleLeaderPaused && MovementComponent->CanMove())
    {
        const FGF_GridCoordinate LeaderNext = LoopTiles[(LoopClock + 1) % L];
        if (TileClear(LeaderNext))
        {
            FGF_GridCoordinate LeaderPos = MovementComponent->GetCurrentGridPosition();
            LeaderPos.Z = 0;
            if (MovementComponent->TryMove(GetDirectionFromDiff(LeaderNext - LeaderPos)))
            {
                MovementComponent->bKeepWalkingAnimation = true;
                ++LoopClock;
            }
        }
        else if ((BlockedLogThrottle++ % 120) == 0)   // static local below; rate-limit ~once/2s
        {
            // Idle and can't take the next tile — say why.
            AActor* Occ = GridSubsystem ? GridSubsystem->GetActorOnTile(LeaderNext) : nullptr;
            UE_LOG(LogTemp, Verbose,
                TEXT("CircleLoop: leader waiting for next tile (%d,%d) - walkable=%s occupant=%s"),
                LeaderNext.X, LeaderNext.Y,
                (GridSubsystem && GridSubsystem->IsTileWalkable(LeaderNext)) ? TEXT("YES") : TEXT("NO"),
                *GetNameSafe(Occ));
        }
    }

    // --- Follower: hold FollowerTilesBehind normally; close to 1 tile behind while the
    //     leader is paused (talking to the captain → his dog walks right up behind him). Stays put
    //     if the player is talking to the follower itself (bFollowerHeld). ---
    if (IsValid(CircleFollower) && CircleFollower->MovementComponent && CircleFollower->MovementComponent->CanMove())
    {
        UGF_GridMovementComponent* FMC = CircleFollower->MovementComponent;

        if (bFollowerHeld)
        {
            FMC->bKeepWalkingAnimation = false;   // talking to the follower - stay put
        }
        else
        {
            const int32 DesiredGap = bCircleLeaderPaused ? 1 : FollowerTilesBehind;
            const int32 Gap        = LoopClock - FollowerClock;   // >= 0; leader is ahead on the ring

            if (Gap > DesiredGap)
            {
                const FGF_GridCoordinate FollowerNext = LoopTiles[((FollowerClock + 1) % L + L) % L];
                if (TileClear(FollowerNext))
                {
                    FGF_GridCoordinate FollowerPos = FMC->GetCurrentGridPosition();
                    FollowerPos.Z = 0;
                    FMC->MovementSpeed         = MovementComponent->MovementSpeed;
                    FMC->bKeepWalkingAnimation = true;
                    if (FMC->TryMove(GetDirectionFromDiff(FollowerNext - FollowerPos)))
                        ++FollowerClock;
                }
            }
            else if (bCircleLeaderPaused)
            {
                FMC->bKeepWalkingAnimation = false;   // reached right-behind, settle
            }
        }
    }
}

void UGF_GridNPCAIComponent::HandleCircleStepCompleted()
{
    if (bRunningCircleLoop)
        AdvanceLoop();
}

void UGF_GridNPCAIComponent::HandleFollowerStepCompleted()
{
    if (bRunningCircleLoop)
        AdvanceLoop();
}

void UGF_GridNPCAIComponent::HandleLookAroundBehavior()
{
    if (!MovementComponent || !MovementComponent->CanMove())
        return;

    // Increment timer
    LookAroundTimer += GetWorld()->GetDeltaSeconds();

    // Look in a new direction every 2 seconds
    if (LookAroundTimer >= 2.0f)
    {
        LookAroundTimer = 0.0f;
        CurrentLookDirection = (CurrentLookDirection + 1) % 4;

        // Set facing direction
        EGF_PlayerDirection Directions[] = {
            EGF_PlayerDirection::North,
            EGF_PlayerDirection::East,
            EGF_PlayerDirection::South,
            EGF_PlayerDirection::West
        };

        MovementComponent->SetFacing(Directions[CurrentLookDirection]);
        UE_LOG(LogTemp, Verbose, TEXT("NPC looking around - New direction: %d"), CurrentLookDirection);
    }
}

void UGF_GridNPCAIComponent::HandleChaseBehavior()
{
    AActor* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
    if (!Player)
    {
        if (bIsCurrentlyFleeing)
        {
            bIsCurrentlyFleeing = false;

              if (Behavior == EGF_NPCBehavior::FleeFromPlayer)
			{
				Swap(MovementComponent->WalkNorth, MovementComponent->WalkSouth);
				Swap(MovementComponent->IdleNorth, MovementComponent->IdleSouth);
			}

            MovementComponent->bKeepWalkingAnimation = false;
            MovementComponent->MovementSpeed = 200.0f;
            MovementComponent->SetFacing(MovementComponent->GetCurrentFacing());
        }
        return;
    }

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());
    float Distance = MyPos.ManhattanDistance(PlayerPos);

    if (Distance > DetectionRange)
    {
        bCanSeePlayer = false;
        if (bIsCurrentlyFleeing)
        {
            bIsCurrentlyFleeing = false;
            MovementComponent->bKeepWalkingAnimation = false;
            MovementComponent->MovementSpeed = 200.0f;
            MovementComponent->SetFacing(MovementComponent->GetCurrentFacing());
        }
        return;
    }

    bCanSeePlayer = true;


    if (Distance <= 1)
    {
        if (bIsCurrentlyFleeing)
        {
            bIsCurrentlyFleeing = false;
            MovementComponent->bKeepWalkingAnimation = false;
            MovementComponent->MovementSpeed = 200.0f;
        }
        FaceTowardsCoordinate(PlayerPos);
        return;
    }


    if (!bIsCurrentlyFleeing)
    {
        bIsCurrentlyFleeing = true;
        MovementComponent->MovementSpeed = 450.0f;
    }


    MovementComponent->bKeepWalkingAnimation = true;


    MovementComponent->MovementSpeed = 450.0f;


    if (!MovementComponent->CanMove())
    {
        return;
    }


    TArray<EGF_PlayerDirection> Directions = {
        EGF_PlayerDirection::North,
        EGF_PlayerDirection::South,
        EGF_PlayerDirection::East,
        EGF_PlayerDirection::West
    };

    float BestScore = 99999.0f;
    EGF_PlayerDirection BestDirection = EGF_PlayerDirection::South;

    for (EGF_PlayerDirection Dir : Directions)
    {
        FGF_GridCoordinate Offset = UGF_GridWorldSubsystem::GetDirectionOffset(Dir);
        FGF_GridCoordinate TestPos = MyPos + Offset;

        if (GridSubsystem->CanMoveToTile(MyPos, TestPos, false))
        {
            float Score = TestPos.ManhattanDistance(PlayerPos);
            if (Score < BestScore)
            {
                BestScore = Score;
                BestDirection = Dir;
            }
        }
    }


    MovementComponent->TryMove(BestDirection);
}



void UGF_GridNPCAIComponent::HandleFleeBehavior()
{
    AActor* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
    if (!Player)
    {
        if (bIsCurrentlyFleeing)
        {
            bIsCurrentlyFleeing = false;
            MovementComponent->bKeepWalkingAnimation = false;
            MovementComponent->SetFacing(MovementComponent->GetCurrentFacing());
        }
        return;
    }

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());

    float Distance = MyPos.ManhattanDistance(PlayerPos);
    if (Distance > DetectionRange)
    {
        bCanSeePlayer = false;
        if (bIsCurrentlyFleeing)
        {
            bIsCurrentlyFleeing = false;
            MovementComponent->bKeepWalkingAnimation = false;
            MovementComponent->SetFacing(MovementComponent->GetCurrentFacing());
        }
        return;
    }

    bCanSeePlayer = true;

    if (!bIsCurrentlyFleeing)
    {
        bIsCurrentlyFleeing = true;
        MovementComponent->bKeepWalkingAnimation = true;
    }

    if (!MovementComponent->CanMove())
    {
        return;
    }

    // Find best direction
    TArray<EGF_PlayerDirection> Directions = {
        EGF_PlayerDirection::North,
        EGF_PlayerDirection::South,
        EGF_PlayerDirection::East,
        EGF_PlayerDirection::West
    };

    float BestScore = -99999.0f;
    EGF_PlayerDirection BestDirection = EGF_PlayerDirection::South;

    for (EGF_PlayerDirection Dir : Directions)
    {
        FGF_GridCoordinate Offset = UGF_GridWorldSubsystem::GetDirectionOffset(Dir);
        FGF_GridCoordinate TestPos = MyPos + Offset;

        if (GridSubsystem->CanMoveToTile(MyPos, TestPos, false))
        {
            float Score = TestPos.ManhattanDistance(PlayerPos);
            if (Score > BestScore)
            {
                BestScore = Score;
                BestDirection = Dir;
            }
        }
    }

    MovementComponent->TryMove(BestDirection);
}

void UGF_GridNPCAIComponent::CheckTamerVision()
{
    if (!MovementComponent || !GridSubsystem)
        return;

    AActor* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
    if (!Player)
        return;

    // Height gate: tiles are only 2D keys, so a bridge and the ground below it share
    // the same XY tile — compare actual world heights to keep vision on one layer.
    // Grid Zs can't be compared directly (NPC Z is floor-based, WorldToGrid Z includes
    // the capsule offset), so use the actors' world Z instead.
    const float HeightDifference = FMath::Abs(
        GetOwner()->GetActorLocation().Z - Player->GetActorLocation().Z);
    if (HeightDifference > MaxVisionHeightDifference * GridSubsystem->TileSize)
    {
        bCanSeePlayer = false;
        return;
    }

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());
    FGF_GridCoordinate DirectionOffset = UGF_GridWorldSubsystem::GetDirectionOffset(VisionDirection);

    // Normalize player Z to NPC's Z — WorldToGrid returns world-height-based Z which differs from NPC's grid Z
    FGF_GridCoordinate PlayerPosAdjusted(PlayerPos.X, PlayerPos.Y, MyPos.Z);

    // Check vision cone
    for (int32 i = 1; i <= VisionRange; i++)
    {
        FGF_GridCoordinate CheckPos(
            MyPos.X + DirectionOffset.X * i,
            MyPos.Y + DirectionOffset.Y * i,
            MyPos.Z
        );

        if (CheckPos == PlayerPosAdjusted)
        {
            if (!bCanSeePlayer)
            {
                bCanSeePlayer = true;
                OnPlayerDetected.Broadcast();
            }
            return;
        }

        // Tile data (walkability/occupancy) is keyed on Z=0 — same as movement code
        FGF_GridCoordinate FlatCheckPos(CheckPos.X, CheckPos.Y, 0);

        if (!GridSubsystem->IsTileWalkable(FlatCheckPos))
        {
            break;
        }

        // Another NPC standing in the cone blocks sight past them
        if (GridSubsystem->IsTileOccupied(FlatCheckPos))
        {
            break;
        }
    }

    // Player is not in the vision cone — reset so detection fires again on next entry
    bCanSeePlayer = false;
}

//====================================================================================
// CORE PATH EXECUTION
//====================================================================================

void UGF_GridNPCAIComponent::ExecuteSinglePathStep()
{

    if (CurrentPath.Num() == 0)
    {
        TimeSinceLastMove = 0.0f;
        return;
    }


    if (CurrentPathIndex >= CurrentPath.Num() - 1 && !MovementComponent->IsMoving())
    {
        if (bIsInCutsceneMovement && bShouldFacePlayerOnCutsceneComplete)
        {

          if (bIsInCutsceneMovement)
				{
		        AActor* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	        if (Player && GridSubsystem && MovementComponent)
	        {
	            FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());
	            FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
	            FGF_GridCoordinate Direction = PlayerPos - MyPos;

	            EGF_PlayerDirection FaceDirection = EGF_PlayerDirection::South;
	            if (FMath::Abs(Direction.X) > FMath::Abs(Direction.Y))
					{
					    // X difference is bigger - face North or South
					    FaceDirection = (Direction.X > 0) ? EGF_PlayerDirection::North : EGF_PlayerDirection::South;
					}
					else
					{
					    // Y difference is bigger - face East or West
					    FaceDirection = (Direction.Y > 0) ? EGF_PlayerDirection::East : EGF_PlayerDirection::West;
					}

	            MovementComponent->bOverrideFacingOnComplete = true;
	            MovementComponent->OverrideFacingDirection = FaceDirection;

                bIdleCanLookAround = true;

	            UE_LOG(LogTemp, Verbose, TEXT("Will face player on completion"));


					}
				}
        }




        // Path complete
        CurrentPath.Empty();
        CurrentPathIndex = 0;
        TimeSinceLastMove = 0.0f;MovementComponent->bKeepWalkingAnimation = false;

        OnMovementStopped.Broadcast(); // broadcast

        OnPathCompleted();  // Trigger event
        return;
    }


    if (!MovementComponent->CanMove())
        return;

    // Guard: need at least one more step available
    if (CurrentPathIndex + 1 >= CurrentPath.Num())
        return;

    FGF_GridCoordinate Current = CurrentPath[CurrentPathIndex];
    FGF_GridCoordinate Next = CurrentPath[CurrentPathIndex + 1];

    // Calculate direction
    FGF_GridCoordinate DirectionVec = Next - Current;
    EGF_PlayerDirection MoveDirection = EGF_PlayerDirection::South;

    if (FMath::Abs(DirectionVec.X) > FMath::Abs(DirectionVec.Y))
    {
        MoveDirection = (DirectionVec.X > 0) ? EGF_PlayerDirection::South : EGF_PlayerDirection::North;
    }
    else
    {
        MoveDirection = (DirectionVec.Y > 0) ? EGF_PlayerDirection::West : EGF_PlayerDirection::East;
    }

    bool bHasMoreSteps = (CurrentPathIndex + 1 < CurrentPath.Num() - 1);

    if (bHasMoreSteps || bLoopAnimation)
    {
        MovementComponent->bKeepWalkingAnimation = true;
    }
    else
    {
        MovementComponent->bKeepWalkingAnimation = false;
    }


    // Capture position before the step — broadcast immediately on move start so the
    // player begins their step at the same time (train behaviour, not 1 step behind).
    FGF_GridCoordinate PreStepPos = MovementComponent->GetCurrentGridPosition();
    if (bPlayerFollowModeEnabled)
        StepFromPosition = PreStepPos;

    // Try to move
    if (MovementComponent->TryMove(MoveDirection))
    {
        CurrentPathIndex++;

        // Broadcast on step START so the player queues and begins moving simultaneously.
        // (Previously broadcast on step completion — caused 1-step lag.)
        if (bPlayerFollowModeEnabled)
            OnNPCStepTaken.Broadcast(StepFromPosition);
    }
    else
    {
        FGF_GridCoordinate FlatNext = Next;
        FlatNext.Z = 0;  // Always check at ground level

        // Is this the last step on a cutscene path?
        // (CurrentPathIndex + 1 is the node we just failed to reach,
        //  CurrentPath.Num() - 1 is the final node index)
        const bool bIsLastCutsceneStep = bIsInCutsceneMovement &&
                                         (CurrentPathIndex + 1 >= CurrentPath.Num() - 1);

        if (bIsLastCutsceneStep)
        {
            // The NPC is one tile away from the destination but the final step is
            // physically blocked (e.g. a prop with collision not registered in the grid).
            // Treat this as "close enough" — advance the index so HandleCutsceneMovement
            // sees path complete and fires OnCutsceneMovementComplete next tick.
            CurrentPathIndex = CurrentPath.Num() - 1;
            MovementComponent->bKeepWalkingAnimation = false;
            return;
        }

        if (GridSubsystem->IsTileOccupied(FlatNext))
        {
            AActor* Occupant = GridSubsystem->GetActorOnTile(FlatNext);
            if (Occupant && Occupant != GetOwner())
            {
                if (bCutsceneForceThroughOccupants)
                {
                    // Choreographed flee — shove straight through the occupant rather than
                    // giving up. ExecuteSkill bypasses the occupancy check that TryMove enforces.
                    MovementComponent->ExecuteSkill(FlatNext, MoveDirection);
                    CurrentPathIndex++;
                    if (bPlayerFollowModeEnabled)
                        OnNPCStepTaken.Broadcast(StepFromPosition);
                    return;
                }

                CurrentPath.Empty();
                CurrentPathIndex = 0;
                TimeSinceLastMove = 0.0f;
                MovementComponent->bKeepWalkingAnimation = false;
                return;
            }
        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("Movement failed! Clearing path."));
            CurrentPath.Empty();
            CurrentPathIndex = 0;
            TimeSinceLastMove = 0.0f;
            MovementComponent->bKeepWalkingAnimation = false;
        }
    }


}

//====================================================================================
// PUBLIC API - CUTSCENE MOVEMENT
//====================================================================================

EGF_PlayerDirection UGF_GridNPCAIComponent::GetDirectionBetweenCoords(FGF_GridCoordinate From, FGF_GridCoordinate To)
{
	int32 DX = To.X - From.X;
    int32 DY = To.Y - From.Y;

    if (FMath::Abs(DY) > FMath::Abs(DX))
    {
        return (DY > 0) ? EGF_PlayerDirection::North : EGF_PlayerDirection::South;
    }
    else
    {
	    return (DX > 0) ? EGF_PlayerDirection::East : EGF_PlayerDirection::West;
    }
}

void UGF_GridNPCAIComponent::NPCMoveTo(FGF_GridCoordinate TargetCoord, bool bRunToTarget, bool bFacePlayerAfter, bool bPreferOpenSpace)
{
    // Self-initialize: a runtime-spawned NPC may have NPCMoveTo called (e.g. from a
    // dialogue event) before its first Tick has run TryInitialize(), leaving
    // MovementComponent/GridSubsystem uncached. TryInitialize() is idempotent.
    if (!TryInitialize())
    {
        UE_LOG(LogTemp, Error, TEXT("NPCMoveTo: Missing components!"));
        OnCutsceneMovementFailed.Broadcast();
        return;
    }

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();

    // Tile registration and pathfinding always operate at Z=0.
    // If the caller passes a non-zero Z (e.g. from WorldToGrid which encodes world height),
    // strip it so FindPath and IsTileOccupied checks use the canonical tile key.
    TargetCoord.Z = 0;

    // Find path — use wall proximity preference for normal movement, skip it for
    // tamer approach (NPCMoveToPlayer passes bPreferOpenSpace=false).
    bCutsceneForceThroughOccupants = false;
    CurrentPath = GridSubsystem->FindPath(MyPos, TargetCoord, false, bPreferOpenSpace);
        if (CurrentPath.Num() == 0)
    {
        // Fallback: allow the route to pass through occupied tiles. This is what lets a
        // choreographed NPC (e.g. a grunt fleeing past the player) path through a narrow
        // corridor the player is standing in / being pushed through. Remember we did this
        // so the per-step executor shoves through occupants instead of abandoning the path.
        CurrentPath = GridSubsystem->FindPath(MyPos, TargetCoord, false, bPreferOpenSpace, /*bIgnoreOccupants*/ true);
        bCutsceneForceThroughOccupants = (CurrentPath.Num() > 0);

        if (CurrentPath.Num() == 0)
        {
            // Was Verbose because a patrolling NPC that can't reach its target retries
            // every attempt and floods the log. But a cutscene move is a one-shot call
            // whose silent failure hangs the scene, so that case has to be visible.
            if (bIsInCutsceneMovement)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("NPCMoveTo [%s]: NO PATH from (%d,%d) to (%d,%d) - cutscene move aborted, "
                         "OnCutsceneMovementComplete will NOT fire."),
                    *GetNameSafe(GetOwner()), MyPos.X, MyPos.Y, TargetCoord.X, TargetCoord.Y);
            }
            else
            {
                UE_LOG(LogTemp, Verbose, TEXT("NPCMoveTo [%s]: No path found from (%d,%d) to (%d,%d)"),
                    *GetNameSafe(GetOwner()), MyPos.X, MyPos.Y, TargetCoord.X, TargetCoord.Y);
            }

            OnCutsceneMovementFailed.Broadcast();
            return;
        }
    }

    // Enter cutscene mode
    bIsInCutsceneMovement = true;

    // Starting a cutscene move with nothing listening means the NPC will walk to
    // its target and the scene will then stop dead. Catch it here, at the call,
    // rather than after the walk. Most common cause: the object that bound the
    // event was destroyed or streamed out (e.g. across a battle transition), so
    // the binding silently vanished while the component itself survived.
    {
        TArray<UObject*> StartListeners = OnCutsceneMovementComplete.GetAllObjects();
        UE_LOG(LogTemp, Warning,
            TEXT("NPCAI MOVE START | actor=%s | listeners=%d | IsBound=%s"),
            *GetPathNameSafe(GetOwner()),
            StartListeners.Num(),
            OnCutsceneMovementComplete.IsBound() ? TEXT("true") : TEXT("FALSE"));

        if (!OnCutsceneMovementComplete.IsBound() && !OnCutsceneMovementFailed.IsBound())
        {
            UE_LOG(LogTemp, Error,
                TEXT("NPCMoveTo [%s]: starting a cutscene move with NO live listeners. The move "
                     "will finish and nothing will continue the scene."),
                *GetNameSafe(GetOwner()));
        }
    }
    bShouldFacePlayerOnCutsceneComplete = bFacePlayerAfter;
    PreviousBehavior = Behavior;
    Behavior = EGF_NPCBehavior::Idle;
    CurrentPathIndex = 0;
    TimeSinceLastMove = MoveCooldown; // Allow immediate first move

    if (bRunToTarget && MovementComponent)
    {
        // Capture the base speed ONLY if we aren't already holding one. OriginalMoveSpeed is
        // restored when the move completes — but a move that never completes (the player left
        // the sublevel mid-cutscene, CancelCurrentMovement, a new NPCMoveTo issued over the
        // top of the old one) leaves MovementSpeed doubled. Re-capturing it there would take
        // the ALREADY-doubled value as the new base and double that: 2x, 4x, 8x, 16x, until
        // the tamer crosses the room in a single frame. This is the blur.
        if (OriginalMoveSpeed <= 0.0f)
            OriginalMoveSpeed = MovementComponent->MovementSpeed;

        MovementComponent->MovementSpeed = OriginalMoveSpeed * 2.0f;
    }
    else if (MovementComponent && OriginalMoveSpeed > 0.0f)
    {
        // A walking move issued while a previous run-move was still boosting the speed:
        // put the base speed back rather than walking this leg at run speed.
        MovementComponent->MovementSpeed = OriginalMoveSpeed;
        OriginalMoveSpeed = 0.0f;
    }

    // Skill NPC after setting coords

   // if (CurrentPath.Num() > 0 && MovementComponent)
   // {

    //    EGF_PlayerDirection Dir = GetDirectionBetweenCoords(MovementComponent->GetCurrentGridPosition(), CurrentPath[0]);
    //    MovementComponent->ExecuteSkill(CurrentPath[0], Dir);
    //}


}

void UGF_GridNPCAIComponent::NPCMoveToLocation(FVector TargetLocation, bool bRunToTarget)
{
    TryInitialize();

    if (!GridSubsystem)
    {
        UE_LOG(LogTemp, Error,
            TEXT("NPCMoveToLocation [%s]: aborting - GridSubsystem is NULL. "
                 "OnCutsceneMovementFailed broadcast, not Complete."),
            *GetNameSafe(GetOwner()));

        OnCutsceneMovementFailed.Broadcast();
        return;
    }

    FGF_GridCoordinate TargetCoord = GridSubsystem->WorldToGrid(TargetLocation);
    NPCMoveTo(TargetCoord, bRunToTarget);
}

void UGF_GridNPCAIComponent::NPCMoveToPlayer(bool bStopAdjacentToPlayer, bool bRunToTarget, bool bStopOnSideOfPlayer)
{
    // Same lazy-init hazard NPCMoveTo guards against: a cutscene can call this
    // before the component's first Tick has cached GridSubsystem/MovementComponent,
    // which used to drop straight into the failure branch below. Idempotent.
    TryInitialize();

    AActor* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
    if (!Player || !GridSubsystem)
    {
        // This fires OnCutsceneMovementFailed, NOT OnCutsceneMovementComplete.
        // A cutscene bound only to Complete will hang here, so make it loud.
        UE_LOG(LogTemp, Error,
            TEXT("NPCMoveToPlayer [%s]: aborting - Player=%s GridSubsystem=%s. "
                 "OnCutsceneMovementFailed broadcast; bind it or the cutscene will hang."),
            *GetNameSafe(GetOwner()),
            Player ? TEXT("ok") : TEXT("NULL"),
            GridSubsystem ? TEXT("ok") : TEXT("NULL"));

        OnCutsceneMovementFailed.Broadcast();
        return;
    }

    FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());
    PlayerPos.Z = 0;

    if (bStopOnSideOfPlayer)
    {
        // Try East first, then West — whichever is free and walkable.
        // East = (0,-1,0), West = (0,1,0) per GetDirectionOffset convention.
        const FGF_GridCoordinate EastOffset = UGF_GridWorldSubsystem::GetDirectionOffset(EGF_PlayerDirection::East);
        const FGF_GridCoordinate WestOffset = UGF_GridWorldSubsystem::GetDirectionOffset(EGF_PlayerDirection::West);

        FGF_GridCoordinate EastTile = FGF_GridCoordinate(PlayerPos.X + EastOffset.X, PlayerPos.Y + EastOffset.Y, 0);
        FGF_GridCoordinate WestTile = FGF_GridCoordinate(PlayerPos.X + WestOffset.X, PlayerPos.Y + WestOffset.Y, 0);

        const bool bEastFree = GridSubsystem->IsTileWalkable(EastTile) && !GridSubsystem->IsTileOccupied(EastTile);
        const bool bWestFree = GridSubsystem->IsTileWalkable(WestTile) && !GridSubsystem->IsTileOccupied(WestTile);

        if (bEastFree)
            PlayerPos = EastTile;
        else if (bWestFree)
            PlayerPos = WestTile;
        else
        {
            // Both side tiles are blocked (furniture, walls, etc.).
            // Rather than falling back to a different target that may also be blocked
            // (causing a path failure and a hung cutscene), stay in place and fire
            // OnCutsceneMovementComplete immediately — the NPC is close enough.
            UE_LOG(LogTemp, Verbose, TEXT("NPCMoveToPlayer [%s]: Both East and West tiles beside player are occupied - staying in place and completing."),
                *GetOwner()->GetName());
            NPCMoveTo(MovementComponent->GetCurrentGridPosition(), bRunToTarget, false);
            return;
        }
    }

    if (!bStopOnSideOfPlayer && bStopAdjacentToPlayer)
    {
        FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();

        // Primary candidate: the tile adjacent to the player in the direction the NPC is coming from.
        FGF_GridCoordinate PrimaryTile;
        FGF_GridCoordinate Direction = PlayerPos - MyPos;
        if (FMath::Abs(Direction.X) > FMath::Abs(Direction.Y))
        {
            int32 OffsetX = (Direction.X > 0) ? -1 : 1;
            PrimaryTile = FGF_GridCoordinate(PlayerPos.X + OffsetX, PlayerPos.Y, PlayerPos.Z);
        }
        else
        {
            int32 OffsetY = (Direction.Y > 0) ? -1 : 1;
            PrimaryTile = FGF_GridCoordinate(PlayerPos.X, PlayerPos.Y + OffsetY, PlayerPos.Z);
        }

        auto TileFree = [&](const FGF_GridCoordinate& Tile) -> bool
        {
            return GridSubsystem->IsTileWalkable(Tile) && !GridSubsystem->IsTileOccupied(Tile);
        };

        if (TileFree(PrimaryTile))
        {
            // Happy path — tile is clear, use it as-is.
            PlayerPos = PrimaryTile;
        }
        else
        {
            // Primary tile is blocked (another NPC, furniture, etc.).
            // Try all four adjacent tiles sorted by Manhattan distance to the NPC
            // so we pick the closest free spot rather than an arbitrary one.
            const EGF_PlayerDirection Dirs[] = {
                EGF_PlayerDirection::North, EGF_PlayerDirection::South,
                EGF_PlayerDirection::East,  EGF_PlayerDirection::West
            };

            FGF_GridCoordinate BestTile   = FGF_GridCoordinate(-1, -1, -1);
            int32           BestDist   = INT_MAX;

            for (EGF_PlayerDirection Dir : Dirs)
            {
                const FGF_GridCoordinate Offset    = UGF_GridWorldSubsystem::GetDirectionOffset(Dir);
                const FGF_GridCoordinate Candidate = FGF_GridCoordinate(PlayerPos.X + Offset.X, PlayerPos.Y + Offset.Y, 0);

                if (!TileFree(Candidate)) continue;

                const int32 Dist = FMath::Abs(Candidate.X - MyPos.X) + FMath::Abs(Candidate.Y - MyPos.Y);
                if (Dist < BestDist)
                {
                    BestDist = Dist;
                    BestTile = Candidate;
                }
            }

            if (BestDist != INT_MAX)
            {
                PlayerPos = BestTile;
            }
            else
            {
                // All adjacent tiles are blocked — stay in place and complete gracefully.
                UE_LOG(LogTemp, Verbose, TEXT("NPCMoveToPlayer [%s]: All tiles adjacent to player are occupied - staying in place and completing."),
                    *GetOwner()->GetName());
                NPCMoveTo(MovementComponent->GetCurrentGridPosition(), bRunToTarget, false);
                return;
            }
        }
    }

    // Tamer approach — skip wall penalty so the path goes straight to the player
    NPCMoveTo(PlayerPos, bRunToTarget, true, /*bPreferOpenSpace=*/false);
}

EGF_PlayerDirection UGF_GridNPCAIComponent::GetDirectionFromOffset(FGF_GridCoordinate Offset) const
{
    return GetDirectionFromDiff(Offset);
}

void UGF_GridNPCAIComponent::CancelCurrentMovement()
{
    if (!bIsInCutsceneMovement) return;

    bIsInCutsceneMovement = false;
    bCutsceneForceThroughOccupants = false;
    CurrentPath.Empty();
    CurrentPathIndex = 0;
    bCanMove = false;

    // Cancelling a run-move has to hand the base speed back. This is the interrupt path the
    // header recommends calling from OnPlayerDetected, so without it every tamer that spots
    // the player mid-run keeps the doubled speed for good.
    if (MovementComponent && OriginalMoveSpeed > 0.0f)
    {
        MovementComponent->MovementSpeed = OriginalMoveSpeed;
        OriginalMoveSpeed = 0.0f;
    }

    if (MovementComponent)
        MovementComponent->bKeepWalkingAnimation = false;
}

void UGF_GridNPCAIComponent::NPCMoveRelative(FGF_GridCoordinate RelativeOffset, bool bRunToTarget)
{
	TryInitialize();

	if (!MovementComponent || !GridSubsystem)
	{
        // Silently doing nothing here means a cutscene waiting on the movement
        // to finish just stops. Neither Complete nor Failed is broadcast, so at
        // minimum say so in the log.
        UE_LOG(LogTemp, Error,
            TEXT("NPCMoveRelative [%s]: aborting - MovementComponent=%s GridSubsystem=%s. No movement event will fire."),
            *GetNameSafe(GetOwner()),
            MovementComponent ? TEXT("ok") : TEXT("NULL"),
            GridSubsystem ? TEXT("ok") : TEXT("NULL"));
		return;
	}

    // Mark this NPC as route-driven: its destinations are expressed relative to wherever it
    // is standing, so a level visibility cycle that freezes it mid-leg shifts every
    // subsequent leg. HandleLevelReentryReset uses this to put it back on its placement tile.
    bUsesRelativeRoute = true;

    FGF_GridCoordinate CurrentPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate TargetPos = CurrentPos + RelativeOffset;

    NPCMoveTo(TargetPos, bRunToTarget, false);
}



//====================================================================================
// NPC-TO-NPC FOLLOW SYSTEM
//====================================================================================

void UGF_GridNPCAIComponent::StartFollowingNPCActor(UGF_GridNPCAIComponent* TargetNPC, bool bRun)
{
    if (!TargetNPC || !MovementComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("StartFollowingNPCActor: invalid target or no movement component."));
        return;
    }

    StopFollowingNPCActor();

    FollowTargetNPC      = TargetNPC;
    bIsFollowingNPCActor = true;
    bFollowTargetRun     = bRun;
    NPCFollowStepQueue.Empty();

    bLoopAnimation                           = true;
    MovementComponent->bKeepWalkingAnimation = true;
    bCanMove                                 = true;
    // No event bindings needed — the tick loop chases FollowTargetNPC directly.
}

void UGF_GridNPCAIComponent::StopFollowingNPCActor()
{
    // Safety: remove any legacy event bindings (harmless if never bound)
    if (IsValid(FollowTargetNPC))
        FollowTargetNPC->OnNPCStepTaken.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandleFollowTargetStepTaken);

    if (MovementComponent)
    {
        MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandleFollowStepComplete);
        MovementComponent->bKeepWalkingAnimation = false;
        MovementComponent->StopWalking();
    }

    FollowTargetNPC       = nullptr;
    bIsFollowingNPCActor  = false;
    bLoopAnimation        = false;
    NPCFollowStepQueue.Empty();
    bIsInCutsceneMovement = false;
    bCutsceneForceThroughOccupants = false;
}

void UGF_GridNPCAIComponent::HandleFollowTargetStepTaken(FGF_GridCoordinate PreviousPosition)
{
    PreviousPosition.Z = 0;
    NPCFollowStepQueue.Add(PreviousPosition);

    // Take the step immediately if we're not already mid-step
    if (!MovementComponent->IsMoving())
        TakeNextNPCFollowStep();
}

void UGF_GridNPCAIComponent::TakeNextNPCFollowStep()
{
    if (NPCFollowStepQueue.Num() == 0 || !MovementComponent || !GridSubsystem)
        return;

    FGF_GridCoordinate Target = NPCFollowStepQueue[0];
    NPCFollowStepQueue.RemoveAt(0);

    // Work out which direction to step — always one tile away
    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate Diff  = Target - MyPos;

    // Already at the target tile — skip this queued step and try the next one
    if (Diff.X == 0 && Diff.Y == 0)
    {
        if (NPCFollowStepQueue.Num() > 0)
            TakeNextNPCFollowStep();
        return;
    }

    EGF_PlayerDirection Dir = EGF_PlayerDirection::South;
    if (FMath::Abs(Diff.X) >= FMath::Abs(Diff.Y))
        Dir = (Diff.X > 0) ? EGF_PlayerDirection::South : EGF_PlayerDirection::North;
    else
        Dir = (Diff.Y > 0) ? EGF_PlayerDirection::West : EGF_PlayerDirection::East;

    // Bind step-complete BEFORE TryMove so we never miss the callback
    if (!MovementComponent->OnStepCompleted.IsAlreadyBound(this, &UGF_GridNPCAIComponent::HandleFollowStepComplete))
        MovementComponent->OnStepCompleted.AddDynamic(this, &UGF_GridNPCAIComponent::HandleFollowStepComplete);

    // Override speed to match the target NPC
    if (bFollowTargetRun && IsValid(FollowTargetNPC) && FollowTargetNPC->MovementComponent)
        MovementComponent->MovementSpeed = FollowTargetNPC->MovementComponent->MovementSpeed;

    MovementComponent->TryMove(Dir);
}

void UGF_GridNPCAIComponent::HandleFollowStepComplete()
{
    // Unbind — we re-bind each TakeNextNPCFollowStep call
    if (MovementComponent)
        MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandleFollowStepComplete);

    // Drain any queued steps that built up while we were moving
    if (NPCFollowStepQueue.Num() > 0)
        TakeNextNPCFollowStep();
}

void UGF_GridNPCAIComponent::EnablePlayerFollowMode()
{
    bPlayerFollowModeEnabled = true;

    // Bind to our own movement component's OnStepCompleted once
    if (!bFollowStepBound && MovementComponent)
    {
        MovementComponent->OnStepCompleted.AddDynamic(this, &UGF_GridNPCAIComponent::HandleOwnStepCompleted);
        bFollowStepBound = true;
    }
}

void UGF_GridNPCAIComponent::DisablePlayerFollowMode()
{
    bPlayerFollowModeEnabled = false;
}

void UGF_GridNPCAIComponent::HandleOwnStepCompleted()
{
    // OnNPCStepTaken is now broadcast at step START (inside the TryMove success block)
    // so the player begins moving simultaneously with the NPC rather than one step later.
    // Nothing to do here for player-follow mode.
}

void UGF_GridNPCAIComponent::CancelCutsceneMovement()
{
    if (!bIsInCutsceneMovement)
        return;

    bIsInCutsceneMovement = false;
    bCutsceneForceThroughOccupants = false;
    Behavior = PreviousBehavior;
    CurrentPath.Empty();
    CurrentPathIndex = 0;
    TimeSinceLastMove = 0.0f;

    if (MovementComponent && OriginalMoveSpeed > 0)
    {
        MovementComponent->MovementSpeed = OriginalMoveSpeed;
        OriginalMoveSpeed = 0.0f;   // released - the next run-move re-captures the real base
    }
}

//====================================================================================
// PUBLIC API - PUSH FOLLOW
//====================================================================================

void UGF_GridNPCAIComponent::PushPlayerBack(UGF_GridMovementComponent* PlayerMovement,
                                          EGF_PlayerDirection PushDirection,
                                          int32 NumTiles,
                                          bool bFacePushDirection,
                                          float SpeedMultiplier)
{
    if (!PlayerMovement || NumTiles <= 0)
        return;

    // Push the player first so their scripted path is queued up before the NPC starts moving
    PlayerMovement->BeginPushedBack(PushDirection, NumTiles, bFacePushDirection, SpeedMultiplier);

    // NPC walks forward in the same direction, in lockstep.
    // NOTE: BeginPushFollow calls FinishPushFollow() internally as a cleanup step.
    // PushFollowPlayerMovement must be set AFTER that call returns, otherwise
    // FinishPushFollow sees a valid (already-moving) player and incorrectly binds
    // HandlePlayerLastPushStepCompleted on every step.
    BeginPushFollow(PushDirection, NumTiles);

    // Safe to store now — FinishPushFollow's cleanup has already run.
    PushFollowPlayerMovement = PlayerMovement;
}

void UGF_GridNPCAIComponent::FleePastPlayer(FGF_GridCoordinate OffScreenTile,
                                          EGF_PlayerDirection PlayerPushDirection,
                                          int32 PushTiles,
                                          float PushSpeedMultiplier)
{
    // Self-initialize (may be called from a dialogue/cutscene event before first Tick).
    if (!TryInitialize())
    {
        UE_LOG(LogTemp, Error, TEXT("FleePastPlayer: Missing components!"));
        OnCutsceneMovementFailed.Broadcast();
        return;
    }

    // 1) Shove the player aside FIRST. BeginPushedBack runs synchronously and unregisters
    //    the player from their current tile at move start, so the lane is already free for
    //    the pathfind below — even on this same frame.
    if (PushTiles > 0)
    {
        AActor* Player = GetPlayerCharacter();
        UGF_GridMovementComponent* PlayerMovement =
            Player ? Player->FindComponentByClass<UGF_GridMovementComponent>() : nullptr;

        if (PlayerMovement)
        {
            // bFacePushDirection = false: player keeps facing the grunt while sliding aside.
            PlayerMovement->BeginPushedBack(PlayerPushDirection, PushTiles, false, PushSpeedMultiplier);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("FleePastPlayer: No player GridMovementComponent - running without a shove."));
        }
    }

    // 2) Run the grunt to the off-screen tile via full pathfinding, at run speed. Kicked off
    //    immediately after the push so the two move together. bPreferOpenSpace = false keeps
    //    the route direct through the tunnel instead of hugging open space.
    //    Fires OnCutsceneMovementComplete on arrival (bind it to hide/despawn the grunt).
    NPCMoveTo(OffScreenTile, /*bRunToTarget*/ true, /*bFacePlayerAfter*/ false, /*bPreferOpenSpace*/ false);
}

void UGF_GridNPCAIComponent::BeginPushFollow(EGF_PlayerDirection FollowDirection, int32 NumTiles)
{
    if (!MovementComponent || NumTiles <= 0)
        return;

    // Cancel any in-flight push follow before starting a new one
    FinishPushFollow();

    PushFollowDirection      = FollowDirection;
    PushFollowRemainingSteps = NumTiles;

    // Freeze vertical movement for the duration of push-follow.
    // The floor trace at each target tile can hit overhead geometry (tree trunks, props)
    // and compute a false FloorDelta — the same sky-climb bug the player has during
    // train-follow.  Freezing Z keeps the NPC flat; normal detection resumes in FinishPushFollow.
    MovementComponent->bFreezeVerticalMovement = true;

    // Bind to OUR OWN OnStepCompleted so each NPC step chains the next one at the
    // correct pace regardless of what the player's movement component is doing.
    MovementComponent->OnStepCompleted.AddDynamic(this, &UGF_GridNPCAIComponent::HandlePushFollowStepCompleted);
    bPushFollowStepBound = true;

    // Kick off the first step immediately — syncs with the player's first push step.
    TakePushFollowStep();
}

void UGF_GridNPCAIComponent::TakePushFollowStep()
{
    if (PushFollowRemainingSteps <= 0)
    {
        FinishPushFollow();
        return;
    }

    // Keep the walk animation running between steps unless this is the final one
    MovementComponent->bKeepWalkingAnimation = (PushFollowRemainingSteps > 1);

    const bool bMoved = MovementComponent->TryMove(PushFollowDirection);
    if (bMoved)
    {
        // Step started successfully — reset the retry counter.
        // HandlePushFollowStepCompleted will fire when the animation finishes.
        PushFollowRetryCount = 0;
    }
    else
    {
        // TryMove failed — most likely because the player is still registered on the
        // target tile (their scripted move advanced in the same CompleteMovement call
        // that fired our OnStepCompleted, so the unregistration may land the very next
        // tick).  Retry for up to MaxPushFollowRetries ticks before giving up.
        PushFollowRetryCount++;
        if (PushFollowRetryCount > MaxPushFollowRetries)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("PushFollow [%s]: TryMove failed %d times in a row - NPC is genuinely blocked. Aborting push-follow."),
                *GetOwner()->GetName(), PushFollowRetryCount);
            FinishPushFollow();
        }
        else
        {
            // Schedule a retry on the very next Tick
            bPushFollowStepPending = true;
        }
    }
}

void UGF_GridNPCAIComponent::HandlePushFollowStepCompleted()
{
    PushFollowRemainingSteps--;

    if (PushFollowRemainingSteps > 0)
    {
        // Defer to next tick: CompleteMovement broadcasts OnStepCompleted BEFORE advancing
        // the player's scripted path (ExecuteSkill for the next push step).  If we call
        // TakePushFollowStep right here the player is still registered on the target tile,
        // causing CanMoveToTile to block the NPC.  One tick later the player has already
        // unregistered and the NPC can step safely.
        bPushFollowStepPending = true;
    }
    else
    {
        FinishPushFollow();
    }
}

void UGF_GridNPCAIComponent::FinishPushFollow()
{
    if (bPushFollowStepBound && MovementComponent)
    {
        MovementComponent->OnStepCompleted.RemoveDynamic(this, &UGF_GridNPCAIComponent::HandlePushFollowStepCompleted);
        bPushFollowStepBound = false;
    }

    PushFollowRemainingSteps  = 0;
    bPushFollowStepPending    = false;   // discard any queued deferred step
    PushFollowRetryCount      = 0;
    // Note: PushFollowPlayerMovement is NOT cleared here — it's still needed by the
    // player-wait logic immediately below.  It gets cleared in HandlePlayerLastPushStepCompleted
    // or in the immediate-fire else-branch.

    if (MovementComponent)
    {
        // Re-enable normal floor detection now that push-follow is complete.
        MovementComponent->bFreezeVerticalMovement = false;
        MovementComponent->bKeepWalkingAnimation = false;
        MovementComponent->StopWalking();
    }

    // The NPC finishes its last step slightly before the player lands on their final tile
    // (the NPC is always one tile behind).  Wait for the player to fully arrive before
    // broadcasting so Blueprint receives the event only when everything has settled.
    if (PushFollowPlayerMovement && PushFollowPlayerMovement->IsMoving())
    {
        PushFollowPlayerMovement->OnStepCompleted.AddDynamic(
            this, &UGF_GridNPCAIComponent::HandlePlayerLastPushStepCompleted);
    }
    else
    {
        // Player already landed (or no reference) — fire immediately.
        PushFollowPlayerMovement = nullptr;
        OnPlayerPushedFinished.Broadcast();
    }
}

void UGF_GridNPCAIComponent::HandlePlayerLastPushStepCompleted()
{
    if (PushFollowPlayerMovement)
    {
        PushFollowPlayerMovement->OnStepCompleted.RemoveDynamic(
            this, &UGF_GridNPCAIComponent::HandlePlayerLastPushStepCompleted);
        PushFollowPlayerMovement = nullptr;
    }

    OnPlayerPushedFinished.Broadcast();
}

//====================================================================================
// PUBLIC API - BASIC FUNCTIONS
//====================================================================================

void UGF_GridNPCAIComponent::SetBehavior(EGF_NPCBehavior NewBehavior)
{

    if (MovementComponent)
    {
        MovementComponent->bKeepWalkingAnimation = false;


    }

    Behavior = NewBehavior;
    CurrentPath.Empty();
    CurrentPathIndex = 0;
}

void UGF_GridNPCAIComponent::AddPatrolWaypoint(FGF_GridCoordinate Waypoint)
{
    PatrolWaypoints.Add(Waypoint);
}

void UGF_GridNPCAIComponent::ClearPatrolWaypoints()
{
    PatrolWaypoints.Empty();
    CurrentWaypointIndex = 0;
}

void UGF_GridNPCAIComponent::SetPatrolWaypoints(const TArray<FGF_GridCoordinate>& Waypoints)
{
    PatrolWaypoints = Waypoints;
    CurrentWaypointIndex = 0;
}

AActor* UGF_GridNPCAIComponent::GetPlayerCharacter() const
{
    return UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
}

float UGF_GridNPCAIComponent::GetDistanceToPlayer() const
{
    AActor* Player = GetPlayerCharacter();
    if (!Player || !MovementComponent || !GridSubsystem)
        return -1.0f;

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());

    return MyPos.ManhattanDistance(PlayerPos);
}

bool UGF_GridNPCAIComponent::IsPlayerInLineOfSight()
{
    // Simplified version
    return GetDistanceToPlayer() <= DetectionRange;
}

bool UGF_GridNPCAIComponent::IsPlayerInVisionCone()
{
    AActor* Player = GetPlayerCharacter();
    if (!Player || !MovementComponent || !GridSubsystem)
        return false;

    // Same height gate as CheckTamerVision — a bridge and the ground below share
    // the same XY tile, so world heights decide whether the layers can see each other.
    const float HeightDifference = FMath::Abs(
        GetOwner()->GetActorLocation().Z - Player->GetActorLocation().Z);
    if (HeightDifference > MaxVisionHeightDifference * GridSubsystem->TileSize)
        return false;

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());
    FGF_GridCoordinate DirectionOffset = UGF_GridWorldSubsystem::GetDirectionOffset(VisionDirection);

    // Compare on XY only — grid Z bases differ between NPC and WorldToGrid output
    FGF_GridCoordinate PlayerPosAdjusted(PlayerPos.X, PlayerPos.Y, MyPos.Z);

    for (int32 i = 1; i <= VisionRange; i++)
    {
        FGF_GridCoordinate CheckPos(
            MyPos.X + DirectionOffset.X * i,
            MyPos.Y + DirectionOffset.Y * i,
            MyPos.Z
        );

        if (CheckPos == PlayerPosAdjusted)
            return true;

        // Tile data (walkability/occupancy) is keyed on Z=0 — same as movement code
        FGF_GridCoordinate FlatCheckPos(CheckPos.X, CheckPos.Y, 0);

        if (!GridSubsystem->IsTileWalkable(FlatCheckPos))
            break;

        // Another NPC standing in the cone blocks sight past them
        if (GridSubsystem->IsTileOccupied(FlatCheckPos))
            break;
    }

    return false;
}

void UGF_GridNPCAIComponent::MoveTowardsPlayer()
{
    if (!MovementComponent || !GridSubsystem)
        return;

    AActor* Player = GetPlayerCharacter();
    if (!Player)
        return;

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());

    // Find path to player
    CurrentPath = GridSubsystem->FindPath(MyPos, PlayerPos, false, true);
    CurrentPathIndex = 0;

    if (CurrentPath.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("MoveTowardsPlayer: No path found!"));
        return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("MoveTowardsPlayer: Path found with %d steps"), CurrentPath.Num());
}

void UGF_GridNPCAIComponent::MoveAwayFromPlayer()
{
    if (!MovementComponent || !GridSubsystem)
        return;

    AActor* Player = GetPlayerCharacter();
    if (!Player)
        return;

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate PlayerPos = GridSubsystem->WorldToGrid(Player->GetActorLocation());

    // Find best direction away from player
    TArray<EGF_PlayerDirection> Directions = {
        EGF_PlayerDirection::North,
        EGF_PlayerDirection::South,
        EGF_PlayerDirection::East,
        EGF_PlayerDirection::West
    };

    float BestScore = -99999.0f;
    EGF_PlayerDirection BestDirection = EGF_PlayerDirection::South;

    for (EGF_PlayerDirection Dir : Directions)
    {
        FGF_GridCoordinate Offset = UGF_GridWorldSubsystem::GetDirectionOffset(Dir);
        FGF_GridCoordinate TestPos = MyPos + Offset;

        if (GridSubsystem->CanMoveToTile(MyPos, TestPos, false))
        {
            float Score = TestPos.ManhattanDistance(PlayerPos);
            if (Score > BestScore)
            {
                BestScore = Score;
                BestDirection = Dir;
            }
        }
    }

    // Execute move
    if (MovementComponent->CanMove())
    {
        MovementComponent->TryMove(BestDirection);
        TimeSinceLastMove = 0.0f;
    }
}

void UGF_GridNPCAIComponent::MoveToCoordinate(FGF_GridCoordinate TargetCoord)
{
    if (!MovementComponent || !GridSubsystem)
        return;

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();

    // Find path
    CurrentPath = GridSubsystem->FindPath(MyPos, TargetCoord, false, true);
    CurrentPathIndex = 0;

    if (CurrentPath.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("MoveToCoordinate: No path found from (%d,%d) to (%d,%d)"),
            MyPos.X, MyPos.Y, TargetCoord.X, TargetCoord.Y);
        return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("MoveToCoordinate: Path found with %d steps"), CurrentPath.Num());
}

void UGF_GridNPCAIComponent::StopMoving()
{
    bCanMove = false;
    CurrentPath.Empty();
    CurrentPathIndex = 0;
}

void UGF_GridNPCAIComponent::ResumeMoving()
{
    bCanMove = true;
}

void UGF_GridNPCAIComponent::InteractWithPlayer()
{
    CurrentPath.Empty();
    CurrentPathIndex = 0;
    OnInteractWithPlayer();
}

void UGF_GridNPCAIComponent::ClearTileRegistration()
{
    if (!MovementComponent || !GridSubsystem)
        return;

    FGF_GridCoordinate Pos = MovementComponent->GetCurrentGridPosition();
    Pos.Z = 0;
    GridSubsystem->UnregisterEntityOnTile(GetOwner(), Pos);
    GridSubsystem->ClearActorFromTile(Pos);
}

void UGF_GridNPCAIComponent::FaceTowardsCoordinate(FGF_GridCoordinate TargetCoord)
{
    if (!MovementComponent)
        return;

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate Direction = TargetCoord - MyPos;

    EGF_PlayerDirection FaceDirection = EGF_PlayerDirection::South;

    if (FMath::Abs(Direction.X) > FMath::Abs(Direction.Y))
    {
        FaceDirection = (Direction.X > 0) ? EGF_PlayerDirection::South : EGF_PlayerDirection::North;
    }
    else
    {
        FaceDirection = (Direction.Y > 0) ? EGF_PlayerDirection::West : EGF_PlayerDirection::East;
    }

    MovementComponent->SetFacing(FaceDirection);
}

void UGF_GridNPCAIComponent::AddRelativePatrolWaypoint(FGF_GridCoordinate RelativeOffset)
{
    if (!TryInitialize() || !MovementComponent)
    {
        return;
    }

    // Anchored to the tile this NPC was PLACED on, not to wherever it is standing right now.
    // These routes get rebuilt from Blueprint BeginPlay, which re-runs every time a streamed
    // sublevel is made visible — and by then the NPC is parked somewhere along its own route,
    // so anchoring to the current tile walks the whole patrol away from its origin a little
    // further on every entry and re-entry.
    FGF_GridCoordinate CurrentStartPosition = StartingPosition;


    // Calculate absolute position from relative offset
    FGF_GridCoordinate AbsoluteWaypoint = CurrentStartPosition + RelativeOffset;

    // Add to patrol list
    PatrolWaypoints.Add(AbsoluteWaypoint);

}

void UGF_GridNPCAIComponent::SetRelativePatrolRoute(const TArray<FGF_GridCoordinate>& RelativeOffsets)
{
    if (!TryInitialize() || !MovementComponent)
    {
        return;
    }

    // Anchored to the placement tile, not the current one — see AddRelativePatrolWaypoint.
    FGF_GridCoordinate CurrentStartPosition = StartingPosition;


    // Clear existing waypoints
    PatrolWaypoints.Empty();

    // Convert all relative offsets to absolute positions
    for (const FGF_GridCoordinate& RelativeOffset : RelativeOffsets)
    {
        FGF_GridCoordinate AbsoluteWaypoint = CurrentStartPosition + RelativeOffset;
        PatrolWaypoints.Add(AbsoluteWaypoint);
    }

    CurrentWaypointIndex = 0;
}

//====================================================================================
// FOLLOWER CREATURE BEHAVIOR
//====================================================================================

void UGF_GridNPCAIComponent::HandleFollowPlayerPath()
{
    if (!PlayerToFollow || !MovementComponent || !GridSubsystem)
        return;

    FGF_GridCoordinate MyPos      = MovementComponent->GetCurrentGridPosition();
    FGF_GridCoordinate PlayerPos  = PlayerToFollow->GetCurrentGridPosition();
    int32 DistXY = FMath::Abs(MyPos.X - PlayerPos.X) + FMath::Abs(MyPos.Y - PlayerPos.Y);

    // Teleport if the gap has grown too large (level transitions, battles, etc.)
    if (DistXY > TeleportThreshold)
    {
        TeleportBehindPlayer();
        bPendingFollowerStep = false;
        return;
    }

    if (!MovementComponent->CanMove())
        return;

    // After a position swap, suppress follow movement for exactly as long as we still LOOK
    // overlapped: the player's CurrentGridPosition is not updated until their swap step
    // completes, so for those few frames DistXY reads 0 and the overlap case below would
    // chase them straight back onto their tile.
    //
    // The condition is the overlap, not "the player is standing still". Waiting for the
    // player to stop meant that a player who kept walking after a swap left the follower
    // frozen indefinitely — it only set off again once they let go of the stick.
    if (bJustSwapped)
    {
        if (DistXY == 0 && PlayerToFollow->IsMoving())
            return;

        bJustSwapped = false;   // Player's swap step has landed - resume following normally
    }

    // Sync speed to the player (handles walking ↔ running transitions)
    MovementComponent->MovementSpeed = PlayerToFollow->MovementSpeed;

    // -----------------------------------------------------------------------
    // True train movement via polling — no OnStepCompleted event needed.
    //
    //   DistXY == FollowDistance (1):
    //     We are in the correct trailing position.  Start our step the INSTANT
    //     the player starts theirs so both animate simultaneously.
    //
    //   DistXY > FollowDistance:
    //     We have fallen behind.  Catch up by stepping along the player's
    //     exact history path (same tiles, same turns), one tile per tick.
    //
    //   DistXY == 0:
    //     We are somehow on the player's settled tile.  Mirror their current
    //     step direction to escape the overlap immediately.
    // -----------------------------------------------------------------------
    FGF_GridCoordinate Diff = PlayerPos - MyPos;

    if (Diff.X == 0 && Diff.Y == 0)
    {
        // Overlap — escape by mirroring the player's active step direction.
        if (PlayerToFollow->IsMoving())
        {
            FGF_GridCoordinate PlayerTarget = PlayerToFollow->GetTargetGridPosition();
            FGF_GridCoordinate TargetDiff   = PlayerTarget - PlayerPos;
            if (TargetDiff.X != 0 || TargetDiff.Y != 0)
                MovementComponent->TryMove(GetDirectionFromDiff(TargetDiff));
        }
        return;
    }

    if (DistXY == FollowDistance)
    {
        // Correct trailing gap — only step when the player begins their step.
        if (PlayerToFollow->IsMoving())
            MovementComponent->TryMove(GetDirectionFromDiff(Diff));
        return;
    }

    // DistXY > FollowDistance: catch-up along the player's exact history path.
    const TArray<FGF_GridCoordinate>& PlayerHistory = PlayerToFollow->MovementHistory;

    FGF_GridCoordinate TargetPos = (PlayerHistory.Num() > FollowDistance)
        ? PlayerHistory[FollowDistance]
        : PlayerPos;

    // Find ourselves in the history so we retrace the player's exact tiles
    // rather than cutting a straight line across open space.
    int32 OurHistoryIndex = INDEX_NONE;
    for (int32 i = FollowDistance + 1; i < PlayerHistory.Num(); i++)
    {
        if (PlayerHistory[i].X == MyPos.X && PlayerHistory[i].Y == MyPos.Y)
        {
            OurHistoryIndex = i;
            break;
        }
    }

    FGF_GridCoordinate NextStep = (OurHistoryIndex != INDEX_NONE && OurHistoryIndex > FollowDistance)
        ? PlayerHistory[OurHistoryIndex - 1]   // one tile closer along exact path
        : TargetPos;                            // not in history - head straight for target

    EGF_PlayerDirection MoveDir = GetDirectionFromDiff(NextStep - MyPos);

    if (MovementComponent->TryMove(MoveDir))
    {
        CurrentPath.Empty();
        CurrentPathIndex = 0;
    }
    else
    {
        // Direct step blocked — A* fallback so we don't get permanently stuck.
        if (CurrentPath.Num() == 0 || CurrentPathIndex >= CurrentPath.Num() - 1)
        {
            CurrentPath      = GridSubsystem->FindPath(MyPos, TargetPos, false, true);
            CurrentPathIndex = 0;
        }

        if (CurrentPath.Num() > 1 && CurrentPathIndex + 1 < CurrentPath.Num())
        {
            FGF_GridCoordinate  PathNext = CurrentPath[CurrentPathIndex + 1];
            EGF_PlayerDirection PathDir  = GetDirectionFromDiff(PathNext - MyPos);

            if (MovementComponent->TryMove(PathDir))
                CurrentPathIndex++;
            else
            {
                CurrentPath.Empty();
                CurrentPathIndex = 0;
            }
        }
    }
}

void UGF_GridNPCAIComponent::OnPlayerStepCompleted()
{
    // Player finished one tile step — queue a matching step for the follower.
    // HandleFollowPlayerPath will consume this flag as soon as CanMove() is true.
    bPendingFollowerStep = true;
}

void UGF_GridNPCAIComponent::CheckIfBlockingPlayer()
{
    AActor* Player = GetPlayerCharacter();
    if (!Player || !MovementComponent) return;

    UGF_GridMovementComponent* PlayerMovement = Player->FindComponentByClass<UGF_GridMovementComponent>();
    if (!PlayerMovement) return;

    // Is player trying to move but stuck?
    if (PlayerMovement->IsMoving()) return;  // Player is moving fine

    FGF_GridCoordinate PlayerPos = PlayerMovement->GetCurrentGridPosition();
    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();

    // Is player adjacent to me?
    int32 Distance = PlayerPos.ManhattanDistance(MyPos);
    if (Distance != 1) return;  // Not adjacent

    // Check if player is facing me
    EGF_PlayerDirection PlayerFacing = PlayerMovement->GetCurrentFacing();
    FGF_GridCoordinate PlayerTargetTile = PlayerPos + GridSubsystem->GetDirectionOffset(PlayerFacing);

    if (PlayerTargetTile == MyPos)
    {
        // Player is facing me - I might be blocking!
        MoveOutOfTheWay(PlayerPos, PlayerFacing);
    }
}

void UGF_GridNPCAIComponent::MoveOutOfTheWay(FGF_GridCoordinate PlayerPos, EGF_PlayerDirection PlayerFacing)
{
    if (!MovementComponent || !GridSubsystem) return;

    FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();

    // Try to find a side tile to move to
    TArray<EGF_PlayerDirection> SideDirections;

    // Get perpendicular directions
    if (PlayerFacing == EGF_PlayerDirection::North || PlayerFacing == EGF_PlayerDirection::South)
    {
        SideDirections.Add(EGF_PlayerDirection::East);
        SideDirections.Add(EGF_PlayerDirection::West);
    }
    else
    {
        SideDirections.Add(EGF_PlayerDirection::North);
        SideDirections.Add(EGF_PlayerDirection::South);
    }

    // Try to move to a side tile
    for (EGF_PlayerDirection SideDir : SideDirections)
    {
        FGF_GridCoordinate SideTile = MyPos + GridSubsystem->GetDirectionOffset(SideDir);

        if (GridSubsystem->CanMoveToTile(MyPos, SideTile, false))
        {
            MovementComponent->ExecuteSkill(SideTile, SideDir);
            return;
        }
    }

    // No side tiles available — step straight back, AWAY from the player, while keeping
    // our gaze on them (don't turn to face the way we're moving).
    //
    // The player advances in PlayerFacing and stands one tile behind us (MyPos = PlayerPos
    // + offset(PlayerFacing)), so:
    //   - facing the player    = opposite(PlayerFacing)
    //   - the tile away from them = MyPos + offset(PlayerFacing)
    const EGF_PlayerDirection FacePlayerDir = GridSubsystem->GetOppositeDirection(PlayerFacing);
    const FGF_GridCoordinate  BackwardTile  = MyPos + GridSubsystem->GetDirectionOffset(PlayerFacing);

    if (GridSubsystem->CanMoveToTile(MyPos, BackwardTile, false))
    {
        // Look at the player, then slide one tile back without turning. StepBackKeepFacing
        // moves opposite our facing, which is exactly BackwardTile.
        MovementComponent->SetFacing(FacePlayerDir);
        MovementComponent->StepBackKeepFacing();
    }
    else
    {
        TeleportBehindPlayer();
    }
}

void UGF_GridNPCAIComponent::TeleportBehindPlayer()
{
    AActor* Player = GetPlayerCharacter();
    if (!Player || !MovementComponent || !GridSubsystem) return;

    UGF_GridMovementComponent* PlayerMovement = Player->FindComponentByClass<UGF_GridMovementComponent>();
    if (!PlayerMovement) return;

    // Tile registration is only ever keyed at Z=0, and a miss reads as "walkable and empty",
    // so flatten before any tile query or the checks below silently pass on every map whose
    // ground is not at world zero.
    FGF_GridCoordinate PlayerPos = PlayerMovement->GetCurrentGridPosition();
    PlayerPos.Z = 0;
    const EGF_PlayerDirection PlayerFacing = PlayerMovement->GetCurrentFacing();

    // This is the path that puts the Creature on the ground when you let it out: it spawns far
    // away, the distance check fires immediately, and it lands here. It used to teleport onto
    // the tile behind the player unconditionally - straight on top of whoever was standing
    // there, which knocked that NPC off its tile and left it pushable.
    //
    // Free our own tiles first so the tile we currently hold can't count as an obstacle.
    GridSubsystem->ClearActorFromAllTiles(GetOwner());

    // Behind the player first, then either side, then in front.
    const bool bFacingVertical = (PlayerFacing == EGF_PlayerDirection::North || PlayerFacing == EGF_PlayerDirection::South);
    TArray<EGF_PlayerDirection> DirectionsToTry;
    DirectionsToTry.Add(UGF_GridWorldSubsystem::GetOppositeDirection(PlayerFacing));
    DirectionsToTry.Add(bFacingVertical ? EGF_PlayerDirection::East : EGF_PlayerDirection::North);
    DirectionsToTry.Add(bFacingVertical ? EGF_PlayerDirection::West : EGF_PlayerDirection::South);
    DirectionsToTry.Add(PlayerFacing);

    const bool bPlayerNeedsSurf = GridSubsystem->GetTileProperties(PlayerPos).bRequiresSurf;

    for (EGF_PlayerDirection Dir : DirectionsToTry)
    {
        const FGF_GridCoordinate Candidate = PlayerPos + UGF_GridWorldSubsystem::GetDirectionOffset(Dir);

        if (GridSubsystem->GetTileProperties(Candidate).bRequiresSurf != bPlayerNeedsSurf)
            continue;

        if (!GridSubsystem->IsTileFreeToStandOn(Candidate, Player, GetOwner()))
            continue;

        UE_LOG(LogTemp, Warning, TEXT("TeleportBehindPlayer: player tile (%d,%d) facing %d -> free tile (%d,%d)"),
            PlayerPos.X, PlayerPos.Y, (int32)PlayerFacing, Candidate.X, Candidate.Y);
        MovementComponent->TeleportToGrid(Candidate);
        return;
    }

    // Nothing free around the player. Stand ON the player rather than on a neighbour: the
    // player is grid-driven and re-asserts its own position every step, so it cannot be
    // knocked off-grid the way an NPC can, and the follower steps out on the next move.
    UE_LOG(LogTemp, Warning, TEXT("TeleportBehindPlayer: no free tile around player tile (%d,%d) - stacking on the player."),
        PlayerPos.X, PlayerPos.Y);
    MovementComponent->TeleportToGrid(PlayerPos);
}

void UGF_GridNPCAIComponent::NPCMoveBehindPlayer(bool bRunToTarget, bool bFacePlayerAfter)
{
    // Self-initialize so this works even on a spawn-frame call (same as NPCMoveTo).
    if (!TryInitialize())
    {
        UE_LOG(LogTemp, Error, TEXT("NPCMoveBehindPlayer: Missing components!"));
        OnCutsceneMovementFailed.Broadcast();
        return;
    }

    AActor* Player = GetPlayerCharacter();
    UGF_GridMovementComponent* PlayerMovement = Player ? Player->FindComponentByClass<UGF_GridMovementComponent>() : nullptr;
    if (!PlayerMovement)
    {
        UE_LOG(LogTemp, Warning, TEXT("NPCMoveBehindPlayer [%s]: No player movement component found."),
            *GetOwner()->GetName());
        OnCutsceneMovementFailed.Broadcast();
        return;
    }

    const FGF_GridCoordinate PlayerPos     = PlayerMovement->GetCurrentGridPosition();
    const EGF_PlayerDirection PlayerFacing = PlayerMovement->GetCurrentFacing();
    const FGF_GridCoordinate FacingOffset  = GridSubsystem->GetDirectionOffset(PlayerFacing);

    // Candidate tiles around the player, best-to-worst for hiding "behind":
    //   1. Directly behind (opposite the player's facing) — the ideal spot.
    //   2/3. The two sides (perpendicular to facing) — used when behind is walled off.
    //   4. In front of the player — last resort so the cutscene never hangs.
    // Sides are perpendicular to the facing axis.
    FGF_GridCoordinate SideA, SideB;
    if (FacingOffset.X != 0)   // facing along the forward/back axis -> sides are left/right (Y)
    {
        SideA = FGF_GridCoordinate(0,  1, 0);
        SideB = FGF_GridCoordinate(0, -1, 0);
    }
    else                        // facing along the left/right axis -> sides are up/down (X)
    {
        SideA = FGF_GridCoordinate( 1, 0, 0);
        SideB = FGF_GridCoordinate(-1, 0, 0);
    }

    TArray<FGF_GridCoordinate> Candidates;
    Candidates.Add(FGF_GridCoordinate(PlayerPos.X - FacingOffset.X, PlayerPos.Y - FacingOffset.Y, 0)); // behind
    Candidates.Add(FGF_GridCoordinate(PlayerPos.X + SideA.X,        PlayerPos.Y + SideA.Y,        0)); // side
    Candidates.Add(FGF_GridCoordinate(PlayerPos.X + SideB.X,        PlayerPos.Y + SideB.Y,        0)); // side
    Candidates.Add(FGF_GridCoordinate(PlayerPos.X + FacingOffset.X, PlayerPos.Y + FacingOffset.Y, 0)); // front

    const FGF_GridCoordinate MyPos = MovementComponent->GetCurrentGridPosition();

    // Mirror NPCMoveTo's two-pass pathfinding so our reachability test matches what it will actually do.
    auto IsReachable = [&](const FGF_GridCoordinate& Tile) -> bool
    {
        return GridSubsystem->FindPath(MyPos, Tile, false, false).Num() > 0
            || GridSubsystem->FindPath(MyPos, Tile, true,  false).Num() > 0;
    };

    // Pick the first candidate that's walkable, unoccupied, AND actually reachable.
    // (A tile can be open yet pathfind-blocked behind a wall — that's what made the NPC
    // walk into the wall and hang.)
    FGF_GridCoordinate ChosenTile = MyPos;
    bool bFound = false;
    for (const FGF_GridCoordinate& Tile : Candidates)
    {
        if (Tile.X == MyPos.X && Tile.Y == MyPos.Y)   // already standing on a good tile
        {
            ChosenTile = Tile;
            bFound = true;
            break;
        }

        if (!GridSubsystem->IsTileWalkable(Tile) || GridSubsystem->IsTileOccupied(Tile))
            continue;

        if (IsReachable(Tile))
        {
            ChosenTile = Tile;
            bFound = true;
            break;
        }
    }

    if (!bFound)
    {
        // Fully boxed in — stay put but still complete so the dialogue can resume.
        UE_LOG(LogTemp, Verbose, TEXT("NPCMoveBehindPlayer [%s]: No reachable tile around player - staying put."),
            *GetOwner()->GetName());
        NPCMoveTo(MyPos, bRunToTarget, bFacePlayerAfter);
        return;
    }

    NPCMoveTo(ChosenTile, bRunToTarget, bFacePlayerAfter);
}

EGF_PlayerDirection UGF_GridNPCAIComponent::GetDirectionFromDiff(const FGF_GridCoordinate& Diff) const
{
    // Prioritize X/Y axis with larger difference
    if (FMath::Abs(Diff.X) > FMath::Abs(Diff.Y))
    {
        return (Diff.X > 0) ? EGF_PlayerDirection::South : EGF_PlayerDirection::North;
    }
    else
    {
        return (Diff.Y > 0) ? EGF_PlayerDirection::West : EGF_PlayerDirection::East;
    }
}