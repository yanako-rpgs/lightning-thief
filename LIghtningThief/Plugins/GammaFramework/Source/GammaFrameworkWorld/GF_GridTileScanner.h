#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "GF_GridWorldSubsystem.h"
#include "GF_GridTileScanner.generated.h"

/**
 * Tool actor that scans your level and automatically sets up grid tiles
 * based on collision geometry. Use a box volume to define the scan area!
 *
 * SETUP:
 * 1. Place in level
 * 2. Scale the box component to cover your area
 * 3. Configure collision settings
 * 4. Press "Scan And Setup Tiles" or enable auto-scan
 *
 * NOTES:
 * - This scans STATIC collision (walls, terrain)
 * - For dynamic objects (doors, NPCs, items), use SetTileProperties in Blueprint
 */
UCLASS()
class GAMMAFRAMEWORKWORLD_API AGF_GridTileScanner : public AActor
{
    GENERATED_BODY()

public:
    AGF_GridTileScanner();

protected:
    virtual void BeginPlay() override;

public:
    //====================================================================================
    // VOLUME COMPONENT
    //====================================================================================

    /** Vault volume that defines the scan area (scale this to cover your map!) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid Scanner")
    UBoxComponent* ScanVolume;

    //====================================================================================
    // SCAN SETTINGS
    //====================================================================================

    /** Use volume bounds for scanning? (recommended - easier to visualize) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Scanner")
    bool bUseVolumeBounds = true;

    /** Height to check for collisions (above ground) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Scanner", meta = (ClampMin = "1.0", ClampMax = "500.0"))
    float ScanHeight = 50.0f;

    /** Collision channel to check (usually WorldStatic or Visibility) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Scanner")
    TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

    /** Should we mark tiles WITH collision as blocked? (true for walls)
      * Or mark tiles WITHOUT collision as blocked? (false for floors) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Scanner")
    bool bBlockedIfHitSomething = true;

    /** Automatically scan on BeginPlay in PIE? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Scanner")
    bool bAutoScanOnPlay = false;

    //====================================================================================
    // VISUALIZATION
    //====================================================================================

    /** Show debug lines during scan */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Scanner|Debug")
    bool bShowDebugLines = true;

    /** How long to show debug lines (seconds) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Scanner|Debug")
    float DebugLineDuration = 5.0f;

    //====================================================================================
    // SCAN ACTIONS
    //====================================================================================

    /**
     * Scan the level and set up grid tiles based on collision
     * Call this from Blueprint or editor button
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Grid Scanner")
    void ScanAndSetupTiles();

    /**
     * Clear all tile properties in scan range
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Grid Scanner")
    void ClearTiles();

    /**
     * Scan a single tile and return if it's blocked
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Scanner")
    bool IsTileBlocked(FGF_GridCoordinate Coord);

    /**
     * Get the bounds of the scan area in grid coordinates
     */
    UFUNCTION(BlueprintCallable, Category = "Grid Scanner")
    void GetScanBounds(FGF_GridCoordinate& OutMin, FGF_GridCoordinate& OutMax);

protected:
    //====================================================================================
    // INTERNAL SCAN LOGIC
    //====================================================================================

    /**
     * Perform line trace at tile coordinate
     */
    bool LineTraceAtTile(FGF_GridCoordinate Coord, FHitResult& OutHit);

    /**
     * Get scan statistics
     */
    void LogScanResults(int32 TotalTiles, int32 BlockedTiles, int32 WalkableTiles);
};