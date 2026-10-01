#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "GF_GridWorldSubsystem.h"
#include "GF_GridTileBlocker.generated.h"

/**
 * Volume-based tile blocker for manually marking areas as unwalkable
 *
 * USAGE:
 * 1. Place in level
 * 2. Scale/position the box to cover the area you want to block (building, wall, etc.)
 * 3. Press "Mark Tiles" button or enable Auto Mark On Play
 * 4. All tiles within the volume become unwalkable
 *
 * PERFECT FOR:
 * - Buildings
 * - Walls
 * - Inaccessible areas
 * - Water (if you don't want player to walk on it)
 * - Any static blocked areas
 */
UCLASS()
class GAMMAFRAMEWORKWORLD_API AGF_GridTileBlocker : public AActor
{
    GENERATED_BODY()

public:
    AGF_GridTileBlocker();

protected:
    virtual void BeginPlay() override;

public:
    //====================================================================================
    // VOLUME COMPONENT
    //====================================================================================

    /** Vault volume defining the blocked area - scale this to cover your walls/buildings! */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid Blocker")
    UBoxComponent* BlockerVolume;

    //====================================================================================
    // BLOCKER SETTINGS
    //====================================================================================

    /** Should tiles in this volume be walkable or blocked? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Blocker")
    bool bMakeTilesWalkable = false;

    /** Automatically mark tiles when play starts? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Blocker")
    bool bAutoMarkOnPlay = true;

    /** Show debug visualization of affected tiles? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Blocker|Debug")
    bool bShowDebugTiles = false;

    /** How long to show debug tiles (seconds) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Blocker|Debug")
    float DebugDuration = 5.0f;

    /** Color for blocked tiles */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Blocker|Debug")
    FColor BlockedTileColor = FColor::Red;

    /** Color for walkable tiles */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Blocker|Debug")
    FColor WalkableTileColor = FColor::Green;

    //====================================================================================
    // ACTIONS
    //====================================================================================

    /**
     * Mark all tiles within the volume as blocked/walkable
     * Call this from Blueprint or use the editor button
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Grid Blocker")
    void MarkTiles();

    /**
     * Clear all tiles within the volume (reset to default walkable)
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Grid Blocker")
    void ClearTiles();

    /**
     * Toggle between blocking and unblocking
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Blocker")
    void ToggleWalkability();

    /**
     * Get how many tiles are affected by this volume
     */
    UFUNCTION(BlueprintPure, Category = "Grid Blocker")
    int32 GetAffectedTileCount() const;

protected:
    //====================================================================================
    // INTERNAL FUNCTIONS
    //====================================================================================

    /**
     * Get the grid bounds of the volume
     */
    void GetVolumeBounds(FGF_GridCoordinate& OutMin, FGF_GridCoordinate& OutMax) const;

    /**
     * Draw debug visualization of affected tiles
     */
    void DrawDebugTiles();

    /**
     * Log marking results
     */
    void LogMarkResults(int32 TotalTiles, bool bWalkable);
};