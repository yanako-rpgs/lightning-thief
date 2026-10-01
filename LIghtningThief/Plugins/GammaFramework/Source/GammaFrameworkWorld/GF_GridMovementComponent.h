#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GF_GridWorldSubsystem.h"
#include "PaperFlipbookComponent.h"
#include "GF_GridMovementComponent.generated.h"

class UGF_GridNPCAIComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnLedgeLanded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnFloorConfirmed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnStepCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBumpIntoWall, EGF_PlayerDirection, Direction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnScriptedMoveComplete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnNPCFollowComplete);

// FacingOnArrival = direction to pass back into CompleteEnterDoor (same facing the player had when entering)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGF_OnDoorEntered, FGF_GridCoordinate, DoorCoord, FGF_GridCoordinate, DestinationCoord, EGF_PlayerDirection, FacingOnArrival);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnDoorExited, FGF_GridCoordinate, ArrivalCoord);
// Fires the instant the player steps on the door tile, before any entry auto-steps begin.
// bOverrideDirection = whether this door forces a specific arrival direction.
// ArrivalDirection   = the facing/auto-walk direction the player will have on the other side
// (normally the entry direction, or the door's override when bOverrideDirection is true).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FGF_OnDoorEnterStarted, FGF_GridCoordinate, DoorCoord, float, StepsDuration, bool, bOverrideDirection, EGF_PlayerDirection, ArrivalDirection);

//====================================================================================
// MOVEMENT STATE ENUM
//====================================================================================

UENUM(BlueprintType)
enum class EGF_GridMovementState : uint8
{
    Idle,           // Standing still, can accept input
    Moving,         // Moving between tiles
    Interacting,    // Talking to NPC, opening door, etc.
    Locked          // Cannot move (cutscene, battle transition)
};

//====================================================================================
// GRID MOVEMENT COMPONENT
//====================================================================================

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMMAFRAMEWORKWORLD_API UGF_GridMovementComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGF_GridMovementComponent();

protected:
    virtual void BeginPlay() override;

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    //====================================================================================
    // MOVEMENT STATE
    //====================================================================================

    UPROPERTY(BlueprintReadOnly, Category = "Grid Movement")
    FGF_GridCoordinate CurrentGridPosition;

    UPROPERTY(BlueprintReadOnly, Category = "Grid Movement")
    FGF_GridCoordinate TargetGridPosition;

    UPROPERTY(BlueprintReadOnly, Category = "Grid Movement")
    EGF_GridMovementState MovementState = EGF_GridMovementState::Idle;

    UPROPERTY(BlueprintReadOnly, Category = "Grid Movement")
    EGF_PlayerDirection CurrentFacing = EGF_PlayerDirection::South;

    //====================================================================================
    // MOVEMENT SETTINGS
    //====================================================================================

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement")
    float MovementSpeed = 300.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Grid Movement")
    float MovementAlpha = 0.0f;

    FVector StartWorldPosition;
    FVector TargetWorldPosition;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement")
	bool bAutoDetectGroundZ = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement")
	float ActorHeightAboveGround = 88.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement")
	float GroundTraceDistance = 300.0f;

	// Maximum height (world units) ABOVE the actor's current floor that the downward
	// floor trace is allowed to START. The trace returns the first (highest) WorldStatic
	// hit, so if it starts high above the actor's head it grabs the top of overhead
	// geometry — a bridge, overpass, or raised walkway — and treats it as the floor,
	// walking the actor up onto it like a giant ramp. Capping the start to just above the
	// feet lets any geometry that clears the head be ignored (walk UNDER it) while a
	// legitimate one-tile step-up onto a slope/raised tile is still detected. Must be
	// larger than the tallest legitimate one-step rise (~one TileSize) but smaller than
	// the head clearance under any walk-under geometry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement")
	float MaxStepUpHeight = 64.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

// When true, ExecuteSkill() skips ALL vertical adjustment for the duration of the move.
	// The actor stays at its current Z — no FloorDelta is applied, no sky-climb possible.
	// Set this on any scripted/cutscene movement where overhead geometry (trees, props,
	// trigger volumes) would otherwise be mistaken for the floor by the downward trace.
	// Equivalent to bIsFollowingNPC's Z-freeze, but usable by any system (push-follow, etc.).
	UPROPERTY(BlueprintReadWrite, Category = "Grid Movement")
	bool bFreezeVerticalMovement = false;

    // When a new direction is pressed while moving, queue it for the next step
	UPROPERTY(BlueprintReadWrite, Category = "Grid Movement")
	bool bHasQueuedSkill = false;

	UPROPERTY(BlueprintReadWrite, Category = "Grid Movement")
	EGF_PlayerDirection QueuedMoveDirection = EGF_PlayerDirection::South;

    //====================================================================================
    // ABILITIES
    //====================================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement|Traits")
    bool bHasSurf = false;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement|Traits")
    bool bHasRockSmash = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement|Traits")
    bool bHasCut = false;

    //====================================================================================
    // SPRITE COMPONENT
    //====================================================================================

    UPROPERTY(BlueprintReadWrite, Category = "Grid Movement|Sprites")
    UPaperFlipbookComponent* SpriteComponent;

    /** Whether this component drives the owner's PaperFlipbookComponent itself.
     *
     *  Set to FALSE on any actor whose animation is owned by something else -- notably
     *  PaperZD on the player pawn. This component only has Idle and Walk flipbooks and no run
     *  animation, so when both are active they fight over the same sprite every frame and
     *  the run cycle restarts (walking looks fine because both pick the same flipbook).
     *  When false, SpriteComponent is never acquired and all animation calls here no-op. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement|Animation")
    bool bDriveSpriteAnimation = true;

    //====================================================================================
    // ANIMATIONS
    //====================================================================================

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Animations")
    UPaperFlipbook* IdleNorth;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Animations")
    UPaperFlipbook* IdleSouth;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Animations")
    UPaperFlipbook* IdleEast;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Animations")
    UPaperFlipbook* IdleWest;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Animations")
    UPaperFlipbook* WalkNorth;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Animations")
    UPaperFlipbook* WalkSouth;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Animations")
    UPaperFlipbook* WalkEast;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Animations")
    UPaperFlipbook* WalkWest;

    /**
     * Replace all 8 animation flipbooks at once and immediately refresh the sprite.
     * Use this at runtime to swap between animation sets (e.g. gender variants).
     * Any parameter left null keeps the existing flipbook for that slot.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Animations")
    void SetAnimationSet(
        UPaperFlipbook* NewIdleNorth, UPaperFlipbook* NewIdleSouth,
        UPaperFlipbook* NewIdleEast,  UPaperFlipbook* NewIdleWest,
        UPaperFlipbook* NewWalkNorth, UPaperFlipbook* NewWalkSouth,
        UPaperFlipbook* NewWalkEast,  UPaperFlipbook* NewWalkWest);

    UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Events")
	FGF_OnBumpIntoWall OnBumpIntoWall;

    //====================================================================================
    // SOUND EFFECTS
    //====================================================================================

    /** Sound to play when jumping off a ledge */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Sounds")
    USoundBase* LedgeJumpSound;

    /** Sound to play on each footstep */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid Movement|Sounds")
    USoundBase* FootstepSound;

    //====================================================================================
    // PUBLIC API
    //====================================================================================

    float PendingTargetFloorZ = 0.0f;

    void ExecuteSkill(FGF_GridCoordinate TargetCoord, EGF_PlayerDirection Direction);

    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    bool TryMove(EGF_PlayerDirection Direction);

    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    bool TryInteract();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement")
    bool bKeepWalkingAnimation = false;

    /** True while any scripted movement is driving the player (follow, ScriptedMoveTo).
     *  Read this in your Paper ZD animation blueprint — when true, use CurrentFacing
     *  for directional animation instead of raw input axes. */
    UPROPERTY(BlueprintReadOnly, Category = "Grid Movement")
    bool bScriptedMovementActive = false;

    /** Continuously accumulates walk cycle time - independent of idle flipbook switches */
    float WalkAnimationTime = 0.0f;

    bool bOverrideFacingOnComplete = false;
	EGF_PlayerDirection OverrideFacingDirection = EGF_PlayerDirection::South;

    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    void LockMovement();

    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    void UnlockMovement();

    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    void TeleportToGrid(FGF_GridCoordinate Coord);

    /**
     * Frees every grid tile this actor occupies. EndPlay calls this automatically on destroy,
     * but you can also call it explicitly right before Destroy Actor as insurance (e.g. when
     * despawning a cutscene NPC) so the tile never stays occupied.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    void UnregisterFromGrid();

    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    void SetFacing(EGF_PlayerDirection Direction);

    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    void SetHasSurf(bool bNewHasSurf);

    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
	void ForceStopMovement();

    /**
     * Lock movement and wait for streaming level floor geometry to appear before
     * unlocking again.  Call this from Blueprint when entering a level-streaming
     * volume — the player will freeze in place until the sub-level floor is loaded,
     * preventing them from falling through the ground.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Streaming")
    void WaitForFloor();

    //====================================================================================
    // SCRIPTED PLAYER MOVEMENT (cutscenes)
    //====================================================================================

    /**
     * Auto-walk the player to a grid coordinate (pathfound, tile-by-tile).
     * Movement is locked while walking. Fires OnScriptedMoveComplete when done.
     * @param TargetCoord  Destination tile.
     * @param bRun         Walk at 2x speed if true.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Scripted")
    void ScriptedMoveTo(FGF_GridCoordinate TargetCoord, bool bRun = false);

    /** Cancel a scripted move in progress. Fires nothing — player stops immediately. */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Scripted")
    void CancelScriptedMove();

    /**
     * Push the player back NumTiles tiles in PushDirection (called by NPC push sequences).
     * Locks the player and scripted-moves them backward. Fires OnScriptedMoveComplete when done.
     *
     * @param PushDirection      Direction the player is pushed (usually the direction the NPC is facing).
     * @param NumTiles           How many tiles to push back.
     * @param bFacePushDirection If true  — player turns to face the push direction (bouncer style: walks away).
     *                           If false — player keeps facing TOWARD the NPC while sliding back (default push).
     * @param SpeedMultiplier    Multiplies the normal movement speed for the push only (1.0 = normal speed,
     *                           2.0 = twice as fast). Restored to normal automatically when the push finishes.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Scripted")
    void BeginPushedBack(EGF_PlayerDirection PushDirection, int32 NumTiles = 1, bool bFacePushDirection = false, float SpeedMultiplier = 1.0f);

    /**
     * Slide one tile straight backward (directly behind CurrentFacing) WITHOUT turning.
     * Facing and animation stay exactly as they are — e.g. an NPC facing West moves East
     * but keeps showing its West walk cycle, and ends on its West idle pose.
     * Smooth slide + movement lock via the scripted-move system; fires OnScriptedMoveComplete.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Scripted")
    void StepBackKeepFacing();

    /** True while any scripted movement is active (ScriptedMoveTo or NPC follow). */
    UFUNCTION(BlueprintPure, Category = "Grid Movement|Scripted")
    bool IsInScriptedMove() const { return bScriptedMovementActive; }

    /** Fires when ScriptedMoveTo finishes — bind this to start your next cutscene action. */
    UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Scripted")
    FGF_OnScriptedMoveComplete OnScriptedMoveComplete;

    //====================================================================================
    // NPC TRAIN FOLLOW (player trails an NPC step-for-step)
    //====================================================================================

    /**
     * Lock the player and start trailing the NPC one tile behind, train-style.
     * Call this BEFORE NPCMoveTo so no steps are missed.
     * Fires OnNPCFollowComplete when the NPC finishes and the player catches up.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Follow")
    void StartFollowingNPC(UGF_GridNPCAIComponent* NPC);

    /** Stop following immediately (e.g. forced cancel). Fires nothing. */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Follow")
    void StopFollowingNPC();

    /** True while the train-follow system is active. */
    UFUNCTION(BlueprintPure, Category = "Grid Movement|Follow")
    bool IsFollowingNPC() const { return bIsFollowingNPC; }

    /**
     * Return control to the player unconditionally.
     *
     * UnlockMovement() deliberately bails when bIsFollowingNPC is true, to protect an
     * active follow cutscene. That is correct for its callers, but it means a handler
     * that "unlocks" can silently leave the player frozen with no error -- and if the
     * thing that was meant to unlock never runs at all (a dialogue that never finishes,
     * a branch that goes the wrong way), the player has to quit the game.
     *
     * This is the escape hatch: it always restores movement and clears the follow flag,
     * so no code path can leave a permanent softlock. Use it in any "we are definitely
     * done, give control back" handler. GF_DialogueSubsystem already does the equivalent
     * inline; this makes the same guarantee available to Blueprint.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Follow")
    void ForceUnlockMovement();

    /** Fires when the NPC has finished moving AND the player has walked the last queued step. */
    UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Follow")
    FGF_OnNPCFollowComplete OnNPCFollowComplete;

    /** Call from Blueprint when movement input is released - switches to idle animation.
     *  This is the ONLY correct place to set idle; CompleteMovement no longer does it. */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement")
    void StopWalking();

    bool bStoppingAfterCurrentMove = false;

    /** Set by StopWalking() when input is released mid-step.
     *  CompleteMovement checks this and switches to idle when the step finishes. */
    bool bPendingIdleAfterStep = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement")
    FVector LastTileWalked;


    //====================================================================================
    // MOVEMENT HISTORY (for follower Creature)
    //====================================================================================

    /** Track recent positions for followers to retrace */
    UPROPERTY(BlueprintReadOnly, Category = "Grid Movement|History")
    TArray<FGF_GridCoordinate> MovementHistory;

    /** How many positions to remember */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement|History")
    int32 MaxHistorySize = 20;

    //====================================================================================
    // FOLLOWER SYSTEM
    //====================================================================================

    /** Is this a follower Creature? (won't block player) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement|Follower")
    bool bIsFollower = false;

    /** Swap positions with another actor (for follower system) */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Follower")
    void SwapPositionsWithActor(AActor* OtherActor, FGF_GridCoordinate OtherActorTile);

    /** Displace this actor to a specific tile (instant teleport) */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Follower")
    void DisplaceToTile(FGF_GridCoordinate NewTile);


    //====================================================================================
    // GETTERS
    //====================================================================================

    UFUNCTION(BlueprintPure, Category = "Grid Movement")
    FGF_GridCoordinate GetCurrentGridPosition() const { return CurrentGridPosition; }

    /** Returns the tile this component is currently stepping toward (equals CurrentGridPosition when idle). */
    UFUNCTION(BlueprintPure, Category = "Grid Movement")
    FGF_GridCoordinate GetTargetGridPosition() const { return TargetGridPosition; }

    UFUNCTION(BlueprintPure, Category = "Grid Movement")
    EGF_PlayerDirection GetCurrentFacing() const { return CurrentFacing; }

    UFUNCTION(BlueprintPure, Category = "Grid Movement")
    bool IsMoving() const { return MovementState == EGF_GridMovementState::Moving; }

    UFUNCTION(BlueprintPure, Category = "Grid Movement")
    bool CanMove() const { return MovementState == EGF_GridMovementState::Idle; }

    /** True while moving OR while another move is already queued -- i.e. moving continuously.
     *
     *  Prefer this over IsMoving() for ANIMATION state machines. MovementState drops back to
     *  Idle for a frame between every grid step, so a PaperZD state keyed off IsMoving()
     *  re-enters (and therefore restarts) each step -- which looks like the sprite shaking on
     *  the first couple of frames of the run cycle. It is more obvious when running because
     *  steps complete faster. bHasQueuedSkill is set while the direction is still held, so this
     *  stays true across consecutive steps and only goes false once the player actually stops.
     *
     *  NOTE: this is used by GAMEPLAY logic (the player pawn's run/input handling), so it must stay
     *  exact -- widening it by even a fraction of a second gates input and makes the player stop
     *  on every tile. For animation use IsMovingForAnimation() below. */
    UFUNCTION(BlueprintPure, Category = "Grid Movement")
    bool IsMovingContinuous() const
    {
        return MovementState == EGF_GridMovementState::Moving || bHasQueuedSkill;
    }

    /** IsMovingContinuous() plus a short grace window -- for ANIMATION STATE MACHINES ONLY.
     *
     *  Within a single frame the order is:
     *      1. this component ticks   -> step completes, MovementState = Idle, nothing queued yet
     *      2. the AnimBP ticks       -> reads Idle and enters the Idle state   <-- the gap
     *      3. Enhanced Input fires   -> TryMove() -> MovementState = Moving
     *  Step 2 sees a genuine Idle, so no transition rule can avoid it, and the run cycle restarts
     *  every step. This swallows that single-tick dip.
     *
     *  Do NOT use this to gate input or movement -- it deliberately lies for MoveContinuityGrace
     *  seconds after movement ends. */
    UFUNCTION(BlueprintPure, Category = "Grid Movement|Animation")
    bool IsMovingForAnimation() const;

    /** How long IsMovingForAnimation() keeps reporting true after movement stops.
     *  Only needs to outlast one frame; anything under ~0.15s is imperceptible when stopping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Movement|Animation")
    float MoveContinuityGrace = 0.1f;

    /** World time when MovementState was last observed as Moving. Drives MoveContinuityGrace. */
    float LastMovingTime = -1.0f;

    /** True while the player is auto-walking through a door entrance or arrival sequence.
     *  Use this alongside the Locked check to block running/other inputs during transitions. */
    UFUNCTION(BlueprintPure, Category = "Grid Movement|Doors")
    bool IsInDoorSequence() const { return bPendingDoorEntryStep || bIsProcessingDoorAutoSteps; }

    //====================================================================================
    // EVENTS (Blueprint Implementable)
    //====================================================================================

    /** Called when player steps on a tile with an item */
    UFUNCTION(BlueprintImplementableEvent, Category = "Grid Movement|Events")
    void OnItemEncountered(FName ItemID);

    /** Called when player picks up an item via interaction */
    UFUNCTION(BlueprintImplementableEvent, Category = "Grid Movement|Events")
    void OnItemPickedUp(FName ItemID);

    /** Called when wild encounter is triggered in tall grass */
    UFUNCTION(BlueprintImplementableEvent, Category = "Grid Movement|Events")
    void OnWildEncounter();

    /** Called when interacting with an actor (NPC, tamer) */
    UFUNCTION(BlueprintImplementableEvent, Category = "Grid Movement|Events")
    void OnActorInteraction(AActor* Actor);

    /** Called when interacting with a door */
    UFUNCTION(BlueprintImplementableEvent, Category = "Grid Movement|Events")
    void OnDoorInteraction(FGF_GridCoordinate DoorCoord);

    /** Called when interacting with an object */
    UFUNCTION(BlueprintImplementableEvent, Category = "Grid Movement|Events")
    void OnObjectInteraction(FGF_GridCoordinate ObjectCoord);

    /** Called when a movement step is completed */
	UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Events")
	FGF_OnStepCompleted OnStepCompleted;;

	/**
	 * How many listeners are currently bound to OnStepCompleted. Lots of systems share
	 * this delegate (egg hatching, sanctuary step counting, tamer line-of-sight, NPC
	 * following) so when one of them silently stops working, check this first.
	 */
	UFUNCTION(BlueprintPure, Category = "Grid Movement|Debug")
	int32 GetStepListenerCount() const;

	/** Logs every object + function currently bound to OnStepCompleted. */
	UFUNCTION(BlueprintCallable, Category = "Grid Movement|Debug")
	void DebugLogStepListeners() const;

	/** Called when player lands after jumping a ledge */
	UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Events")
	FGF_OnLedgeLanded OnLedgeLanded;

    //====================================================================================
    // DOOR SYSTEM
    //====================================================================================

    /**
     * Fires the instant the player steps onto a door tile, BEFORE any entry auto-steps begin.
     * Bind this to start your fade-out immediately so it plays over the walk-in animation.
     */
    UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Doors")
    FGF_OnDoorEnterStarted OnDoorEnterStarted;

    /**
     * Fired after the player auto-steps through the door tile.
     * Controls are locked automatically before this fires.
     * FacingOnArrival is the direction the player was walking — pass it straight into CompleteEnterDoor.
     * DestinationCoord comes from the linked marker on the door tile — also pass it straight in.
     */
    UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Doors")
    FGF_OnDoorEntered OnDoorEntered;

    /**
     * Fired after the player finishes all auto-steps on arrival.
     * Use this to fade back in and play arrival sounds.
     */
    UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Doors")
    FGF_OnDoorExited OnDoorExited;

    /**
     * Fired when floor geometry is confirmed under the player after a level load,
     * door transition, or WaitForFloor() call.  Bind to this instead of using a
     * fixed timer to know exactly when it's safe to unlock menus and UI input.
     */
    UPROPERTY(BlueprintAssignable, Category = "Grid Movement|Streaming")
    FGF_OnFloorConfirmed OnFloorConfirmed;

    /**
     * Call from Blueprint once your transition (fade, animation) is done.
     * Teleports the player to DestinationCoord, auto-walks them forward by AutoStepsOnEntry,
     * then fires OnDoorExited.
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Movement|Doors")
    void CompleteEnterDoor(FGF_GridCoordinate DestinationCoord, EGF_PlayerDirection FacingOnArrival = EGF_PlayerDirection::South);

protected:
    // Pending entry steps: player auto-walks into the door before the transition fires
    bool bPendingDoorEntryStep = false;
    int32 RemainingEntrySteps = 0;               // Counts down from EntryStepsBeforeTeleport
    FGF_GridCoordinate PendingDoorTileCoord;        // The door tile coord (stored for delegate data)
    FGF_GridCoordinate PendingDoorDestCoord;        // Destination from the marker
    EGF_PlayerDirection PendingDoorFacing = EGF_PlayerDirection::South;
    // Facing/auto-walk direction applied AFTER the teleport. Normally equals PendingDoorFacing
    // (player keeps their direction), but a door with bDoorOverrideStepDirection forces a
    // specific arrival direction here (e.g. enter North -> arrive walking South).
    EGF_PlayerDirection PendingDoorArrivalFacing = EGF_PlayerDirection::South;
    float PendingDoorTransitionDelay = 0.0f;     // Seconds to hold on black screen before teleport
    float PendingDoorArrivalDelay = 0.0f;        // Seconds to hold after teleport (streaming loads)
    bool  bDoorTeleportComplete = false;         // True once PerformDoorTeleport has run
    FTimerHandle DoorTransitionTimerHandle;      // Phase 1: fires PerformDoorTeleport after fade completes
    FTimerHandle DoorArrivalTimerHandle;         // Phase 2: fires OnDoorEntered after level loads

    // Gravity scale saved before suppression so we can restore the original value
    float SavedGravityScale = 1.0f;
    bool  bDoorSuppressedGravity = false;

    // ── Floor confirmation polling ──────────────────────────────────────────────
    // Set when a floor trace misses after a door teleport, on BeginPlay, or when
    // WaitForFloor() is called.  TickComponent polls TrySnapToFloor() each frame
    // until the sub-level geometry is ready, then restores gravity and continues.
    bool              bAwaitingFloorConfirmation  = false;
    bool              bFloorConfirmSkipDoorEvents = false; // true = just unlock, no door events
    EGF_PlayerDirection  FloorConfirmFacing          = EGF_PlayerDirection::South;
    int32             FloorConfirmAutoSteps        = 0;
    FGF_GridCoordinate   FloorConfirmDestCoord;
    float             FloorConfirmElapsed          = 0.0f;
    bool              bFloorConfirmStallLogged     = false; // one-shot: kept waiting past the timeout
    // Only bypasses the "all sub-levels visible" half of the wait. A missing floor is never
    // timed out past -- releasing a player onto geometry that has not streamed in drops them
    // through the world. See the release block in TickComponent.
    static constexpr float FloorConfirmTimeout     = 5.0f;

    // Traces down from the actor's current position to find the floor.
    // On success: snaps actor Z to floor + ActorHeightAboveGround and returns true.
    bool TrySnapToFloor();

    // True only when every streaming level that should be visible has finished
    // AddToWorld (IsLevelVisible()).  Gates floor confirmation so the loading
    // screen / player unlock waits for the sub-level to fully stream in, not just
    // for the first floor collision to appear.
    bool AreStreamingLevelsReady() const;

    // Forces the streaming-volume system to re-evaluate at the actor's current
    // location.  Call right after a teleport so the destination sub-level is
    // flagged ShouldBeVisible() immediately — otherwise AreStreamingLevelsReady()
    // can briefly (and wrongly) report "ready" for a frame after the teleport.
    void SeedStreamingForCurrentLocation();

    // Teleports the player to the door destination and registers on the new tile.
    // Called before the delay timer so level streaming starts immediately.
    void PerformDoorTeleport(FGF_GridCoordinate DestCoord, EGF_PlayerDirection Facing);

    // Zeroes gravity/velocity so the player doesn't fall during the ArrivalDelay.
    void SuppressDoorGravity();

    // Restores gravity/velocity to pre-teleport state.
    void RestoreDoorGravity();

    // Suppresses door detection for one CompleteMovement after CompleteEnterDoor,
    // preventing the arrival tile from immediately re-triggering if it is also a door
    bool bSuppressDoorDetection = false;

    // Auto-step state after arrival (consumed in CompleteMovement)
    int32 RemainingAutoSteps = 0;
    EGF_PlayerDirection AutoStepDirection = EGF_PlayerDirection::South;
    bool bIsProcessingDoorAutoSteps = false;

    // Scripted player movement state
    bool bIsProcessingScriptedMove = false;

    // When true, overrides the facing/animation direction during a scripted move.
    // The player still physically moves in the real path direction — only the sprite is affected.
    // Used by BeginPushedBack so the player can face toward the NPC while sliding backward.
    bool bScriptedFacingOverride = false;
    EGF_PlayerDirection ScriptedFacingOverrideDirection = EGF_PlayerDirection::South;
    TArray<FGF_GridCoordinate> ScriptedMovePath;
    int32 ScriptedMovePathIndex = 0;
    float ScriptedMoveOriginalSpeed = 0.0f;

    // Restores MovementSpeed after a temporary scripted-move speed change (run / push speed).
    // No-op if no temporary change is pending.
    void RestoreScriptedMoveSpeed();

    // NPC train-follow state
    bool bIsFollowingNPC         = false;
    bool bNPCFollowPathFinished  = false;
    TArray<FGF_GridCoordinate> NPCFollowQueue;

    UPROPERTY()
    UGF_GridNPCAIComponent* FollowedNPC = nullptr;

    UFUNCTION()
    void HandleNPCStepTaken(FGF_GridCoordinate PreviousPosition);

    UFUNCTION()
    void HandleNPCMoveComplete();

    void TakeNextFollowStep();
    //====================================================================================
    // INTERNAL MOVEMENT LOGIC
    //====================================================================================


    void TickMovement(float DeltaTime);
    void CompleteMovement();

    //====================================================================================
    // SPRITE MANAGEMENT
    //====================================================================================

    void UpdateSprite();
    void SetIdleAnimation(EGF_PlayerDirection Direction);
    void SetWalkAnimation(EGF_PlayerDirection Direction);

    //====================================================================================
    // CACHED REFERENCES
    //====================================================================================

    UPROPERTY()
    UGF_GridWorldSubsystem* GridSubsystem;

    //====================================================================================
    // LEDGE JUMPING
    //====================================================================================

    bool bIsJumpingLedge = false;
    float LedgeJumpSpeed = 600.0f;

};