// GF_TownMapWidget.h
#pragma once

#include "CoreMinimal.h"
#include "UINavWidget.h"
#include "Engine/TimerHandle.h"
#include "InputCoreTypes.h"
#include "Types/SlateEnums.h"
#include "TownMap/GF_TownMapNodeWidget.h"
#include "GF_TownMapWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;

/**
 * What the map is being opened for. One widget serves all of them.
 */
UENUM(BlueprintType)
enum class EGF_GETownMapMode : uint8
{
    /** Menu / Town Map item. Confirm does nothing. */
    Browse  UMETA(DisplayName = "Browse"),

    /** Fly target picker. Confirm only accepts revealed locations with bCanFlyTo. */
    Fly     UMETA(DisplayName = "Fly")
};

/**
 * The region town map.
 *
 * DESIGN NOTES
 * ------------
 * The map art is hand-drawn and not tile-aligned, so there is no grid anywhere in here.
 * Selectable places are UGF_TownMapNodeWidget instances dropped onto MapCanvas in the UMG
 * designer, positioned by eye over the drawing. Their canvas slot positions are read once
 * at setup and become the navigation graph.
 *
 * Directional input picks the next node by spatial scoring (a cone in the input direction,
 * then nearest by distance-along plus a weighted perpendicular offset), so an irregular
 * scattered layout needs no hand-authored links. Individual nodes can still override any
 * of the four directions for the few spots that score badly.
 *
 * INPUT
 * -----
 * This is a UUINavWidget deliberately containing NO navigable UINavComponents. UINav still
 * gives you the parent/child widget stack, OnReturn for B, nav sounds and input-type icon
 * swapping; it just never owns the cursor. Direction comes in through NativeOnNavigation,
 * which means UINav's own deadzone handling and key-repeat timer drive the cursor for free
 * on keyboard, D-pad and analog stick alike.
 *
 * Leave bUseAnalogDirectionalInput ON in your UINavPCComponent settings, and leave
 * UseThumbstickAsMouse set to None -- a floating analog cursor fights node snapping and
 * does nothing for keyboard players.
 *
 * FOCUS is what makes that work, and it is not automatic. Slate only raises navigation
 * events along the focused widget's path, and UINavWidget normally hands focus to its
 * current UINavComponent -- of which this widget has none. So the widget makes itself
 * focusable and claims keyboard focus at setup, then reclaims it in NativeTick the same way
 * GF_DialogueWidgetBase does (alt-tab and viewport clicks otherwise drop it silently).
 *
 * Do NOT "fix" a dead cursor by adding a UINavComponent. UUINavComponent::NativeOnNavigation
 * calls HandleOnNavigation, which returns FNavigationReply::Stop() -- the component would
 * consume the event before it ever bubbled up to this widget, which is strictly worse.
 *
 * If direction still never arrives, bind IA_Movement in Blueprint and call
 * MoveCursorInDirection() directly. The whole cursor API is BlueprintCallable precisely so
 * that fallback costs nothing.
 */
UCLASS(Abstract, Blueprintable)
class GAMMAFRAMEWORKCREATURES_API UGF_TownMapWidget : public UUINavWidget
{
    GENERATED_BODY()

public:

    UGF_TownMapWidget(const FObjectInitializer& ObjectInitializer);

    //====================================================================================
    // DESIGNER BINDINGS
    //====================================================================================

    /** The canvas holding the map image and every UGF_TownMapNodeWidget. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Town Map")
    UCanvasPanel* MapCanvas = nullptr;

    /**
     * The selection box. Must be a direct child of MapCanvas. Its slot alignment and
     * position are driven from code, so anything you author on the slot is overwritten.
     */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Town Map")
    UWidget* CursorWidget = nullptr;

    /** Optional player-position marker (the player's head icon). Also a direct child of MapCanvas. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Town Map")
    UWidget* PlayerMarkerWidget = nullptr;

    //====================================================================================
    // TUNING
    //====================================================================================

    /**
     * Half-width of the acceptance cone, as a slope (perpendicular / along).
     * 1.0 is a 45 degree cone, 1.7 is roughly 60. Higher accepts more diagonal candidates.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Navigation", meta = (ClampMin = "0.1"))
    float ConeSlope = 1.0f;

    /**
     * Retry slope used when the tight cone finds nothing. Without this, isolated islands
     * become dead ends. Set to 0 to disable the retry.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Navigation", meta = (ClampMin = "0.0"))
    float WideConeSlope = 3.0f;

    /**
     * How much a candidate is penalised for being off-axis. Around 2-3 makes the cursor
     * prefer staying in a visual row or column instead of drifting diagonally, which is
     * what makes the movement read as deliberate rather than random.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Navigation", meta = (ClampMin = "0.0"))
    float PerpBias = 2.5f;

    //====================================================================================
    // FREE ROAM
    //====================================================================================

    /**
     * Cursor roams freely across the map instead of hopping node to node.
     *
     * Each press moves it FreeRoamStep in that direction; whichever node's region it lands
     * inside becomes the selection, which is what makes un-iconed routes name themselves.
     * Turn off to fall back to pure node-to-node navigation (the cone scoring below).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Free Roam")
    bool bFreeRoamCursor = true;

    /** Canvas units the loose cursor travels per press. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Free Roam", meta = (ClampMin = "1.0"))
    float FreeRoamStep = 16.0f;

    /**
     * On first entering an icon node's region, pull the cursor onto the marker so the box
     * frames it. Only fires on entry -- once inside, the cursor moves freely again, otherwise
     * it would be glued to the icon and could never leave.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Free Roam")
    bool bMagnetiseToIcons = true;

    /**
     * Keep showing the last location name when the cursor is over open sea rather than
     * blanking it. OnCursorMoved still reports the real (null) node.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Free Roam")
    bool bKeepNameOutsideRegions = true;

    /** Seconds for the selection box to slide to a new node. ~0.06 reads far better than a hard cut. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Cursor", meta = (ClampMin = "0.0"))
    float CursorMoveTime = 0.06f;

    /** Fallback selection box size when a node specifies neither CursorSize nor a sized slot. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Cursor")
    FVector2D DefaultCursorSize = FVector2D(24.0, 24.0);

    /** Canvas Z order for the cursor. Above the map art and the markers. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Cursor")
    int32 CursorZOrder = 100;

    //====================================================================================
    // HIGHLIGHT
    //====================================================================================

    /**
     * Widget spawned once per FGF_GEMapHighlightRect on the selected node -- a plain
     * translucent Image is enough. Leave empty to skip pooling entirely and draw the
     * region yourself from the OnHighlightChanged event.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Highlight")
    TSubclassOf<UUserWidget> HighlightWidgetClass;

    /** Canvas Z order for highlight rects. Negative keeps them behind the markers. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Highlight")
    int32 HighlightZOrder = -1;

    //====================================================================================
    // FOCUS
    //====================================================================================

    /**
     * Re-claim keyboard focus from NativeTick if something else takes it (alt-tab, a stray
     * viewport click). Without focus this widget receives no navigation events at all and
     * the cursor simply stops responding, with no error anywhere.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    bool bReclaimKeyboardFocus = true;

    /**
     * Log every navigation event that reaches this widget. Turn on when the cursor is dead:
     * silence means focus or input mode is the problem, not the scoring.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    bool bLogNavigationEvents = false;

    /**
     * Re-apply Game-and-UI input mode with this widget as the focus target when focus cannot
     * otherwise be taken. In Game-and-UI the viewport owns keyboard focus and every
     * SetUserFocus call is ignored, which leaves the cursor completely dead. Turn this off
     * only if you set the input mode yourself and pass this widget as the focus target.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    bool bTakeInputModeFocus = true;

    /**
     * Handle direction keys directly in NativeOnKeyDown instead of waiting for Slate to turn
     * them into navigation events. This bypasses UINav's navigation config entirely, so it
     * works regardless of what is bound to IA_MenuUp/Down/Left/Right.
     *
     * No double-move risk: Slate raises a key event first and only synthesises navigation if
     * nothing handled the key, so consuming it here means the navigation path never runs.
     * Analog stick still arrives through NativeOnNavigation, which is left intact.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    bool bHandleKeysDirectly = true;

    /** Keys that move the cursor. Editable per-project -- changing these needs no rebuild. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    TArray<FKey> KeysUp;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    TArray<FKey> KeysDown;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    TArray<FKey> KeysLeft;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    TArray<FKey> KeysRight;

    /** Keys that confirm the hovered location. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Input")
    TArray<FKey> KeysConfirm;

    /**
     * Inspect every node and log every authoring problem at once: dead regions, missing ids,
     * markers that cannot be measured, unreachable nodes. Runs automatically shortly after
     * the map opens (see bAuditOnOpen) once layout is real, and is callable on demand.
     *
     * The point is that one run surfaces the whole list, rather than one fault per test.
     */
    UFUNCTION(BlueprintCallable, Category = "Town Map|Debug")
    void RunMapAudit();

    /** Run RunMapAudit automatically half a second after the map opens. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Debug")
    bool bAuditOnOpen = true;

    /** Force a focus attempt now. Exposed so a Blueprint can retry without a code change. */
    UFUNCTION(BlueprintCallable, Category = "Town Map|Input")
    void ForceClaimFocus() { TryClaimFocus(); }

    /** Current focus state, for on-screen debugging without touching the log. */
    UFUNCTION(BlueprintPure, Category = "Town Map|Input")
    bool HasMapFocus() const;

    //====================================================================================
    // STATE
    //====================================================================================

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map")
    EGF_GETownMapMode Mode = EGF_GETownMapMode::Browse;

    /**
     * Locations the player has discovered. untagged nodes are skipped by navigation
     * and told to hide themselves.
     *
     * Back this with a saved TSet fed from UGF_RouteSubsystem::OnRouteChanged rather than
     * with story flags -- a visited set has no "blank flag name opens every gate" failure
     * mode, and RouteSubsystem already knows where the player is.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Town Map|Reveal")
    TSet<FName> RevealedLocations;

    /**
     * Let the cursor hover undiscovered locations and show untaggedName instead of
     * skipping them as open sea. Confirm still rejects them, so a player can feel that
     * something is there without being told what.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Reveal")
    bool bAllowHoveruntagged = true;

    /** Name shown for an undiscovered location. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Reveal")
    FText untaggedName = NSLOCTEXT("TownMap", "untagged", "???");

    /** Debug / testing: treat every node as discovered. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Town Map|Reveal")
    bool bRevealAllLocations = false;

    /** Where the player currently is. Drives PlayerMarkerWidget and the cursor's start node. */
    UPROPERTY(BlueprintReadOnly, Category = "Town Map")
    FName PlayerLocationId;

    //====================================================================================
    // CURSOR API
    //====================================================================================

    /**
     * Skill the cursor one step. Returns false if nothing lies that way (play a bump).
     * Also the entry point to call from Blueprint if you end up driving this from
     * IA_Movement instead of UINav's navigation events.
     */
    UFUNCTION(BlueprintCallable, Category = "Town Map|Cursor")
    bool MoveCursorInDirection(EUINavigation Direction);

    UFUNCTION(BlueprintCallable, Category = "Town Map|Cursor")
    void SetCursorToNode(UGF_TownMapNodeWidget* Node, bool bInstant = false);

    /** Returns false if no revealed node carries that id. */
    UFUNCTION(BlueprintCallable, Category = "Town Map|Cursor")
    bool SetCursorToLocation(FName LocationId, bool bInstant = false);

    UFUNCTION(BlueprintPure, Category = "Town Map|Cursor")
    UGF_TownMapNodeWidget* GetCurrentNode() const { return CurrentNode; }

    /** Name of the location under the cursor -- bind your name box to this. */
    UFUNCTION(BlueprintPure, Category = "Town Map|Cursor")
    FText GetCurrentLocationName() const;

    UFUNCTION(BlueprintPure, Category = "Town Map|Cursor")
    FText GetCurrentLocationSubtitle() const;

    /**
     * False when the cursor is on a location the player has not discovered. Use it to style
     * the name box differently for "???", or to hide the subtitle.
     */
    UFUNCTION(BlueprintPure, Category = "Town Map|Cursor")
    bool IsCurrentLocationDiscovered() const;

    /**
     * Fires OnLocationConfirmed, or OnConfirmRejected in Fly mode when the location is not
     * a legal Fly target. Hook this to UINav's OnSelect / your A button.
     */
    UFUNCTION(BlueprintCallable, Category = "Town Map|Cursor")
    void ConfirmCurrentLocation();

    UFUNCTION(BlueprintPure, Category = "Town Map|Cursor")
    bool CanConfirmNode(const UGF_TownMapNodeWidget* Node) const;

    //====================================================================================
    // GRAPH
    //====================================================================================

    /**
     * Re-read every node's canvas slot and rebuild the navigation graph. Called
     * automatically from OnSetupCompleted; call it again if you add nodes at runtime.
     */
    UFUNCTION(BlueprintCallable, Category = "Town Map|Graph")
    void RebuildNodeGraph();

    UFUNCTION(BlueprintPure, Category = "Town Map|Graph")
    UGF_TownMapNodeWidget* GetNodeById(FName LocationId) const;

    /**
     * Node representing a RouteData name, checking each node's AdditionalRouteIds as well.
     * This is what maps interiors (Tamer School, trial, houses) back onto their city.
     */
    UFUNCTION(BlueprintPure, Category = "Town Map|Graph")
    UGF_TownMapNodeWidget* GetNodeForRouteId(FName RouteId) const;

    /** Topmost node whose region contains a canvas-space point, or null over open sea. */
    UFUNCTION(BlueprintPure, Category = "Town Map|Graph")
    UGF_TownMapNodeWidget* FindNodeAtPosition(FVector2D CanvasPoint) const;

    /** Current loose cursor position in canvas space. Meaningful only in free-roam mode. */
    UFUNCTION(BlueprintPure, Category = "Town Map|Cursor")
    FVector2D GetCursorPosition() const { return CursorFreePos; }

    UFUNCTION(BlueprintPure, Category = "Town Map|Graph")
    const TArray<UGF_TownMapNodeWidget*>& GetNodes() const { return Nodes; }

    //====================================================================================
    // REVEAL
    //====================================================================================

    UFUNCTION(BlueprintPure, Category = "Town Map|Reveal")
    bool IsRevealed(FName LocationId) const;

    /** Replace the whole revealed set and refresh every node's visibility. */
    UFUNCTION(BlueprintCallable, Category = "Town Map|Reveal")
    void SetRevealedLocations(const TSet<FName>& InRevealed);

    UFUNCTION(BlueprintCallable, Category = "Town Map|Reveal")
    void MarkLocationRevealed(FName LocationId);

    /**
     * Reveal every node the player has actually visited, by asking RouteSubsystem whether any
     * of the node's route names (LocationId or AdditionalRouteIds) carries a MapSeen flag.
     *
     * Called automatically on open, so discovery needs no Blueprint wiring at all: walking
     * into an area records the flag, and the map picks it up next time it is opened.
     */
    UFUNCTION(BlueprintCallable, Category = "Town Map|Reveal")
    void RefreshRevealedFromVisitedRoutes();

    //====================================================================================
    // PLAYER POSITION
    //====================================================================================

    /**
     * Read the player's current area from UGF_RouteSubsystem and place the player marker.
     * Called automatically on construct.
     */
    UFUNCTION(BlueprintCallable, Category = "Town Map")
    void SyncPlayerLocationFromRoute();

    UFUNCTION(BlueprintCallable, Category = "Town Map")
    void SetPlayerLocation(FName LocationId);

    //====================================================================================
    // MOUSE
    //====================================================================================

    /** Called by nodes on hover. Ignored unless UINav reports the mouse as active input. */
    void HandleNodeMouseEnter(UGF_TownMapNodeWidget* Node);

    //====================================================================================
    // BLUEPRINT EVENTS
    //====================================================================================

    /** Update the name box here. Either node may be null. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Town Map|Events")
    void OnCursorMoved(UGF_TownMapNodeWidget* FromNode, UGF_TownMapNodeWidget* ToNode);

    /** Nothing lay in that direction -- bump SFX. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Town Map|Events")
    void OnCursorBlocked(EUINavigation Direction);

    UFUNCTION(BlueprintImplementableEvent, Category = "Town Map|Events")
    void OnLocationConfirmed(UGF_TownMapNodeWidget* Node);

    /** Confirm pressed on something that is not a legal target in the current mode. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Town Map|Events")
    void OnConfirmRejected(UGF_TownMapNodeWidget* Node);

    /**
     * Fires alongside the pooled highlight update. Use this instead of HighlightWidgetClass
     * if you want to drive the region overlay yourself.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Town Map|Events")
    void OnHighlightChanged(UGF_TownMapNodeWidget* Node);

protected:

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FNavigationReply NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent, const FNavigationReply& InDefaultReply) override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual void OnSetupCompleted_Implementation() override;

    /**
     * The scoring pass. Candidates must sit inside a cone of half-slope InConeSlope around
     * Dir; the winner minimises (distance along Dir) + (perpendicular offset * PerpBias).
     */
    UGF_TownMapNodeWidget* FindNodeInDirection(FVector2D From, FVector2D Dir, float InConeSlope, const UGF_TownMapNodeWidget* Exclude) const;

    void UpdateHighlight(UGF_TownMapNodeWidget* Node);
    void RefreshNodeRevealStates();
    void ApplyCursorTransform(FVector2D Position, FVector2D Size);

    static FVector2D DirectionToVector(EUINavigation Direction);

    /**
     * Builds the graph, claims focus and places the cursor. Idempotent, and called from
     * both NativeConstruct and OnSetupCompleted so it runs no matter how the widget is
     * opened. Always logs one summary line at Warning level.
     */
    void InitializeMap();

    /** Free-roam step: move the loose cursor, then resolve which region it landed in. */
    bool MoveCursorFreeRoam(EUINavigation Direction);

    /** Adopt a node as the selection without moving the loose cursor onto it. */
    void SetSelectedNode(UGF_TownMapNodeWidget* Node);

    /**
     * Attempt to take keyboard focus, retried on a timer until it succeeds. Logs once per
     * open if it cannot, including UINav's input mode -- in Game mode no widget can hold
     * focus and no navigation event will ever be raised.
     */
    void TryClaimFocus();

    /** Resolves a node's cursor anchor, box size and canvas-space region rect. */
    void ResolveNodeGeometry(UGF_TownMapNodeWidget* Node) const;

    /** Node whose name the UI should show, honouring bKeepNameOutsideRegions. */
    const UGF_TownMapNodeWidget* GetNodeForDisplay() const;

private:

    UPROPERTY(Transient)
    TArray<UGF_TownMapNodeWidget*> Nodes;

    UPROPERTY(Transient)
    UGF_TownMapNodeWidget* CurrentNode = nullptr;

    /** Last node actually entered. Backs bKeepNameOutsideRegions so the name box does not
     *  blank out over open sea while CurrentNode stays honestly null. */
    UPROPERTY(Transient)
    UGF_TownMapNodeWidget* LastNamedNode = nullptr;

    UPROPERTY(Transient)
    TArray<UUserWidget*> HighlightPool;

    UPROPERTY(Transient)
    UCanvasPanelSlot* CursorSlot = nullptr;

    // Cursor slide state
    FVector2D CursorStartPos = FVector2D::ZeroVector;
    FVector2D CursorStartSize = FVector2D::ZeroVector;
    FVector2D CursorTargetPos = FVector2D::ZeroVector;
    FVector2D CursorTargetSize = FVector2D::ZeroVector;
    FVector2D CursorFreePos = FVector2D::ZeroVector;
    float CursorElapsed = 0.0f;
    bool bCursorSliding = false;
    bool bMapInitialized = false;
    bool bFocusWarningLogged = false;

    FTimerHandle FocusRetryTimer;
    FTimerHandle AuditTimer;
};
