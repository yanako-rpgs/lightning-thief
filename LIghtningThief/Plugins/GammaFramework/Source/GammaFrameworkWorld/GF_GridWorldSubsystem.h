#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Sound/SoundBase.h"
#include "GF_GridWorldSubsystem.generated.h"

//====================================================================================
// ENUMS
//====================================================================================

UENUM(BlueprintType)
enum class EGF_PlayerDirection : uint8
{
    North,      // Up (positive Y)
    South,      // Down (negative Y)
    East,       // Right (positive X)
    West        // Left (negative X)
};

UENUM(BlueprintType)
enum class EGF_TileType : uint8
{
    Normal,         // Standard walkable tile
    Blocked,        // Cannot walk on
    Slope,          // Connects two height levels
    Ledge,          // Can jump down (one-way movement)
    Water,          // Requires Surf
    Item,           // Contains an item to pick up
    Door,           // Can open/close
    TallGrass,      // Random encounters
    Ice             // Slides until hitting obstacle
};

//====================================================================================
// GRID COORDINATE STRUCT
//====================================================================================

USTRUCT(BlueprintType)
struct FGF_GridCoordinate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 X = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 Y = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 Z = 0;  // Height level for slopes (0=ground, 1=elevated, etc.)

    FGF_GridCoordinate() : X(0), Y(0), Z(0) {}
    FGF_GridCoordinate(int32 InX, int32 InY, int32 InZ = 0) : X(InX), Y(InY), Z(InZ) {}

    bool operator==(const FGF_GridCoordinate& Other) const
    {
        return X == Other.X && Y == Other.Y && Z == Other.Z;
    }

    bool operator!=(const FGF_GridCoordinate& Other) const
    {
        return !(*this == Other);
    }

    friend uint32 GetTypeHash(const FGF_GridCoordinate& Coord)
    {
        return HashCombine(HashCombine(GetTypeHash(Coord.X), GetTypeHash(Coord.Y)), GetTypeHash(Coord.Z));
    }

    FGF_GridCoordinate operator+(const FGF_GridCoordinate& Other) const
    {
        return FGF_GridCoordinate(X + Other.X, Y + Other.Y, Z + Other.Z);
    }

    FGF_GridCoordinate operator-(const FGF_GridCoordinate& Other) const
    {
        return FGF_GridCoordinate(X - Other.X, Y - Other.Y, Z - Other.Z);
    }

    // Distance calculation for pathfinding
    int32 ManhattanDistance(const FGF_GridCoordinate& Other) const
    {
        return FMath::Abs(X - Other.X) + FMath::Abs(Y - Other.Y) + FMath::Abs(Z - Other.Z);
    }

    FGF_GridCoordinate operator*(int32 Scalar) const
{
    return FGF_GridCoordinate(X * Scalar, Y * Scalar, Z * Scalar);
}

friend FGF_GridCoordinate operator*(int32 Scalar, const FGF_GridCoordinate& Coord)
{
    return FGF_GridCoordinate(Coord.X * Scalar, Coord.Y * Scalar, Coord.Z * Scalar);
}
};

//====================================================================================
// TILE PROPERTIES STRUCT
//====================================================================================

USTRUCT(BlueprintType)
struct FGF_TileProperties
{
    GENERATED_BODY()

    // Basic properties
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    EGF_TileType TileType = EGF_TileType::Normal;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bIsWalkable = true;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 HeightLevel = 0;  // 0=ground, 1=elevated, etc.

    // Slope properties
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bIsSlope = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    EGF_PlayerDirection SlopeDirection = EGF_PlayerDirection::North;  // Direction that goes UP

    // Ledge properties (one-way movement)
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bIsLedge = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    EGF_PlayerDirection LedgeJumpDirection = EGF_PlayerDirection::South;  // Direction you can jump

    // Item properties
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bHasItem = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FName ItemID = NAME_None;  // Item identifier (e.g., "Potion", "TM01")

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bItemPickedUp = false;  // Has this item been collected?

    // Interaction properties
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bHasDoor = false;

    // True if this door leads into a cave/dungeon. Entering it registers the
    // outdoor tile as the Escape Rope / Dig return point.
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bIsDungeonEntrance = false;

    // True if this door leads back out to the overworld. Entering it clears the
    // "in dungeon" state so the Escape Rope greys out again.
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bIsDungeonExit = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bHasInteractable = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bIsDoorOpen = false;

    // Sound to play the instant this door tile is activated (stepped on)
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    USoundBase* DoorSound = nullptr;

    // Seconds to wait after entry auto-steps before firing OnDoorEntered (0 = instant)
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    float DoorTransitionDelay = 0.0f;

    // Seconds to wait after teleport before the arrival auto-walk begins (0 = instant).
    // Player is already at the destination during this time, so the level streaming
    // volume is active and loading — use this to wait for it to finish.
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    float DoorArrivalDelay = 0.0f;

    // Door destination coord (2D, for grid registration)
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FGF_GridCoordinate DoorDestinationCoord;

    // Door destination exact world location from the marker's actor position — used for teleport, no trace needed
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FVector DoorDestinationWorldLocation = FVector::ZeroVector;

    // How many tiles the player auto-walks BEFORE the teleport (default 1)
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 EntryStepsBeforeTeleport = 1;

    // How many tiles the player auto-walks on arrival (0 = no auto-walk)
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DoorAutoSteps = 0;

    // When true, override the arrival facing/auto-walk direction instead of keeping the
    // direction the player entered with (e.g. enter North -> walk South after teleport).
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bDoorOverrideStepDirection = false;

    // Direction the player faces/auto-walks on arrival when bDoorOverrideStepDirection is true.
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    EGF_PlayerDirection DoorOverrideStepDirection = EGF_PlayerDirection::South;

    // Occupancy
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    AActor* OccupyingActor = nullptr;  // Which actor is on this tile

    // Follower system
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bIsPlayerFollower = false;  // Is the occupying actor a follower Creature?

    // Water/Surf properties
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bRequiresSurf = false;

    // Tall grass properties
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bCanTriggerEncounter = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    float EncounterChance = 0.1f;  // 10% chance per step

    FGF_TileProperties() {}
};

//====================================================================================
// PATHFINDING NODE (for A* algorithm)
//====================================================================================

USTRUCT()
struct FGF_PathNode
{
    GENERATED_BODY()

    FGF_GridCoordinate Coordinate;
    FGF_GridCoordinate Parent;
    int32 GCost = 0;  // Distance from start
    int32 HCost = 0;  // Distance to goal (heuristic)
    int32 FCost = 0;  // GCost + HCost

    FGF_PathNode() {}
    FGF_PathNode(FGF_GridCoordinate InCoord) : Coordinate(InCoord) {}

    void CalculateFCost()
    {
        FCost = GCost + HCost;
    }

    bool operator==(const FGF_PathNode& Other) const
    {
        return Coordinate == Other.Coordinate;
    }
};

//====================================================================================
// GRID WORLD SUBSYSTEM
//====================================================================================

UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_GridWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //====================================================================================
    // INITIALIZATION
    //====================================================================================

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    //====================================================================================
    // GRID PROPERTIES
    //====================================================================================

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grid")
    float TileSize = 64;

    //====================================================================================
    // COORDINATE CONVERSION
    //====================================================================================

    UFUNCTION(BlueprintPure, Category = "Grid")
    FGF_GridCoordinate WorldToGrid(FVector WorldLocation) const;

    UFUNCTION(BlueprintPure, Category = "Grid")
    FVector GridToWorld(FGF_GridCoordinate GridCoord) const;

    UFUNCTION(BlueprintPure, Category = "Grid")
    static FGF_GridCoordinate GetDirectionOffset(EGF_PlayerDirection Direction);

    UFUNCTION(BlueprintPure, Category = "Grid")
    static EGF_PlayerDirection GetOppositeDirection(EGF_PlayerDirection Direction);

    //====================================================================================
    // TILE QUERIES
    //====================================================================================

    UFUNCTION(BlueprintPure, Category = "Grid")
    bool IsTileWalkable(FGF_GridCoordinate Coord) const;

    UFUNCTION(BlueprintPure, Category = "Grid")
    bool IsTileOccupied(FGF_GridCoordinate Coord) const;

    UFUNCTION(BlueprintPure, Category = "Grid")
    FGF_TileProperties GetTileProperties(FGF_GridCoordinate Coord) const;

    UFUNCTION(BlueprintCallable, Category = "Grid")
    void SetTileProperties(FGF_GridCoordinate Coord, FGF_TileProperties Properties);

    // Check if movement is valid (considers ledges, slopes, surf requirement, etc.)
    UFUNCTION(BlueprintPure, Category = "Grid")
    bool CanMoveToTile(FGF_GridCoordinate From, FGF_GridCoordinate To, bool bHasSurf = false, bool bIgnoreOccupants = false) const;

    UFUNCTION(BlueprintCallable, Category = "Grid World")
	void ClearActorFromTile(FGF_GridCoordinate Coord);

    //====================================================================================
    // SLOPE & LEDGE LOGIC
    //====================================================================================

    // Check if moving from one tile to another is a valid slope transition
    UFUNCTION(BlueprintPure, Category = "Grid|Slopes")
    bool IsValidSlopeTransition(FGF_GridCoordinate From, FGF_GridCoordinate To) const;

    // Check if this is a valid ledge jump
    UFUNCTION(BlueprintPure, Category = "Grid|Ledges")
    bool CanJumpLedge(FGF_GridCoordinate From, FGF_GridCoordinate To, EGF_PlayerDirection Direction) const;

    //====================================================================================
    // ITEM SYSTEM
    //====================================================================================

    UFUNCTION(BlueprintCallable, Category = "Grid|Items")
    bool PickUpItem(FGF_GridCoordinate Coord);

    UFUNCTION(BlueprintPure, Category = "Grid|Items")
    bool HasItemAtTile(FGF_GridCoordinate Coord) const;

    UFUNCTION(BlueprintPure, Category = "Grid|Items")
    FName GetItemIDAtTile(FGF_GridCoordinate Coord) const;

    //====================================================================================
    // OCCUPANCY MANAGEMENT
    //====================================================================================

    UFUNCTION(BlueprintCallable, Category = "Grid")
    void RegisterEntityOnTile(AActor* Entity, FGF_GridCoordinate Coord);

    UFUNCTION(BlueprintCallable, Category = "Grid")
    void UnregisterEntityOnTile(AActor* Entity, FGF_GridCoordinate Coord);

    // Clears the actor's occupancy from EVERY tile it is registered on. Use on destroy/despawn
    // to guarantee no tile is left occupied, regardless of which tile the actor was registered
    // on (e.g. its in-flight target tile if it was mid-move when removed).
    UFUNCTION(BlueprintCallable, Category = "Grid")
    void ClearActorFromAllTiles(AActor* Actor);

    //UFUNCTION(BlueprintCallable, Category = "Grid System")
	//void UnregisterFollowerOnTile(AActor* Actor, FGF_GridCoordinate Coord);

    UFUNCTION(BlueprintPure, Category = "Grid")
    AActor* GetActorOnTile(FGF_GridCoordinate Coord) const;

    UFUNCTION(BlueprintCallable, Category = "Grid")
    void RegisterFollowerOnTile(AActor* Follower, FGF_GridCoordinate Coord);

    //====================================================================================
    // FOLLOWER PLACEMENT
    //====================================================================================

    // True when nothing is standing on Coord. Two checks, because either one alone lies:
    //  - the tile map (authoritative for anything grid-driven), and
    //  - a physical pawn sweep, which catches actors the tile map never knew about (an NPC
    //    still initialising, or one already nudged off its tile).
    // ReferenceActor supplies the height to sweep at (pass the player) and is never counted
    // as an occupant; IgnoreActor is skipped as well - pass the actor being placed.
    // Coord's Z is ignored: the tile map is only ever keyed at Z=0.
    UFUNCTION(BlueprintPure, Category = "Grid|Follower")
    bool IsTileFreeToStandOn(FGF_GridCoordinate Coord, AActor* ReferenceActor, AActor* IgnoreActor) const;

    // Picks a free tile next to the player to let a follower out on: behind first, then either
    // side, then in front. OutLocation is the tile centre with DesiredLocation's Z kept, so the
    // caller's own height offset survives - feed it straight into SpawnActor's location pin.
    // Returns false when every neighbour is blocked or taken, in which case NOTHING should be
    // spawned: placing the follower anyway is what shoves NPCs off their tiles.
    UFUNCTION(BlueprintCallable, Category = "Grid|Follower")
    bool GetFollowerSpawnLocation(AActor* PlayerActor, FVector DesiredLocation, FVector& OutLocation,
                                  FGF_GridCoordinate& OutTile, bool bAllowTileInFront = true) const;

    //====================================================================================
    // PATHFINDING (A* Algorithm)
    //====================================================================================

    // Find path from Start to Goal (returns empty array if no path found)
    UFUNCTION(BlueprintCallable, Category = "Grid|Pathfinding")
    // bIgnoreOccupants: when true, the path may route through tiles occupied by other entities
    // (used for choreographed cutscene movement, e.g. an NPC shoving past the player).
    TArray<FGF_GridCoordinate> FindPath(FGF_GridCoordinate Start, FGF_GridCoordinate Goal, bool bHasSurf = false, bool bPreferOpenSpace = false, bool bIgnoreOccupants = false);

    // Get all walkable neighbors of a tile (for pathfinding)
    UFUNCTION(BlueprintPure, Category = "Grid|Pathfinding")
    TArray<FGF_GridCoordinate> GetWalkableNeighbors(FGF_GridCoordinate Coord, bool bHasSurf = false, bool bIgnoreOccupants = false) const;

protected:
    //====================================================================================
    // TILE DATA STORAGE
    //====================================================================================

    UPROPERTY()
    TMap<FGF_GridCoordinate, FGF_TileProperties> TileMap;

    FGF_TileProperties DefaultTileProperties;



    //====================================================================================
    // PATHFINDING HELPERS
    //====================================================================================

    FGF_PathNode* FindLowestFCostNode(TArray<FGF_PathNode>& NodeList);
    void ReconstructPath(const TMap<FGF_GridCoordinate, FGF_GridCoordinate>& CameFrom, FGF_GridCoordinate Current, TArray<FGF_GridCoordinate>& OutPath);
};