#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GF_GridWorldSubsystem.h"
#include "GF_GridMovementComponent.h"
#include "GF_GridNPCAIComponent.generated.h"

//====================================================================================
// NPC BEHAVIOR ENUM
//====================================================================================

UENUM(BlueprintType)
enum class EGF_NPCBehavior : uint8
{
    Idle,               // Stays in place
    Patrol,             // Patrols between waypoints
    LookAround,         // Rotates to look in different directions
    ChasePlayer,        // Actively pursues player when in range
    FleeFromPlayer,     // Runs away from player
    FollowPlayerPath    // Follow player's exact path (follower Creature)
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnNPCMovementComplete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnNPCReachedPosition, FGF_GridCoordinate, Position);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnCutsceneMovementFailed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnPlayerDetectedDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnCutsceneMovementCompleteDelegate);
// Fires each time the NPC completes one tile step, passing the tile it just LEFT.
// Bind this on the player's GridMovementComponent to drive the train-follow system.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnNPCStepTaken, FGF_GridCoordinate, PreviousPosition);
// Fires when PushPlayerBack finishes all steps (or is aborted early).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnPlayerPushedFinished);

//====================================================================================
// NPC AI COMPONENT
//====================================================================================

/**
 * AI Component for grid-based NPCs
 * Handles pathfinding, patrolling, player detection, and interactions
 * Attach to any grid-based NPC
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMMAFRAMEWORKWORLD_API UGF_GridNPCAIComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGF_GridNPCAIComponent();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    //====================================================================================
    // NPC BEHAVIOR SETTINGS
    //====================================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Behavior")
    EGF_NPCBehavior Behavior = EGF_NPCBehavior::Idle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Behavior")
    bool bCanMove = true;

    // How long to wait between moves (in seconds)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Behavior", meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float MoveCooldown = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Behavior", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float FleeCooldown = 0.3f;

    // ✅ NEW: Can idle NPCs look around randomly?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Behavior")
    bool bIdleCanLookAround = false;

    /**
     * Put this NPC back on the tile it was placed on when its streamed sublevel is made
     * visible again (walking back into a building/route).
     *
     * Making a sublevel invisible routes EndPlay but does NOT destroy the actor: it freezes
     * part-way through a step and keeps every bit of C++ state. Without this reset, a route
     * that is expressed relative to "where I am now" (NPCMoveRelative chains, patrol offsets,
     * StartCircleLoop) re-anchors to the tile the NPC froze on, so the whole path walks a
     * little further from its origin on every single entry and re-entry.
     *
     * Only applied when the NPC actually had a route running when the level was hidden
     * (patrol waypoints, a circle loop, or a cutscene move in flight), so an NPC that a
     * story event permanently relocated is left where it stands. Turn this off for an NPC
     * whose position must be driven entirely by Blueprint.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Behavior")
    bool bResetToStartOnLevelReentry = true;

    //====================================================================================
    // PATROL SETTINGS
    //====================================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Patrol")
    TArray<FGF_GridCoordinate> PatrolWaypoints;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Patrol")
    bool bLoopPatrol = true;

    /**
     * When true, this NPC's patrol pathfinding ignores the open-space (wall-proximity)
     * penalty, so the route may hug walls and blocking tiles directly. Use this for tight
     * loops around furniture (e.g. circling a table that must stay grid-blocking).
     * When false (default), patrol prefers open space and bows away from walls/blockers.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Patrol")
    bool bHugWalls = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Patrol")
    int32 CurrentWaypointIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Patrol")
    bool bWaitAtWaypoints = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Patrol", meta = (EditCondition = "bWaitAtWaypoints", ClampMin = "0.5", ClampMax = "10.0"))
    float WaypointWaitTime = 2.0f;

    //====================================================================================
    // PLAYER DETECTION
    //====================================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Detection")
    float DetectionRange = 5.0f;  // In tiles

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Detection")
    bool bRequireLineOfSight = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Detection")
    bool bCanSeePlayer = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Detection")
    bool bReturnToStartAfterChase = true;



    //====================================================================================
    // TAMER SETTINGS (for tamer NPCs)
    //====================================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Tamer")
    bool bIsTamer = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Tamer")
    EGF_PlayerDirection VisionDirection = EGF_PlayerDirection::South;

    EGF_PlayerDirection GetDirectionBetweenCoords(FGF_GridCoordinate From, FGF_GridCoordinate To);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Tamer")
    int32 VisionRange = 4;  // How many tiles tamer can see

    /** Max world-height difference (in tiles) at which the tamer can still see the
     *  player. 1 keeps slight slopes visible while blocking bridges/ledges above or
     *  below that the NPC can't actually reach. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Tamer")
    float MaxVisionHeightDifference = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Tamer")
    bool bHasBeatenTamer = false;  // Has player defeated this tamer?

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Tamer")
    bool bTamerWalkToPlayer = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Tamer")
    bool bFacePlayerOnCutsceneComplete = true;

    //====================================================================================
    // FOLLOWER CREATURE SETTINGS
    //====================================================================================

    /** Player movement component to follow (for follower Creature) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Follower")
    UGF_GridMovementComponent* PlayerToFollow = nullptr;

    /** How many tiles behind player to stay */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Follower")
    int32 FollowDistance = 1;

    /** Auto-move when blocking player's path? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Follower")
    bool bAutoMoveWhenBlocking = true;

    /** Distance to teleport behind player if too far */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Follower")
    float TeleportThreshold = 3.0f;

    /**
     * Called when the player completes a tile step.
     * Sets bPendingFollowerStep so the follower takes its matching step next tick.
     * This is what creates the "train" effect — one follower step per player step,
     * at the same speed, with no tick-based lag or rapid-fire.
     */
    UFUNCTION()
    void OnPlayerStepCompleted();

    //====================================================================================
    // PUBLIC API
    //====================================================================================

    /**
     * Set the NPC's behavior
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void SetBehavior(EGF_NPCBehavior NewBehavior);

    /**
     * Add a waypoint to patrol route
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void AddPatrolWaypoint(FGF_GridCoordinate Waypoint);

    /**
     * Clear all patrol waypoints
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void ClearPatrolWaypoints();

    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void SetPatrolWaypoints(const TArray<FGF_GridCoordinate>& Waypoints);

    /**
     * Make the NPC walk a fixed square loop forever by stepping tile-by-tile — no
     * pathfinding, no wall penalty, no rerouting. Use this to circle furniture (e.g. a
     * table) that must stay grid-blocking: the NPC steps directly and never detours.
     *
     * The loop is StepsPerSide tiles on each of the four sides and always returns to the
     * start tile. With the defaults it walks the same shape as patrol points
     * North x3 -> East x3 -> South x3 -> West x3.
     *
     * Optionally pass a FollowerNPC (e.g. a pet's GridNPCAIComponent) and it will trail
     * the leader along a breadcrumb trail of the exact vacated tiles — so it rounds the
     * corners cleanly instead of cutting across the table. It holds TilesBehind tiles of
     * gap. Place the follower TilesBehind tiles directly behind the leader along the first
     * leg before calling this.
     *
     * Pause/Resume model (for talking to the leader mid-loop):
     *   - PauseCircleLoop() stops the leader stepping; the follower keeps walking until it
     *     is right behind the leader (1 tile), then waits.
     *   - ResumeCircleLoop() restarts the leader; the follower waits until the gap has
     *     reopened to TilesBehind before it starts trailing again.
     *
     * @param FollowerNPC     Follower's AI component to trail the leader, or null for none.
     * @param FirstDirection  Direction of the first leg (orient the loop to your layout).
     * @param StepsPerSide    Tiles walked per side before turning (default 3 = a 3x3 loop).
     * @param bClockwise      true turns right each corner; false turns left.
     * @param TilesBehind     Steady-state gap the follower keeps (default 2).
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Patrol")
    void StartCircleLoop(UGF_GridNPCAIComponent* FollowerNPC = nullptr, EGF_PlayerDirection FirstDirection = EGF_PlayerDirection::North, int32 StepsPerSide = 3, bool bClockwise = true, int32 TilesBehind = 2);

    /** Stop the circle loop started by StartCircleLoop and leave the NPC (and follower) idle. */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Patrol")
    void StopCircleLoop();

    /** Freeze the loop leader (e.g. to talk to the player). Call ResumeCircleLoop() to
     *  restart.
     *  @param bHoldFollowerInPlace  false (default): the follower closes up to right behind
     *         the leader and waits — use when talking to the LEADER.
     *         true: the follower also stops where it is at the next tile boundary — use when
     *         talking to the FOLLOWER. This is tile-safe: never lock the follower's
     *         MovementComponent directly (locking mid-step strands it between tiles). */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Patrol")
    void PauseCircleLoop(bool bHoldFollowerInPlace = false);

    /** Resume the loop after PauseCircleLoop(). The follower reopens the gap to TilesBehind
     *  before it starts trailing again. */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Patrol")
    void ResumeCircleLoop();

    /**
     * Get the player character
     */
    UFUNCTION(BlueprintPure, Category = "NPC AI")
    AActor* GetPlayerCharacter() const;

    /**
     * Get distance to player (in tiles)
     */
    UFUNCTION(BlueprintPure, Category = "NPC AI")
    float GetDistanceToPlayer() const;

    /**
     * Check if player is in line of sight
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    bool IsPlayerInLineOfSight();

    /**
     * Check if player is in tamer's vision cone
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    bool IsPlayerInVisionCone();

    /**
     * Skill towards the player (pathfinding)
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void MoveTowardsPlayer();

    /**
     * Skill away from the player
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void MoveAwayFromPlayer();

    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void MoveToCoordinate(FGF_GridCoordinate TargetCoord);
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void StopMoving();

    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void ResumeMoving();

    /**
     * Start conversation/battle with player
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void InteractWithPlayer();

    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void FaceTowardsCoordinate(FGF_GridCoordinate TargetCoord);

    /**
     * Clears this NPC's tile registration from the grid.
     * Call this BEFORE hiding, destroying, or teleporting the NPC via non-grid means
     * (e.g. before playing a disappear animation or SetActorHiddenInGame).
     * Without this the NPC's last tile stays permanently "occupied" until level reload.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void ClearTileRegistration();

    //====================================================================================
    // CUTSCENE MOVEMENT (for scripted events)
    //====================================================================================

    /**
     * Skill NPC to a specific grid coordinate (for cutscenes)
     * Fires OnCutsceneMovementComplete when finished
     * @param TargetCoord - Grid coordinate to move to
     * @param bRunToTarget - Should NPC run instead of walk?
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void NPCMoveTo(FGF_GridCoordinate TargetCoord, bool bRunToTarget = false, bool bFacePlayerAfter = true, bool bPreferOpenSpace = true);

    /**
     * Skill NPC to a world location (for cutscenes)
     * Automatically converts to grid coordinate
     * @param TargetLocation - World space location to move to
     * @param bRunToTarget - Should NPC run instead of walk?
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void NPCMoveToLocation(FVector TargetLocation, bool bRunToTarget = false);

    /**
     * Skill NPC to the player's position (for cutscenes)
     * @param bStopAdjacentToPlayer  - Stop one tile in front of the player (in the NPC's approach direction)
     * @param bRunToTarget           - Should NPC run instead of walk?
     * @param bStopOnSideOfPlayer    - Stop on the East or West tile beside the player (East checked first).
     *                                 Useful for cutscenes where you want both characters visible side-by-side
     *                                 without the NPC being hidden behind the player sprite.
     *                                 Takes priority over bStopAdjacentToPlayer when true.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void NPCMoveToPlayer(bool bStopAdjacentToPlayer = true, bool bRunToTarget = false, bool bStopOnSideOfPlayer = false);

    /**
     * Skill the NPC to the tile directly BEHIND the player (opposite the player's facing),
     * so the player ends up between this NPC and whatever it's facing. For "hide behind me"
     * cutscenes. Facing-aware via GetDirectionOffset, so it works whichever way the player is turned.
     * @param bRunToTarget      - Should the NPC run instead of walk?
     * @param bFacePlayerAfter  - Turn to face the player once arrived (default true).
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void NPCMoveBehindPlayer(bool bRunToTarget = true, bool bFacePlayerAfter = true);

    UFUNCTION(BlueprintCallable, Category="NPC AI|Cutscene")
    void NPCMoveRelative(FGF_GridCoordinate RelativeOffset, bool bRunToTarget = false);

    /** Returns the cardinal direction that best matches a grid offset.
     *  Use this to set VisionDirection from a patrol point offset BEFORE calling
     *  NPCMoveRelative — GetCurrentFacing won't update until the move actually starts. */
    UFUNCTION(BlueprintPure, Category="NPC AI|Utility")
    EGF_PlayerDirection GetDirectionFromOffset(FGF_GridCoordinate Offset) const;

    /** Immediately cancels any in-progress NPCMoveTo / NPCMoveRelative.
     *  The NPC stops on its current tile — no OnCutsceneMovementComplete is fired.
     *  Call this from OnPlayerDetected to interrupt a patrol step before walking
     *  into the player's approach logic. */
    UFUNCTION(BlueprintCallable, Category="NPC AI|Cutscene")
    void CancelCurrentMovement();

    /**
     * Makes this NPC follow another NPC step-for-step, always one tile behind.
     * The follower mirrors every step the target takes automatically.
     * Call StopFollowingNPCActor() to end the follow.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void StartFollowingNPCActor(UGF_GridNPCAIComponent* TargetNPC, bool bRun = false);

    /** Stops NPC-to-NPC following and returns this NPC to idle. */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void StopFollowingNPCActor();



    /**
     * Check if NPC is currently executing a cutscene movement
     */
    UFUNCTION(BlueprintPure, Category = "NPC AI|Cutscene")
    bool IsInCutsceneMovement() const { return bIsInCutsceneMovement; }

    /**
     * Cancel current cutscene movement
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void CancelCutsceneMovement();

    /**
     * Push the player back AND have this NPC walk forward in the same direction —
     * one Blueprint call does everything.
     *
     * The NPC steps simultaneously with the player so it visually "moves" the player.
     *
     * @param PlayerMovement    The player's GridMovementComponent
     * @param PushDirection     Direction both the player retreats and the NPC advances
     * @param NumTiles          How many tiles the player is pushed (NPC matches)
     * @param bFacePushDirection  Passed through to BeginPushedBack:
     *                            false = player faces the NPC while sliding (default, looks natural)
     *                            true  = player faces the push direction (bouncer style)
     * @param SpeedMultiplier   Speeds up the player's push (1.0 = normal, 2.0 = twice as fast).
     *                          Use this to match a running NPC so the player keeps pace.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void PushPlayerBack(UGF_GridMovementComponent* PlayerMovement,
                        EGF_PlayerDirection PushDirection,
                        int32 NumTiles = 1,
                        bool bFacePushDirection = false,
                        float SpeedMultiplier = 1.0f);

    /**
     * One-call "shove past and flee" for an NPC escaping past the player (e.g. a defeated
     * grunt fleeing down a tunnel). Shoves the player aside to clear the lane, then runs
     * this NPC along a full pathfound route to OffScreenTile — both kicked off together so
     * they move at the same time.
     *
     * Ordering is handled internally: the push runs first, which frees the player's tile
     * immediately (ExecuteSkill unregisters at move start), so the NPC's pathfind can route
     * through the vacated lane on the same frame.
     *
     * Bind OnCutsceneMovementComplete to hide/despawn the NPC once it reaches OffScreenTile.
     *
     * @param OffScreenTile        Tile (off camera) the NPC runs to.
     * @param PlayerPushDirection  Direction to shove the player so the lane clears (e.g. North).
     * @param PushTiles            How many tiles to shove the player (usually 1). 0 = don't push.
     * @param PushSpeedMultiplier  Speeds up the player's shove to match the running NPC (2.0 = 2x).
     */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void FleePastPlayer(FGF_GridCoordinate OffScreenTile,
                        EGF_PlayerDirection PlayerPushDirection,
                        int32 PushTiles = 1,
                        float PushSpeedMultiplier = 2.0f);

    // Internal — exposed only so BeginPushFollow can still be called standalone if needed.
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void BeginPushFollow(EGF_PlayerDirection FollowDirection, int32 NumTiles = 1);

    //====================================================================================
    // EVENTS (Blueprint implementable)
    //====================================================================================

    UPROPERTY(BlueprintAssignable, Category = "NPC AI|Events")
    FGF_OnPlayerDetectedDelegate OnPlayerDetected;

    UFUNCTION(BlueprintImplementableEvent, Category = "NPC AI|Events")
    void OnPlayerLost();

    UFUNCTION(BlueprintImplementableEvent, Category = "NPC AI|Events")
    void OnReachedWaypoint(int32 WaypointIndex);

    UFUNCTION(BlueprintImplementableEvent, Category = "NPC AI|Events")
    void OnInteractWithPlayer();

    /**
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "NPC AI|Events")
    void OnPathCompleted();

    UPROPERTY(BlueprintAssignable, Category = "NPC AI|Events")
    FGF_OnCutsceneMovementCompleteDelegate OnCutsceneMovementComplete;

    /** Fires each tile step with the position the NPC just LEFT.
     *  Enable via EnablePlayerFollowMode() before the NPC starts moving. */
    UPROPERTY(BlueprintAssignable, Category = "NPC AI|Events")
    FGF_OnNPCStepTaken OnNPCStepTaken;

    /** Allow the player's GridMovementComponent to trail this NPC step-for-step.
     *  Call this before NPCMoveTo so no steps are missed. */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void EnablePlayerFollowMode();

    /** Stop emitting per-step events (called automatically by StopFollowingNPC). */
    UFUNCTION(BlueprintCallable, Category = "NPC AI|Cutscene")
    void DisablePlayerFollowMode();

    /**
     * Called when cutscene movement fails (no path found)
     */
    UPROPERTY(BlueprintAssignable, Category="NPC AI|Events")
    FGF_OnCutsceneMovementFailed OnCutsceneMovementFailed;

    /** Fires when NPC stops moving (any reason) */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI|Events")
	FGF_OnNPCMovementComplete OnMovementStopped;

	/** Fires when NPC reaches a specific position */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI|Events")
	FGF_OnNPCReachedPosition OnPositionReached;

	/** Fires when PushPlayerBack finishes all push-follow steps (or aborts early due to a wall).
	 *  Use this instead of OnMovementStopped / OnCutsceneMovementComplete to avoid
	 *  responding to unrelated movement events that fire at the same time. */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI|Events")
	FGF_OnPlayerPushedFinished OnPlayerPushedFinished;

    /** Called by SwapPositionsWithActor to suppress follow-movement for one animation cycle. */
    void NotifyPositionSwapped() { bJustSwapped = true; }

protected:
    //====================================================================================
    // SIMPLIFIED BEHAVIOR HANDLERS (for MINIMAL version)
    //====================================================================================

    void HandleCutsceneMovement();
    void HandlePatrolBehavior();
    void HandleLookAroundBehavior();
    void HandleChaseBehavior();
    void HandleFleeBehavior();
    void CheckTamerVision();
    void ExecuteSinglePathStep();

    // Follower Creature handlers
    void HandleFollowPlayerPath();
    void CheckIfBlockingPlayer();
    void MoveOutOfTheWay(FGF_GridCoordinate PlayerPos, EGF_PlayerDirection PlayerFacing);
    void TeleportBehindPlayer();
    EGF_PlayerDirection GetDirectionFromDiff(const FGF_GridCoordinate& Diff) const;

    // Circle loop (StartCircleLoop). Leader and follower are BOTH driven off one precomputed
    // ring of tiles (LoopTiles) via a shared clock: leader at index N, follower at N-Behind.
    // Positions are computed, not reactive, so they can never block each other; if the PLAYER
    // blocks the next tile they simply both wait. Advancing is driven by OnStepCompleted for
    // continuity, with a per-frame safety net in TickComponent for the start / blocked retry.
    void AdvanceLoop();                                // move leader+follower one tile in lockstep
    UFUNCTION() void HandleCircleStepCompleted();      // leader finished a tile -> advance
    UFUNCTION() void HandleFollowerStepCompleted();    // follower finished a tile -> advance
    static EGF_PlayerDirection RotateDirection(EGF_PlayerDirection Dir, bool bClockwise);

    bool bRunningCircleLoop = false;
    bool bCircleLeaderPaused = false;
    bool bFollowerHeld = false;   // kept for API compat; pause stops both now

    UPROPERTY()
    UGF_GridNPCAIComponent* CircleFollower = nullptr;   // trails the loop, FollowerTilesBehind back

    TArray<FGF_GridCoordinate> LoopTiles;   // the ring of absolute tiles, P[0..L-1]
    int32 LoopClock = 0;                 // leader is at LoopTiles[LoopClock % L]
    int32 FollowerClock = 0;             // follower is at LoopTiles[FollowerClock % L]; trails LoopClock
    int32 FollowerTilesBehind = 2;

    // The tile the ring was FIRST built from, plus the shape it was built with. The ring has
    // to stay pinned to this tile: StartCircleLoop is called again every time the sublevel is
    // made visible, and by then the leader is frozen part-way round the loop — rebuilding
    // from its current tile would slide the whole ring by that many tiles, every re-entry.
    FGF_GridCoordinate  CircleLoopAnchor;
    bool             bHasCircleLoopAnchor = false;
    EGF_PlayerDirection CircleFirstDirection  = EGF_PlayerDirection::North;
    int32            CircleStepsPerSide    = 3;
    bool             bCircleClockwise      = true;

    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void AddRelativePatrolWaypoint(FGF_GridCoordinate RelativeOffset);
    UFUNCTION(BlueprintCallable, Category = "NPC AI")
    void SetRelativePatrolRoute(const TArray<FGF_GridCoordinate>& RelativeOffsets);


    //====================================================================================
    // CACHED REFERENCES
    //====================================================================================

    UPROPERTY()
    UGF_GridWorldSubsystem* GridSubsystem;

    UPROPERTY()
    UGF_GridMovementComponent* MovementComponent;

    //====================================================================================
    // INTERNAL STATE
    //====================================================================================

    bool bInitialized = false;
    bool TryInitialize();

    // Set in BeginPlay when bInitialized is already true — i.e. this is the sublevel being
    // made visible again, not the first load. Consumed on the next Tick (not in BeginPlay
    // itself: the level's collision is not necessarily registered yet, and TeleportToGrid
    // floor-traces).
    bool bPendingLevelReentryReset = false;

    // Whether this NPC had a route in flight when the level was hidden. Gates the position
    // reset so story-relocated idle NPCs are never yanked back to their placement tile.
    bool bWasRoutedWhenHidden = false;

    // Set by NPCMoveRelative. A relative move is by definition a leg of a route ("three tiles
    // north of wherever I am"), which is the shape that drifts: a Blueprint chain that walks
    // an L or a box re-anchors to the tile the NPC froze on. An absolute NPCMoveTo /
    // NPCMoveToPlayer is a story destination and does not set this, so a cutscene that
    // permanently relocates an NPC is left alone on re-entry.
    bool bUsesRelativeRoute = false;

    /** Undo the half-finished step / drifted anchor left behind by a sublevel visibility cycle. */
    void HandleLevelReentryReset();

    // True once we've bound to the player's OnStepCompleted delegate
    bool bFollowerEventBound = false;

    // Set to true each time the player finishes a tile step.
    // HandleFollowPlayerPath consumes it to take one matching step.
    bool bPendingFollowerStep = false;

    float TimeSinceLastMove = 0.0f;
    float LookAroundTimer = 0.0f;
    int32 CurrentLookDirection = 0;

    TArray<FGF_GridCoordinate> CurrentPath;
    int32 CurrentPathIndex = 0;

    bool bWasPlayerInSight = false;
    bool bIsCurrentlyFleeing = false;

    // Set by SwapPositionsWithActor to block follow movement for the entire
    // duration of the swap animation — prevents the overlap case from chasing
    // the follower back onto the player immediately after the swap.
    bool bJustSwapped = false;
    EGF_PlayerDirection LastFleeDirection = EGF_PlayerDirection::South;

    FGF_GridCoordinate OriginalPosition;


    // Waypoint wait timer
    float WaypointWaitTimer = 0.0f;

    // Cutscene movement state
    bool bIsInCutsceneMovement = false;
    EGF_NPCBehavior PreviousBehavior;
    float OriginalMoveSpeed = 0.0f;

    // True when the current cutscene path was planned through occupied tiles (the
    // occupancy-ignoring FindPath fallback was used). Lets each step shove through an
    // occupant instead of abandoning the path — e.g. a grunt fleeing past the player.
    bool bCutsceneForceThroughOccupants = false;

    // Pathfinding failure prevention
    int32 ConsecutivePathfindingFailures = 0;
    const int32 MaxPathfindingAttempts = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Movement")
	float PathfindingCooldown = 0.1f;  // How often to recalculate path

	// Skill smoothly without pauses between tiles (original field — do not repurpose)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Movement")
	bool bContinuousMovement = true;

	/** When true, bKeepWalkingAnimation is never forced to false between chained NPCMoveTo steps.
	 *  Set this to true at the start of a looping run/chase cutscene, false when it ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI|Movement")
	bool bLoopAnimation = false;

    FGF_GridCoordinate StartingPosition;

    bool bShouldFacePlayerOnCutsceneComplete = true;

    // Player-follow mode: emit OnNPCStepTaken after every tile step
    bool bPlayerFollowModeEnabled = false;
    bool bFollowStepBound         = false;
    FGF_GridCoordinate StepFromPosition;   // NPC position captured just before each step

    // NPC-to-NPC follow state
    UPROPERTY()
    UGF_GridNPCAIComponent* FollowTargetNPC = nullptr;
    bool bIsFollowingNPCActor = false;
    bool bFollowTargetRun     = false;
    TArray<FGF_GridCoordinate> NPCFollowStepQueue;

    UFUNCTION()
    void HandleFollowTargetStepTaken(FGF_GridCoordinate PreviousPosition);

    UFUNCTION()
    void HandleFollowStepComplete();

    void TakeNextNPCFollowStep();

    UFUNCTION()
    void HandleOwnStepCompleted();

    // Push-follow state (NPC shadowing a player push-back sequence)
    EGF_PlayerDirection PushFollowDirection      = EGF_PlayerDirection::South;
    int32            PushFollowRemainingSteps = 0;
    bool             bPushFollowStepBound     = false;

    // Set by HandlePushFollowStepCompleted so the next NPC step is deferred to the
    // following Tick.  By then CompleteMovement has fully finished on the player's side
    // (scripted move advanced, player unregistered from the tile NPC wants to enter).
    bool             bPushFollowStepPending   = false;

    // How many consecutive ticks TakePushFollowStep has retried without success.
    // Used to abort gracefully if the NPC is genuinely blocked (wall / permanent occupant).
    int32            PushFollowRetryCount     = 0;
    static constexpr int32 MaxPushFollowRetries = 10;  // ~10 frames at 60 fps ≈ 167 ms

    // Player movement component stored during a push so we can wait for their last step
    // to finish before broadcasting OnPlayerPushedFinished.
    UPROPERTY()
    UGF_GridMovementComponent* PushFollowPlayerMovement = nullptr;

    /** Takes the next push-follow step; cleans up if blocked or all steps done. */
    void TakePushFollowStep();

    /** Fires on our own OnStepCompleted during a push-follow sequence. */
    UFUNCTION()
    void HandlePushFollowStepCompleted();

    /** Unbinds the push-follow delegate and resets animation state. */
    void FinishPushFollow();

    /** Fires on the player's OnStepCompleted after the NPC's last push step —
     *  waits for the player to fully land before broadcasting OnPlayerPushedFinished. */
    UFUNCTION()
    void HandlePlayerLastPushStepCompleted();

    private:
    // Cache paths between patrol waypoints
    TMap<int32, TArray<FGF_GridCoordinate>> CachedPatrolPaths;

    // Pre-calculate all patrol paths
    void PreCalculatePatrolPaths();

};