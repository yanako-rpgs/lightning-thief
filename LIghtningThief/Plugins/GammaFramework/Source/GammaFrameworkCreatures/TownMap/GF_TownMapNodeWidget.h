// GF_TownMapNodeWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "GF_TownMapNodeWidget.generated.h"

class UGF_TownMapWidget;
class UImage;

/**
 * What this node represents on the town map.
 * NOTE: never derive user-facing text from the UMETA(DisplayName) values below --
 * UMETA display names are stripped from cooked builds and come back blank in the
 * packaged game. Use the node's DisplayName FText instead.
 */
UENUM(BlueprintType)
enum class EGF_GEMapMarker : uint8
{
    None        UMETA(DisplayName = "None"),
    City        UMETA(DisplayName = "City (large marker)"),
    Town        UMETA(DisplayName = "Town (small marker)"),
    Landmark    UMETA(DisplayName = "Landmark"),
    Route       UMETA(DisplayName = "Route (invisible region)"),
    Water       UMETA(DisplayName = "Water Route (invisible region)")
};

/**
 * One rectangle of the translucent region highlight, in map-canvas design space.
 * Regions that are L-shaped or split across islands just use several of these.
 */
USTRUCT(BlueprintType)
struct FGF_GEMapHighlightRect
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map")
    FVector2D Position = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map")
    FVector2D Size = FVector2D::ZeroVector;
};

/**
 * A single selectable hotspot on the town map.
 *
 * Place these directly onto the town map's canvas in the UMG designer, positioned by
 * eye over the hand-drawn art -- their canvas slot position IS the authoring data, so
 * nothing has to line up to a grid.
 *
 * Two flavours:
 *   - Icon nodes (City / Town / Landmark): small, visible art. The cursor frames them.
 *   - Region nodes (Route / Water): a large TRANSPARENT widget stretched over the whole
 *     route. Invisible to the eye but a real hit target, so the mouse can hover anywhere
 *     along the route while the keyboard cursor snaps to CursorAnchor.
 *
 * Z-ORDER: add region nodes to the canvas BEFORE icon nodes. UMG hit-tests topmost-first,
 * so a route region placed above an icon would swallow the icon's mouse hover.
 *
 * Long routes can carry several nodes sharing one LocationId -- the cursor travels along
 * them while the name box stays put.
 */
UCLASS(Abstract, Blueprintable)
class GAMMAFRAMEWORKCREATURES_API UGF_TownMapNodeWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    //====================================================================================
    // IDENTITY
    //====================================================================================

    /** Internal id. Match RouteData::RouteName so the player marker and visited set line up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Identity", meta = (ExposeOnSpawn = true))
    FName LocationId;

    /**
     * Name shown in the map's name box (e.g. "Route 104", "Slatehaven City").
     *
     * Deliberately an FText and NOT a UGF_RouteData reference: UGF_RouteData holds
     * UGF_CreatureSpeciesData by hard ref for its encounter tables, so hard-linking routes
     * from the map would drag every species' sprites and cries into the menu's load.
     * If you want the link anyway, use a TSoftObjectPtr and resolve it on demand.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Identity", meta = (ExposeOnSpawn = true))
    FText DisplayName;

    /**
     * Other RouteData names that should resolve to THIS map location.
     *
     * Interiors have their own UGF_RouteData (own music, own encounters), so the Tamer School
     * reports "SlatehavenTamerSchool" and would leave the player marker with nowhere to sit.
     * List those route names here on the Slatehaven node: trial, houses, school, Creature Center.
     * Matching is LocationId first, then this array.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Identity")
    TArray<FName> AdditionalRouteIds;

    /** Optional second line under the name (e.g. "The City Probing the Integration of Temperament and Science"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Identity", meta = (ExposeOnSpawn = true))
    FText Subtitle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Identity", meta = (ExposeOnSpawn = true))
    EGF_GEMapMarker MarkerType = EGF_GEMapMarker::Town;

    //====================================================================================
    // CURSOR FRAMING
    //====================================================================================

    /**
     * Optional. Name your marker Image "MarkerImage" in the node Blueprint and the cursor
     * box, the anchor point and the hit region are all measured from THAT widget instead of
     * the node's root.
     *
     * Without it everything is measured from the node root, which is typically a fixed box
     * sized for the widest marker -- so a narrow vertical capsule sitting inside a 76x36 root
     * gets a horizontal cursor box centred ~38px to the right of the art.
     *
     * Route region nodes deliberately leave this unset: their region IS the stretched root.
     *
     * Typed UImage rather than UWidget so Blueprint can drive it directly (Set Render
     * Opacity, Set Brush) with no Cast To Image -- that cast fails to resolve against a
     * generic widget reference and takes the whole node Blueprint down with it.
     */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Town Map|Cursor")
    UImage* MarkerImage = nullptr;

    /**
     * Where the selection box centers, as an offset from this widget's top-left in canvas
     * design space. Leave at (-1,-1) to use the widget's own center -- correct for icon
     * nodes. Region nodes usually want an explicit anchor so the box doesn't sit in the
     * middle of a big empty rectangle.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Cursor")
    FVector2D CursorAnchor = FVector2D(-1.0, -1.0);

    /**
     * Size of the selection box when this node is hovered. Leave at (0,0) to use the
     * widget's canvas slot size -- right for icons (small square for the round markers,
     * wider for the capsule city markers). Region nodes should set this explicitly,
     * otherwise the box wraps the entire route.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Cursor")
    FVector2D CursorSize = FVector2D::ZeroVector;

    //====================================================================================
    // FLY / TELEPORT
    //====================================================================================

    /** Whether Fly may target this location (still gated on it having been revealed). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Fly")
    bool bCanFlyTo = false;

    /** Identifies the warp/spawn this location flies to. Resolved by your Fly flow, not here. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Fly")
    FName FlyTargetId;

    //====================================================================================
    // NAVIGATION OVERRIDES
    //====================================================================================

    /**
     * Optional hand-authored destinations. The map's directional scoring handles the vast
     * majority of moves on its own; fill these in only for the handful of spots that feel
     * wrong (scattered islands and long ocean routes are the usual offenders).
     * Leave NAME_None to use automatic scoring for that direction.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Navigation")
    FName OverrideUp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Navigation")
    FName OverrideDown;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Navigation")
    FName OverrideLeft;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Navigation")
    FName OverrideRight;

    /**
     * Skip this node during directional navigation. Use for extra nodes that exist only
     * to widen a route's mouse hit area.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Navigation")
    bool bSkipDirectionalNavigation = false;

    //====================================================================================
    // REGION HIGHLIGHT
    //====================================================================================

    /**
     * The translucent region drawn behind the map art while this node is selected, in
     * canvas design space. Free-floating rects -- nothing has to be grid-aligned.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Town Map|Highlight")
    TArray<FGF_GEMapHighlightRect> HighlightRects;

    //====================================================================================
    // RUNTIME
    //====================================================================================

    /** Cursor target in canvas design space. Computed by the owning map in RebuildNodeGraph(). */
    UPROPERTY(BlueprintReadOnly, Category = "Town Map|Runtime")
    FVector2D MapPos = FVector2D::ZeroVector;

    /** Selection box size resolved from CursorSize / the slot. Computed by the owning map. */
    UPROPERTY(BlueprintReadOnly, Category = "Town Map|Runtime")
    FVector2D ResolvedCursorSize = FVector2D::ZeroVector;

    /**
     * This node's actual rectangle in canvas space -- the widget's own bounds, not the cursor
     * anchor. Free-roam uses it to work out which location the loose cursor is over, which is
     * why route nodes are stretched across their whole route.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Town Map|Runtime")
    FVector2D RegionPos = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Town Map|Runtime")
    FVector2D RegionSize = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Town Map|Runtime")
    TWeakObjectPtr<UGF_TownMapWidget> OwnerMap;

    //====================================================================================
    // BLUEPRINT HOOKS
    //====================================================================================

    /** The cursor landed on this node -- play the marker's bounce / brighten here. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Town Map|Events")
    void OnCursorEnter();

    UFUNCTION(BlueprintImplementableEvent, Category = "Town Map|Events")
    void OnCursorExit();

    /**
     * Called whenever the map re-evaluates reveal state. Hide the marker art for
     * undiscovered locations here (the map already excludes them from navigation).
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Town Map|Events")
    void OnRevealStateChanged(bool bRevealed);

    //====================================================================================
    // HELPERS
    //====================================================================================

    /** The override id for a direction, or NAME_None if that direction uses auto scoring. */
    UFUNCTION(BlueprintPure, Category = "Town Map")
    FName GetOverrideForDirection(EUINavigation Direction) const;

    /** True for nodes that draw a marker, false for the invisible route regions. */
    UFUNCTION(BlueprintPure, Category = "Town Map")
    bool HasVisibleMarker() const;

    /** True if this node represents the given RouteData name, via LocationId or AdditionalRouteIds. */
    UFUNCTION(BlueprintPure, Category = "Town Map")
    bool MatchesRouteId(FName RouteId) const;

    /** True if a canvas-space point falls inside this node's region. */
    UFUNCTION(BlueprintPure, Category = "Town Map")
    bool ContainsPoint(FVector2D CanvasPoint) const;

protected:

    /**
     * Mouse hover moves the cursor here. The owning map ignores it unless UINav reports
     * the mouse as the active input device, so a resting mouse cannot fight the gamepad.
     */
    virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
};
