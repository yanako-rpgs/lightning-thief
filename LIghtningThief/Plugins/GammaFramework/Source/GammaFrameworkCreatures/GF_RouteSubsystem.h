#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_RouteData.h"
#include "GF_ResettableState.h"
#include "GF_RouteSubsystem.generated.h"

class UGF_OSTManager;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnRouteChanged, UGF_RouteData*, NewRoute);

/**
 * Tracks the current route/area the player is in.
 * Automatically plays the route's music via OSTManager when the area changes.
 * Call RestoreRouteMusic() after a battle ends to resume the overworld track.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_RouteSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
    GENERATED_BODY()

public:

    // --------------------------------------------------------
    // AREA API
    // --------------------------------------------------------

    /** Called by RouteVolume when the player enters an area. Plays music automatically. */
    UFUNCTION(BlueprintCallable, Category = "Route")
    void SetCurrentRoute(UGF_RouteData* NewRoute);

    /** The route the player is currently in. May be nullptr before any volume is entered. */
    UFUNCTION(BlueprintPure, Category = "Route")
    UGF_RouteData* GetCurrentRoute() const { return CurrentRoute; }

    /** Returns the internal route name (e.g. "Route101"). Empty if no route set. */
    UFUNCTION(BlueprintPure, Category = "Route")
    FName GetCurrentRouteName() const;

    /**
     * Returns the display name for the current route, with {PlayerName} replaced.
     * Use this for the area banner UI instead of reading DisplayName directly.
     * Returns empty text if no route is set.
     */
    UFUNCTION(BlueprintCallable, Category = "Route")
    FText GetCurrentRouteDisplayName(const FString& PlayerName) const;

    // --------------------------------------------------------
    // MAP DISCOVERY
    // --------------------------------------------------------

    /**
     * True if the player has ever set foot in this route/area.
     *
     * Recorded automatically by SetCurrentRoute, and stored as a QuestSubsystem story flag
     * ("MapSeen_<RouteName>") so it persists with the existing save rather than needing its
     * own serialisation. The town map reads this to decide which locations are discovered.
     */
    UFUNCTION(BlueprintPure, Category = "Route|Discovery")
    bool HasVisitedRoute(FName RouteName) const;

    /** The story-flag name used to record a visit. Public so the town map can batch-query. */
    UFUNCTION(BlueprintPure, Category = "Route|Discovery")
    static FName MakeMapSeenFlag(FName RouteName);

    /** True if the player is currently in a route that has grass encounters. */
    UFUNCTION(BlueprintPure, Category = "Route")
    bool HasWildEncounters() const;

    // --------------------------------------------------------
    // MUSIC API
    // --------------------------------------------------------

    /**
     * Resume the current route's music after a battle or cutscene ends.
     * Call this from your battle-end / wild-encounter-end flow.
     * Does nothing if no route is set or the route has no music.
     */
    UFUNCTION(BlueprintCallable, Category = "Route|Music")
    void RestoreRouteMusic(float FadeInDuration = 1.0f);

    /**
     * Manually suppress automatic music changes (e.g. during a cutscene
     * where you want full control). Call RestoreRouteMusic() to re-enable.
     */
    UFUNCTION(BlueprintCallable, Category = "Route|Music")
    void SetMusicSuppressed(bool bSuppressed) { bMusicSuppressed = bSuppressed; }

    // --------------------------------------------------------
    // EVENTS
    // --------------------------------------------------------

    /** Fires whenever the player moves into a new area. */
    UPROPERTY(BlueprintAssignable, Category = "Route|Events")
    FGF_OnRouteChanged OnRouteChanged;

private:

    UPROPERTY()
    UGF_RouteData* CurrentRoute = nullptr;

public:
    // IGF_ResettableState - CurrentRoute is restored from VaultSystem->SavedRoutePath on
    // load. bMusicSuppressed is transient and sticking on would mute the next run.
    virtual void ResetToBootState() override
    {
        CurrentRoute      = nullptr;
        bMusicSuppressed  = false;
    }

private:

    // When true, SetCurrentRoute won't trigger a music change
    bool bMusicSuppressed = false;

    /** Writes the MapSeen story flag for a newly entered area. */
    void RecordRouteVisited(const UGF_RouteData* Route);

    void PlayRouteMusicInternal(UGF_RouteData* Route, float FadeInOverride = -1.f);
    UGF_OSTManager* GetOSTManager() const;
};
