#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "Components/BillboardComponent.h"
#include "GF_GridWorldSubsystem.h"
#include "GF_GridTileMarker.generated.h"

/**
 * Actor for marking special tiles in the editor
 * Place these in your level to set up items, ledges, slopes, surf tiles, etc.
 *
 * USAGE:
 * 1. Drag into level
 * 2. Set Tile Type (Item, Ledge, Slope, Water, etc.)
 * 3. Configure properties
 * 4. Press "Mark Tile" button or enable Auto Mark On Play
 */



UCLASS()
class GAMMAFRAMEWORKWORLD_API AGF_GridTileMarker : public AActor
{
    GENERATED_BODY()

public:
    AGF_GridTileMarker();

    // ✅ CRITICAL FIX: Initialize tiles BEFORE other actors spawn
    virtual void PreInitializeComponents() override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    //====================================================================================
    // COMPONENTS
    //====================================================================================

    /** Visual marker in editor */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid Marker")
    UBillboardComponent* MarkerSprite;

    /** Collision volume for visualization */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid Marker")
    UBoxComponent* MarkerVolume;

    //====================================================================================
    // TILE PROPERTIES
    //====================================================================================

    /** What type of tile is this? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Properties")
    EGF_TileType TileType = EGF_TileType::Normal;

    /** Is this tile walkable? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Properties")
    bool bIsWalkable = true;

    /** Height level (for slopes) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Properties", meta = (ClampMin = "0", ClampMax = "10"))
    int32 HeightLevel = 0;

    //====================================================================================
    // SLOPE SETTINGS
    //====================================================================================

    /** Is this a slope tile? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Slope", meta = (EditCondition = "TileType == EGF_TileType::Slope"))
    bool bIsSlope = false;

    /** Direction that leads UP the slope */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Slope", meta = (EditCondition = "bIsSlope"))
    EGF_PlayerDirection SlopeDirection = EGF_PlayerDirection::North;

    //====================================================================================
    // LEDGE SETTINGS
    //====================================================================================

    /** Is this a ledge (one-way jump)? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Ledge", meta = (EditCondition = "TileType == EGF_TileType::Ledge"))
    bool bIsLedge = false;

    /** Direction you can jump off the ledge */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Ledge", meta = (EditCondition = "bIsLedge"))
    EGF_PlayerDirection LedgeJumpDirection = EGF_PlayerDirection::South;

    //====================================================================================
    // ITEM SETTINGS
    //====================================================================================

    /** Does this tile have an item? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Item", meta = (EditCondition = "TileType == EGF_TileType::Item"))
    bool bHasItem = false;

    /** Item identifier (e.g., "Potion", "TM01", "Growth Candy") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Item", meta = (EditCondition = "bHasItem"))
    FName ItemID = NAME_None;

    //====================================================================================
    // WATER/SURF SETTINGS
    //====================================================================================

    /** Requires Surf to walk on? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Water", meta = (EditCondition = "TileType == EGF_TileType::Water"))
    bool bRequiresSurf = false;

    //====================================================================================
    // TALL GRASS SETTINGS
    //====================================================================================

    /** Can trigger wild encounters? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|TallGrass", meta = (EditCondition = "TileType == EGF_TileType::TallGrass"))
    bool bCanTriggerEncounter = false;

    /** Encounter chance (0.0 - 1.0) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|TallGrass", meta = (EditCondition = "bCanTriggerEncounter", ClampMin = "0.0", ClampMax = "1.0"))
    float EncounterChance = 0.1f;

    //====================================================================================
    // DOOR SETTINGS
    //====================================================================================

    /** Is this a door? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "TileType == EGF_TileType::Door"))
    bool bHasDoor = false;

    /**
     * Drag the destination GridTileMarker here (the tile the player arrives at on the other side).
     * The coord and exact world position are read from it automatically.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor"))
    AGF_GridTileMarker* LinkedDestinationMarker = nullptr;

    /**
     * Tick this on a CAVE/DUNGEON entrance door (the outdoor marker that leads inside).
     * When the player enters through it, the tile just outside is remembered as the
     * Escape Rope / Dig return point automatically — no Blueprint wiring needed.
     * Leave OFF for building/house doors.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor"))
    bool bIsDungeonEntrance = false;

    /**
     * Tick this on the INSIDE exit door that leads back out to the overworld.
     * Walking through it clears the "in dungeon" state so the Escape Rope greys
     * out again — the counterpart to bIsDungeonEntrance. No Blueprint wiring needed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor"))
    bool bIsDungeonExit = false;

    /**
     * How many tiles the player walks INTO the entrance before the transition fires.
     * 1 = one step through the door frame, then fade (default).
     * 2-3 = deeper walk-in before the screen transitions.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor", ClampMin = "1", ClampMax = "10"))
    int32 EntryStepsBeforeTeleport = 1;

    /**
     * How many tiles the player automatically walks forward after arriving at the destination.
     * Use 3-5 for forest/cave entrances so the player clears the entrance mesh before control returns.
     * 0 = player regains control immediately on arrival.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor", ClampMin = "0", ClampMax = "10"))
    int32 AutoStepsOnEntry = 0;

    /**
     * Sound to play the instant the player activates this door (steps onto it).
     * For an entrance marker this is the "enter building" sound.
     * For an exit marker (inside the building) this is the "leave building" sound.
     * Leave empty for no sound.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor"))
    USoundBase* DoorSound = nullptr;

    /**
     * Seconds to wait after the player finishes walking into the door before
     * OnDoorEntered fires and the teleport is triggered.
     * 0 = instant (default).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor", ClampMin = "0.0", ClampMax = "10.0"))
    float DoorTransitionDelay = 0.0f;

    /**
     * Seconds to wait AFTER the player teleports to the destination before the
     * arrival auto-walk begins. The player is already at the destination during
     * this time (invisible on a black screen), so the level streaming volume is
     * active and loading. Set to 1-2 seconds to let the streamed level finish
     * before the player becomes visible.
     * 0 = start walking immediately (default).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor", ClampMin = "0.0", ClampMax = "10.0"))
    float ArrivalDelay = 0.0f;

    /**
     * Normally the player keeps facing/walking in the SAME direction they entered the door
     * after teleporting. Enable this to force a specific arrival direction instead.
     * Example: Slatehaven apartment stairs — entering going North must walk the player South
     * after arriving on the other side.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor"))
    bool bOverrideStepDirection = false;

    /**
     * Direction the player faces and auto-walks after arriving at the destination.
     * Only used when bOverrideStepDirection is true.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Door", meta = (EditCondition = "bHasDoor && bOverrideStepDirection"))
    EGF_PlayerDirection OverrideStepDirection = EGF_PlayerDirection::South;

    /** Interactable? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Interaction")
    bool bHasInteractable = false;

    //====================================================================================
    // MARKER SETTINGS
    //====================================================================================

    /** Automatically mark this tile when play starts? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Setup")
    bool bAutoMarkOnPlay = true;

    /** Show debug visualization? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Debug")
    bool bShowDebugVisualization = true;

    /** How long to show debug (seconds, 0 = permanent) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Marker|Debug")
    float DebugDuration = 0.0f;

    //====================================================================================
    // ACTIONS
    //====================================================================================

    /**
     * Mark this tile with configured properties
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Grid Marker")
    void MarkTile();

    /**
     * Clear this tile (reset to default)
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Grid Marker")
    void ClearTile();

    /**
     * Get the grid coordinate of this marker
     */
    UFUNCTION(BlueprintPure, Category = "Grid Marker")
    FGF_GridCoordinate GetGridCoordinate() const;

protected:
    //====================================================================================
    // INTERNAL FUNCTIONS
    //====================================================================================

    void DrawDebugVisualization();
    FColor GetTileTypeColor() const;
    void UpdateMarkerColor();

    // Backup re-mark scheduled from BeginPlay (see the comment there). Kept as a member
    // rather than a local so EndPlay can cancel it: a marker whose sub-level is hidden
    // inside the 0.2s window would otherwise still be holding a queued callback.
    FTimerHandle MarkTileBackupTimer;

    #if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
#endif
};