#include "GF_GridTileMarker.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"


#if WITH_EDITOR
void AGF_GridTileMarker::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // Mark tile in editor when moved or properties changed
    if (GetWorld() && !GetWorld()->IsGameWorld())
    {
        MarkTile();
    }

    UpdateMarkerColor();
}
#endif


AGF_GridTileMarker::AGF_GridTileMarker()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create root component
    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    // Create billboard for editor visualization
    MarkerSprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("MarkerSprite"));
    MarkerSprite->SetupAttachment(RootComponent);
    MarkerSprite->SetHiddenInGame(true);

    #if WITH_EDITORONLY_DATA
    // Load sprite texture (you can replace with your own icon)
    static ConstructorHelpers::FObjectFinder<UTexture2D> SpriteTexture(TEXT("/Engine/EditorResources/S_Note"));
    if (SpriteTexture.Succeeded())
    {
        MarkerSprite->SetSprite(SpriteTexture.Object);
    }
    #endif

    // Create box component for visualization
    MarkerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("MarkerVolume"));
    MarkerVolume->SetupAttachment(RootComponent);
    MarkerVolume->SetBoxExtent(FVector(32.0f, 32.0f, 10.0f));  // Single tile size
    MarkerVolume->SetHiddenInGame(true);
    MarkerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    #if WITH_EDITORONLY_DATA
    MarkerVolume->bDrawOnlyIfSelected = false;
    MarkerVolume->SetLineThickness(2.0f);
    #endif

    // Editor visibility
    #if WITH_EDITOR
    bRunConstructionScriptOnDrag = true;
    #endif

    UpdateMarkerColor();
}

// Mark tiles BEFORE BeginPlay so tile data is ready before any entity spawns
void AGF_GridTileMarker::PreInitializeComponents()
{
    Super::PreInitializeComponents();

    if (bAutoMarkOnPlay && GetWorld() && GetWorld()->IsGameWorld())
    {
        MarkTile();
    }
}

void AGF_GridTileMarker::BeginPlay()
{
    Super::BeginPlay();

    // Mark again as backup in case PreInitializeComponents ran before the subsystem was ready
    if (bAutoMarkOnPlay)
    {
        MarkTile();

        // Final backup after a short delay — covers streaming-level late-init cases.
        //
        // Bound as a member function, NEVER as a lambda capturing `this`.
        //
        // FTimerManager only knows how to cancel a pending timer if the binding carries a
        // weak reference to the bound object, which is what the (Obj, MemFunc) overload
        // stores. A lambda is opaque to it: the capture is an ordinary raw pointer, the
        // timer survives the actor, and 0.2s later it calls into freed memory. That is
        // exactly how a tester crashed loading a save into Slatehaven -- markers BeginPlay'd
        // during the floor-confirmation hold (which boosts the streaming budget hard and
        // re-seeds the streaming volumes every frame, so sub-levels can load and unload
        // within a few frames), then a forced GC collected them mid-window and the queued
        // callback faulted in AActor::GetWorld on a dead `this`.
        GetWorld()->GetTimerManager().SetTimer(
            MarkTileBackupTimer, this, &AGF_GridTileMarker::MarkTile, 0.2f, false);
    }
}

void AGF_GridTileMarker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // The weak binding above already makes a late fire harmless, but cancelling outright
    // also stops a marker that was hidden mid-window from re-marking its tile on the way
    // out -- a hidden sub-level's markers have no business writing tile data.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(MarkTileBackupTimer);
    }

    Super::EndPlay(EndPlayReason);
}

//====================================================================================
// ACTIONS
//====================================================================================

void AGF_GridTileMarker::MarkTile()
{
    //UE_LOG(LogTemp, Warning, TEXT(">>> MarkTile() CALLED for %s <<<"), *GetName());

    UWorld* World = GetWorld();
    if (!World)
    {
       // UE_LOG(LogTemp, Error, TEXT("❌ GridTileMarker: No world!"));
        return;
    }

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
    {
       // UE_LOG(LogTemp, Error, TEXT("❌ GridTileMarker: GridWorldSubsystem not found!"));
        return;
    }

    //UE_LOG(LogTemp, Warning, TEXT("✅ GridSubsystem found!"));

    // Get grid coordinate
    FGF_GridCoordinate Coord = GetGridCoordinate();

    FVector WorldLocation = GetActorLocation();
    //UE_LOG(LogTemp, Warning, TEXT("📍 Grid Coord: (%d, %d, %d) [World: %.1f, %.1f, %.1f]"),
      // Coord.X, Coord.Y, Coord.Z,
      // WorldLocation.X, WorldLocation.Y, WorldLocation.Z);

    // Build tile properties
    FGF_TileProperties TileProps;
    TileProps.TileType = TileType;
    TileProps.bIsWalkable = bIsWalkable;
    TileProps.HeightLevel = HeightLevel;

    // Slope
    TileProps.bIsSlope = bIsSlope && (TileType == EGF_TileType::Slope);
    TileProps.SlopeDirection = SlopeDirection;

    // Ledge
    TileProps.bIsLedge = bIsLedge && (TileType == EGF_TileType::Ledge);
    TileProps.LedgeJumpDirection = LedgeJumpDirection;

    // 🔍 DEBUG: Log what we're setting
    //UE_LOG(LogTemp, Warning, TEXT("🏗️ Setting Ledge Properties: bIsLedge=%s, JumpDir=%s"),
      // TileProps.bIsLedge ? TEXT("YES") : TEXT("NO"),
       // *UEnum::GetValueAsString(TileProps.LedgeJumpDirection));

    // Item
    TileProps.bHasItem = bHasItem && (TileType == EGF_TileType::Item);
    TileProps.ItemID = ItemID;
    TileProps.bItemPickedUp = false;

    // Door
    TileProps.bHasDoor = bHasDoor && (TileType == EGF_TileType::Door);
    TileProps.bIsDoorOpen = false;
    TileProps.bIsDungeonEntrance = bIsDungeonEntrance && TileProps.bHasDoor;
    TileProps.bIsDungeonExit = bIsDungeonExit && TileProps.bHasDoor;

    if (TileProps.bHasDoor && LinkedDestinationMarker)
    {
        // Store the grid coord for tile registration
        TileProps.DoorDestinationCoord = LinkedDestinationMarker->GetGridCoordinate();
        // Store the exact world location of the marker — used for teleport instead of a trace,
        // so the player always lands exactly where the marker is placed regardless of Z depth.
        TileProps.DoorDestinationWorldLocation = LinkedDestinationMarker->GetActorLocation();
        TileProps.EntryStepsBeforeTeleport = EntryStepsBeforeTeleport;
        TileProps.DoorAutoSteps = AutoStepsOnEntry;
        TileProps.DoorSound = DoorSound;
        TileProps.DoorTransitionDelay = DoorTransitionDelay;
        TileProps.DoorArrivalDelay = ArrivalDelay;
        TileProps.bDoorOverrideStepDirection = bOverrideStepDirection;
        TileProps.DoorOverrideStepDirection = OverrideStepDirection;
    }

    // Interaction
    TileProps.bHasInteractable = bHasInteractable;

    // Water/Surf
    TileProps.bRequiresSurf = bRequiresSurf && (TileType == EGF_TileType::Water);

    // Tall grass
    TileProps.bCanTriggerEncounter = bCanTriggerEncounter && (TileType == EGF_TileType::TallGrass);
    TileProps.EncounterChance = EncounterChance;

    // Set in subsystem
    GridSubsystem->SetTileProperties(Coord, TileProps);

    //UE_LOG(LogTemp, Warning, TEXT("✅ MARKED TILE (%d, %d, %d) as %s | IsLedge=%s"),
       // Coord.X, Coord.Y, Coord.Z,
       // *UEnum::GetValueAsString(TileType),
       // TileProps.bIsLedge ? TEXT("YES") : TEXT("NO"));

    // 🔍 Verify it was set correctly
    FGF_TileProperties VerifyProps = GridSubsystem->GetTileProperties(Coord);
    //UE_LOG(LogTemp, Warning, TEXT("🔍 VERIFICATION: IsLedge=%s, OccupiedBy=%s"),
       // VerifyProps.bIsLedge ? TEXT("YES") : TEXT("NO"),
        //VerifyProps.OccupyingActor ? *VerifyProps.OccupyingActor->GetName() : TEXT("None"));

    // Show debug visualization
    if (bShowDebugVisualization)
    {
        DrawDebugVisualization();
    }
}

void AGF_GridTileMarker::ClearTile()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return;

    FGF_GridCoordinate Coord = GetGridCoordinate();

    // Reset to default
    FGF_TileProperties DefaultProps;
    DefaultProps.bIsWalkable = true;
    DefaultProps.TileType = EGF_TileType::Normal;

    GridSubsystem->SetTileProperties(Coord, DefaultProps);

    UE_LOG(LogTemp, Log, TEXT("GridTileMarker: Cleared tile (%d, %d, %d)"), Coord.X, Coord.Y, Coord.Z);
}

FGF_GridCoordinate AGF_GridTileMarker::GetGridCoordinate() const
{
    UWorld* World = GetWorld();
    if (!World)
        return FGF_GridCoordinate();

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return FGF_GridCoordinate();

    FVector WorldLocation = GetActorLocation();
    FGF_GridCoordinate Coord = GridSubsystem->WorldToGrid(WorldLocation);
    Coord.Z = 0;  // Tile type data is always 2D - always store/lookup at Z=0
    return Coord;
}

//====================================================================================
// INTERNAL FUNCTIONS
//====================================================================================

void AGF_GridTileMarker::DrawDebugVisualization()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    UGF_GridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGF_GridWorldSubsystem>();
    if (!GridSubsystem)
        return;

    FGF_GridCoordinate Coord = GetGridCoordinate();
    FVector TileCenter = GridSubsystem->GridToWorld(Coord);
    TileCenter.Z += 5.0f;

    FColor DebugColor = GetTileTypeColor();
    float HalfTile = GridSubsystem->TileSize * 0.5f;

    FlushPersistentDebugLines(World);

    // Draw box
    DrawDebugBox(
        World,
        TileCenter,
        FVector(HalfTile, HalfTile, 3.0f),
        DebugColor,
        DebugDuration <= 0.0f,  // Persistent if duration is 0
        DebugDuration,
        0,
        3.0f
    );

    // Draw text label
    FString Label;
    switch (TileType)
    {
        case EGF_TileType::Item:
            Label = FString::Printf(TEXT("ITEM\n%s"), *ItemID.ToString());
            break;
        case EGF_TileType::Ledge:
            Label = TEXT("LEDGE");
            break;
        case EGF_TileType::Slope:
            Label = TEXT("SLOPE");
            break;
        case EGF_TileType::Water:
            Label = TEXT("WATER/SURF");
            break;
        case EGF_TileType::TallGrass:
            Label = TEXT("TALL GRASS");
            break;
        case EGF_TileType::Door:
            Label = TEXT("DOOR");
            break;
        default:
            Label = UEnum::GetValueAsString(TileType);
            break;
    }

    DrawDebugString(
        World,
        TileCenter + FVector(0, 0, 20.0f),
        Label,
        nullptr,
        DebugColor,
        DebugDuration <= 0.0f ? 99999.0f : DebugDuration
    );
}

FColor AGF_GridTileMarker::GetTileTypeColor() const
{
    switch (TileType)
    {
        case EGF_TileType::Normal:
            return FColor::White;
        case EGF_TileType::Blocked:
            return FColor::Red;
        case EGF_TileType::Slope:
            return FColor::Yellow;
        case EGF_TileType::Ledge:
            return FColor::Orange;
        case EGF_TileType::Water:
            return FColor::Blue;
        case EGF_TileType::Item:
            return FColor::Magenta;
        case EGF_TileType::Door:
            return FColor::Cyan;
        case EGF_TileType::TallGrass:
            return FColor::Green;
        case EGF_TileType::Ice:
            return FColor(200, 240, 255);  // Light blue
        default:
            return FColor::White;
    }
}

void AGF_GridTileMarker::UpdateMarkerColor()
{
    #if WITH_EDITORONLY_DATA
    if (MarkerVolume)
    {
        MarkerVolume->ShapeColor = GetTileTypeColor();
    }
    #endif
}