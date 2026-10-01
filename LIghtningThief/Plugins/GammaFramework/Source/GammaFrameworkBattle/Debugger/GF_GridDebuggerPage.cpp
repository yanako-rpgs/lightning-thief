// GF_GridDebuggerPage.cpp
#include "GF_GridDebuggerPage.h"
#include "GF_DebuggerWidget.h"

#include "GF_GridDebugComponent.h"
#include "GF_GridWorldSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace GEGridPageStyle
{
    static FLinearColor Header  { 1.00f, 0.80f, 0.20f, 1.f };
    static FLinearColor Body    { 0.88f, 0.88f, 0.90f, 1.f };
    static FLinearColor SubText { 0.65f, 0.65f, 0.65f, 1.f };
    static FLinearColor Warn    { 0.95f, 0.45f, 0.35f, 1.f };
    static FLinearColor Panel   { 0.10f, 0.10f, 0.12f, 1.f };
}

// --- Helpers -----------------------------------------------------------------

UWorld* SGF_GridDebuggerPage::GetWorld() const
{
    if (TSharedPtr<SGF_DebuggerWidget> W = SGF_DebuggerWidget::CurrentInstance.Pin())
    {
        if (UWorld* Ctx = W->GetWorldContext())
        {
            return Ctx;
        }
    }

    // The window can outlive the world it was opened against (level change), so
    // fall back to whatever world is live now rather than going dead.
    if (GEngine)
    {
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            if ((Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game) && IsValid(Ctx.World()))
            {
                return Ctx.World();
            }
        }
    }
    return nullptr;
}

UGF_GridDebugComponent* SGF_GridDebuggerPage::GetDebugComp() const
{
    UWorld* World = GetWorld();
    return World ? UGF_GridDebugComponent::GetOrCreateForPlayer(World) : nullptr;
}

static UGF_GridWorldSubsystem* GetGridSys(UWorld* World)
{
    return World ? World->GetSubsystem<UGF_GridWorldSubsystem>() : nullptr;
}

static FString TileTypeToString(EGF_TileType Type)
{
    switch (Type)
    {
    case EGF_TileType::Normal:    return TEXT("Normal");
    case EGF_TileType::Blocked:   return TEXT("Blocked");
    case EGF_TileType::Slope:     return TEXT("Slope");
    case EGF_TileType::Ledge:     return TEXT("Ledge");
    case EGF_TileType::Water:     return TEXT("Water");
    case EGF_TileType::Item:      return TEXT("Item");
    case EGF_TileType::Door:      return TEXT("Door");
    case EGF_TileType::TallGrass: return TEXT("TallGrass");
    case EGF_TileType::Ice:       return TEXT("Ice");
    default:                   return TEXT("Unknown");
    }
}

// --- Construct ---------------------------------------------------------------

void SGF_GridDebuggerPage::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SScrollBox)

        + SScrollBox::Slot()
        .Padding(10.f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
            [
                BuildOverlaySection()
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
            [
                BuildTileSection()
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                BuildAdjacentSection()
            ]
        ]
    ];
}

// --- Overlay section ---------------------------------------------------------

TSharedRef<SWidget> SGF_GridDebuggerPage::BuildOverlaySection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(GEGridPageStyle::Panel)
        .Padding(FMargin(10.f, 8.f))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("OVERLAY")))
                .ColorAndOpacity(GEGridPageStyle::Header)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
            ]

            // Master toggle + range
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    MakeToggle(TEXT("Show grid overlay"), &UGF_GridDebugComponent::bShowDebugGrid)
                ]

                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(24.f, 0.f, 6.f, 0.f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Range (tiles):")))
                    .ColorAndOpacity(GEGridPageStyle::Body)
                ]

                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SBox).WidthOverride(80.f)
                    [
                        SNew(SSpinBox<int32>)
                        .MinValue(1).MaxValue(50)
                        .MinSliderValue(1).MaxSliderValue(50)
                        .Value(this, &SGF_GridDebuggerPage::GetRange)
                        .OnValueChanged(this, &SGF_GridDebuggerPage::OnRangeChanged)
                    ]
                ]
            ]

            // Sub-toggles
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 16.f, 0.f)
                [ MakeToggle(TEXT("Coordinates"), &UGF_GridDebugComponent::bShowTileCoordinates) ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 16.f, 0.f)
                [ MakeToggle(TEXT("Walkable"), &UGF_GridDebugComponent::bShowWalkableTiles) ]

                + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 16.f, 0.f)
                [ MakeToggle(TEXT("Occupied"), &UGF_GridDebugComponent::bShowOccupiedTiles) ]

                + SHorizontalBox::Slot().AutoWidth()
                [ MakeToggle(TEXT("Slopes"), &UGF_GridDebugComponent::bShowSlopeTiles) ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
            [
                SNew(STextBlock)
                .Text(this, &SGF_GridDebuggerPage::GetStatusText)
                .ColorAndOpacity(GEGridPageStyle::SubText)
                .AutoWrapText(true)
            ]
        ];
}

TSharedRef<SWidget> SGF_GridDebuggerPage::MakeToggle(const FString& Label, FToggleMember Member)
{
    return SNew(SCheckBox)
        .IsChecked(TAttribute<ECheckBoxState>::CreateSP(this, &SGF_GridDebuggerPage::GetToggle, Member))
        .OnCheckStateChanged(this, &SGF_GridDebuggerPage::OnToggleChanged, Member)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Label))
            .ColorAndOpacity(GEGridPageStyle::Body)
        ];
}

ECheckBoxState SGF_GridDebuggerPage::GetToggle(FToggleMember Member) const
{
    const UGF_GridDebugComponent* Comp = GetDebugComp();
    if (!Comp)
    {
        return ECheckBoxState::Unchecked;
    }
    return (Comp->*Member) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SGF_GridDebuggerPage::OnToggleChanged(ECheckBoxState NewState, FToggleMember Member)
{
    UGF_GridDebugComponent* Comp = GetDebugComp();
    if (!Comp)
    {
        return;
    }

    const bool bNewValue = (NewState == ECheckBoxState::Checked);
    (Comp->*Member) = bNewValue;

    // Ticking a sub-display with the overlay off looks broken - nothing appears.
    // Treat it as intent to see the overlay, same as the gf.grid command does.
    if (bNewValue && Member != &UGF_GridDebugComponent::bShowDebugGrid)
    {
        Comp->bShowDebugGrid = true;
    }
}

int32 SGF_GridDebuggerPage::GetRange() const
{
    const UGF_GridDebugComponent* Comp = GetDebugComp();
    return Comp ? Comp->DebugRangeX : 8;
}

void SGF_GridDebuggerPage::OnRangeChanged(int32 NewValue)
{
    if (UGF_GridDebugComponent* Comp = GetDebugComp())
    {
        Comp->SetDebugRange(NewValue);
    }
}

// --- Player tile section -----------------------------------------------------

TSharedRef<SWidget> SGF_GridDebuggerPage::BuildTileSection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(GEGridPageStyle::Panel)
        .Padding(FMargin(10.f, 8.f))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("PLAYER TILE")))
                .ColorAndOpacity(GEGridPageStyle::Header)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
            [
                SNew(STextBlock)
                .Text(this, &SGF_GridDebuggerPage::GetPlayerCoordText)
                .ColorAndOpacity(GEGridPageStyle::Body)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(this, &SGF_GridDebuggerPage::GetTilePropsText)
                .ColorAndOpacity(GEGridPageStyle::Body)
                .AutoWrapText(true)
            ]
        ];
}

FText SGF_GridDebuggerPage::GetPlayerCoordText() const
{
    UWorld* World = GetWorld();
    UGF_GridWorldSubsystem* Grid = GetGridSys(World);
    APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;

    if (!Grid || !Pawn)
    {
        return FText::FromString(TEXT("Coord: (no player / no grid subsystem)"));
    }

    const FVector Loc = Pawn->GetActorLocation();
    const FGF_GridCoordinate Coord = Grid->WorldToGrid(Loc);

    return FText::FromString(FString::Printf(
        TEXT("Coord: %d, %d   (grid Z %d)      World: %.0f, %.0f, %.0f"),
        Coord.X, Coord.Y, Coord.Z, Loc.X, Loc.Y, Loc.Z));
}

FText SGF_GridDebuggerPage::GetTilePropsText() const
{
    UWorld* World = GetWorld();
    UGF_GridWorldSubsystem* Grid = GetGridSys(World);
    APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;

    if (!Grid || !Pawn)
    {
        return FText::GetEmpty();
    }

    const FGF_GridCoordinate Coord = Grid->WorldToGrid(Pawn->GetActorLocation());
    const FGF_TileProperties Props = Grid->GetTileProperties(Coord);

    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("Type: %s     Walkable: %s     Occupied: %s     Height level: %d"),
        *TileTypeToString(Props.TileType),
        Props.bIsWalkable ? TEXT("YES") : TEXT("NO"),
        Grid->IsTileOccupied(Coord) ? TEXT("YES") : TEXT("no"),
        Props.HeightLevel));

    if (Props.bIsSlope)
    {
        Lines.Add(FString::Printf(TEXT("Slope: yes (up direction %d)"), (int32)Props.SlopeDirection));
    }
    if (Props.bIsLedge)
    {
        Lines.Add(FString::Printf(TEXT("Ledge: yes (jump direction %d)"), (int32)Props.LedgeJumpDirection));
    }
    if (Props.bHasItem)
    {
        Lines.Add(FString::Printf(TEXT("Item: %s%s"),
            *Props.ItemID.ToString(),
            Props.bItemPickedUp ? TEXT(" (already picked up)") : TEXT("")));
    }
    if (Props.bHasDoor)
    {
        Lines.Add(FString::Printf(TEXT("Door: yes%s"),
            Props.bIsDungeonEntrance ? TEXT(" (dungeon entrance)") : TEXT("")));
    }

    return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

// --- Adjacent tiles ----------------------------------------------------------

TSharedRef<SWidget> SGF_GridDebuggerPage::BuildAdjacentSection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(GEGridPageStyle::Panel)
        .Padding(FMargin(10.f, 8.f))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("ADJACENT TILES")))
                .ColorAndOpacity(GEGridPageStyle::Header)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(this, &SGF_GridDebuggerPage::GetAdjacentText)
                .ColorAndOpacity(GEGridPageStyle::Body)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT(
                    "Screen directions on the fixed camera: Up = X+, Down = X-, Right = Y+, Left = Y-.")))
                .ColorAndOpacity(GEGridPageStyle::SubText)
                .AutoWrapText(true)
            ]
        ];
}

FText SGF_GridDebuggerPage::GetAdjacentText() const
{
    UWorld* World = GetWorld();
    UGF_GridWorldSubsystem* Grid = GetGridSys(World);
    APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;

    if (!Grid || !Pawn)
    {
        return FText::FromString(TEXT("(no player / no grid subsystem)"));
    }

    const FGF_GridCoordinate C = Grid->WorldToGrid(Pawn->GetActorLocation());

    struct FGF_Dir { const TCHAR* Label; int32 DX; int32 DY; };
    static const FGF_Dir Dirs[] = {
        { TEXT("Up    (X+)"),  1,  0 },
        { TEXT("Down  (X-)"), -1,  0 },
        { TEXT("Right (Y+)"),  0,  1 },
        { TEXT("Left  (Y-)"),  0, -1 },
    };

    TArray<FString> Lines;
    for (const FGF_Dir& D : Dirs)
    {
        const FGF_GridCoordinate N(C.X + D.DX, C.Y + D.DY, C.Z);
        const FGF_TileProperties P = Grid->GetTileProperties(N);

        Lines.Add(FString::Printf(TEXT("%s  %5d,%-6d  %-10s walkable:%-4s occupied:%s"),
            D.Label, N.X, N.Y,
            *TileTypeToString(P.TileType),
            Grid->IsTileWalkable(N) ? TEXT("YES") : TEXT("NO"),
            Grid->IsTileOccupied(N) ? TEXT("YES") : TEXT("no")));
    }

    return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

// --- Status ------------------------------------------------------------------

FText SGF_GridDebuggerPage::GetStatusText() const
{
    if (!GetWorld())
    {
        return FText::FromString(TEXT("No live world - start play before using this page."));
    }
    if (!GetDebugComp())
    {
        return FText::FromString(TEXT("No player pawn yet - the overlay attaches once the player spawns."));
    }
    return FText::FromString(TEXT(
        "Same controls as the gf.grid console command. Overlay is drawn at the player's grid Z only, "
        "so tiles on other height levels are not shown. Raise Range if you need a wider view - it costs "
        "framerate, since every tile is a set of debug lines."));
}
