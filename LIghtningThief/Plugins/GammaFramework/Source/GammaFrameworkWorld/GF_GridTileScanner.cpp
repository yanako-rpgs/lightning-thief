#include "GF_GridTileScanner.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

AGF_GridTileScanner::AGF_GridTileScanner()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create box component for volume-based scanning
    ScanVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ScanVolume"));
    RootComponent = ScanVolume;

    // Set default box extent (10x10 tiles at 64 units = 640x640)
    ScanVolume->SetBoxExtent(FVector(640.0f, 640.0f, 100.0f));

    // Make it visible in editor
    ScanVolume->SetHiddenInGame(true);
    ScanVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    #if WITH_EDITORONLY_DATA
    ScanVolume->bDrawOnlyIfSelected = false;
    ScanVolume->SetLineThickness(3.0f);
    #endif

    // Make this actor visible in editor
    #if WITH_EDITOR
    bRunConstructionScriptOnDrag = true;
    #endif
}

void AGF_GridTileScanner::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoScanOnPlay)
    {
        ScanAndSetupTiles();
    }
}

//====================================================================================
// SCAN FUNCTIONS
//====================================================================================

void AGF_GridTileScanner::ScanAndSetupTiles()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("GridTileScanner: No world!"));
        return;
    }

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("GridTileScanner: GridWorldSubsystem not found!"));
        return;
    }

    // Get scan bounds
    FGF_GridCoordinate MinCoord, MaxCoord;
    GetScanBounds(MinCoord, MaxCoord);

    int32 RangeX = MaxCoord.X - MinCoord.X;
    int32 RangeY = MaxCoord.Y - MinCoord.Y;

    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("GridTileScanner: Starting scan..."));
    UE_LOG(LogTemp, Warning, TEXT("Scan Range: %d x %d tiles"), RangeX + 1, RangeY + 1);
    UE_LOG(LogTemp, Warning, TEXT("From (%d,%d) to (%d,%d)"), MinCoord.X, MinCoord.Y, MaxCoord.X, MaxCoord.Y);
    UE_LOG(LogTemp, Warning, TEXT("========================================"));

    int32 TotalTiles = 0;
    int32 BlockedTiles = 0;
    int32 WalkableTiles = 0;

    // Scan all tiles in range
    for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
    {
        for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
        {
            FGF_GridCoordinate Coord(X, Y, 0);
            TotalTiles++;

            // Check if tile is blocked
            bool bIsBlocked = IsTileBlocked(Coord);

            // Set tile properties
            FGF_TileProperties TileProps;
            TileProps.bIsWalkable = !bIsBlocked;
            TileProps.bIsSlope = false;
            TileProps.HeightLevel = 0;

            GridSubsystem->SetTileProperties(Coord, TileProps);

            if (bIsBlocked)
            {
                BlockedTiles++;
            }
            else
            {
                WalkableTiles++;
            }
        }
    }

    // Log results
    LogScanResults(TotalTiles, BlockedTiles, WalkableTiles);
}

void AGF_GridTileScanner::ClearTiles()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return;

    // Get scan bounds
    FGF_GridCoordinate MinCoord, MaxCoord;
    GetScanBounds(MinCoord, MaxCoord);

    // Reset all tiles to default (walkable)
    FGF_TileProperties DefaultProps;
    DefaultProps.bIsWalkable = true;
    DefaultProps.bIsSlope = false;
    DefaultProps.HeightLevel = 0;

    for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
    {
        for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
        {
            FGF_GridCoordinate Coord(X, Y, 0);
            GridSubsystem->SetTileProperties(Coord, DefaultProps);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("GridTileScanner: Cleared all tiles in range"));
}

bool AGF_GridTileScanner::IsTileBlocked(FGF_GridCoordinate Coord)
{
    FHitResult HitResult;
    bool bHitSomething = LineTraceAtTile(Coord, HitResult);

    // Logic depends on bBlockedIfHitSomething
    // TRUE = walls block (if we hit something, it's blocked)
    // FALSE = floors block (if we DON'T hit something, it's blocked - for detecting pits/holes)
    return bBlockedIfHitSomething ? bHitSomething : !bHitSomething;
}

void AGF_GridTileScanner::GetScanBounds(FGF_GridCoordinate& OutMin, FGF_GridCoordinate& OutMax)
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return;

    if (bUseVolumeBounds && ScanVolume)
    {
        // Use box component bounds
        FVector Origin = ScanVolume->GetComponentLocation();
        FVector BoxExtent = ScanVolume->GetScaledBoxExtent();

        // Calculate world space bounds
        FVector MinWorld = Origin - BoxExtent;
        FVector MaxWorld = Origin + BoxExtent;

        // Convert to grid coordinates
        OutMin = GridSubsystem->WorldToGrid(MinWorld);
        OutMax = GridSubsystem->WorldToGrid(MaxWorld);
    }
}

//====================================================================================
// INTERNAL HELPERS
//====================================================================================

bool AGF_GridTileScanner::LineTraceAtTile(FGF_GridCoordinate Coord, FHitResult& OutHit)
{
    UWorld* World = GetWorld();
    if (!World)
        return false;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return false;

    // Get world position of tile center
    FVector TileCenter = GridSubsystem->GridToWorld(Coord);

    // Line trace from above to below
    FVector TraceStart = TileCenter + FVector(0, 0, ScanHeight);
    FVector TraceEnd = TileCenter - FVector(0, 0, ScanHeight);

    // Set up trace parameters
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);  // Ignore scanner actor
    QueryParams.bTraceComplex = true;   // Use complex collision

    // Perform line trace
    bool bHit = World->LineTraceSingleByChannel(
        OutHit,
        TraceStart,
        TraceEnd,
        CollisionChannel,
        QueryParams
    );

    // Debug visualization
    if (bShowDebugLines)
    {
        FColor LineColor = bHit ? FColor::Red : FColor::Green;
        DrawDebugLine(
            World,
            TraceStart,
            TraceEnd,
            LineColor,
            false,
            DebugLineDuration,
            0,
            1.0f
        );

        if (bHit)
        {
            DrawDebugPoint(World, OutHit.ImpactPoint, 5.0f, FColor::Red, false, DebugLineDuration);
        }
    }

    return bHit;
}

void AGF_GridTileScanner::LogScanResults(int32 TotalTiles, int32 BlockedTiles, int32 WalkableTiles)
{
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("GridTileScanner: Scan complete!"));
    UE_LOG(LogTemp, Warning, TEXT("Total Tiles: %d"), TotalTiles);
    UE_LOG(LogTemp, Warning, TEXT("Blocked Tiles: %d (%.1f%%)"), BlockedTiles, (float)BlockedTiles / TotalTiles * 100.0f);
    UE_LOG(LogTemp, Warning, TEXT("Walkable Tiles: %d (%.1f%%)"), WalkableTiles, (float)WalkableTiles / TotalTiles * 100.0f);
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
}