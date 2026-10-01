// GF_TownMapNodeWidget.cpp

#include "TownMap/GF_TownMapNodeWidget.h"
#include "TownMap/GF_TownMapWidget.h"

FName UGF_TownMapNodeWidget::GetOverrideForDirection(EUINavigation Direction) const
{
    switch (Direction)
    {
        case EUINavigation::Up:    return OverrideUp;
        case EUINavigation::Down:  return OverrideDown;
        case EUINavigation::Left:  return OverrideLeft;
        case EUINavigation::Right: return OverrideRight;
        default:                   return NAME_None;
    }
}

bool UGF_TownMapNodeWidget::HasVisibleMarker() const
{
    return MarkerType == EGF_GEMapMarker::City
        || MarkerType == EGF_GEMapMarker::Town
        || MarkerType == EGF_GEMapMarker::Landmark;
}

bool UGF_TownMapNodeWidget::MatchesRouteId(FName RouteId) const
{
    if (RouteId.IsNone())
    {
        return false;
    }

    return RouteId == LocationId || AdditionalRouteIds.Contains(RouteId);
}

bool UGF_TownMapNodeWidget::ContainsPoint(FVector2D CanvasPoint) const
{
    if (RegionSize.X <= 0.0 || RegionSize.Y <= 0.0)
    {
        return false;
    }

    return CanvasPoint.X >= RegionPos.X && CanvasPoint.X <= RegionPos.X + RegionSize.X
        && CanvasPoint.Y >= RegionPos.Y && CanvasPoint.Y <= RegionPos.Y + RegionSize.Y;
}

void UGF_TownMapNodeWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseEnter(InGeometry, InMouseEvent);

    if (UGF_TownMapWidget* Map = OwnerMap.Get())
    {
        Map->HandleNodeMouseEnter(this);
    }
}

FReply UGF_TownMapNodeWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        if (UGF_TownMapWidget* Map = OwnerMap.Get())
        {
            // Skill first: clicking a node the cursor is not on should select that node,
            // not confirm whatever happened to be under the cursor beforehand. Uses
            // SetCursorToNode rather than the hover path because a click is an explicit
            // mouse action and must land even if UINav has not flipped to mouse input yet.
            Map->SetCursorToNode(this, /*bInstant=*/false);
            Map->ConfirmCurrentLocation();
            return FReply::Handled();
        }
    }

    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
