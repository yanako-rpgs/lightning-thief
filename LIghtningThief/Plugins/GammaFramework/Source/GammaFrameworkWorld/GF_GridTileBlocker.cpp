#include "GF_GridTileBlocker.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

AGF_GridTileBlocker::AGF_GridTileBlocker()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create box component for the blocker volume
    BlockerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("BlockerVolume"));
    RootComponent = BlockerVolume;

    // Set default box extent (5x5 tiles at 64 units = 320x320)
    BlockerVolume->SetBoxExtent(FVector(320.0f, 320.0f, 100.0f));

    // Make it visible in editor
    BlockerVolume->SetHiddenInGame(true);
    BlockerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    #if WITH_EDITORONLY_DATA
    BlockerVolume->bDrawOnlyIfSelected = false;
    BlockerVolume->SetLineThickness(3.0f);
    BlockerVolume->ShapeColor = FColor::Red; // Red = blocks by default
    #endif

    // Make this actor visible in editor
    #if WITH_EDITOR
    bRunConstructionScriptOnDrag = true;
    #endif
}

void AGF_GridTileBlocker::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoMarkOnPlay)
    {
        MarkTiles();
    }
}

//====================================================================================
// MARK TILES
//====================================================================================

void AGF_GridTileBlocker::MarkTiles()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("GridTileBlocker: No world!"));
        return;
    }

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("GridTileBlocker: GridWorldSubsystem not found!"));
        return;
    }

    // Get volume bounds in grid coordinates
    FGF_GridCoordinate MinCoord, MaxCoord;
    GetVolumeBounds(MinCoord, MaxCoord);

    int32 TotalTiles = 0;

    // Mark all tiles within the volume
	for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
	{
	    for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
	    {
	        // Mark multiple Z levels!
	        for (int32 Z = MinCoord.Z; Z <= MaxCoord.Z; Z++)  // <- Cover Z levels -2 to 2
	        {
	            FGF_GridCoordinate Coord(X, Y, Z);
	            TotalTiles++;

	            // Get existing properties or create new ones
	            FGF_TileProperties TileProps = GridSubsystem->GetTileProperties(Coord);

	            // Set walkability
	            TileProps.bIsWalkable = bMakeTilesWalkable;

	            // Update in subsystem
	            GridSubsystem->SetTileProperties(Coord, TileProps);
	        }
	    }
	}

    // Log results
    LogMarkResults(TotalTiles, bMakeTilesWalkable);

    // Show debug visualization
    if (bShowDebugTiles)
    {
        DrawDebugTiles();
    }
}

void AGF_GridTileBlocker::ClearTiles()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return;

    // Get volume bounds
    FGF_GridCoordinate MinCoord, MaxCoord;
    GetVolumeBounds(MinCoord, MaxCoord);

    // Reset all tiles to default (walkable)
    FGF_TileProperties DefaultProps;
    DefaultProps.bIsWalkable = true;
    DefaultProps.bIsSlope = false;
    DefaultProps.HeightLevel = 0;

    int32 TotalTiles = 0;

    for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
    {
        for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
        {
            FGF_GridCoordinate Coord(X, Y, 0);
            GridSubsystem->SetTileProperties(Coord, DefaultProps);
            TotalTiles++;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("GridTileBlocker: Cleared %d tiles"), TotalTiles);
}

void AGF_GridTileBlocker::ToggleWalkability()
{
    bMakeTilesWalkable = !bMakeTilesWalkable;
    MarkTiles();
}

int32 AGF_GridTileBlocker::GetAffectedTileCount() const
{
    FGF_GridCoordinate MinCoord, MaxCoord;
    GetVolumeBounds(MinCoord, MaxCoord);

    int32 Width = (MaxCoord.X - MinCoord.X) + 1;
    int32 Height = (MaxCoord.Y - MinCoord.Y) + 1;

    return Width * Height;
}

//====================================================================================
// INTERNAL HELPERS
//====================================================================================

void AGF_GridTileBlocker::GetVolumeBounds(FGF_GridCoordinate& OutMin, FGF_GridCoordinate& OutMax) const
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return;

    // Get box component bounds
    FVector Origin = BlockerVolume->GetComponentLocation();
    FVector BoxExtent = BlockerVolume->GetScaledBoxExtent();

    // Calculate world space bounds
    FVector MinWorld = Origin - BoxExtent;
    FVector MaxWorld = Origin + BoxExtent;

    // Convert to grid coordinates
    OutMin = GridSubsystem->WorldToGrid(MinWorld);
    OutMax = GridSubsystem->WorldToGrid(MaxWorld);
}

void AGF_GridTileBlocker::DrawDebugTiles()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return;

    FGF_GridCoordinate MinCoord, MaxCoord;
    GetVolumeBounds(MinCoord, MaxCoord);

    FColor TileColor = bMakeTilesWalkable ? WalkableTileColor : BlockedTileColor;
    float HalfTile = GridSubsystem->TileSize * 0.5f;

    // Draw a box around each affected tile
    for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
    {
        for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
        {
            FGF_GridCoordinate Coord(X, Y, 0);
            FVector TileCenter = GridSubsystem->GridToWorld(Coord);
            TileCenter.Z += 5.0f; // Slightly above ground

            // Draw box around tile
            DrawDebugBox(
                World,
                TileCenter,
                FVector(HalfTile, HalfTile, 2.0f),
                TileColor,
                false,
                DebugDuration,
                0,
                2.0f
            );
        }
    }
}

void AGF_GridTileBlocker::LogMarkResults(int32 TotalTiles, bool bWalkable)
{

}