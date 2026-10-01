#include "GF_GridWorldSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"
#include "GF_GridMovementComponent.h"

//====================================================================================
// INITIALIZATION
//====================================================================================

void UGF_GridWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogTemp, Log, TEXT("GridWorldSubsystem: Initialized (TileSize: %.1f)"), TileSize);

    // Set default tile properties
    DefaultTileProperties.bIsWalkable = true;
    DefaultTileProperties.TileType = EGF_TileType::Normal;
    DefaultTileProperties.HeightLevel = 0;
}

void UGF_GridWorldSubsystem::Deinitialize()
{
    TileMap.Empty();
    UE_LOG(LogTemp, Log, TEXT("GridWorldSubsystem: Deinitialized"));

    Super::Deinitialize();
}

//====================================================================================
// COORDINATE CONVERSION
//====================================================================================

FGF_GridCoordinate UGF_GridWorldSubsystem::WorldToGrid(FVector WorldLocation) const
{
    int32 X = FMath::RoundToInt(WorldLocation.X / TileSize);
    int32 Y = FMath::RoundToInt(WorldLocation.Y / TileSize);
    int32 Z = FMath::RoundToInt(WorldLocation.Z / TileSize);

    return FGF_GridCoordinate(X, Y, Z);
}

FVector UGF_GridWorldSubsystem::GridToWorld(FGF_GridCoordinate GridCoord) const
{
    float X = GridCoord.X * TileSize;
    float Y = GridCoord.Y * TileSize;
    float Z = GridCoord.Z * TileSize;

    return FVector(X, Y, Z);
}

FGF_GridCoordinate UGF_GridWorldSubsystem::GetDirectionOffset(EGF_PlayerDirection Direction)
{
    switch (Direction)
    {
        case EGF_PlayerDirection::North:
            return FGF_GridCoordinate(-1, 0, 0);   // Up on screen
        case EGF_PlayerDirection::South:
            return FGF_GridCoordinate(1, 0, 0);    // Down on screen
        case EGF_PlayerDirection::East:
            return FGF_GridCoordinate(0, -1, 0);   // Right on screen
        case EGF_PlayerDirection::West:
            return FGF_GridCoordinate(0, 1, 0);    // Left on screen
        default:
            return FGF_GridCoordinate(0, 0, 0);
    }
}

EGF_PlayerDirection UGF_GridWorldSubsystem::GetOppositeDirection(EGF_PlayerDirection Direction)
{
    switch (Direction)
    {
        case EGF_PlayerDirection::North:
            return EGF_PlayerDirection::South;
        case EGF_PlayerDirection::South:
            return EGF_PlayerDirection::North;
        case EGF_PlayerDirection::East:
            return EGF_PlayerDirection::West;
        case EGF_PlayerDirection::West:
            return EGF_PlayerDirection::East;
        default:
            return Direction;
    }
}

//====================================================================================
// TILE QUERIES
//====================================================================================

bool UGF_GridWorldSubsystem::IsTileWalkable(FGF_GridCoordinate Coord) const
{
    const FGF_TileProperties* Props = TileMap.Find(Coord);
    if (Props)
    {
        return Props->bIsWalkable;
    }
    return DefaultTileProperties.bIsWalkable;
}

bool UGF_GridWorldSubsystem::IsTileOccupied(FGF_GridCoordinate Coord) const
{
    const FGF_TileProperties* Props = TileMap.Find(Coord);
    if (Props)
    {
        if (Props->bIsPlayerFollower)
            return false;

        return Props->OccupyingActor != nullptr;
    }
    return false;
}

FGF_TileProperties UGF_GridWorldSubsystem::GetTileProperties(FGF_GridCoordinate Coord) const
{
    const FGF_TileProperties* Props = TileMap.Find(Coord);
    if (Props)
    {
        // Always log for (257, 1465) to debug the ledge issue
        if (Coord.X == 257 && Coord.Y == 1465)
        {
           // UE_LOG(LogTemp, Warning, TEXT("🔍 GetTileProperties (257, 1465): FOUND! IsLedge=%s, TileType=%s, Occupied=%s"),
            //    Props->bIsLedge ? TEXT("YES") : TEXT("NO"),
              //  *UEnum::GetValueAsString(Props->TileType),
              //  Props->OccupyingActor ? *Props->OccupyingActor->GetName() : TEXT("NO"));
        }
        else
        {
           // UE_LOG(LogTemp, Warning, TEXT("GetTileProperties (%d, %d): IsLedge=%s, TileType=%s, Occupied=%s"),
       // Coord.X, Coord.Y,
      //  Props->bIsLedge ? TEXT("YES") : TEXT("NO"),
      //  *UEnum::GetValueAsString(Props->TileType),
      //  Props->OccupyingActor ? TEXT("YES") : TEXT("NO"));
        }
        return *Props;
    }

    // Log when tile not found
    if (Coord.X == 257 && Coord.Y == 1465)
    {
        //UE_LOG(LogTemp, Error, TEXT("❌❌❌ GetTileProperties (257, 1465): TILE NOT FOUND! Returning defaults. TileMap size: %d"),
           // TileMap.Num());
    }
    else
    {
        UE_LOG(LogTemp, VeryVerbose, TEXT("GetTileProperties (%d, %d): TILE NOT FOUND - returning defaults"),
            Coord.X, Coord.Y);
    }
    return DefaultTileProperties;
}

void UGF_GridWorldSubsystem::SetTileProperties(FGF_GridCoordinate Coord, FGF_TileProperties Properties)
{
    // Check if we're overwriting an existing tile
    FGF_TileProperties* ExistingProps = TileMap.Find(Coord);

    if (ExistingProps)
    {
        // Preserve the occupying actor when overwriting tile properties
        if (ExistingProps->OccupyingActor && !Properties.OccupyingActor)
            Properties.OccupyingActor = ExistingProps->OccupyingActor;
    }

    TileMap.Add(Coord, Properties);
}

void UGF_GridWorldSubsystem::ClearActorFromTile(FGF_GridCoordinate Coord)
{
	//TileMap.Remove(Coord);

    FGF_TileProperties* Props = TileMap.Find(Coord);
    if (Props)
    {
        Props->OccupyingActor = nullptr;  // Preserve all other properties!
    }



}

bool UGF_GridWorldSubsystem::CanMoveToTile(FGF_GridCoordinate From, FGF_GridCoordinate To, bool bHasSurf, bool bIgnoreOccupants) const
{


    // Get tile properties
    FGF_TileProperties ToProps = GetTileProperties(To);

    //  Check if tile is walkable
    if (!ToProps.bIsWalkable)
    {
      //  UE_LOG(LogTemp, Verbose, TEXT("CanMoveToTile: Target (%d, %d) is not walkable"), To.X, To.Y);
        return false;
    }

    //  Check if tile is occupied by another entity (follower counts as solid).
    //  Skipped when bIgnoreOccupants — choreographed cutscene moves may pass through actors.
    if (!bIgnoreOccupants && ToProps.OccupyingActor != nullptr)
    {
       // UE_LOG(LogTemp, Verbose, TEXT("CanMoveToTile: Target (%d, %d) is occupied"), To.X, To.Y);
        return false;
    }

    //  Check surf requirement
    if (ToProps.bRequiresSurf && !bHasSurf)
    {
       // UE_LOG(LogTemp, Verbose, TEXT("CanMoveToTile: Target (%d, %d) requires Surf"), To.X, To.Y);
        return false;
    }

    //  Check slope transitions
    if (ToProps.bIsSlope || GetTileProperties(From).bIsSlope)
    {
        if (!IsValidSlopeTransition(From, To))
        {
          //  UE_LOG(LogTemp, Verbose, TEXT("CanMoveToTile: Invalid slope transition from (%d, %d) to (%d, %d)"),
            //    From.X, From.Y, To.X, To.Y);
            return false;
        }
    }

    if (!bIgnoreOccupants)
    {
        FGF_GridCoordinate FlatTo = To;
        FlatTo.Z = 0;

        AActor* ActorOnTile = GetActorOnTile(FlatTo);
        if (ActorOnTile != nullptr)
        {
            return false;  // Tile is blocked (any occupant, including follower)
        }
    }

    return true;
}

//====================================================================================
// SLOPE & LEDGE LOGIC
//====================================================================================

bool UGF_GridWorldSubsystem::IsValidSlopeTransition(FGF_GridCoordinate From, FGF_GridCoordinate To) const
{
    FGF_TileProperties FromProps = GetTileProperties(From);
    FGF_TileProperties ToProps = GetTileProperties(To);

    // Calculate direction of movement
    FGF_GridCoordinate Direction = To - From;

    // If moving to a slope, check if we're entering from the correct direction
    if (ToProps.bIsSlope)
    {
        FGF_GridCoordinate SlopeOffset = GetDirectionOffset(ToProps.SlopeDirection);

        // Can enter slope from the bottom (opposite of slope direction)
        if (Direction == (SlopeOffset * -1))
        {
            return true;
        }
    }

    // If leaving a slope, check if we're exiting from the correct direction
    if (FromProps.bIsSlope)
    {
        FGF_GridCoordinate SlopeOffset = GetDirectionOffset(FromProps.SlopeDirection);

        // Can exit slope from the top (same as slope direction)
        if (Direction == SlopeOffset)
        {
            return true;
        }
    }

    // Not a slope transition - allow
    if (!FromProps.bIsSlope && !ToProps.bIsSlope)
    {
        return true;
    }

    return false;
}

bool UGF_GridWorldSubsystem::CanJumpLedge(FGF_GridCoordinate From, FGF_GridCoordinate To, EGF_PlayerDirection Direction) const
{
    FGF_TileProperties FromProps = GetTileProperties(From);

    // Check if current tile is a ledge
    if (!FromProps.bIsLedge)
    {
        return false;
    }

    // Check if moving in the correct jump direction
    if (Direction != FromProps.LedgeJumpDirection)
    {
        return false;
    }

    // Check if destination is 2 tiles away in jump direction
    FGF_GridCoordinate Offset = GetDirectionOffset(Direction);
    FGF_GridCoordinate ExpectedTarget = From + (Offset * 2);

    if (To != ExpectedTarget)
    {
        return false;
    }

    // Target must be walkable
    FGF_TileProperties ToProps = GetTileProperties(To);
    if (!ToProps.bIsWalkable || ToProps.OccupyingActor != nullptr)
    {
        return false;
    }

    return true;
}

//====================================================================================
// ITEM SYSTEM
//====================================================================================

bool UGF_GridWorldSubsystem::PickUpItem(FGF_GridCoordinate Coord)
{
    FGF_TileProperties* Props = TileMap.Find(Coord);
    if (!Props)
        return false;

    if (!Props->bHasItem || Props->bItemPickedUp)
        return false;

    Props->bItemPickedUp = true;
    UE_LOG(LogTemp, Log, TEXT("GridWorldSubsystem: Item picked up at (%d, %d): %s"),
        Coord.X, Coord.Y, *Props->ItemID.ToString());

    return true;
}

bool UGF_GridWorldSubsystem::HasItemAtTile(FGF_GridCoordinate Coord) const
{
    const FGF_TileProperties* Props = TileMap.Find(Coord);
    if (!Props)
        return false;

    return Props->bHasItem && !Props->bItemPickedUp;
}

FName UGF_GridWorldSubsystem::GetItemIDAtTile(FGF_GridCoordinate Coord) const
{
    const FGF_TileProperties* Props = TileMap.Find(Coord);
    if (!Props)
        return NAME_None;

    if (Props->bHasItem && !Props->bItemPickedUp)
    {
        return Props->ItemID;
    }

    return NAME_None;
}

//====================================================================================
// OCCUPANCY MANAGEMENT
//====================================================================================

void UGF_GridWorldSubsystem::RegisterEntityOnTile(AActor* Entity, FGF_GridCoordinate Coord)
{
    if (!Entity)
        return;

    FGF_TileProperties* Props = TileMap.Find(Coord);
    if (Props)
    {
        // Tile exists - just update the occupying actor
        Props->OccupyingActor = Entity;

        // This entry point is for everything that is NOT the follower (RegisterFollowerOnTile
        // is its counterpart), so a leftover follower flag from a previous occupant would
        // describe the wrong actor.
        Props->bIsPlayerFollower = false;
    }
    else
    {
        // Tile doesn't exist - this shouldn't happen if markers ran first
        // DON'T create a new tile - just log a warning
        ////UE_LOG(LogTemp, Warning, TEXT("RegisterEntity at (%d, %d): Tile doesn't exist! Entity may be on unmarked tile."),
           // Coord.X, Coord.Y);

        // We could create a minimal tile here, but it's better to leave it undefined
        FGF_TileProperties NewProps = DefaultTileProperties;
        NewProps.OccupyingActor = Entity;
        TileMap.Add(Coord, NewProps);
    }
}

void UGF_GridWorldSubsystem::ClearActorFromAllTiles(AActor* Actor)
{
    if (!Actor)
        return;

    // Sweep every registered tile and free any that this actor occupies. O(tiles), but only
    // runs on destroy/despawn (rare), so the cost is fine. This is the reliable cleanup: it
    // doesn't depend on CurrentGridPosition/TargetGridPosition being accurate at destroy time.
    for (TPair<FGF_GridCoordinate, FGF_TileProperties>& Pair : TileMap)
    {
        if (Pair.Value.OccupyingActor == Actor)
        {
            Pair.Value.OccupyingActor    = nullptr;
            Pair.Value.bIsPlayerFollower = false;
        }
    }
}

void UGF_GridWorldSubsystem::UnregisterEntityOnTile(AActor* Entity, FGF_GridCoordinate Coord)
{
    if (!Entity)
        return;

    FGF_TileProperties* Props = TileMap.Find(Coord);
    if (Props && Props->OccupyingActor == Entity)
    {
        // CRITICAL: Only clear occupying actor, preserve ALL other properties
        Props->OccupyingActor = nullptr;

        // ...and the follower flag, which describes the occupant, not the tile. Leaving it
        // set marked every tile the follower had ever stood on as "a follower is here"
        // forever: IsTileOccupied returned false for all of them (so NPCs standing there
        // stopped blocking), and TryMove's swap branch fired against the player's own tile.
        Props->bIsPlayerFollower = false;
    }
}


AActor* UGF_GridWorldSubsystem::GetActorOnTile(FGF_GridCoordinate Coord) const
{
    const FGF_TileProperties* Props = TileMap.Find(Coord);
    if (Props)
    {
        return Props->OccupyingActor;
    }
    return nullptr;
}

void UGF_GridWorldSubsystem::RegisterFollowerOnTile(AActor* Follower, FGF_GridCoordinate Coord)
{
    if (!Follower)
        return;

    FGF_TileProperties* Props = TileMap.Find(Coord);
    if (Props)
    {
        // Mark tile as occupied by follower
        Props->OccupyingActor = Follower;
        Props->bIsPlayerFollower = true;
    }
    else
    {
        // Create tile if it doesn't exist
        FGF_TileProperties NewProps = DefaultTileProperties;
        NewProps.OccupyingActor = Follower;
        NewProps.bIsPlayerFollower = true;
        TileMap.Add(Coord, NewProps);
    }
}

//====================================================================================
// FOLLOWER PLACEMENT
//====================================================================================

bool UGF_GridWorldSubsystem::IsTileFreeToStandOn(FGF_GridCoordinate Coord, AActor* ReferenceActor, AActor* IgnoreActor) const
{
    // Every register/unregister call flattens to Z=0, so the tile map is only ever keyed at
    // Z=0. Looking it up with a live grid Z (-1 on a map whose ground sits a tile below world
    // zero) misses the map entirely, and the misses are PERMISSIVE: IsTileWalkable falls back
    // to "walkable" and IsTileOccupied returns "empty". Flatten before touching the map.
    Coord.Z = 0;

    if (!IsTileWalkable(Coord))
        return false;

    // A ledge is a one-way hop, not somewhere to stand.
    const FGF_TileProperties Props = GetTileProperties(Coord);
    if (Props.bIsLedge)
        return false;

    // GetActorOnTile, not IsTileOccupied: IsTileOccupied deliberately reports follower-flagged
    // tiles as free so the player can walk through their own Creature, which would hide a
    // genuine occupant from us here.
    if (const AActor* Occupant = GetActorOnTile(Coord))
    {
        if (Occupant != IgnoreActor)
            return false;
    }

    // Physical backstop for anything the tile map doesn't know about. Those actors are
    // invisible to the grid but still depenetrate against whatever we place here, which is
    // exactly what nudges NPCs off their tiles.
    const UWorld* World = GetWorld();
    if (World && ReferenceActor)
    {
        FVector Probe = GridToWorld(Coord);
        Probe.Z = ReferenceActor->GetActorLocation().Z;

        FCollisionQueryParams Params(SCENE_QUERY_STAT(GridTileFree), false, ReferenceActor);
        if (IgnoreActor)
            Params.AddIgnoredActor(IgnoreActor);

        FCollisionObjectQueryParams ObjParams;
        ObjParams.AddObjectTypesToQuery(ECC_Pawn);

        // Keep the sphere well inside the tile so a pawn standing on the NEXT tile over (one
        // full TileSize away) can't read as occupying this one.
        TArray<FOverlapResult> Overlaps;
        if (World->OverlapMultiByObjectType(Overlaps, Probe, FQuat::Identity, ObjParams,
                FCollisionShape::MakeSphere(TileSize * 0.25f), Params))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                const AActor* Other = Overlap.GetActor();
                if (Other && Other != ReferenceActor && Other != IgnoreActor)
                    return false;
            }
        }
    }

    return true;
}

bool UGF_GridWorldSubsystem::GetFollowerSpawnLocation(AActor* PlayerActor, FVector DesiredLocation, FVector& OutLocation,
                                                   FGF_GridCoordinate& OutTile, bool bAllowTileInFront) const
{
    // Caller gets its original location back on failure so a mis-wired Branch can't spawn the
    // follower at the world origin.
    OutLocation = DesiredLocation;
    OutTile = FGF_GridCoordinate();

    if (!PlayerActor)
    {
        // Loud on purpose. This is reached from Blueprint, where an unconnected PlayerActor pin
        // looks identical to a connected one at a glance, and the only symptom is this function
        // quietly returning false. BP_Follower_Master gated its BeginPlay bindings on that return
        // value, so a blank pin meant the follower never bound OnDialogueFinished and every
        // conversation with it left the player permanently unable to move, with nothing logged.
        UE_LOG(LogTemp, Warning,
            TEXT("GetFollowerSpawnLocation: called with a null PlayerActor - returning false. ")
            TEXT("Check that the caller's PlayerActor pin is connected."));
        return false;
    }

    const UGF_GridMovementComponent* PlayerMovement = PlayerActor->FindComponentByClass<UGF_GridMovementComponent>();
    if (!PlayerMovement)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetFollowerSpawnLocation: %s has no GridMovementComponent."), *PlayerActor->GetName());
        return false;
    }

    FGF_GridCoordinate PlayerPos = PlayerMovement->GetCurrentGridPosition();
    PlayerPos.Z = 0;
    const EGF_PlayerDirection Facing = PlayerMovement->GetCurrentFacing();

    // Water is only a legal tile if the player is out on the water too, so match against
    // whatever the player is currently standing on rather than hard-coding "no surf tiles".
    const bool bPlayerNeedsSurf = GetTileProperties(PlayerPos).bRequiresSurf;

    // Behind first, then either side, then in front - the tile in front is the least natural
    // place for a Creature to appear, so it is the last resort rather than a coin flip.
    const bool bFacingVertical = (Facing == EGF_PlayerDirection::North || Facing == EGF_PlayerDirection::South);
    TArray<EGF_PlayerDirection> DirectionsToTry;
    DirectionsToTry.Add(GetOppositeDirection(Facing));
    DirectionsToTry.Add(bFacingVertical ? EGF_PlayerDirection::East : EGF_PlayerDirection::North);
    DirectionsToTry.Add(bFacingVertical ? EGF_PlayerDirection::West : EGF_PlayerDirection::South);
    if (bAllowTileInFront)
        DirectionsToTry.Add(Facing);

    for (EGF_PlayerDirection Dir : DirectionsToTry)
    {
        const FGF_GridCoordinate Candidate = PlayerPos + GetDirectionOffset(Dir);

        if (GetTileProperties(Candidate).bRequiresSurf != bPlayerNeedsSurf)
            continue;

        if (!IsTileFreeToStandOn(Candidate, PlayerActor, nullptr))
            continue;

        const FVector TileCentre = GridToWorld(Candidate);
        OutLocation = FVector(TileCentre.X, TileCentre.Y, DesiredLocation.Z);
        OutTile = Candidate;

        UE_LOG(LogTemp, Warning, TEXT("GetFollowerSpawnLocation: player tile (%d,%d) facing %d -> free tile (%d,%d)"),
            PlayerPos.X, PlayerPos.Y, (int32)Facing, Candidate.X, Candidate.Y);
        return true;
    }

    UE_LOG(LogTemp, Warning, TEXT("GetFollowerSpawnLocation: no free tile around player tile (%d,%d) - do not spawn."),
        PlayerPos.X, PlayerPos.Y);
    return false;
}

//====================================================================================
// PATHFINDING (A* Algorithm)
//====================================================================================

TArray<FGF_GridCoordinate> UGF_GridWorldSubsystem::FindPath(FGF_GridCoordinate Start, FGF_GridCoordinate Goal, bool bHasSurf, bool bPreferOpenSpace, bool bIgnoreOccupants)
{
    TArray<FGF_GridCoordinate> Path;




    FGF_GridCoordinate FlatGoal = Goal;
    FlatGoal.Z = 0;

    FGF_GridCoordinate FlatStart = Start;
    FlatStart.Z = 0;

    AActor* GoalActor = GetActorOnTile(Goal);





    // Check tile properties
    FGF_TileProperties StartProps = GetTileProperties(FlatStart);
    FGF_TileProperties GoalProps = GetTileProperties(FlatGoal);



    // Check neighbors
    TArray<FGF_GridCoordinate> StartNeighbors = GetWalkableNeighbors(FlatStart, bHasSurf, bIgnoreOccupants);

    for (int32 i = 0; i < FMath::Min(StartNeighbors.Num(), 5); i++)
    {

    }



    // Early exit if start or goal is invalid. Goal occupancy is ignored when bIgnoreOccupants
    // (the choreographed mover is allowed to end on / pass through an occupied tile).
    if (!IsTileWalkable(FlatStart) || !IsTileWalkable(FlatGoal) || (!bIgnoreOccupants && IsTileOccupied(FlatGoal)))
		{
		    return Path;
		}

    // A* algorithm
    TArray<FGF_PathNode> OpenList;
    TSet<FGF_GridCoordinate> ClosedSet;  // FIXED: Use TSet instead of TArray for fast lookup
    TMap<FGF_GridCoordinate, FGF_GridCoordinate> CameFrom;

    // Add start node
    FGF_PathNode StartNode(FlatStart);
    StartNode.GCost = 0;
    StartNode.HCost = FlatStart.ManhattanDistance(FlatGoal);
    StartNode.CalculateFCost();
    OpenList.Add(StartNode);

    int32 Iterations = 0;
    int32 Distance = FlatStart.ManhattanDistance(FlatGoal);
	const int32 MaxIterations = FMath::Max(1000, Distance * 100);  // More iterations for farther goals

	//UE_LOG(LogTemp, Log, TEXT("FindPath: Distance=%d, MaxIterations=%d"), Distance, MaxIterations);

    while (OpenList.Num() > 0 && Iterations < MaxIterations)
    {
        Iterations++;

        // Find node with lowest FCost BY INDEX
        int32 LowestIndex = 0;
        int32 LowestFCost = OpenList[0].FCost;

        for (int32 i = 1; i < OpenList.Num(); i++)
        {
            if (OpenList[i].FCost < LowestFCost)
            {
                LowestFCost = OpenList[i].FCost;
                LowestIndex = i;
            }
        }

        // Copy node by VALUE (not pointer!)
        FGF_PathNode CurrentNode = OpenList[LowestIndex];
        FGF_GridCoordinate Current = CurrentNode.Coordinate;

        // Found goal?
        if (Current == FlatGoal)
        {
            ReconstructPath(CameFrom, Current, Path);

            return Path;
        }

        // Remove by index (safe!)
        OpenList.RemoveAt(LowestIndex);

        // Add to closed set (fast Contains check)
        ClosedSet.Add(Current);

        // Check all neighbors
        TArray<FGF_GridCoordinate> Neighbors = GetWalkableNeighbors(Current, bHasSurf, bIgnoreOccupants);

        for (const FGF_GridCoordinate& Neighbor : Neighbors)
        {
            if (ClosedSet.Contains(Neighbor))
                continue;

            // Wall proximity penalty — only applied for autonomous AI patrol paths
            // (bPreferOpenSpace=true). Scripted NPCMoveTo calls use the default (false)
            // so tamer approach and cutscene movement are never affected.
            int32 WallPenalty = 0;
            if (bPreferOpenSpace)
            {
                static const FGF_GridCoordinate CardinalOffsets[4] = {
                    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}
                };
                int32 WallNeighborCount = 0;
                for (const FGF_GridCoordinate& Offset : CardinalOffsets)
                {
                    FGF_GridCoordinate Adjacent(Neighbor.X + Offset.X, Neighbor.Y + Offset.Y, 0);
                    if (!IsTileWalkable(Adjacent))
                        WallNeighborCount++;
                }
                constexpr int32 WallPenaltyPerTile = 3;
                WallPenalty = WallNeighborCount * WallPenaltyPerTile;
            }

            // Calculate tentative GCost
            int32 TentativeGCost = CurrentNode.GCost + 1 + WallPenalty;

            // Find if neighbor is already in open list
            FGF_PathNode* NeighborNode = nullptr;
            for (FGF_PathNode& OpenNode : OpenList)
            {
                if (OpenNode.Coordinate == Neighbor)
                {
                    NeighborNode = &OpenNode;
                    break;
                }
            }

            if (!NeighborNode)
            {
                // Add to open list
                FGF_PathNode NewNode(Neighbor);
                NewNode.GCost = TentativeGCost;
                NewNode.HCost = Neighbor.ManhattanDistance(FlatGoal);
                NewNode.CalculateFCost();
                OpenList.Add(NewNode);
                CameFrom.Add(Neighbor, Current);
            }
            else if (TentativeGCost < NeighborNode->GCost)
            {
                // Update existing node
                NeighborNode->GCost = TentativeGCost;
                NeighborNode->CalculateFCost();
                CameFrom.Add(Neighbor, Current);
            }
        }
    }



    // Show some tiles from ClosedSet to understand what was explored
    int32 Count = 0;

    return Path;
}

TArray<FGF_GridCoordinate> UGF_GridWorldSubsystem::GetWalkableNeighbors(FGF_GridCoordinate Coord, bool bHasSurf, bool bIgnoreOccupants) const
{
    TArray<FGF_GridCoordinate> Neighbors;
	FGF_TileProperties CurrentProps = GetTileProperties(Coord);




    if (CurrentProps.bIsLedge)
    {
        FGF_GridCoordinate LedgeOffset = GetDirectionOffset(CurrentProps.LedgeJumpDirection);
        FGF_GridCoordinate LedgeTarget = Coord + (LedgeOffset * 2);

        if (CanMoveToTile(Coord, LedgeTarget, bHasSurf, bIgnoreOccupants))
        {
            Neighbors.Add(LedgeTarget);
        }

        // Can also move in other directions (walk off ledge edge)
        TArray<FGF_GridCoordinate> NormalOffsets = {
            FGF_GridCoordinate(-1, 0, 0), FGF_GridCoordinate(1, 0, 0),
            FGF_GridCoordinate(0, -1, 0), FGF_GridCoordinate(0, 1, 0)
        };

        for (const FGF_GridCoordinate& Offset : NormalOffsets)
        {
            if (Offset == LedgeOffset)
                continue;

            FGF_GridCoordinate Neighbor = Coord + Offset;
            if (CanMoveToTile(Coord, Neighbor, bHasSurf, bIgnoreOccupants))
            {
                Neighbors.Add(Neighbor);
            }
        }
    }
    else
    {

        TArray<FGF_GridCoordinate> Offsets = {
            FGF_GridCoordinate(-1, 0, 0), FGF_GridCoordinate(1, 0, 0),
            FGF_GridCoordinate(0, -1, 0), FGF_GridCoordinate(0, 1, 0)
        };

        for (const FGF_GridCoordinate& Offset : Offsets)
        {
            FGF_GridCoordinate Neighbor = Coord + Offset;
            FGF_TileProperties NeighborProps = GetTileProperties(Neighbor);

            // Don't walk backwards onto a ledge
            if (NeighborProps.bIsLedge)
            {
                FGF_GridCoordinate LedgeJumpOffset = GetDirectionOffset(NeighborProps.LedgeJumpDirection);
                FGF_GridCoordinate OppositeOffset = LedgeJumpOffset * -1;

                if (Offset == OppositeOffset)
                {
                    continue;  // Skip backwards ledge approach
                }
            }

            if (CanMoveToTile(Coord, Neighbor, bHasSurf, bIgnoreOccupants))
            {
                Neighbors.Add(Neighbor);
            }
        }
    }

    return Neighbors;
}

//====================================================================================
// PATHFINDING HELPERS
//====================================================================================

FGF_PathNode* UGF_GridWorldSubsystem::FindLowestFCostNode(TArray<FGF_PathNode>& NodeList)
{
    if (NodeList.Num() == 0)
        return nullptr;

    FGF_PathNode* LowestNode = &NodeList[0];
    for (int32 i = 1; i < NodeList.Num(); i++)
    {
        if (NodeList[i].FCost < LowestNode->FCost)
        {
            LowestNode = &NodeList[i];
        }
    }
    return LowestNode;
}

void UGF_GridWorldSubsystem::ReconstructPath(const TMap<FGF_GridCoordinate, FGF_GridCoordinate>& CameFrom, FGF_GridCoordinate Current, TArray<FGF_GridCoordinate>& OutPath)
{
    OutPath.Add(Current);
    while (CameFrom.Contains(Current))
    {
        Current = CameFrom[Current];
        OutPath.Insert(Current, 0);
    }
}