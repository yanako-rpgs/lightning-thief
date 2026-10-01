#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_RouteData.generated.h"

// ─────────────────────────────────────────────
// What kind of area this is
// ─────────────────────────────────────────────
UENUM(BlueprintType)
enum class EGF_AreaType : uint8
{
    Route       UMETA(DisplayName = "Route"),
    City        UMETA(DisplayName = "City / Town"),
    Cave        UMETA(DisplayName = "Cave"),
    Water       UMETA(DisplayName = "Water"),
    Building    UMETA(DisplayName = "Building / Indoor"),
    Forest      UMETA(DisplayName = "Forest"),
};

// ─────────────────────────────────────────────
// Whether the field moves Fly / Teleport may be used to leave an area.
// Auto derives it from AreaType (outdoors yes, Cave / Building no); the explicit
// values are for hand-authored exceptions in either direction.
// ─────────────────────────────────────────────
UENUM(BlueprintType)
enum class EGF_AreaTeleportRule : uint8
{
    Auto        UMETA(DisplayName = "Auto (from Area Type)"),
    AlwaysAllow UMETA(DisplayName = "Always Allow"),
    AlwaysBlock UMETA(DisplayName = "Always Block"),
};

// ─────────────────────────────────────────────
// Which fishing rod is being used — selects the encounter table
// ─────────────────────────────────────────────
UENUM(BlueprintType)
enum class EGF_FishingRod : uint8
{
    SimpleRod      UMETA(DisplayName = "Simple Rod"),
    KeenRod     UMETA(DisplayName = "Keen Rod"),
    DeepRod    UMETA(DisplayName = "Deep Rod"),
};

// ─────────────────────────────────────────────
// One slot in a wild encounter table
// ─────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FGF_WildEncounterSlot
{
    GENERATED_BODY()

    /**
     * Which species spawns.
     *
     * SOFT on purpose. This used to be a hard UGF_CreatureSpeciesData*, which meant a
     * RouteVolume -> UGF_RouteData -> encounter table chain pulled in every species on
     * the route -- and each species hard-references its full sprite set (~35 MB).
     * Measured: 94 species resident just from walking around with sublevels loaded.
     *
     * Nothing loads now until an encounter actually rolls. Resolve it with
     * UGF_RouteData::ResolveEncounterSpecies, which goes through the manager's registry
     * index and loads exactly one asset.
     *
     * Changing the type needed no data migration: FSoftObjectProperty::ConvertFromType
     * converts a serialised ObjectProperty in place on load.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter")
    TSoftObjectPtr<UGF_CreatureSpeciesData> Species;

    /** Minimum level for this slot */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "1", ClampMax = "100"))
    int32 MinLevel = 2;

    /** Maximum level for this slot */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "1", ClampMax = "100"))
    int32 MaxLevel = 5;

    /** Weight relative to other slots — higher = appears more often */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "1", ClampMax = "100"))
    int32 Weight = 10;
};

// ─────────────────────────────────────────────
// Route / Area data asset
// One asset per route — assign to a RouteVolume in the world
// ─────────────────────────────────────────────
UCLASS(BlueprintType)
class GAMMAFRAMEWORKCREATURES_API UGF_RouteData : public UDataAsset
{
    GENERATED_BODY()

public:

    //--------------------
    // IDENTITY
    //--------------------

    /** Internal name used in code / save data (e.g. "Route101") */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
    FName RouteName;

    /** Display name shown in the area banner (e.g. "Route 101") */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
    FText DisplayName;

    /** City / location subtitle shown under display name (e.g. "Where Things Begin") */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
    FText Subtitle;

    /** What kind of area this is */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
    EGF_AreaType AreaType = EGF_AreaType::Route;

    /**
     * Can the player use Fly / Teleport to leave this area?
     * Auto = allowed on Route / City / Forest / Water, blocked in Cave / Building.
     * Override for exceptions: AlwaysBlock for outdoor maps you don't want escaped
     * (Safari Zone, Sky Pillar), AlwaysAllow for outdoor maps tagged Building.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
    EGF_AreaTeleportRule TeleportRule = EGF_AreaTeleportRule::Auto;

    //--------------------
    // AUDIO
    //--------------------

    /** One-shot intro track (plays once before the loop). Leave null if no intro. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Audio")
    USoundBase* MusicIntro = nullptr;

    /** Looping track for this area */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Audio")
    USoundBase* MusicLoop = nullptr;

    /** Fade-in duration when entering this area (0 = instant) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Audio", meta = (ClampMin = "0.0"))
    float MusicFadeInDuration = 1.0f;

    //--------------------
    // WILD ENCOUNTERS
    //--------------------

    /** Encounter rate 0-100. 0 = no encounters */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Wild Creature", meta = (ClampMin = "0", ClampMax = "100"))
    float EncounterRate = 15.0f;

    /** Creature found in tall grass / normal walking */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Wild Creature")
    TArray<FGF_WildEncounterSlot> GrassEncounters;

    /** Creature found while surfing */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Wild Creature")
    TArray<FGF_WildEncounterSlot> SurfEncounters;

    /** Creature reachable with the Old Rod (low-level commons) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Wild Creature|Fishing")
    TArray<FGF_WildEncounterSlot> OldRodEncounters;

    /** Creature reachable with the Good Rod */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Wild Creature|Fishing")
    TArray<FGF_WildEncounterSlot> GoodRodEncounters;

    /** Creature reachable with the Super Rod (rarest / highest level) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Wild Creature|Fishing")
    TArray<FGF_WildEncounterSlot> SuperRodEncounters;

    /** Creature found by smashing rocks with Rock Smash */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Wild Creature")
    TArray<FGF_WildEncounterSlot> RockSmashEncounters;

    //--------------------
    // HELPERS
    //--------------------

    /**
     * Returns the display name with any {PlayerName} token replaced by the given string.
     * Routes that don't use {PlayerName} in their DisplayName are unaffected.
     *
     * Example: DisplayName = "{PlayerName}'s House"
     *          GetFormattedDisplayName("Ash") → "Ash's House"
     */
    UFUNCTION(BlueprintCallable, Category = "Route")
    FText GetFormattedDisplayName(const FString& PlayerName) const;

    /**
     * Pick a random encounter from GrassEncounters using weighted random selection.
     * Returns nullptr if no encounters are set.
     */
    UFUNCTION(BlueprintCallable, Category = "Route|Wild Creature")
    FGF_WildEncounterSlot PickRandomGrassEncounter() const;

    UFUNCTION(BlueprintCallable, Category = "Route|Wild Creature")
    FGF_WildEncounterSlot PickRandomSurfEncounter() const;

    /**
     * Pick a random fishing encounter for the given rod using weighted random selection.
     * Each rod has its own table; returns an empty slot (Species == nullptr) if that
     * rod's table is empty on this route (i.e. nothing bites).
     */
    UFUNCTION(BlueprintCallable, Category = "Route|Wild Creature")
    FGF_WildEncounterSlot PickRandomFishingEncounter(EGF_FishingRod Rod) const;

    /**
     * Pick a random Rock Smash encounter using weighted random selection.
     * Returns an empty slot (Species == nullptr) if this route has no rock encounters.
     */
    UFUNCTION(BlueprintCallable, Category = "Route|Wild Creature")
    FGF_WildEncounterSlot PickRandomRockSmashEncounter() const;

    /**
     * True if Fly / Teleport may be used to leave this area — TeleportRule if it's
     * been set explicitly, otherwise derived from AreaType.
     * Prefer UGF_CreatureManagerSubsystem::CanUseTeleport() at call sites; that also
     * covers battle / dialogue state and the dungeon flag.
     */
    UFUNCTION(BlueprintPure, Category = "Route")
    bool AllowsTeleportOut() const;

private:
    FGF_WildEncounterSlot PickFromTable(const TArray<FGF_WildEncounterSlot>& Table) const;

public:
    /**
     * Load the species an encounter slot points at, and return it.
     *
     * Call this on the slot you just picked -- it is the one node that replaces the
     * old direct "Species" pin. Loads a single asset via the manager's registry
     * index; returns null for an empty slot.
     */
    UFUNCTION(BlueprintCallable, Category = "Route|Wild Creature", meta = (WorldContext = "WorldContextObject"))
    static UGF_CreatureSpeciesData* ResolveEncounterSpecies(const UObject* WorldContextObject, const FGF_WildEncounterSlot& Slot);

    /** True if the slot points at a species at all, without loading it. */
    UFUNCTION(BlueprintPure, Category = "Route|Wild Creature")
    static bool IsEncounterSlotValid(const FGF_WildEncounterSlot& Slot) { return !Slot.Species.IsNull(); }
};
