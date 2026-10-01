// GF_TownMapWidget.cpp

#include "TownMap/GF_TownMapWidget.h"
#include "GF_RouteSubsystem.h"

#include "UINavPCComponent.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"

//========================================================================================
// LIFECYCLE
//========================================================================================

UGF_TownMapWidget::UGF_TownMapWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Must be set here, not in NativeConstruct: focusability is baked in when the Slate
    // widget is built, and is not modifiable afterwards.
    SetIsFocusable(true);

    // WASD + D-pad only. Arrow keys are deliberately absent: this project uses WASD across
    // all UI and never binds the arrows, so accepting them here would be the odd one out.
    // Add them in Class Defaults if that ever changes -- no rebuild needed.
    KeysUp      = { EKeys::W, EKeys::Gamepad_DPad_Up };
    KeysDown    = { EKeys::S, EKeys::Gamepad_DPad_Down };
    KeysLeft    = { EKeys::A, EKeys::Gamepad_DPad_Left };
    KeysRight   = { EKeys::D, EKeys::Gamepad_DPad_Right };
    KeysConfirm = { EKeys::E, EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom };
}

bool UGF_TownMapWidget::HasMapFocus() const
{
    APlayerController* PC = GetOwningPlayer();
    return PC && (HasUserFocus(PC) || HasUserFocusedDescendants(PC));
}

FReply UGF_TownMapWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (bHandleKeysDirectly)
    {
        const FKey Key = InKeyEvent.GetKey();

        if (bLogNavigationEvents)
        {
            UE_LOG(LogTemp, Warning, TEXT("[TownMap] Key down: %s"), *Key.ToString());
        }

        EUINavigation Direction = EUINavigation::Invalid;
        if      (KeysUp.Contains(Key))    Direction = EUINavigation::Up;
        else if (KeysDown.Contains(Key))  Direction = EUINavigation::Down;
        else if (KeysLeft.Contains(Key))  Direction = EUINavigation::Left;
        else if (KeysRight.Contains(Key)) Direction = EUINavigation::Right;

        if (Direction != EUINavigation::Invalid)
        {
            MoveCursorInDirection(Direction);
            // Handled even on a blocked move, so the key never falls through to the game and
            // walks the player around behind the open map.
            return FReply::Handled();
        }

        if (KeysConfirm.Contains(Key))
        {
            ConfirmCurrentLocation();
            return FReply::Handled();
        }
    }

    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UGF_TownMapWidget::TryClaimFocus()
{
    APlayerController* PC = GetOwningPlayer();
    if (!PC)
    {
        return;
    }

    if (HasUserFocus(PC) || HasUserFocusedDescendants(PC))
    {
        bFocusWarningLogged = false;
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(FocusRetryTimer);
        }
        return;
    }

    // In Game-and-UI the game viewport holds keyboard focus, so asking the widget to take it
    // is simply ignored -- which is exactly what InputMode=1 in the log meant. Re-apply the
    // same input mode with this widget named as the focus target. UINav never sets input mode
    // itself (it only reads it via GetInputMode), so this does not fight the plugin, and it
    // preserves Game-and-UI rather than forcing UI-only and starving the game of input.
    const TSharedPtr<SWidget> SafeWidget = GetCachedWidget();
    if (bTakeInputModeFocus && SafeWidget.IsValid())
    {
        // Named InputModeData, not Mode: the class already has a Mode member (Browse / Fly).
        FInputModeGameAndUI InputModeData;
        InputModeData.SetWidgetToFocus(SafeWidget);
        InputModeData.SetHideCursorDuringCapture(false);
        InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(InputModeData);
    }

    // SetUserFocus targets this specific player; SetKeyboardFocus is the broader fallback.
    SetUserFocus(PC);
    if (!HasUserFocus(PC))
    {
        SetKeyboardFocus();
    }

    // Log the failure once per open, not once per frame.
    if (!HasUserFocus(PC) && !bFocusWarningLogged)
    {
        bFocusWarningLogged = true;
        UE_LOG(LogTemp, Warning, TEXT("[TownMap] Could not take keyboard focus. UINavPC InputMode=%d. If this is Game mode, no widget can hold focus and no navigation event will ever fire."),
            UINavPC ? static_cast<int32>(UINavPC->GetInputMode()) : -1);
    }
}

void UGF_TownMapWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Drive the cursor slot from its center so a node's MapPos can be used verbatim as the
    // box position, independent of the box's size.
    if (CursorWidget)
    {
        CursorSlot = Cast<UCanvasPanelSlot>(CursorWidget->Slot);
        if (CursorSlot)
        {
            // Top-left anchors so SetPosition is plain canvas-local space, matching the
            // node positions computed in ResolveNodeGeometry. Whatever anchors were authored
            // in the designer are irrelevant -- this slot is driven entirely from code.
            CursorSlot->SetAnchors(FAnchors(0.0f, 0.0f));
            CursorSlot->SetAlignment(FVector2D(0.5, 0.5));
            CursorSlot->SetAutoSize(false);
            CursorSlot->SetZOrder(CursorZOrder);
            CursorWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[TownMap] CursorWidget is not a direct child of MapCanvas - the selection box cannot be positioned."));
        }
    }

    SyncPlayerLocationFromRoute();

    // Initialize here rather than only from OnSetupCompleted. OnSetupCompleted is UINav's
    // callback and only fires when the widget is opened through UINav's own flow -- a plain
    // AddToViewport skips it entirely, leaving an empty graph and no focus, which looks
    // exactly like "the cursor is broken" with nothing logged anywhere.
    InitializeMap();
}

void UGF_TownMapWidget::RunMapAudit()
{
    // Force every node to re-measure first: an audit against pre-layout values would report
    // faults that do not exist.
    for (UGF_TownMapNodeWidget* Node : Nodes)
    {
        ResolveNodeGeometry(Node);
    }

    const FVector2D CanvasSize = MapCanvas ? MapCanvas->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;

    UE_LOG(LogTemp, Warning, TEXT("========== [TownMap] AUDIT =========="));
    UE_LOG(LogTemp, Warning, TEXT("Nodes=%d  Canvas=%.0fx%.0f  RevealAll=%s  Revealed=%d  FreeRoam=%s  Step=%.0f"),
        Nodes.Num(), CanvasSize.X, CanvasSize.Y,
        bRevealAllLocations ? TEXT("ON") : TEXT("off"),
        RevealedLocations.Num(),
        bFreeRoamCursor ? TEXT("ON") : TEXT("off"),
        FreeRoamStep);

    int32 Problems = 0;

    for (const UGF_TownMapNodeWidget* Node : Nodes)
    {
        if (!Node)
        {
            continue;
        }

        const FString Id = Node->LocationId.IsNone() ? TEXT("<no LocationId>") : Node->LocationId.ToString();

        if (Node->LocationId.IsNone())
        {
            ++Problems;
            UE_LOG(LogTemp, Warning, TEXT("  [%s] widget '%s' has NO LocationId -- it is skipped entirely. FIX: set LocationId."),
                *Id, *Node->GetName());
            continue;
        }

        if (Node->RegionSize.X <= 0.0 || Node->RegionSize.Y <= 0.0)
        {
            ++Problems;
            UE_LOG(LogTemp, Warning, TEXT("  [%s] region is %.1fx%.1f -- can never be hovered. FIX: give the node a real size on the canvas."),
                *Id, Node->RegionSize.X, Node->RegionSize.Y);
            continue;
        }

        // Deliberately does NOT 'continue'. Reveal state is a gameplay fact, not an authoring
        // fault, and skipping here meant structural checks below only ever ran on the handful
        // of discovered nodes -- which is why mistyped route regions went unreported.
        if (!bRevealAllLocations && !RevealedLocations.Contains(Node->LocationId))
        {
            UE_LOG(LogTemp, Warning, TEXT("  [%s] not revealed -- intentionally inert (unshipped area?)."), *Id);
        }

        // MarkerType defaults to Town, so any route region node whose type was never set
        // reports as an icon -- which makes HasVisibleMarker() true and paints the whole
        // route bar. Large regions are the giveaway: a city marker is tens of units, a route
        // stretches hundreds.
        if (Node->HasVisibleMarker()
            && (Node->RegionSize.X > 120.0 || Node->RegionSize.Y > 120.0))
        {
            ++Problems;
            UE_LOG(LogTemp, Warning, TEXT("  [%s] MarkerType is an icon type but the region is %.0fx%.0f -- almost certainly a route region left on the default 'Town'. FIX: set Marker Type = Route (or Water) on this node."),
                *Id, Node->RegionSize.X, Node->RegionSize.Y);
        }

        if (Node->HasVisibleMarker())
        {
            // No MarkerImage is fine and now the norm: GetRenderBoundingRect measures the node
            // root correctly through any render transform, so the marker override is only
            // useful when the art is genuinely smaller than the root.
            if (Node->MarkerImage)
            {
                // A brush with no texture renders as a plain white box -- exactly the
                // "white icons with no explanation" symptom.
                if (!Node->MarkerImage->GetBrush().GetResourceObject())
                {
                    ++Problems;
                    UE_LOG(LogTemp, Warning, TEXT("  [%s] MarkerImage has NO texture on its brush -- it draws as a WHITE BOX. FIX: delete this Image (the node root now measures correctly on its own), or assign its brush texture."),
                        *Id);
                }
            }
        }
    }

    if (Problems == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("  No authoring problems found."));
    }

    // Report what each icon node's child Images are ACTUALLY showing at runtime, with the
    // reveal state that produced it. If an undiscovered node still shows its colour texture,
    // the reveal handler is not applying the swap; if it shows the grey one, something later
    // is overwriting it. This distinguishes the two without more guesswork.
    int32 Reported = 0;
    for (const UGF_TownMapNodeWidget* Node : Nodes)
    {
        if (!Node || !Node->HasVisibleMarker() || Reported >= 6 || !Node->WidgetTree)
        {
            continue;
        }
        ++Reported;

        const bool bNodeRevealed = IsRevealed(Node->LocationId);
        FString Images;

        Node->WidgetTree->ForEachWidget([&Images](UWidget* Child)
        {
            if (const UImage* Img = Cast<UImage>(Child))
            {
                const FSlateBrush& Brush = Img->GetBrush();
                const UObject* Res = Brush.GetResourceObject();

                // Texture alone does not decide what is drawn. A coloured ColorAndOpacity or
                // brush TintColor over a grey texture still renders coloured, and Set Brush
                // from Texture leaves both untouched.
                const FLinearColor CO = Img->GetColorAndOpacity();
                const FLinearColor Tint = Brush.TintColor.GetSpecifiedColor();

                Images += FString::Printf(
                    TEXT("\n      %s tex='%s' colorOpacity=(%.2f,%.2f,%.2f,%.2f) brushTint=(%.2f,%.2f,%.2f,%.2f) renderOpacity=%.2f vis=%d"),
                    *Img->GetName(),
                    Res ? *Res->GetName() : TEXT("<none>"),
                    CO.R, CO.G, CO.B, CO.A,
                    Tint.R, Tint.G, Tint.B, Tint.A,
                    Img->GetRenderOpacity(),
                    static_cast<int32>(Img->GetVisibility()));
            }
            else if (const UUserWidget* Nested = Cast<UUserWidget>(Child))
            {
                // Images inside a nested UserWidget live in ITS WidgetTree, not this one, so
                // they would otherwise be invisible to this report -- and could well be what
                // is actually drawing the coloured marker.
                Images += FString::Printf(TEXT("\n      [nested widget '%s' of class %s -- its images are not listed here]"),
                    *Nested->GetName(), *Nested->GetClass()->GetName());
            }
        });

        UE_LOG(LogTemp, Warning, TEXT("  [%s] revealed=%s images:%s"),
            *Node->LocationId.ToString(),
            bNodeRevealed ? TEXT("yes") : TEXT("NO"),
            Images.IsEmpty() ? TEXT(" <no UImage children>") : *Images);
    }

    UE_LOG(LogTemp, Warning, TEXT("Player route '%s' -> %s"),
        *PlayerLocationId.ToString(),
        GetNodeForRouteId(PlayerLocationId)
            ? *GetNodeForRouteId(PlayerLocationId)->LocationId.ToString()
            : TEXT("NO NODE. FIX: add this route name to a node's AdditionalRouteIds."));

    UE_LOG(LogTemp, Warning, TEXT("========== [TownMap] %d problem(s) =========="), Problems);
}

void UGF_TownMapWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(FocusRetryTimer);
        World->GetTimerManager().ClearTimer(AuditTimer);
    }

    Super::NativeDestruct();
}

void UGF_TownMapWidget::OnSetupCompleted_Implementation()
{
    Super::OnSetupCompleted_Implementation();

    // Idempotent: whichever of the two paths runs first wins, the other is a no-op.
    InitializeMap();
}

void UGF_TownMapWidget::InitializeMap()
{
    if (bMapInitialized)
    {
        return;
    }
    bMapInitialized = true;

    RebuildNodeGraph();

    // Discovery is automatic: walking into an area writes a MapSeen story flag, and this
    // turns those flags into revealed nodes. No Blueprint wiring required.
    RefreshRevealedFromVisitedRoutes();

    // This widget has no UINavComponents, so UINav has nothing to hand focus to. Claim it
    // ourselves -- Slate only raises navigation events along the focused widget's path, so
    // without this the cursor never receives a single input.
    // Expect this to fail here: at construct time the widget is not in the viewport yet.
    // A repeating timer retries until it sticks. Deliberately a timer rather than relying on
    // NativeTick: UUserWidget's TickFrequency defaults to Auto, which can resolve to "never
    // tick", and that would make the retry silently never run.
    TryClaimFocus();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            FocusRetryTimer, this, &UGF_TownMapWidget::TryClaimFocus, 0.1f, /*bLoop=*/true);

        // Delayed so layout has definitely run -- auditing pre-layout geometry would report
        // faults that are not real.
        if (bAuditOnOpen)
        {
            World->GetTimerManager().SetTimer(
                AuditTimer, this, &UGF_TownMapWidget::RunMapAudit, 0.5f, /*bLoop=*/false);
        }
    }

    // Open on the player's own location when we can, otherwise the first revealed node so
    // the map never comes up with an empty name box.
    if (!SetCursorToLocation(PlayerLocationId, /*bInstant=*/true))
    {
        for (UGF_TownMapNodeWidget* Node : Nodes)
        {
            if (Node && !Node->bSkipDirectionalNavigation && IsRevealed(Node->LocationId))
            {
                SetCursorToNode(Node, /*bInstant=*/true);
                break;
            }
        }
    }

    // Unconditional and at Warning, matching how the rest of this project logs, so a run
    // either produces this line or definitively did not create the widget. No ambiguity.
    APlayerController* PC = GetOwningPlayer();   // non-const: HasUserFocus takes APlayerController*
    UE_LOG(LogTemp, Warning, TEXT("[TownMap] Init: Nodes=%d Revealed=%d Cursor=%s Focused=%s CanvasBound=%s UINavPC=%s InputMode=%d Focusable=%s"),
        Nodes.Num(),
        bRevealAllLocations ? -1 : RevealedLocations.Num(),
        CurrentNode ? *CurrentNode->LocationId.ToString() : TEXT("NONE"),
        (PC && HasUserFocus(PC)) ? TEXT("yes") : TEXT("NO"),
        MapCanvas ? TEXT("yes") : TEXT("NO"),
        UINavPC ? TEXT("yes") : TEXT("NO"),
        UINavPC ? static_cast<int32>(UINavPC->GetInputMode()) : -1,
        IsFocusable() ? TEXT("yes") : TEXT("NO"));

    // Cursor placement, logged separately: if the box is invisible this says whether it is
    // mispositioned, zero-sized, or has no canvas slot at all.
    UE_LOG(LogTemp, Warning, TEXT("[TownMap] Cursor slot: Bound=%s Pos=%s Size=%s"),
        CursorSlot ? TEXT("yes") : TEXT("NO"),
        *CursorTargetPos.ToString(),
        *CursorTargetSize.ToString());
}

void UGF_TownMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // Same reclaim the dialogue widget does: alt-tab and stray viewport clicks drop keyboard
    // focus silently, and a map with no focus takes no input at all.
    if (bReclaimKeyboardFocus)
    {
        TryClaimFocus();
    }

    if (!bCursorSliding)
    {
        // Re-resolve EVERY node against live geometry, not just the current one.
        //
        // RebuildNodeGraph runs from NativeConstruct, before layout exists, so every node
        // falls back to anchor-relative slot values that are in the wrong coordinate space.
        // Only re-resolving the selected node left all the others holding those bad rects,
        // so free-roam's hit-test missed them. This also keeps regions correct across a
        // window resize, which genuinely does move everything in canvas space.
        for (UGF_TownMapNodeWidget* Node : Nodes)
        {
            ResolveNodeGeometry(Node);
        }

        // Keep the player marker glued to its node. SetPlayerLocation runs at construct time
        // when geometry is still zero, so the marker was being placed from the anchor-relative
        // fallback and then never touched again -- which is why it appeared to do nothing.
        if (PlayerMarkerWidget)
        {
            UGF_TownMapNodeWidget* PlayerNode = GetNodeForRouteId(PlayerLocationId);
            UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(PlayerMarkerWidget->Slot);

            if (PlayerNode && MarkerSlot)
            {
                ResolveNodeGeometry(PlayerNode);
                MarkerSlot->SetAnchors(FAnchors(0.0f, 0.0f));
                MarkerSlot->SetAlignment(FVector2D(0.5, 0.5));
                MarkerSlot->SetPosition(PlayerNode->MapPos);
                MarkerSlot->SetZOrder(CursorZOrder - 1);   // under the cursor, over the markers
                PlayerMarkerWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
            else if (!PlayerNode)
            {
                PlayerMarkerWidget->SetVisibility(ESlateVisibility::Collapsed);
            }
        }

        // Free-roam seeding. The initial placement in InitializeMap necessarily runs before
        // layout exists, so it uses the anchor-relative fallback and lands off-canvas (the
        // X=-454 in the logs). Re-seat on the player's own location the moment real geometry
        // is available. Self-limiting: once in bounds MoveCursorFreeRoam clamps it there, so
        // this cannot fight the player's own movement.
        if (bFreeRoamCursor && MapCanvas)
        {
            const FVector2D CanvasSize = MapCanvas->GetCachedGeometry().GetLocalSize();
            const bool bOutOfBounds = CanvasSize.X > 0.0 && CanvasSize.Y > 0.0
                && (CursorFreePos.X < 0.0 || CursorFreePos.Y < 0.0
                    || CursorFreePos.X > CanvasSize.X || CursorFreePos.Y > CanvasSize.Y);

            if (bOutOfBounds)
            {
                UGF_TownMapNodeWidget* StartNode = GetNodeForRouteId(PlayerLocationId);
                if (StartNode)
                {
                    ResolveNodeGeometry(StartNode);
                    CursorFreePos = StartNode->MapPos;
                    CursorTargetSize = StartNode->HasVisibleMarker()
                        ? StartNode->ResolvedCursorSize
                        : DefaultCursorSize;
                    SetSelectedNode(StartNode);
                }
                else
                {
                    // No node for the player's area: centre the cursor rather than strand it
                    // off-screen where the player cannot find it.
                    CursorFreePos = CanvasSize * 0.5;
                    CursorTargetSize = DefaultCursorSize;
                }

                CursorTargetPos = CursorFreePos;
                bCursorSliding = false;
                ApplyCursorTransform(CursorTargetPos, CursorTargetSize);

                // Fires once per open: after seeding the cursor is in bounds and clamped there.
                UE_LOG(LogTemp, Warning, TEXT("[TownMap] Seeded cursor: playerRoute='%s' startNode=%s pos=(%.0f,%.0f) canvas=%.0fx%.0f"),
                    *PlayerLocationId.ToString(),
                    StartNode ? *StartNode->LocationId.ToString() : TEXT("NONE (centred instead)"),
                    CursorFreePos.X, CursorFreePos.Y,
                    CanvasSize.X, CanvasSize.Y);
            }
        }

        // Drift correction is node-hop only. In free-roam the cursor is deliberately NOT at
        // the node's anchor -- it sits wherever the player roamed to -- so snapping it back
        // to MapPos here would drag it onto the marker every frame and make it impossible to
        // move off a location once entered.
        if (!bFreeRoamCursor && CurrentNode)
        {
            if (!CurrentNode->MapPos.Equals(CursorTargetPos, 0.5)
                || !CurrentNode->ResolvedCursorSize.Equals(CursorTargetSize, 0.5))
            {
                CursorTargetPos = CurrentNode->MapPos;
                CursorTargetSize = CurrentNode->ResolvedCursorSize;
                ApplyCursorTransform(CursorTargetPos, CursorTargetSize);
            }
        }
        return;
    }

    CursorElapsed += InDeltaTime;

    const float Alpha = (CursorMoveTime <= KINDA_SMALL_NUMBER)
        ? 1.0f
        : FMath::Clamp(CursorElapsed / CursorMoveTime, 0.0f, 1.0f);

    // Ease-out: fast off the mark, settles onto the marker.
    const float Eased = 1.0f - FMath::Square(1.0f - Alpha);

    ApplyCursorTransform(
        FMath::Lerp(CursorStartPos, CursorTargetPos, Eased),
        FMath::Lerp(CursorStartSize, CursorTargetSize, Eased));

    if (Alpha >= 1.0f)
    {
        bCursorSliding = false;
    }
}

//========================================================================================
// INPUT
//========================================================================================

FNavigationReply UGF_TownMapWidget::NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent, const FNavigationReply& InDefaultReply)
{
    const EUINavigation Direction = InNavigationEvent.GetNavigationType();

    if (bLogNavigationEvents)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TownMap] Navigation event: %d (CurrentNode=%s, Nodes=%d)"),
            static_cast<int32>(Direction),
            CurrentNode ? *CurrentNode->LocationId.ToString() : TEXT("none"),
            Nodes.Num());
    }

    switch (Direction)
    {
        case EUINavigation::Up:
        case EUINavigation::Down:
        case EUINavigation::Left:
        case EUINavigation::Right:
            MoveCursorInDirection(Direction);
            // Stop unconditionally, including on a blocked move. Letting a failed move fall
            // through to Slate navigation would hand focus to something outside the map.
            return FNavigationReply::Stop();

        default:
            return Super::NativeOnNavigation(MyGeometry, InNavigationEvent, InDefaultReply);
    }
}

FVector2D UGF_TownMapWidget::DirectionToVector(EUINavigation Direction)
{
    // Canvas design space: +Y is down.
    switch (Direction)
    {
        case EUINavigation::Up:    return FVector2D(0.0, -1.0);
        case EUINavigation::Down:  return FVector2D(0.0, 1.0);
        case EUINavigation::Left:  return FVector2D(-1.0, 0.0);
        case EUINavigation::Right: return FVector2D(1.0, 0.0);
        default:                   return FVector2D::ZeroVector;
    }
}

//========================================================================================
// CURSOR
//========================================================================================

bool UGF_TownMapWidget::MoveCursorInDirection(EUINavigation Direction)
{
    const FVector2D Dir = DirectionToVector(Direction);
    if (Dir.IsNearlyZero())
    {
        return false;
    }

    if (bFreeRoamCursor)
    {
        return MoveCursorFreeRoam(Direction);
    }

    if (!CurrentNode)
    {
        OnCursorBlocked(Direction);
        return false;
    }

    // Hand-authored override wins outright, so a designer can always force a move.
    const FName Override = CurrentNode->GetOverrideForDirection(Direction);
    if (!Override.IsNone())
    {
        if (UGF_TownMapNodeWidget* Target = GetNodeById(Override))
        {
            if (IsRevealed(Target->LocationId))
            {
                SetCursorToNode(Target);
                return true;
            }
        }
        // An override pointing at an undiscovered or missing location falls through to
        // scoring rather than trapping the cursor.
    }

    UGF_TownMapNodeWidget* Best = FindNodeInDirection(CurrentNode->MapPos, Dir, ConeSlope, CurrentNode);

    // Widen once before giving up, otherwise isolated islands become dead ends.
    if (!Best && WideConeSlope > ConeSlope)
    {
        Best = FindNodeInDirection(CurrentNode->MapPos, Dir, WideConeSlope, CurrentNode);
    }

    if (!Best)
    {
        OnCursorBlocked(Direction);
        return false;
    }

    SetCursorToNode(Best);
    return true;
}

bool UGF_TownMapWidget::MoveCursorFreeRoam(EUINavigation Direction)
{
    const FVector2D Dir = DirectionToVector(Direction);
    FVector2D NewPos = CursorFreePos + Dir * FreeRoamStep;

    // Keep the cursor on the map. Hitting an edge is a blocked move, not a silent no-op.
    if (MapCanvas)
    {
        const FVector2D CanvasSize = MapCanvas->GetCachedGeometry().GetLocalSize();
        if (CanvasSize.X > 0.0 && CanvasSize.Y > 0.0)
        {
            const FVector2D Clamped(
                FMath::Clamp(NewPos.X, 0.0, CanvasSize.X),
                FMath::Clamp(NewPos.Y, 0.0, CanvasSize.Y));

            if (Clamped.Equals(CursorFreePos, 0.01))
            {
                OnCursorBlocked(Direction);
                return false;
            }
            NewPos = Clamped;
        }
    }

    UGF_TownMapNodeWidget* NodeHere = FindNodeAtPosition(NewPos);
    const bool bEnteredNewNode = (NodeHere != CurrentNode);

    if (bLogNavigationEvents)
    {
        // A node with a zero-size region is invisible to the hit-test. That is the signature
        // of a Collapsed widget (Collapsed has no geometry; Hidden still does) or a node that
        // was never stretched over its route.
        int32 ZeroRegionCount = 0;
        FString ZeroRegionNames;
        for (const UGF_TownMapNodeWidget* Node : Nodes)
        {
            if (Node && (Node->RegionSize.X <= 0.0 || Node->RegionSize.Y <= 0.0))
            {
                ++ZeroRegionCount;
                if (ZeroRegionCount <= 4)
                {
                    // vis=4 (SelfHitTestInvisible, the UUserWidget default) already ruled out
                    // visibility, so report the authored slot instead: an auto-sizing slot
                    // whose content has no desired size measures zero however visible it is.
                    const UCanvasPanelSlot* NodeSlot = Cast<UCanvasPanelSlot>(Node->Slot);
                    const FVector2D SlotSize = NodeSlot ? NodeSlot->GetSize() : FVector2D::ZeroVector;
                    const FVector2D DesiredSize = Node->GetDesiredSize();
                    const FVector2D GeoSize = Node->GetCachedGeometry().GetLocalSize();

                    ZeroRegionNames += FString::Printf(
                        TEXT("%s%s[slot=%s auto=%s slotSize=%.1fx%.1f desired=%.1fx%.1f geo=%.1fx%.1f vis=%d]"),
                        ZeroRegionCount > 1 ? TEXT(", ") : TEXT(""),
                        *Node->LocationId.ToString(),
                        NodeSlot ? TEXT("canvas") : TEXT("NONE"),
                        (NodeSlot && NodeSlot->GetAutoSize()) ? TEXT("yes") : TEXT("no"),
                        SlotSize.X, SlotSize.Y,
                        DesiredSize.X, DesiredSize.Y,
                        GeoSize.X, GeoSize.Y,
                        static_cast<int32>(Node->GetVisibility()));
                }
            }
        }

        if (ZeroRegionCount > 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("[TownMap] %d node(s) have a zero-size region and can never be hovered. First few: %s"),
                ZeroRegionCount, *ZeroRegionNames);
        }

        // Marker diagnostics: says whether MarkerImage bound at all, its measured size, and
        // the MarkerType -- measurement only switches to the art when HasVisibleMarker() is
        // true AND the image is bound AND it has non-zero geometry.
        FString MarkerInfo(TEXT("-"));
        if (NodeHere)
        {
            const FVector2D MarkerSize = NodeHere->MarkerImage
                ? NodeHere->MarkerImage->GetCachedGeometry().GetLocalSize()
                : FVector2D::ZeroVector;

            MarkerInfo = FString::Printf(TEXT("bound=%s size=%.1fx%.1f type=%d visibleMarker=%s"),
                NodeHere->MarkerImage ? TEXT("yes") : TEXT("NO"),
                MarkerSize.X, MarkerSize.Y,
                static_cast<int32>(NodeHere->MarkerType),
                NodeHere->HasVisibleMarker() ? TEXT("yes") : TEXT("NO"));
        }

        UE_LOG(LogTemp, Warning, TEXT("[TownMap] FreeRoam pos=(%.0f,%.0f) Node=%s Region=%s size=%s | Marker: %s | ZeroRegionNodes=%d/%d"),
            NewPos.X, NewPos.Y,
            NodeHere ? *NodeHere->LocationId.ToString() : TEXT("NONE"),
            NodeHere ? *NodeHere->RegionPos.ToString() : TEXT("-"),
            NodeHere ? *NodeHere->RegionSize.ToString() : TEXT("-"),
            *MarkerInfo,
            ZeroRegionCount, Nodes.Num());
    }

    // Magnetism fires ONLY on entering a node, never while already inside it -- otherwise the
    // cursor would be pinned to the marker and could never travel back out.
    if (bEnteredNewNode && NodeHere && bMagnetiseToIcons && NodeHere->HasVisibleMarker())
    {
        ResolveNodeGeometry(NodeHere);
        NewPos = NodeHere->MapPos;
    }

    CursorFreePos = NewPos;

    if (bEnteredNewNode)
    {
        SetSelectedNode(NodeHere);
    }

    // Vault frames the marker when magnetised to an icon, otherwise stays the free cursor size.
    const bool bFramingIcon = NodeHere && bMagnetiseToIcons && NodeHere->HasVisibleMarker();
    CursorTargetPos = CursorFreePos;
    CursorTargetSize = bFramingIcon ? NodeHere->ResolvedCursorSize : DefaultCursorSize;

    if (CursorMoveTime <= KINDA_SMALL_NUMBER)
    {
        bCursorSliding = false;
        ApplyCursorTransform(CursorTargetPos, CursorTargetSize);
    }
    else
    {
        CursorStartPos = CursorSlot ? CursorSlot->GetPosition() : CursorTargetPos;
        CursorStartSize = CursorSlot ? CursorSlot->GetSize() : CursorTargetSize;
        CursorElapsed = 0.0f;
        bCursorSliding = true;
    }

    return true;
}

void UGF_TownMapWidget::SetSelectedNode(UGF_TownMapNodeWidget* Node)
{
    if (Node == CurrentNode)
    {
        return;
    }

    UGF_TownMapNodeWidget* PreviousNode = CurrentNode;
    if (PreviousNode)
    {
        PreviousNode->OnCursorExit();
    }

    CurrentNode = Node;
    if (Node)
    {
        LastNamedNode = Node;
        Node->OnCursorEnter();
        UpdateHighlight(Node);
    }
    else if (!bKeepNameOutsideRegions)
    {
        UpdateHighlight(nullptr);
    }

    OnCursorMoved(PreviousNode, Node);
}

UGF_TownMapNodeWidget* UGF_TownMapWidget::FindNodeAtPosition(FVector2D CanvasPoint) const
{
    // Test the cursor BOX against each region, not the cursor's centre point.
    //
    // A city marker is only a few units across, so with a 16-unit step a point test steps
    // clean over it and the location never registers. Expanding each region by half the
    // cursor size makes this "the cursor overlaps the location", which is both what the
    // player sees and what makes small icons reliably catchable.
    // Two passes: exact containment first, then a padded retry.
    //
    // Pass 1 (no padding) answers "what is the cursor actually on". Pass 2 only runs when
    // that finds nothing, and widens each region so small markers stay catchable at a 16-unit
    // step. Padding on every pass was the bug that ate Route 101: Hearthvale and Brookfield each
    // grew by half a cursor, closing the gap between them, and being smaller in area they beat
    // the route the cursor was genuinely sitting on.
    //
    // Within a pass the SMALLEST region wins, so a town icon beats the route region it sits
    // inside -- the specific place beats the general area.
    const FVector2D Passes[] = { FVector2D::ZeroVector, DefaultCursorSize * 0.5 };

    for (const FVector2D& Pad : Passes)
    {
        UGF_TownMapNodeWidget* Best = nullptr;
        double BestArea = TNumericLimits<double>::Max();

        for (int32 Index = Nodes.Num() - 1; Index >= 0; --Index)
        {
            UGF_TownMapNodeWidget* Node = Nodes[Index];
            if (!Node || Node->RegionSize.X <= 0.0 || Node->RegionSize.Y <= 0.0)
            {
                continue;
            }

            // untagged nodes are still hoverable when allowed -- they report as "???"
            // rather than reading as empty sea. Confirm still rejects them (CanConfirmNode).
            if (!IsRevealed(Node->LocationId) && !bAllowHoveruntagged)
            {
                continue;
            }

            const FVector2D Min = Node->RegionPos - Pad;
            const FVector2D Max = Node->RegionPos + Node->RegionSize + Pad;

            if (CanvasPoint.X >= Min.X && CanvasPoint.X <= Max.X
                && CanvasPoint.Y >= Min.Y && CanvasPoint.Y <= Max.Y)
            {
                const double Area = Node->RegionSize.X * Node->RegionSize.Y;
                if (Area < BestArea)
                {
                    BestArea = Area;
                    Best = Node;
                }
            }
        }

        if (Best)
        {
            return Best;
        }
    }

    return nullptr;
}

UGF_TownMapNodeWidget* UGF_TownMapWidget::GetNodeForRouteId(FName RouteId) const
{
    for (UGF_TownMapNodeWidget* Node : Nodes)
    {
        if (Node && Node->MatchesRouteId(RouteId))
        {
            return Node;
        }
    }
    return nullptr;
}

UGF_TownMapNodeWidget* UGF_TownMapWidget::FindNodeInDirection(FVector2D From, FVector2D Dir, float InConeSlope, const UGF_TownMapNodeWidget* Exclude) const
{
    UGF_TownMapNodeWidget* Best = nullptr;
    double BestScore = TNumericLimits<double>::Max();

    for (UGF_TownMapNodeWidget* Node : Nodes)
    {
        if (!Node || Node == Exclude || Node->bSkipDirectionalNavigation)
        {
            continue;
        }

        // Nodes sharing the current location's id are alternate anchors on the same route;
        // they are legal targets, which is how the cursor travels along a long route.
        // untagged nodes stay navigable when hovering them is allowed, so node-hop mode
        // matches free-roam rather than silently having a different set of destinations.
        if (!IsRevealed(Node->LocationId) && !bAllowHoveruntagged)
        {
            continue;
        }

        const FVector2D D = Node->MapPos - From;

        const double Along = FVector2D::DotProduct(D, Dir);
        if (Along <= KINDA_SMALL_NUMBER)
        {
            continue;   // behind the cursor, or exactly perpendicular
        }

        const double Perp = FMath::Abs(FVector2D::CrossProduct(D, Dir));
        if (Perp > Along * InConeSlope)
        {
            continue;   // outside the acceptance cone
        }

        const double Score = Along + Perp * PerpBias;
        if (Score < BestScore)
        {
            BestScore = Score;
            Best = Node;
        }
    }

    return Best;
}

void UGF_TownMapWidget::SetCursorToNode(UGF_TownMapNodeWidget* Node, bool bInstant)
{
    if (!Node || Node == CurrentNode)
    {
        return;
    }

    UGF_TownMapNodeWidget* PreviousNode = CurrentNode;
    if (PreviousNode)
    {
        PreviousNode->OnCursorExit();
    }

    CurrentNode = Node;
    LastNamedNode = Node;
    Node->OnCursorEnter();

    // Geometry is resolved lazily as well as at graph build, so nodes whose slot was still
    // being laid out at setup time correct themselves the first time they are selected.
    ResolveNodeGeometry(Node);

    CursorTargetPos = Node->MapPos;
    CursorTargetSize = Node->ResolvedCursorSize;

    // Keep the loose cursor in sync so free-roam resumes from wherever this put us -- this is
    // the path used by init, mouse hover and SetCursorToLocation.
    CursorFreePos = Node->MapPos;

    if (bInstant || CursorMoveTime <= KINDA_SMALL_NUMBER)
    {
        bCursorSliding = false;
        ApplyCursorTransform(CursorTargetPos, CursorTargetSize);
    }
    else
    {
        CursorStartPos = CursorSlot ? CursorSlot->GetPosition() : CursorTargetPos;
        CursorStartSize = CursorSlot ? CursorSlot->GetSize() : CursorTargetSize;
        CursorElapsed = 0.0f;
        bCursorSliding = true;
    }

    UpdateHighlight(Node);
    OnCursorMoved(PreviousNode, Node);
}

bool UGF_TownMapWidget::SetCursorToLocation(FName LocationId, bool bInstant)
{
    if (LocationId.IsNone())
    {
        return false;
    }

    // GetNodeForRouteId, so this accepts an interior's route name as well as a node id.
    UGF_TownMapNodeWidget* Node = GetNodeForRouteId(LocationId);
    if (!Node || !IsRevealed(Node->LocationId))
    {
        return false;
    }

    SetCursorToNode(Node, bInstant);
    return true;
}

void UGF_TownMapWidget::ApplyCursorTransform(FVector2D Position, FVector2D Size)
{
    // Resolve the slot lazily rather than trusting NativeConstruct to have done it.
    // UINav runs OnSetupCompleted from inside Super::NativeConstruct(), so InitializeMap can
    // fire BEFORE this class's NativeConstruct body caches CursorSlot -- and bMapInitialized
    // then makes the later pass a no-op, leaving the slot permanently null and every cursor
    // move silently discarded.
    if (!CursorSlot && CursorWidget)
    {
        CursorSlot = Cast<UCanvasPanelSlot>(CursorWidget->Slot);
        if (CursorSlot)
        {
            CursorSlot->SetAnchors(FAnchors(0.0f, 0.0f));
            CursorSlot->SetAlignment(FVector2D(0.5, 0.5));
            CursorSlot->SetAutoSize(false);
            CursorSlot->SetZOrder(CursorZOrder);
            CursorWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }

    if (!CursorSlot)
    {
        return;
    }

    CursorSlot->SetPosition(Position);
    CursorSlot->SetSize(Size);
}

const UGF_TownMapNodeWidget* UGF_TownMapWidget::GetNodeForDisplay() const
{
    if (CurrentNode)
    {
        return CurrentNode;
    }
    // Over open sea CurrentNode is honestly null; the name box optionally holds the last one.
    return bKeepNameOutsideRegions ? LastNamedNode : nullptr;
}

bool UGF_TownMapWidget::IsCurrentLocationDiscovered() const
{
    const UGF_TownMapNodeWidget* Node = GetNodeForDisplay();
    return Node && IsRevealed(Node->LocationId);
}

FText UGF_TownMapWidget::GetCurrentLocationName() const
{
    const UGF_TownMapNodeWidget* Node = GetNodeForDisplay();
    if (!Node)
    {
        return FText::GetEmpty();
    }

    // Somewhere is there, but the player has not been told what.
    return IsRevealed(Node->LocationId) ? Node->DisplayName : untaggedName;
}

FText UGF_TownMapWidget::GetCurrentLocationSubtitle() const
{
    const UGF_TownMapNodeWidget* Node = GetNodeForDisplay();

    // No flavour text for an undiscovered place -- it would give away what "???" is hiding.
    if (!Node || !IsRevealed(Node->LocationId))
    {
        return FText::GetEmpty();
    }

    return Node->Subtitle;
}

bool UGF_TownMapWidget::CanConfirmNode(const UGF_TownMapNodeWidget* Node) const
{
    if (!Node || !IsRevealed(Node->LocationId))
    {
        return false;
    }

    switch (Mode)
    {
        case EGF_GETownMapMode::Fly:    return Node->bCanFlyTo;
        case EGF_GETownMapMode::Browse: return true;
        default:                     return false;
    }
}

void UGF_TownMapWidget::ConfirmCurrentLocation()
{
    if (CanConfirmNode(CurrentNode))
    {
        OnLocationConfirmed(CurrentNode);
    }
    else
    {
        OnConfirmRejected(CurrentNode);
    }
}

//========================================================================================
// GRAPH
//========================================================================================

void UGF_TownMapWidget::RebuildNodeGraph()
{
    Nodes.Reset();

    if (!MapCanvas)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TownMap] MapCanvas is not bound - no nodes to build."));
        return;
    }

    for (UWidget* Child : MapCanvas->GetAllChildren())
    {
        UGF_TownMapNodeWidget* Node = Cast<UGF_TownMapNodeWidget>(Child);
        if (!Node)
        {
            continue;
        }

        if (Node->LocationId.IsNone())
        {
            UE_LOG(LogTemp, Warning, TEXT("[TownMap] Node '%s' has no LocationId and was skipped."), *Node->GetName());
            continue;
        }

        Node->OwnerMap = this;
        ResolveNodeGeometry(Node);
        Nodes.Add(Node);
    }

    // Nodes nested inside another panel never get a UCanvasPanelSlot, so their position
    // would silently read as zero. Catch that here rather than in the field.
    if (WidgetTree)
    {
        WidgetTree->ForEachWidget([this](UWidget* Widget)
        {
            UGF_TownMapNodeWidget* Node = Cast<UGF_TownMapNodeWidget>(Widget);
            if (Node && !Nodes.Contains(Node))
            {
                UE_LOG(LogTemp, Warning, TEXT("[TownMap] Node '%s' is not a direct child of MapCanvas and is unreachable. Skill it onto the map canvas."), *Node->GetName());
            }
        });
    }

    RefreshNodeRevealStates();
}

/** The widget a node should be measured from: its marker art if it names one, else itself. */
static const UWidget* MarkerGeometrySource(const UGF_TownMapNodeWidget* Node)
{
    // Icon nodes only. Route/Water regions must keep measuring their stretched root -- they
    // share this Blueprint class, so measuring a marker image would collapse a whole route
    // down to the size of an icon.
    // Written as an early return, not a ternary: UImage* and const UGF_TownMapNodeWidget*
    // have no common type, so a ternary will not compile.
    // BOTH axes must be non-zero. Checking width alone let a marker measuring e.g. 94x0
    // through, which produced a zero-height region -- the node then became impossible to
    // reach with the cursor while the mouse (which uses UMG hit-testing, not these rects)
    // still worked perfectly.
    if (Node->HasVisibleMarker() && Node->MarkerImage)
    {
        const FVector2D MarkerSize = Node->MarkerImage->GetCachedGeometry().GetLocalSize();
        if (MarkerSize.X > 0.0f && MarkerSize.Y > 0.0f)
        {
            return Node->MarkerImage;
        }
    }

    return Node;
}

void UGF_TownMapWidget::ResolveNodeGeometry(UGF_TownMapNodeWidget* Node) const
{
    if (!Node)
    {
        return;
    }

    FVector2D SlotPos = FVector2D::ZeroVector;
    FVector2D SlotSize = FVector2D::ZeroVector;
    bool bFromGeometry = false;

    // Preferred source: real laid-out geometry, converted into canvas-local space.
    //
    // A raw CanvasPanelSlot position is ANCHOR-RELATIVE, so two widgets with different
    // anchors report positions in different coordinate spaces -- reading a node's slot and
    // applying it to the cursor's slot silently throws the cursor off-screen. Cached geometry
    // sidesteps that entirely, and also copes with auto-size and any ScaleBox above the map.
    if (MapCanvas)
    {
        const FGeometry& CanvasGeo = MapCanvas->GetCachedGeometry();

        // Measure the marker art when the node exposes one. The node root is usually a fixed
        // box sized for the widest marker, so measuring it gives a vertical capsule a
        // horizontal cursor box offset to the right of the art it is supposed to frame.
        const FGeometry& NodeGeo = MarkerGeometrySource(Node)->GetCachedGeometry();

        if (CanvasGeo.GetLocalSize().X > 0.0f && NodeGeo.GetLocalSize().X > 0.0f)
        {
            // GetRenderBoundingRect, NOT GetLocalSize.
            //
            // A Render Transform does not affect layout, so GetLocalSize keeps reporting the
            // UNROTATED box: a horizontal capsule rotated 90 degrees still measures 76x36
            // while what is drawn is 36x76, somewhere else entirely. GetRenderBoundingRect
            // is the transformed rect -- the same one Slate hit-tests against, which is why
            // the mouse always landed correctly while the keyboard cursor sat off to the side.
            // It also covers scale and translation, not just rotation.
            const FSlateRect RenderRect = NodeGeo.GetRenderBoundingRect();

            const FVector2D TopLeft = CanvasGeo.AbsoluteToLocal(FVector2D(RenderRect.Left, RenderRect.Top));
            const FVector2D BottomRight = CanvasGeo.AbsoluteToLocal(FVector2D(RenderRect.Right, RenderRect.Bottom));

            SlotPos = TopLeft;
            SlotSize = BottomRight - TopLeft;
            bFromGeometry = true;
        }
    }

    // Fallback for the first frame, before layout has run.
    if (!bFromGeometry)
    {
        if (const UCanvasPanelSlot* NodeSlot = Cast<UCanvasPanelSlot>(Node->Slot))
        {
            SlotPos = NodeSlot->GetPosition();
            SlotSize = NodeSlot->GetSize();

            // Auto-sized slots report a degenerate size; fall back to the desired size.
            if (SlotSize.IsNearlyZero())
            {
                SlotSize = Node->GetDesiredSize();
            }
        }
    }

    // Normalise the rect. A widget dragged out bottom-to-top or right-to-left in the designer
    // gets a NEGATIVE slot size -- UMG renders it fine, so it looks correct, but every
    // rect test silently rejects it. Flip the origin and take the magnitude instead of
    // making the designer re-drag the node.
    if (SlotSize.X < 0.0)
    {
        SlotPos.X += SlotSize.X;
        SlotSize.X = -SlotSize.X;
    }
    if (SlotSize.Y < 0.0)
    {
        SlotPos.Y += SlotSize.Y;
        SlotSize.Y = -SlotSize.Y;
    }

    // The node's true rectangle, used by free-roam to decide which location the loose cursor
    // is over. Kept separate from MapPos, which is only the cursor anchor.
    Node->RegionPos = SlotPos;
    Node->RegionSize = SlotSize;

    // CursorAnchor is an offset from the slot's top-left. (-1,-1) means "use the center",
    // which is what every icon node wants.
    const bool bUseCenter = Node->CursorAnchor.X < 0.0 && Node->CursorAnchor.Y < 0.0;
    Node->MapPos = bUseCenter
        ? SlotPos + SlotSize * 0.5
        : SlotPos + Node->CursorAnchor;

    // Explicit CursorSize wins; then the slot size (correct for icons, since the box should
    // frame the marker art); then the configured fallback.
    if (!Node->CursorSize.IsNearlyZero())
    {
        Node->ResolvedCursorSize = Node->CursorSize;
    }
    else if (!SlotSize.IsNearlyZero() && Node->HasVisibleMarker())
    {
        Node->ResolvedCursorSize = SlotSize;
    }
    else
    {
        // Region nodes deliberately do NOT inherit their slot size - the box would wrap the
        // whole route instead of pointing at a spot on it.
        Node->ResolvedCursorSize = DefaultCursorSize;
    }
}

UGF_TownMapNodeWidget* UGF_TownMapWidget::GetNodeById(FName LocationId) const
{
    if (LocationId.IsNone())
    {
        return nullptr;
    }

    for (UGF_TownMapNodeWidget* Node : Nodes)
    {
        if (Node && Node->LocationId == LocationId)
        {
            return Node;
        }
    }
    return nullptr;
}

//========================================================================================
// HIGHLIGHT
//========================================================================================

void UGF_TownMapWidget::UpdateHighlight(UGF_TownMapNodeWidget* Node)
{
    OnHighlightChanged(Node);

    if (!HighlightWidgetClass || !MapCanvas)
    {
        return;
    }

    static const TArray<FGF_GEMapHighlightRect> EmptyRects;
    const TArray<FGF_GEMapHighlightRect>& Rects = Node ? Node->HighlightRects : EmptyRects;

    // Grow the pool to fit, then position what we need and hide the remainder. The pool is
    // never shrunk - region counts are small and churn would cost more than the memory.
    while (HighlightPool.Num() < Rects.Num())
    {
        UUserWidget* NewHighlight = CreateWidget<UUserWidget>(this, HighlightWidgetClass);
        if (!NewHighlight)
        {
            break;
        }

        UCanvasPanelSlot* NewSlot = MapCanvas->AddChildToCanvas(NewHighlight);
        if (NewSlot)
        {
            NewSlot->SetAlignment(FVector2D::ZeroVector);
            NewSlot->SetAutoSize(false);
            NewSlot->SetZOrder(HighlightZOrder);
        }
        HighlightPool.Add(NewHighlight);
    }

    for (int32 Index = 0; Index < HighlightPool.Num(); ++Index)
    {
        UUserWidget* Highlight = HighlightPool[Index];
        if (!Highlight)
        {
            continue;
        }

        if (Index < Rects.Num())
        {
            if (UCanvasPanelSlot* PoolSlot = Cast<UCanvasPanelSlot>(Highlight->Slot))
            {
                PoolSlot->SetPosition(Rects[Index].Position);
                PoolSlot->SetSize(Rects[Index].Size);
            }
            Highlight->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            Highlight->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

//========================================================================================
// REVEAL
//========================================================================================

bool UGF_TownMapWidget::IsRevealed(FName LocationId) const
{
    if (bRevealAllLocations)
    {
        return true;
    }

    // An empty id is never "revealed". A node with no id would otherwise be navigable and
    // show a blank name box - the same class of bug as a blank story flag opening a gate.
    return !LocationId.IsNone() && RevealedLocations.Contains(LocationId);
}

void UGF_TownMapWidget::SetRevealedLocations(const TSet<FName>& InRevealed)
{
    RevealedLocations = InRevealed;
    RefreshNodeRevealStates();
}

void UGF_TownMapWidget::MarkLocationRevealed(FName LocationId)
{
    if (LocationId.IsNone())
    {
        return;
    }

    bool bAlreadyPresent = false;
    RevealedLocations.Add(LocationId, &bAlreadyPresent);

    if (!bAlreadyPresent)
    {
        RefreshNodeRevealStates();
    }
}

void UGF_TownMapWidget::RefreshRevealedFromVisitedRoutes()
{
    const UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        return;
    }

    const UGF_RouteSubsystem* RouteSubsystem = GameInstance->GetSubsystem<UGF_RouteSubsystem>();
    if (!RouteSubsystem)
    {
        return;
    }

    int32 RevealedCount = 0;

    for (const UGF_TownMapNodeWidget* Node : Nodes)
    {
        if (!Node || Node->LocationId.IsNone() || RevealedLocations.Contains(Node->LocationId))
        {
            continue;
        }

        // A node counts as discovered if the player has entered ANY area it represents --
        // walking into the Tamer School discovers Slatehaven, not a separate school pin.
        bool bSeen = RouteSubsystem->HasVisitedRoute(Node->LocationId);
        if (!bSeen)
        {
            for (const FName& RouteId : Node->AdditionalRouteIds)
            {
                if (RouteSubsystem->HasVisitedRoute(RouteId))
                {
                    bSeen = true;
                    break;
                }
            }
        }

        if (bSeen)
        {
            RevealedLocations.Add(Node->LocationId);
            ++RevealedCount;
        }
    }

    if (RevealedCount > 0)
    {
        RefreshNodeRevealStates();
    }

    UE_LOG(LogTemp, Warning, TEXT("[TownMap] Discovery: %d node(s) revealed from visited routes (%d revealed total)."),
        RevealedCount, RevealedLocations.Num());
}

void UGF_TownMapWidget::RefreshNodeRevealStates()
{
    for (UGF_TownMapNodeWidget* Node : Nodes)
    {
        if (Node)
        {
            Node->OnRevealStateChanged(IsRevealed(Node->LocationId));
        }
    }
}

//========================================================================================
// PLAYER POSITION
//========================================================================================

void UGF_TownMapWidget::SyncPlayerLocationFromRoute()
{
    const UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        return;
    }

    if (const UGF_RouteSubsystem* RouteSubsystem = GameInstance->GetSubsystem<UGF_RouteSubsystem>())
    {
        SetPlayerLocation(RouteSubsystem->GetCurrentRouteName());
    }
}

void UGF_TownMapWidget::SetPlayerLocation(FName LocationId)
{
    PlayerLocationId = LocationId;

    // Reveal the NODE's id, not the raw route id -- standing in the Tamer School must
    // reveal "SlatehavenCity", since that is what the reveal set and navigation are keyed on.
    if (const UGF_TownMapNodeWidget* OwningNode = GetNodeForRouteId(LocationId))
    {
        MarkLocationRevealed(OwningNode->LocationId);
    }

    if (!PlayerMarkerWidget)
    {
        return;
    }

    UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(PlayerMarkerWidget->Slot);

    // Route id, not location id: interiors have their own UGF_RouteData (own music, own
    // encounters), so the Slatehaven Tamer School reports "SlatehavenTamerSchool" and only
    // resolves to the Slatehaven node via that node's AdditionalRouteIds.
    UGF_TownMapNodeWidget* Node = GetNodeForRouteId(LocationId);

    if (!MarkerSlot || !Node)
    {
        // Genuinely unmapped area (a cave interior with no city): hide rather than strand the
        // marker wherever it was last placed.
        UE_LOG(LogTemp, Warning, TEXT("[TownMap] No map node for route '%s' - player marker hidden. Add it to a node's AdditionalRouteIds."),
            *LocationId.ToString());
        PlayerMarkerWidget->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }

    ResolveNodeGeometry(Node);
    MarkerSlot->SetAnchors(FAnchors(0.0f, 0.0f));   // same canvas-local space as the cursor
    MarkerSlot->SetAlignment(FVector2D(0.5, 0.5));
    MarkerSlot->SetPosition(Node->MapPos);
    PlayerMarkerWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
}

//========================================================================================
// MOUSE
//========================================================================================

void UGF_TownMapWidget::HandleNodeMouseEnter(UGF_TownMapNodeWidget* Node)
{
    if (!Node || !IsRevealed(Node->LocationId))
    {
        return;
    }

    // Only follow the mouse when the mouse is actually the active device, otherwise a
    // stationary cursor sitting over the map would fight every gamepad input.
    if (!UINavPC || !UINavPC->IsUsingMouse())
    {
        return;
    }

    SetCursorToNode(Node);
}
