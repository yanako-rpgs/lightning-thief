// GF_GridDebuggerPage.h
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UGF_GridDebugComponent;

/**
 * Grid / Collision debug page.
 *
 * +-------------------------------------------------------------+
 * |  OVERLAY                                                    |
 * |  [x] Show grid overlay      Range: [ 8 ]                    |
 * |  [ ] Coordinates  [x] Walkable  [x] Occupied  [ ] Slopes    |
 * +-------------------------------------------------------------+
 * |  PLAYER TILE                                                |
 * |  Coord: 124, 2384 (Z 0)                                     |
 * |  Type / walkable / height / slope / ledge / item / door     |
 * +-------------------------------------------------------------+
 * |  ADJACENT TILES                                             |
 * |  Up / Down / Left / Right walkability                       |
 * +-------------------------------------------------------------+
 *
 * Everything here reads live off UGF_GridDebugComponent on the player pawn,
 * so it stays in sync with the gf.grid console command - both go through
 * UGF_GridDebugComponent::GetOrCreateForPlayer().
 */
class GAMMAFRAMEWORKBATTLE_API SGF_GridDebuggerPage : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGF_GridDebuggerPage) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // --- Section builders ----------------------------------------------------
    TSharedRef<SWidget> BuildOverlaySection();
    TSharedRef<SWidget> BuildTileSection();
    TSharedRef<SWidget> BuildAdjacentSection();

    // --- Toggle plumbing -----------------------------------------------------
    // Bound by member pointer so one pair of handlers drives every checkbox.
    using FToggleMember = bool UGF_GridDebugComponent::*;

    TSharedRef<SWidget> MakeToggle(const FString& Label, FToggleMember Member);
    ECheckBoxState      GetToggle(FToggleMember Member) const;
    void                OnToggleChanged(ECheckBoxState NewState, FToggleMember Member);

    // --- Range ---------------------------------------------------------------
    int32 GetRange() const;
    void  OnRangeChanged(int32 NewValue);

    // --- Live readouts (bound as attributes, so no refresh button needed) -----
    FText GetPlayerCoordText() const;
    FText GetTilePropsText() const;
    FText GetAdjacentText() const;
    FText GetStatusText() const;

    // --- Helpers -------------------------------------------------------------
    UWorld*              GetWorld() const;
    UGF_GridDebugComponent* GetDebugComp() const;
};
