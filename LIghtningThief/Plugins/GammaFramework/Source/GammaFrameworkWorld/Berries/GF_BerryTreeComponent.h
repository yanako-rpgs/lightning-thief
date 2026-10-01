// GF_BerryTreeComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GF_BerryTreeComponent.generated.h"

class UPaperFlipbook;
class UPaperFlipbookComponent;
class UGF_BerryGrowthSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBerryStageChanged, int32, NewStage);

/**
 * Drop this on a berry tree actor and the tree grows. That is the whole setup.
 *
 * WHY A COMPONENT AND NOT A BASE CLASS
 * ------------------------------------
 * BP_BerryTree already owns its own variables (BerryName, BerryAmount,
 * BerryTreeState, four flipbooks) and its own dialogue and interact graph. A C++
 * parent class would collide with every one of those names on reparent, and would
 * mean re-doing work that already exists. A component sits alongside instead, the
 * same way GridMovementComponent and GridNPCAIComponent already do in this project.
 *
 * WHAT IT DOES FOR YOU
 * --------------------
 *   - On BeginPlay, works out this tree's identity, asks UGF_BerryGrowthSubsystem
 *     what stage it should be at, and swaps the flipbook to match. This runs every
 *     time the sublevel streams in, which is exactly when the tree needs to be right.
 *   - Reads how long this berry takes to ripen from the berry's own UGF_ItemData asset
 *     (BerryGrowthHours), so growth time is authored once per berry, not per tree.
 *   - Recomputes on a slow timer so a tree ripens while the player is standing there.
 *   - Harvest() records the pick and drops the visual straight back to Seedling.
 *
 * WHAT IT DELIBERATELY DOES NOT DO
 * --------------------------------
 * Give the player items, play dialogue, or touch BerryTreeState. That logic already
 * exists in your Blueprint and is better off staying there. Bind OnStageChanged if
 * you want to keep the Blueprint's own enum in step.
 *
 * IT IS NOT A CLOCK. BP_StylizedSky still owns time of day and the day/night cycle;
 * nothing here reads it. Growth is measured in REAL hours against the save - see
 * UGF_BerryGrowthSubsystem for why the two must not share a clock.
 */
UCLASS(ClassGroup = (Creature), meta = (BlueprintSpawnableComponent), DisplayName = "GE Berry Tree")
class GAMMAFRAMEWORKWORLD_API UGF_BerryTreeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGF_BerryTreeComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    //--------------------
    // SETUP (set these in the Blueprint's Details panel)
    //--------------------

    /**
     * Which berry this tree bears - the key used to look up the growth time from the
     * item asset's BerryGrowthHours.
     *
     * LEAVE THIS EMPTY if the owning Blueprint already has a Name variable called
     * "BerryName" (BP_BerryTree does, overridden per placed tree): BeginPlay adopts
     * that value automatically. Authoring the berry twice invites the two to drift,
     * and a silent mismatch here only shows up as a tree with the wrong growth time.
     *
     * Set it explicitly only for a tree whose Blueprint has no such variable.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berry Tree")
    FName BerryItemName;

    /**
     * Per-tree override of the berry's growth time, in REAL hours. Leave at 0 - the
     * normal answer - and the berry asset decides. Only set this for a deliberate
     * one-off (a story tree that must be ready by morning, say).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berry Tree|Advanced", meta = (ClampMin = "0.0"))
    float GrowthHoursOverride = 0.0f;

    /** Stage 0. Same assets your Blueprint already has - drag the same four in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berry Tree|Flipbooks")
    TObjectPtr<UPaperFlipbook> SeedlingFlipbook;

    /** Stage 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berry Tree|Flipbooks")
    TObjectPtr<UPaperFlipbook> SproutFlipbook;

    /** Stage 2. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berry Tree|Flipbooks")
    TObjectPtr<UPaperFlipbook> AlmostDoneFlipbook;

    /** Stage 3 - harvestable. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berry Tree|Flipbooks")
    TObjectPtr<UPaperFlipbook> ReadyToBeHarvestedFlipbook;

    /**
     * Which flipbook component to drive. Leave empty and the first one found on the
     * owning actor is used, which is the right answer for a tree with one sprite.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berry Tree|Advanced")
    TObjectPtr<UPaperFlipbookComponent> TargetFlipbookComponent;

    /**
     * How often to re-check ripeness while the level is loaded, in real seconds.
     * This is only so a tree visibly ripens under the player's nose; correctness
     * never depends on it, because every query recomputes from timestamps.
     * 0 disables the timer entirely.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Berry Tree|Advanced", meta = (ClampMin = "0.0"))
    float RefreshIntervalSeconds = 60.0f;

    //--------------------
    // RUNTIME
    //--------------------

    /** Fires whenever the stage actually changes (0-3). Matches ENUM_BerryTreeState's order. */
    UPROPERTY(BlueprintAssignable, Category = "Berry Tree")
    FGF_OnBerryStageChanged OnStageChanged;

    /** Is there fruit on it right now? Call this instead of reading a cached variable. */
    UFUNCTION(BlueprintPure, Category = "Berry Tree")
    bool IsReady() const;

    /** 0-3, recomputed live. */
    UFUNCTION(BlueprintPure, Category = "Berry Tree")
    int32 GetStage() const;

    /** Real hours until ripe, 0 when it already is. For the "not ready" line. */
    UFUNCTION(BlueprintPure, Category = "Berry Tree")
    float GetHoursRemaining() const;

    /** This tree's identity in the save: "LevelName.ActorName". */
    UFUNCTION(BlueprintPure, Category = "Berry Tree")
    FName GetTreeID() const { return TreeID; }

    /** The growth time actually in use: the override if set, else the berry asset's. */
    UFUNCTION(BlueprintPure, Category = "Berry Tree")
    float GetGrowthHours() const;

    /**
     * Call after the berries have actually been given. Starts a fresh growth cycle
     * and empties the tree on screen. Persisted with the player's next save.
     */
    UFUNCTION(BlueprintCallable, Category = "Berry Tree")
    void Harvest();

    /** Re-read the stage and update the sprite. Cheap; call it on interact. */
    UFUNCTION(BlueprintCallable, Category = "Berry Tree")
    void RefreshNow();

private:
    UGF_BerryGrowthSubsystem* GetGrowthSubsystem() const;

    /** Adopts the owning Blueprint's "BerryName" variable when BerryItemName is unset. */
    void ResolveBerryName();

    /** Resolves TargetFlipbookComponent once, in BeginPlay - never lazily on tick. */
    void ResolveFlipbookComponent();

    void ApplyStageVisual(int32 Stage);

    UPROPERTY(Transient)
    FName TreeID;

    /**
     * Growth time resolved from the berry asset, cached because GetItemByName logs a
     * warning on a miss and the queries below run on a timer.
     */
    mutable float CachedGrowthHours = 0.0f;

    /** Last stage pushed to the sprite, so OnStageChanged only fires on real changes. */
    int32 LastAppliedStage = INDEX_NONE;

    FTimerHandle RefreshTimer;
};
