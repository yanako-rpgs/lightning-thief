// GF_BattleDebuggerPage.cpp
#include "GF_BattleDebuggerPage.h"
#include "GF_ElementTypes.h"
#include "GF_DebuggerWidget.h"
#include "GF_DebugMenuSubsystem.h"
#include "Kismet/GameplayStatics.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "EngineUtils.h"

// ─── Colour palette (shared inside this TU) ──────────────────────────────────
namespace BattlePage
{
    static const FLinearColor PanelBg  { 0.10f, 0.10f, 0.12f, 1.f };
    static const FLinearColor RowEven  { 0.13f, 0.13f, 0.15f, 1.f };
    static const FLinearColor RowOdd   { 0.11f, 0.11f, 0.13f, 1.f };
    static const FLinearColor Selected { 0.18f, 0.40f, 0.75f, 1.f };
    static const FLinearColor Green    { 0.15f, 0.70f, 0.25f, 1.f };
    static const FLinearColor Red      { 0.80f, 0.15f, 0.15f, 1.f };
    static const FLinearColor Gold     { 1.00f, 0.80f, 0.20f, 1.f };
    static const FLinearColor White    { 1.00f, 1.00f, 1.00f, 1.f };
    static const FLinearColor SubText  { 0.60f, 0.60f, 0.60f, 1.f };
    static const FLinearColor SectionHeader { 0.20f, 0.20f, 0.24f, 1.f };
}

// ─── Type colours ─────────────────────────────────────────────────────────────
FLinearColor SGF_BattleDebuggerPage::TypeToColor(EGF_Element T)
{
    return UGF_ElementLibrary::GetElementColor(T);
}

FString SGF_BattleDebuggerPage::TypeToString(EGF_Element T)
{
    const UEnum* Enum = StaticEnum<EGF_Element>();
    return Enum ? Enum->GetDisplayNameTextByValue((int64)T).ToString() : TEXT("???");
}

// ─── World helper ─────────────────────────────────────────────────────────────
UWorld* SGF_BattleDebuggerPage::GetWorld() const
{
    if (TSharedPtr<SGF_DebuggerWidget> Widget = SGF_DebuggerWidget::CurrentInstance.Pin())
        return Widget->GetWorldContext();
    if (GEngine)
    {
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            if ((Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game) && IsValid(Ctx.World()))
                return Ctx.World();
    }
    return nullptr;
}

// ─── Construct ───────────────────────────────────────────────────────────────
void SGF_BattleDebuggerPage::Construct(const FArguments& InArgs)
{
    // Populate difficulty options matching EGF_TamerAIDifficulty
    DifficultyOptions.Add(MakeShared<FString>(TEXT("Random")));
    DifficultyOptions.Add(MakeShared<FString>(TEXT("Basic")));
    DifficultyOptions.Add(MakeShared<FString>(TEXT("Smart")));
    DifficultyOptions.Add(MakeShared<FString>(TEXT("Expert")));

    LoadCreatureAssets();
    LoadMapAssets();
    LoadSoundAssets();
    LoadSkillAssets();
    LoadTamerClasses();

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [ BuildWildBattleSection() ]

            + SVerticalBox::Slot().AutoHeight()
            [ BuildTamerBattleSection() ]
        ]
    ];
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WILD BATTLE SECTION
// ═══════════════════════════════════════════════════════════════════════════════

TSharedRef<SWidget> SGF_BattleDebuggerPage::BuildWildBattleSection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(BattlePage::PanelBg)
        .Padding(8.f)
        [
            SNew(SVerticalBox)

            // ── Section header ────────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("WILD BATTLE")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                .ColorAndOpacity(BattlePage::Gold)
            ]

            // ── Type filter bar ───────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
            [ BuildTypeFilterBar() ]

            // ── Creature list + options ────────────────────────────────────────
            + SVerticalBox::Slot().FillHeight(1.f).MaxHeight(280.f)
            [
                SNew(SHorizontalBox)

                // List
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.09f, 1.f))
                    [
                        SAssignNew(CreatureListView, STileView<TSharedPtr<FGF_BattleCreatureEntry>>)
                        .ListItemsSource(&FilteredCreature)
                        .OnGenerateTile(this, &SGF_BattleDebuggerPage::MakeCreatureTile)
                        .OnSelectionChanged(this, &SGF_BattleDebuggerPage::OnCreatureSelected)
                        .SelectionMode(ESelectionMode::Single)
                        .ItemWidth(80.f)
                        .ItemHeight(90.f)
                    ]
                ]

                // Side panel
                + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
                [
                    SNew(SVerticalBox)
                    .Clipping(EWidgetClipping::ClipToBounds)

                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                    [
                        SNew(STextBlock)
                        .Text(TAttribute<FText>::CreateSP(this, &SGF_BattleDebuggerPage::GetWildStatusText))
                        .ColorAndOpacity(BattlePage::White)
                        .WrapTextAt(200.f)
                    ]

                    // Level
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("Level:")))
                            .ColorAndOpacity(BattlePage::SubText)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SAssignNew(LevelSpinBox, SSpinBox<int32>)
                            .MinValue(1).MaxValue(100)
                            .Value(WildLevel)
                            .MinDesiredWidth(60.f)
                            .OnValueChanged_Lambda([this](int32 V){ WildLevel = V; })
                        ]
                    ]

                    // Map selector
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("Map:")))
                            .ColorAndOpacity(BattlePage::SubText)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SAssignNew(MapComboBox, STextComboBox)
                            .OptionsSource(&MapOptions)
                            .InitiallySelectedItem(MapOptions.Num() > 0 ? MapOptions[0] : nullptr)
                            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                            {
                                SelectedMapOption = Item;
                                // Find matching path index
                                for (int32 i = 0; i < MapOptions.Num(); ++i)
                                {
                                    if (MapOptions[i] == Item && MapPaths.IsValidIndex(i))
                                    {
                                        // MapPaths[i] is the full package path for StartWildBattle
                                        break;
                                    }
                                }
                            })
                        ]
                    ]

                    // Intro sound selector
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("Intro:")))
                            .ColorAndOpacity(BattlePage::SubText)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SAssignNew(IntroSoundComboBox, STextComboBox)
                            .OptionsSource(&SoundOptions)
                            .InitiallySelectedItem(SelectedIntroSound)
                            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                            {
                                SelectedIntroSound = Item;
                            })
                        ]
                    ]

                    // Loop sound selector
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("Loop:")))
                            .ColorAndOpacity(BattlePage::SubText)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SAssignNew(LoopSoundComboBox, STextComboBox)
                            .OptionsSource(&SoundOptions)
                            .InitiallySelectedItem(SelectedLoopSound)
                            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                            {
                                SelectedLoopSound = Item;
                            })
                        ]
                    ]

                    + SVerticalBox::Slot().FillHeight(1.f) // spacer

                    // Skill override label
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Skill Override  (empty = defaults)")))
                        .ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.f, 1.f)))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
                    [ BuildMoveOverrideRow(0) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
                    [ BuildMoveOverrideRow(1) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
                    [ BuildMoveOverrideRow(2) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                    [ BuildMoveOverrideRow(3) ]

                    // Start button
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("▶  Start Wild Battle")))
                        .ButtonColorAndOpacity(BattlePage::Green)
                        .ForegroundColor(BattlePage::White)
                        .OnClicked(this, &SGF_BattleDebuggerPage::OnStartWildBattle)
                    ]
                ]
            ]
        ];
}

// ── Type filter bar ───────────────────────────────────────────────────────────
TSharedRef<SWidget> SGF_BattleDebuggerPage::BuildTypeFilterBar()
{
    TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);

    // "ALL" button
    Bar->AddSlot().AutoWidth().Padding(2.f, 0.f)
    [
        SNew(SButton)
        .Text(FText::FromString(TEXT("ALL")))
        .ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]()
        {
            return FSlateColor(bShowAll ? FLinearColor(0.3f,0.6f,1.f,1.f) : FLinearColor(0.2f,0.2f,0.2f,1.f));
        }))
        .ForegroundColor(BattlePage::White)
        .OnClicked_Lambda([this]() -> FReply
        {
            bShowAll = true;
            ActiveTypeFilter = EGF_Element::None;
            ApplyTypeFilter();
            return FReply::Handled();
        })
    ];

    // One button per type (skip None)
    const UEnum* TypeEnum = StaticEnum<EGF_Element>();
    for (int32 i = 1; i < TypeEnum->NumEnums() - 1; ++i)
    {
        EGF_Element Type = (EGF_Element)TypeEnum->GetValueByIndex(i);
        Bar->AddSlot().AutoWidth().Padding(2.f, 0.f)
        [
            SNew(SButton)
            .Text(FText::FromString(TypeToString(Type)))
            .ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &SGF_BattleDebuggerPage::GetTypeButtonColor, Type))
            .ForegroundColor(BattlePage::White)
            .OnClicked_Lambda([this, Type]() -> FReply
            {
                OnTypeFilterClicked(Type);
                return FReply::Handled();
            })
        ];
    }

    return SNew(SScrollBox)
        .Orientation(Orient_Horizontal)
        + SScrollBox::Slot()[ Bar ];
}

FSlateColor SGF_BattleDebuggerPage::GetTypeButtonColor(EGF_Element Type) const
{
    if (!bShowAll && ActiveTypeFilter == Type)
        return FSlateColor(TypeToColor(Type));
    return FSlateColor(TypeToColor(Type) * 0.45f);
}

void SGF_BattleDebuggerPage::OnTypeFilterClicked(EGF_Element Type)
{
    bShowAll = false;
    ActiveTypeFilter = Type;
    ApplyTypeFilter();
}

// ── Creature tile (grid cell) ──────────────────────────────────────────────────
TSharedRef<ITableRow> SGF_BattleDebuggerPage::MakeCreatureTile(
    TSharedPtr<FGF_BattleCreatureEntry> Item,
    const TSharedRef<STableViewBase>& Owner)
{
    const FLinearColor T1Color = TypeToColor(Item->PrimaryElement);

    return SNew(STableRow<TSharedPtr<FGF_BattleCreatureEntry>>, Owner)
    .Padding(2.f)
    [
        SNew(SVerticalBox)

        // Icon
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
        [
            SNew(SBox).WidthOverride(64.f).HeightOverride(64.f)
            [
                Item->IconBrush.IsValid()
                    ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Item->IconBrush.Get()))
                    : StaticCastSharedRef<SWidget>(SNew(SBox))
            ]
        ]

        // Name (short — just species name, no dex number)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromName(Item->SpeciesData.IsValid()
                ? Item->SpeciesData->SpeciesName : NAME_None))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
            .ColorAndOpacity(BattlePage::White)
            .Justification(ETextJustify::Center)
        ]

        // Primary type emblem
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 1.f)
        [
            SNew(SBorder)
            .BorderBackgroundColor(T1Color)
            .Padding(FMargin(3.f, 0.f))
            [
                SNew(STextBlock)
                .Text(FText::FromString(TypeToString(Item->PrimaryElement)))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 6))
                .ColorAndOpacity(BattlePage::White)
            ]
        ]
    ];
}

void SGF_BattleDebuggerPage::OnCreatureSelected(TSharedPtr<FGF_BattleCreatureEntry> Item, ESelectInfo::Type)
{
    SelectedCreature    = Item;
    WildStatusMessage  = Item.IsValid()
        ? FString::Printf(TEXT("%s selected!"), *Item->DisplayName)
        : TEXT("");
}

FText SGF_BattleDebuggerPage::GetWildStatusText() const
{
    return FText::FromString(WildStatusMessage.IsEmpty()
        ? TEXT("Select a Creature from the list.")
        : WildStatusMessage);
}

// ── Start Wild Battle ─────────────────────────────────────────────────────────
FReply SGF_BattleDebuggerPage::OnStartWildBattle()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        WildStatusMessage = TEXT("ERROR: No world context.");
        return FReply::Handled();
    }
    if (!SelectedCreature.IsValid() || !SelectedCreature->SpeciesData.IsValid())
    {
        WildStatusMessage = TEXT("Select a Creature first!");
        return FReply::Handled();
    }

    UGF_CreatureSpeciesData* Species = SelectedCreature->SpeciesData.Get();

    // ── Find BP_BattleManager ─────────────────────────────────────────────────
    AActor* BattleManager = nullptr;
    TArray<AActor*> Actors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), Actors);
    for (AActor* A : Actors)
    {
        if (A && A->GetClass()->GetName().Contains(TEXT("BP_BattleManager")))
        {
            BattleManager = A;
            break;
        }
    }

    if (!BattleManager)
    {
        WildStatusMessage = TEXT("ERROR: BP_BattleManager not in level.");
        return FReply::Handled();
    }

    // Set the map directly on BP_BattleManager as a Soft World Reference variable.
    // We can't pass TSoftObjectPtr through ProcessEvent safely, but we CAN write
    // it to a BP variable via FindFProperty before firing the event.
    FString MapPath;
    for (int32 i = 0; i < MapOptions.Num(); ++i)
    {
        if (MapOptions[i] == SelectedMapOption && MapPaths.IsValidIndex(i))
        {
            MapPath = MapPaths[i];
            break;
        }
    }

    if (!MapPath.IsEmpty())
    {
        if (FProperty* Prop = BattleManager->GetClass()->FindPropertyByName(TEXT("DebugBattleMap")))
        {
            // ImportText_InContainer sets any property from its string representation —
            // the correct way to write a soft object ref from a path string.
            Prop->ImportText_InContainer(*MapPath, BattleManager, BattleManager, PPF_None);
        }
    }

    // Write move overrides to BP variables (DebugWildSkill1..4)
    // BP reads these in DebugStartWildBattle and applies them to the wild Creature
    const TCHAR* SkillVarNames[NUM_WILD_MOVE_SLOTS] = {
        TEXT("DebugWildSkill1"), TEXT("DebugWildSkill2"),
        TEXT("DebugWildSkill3"), TEXT("DebugWildSkill4")
    };
    for (int32 i = 0; i < NUM_WILD_MOVE_SLOTS; ++i)
    {
        FString MovePath;
        if (SelectedWildSkills[i].IsValid())
        {
            const FString& Sel = *SelectedWildSkills[i];
            if (!Sel.IsEmpty() && Sel != TEXT("(none)"))
            {
                for (int32 j = 0; j < SkillOptions.Num(); ++j)
                {
                    if (SkillOptions[j].IsValid() && *SkillOptions[j] == Sel && MovePaths.IsValidIndex(j))
                    {
                        MovePath = MovePaths[j];
                        break;
                    }
                }
            }
        }
        if (FProperty* Prop = BattleManager->GetClass()->FindPropertyByName(SkillVarNames[i]))
            Prop->ImportText_InContainer(*MovePath, BattleManager, BattleManager, PPF_None);
    }

    // Fire the event — BP reads DebugBattleMap and passes it to StartWildBattle
    if (UFunction* Func = BattleManager->FindFunction(TEXT("DebugStartWildBattle")))
    {
        struct FGF_DebugParams
        {
            UGF_CreatureSpeciesData* WildSpecies = nullptr;
            int32                WildLevel   = 5;
        };
        FGF_DebugParams P;
        P.WildSpecies = Species;
        P.WildLevel   = WildLevel;
        BattleManager->ProcessEvent(Func, &P);
    }
    else
    {
        WildStatusMessage = TEXT("ERROR: Add DebugStartWildBattle event to BP_BattleManager.");
        return FReply::Handled();
    }

    WildStatusMessage = FString::Printf(TEXT("Wild %s Lv%d"),
        *Species->SpeciesName.ToString(), WildLevel);

    return FReply::Handled();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TAMER BATTLE SECTION
// ═══════════════════════════════════════════════════════════════════════════════

TSharedRef<SWidget> SGF_BattleDebuggerPage::BuildTamerBattleSection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(BattlePage::PanelBg)
        .Padding(8.f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("TAMER BATTLE")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                    .ColorAndOpacity(BattlePage::Gold)
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("⟳ Refresh Tamers")))
                    .OnClicked(this, &SGF_BattleDebuggerPage::OnRefreshTamers)
                ]
            ]

            + SVerticalBox::Slot().FillHeight(1.f).MaxHeight(220.f)
            [
                SNew(SHorizontalBox)

                // Tamer list
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.09f, 1.f))
                    [
                        SAssignNew(TamerListView, SListView<TSharedPtr<FGF_TamerEntry>>)
                        .ListItemsSource(&TamerEntries)
                        .OnGenerateRow(this, &SGF_BattleDebuggerPage::MakeTamerRow)
                        .OnSelectionChanged(this, &SGF_BattleDebuggerPage::OnTamerSelected)
                        .SelectionMode(ESelectionMode::Single)
                    ]
                ]

                // Side options
                + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                    [
                        SNew(STextBlock)
                        .Text(TAttribute<FText>::CreateSP(this, &SGF_BattleDebuggerPage::GetTamerStatusText))
                        .ColorAndOpacity(BattlePage::White)
                        .WrapTextAt(200.f)
                    ]

                    // Difficulty override
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("AI Difficulty:")))
                            .ColorAndOpacity(BattlePage::SubText)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SAssignNew(DifficultyComboBox, STextComboBox)
                            .OptionsSource(&DifficultyOptions)
                            .InitiallySelectedItem(DifficultyOptions.Num() > 2 ? DifficultyOptions[2] : nullptr)
                            .OnSelectionChanged(this, &SGF_BattleDebuggerPage::OnDifficultyChanged)
                        ]
                    ]

                    + SVerticalBox::Slot().FillHeight(1.f)

                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("▶  Start Tamer Battle")))
                        .ButtonColorAndOpacity(FLinearColor(0.7f, 0.3f, 0.1f, 1.f))
                        .ForegroundColor(BattlePage::White)
                        .OnClicked(this, &SGF_BattleDebuggerPage::OnStartTamerBattle)
                    ]
                ]
            ]
        ];
}

TSharedRef<ITableRow> SGF_BattleDebuggerPage::MakeTamerRow(
    TSharedPtr<FGF_TamerEntry> Item,
    const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<TSharedPtr<FGF_TamerEntry>>, Owner)
    [
        SNew(STextBlock)
        .Text(FText::FromString(Item->DisplayName))
        .ColorAndOpacity(BattlePage::White)
        .Margin(FMargin(4.f, 2.f))
    ];
}

void SGF_BattleDebuggerPage::OnTamerSelected(TSharedPtr<FGF_TamerEntry> Item, ESelectInfo::Type)
{
    SelectedTamer        = Item;
    TamerStatusMessage   = Item.IsValid()
        ? FString::Printf(TEXT("%s selected!"), *Item->DisplayName)
        : TEXT("");
}

FText SGF_BattleDebuggerPage::GetTamerStatusText() const
{
    return FText::FromString(TamerStatusMessage.IsEmpty()
        ? TEXT("Select a tamer from the list.")
        : TamerStatusMessage);
}

void SGF_BattleDebuggerPage::OnDifficultyChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type)
{
    if (!NewSel.IsValid()) return;
    if (*NewSel == TEXT("Random"))       SelectedDifficulty = EGF_TamerAIDifficulty::Random;
    else if (*NewSel == TEXT("Basic"))   SelectedDifficulty = EGF_TamerAIDifficulty::Basic;
    else if (*NewSel == TEXT("Smart"))   SelectedDifficulty = EGF_TamerAIDifficulty::Smart;
    else if (*NewSel == TEXT("Expert"))  SelectedDifficulty = EGF_TamerAIDifficulty::Expert;
}

FReply SGF_BattleDebuggerPage::OnStartTamerBattle()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        TamerStatusMessage = TEXT("ERROR: No world context.");
        return FReply::Handled();
    }
    if (!SelectedTamer.IsValid() || !SelectedTamer->TamerClass)
    {
        TamerStatusMessage = TEXT("Select a tamer first!");
        return FReply::Handled();
    }

    // Find BP_BattleManager
    AActor* BattleManager = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetClass()->GetName().Contains(TEXT("BP_BattleManager")))
        {
            BattleManager = *It;
            break;
        }
    }
    if (!BattleManager)
    {
        TamerStatusMessage = TEXT("ERROR: BP_BattleManager not in level.");
        return FReply::Handled();
    }

    // Stage data on the subsystem — BP reads it, no ProcessEvent struct needed
    UGameInstance* GI = World->GetGameInstance();
    if (UGF_DebugMenuSubsystem* DS = GI ? GI->GetSubsystem<UGF_DebugMenuSubsystem>() : nullptr)
    {
        DS->PendingTamerClass      = SelectedTamer->TamerClass;
        DS->PendingTamerDifficulty = SelectedDifficulty;
    }
    else
    {
        // Fallback: no subsystem available, can't stage safely
        TamerStatusMessage = TEXT("ERROR: DebugMenuSubsystem unavailable.");
        return FReply::Handled();
    }

    // Push selected map directly to BP_BattleManager (same pattern as wild battle's DebugBattleMap)
    {
        FString MapPath;
        for (int32 i = 0; i < MapOptions.Num(); ++i)
        {
            if (MapOptions[i] == SelectedMapOption && MapPaths.IsValidIndex(i))
            {
                MapPath = MapPaths[i];
                break;
            }
        }
        if (!MapPath.IsEmpty())
        {
            if (FProperty* Prop = BattleManager->GetClass()->FindPropertyByName(TEXT("DebugTamerMap")))
                Prop->ImportText_InContainer(*MapPath, BattleManager, BattleManager, PPF_None);
        }

        // Push selected sounds so StartOST doesn't receive null assets
        auto WriteSound = [&](const TSharedPtr<FString>& SelectedSound, const TCHAR* VarName)
        {
            FString SoundPath;
            for (int32 i = 0; i < SoundOptions.Num(); ++i)
            {
                if (SoundOptions[i] == SelectedSound && SoundPaths.IsValidIndex(i))
                {
                    SoundPath = SoundPaths[i];
                    break;
                }
            }
            if (FProperty* Prop = BattleManager->GetClass()->FindPropertyByName(VarName))
                Prop->ImportText_InContainer(*SoundPath, BattleManager, BattleManager, PPF_None);
        };
        WriteSound(SelectedIntroSound, TEXT("DebugTamerIntroSound"));
        WriteSound(SelectedLoopSound,  TEXT("DebugTamerLoopSound"));
    }

    // Call a ZERO-parameter BP event — no struct layout risk
    if (UFunction* Func = BattleManager->FindFunction(TEXT("Debug_StartTamerBattle")))
    {
        BattleManager->ProcessEvent(Func, nullptr);
        TamerStatusMessage = FString::Printf(TEXT("Started battle vs %s!"), *SelectedTamer->DisplayName);
    }
    else
    {
        TamerStatusMessage = TEXT("Add 'Debug_StartTamerBattle' Custom Event to BP_BattleManager.\n"
                                   "Read TamerClass + Difficulty from DebugMenuSubsystem.");
    }

    return FReply::Handled();
}

FReply SGF_BattleDebuggerPage::OnRefreshTamers()
{
    LoadTamerClasses();
    if (TamerListView.IsValid())
        TamerListView->RequestListRefresh();
    return FReply::Handled();
}

TSharedRef<SWidget> SGF_BattleDebuggerPage::MakeDifficultyWidget(TSharedPtr<FString> Item)
{
    return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DATA LOADING
// ═══════════════════════════════════════════════════════════════════════════════

void SGF_BattleDebuggerPage::LoadCreatureAssets()
{
    AllCreature.Empty();

    FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    FARFilter Filter;
    Filter.ClassPaths.Add(UGF_CreatureSpeciesData::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths   = true;
    Filter.PackagePaths.Add(TEXT("/Game"));

    TArray<FAssetData> Assets;
    AR.Get().GetAssets(Filter, Assets);

    for (const FAssetData& Asset : Assets)
    {
        UGF_CreatureSpeciesData* Species = Cast<UGF_CreatureSpeciesData>(Asset.GetAsset());
        if (!Species) continue;

        auto Entry          = MakeShared<FGF_BattleCreatureEntry>();
        Entry->SpeciesData  = Species;
        Entry->PrimaryElement  = Species->PrimaryElement;
        Entry->SecondaryElement= Species->SecondaryElement;
        Entry->DisplayName  = FString::Printf(TEXT("%03d  %s"),
            Species->CompendiumNumber, *Species->SpeciesName.ToString());

        if (IsValid(Species->DisplayIcon1))
        {
            // Pass the UPaperSprite directly — Slate renders it with correct UV/frame automatically
            Entry->IconBrush = MakeShared<FSlateImageBrush>(
                Species->DisplayIcon1, FVector2D(64.f, 64.f));
        }

        AllCreature.Add(Entry);
    }

    // Sort by compendium number
    AllCreature.Sort([](const TSharedPtr<FGF_BattleCreatureEntry>& A, const TSharedPtr<FGF_BattleCreatureEntry>& B)
    {
        return A->SpeciesData.IsValid() && B->SpeciesData.IsValid()
            && A->SpeciesData->CompendiumNumber < B->SpeciesData->CompendiumNumber;
    });

    ApplyTypeFilter();
}

void SGF_BattleDebuggerPage::ApplyTypeFilter()
{
    FilteredCreature.Empty();
    for (const auto& Entry : AllCreature)
    {
        if (bShowAll
         || Entry->PrimaryElement == ActiveTypeFilter
         || Entry->SecondaryElement == ActiveTypeFilter)
        {
            FilteredCreature.Add(Entry);
        }
    }
    if (CreatureListView.IsValid())
        CreatureListView->RequestListRefresh();
}

void SGF_BattleDebuggerPage::LoadMapAssets()
{
    MapOptions.Empty();
    MapPaths.Empty();

    FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    FARFilter Filter;
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("World")));
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(TEXT("/Game"));

    TArray<FAssetData> Assets;
    AR.Get().GetAssets(Filter, Assets);

    // Only show maps that have "Battle" in their name
    for (const FAssetData& Asset : Assets)
    {
        FString PackageName = Asset.PackageName.ToString();
        FString ShortName   = FPaths::GetBaseFilename(PackageName);

        if (!ShortName.Contains(TEXT("Battle"), ESearchCase::IgnoreCase)) continue;

        MapOptions.Add(MakeShared<FString>(ShortName));
        MapPaths.Add(PackageName);
    }

    if (MapOptions.Num() == 0)
    {
        MapOptions.Add(MakeShared<FString>(TEXT("(No maps found)")));
        MapPaths.Add(TEXT(""));
    }

    SelectedMapOption = MapOptions[0];
}

void SGF_BattleDebuggerPage::LoadSoundAssets()
{
    SoundOptions.Empty();
    SoundPaths.Empty();

    // First option is always "(none)"
    SoundOptions.Add(MakeShared<FString>(TEXT("(none)")));
    SoundPaths.Add(TEXT(""));

    FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    FARFilter Filter;
    // SoundWave and SoundCue both derive from SoundBase
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundWave")));
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundCue")));
    Filter.bRecursivePaths   = true;
    Filter.bRecursiveClasses = false;
    Filter.PackagePaths.Add(TEXT("/Game"));

    TArray<FAssetData> Assets;
    AR.Get().GetAssets(Filter, Assets);

    TArray<TPair<FString, FString>> NamePathPairs;
    for (const FAssetData& Asset : Assets)
    {
        FString Name = Asset.AssetName.ToString();
        // Only include assets whose name starts with "OST_"
        if (!Name.StartsWith(TEXT("OST_"), ESearchCase::IgnoreCase)) continue;

        NamePathPairs.Add({ Name, Asset.GetObjectPathString() });
    }

    NamePathPairs.Sort([](const TPair<FString,FString>& A, const TPair<FString,FString>& B)
    {
        return A.Key < B.Key;
    });

    for (const auto& Pair : NamePathPairs)
    {
        SoundOptions.Add(MakeShared<FString>(Pair.Key));
        SoundPaths.Add(Pair.Value);
    }

    SelectedIntroSound = SoundOptions[0];
    SelectedLoopSound  = SoundOptions[0];

    // Default to the wild battle OST tracks if present
    for (int32 i = 0; i < SoundPaths.Num(); ++i)
    {
        if (SoundPaths[i].Contains(TEXT("OST_WildCreatureBattleIntro")))
            SelectedIntroSound = SoundOptions[i];
        if (SoundPaths[i].Contains(TEXT("OST_WildCreatureBattleLOOP")))
            SelectedLoopSound = SoundOptions[i];
    }
}

// ── Skill override UI ──────────────────────────────────────────────────────────
TSharedRef<SWidget> SGF_BattleDebuggerPage::BuildMoveOverrideRow(int32 SlotIndex)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,4.f,0.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(TEXT("Skill %d:"), SlotIndex + 1)))
            .ColorAndOpacity(BattlePage::SubText)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SAssignNew(WildSkillComboBoxes[SlotIndex], STextComboBox)
            .OptionsSource(&SkillOptions)
            .InitiallySelectedItem(SkillOptions.Num() > 0 ? SkillOptions[0] : nullptr)
            .OnSelectionChanged_Lambda([this, SlotIndex](TSharedPtr<FString> Sel, ESelectInfo::Type Info)
            {
                OnWildSkillChanged(Sel, Info, SlotIndex);
            })
        ];
}

void SGF_BattleDebuggerPage::OnWildSkillChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type, int32 Slot)
{
    if (Slot >= 0 && Slot < NUM_WILD_MOVE_SLOTS)
        SelectedWildSkills[Slot] = NewSel;
}

void SGF_BattleDebuggerPage::LoadSkillAssets()
{
    SkillOptions.Empty();
    MovePaths.Empty();

    SkillOptions.Add(MakeShared<FString>(TEXT("(none)")));
    MovePaths.Add(TEXT(""));

    FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    FARFilter Filter;
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(TEXT("/Game"));

    TArray<FAssetData> Assets;
    AR.Get().GetAssets(Filter, Assets);

    TMap<FString, FString> SortedSkills; // display name -> class path

    for (const FAssetData& Asset : Assets)
    {
        FString AssetName = Asset.AssetName.ToString();
        if (AssetName.StartsWith(TEXT("SKEL_"))) continue;

        FString GeneratedClass = Asset.PackageName.ToString() + TEXT(".") + AssetName + TEXT("_C");

        // Only include blueprints under the Skills folder
        if (!Asset.PackageName.ToString().Contains(TEXT("/Traits/Skills/")) &&
            !Asset.PackageName.ToString().Contains(TEXT("/ABILITIES/Skills/"))) continue;

        FString DisplayName = AssetName;
        DisplayName.RemoveFromStart(TEXT("BP_"));
        SortedSkills.Add(DisplayName, GeneratedClass);
    }

    SortedSkills.KeySort([](const FString& A, const FString& B){ return A < B; });

    for (const auto& Pair : SortedSkills)
    {
        SkillOptions.Add(MakeShared<FString>(Pair.Key));
        MovePaths.Add(Pair.Value);
    }

    for (int32 i = 0; i < NUM_WILD_MOVE_SLOTS; ++i)
        SelectedWildSkills[i] = SkillOptions[0];
}

void SGF_BattleDebuggerPage::LoadTamerClasses()
{
    TamerEntries.Empty();

    // GetDerivedClasses covers all Blueprint subclasses that are currently loaded
    TArray<UClass*> Derived;
    GetDerivedClasses(AGF_TamerMaster::StaticClass(), Derived, /*bRecursive=*/true);

    for (UClass* Class : Derived)
    {
        if (!Class || Class->HasAnyClassFlags(CLASS_Abstract)) continue;

        FString Name = Class->GetName();
        // Skip skeleton classes (SKEL_ prefix = uncompiled BP intermediate)
        if (Name.StartsWith(TEXT("SKEL_"))) continue;
        Name.RemoveFromEnd(TEXT("_C"));

        auto Entry          = MakeShared<FGF_TamerEntry>();
        Entry->TamerClass = Class;
        Entry->DisplayName  = Name;
        TamerEntries.Add(Entry);
    }

    TamerEntries.Sort([](const TSharedPtr<FGF_TamerEntry>& A, const TSharedPtr<FGF_TamerEntry>& B)
    {
        return A->DisplayName < B->DisplayName;
    });

    if (TamerEntries.Num() == 0)
    {
        // Placeholder so the list isn't blank
        auto Placeholder = MakeShared<FGF_TamerEntry>();
        Placeholder->DisplayName = TEXT("(No tamer BPs loaded yet - try Refresh)");
        TamerEntries.Add(Placeholder);
    }
}
