

#include "GF_GridDebugComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UGF_GridDebugComponent::UGF_GridDebugComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UGF_GridDebugComponent::BeginPlay()
{
    Super::BeginPlay();

    // Get grid subsystem
    UWorld* World = GetWorld();
    if (World)
    {
        GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
        if (!GridSubsystem)
        {
            UE_LOG(LogTemp, Error, TEXT("GridDebugComponent: Could not find GridWorldSubsystem!"));
        }
    }
}

void UGF_GridDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bShowDebugGrid || !GridSubsystem)
        return;

    UWorld* World = GetWorld();
    if (!World)
        return;

    DrawDebugGrid(World);
}

//====================================================================================
// DRAWING FUNCTIONS
//====================================================================================

void UGF_GridDebugComponent::DrawDebugGrid(UWorld* World)
{
    if (!GridSubsystem)
        return;

    // Get owner (player) position to center debug view
    AActor* Owner = GetOwner();
    if (!Owner)
        return;

    FVector OwnerLocation = Owner->GetActorLocation();
    FGF_GridCoordinate PlayerCoord = GridSubsystem->WorldToGrid(OwnerLocation);

    // Draw tiles around player
    for (int32 X = PlayerCoord.X - DebugRangeX; X <= PlayerCoord.X + DebugRangeX; X++)
    {
        for (int32 Y = PlayerCoord.Y - DebugRangeY; Y <= PlayerCoord.Y + DebugRangeY; Y++)
        {
            FGF_GridCoordinate Coord(X, Y, PlayerCoord.Z);  // Match player's current height level

            // Get tile properties
            FGF_TileProperties Props = GridSubsystem->GetTileProperties(Coord);

            // Check if this is the player's tile
            bool bIsPlayerTile = (Coord == PlayerCoord);

            // Determine tile color
            FColor TileColor = GetTileColor(Props, bIsPlayerTile);

            // Calculate height offset for slopes
            float HeightOffset = Props.HeightLevel * GridSubsystem->TileSize;

            // Draw the tile
            DrawTileAtCoordinate(World, Coord, TileColor, HeightOffset);

            // Draw coordinates
            if (bShowTileCoordinates)
            {
                FString CoordText = FString::Printf(TEXT("%d,%d"), X, Y);
                if (Props.HeightLevel != 0)
                {
                    CoordText += FString::Printf(TEXT("\nZ:%d"), Props.HeightLevel);
                }
                DrawTileText(World, Coord, CoordText);
            }
        }
    }
}

void UGF_GridDebugComponent::DrawTileAtCoordinate(UWorld* World, FGF_GridCoordinate Coord, FColor Color, float HeightOffset)
{
    if (!GridSubsystem)
        return;

    FVector TileCenter = GridSubsystem->GridToWorld(Coord);

    // ✅ AUTO-DETECT GROUND Z (same as movement component!)
    if (bAutoDetectGroundZ)
    {
        FVector TraceStart = TileCenter + FVector(0, 0, GroundTraceDistance);
        FVector TraceEnd = TileCenter - FVector(0, 0, GroundTraceDistance);

        FHitResult HitResult;
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(GetOwner());
        QueryParams.bTraceComplex = true;

        if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, GroundTraceChannel, QueryParams))
        {
            TileCenter.Z = HitResult.ImpactPoint.Z + 5.0f;  // Slightly above ground
        }
        else
        {
            TileCenter.Z += HeightOffset + 5.0f;  // Fallback to old height
        }
    }
    else
    {
        TileCenter.Z += HeightOffset;
    }

    float HalfTile = GridSubsystem->TileSize * 0.5f;

    // Define corners of the tile
    FVector Corners[4];
    Corners[0] = TileCenter + FVector(-HalfTile, -HalfTile, 0);  // Bottom-left
    Corners[1] = TileCenter + FVector(HalfTile, -HalfTile, 0);   // Bottom-right
    Corners[2] = TileCenter + FVector(HalfTile, HalfTile, 0);    // Top-right
    Corners[3] = TileCenter + FVector(-HalfTile, HalfTile, 0);   // Top-left

    // Draw the 4 edges of the tile
    for (int32 i = 0; i < 4; i++)
    {
        FVector Start = Corners[i];
        FVector End = Corners[(i + 1) % 4];

        DrawDebugLine(
            World,
            Start,
            End,
            Color,
            false,      // Not persistent
            -1.0f,      // Lifetime (one frame)
            0,          // Depth priority
            LineThickness
        );
    }

    // Optional: Draw an X in the center for easier visibility
    DrawDebugLine(World, Corners[0], Corners[2], Color, false, -1.0f, 0, LineThickness * 0.5f);
    DrawDebugLine(World, Corners[1], Corners[3], Color, false, -1.0f, 0, LineThickness * 0.5f);
}

void UGF_GridDebugComponent::DrawTileText(UWorld* World, FGF_GridCoordinate Coord, const FString& Text)
{
    if (!GridSubsystem)
        return;

    FVector TileCenter = GridSubsystem->GridToWorld(Coord);

    // ✅ AUTO-DETECT GROUND Z
    if (bAutoDetectGroundZ)
    {
        FVector TraceStart = TileCenter + FVector(0, 0, GroundTraceDistance);
        FVector TraceEnd = TileCenter - FVector(0, 0, GroundTraceDistance);

        FHitResult HitResult;
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(GetOwner());
        QueryParams.bTraceComplex = true;

        if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, GroundTraceChannel, QueryParams))
        {
            TileCenter.Z = HitResult.ImpactPoint.Z + 20.0f;  // Above ground for text
        }
        else
        {
            TileCenter.Z += 20.0f;
        }
    }
    else
    {
        TileCenter.Z += 10.0f;
    }

    DrawDebugString(
        World,
        TileCenter,
        Text,
        nullptr,
        FColor::White,
        0.0f,       // One frame
        true,       // Draw shadow for readability
        TextScale
    );
}

FColor UGF_GridDebugComponent::GetTileColor(const FGF_TileProperties& Props, bool bIsPlayerTile) const
{
    // Priority: Player tile > Slope > Occupied > Walkable/Blocked

    if (bIsPlayerTile)
    {
        return PlayerTileColor;
    }

    if (bShowSlopeTiles && Props.bIsSlope)
    {
        return SlopeTileColor;
    }

    if (bShowOccupiedTiles && Props.OccupyingActor != nullptr)
    {
        return OccupiedTileColor;
    }

    if (bShowWalkableTiles)
    {
        return Props.bIsWalkable ? WalkableTileColor : BlockedTileColor;
    }

    return GridLineColor;
}

//====================================================================================
// PUBLIC API
//====================================================================================

UGF_GridDebugComponent* UGF_GridDebugComponent::GetOrCreateForPlayer(const UObject* WorldContextObject)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World)
    {
        return nullptr;
    }

    APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
    if (!Pawn)
    {
        return nullptr;
    }

    if (UGF_GridDebugComponent* Existing = Pawn->FindComponentByClass<UGF_GridDebugComponent>())
    {
        return Existing;
    }

    UGF_GridDebugComponent* Created = NewObject<UGF_GridDebugComponent>(
        Pawn, UGF_GridDebugComponent::StaticClass(), TEXT("RuntimeGridDebug"));
    if (!Created)
    {
        return nullptr;
    }

    // RegisterComponent runs BeginPlay, which is what caches GridWorldSubsystem.
    // Without it TickComponent early-outs on the null check and draws nothing.
    Created->RegisterComponent();

    // Editor defaults draw 41x41 tiles. Start tighter for runtime attach; the
    // range control in the debugger (and gf.grid range) widens it on demand.
    Created->DebugRangeX = 8;
    Created->DebugRangeY = 8;
    Created->bShowWalkableTiles = true;
    Created->bShowOccupiedTiles = true;

    return Created;
}

void UGF_GridDebugComponent::ToggleDebugGrid()
{
    bShowDebugGrid = !bShowDebugGrid;
    UE_LOG(LogTemp, Log, TEXT("Grid Debug: %s"), bShowDebugGrid ? TEXT("ON") : TEXT("OFF"));
}

void UGF_GridDebugComponent::SetDebugRange(int32 Range)
{
    DebugRangeX = FMath::Clamp(Range, 1, 50);
    DebugRangeY = FMath::Clamp(Range, 1, 50);
}