// GF_BerryGrowthSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "GF_ResettableState.h"
#include "GF_BerryGrowthSubsystem.generated.h"

class AActor;

/**
 * One berry tree's growth cycle.
 *
 * A tree only gets an entry once it has been harvested at least once. No entry
 * means "never touched", which reads as fully grown, so the world's trees do not
 * need to be pre-registered and adding a new one to a level needs no migration.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKWORLD_API FGF_BerryTreeState
{
    GENERATED_BODY()

    /** Stable identity of the placed actor. See UGF_BerryGrowthSubsystem::MakeBerryTreeID. */
    UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Berry")
    FName TreeID;

    /** Which berry this tree bears. */
    UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Berry")
    FName BerryName;

    /**
     * Real-world UTC when this growth cycle started (i.e. when it was harvested).
     *
     * REAL time, deliberately, and nothing to do with the sky's day/night clock -
     * see the class comment.
     */
    UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Berry")
    FDateTime CycleStartRealUtc = FDateTime(0);

    /** Purely for stats/debug. */
    UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Berry")
    int32 TimesHarvested = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnBerryDataChanged);

/** Disk format for berry growth. Own slot, written from UGF_VaultSystem::SaveToDisk. */
UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_BerrySave : public USaveGame
{
    GENERATED_BODY()

public:
    static const FString SlotName;
    static const int32   UserIndex;

    UPROPERTY()
    TArray<FGF_BerryTreeState> BerryTrees;
};

/**
 * Remembers which berry trees have been picked, and how long ago.
 *
 * THIS IS NOT A CLOCK, AND IT DOES NOT TOUCH THE SKY
 * --------------------------------------------------
 * BP_StylizedSky owns the time of day and the day/night cycle. Nothing here reads
 * it, writes to it, or competes with it. If you want to know what time it is, ask
 * the sky - that is still the one and only answer in this project.
 *
 * What the sky cannot do is remember. It is a level actor: destroyed and rebuilt
 * every time the map opens, so it cannot know that a tree was picked twenty
 * minutes ago, and it cannot know the game was shut for two days. That is exactly
 * why berry trees used to refill on any reload - the state lived only in the level.
 *
 * So this subsystem does the one job the sky structurally cannot: it writes per-tree
 * state to disk, and it measures elapsed REAL time between sessions.
 *
 * HOW GROWTH IS MEASURED
 * ----------------------
 * Each tree stores the real UTC at which its cycle started; ripeness is
 * (UtcNow - CycleStartRealUtc) against a GrowthHours expressed in REAL hours.
 *
 * Real hours rather than the sky's hours, on purpose: the sky's Day Length is a
 * look - a day has to read inside a play session - so a berry aged off it would
 * ripen while the player crosses a route. The two run at different speeds because
 * they are measuring different things.
 *
 * There is no accumulator and no catch-up loop, which is what makes "closed for two
 * days" simply work, and what stops a hitch, a long level load or a paused editor
 * from losing growth. A player who winds the system clock backwards gets nothing -
 * elapsed goes negative and the tree reads as freshly planted. Winding it forwards
 * does ripen berries; that was true of classic as well, and the alternative (counting
 * session time only) throws away offline growth, which is the point.
 *
 * PERSISTENCE
 * -----------
 * Own slot, hooked at the same three choke points that already handle the quest
 * slot, so no Blueprint can forget it: UGF_VaultSystem::SaveToDisk -> SaveBerries,
 * CreatureManagerSubsystem::Initialize and ::LoadGame -> LoadBerries, ::DeleteSave ->
 * DeleteBerrySave.
 *
 * Harvesting deliberately does NOT write to disk. It hands the player berries that
 * live in the *main* save, so writing tree state early would let a soft reset roll
 * back the berries while the tree stayed empty. Both halves move on the next save.
 */
UCLASS(BlueprintType)
class GAMMAFRAMEWORKWORLD_API UGF_BerryGrowthSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
    GENERATED_BODY()

public:
    /** IGF_ResettableState - drop every tree; LoadBerries() puts them back. */
    virtual void ResetToBootState() override;

    /**
     * Fires whenever stored growth data changes underneath the world: a debug skip, a
     * forced ripen, a load, a reset.
     *
     * Trees are not polled - ripeness is derived on demand - so without this a tree
     * already standing in a loaded level would keep showing its old sprite until its
     * next refresh tick. UGF_BerryTreeComponent binds this in BeginPlay, which is what
     * makes Debug Advance Growth Hours update the world immediately.
     *
     * Also broadcast on harvest, because a Blueprint may call HarvestBerryTree here
     * directly rather than going through the component - in which case this is the
     * only thing that tells the tree to change its sprite.
     */
    UPROPERTY(BlueprintAssignable, Category = "Berries|Events")
    FGF_OnBerryDataChanged OnBerryDataChanged;

    /**
     * Stable ID for a level-placed tree: "LevelName.ActorName", PIE prefix stripped.
     * Placed actors keep their name across sessions and streaming, so this survives
     * saves without anyone hand-authoring an ID per tree.
     */
    UFUNCTION(BlueprintPure, Category = "Berries", meta = (DefaultToSelf = "TreeActor"))
    static FName MakeBerryTreeID(const AActor* TreeActor);

    /**
     * 0 = Seedling, 1 = Sprout, 2 = Almost done, 3 = Ready to harvest - the same
     * order as ENUM_BerryTreeState, so the result maps straight onto SetTreeState.
     *
     * GrowthHours is the full picked-to-ripe time in REAL hours (8 = eight hours of
     * wall clock, game open or not); the three stage changes are spread evenly
     * across it. A tree this save has never heard of reads as Ready.
     */
    UFUNCTION(BlueprintPure, Category = "Berries")
    int32 GetBerryTreeStage(FName TreeID, float GrowthHours = 8.0f) const;

    UFUNCTION(BlueprintPure, Category = "Berries")
    bool IsBerryTreeReady(FName TreeID, float GrowthHours = 8.0f) const;

    /** Real hours until ripe, 0 when it already is. Good for the "not ready" line. */
    UFUNCTION(BlueprintPure, Category = "Berries")
    float GetBerryTreeHoursRemaining(FName TreeID, float GrowthHours = 8.0f) const;

    /**
     * Call when the player takes the berries. Starts a fresh growth cycle from now.
     * Persisted with the next game save, not immediately - see the class comment.
     */
    UFUNCTION(BlueprintCallable, Category = "Berries")
    void HarvestBerryTree(FName TreeID, FName BerryName);

    /** Debug: ripen one tree immediately. */
    UFUNCTION(BlueprintCallable, Category = "Berries|Debug")
    void ForceBerryTreeReady(FName TreeID);

    /**
     * Debug: age every tracked tree by this many hours, so regrowth can be tested
     * without waiting out a real GrowthHours.
     */
    UFUNCTION(BlueprintCallable, Category = "Berries|Debug")
    void DebugAdvanceGrowthHours(float Hours);

    /** Debug/UI: every tree the save is currently tracking. */
    UFUNCTION(BlueprintPure, Category = "Berries|Debug")
    TArray<FGF_BerryTreeState> GetTrackedBerryTrees() const { return BerryTrees; }

    //--------------------
    // PERSISTENCE
    //--------------------

    /** Called from UGF_VaultSystem::SaveToDisk. */
    UFUNCTION(BlueprintCallable, Category = "Berries|Save")
    bool SaveBerries();

    /** Called from CreatureManagerSubsystem Initialize/LoadGame. False = no slot yet. */
    UFUNCTION(BlueprintCallable, Category = "Berries|Save")
    bool LoadBerries();

    /** Called from CreatureManagerSubsystem::DeleteSave. */
    UFUNCTION(BlueprintCallable, Category = "Berries|Save")
    static bool DeleteBerrySave();

private:
    const FGF_BerryTreeState* FindTree(FName TreeID) const;

    TArray<FGF_BerryTreeState> BerryTrees;
};
