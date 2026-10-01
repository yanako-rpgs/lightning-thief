
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GF_GridWorldSubsystem.h"
#include "GF_GridDebugComponent.generated.h"

/**
 * Component for visualizing the grid system in-game
 * Shows colored tiles, coordinates, and occupancy
 * Attach to player character for best results
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMMAFRAMEWORKWORLD_API UGF_GridDebugComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGF_GridDebugComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    //====================================================================================
    // DEBUG TOGGLES
    //====================================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Toggles")
    bool bShowDebugGrid = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Toggles")
    bool bShowTileCoordinates = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Toggles")
    bool bShowOccupiedTiles = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Toggles")
    bool bShowWalkableTiles = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Toggles")
    bool bShowSlopeTiles = false;

    //====================================================================================
    // DEBUG COLORS
    //====================================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Colors")
    FColor GridLineColor = FColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Colors")
    FColor WalkableTileColor = FColor::Green;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Colors")
    FColor BlockedTileColor = FColor::Red;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Colors")
    FColor SlopeTileColor = FColor::Yellow;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Colors")
    FColor OccupiedTileColor = FColor::Cyan;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Colors")
    FColor PlayerTileColor = FColor::Blue;

    //====================================================================================
    // DEBUG RANGE
    //====================================================================================

    // How many tiles to draw around the player
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Settings", meta = (ClampMin = "1", ClampMax = "50"))
    int32 DebugRangeX = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Settings", meta = (ClampMin = "1", ClampMax = "50"))
    int32 DebugRangeY = 20;

    // Line thickness for grid lines
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Settings", meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float LineThickness = 2.0f;

    // Text scale for coordinates
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Settings", meta = (ClampMin = "0.5", ClampMax = "3.0"))
    float TextScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Settings")
	bool bAutoDetectGroundZ = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Settings")
	float GroundTraceDistance = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Settings")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

    //====================================================================================
    // PUBLIC API
    //====================================================================================

    /**
     * Find the debug component on the local player pawn, attaching one if it isn't
     * there yet. The component is not placed on the player pawn, so every entry point
     * (gf.grid, the GE Debugger Grid tab, Blueprint) goes through here rather than
     * each rolling its own spawn.
     *
     * Runtime-attached components start at a tighter range than the editor defaults:
     * 41x41 tiles of debug lines per frame is not something to hand a tester.
     *
     * Returns nullptr if there is no player pawn yet.
     */
    UFUNCTION(BlueprintCallable, Category = "Debug", meta = (WorldContext = "WorldContextObject"))
    static UGF_GridDebugComponent* GetOrCreateForPlayer(const UObject* WorldContextObject);

    /**
     * Toggle debug grid on/off
     */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void ToggleDebugGrid();

    /**
     * Set debug range
     */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void SetDebugRange(int32 Range);

protected:
    //====================================================================================
    // INTERNAL DRAWING FUNCTIONS
    //====================================================================================

    /**
     * Draw the entire debug grid
     */
    void DrawDebugGrid(UWorld* World);

    /**
     * Draw a single tile at coordinate
     */
    void DrawTileAtCoordinate(UWorld* World, FGF_GridCoordinate Coord, FColor Color, float HeightOffset = 0.0f);

    /**
     * Draw text at tile coordinate
     */
    void DrawTileText(UWorld* World, FGF_GridCoordinate Coord, const FString& Text);

    /**
     * Get the appropriate color for a tile based on its properties
     */
    FColor GetTileColor(const FGF_TileProperties& Props, bool bIsPlayerTile) const;

    //====================================================================================
    // CACHED REFERENCES
    //====================================================================================

    UPROPERTY()
    UGF_GridWorldSubsystem* GridSubsystem;
};