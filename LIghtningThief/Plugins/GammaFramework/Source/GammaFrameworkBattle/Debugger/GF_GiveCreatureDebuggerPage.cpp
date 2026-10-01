// GF_GiveCreatureDebuggerPage.cpp
#include "GF_GiveCreatureDebuggerPage.h"
#include "GF_ElementTypes.h"
#include "GF_DebuggerWidget.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_CreatureStatLibrary.h"
#include "GF_ItemData.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Text/STextBlock.h"

namespace GivePoke
{
    static const FLinearColor PanelBg  { 0.10f, 0.10f, 0.12f, 1.f };
    static const FLinearColor Green    { 0.15f, 0.70f, 0.25f, 1.f };
    static const FLinearColor Gold     { 1.00f, 0.80f, 0.20f, 1.f };
    static const FLinearColor White    { 1.00f, 1.00f, 1.00f, 1.f };
    static const FLinearColor SubText  { 0.60f, 0.60f, 0.60f, 1.f };
    static const FLinearColor UniqueCol { 1.00f, 0.92f, 0.23f, 1.f };
}

// ─── Type helpers (mirrors BattlePage — keep in sync or factor into shared TU) ─
FLinearColor SGF_GiveCreatureDebuggerPage::TypeToColor(EGF_Element T)
{
    return UGF_ElementLibrary::GetElementColor(T);
}
FString SGF_GiveCreatureDebuggerPage::TypeToString(EGF_Element T)
{
    const UEnum* E = StaticEnum<EGF_Element>();
    return E ? E->GetDisplayNameTextByValue((int64)T).ToString() : TEXT("???");
}

UWorld* SGF_GiveCreatureDebuggerPage::GetWorld() const
{
    if (TSharedPtr<SGF_DebuggerWidget> W = SGF_DebuggerWidget::CurrentInstance.Pin())
        return W->GetWorldContext();
    if (GEngine)
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            if ((Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game) && IsValid(Ctx.World()))
                return Ctx.World();
    return nullptr;
}

// ─── Construct ───────────────────────────────────────────────────────────────
void SGF_GiveCreatureDebuggerPage::Construct(const FArguments& InArgs)
{
    LoadCreatureAssets();
    LoadHeldItemAssets();
    LoadSkillAssets();

    ChildSlot
    [
        SNew(SVerticalBox)

        // Type filter bar
        + SVerticalBox::Slot().AutoHeight().Padding(6.f, 6.f, 6.f, 3.f)
        [ BuildTypeFilterBar() ]

        // Main split: list left, options right
        + SVerticalBox::Slot().FillHeight(1.f).Padding(6.f, 0.f)
        [
            SNew(SHorizontalBox)

            // ── Creature list ──────────────────────────────────────────────────
            + SHorizontalBox::Slot().FillWidth(1.f)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.09f, 1.f))
                [
                    SAssignNew(CreatureListView, STileView<TSharedPtr<FGF_GiveCreatureEntry>>)
                    .ListItemsSource(&FilteredCreature)
                    .OnGenerateTile(this, &SGF_GiveCreatureDebuggerPage::MakeCreatureTile)
                    .OnSelectionChanged(this, &SGF_GiveCreatureDebuggerPage::OnCreatureSelected)
                    .SelectionMode(ESelectionMode::Single)
                    .ItemWidth(80.f)
                    .ItemHeight(90.f)
                ]
            ]

            // ── Option panel ──────────────────────────────────────────────────
            + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
            [ BuildOptionPanel() ]
        ]
    ];
}

// ─── Type filter bar ─────────────────────────────────────────────────────────
TSharedRef<SWidget> SGF_GiveCreatureDebuggerPage::BuildTypeFilterBar()
{
    TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);

    Bar->AddSlot().AutoWidth().Padding(2.f, 0.f)
    [
        SNew(SButton)
        .Text(FText::FromString(TEXT("ALL")))
        .ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]()
        {
            return FSlateColor(bShowAll ? FLinearColor(0.3f,0.6f,1.f,1.f) : FLinearColor(0.2f,0.2f,0.2f,1.f));
        }))
        .ForegroundColor(GivePoke::White)
        .OnClicked_Lambda([this]() -> FReply
        {
            bShowAll = true;
            ActiveTypeFilter = EGF_Element::None;
            ApplyTypeFilter();
            return FReply::Handled();
        })
    ];

    const UEnum* TypeEnum = StaticEnum<EGF_Element>();
    for (int32 i = 1; i < TypeEnum->NumEnums() - 1; ++i)
    {
        EGF_Element Type = (EGF_Element)TypeEnum->GetValueByIndex(i);
        Bar->AddSlot().AutoWidth().Padding(2.f, 0.f)
        [
            SNew(SButton)
            .Text(FText::FromString(TypeToString(Type)))
            .ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &SGF_GiveCreatureDebuggerPage::GetTypeButtonColor, Type))
            .ForegroundColor(GivePoke::White)
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

FSlateColor SGF_GiveCreatureDebuggerPage::GetTypeButtonColor(EGF_Element Type) const
{
    return FSlateColor(!bShowAll && ActiveTypeFilter == Type
        ? TypeToColor(Type)
        : TypeToColor(Type) * 0.45f);
}

void SGF_GiveCreatureDebuggerPage::OnTypeFilterClicked(EGF_Element Type)
{
    bShowAll = false;
    ActiveTypeFilter = Type;
    ApplyTypeFilter();
}

// ─── Creature tile (grid cell) ─────────────────────────────────────────────────
TSharedRef<ITableRow> SGF_GiveCreatureDebuggerPage::MakeCreatureTile(
    TSharedPtr<FGF_GiveCreatureEntry> Item,
    const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<TSharedPtr<FGF_GiveCreatureEntry>>, Owner)
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

        // Species name
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromName(Item->SpeciesData.IsValid()
                ? Item->SpeciesData->SpeciesName : NAME_None))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
            .ColorAndOpacity(GivePoke::White)
            .Justification(ETextJustify::Center)
        ]

        // Primary type emblem
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 1.f)
        [
            SNew(SBorder)
            .BorderBackgroundColor(TypeToColor(Item->PrimaryElement))
            .Padding(FMargin(3.f, 0.f))
            [
                SNew(STextBlock)
                .Text(FText::FromString(TypeToString(Item->PrimaryElement)))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 6))
                .ColorAndOpacity(GivePoke::White)
            ]
        ]
    ];
}

void SGF_GiveCreatureDebuggerPage::OnCreatureSelected(TSharedPtr<FGF_GiveCreatureEntry> Item, ESelectInfo::Type)
{
    SelectedCreature = Item;
    StatusMessage   = TEXT("");
}

FText SGF_GiveCreatureDebuggerPage::GetSelectionText() const
{
    if (!SelectedCreature.IsValid())
        return FText::FromString(TEXT("No Creature selected."));

    FString Name = SelectedCreature->SpeciesData.IsValid()
        ? SelectedCreature->SpeciesData->SpeciesName.ToString()
        : TEXT("???");

    return FText::FromString(FString::Printf(TEXT("%s is selected!"), *Name));
}

FText SGF_GiveCreatureDebuggerPage::GetStatusText() const
{
    return FText::FromString(StatusMessage);
}

// ─── Option panel ─────────────────────────────────────────────────────────────
TSharedRef<SWidget> SGF_GiveCreatureDebuggerPage::BuildOptionPanel()
{
    return SNew(SVerticalBox)
        .Clipping(EWidgetClipping::ClipToBounds)

        // Selection banner
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
        [
            SNew(SBorder)
            .BorderBackgroundColor(FLinearColor(0.14f, 0.28f, 0.50f, 1.f))
            .Padding(6.f)
            [
                SNew(STextBlock)
                .Text(TAttribute<FText>::CreateSP(this, &SGF_GiveCreatureDebuggerPage::GetSelectionText))
                .ColorAndOpacity(GivePoke::White)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                .WrapTextAt(210.f)
            ]
        ]

        // Level
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Level:"))).ColorAndOpacity(GivePoke::SubText) ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SAssignNew(LevelSpinBox, SSpinBox<int32>)
                .MinValue(1).MaxValue(100).Value(GiveLevel).MinDesiredWidth(60.f)
                .OnValueChanged_Lambda([this](int32 V){ GiveLevel = V; })
            ]
        ]

        // Unique
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Unique:"))).ColorAndOpacity(GivePoke::UniqueCol) ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SAssignNew(UniqueCheckBox, SCheckBox)
                .IsChecked(ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                {
                    bIsUnique = (S == ECheckBoxState::Checked);
                })
            ]
        ]

        // Held item
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Held Item:"))).ColorAndOpacity(GivePoke::SubText) ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SAssignNew(HeldItemComboBox, STextComboBox)
                .OptionsSource(&HeldItemDisplayOptions)
                .InitiallySelectedItem(HeldItemDisplayOptions.Num() > 0 ? HeldItemDisplayOptions[0] : nullptr)
                .OnSelectionChanged(this, &SGF_GiveCreatureDebuggerPage::OnHeldItemChanged)
            ]
        ]

        // ── Skill override section ─────────────────────────────────────────────
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 3.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Skill Override  (empty = level-up defaults)")))
            .ColorAndOpacity(GivePoke::Gold)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
        [ BuildMoveOverrideRow(0) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
        [ BuildMoveOverrideRow(1) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
        [ BuildMoveOverrideRow(2) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
        [ BuildMoveOverrideRow(3) ]

        // Status text
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [
            SNew(STextBlock)
            .Text(TAttribute<FText>::CreateSP(this, &SGF_GiveCreatureDebuggerPage::GetStatusText))
            .ColorAndOpacity(GivePoke::White)
            .WrapTextAt(220.f)
        ]

        + SVerticalBox::Slot().FillHeight(1.f)

        // Receive button
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Receive Creature")))
            .ButtonColorAndOpacity(GivePoke::Green)
            .ForegroundColor(GivePoke::White)
            .OnClicked(this, &SGF_GiveCreatureDebuggerPage::OnReceiveCreature)
        ];
}

TSharedRef<SWidget> SGF_GiveCreatureDebuggerPage::BuildMoveOverrideRow(int32 SlotIndex)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,4.f,0.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(TEXT("Skill %d:"), SlotIndex + 1)))
            .ColorAndOpacity(GivePoke::SubText)
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SAssignNew(SkillComboBoxes[SlotIndex], STextComboBox)
            .OptionsSource(&SkillOptions)
            .InitiallySelectedItem(SkillOptions.Num() > 0 ? SkillOptions[0] : nullptr)
            .OnSelectionChanged_Lambda([this, SlotIndex](TSharedPtr<FString> Sel, ESelectInfo::Type Info)
            {
                OnSkillChanged(Sel, Info, SlotIndex);
            })
        ];
}

// ─── Held item helpers ────────────────────────────────────────────────────────
TSharedRef<SWidget> SGF_GiveCreatureDebuggerPage::MakeHeldItemWidget(TSharedPtr<FString> Item)
{
    return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
}
void SGF_GiveCreatureDebuggerPage::OnHeldItemChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type)
{
    SelectedHeldItem = NewSel;
}

// ─── Skill combo helpers ───────────────────────────────────────────────────────
TSharedRef<SWidget> SGF_GiveCreatureDebuggerPage::MakeSkillWidget(TSharedPtr<FString> Item)
{
    return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
}
void SGF_GiveCreatureDebuggerPage::OnSkillChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type, int32 Slot)
{
    if (Slot >= 0 && Slot < NUM_MOVE_SLOTS)
        SelectedSkills[Slot] = NewSel;
}

// ─── Receive Creature ──────────────────────────────────────────────────────────
FReply SGF_GiveCreatureDebuggerPage::OnReceiveCreature()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        StatusMessage = TEXT("ERROR: No world context.");
        return FReply::Handled();
    }
    if (!SelectedCreature.IsValid() || !SelectedCreature->SpeciesData.IsValid())
    {
        StatusMessage = TEXT("Select a Creature first!");
        return FReply::Handled();
    }

    UGameInstance* GI = World->GetGameInstance();
    if (!GI)
    {
        StatusMessage = TEXT("ERROR: No GameInstance.");
        return FReply::Handled();
    }

    UGF_CreatureManagerSubsystem* CreatureMgr = GI->GetSubsystem<UGF_CreatureManagerSubsystem>();
    if (!CreatureMgr)
    {
        StatusMessage = TEXT("ERROR: CreatureManagerSubsystem not found.");
        return FReply::Handled();
    }

    UGF_CreatureSpeciesData* Species = SelectedCreature->SpeciesData.Get();
    FString SpeciesName = Species->SpeciesName.ToString();

    // Resolve move overrides
    TArray<TSubclassOf<AGF_SkillDefinition>> ForcedSkills;
    bool bHasAnySkillOverride = false;
    for (int32 i = 0; i < NUM_MOVE_SLOTS; ++i)
    {
        if (!SelectedSkills[i].IsValid()) continue;
        const FString& Str = *SelectedSkills[i];
        if (Str.IsEmpty() || Str == TEXT("(none)")) continue;

        // Find matching class path
        for (int32 j = 0; j < SkillOptions.Num(); ++j)
        {
            if (SkillOptions[j].IsValid() && *SkillOptions[j] == Str && MovePaths.IsValidIndex(j))
            {
                UClass* SkillClass = LoadObject<UClass>(nullptr, *MovePaths[j]);
                if (SkillClass && SkillClass->IsChildOf(AGF_SkillDefinition::StaticClass()))
                {
                    ForcedSkills.Add(TSubclassOf<AGF_SkillDefinition>(SkillClass));
                    bHasAnySkillOverride = true;
                }
                break;
            }
        }
    }

    // Create instance
    FGF_CreatureInstanceData NewCreature = bHasAnySkillOverride
        ? CreatureMgr->CreateCreatureWithSkills(Species, GiveLevel, ForcedSkills)
        : CreatureMgr->CreateCreature(Species, GiveLevel);

    // ── Fix the placeholder MaxHP=100 with real Gen-3 formula ─────────────────
    {
        FGF_CreatureCurrentStats Calculated = UGF_CreatureStatLibrary::CalculateCreatureStats(NewCreature);
        NewCreature.MaxHP    = Calculated.MaxHP;
        NewCreature.CurrentHP = Calculated.MaxHP; // full HP on receive
    }
    // ─────────────────────────────────────────────────────────────────────────

    // Apply unique flag
    NewCreature.bIsUnique = bIsUnique;

    // Apply held item
    if (SelectedHeldItem.IsValid() && !SelectedHeldItem->IsEmpty() && *SelectedHeldItem != TEXT("(none)"))
    {
        // Find matching item name
        for (int32 i = 0; i < HeldItemDisplayOptions.Num(); ++i)
        {
            if (HeldItemDisplayOptions[i].IsValid() && *HeldItemDisplayOptions[i] == *SelectedHeldItem
                && HeldItemNames.IsValidIndex(i))
            {
                NewCreature.HeldItem = HeldItemNames[i];
                break;
            }
        }
    }

    // Add to party; if full, falls back to boxes automatically
    const bool bPartyWasFull = CreatureMgr->IsPartyFull();
    const bool bAdded        = CreatureMgr->GiveCreatureInstance(NewCreature);

    if (bAdded)
    {
        StatusMessage = FString::Printf(TEXT("%s%s added to %s! (Lv %d)"),
            bIsUnique ? TEXT("") : TEXT(""),
            *SpeciesName,
            bPartyWasFull ? TEXT("Vault") : TEXT("Party"),
            GiveLevel);
    }
    else
    {
        StatusMessage = FString::Printf(TEXT("ERROR: Failed to add %s. Check box space."), *SpeciesName);
    }

    return FReply::Handled();
}

// ─── Data loading ─────────────────────────────────────────────────────────────
void SGF_GiveCreatureDebuggerPage::LoadCreatureAssets()
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
        UGF_CreatureSpeciesData* S = Cast<UGF_CreatureSpeciesData>(Asset.GetAsset());
        if (!S) continue;
        auto E           = MakeShared<FGF_GiveCreatureEntry>();
        E->SpeciesData   = S;
        E->PrimaryElement   = S->PrimaryElement;
        E->SecondaryElement = S->SecondaryElement;
        E->DisplayName   = FString::Printf(TEXT("%03d  %s"), S->CompendiumNumber, *S->SpeciesName.ToString());

        // Pass UPaperSprite directly — Slate reads its UV region for the correct frame
        if (IsValid(S->DisplayIcon1))
        {
            E->IconBrush = MakeShared<FSlateImageBrush>(
                S->DisplayIcon1, FVector2D(64.f, 64.f));
        }

        AllCreature.Add(E);
    }

    AllCreature.Sort([](const TSharedPtr<FGF_GiveCreatureEntry>& A, const TSharedPtr<FGF_GiveCreatureEntry>& B)
    {
        return A->SpeciesData.IsValid() && B->SpeciesData.IsValid()
            && A->SpeciesData->CompendiumNumber < B->SpeciesData->CompendiumNumber;
    });

    ApplyTypeFilter();
}

void SGF_GiveCreatureDebuggerPage::ApplyTypeFilter()
{
    FilteredCreature.Empty();
    for (const auto& E : AllCreature)
    {
        if (bShowAll || E->PrimaryElement == ActiveTypeFilter || E->SecondaryElement == ActiveTypeFilter)
            FilteredCreature.Add(E);
    }
    if (CreatureListView.IsValid())
        CreatureListView->RequestListRefresh();
}

void SGF_GiveCreatureDebuggerPage::LoadHeldItemAssets()
{
    HeldItemDisplayOptions.Empty();
    HeldItemNames.Empty();

    // "(none)" as first option
    HeldItemDisplayOptions.Add(MakeShared<FString>(TEXT("(none)")));
    HeldItemNames.Add(NAME_None);

    FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    FARFilter Filter;
    Filter.ClassPaths.Add(UGF_ItemData::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths   = true;
    Filter.PackagePaths.Add(TEXT("/Game"));

    TArray<FAssetData> Assets;
    AR.Get().GetAssets(Filter, Assets);

    for (const FAssetData& Asset : Assets)
    {
        UGF_ItemData* Item = Cast<UGF_ItemData>(Asset.GetAsset());
        if (!Item) continue;
        // Only include items that have a held effect (i.e. valid held items)
        if (Item->HeldItemEffect == EGF_HeldItemEffect::None) continue;

        FString DisplayStr = Item->DisplayName.ToString();
        if (DisplayStr.IsEmpty()) DisplayStr = Item->ItemName.ToString();

        HeldItemDisplayOptions.Add(MakeShared<FString>(DisplayStr));
        HeldItemNames.Add(Item->ItemName);
    }

    SelectedHeldItem = HeldItemDisplayOptions[0];
}

void SGF_GiveCreatureDebuggerPage::LoadSkillAssets()
{
    SkillOptions.Empty();
    MovePaths.Empty();

    SkillOptions.Add(MakeShared<FString>(TEXT("(none)")));
    MovePaths.Add(TEXT(""));

    // Scan the actual moves folder by path — avoids SKEL_ skeletons and base classes
    // that GetDerivedClasses() would include.
    // Folders holding this project's move assets.
    //
    // Config/DefaultGame.ini:
    //   [GammaFramework.Skills]
    //   +SkillDataPaths=/Game/Godsmarch/Blueprints/Attacks
    TArray<FString> SkillScanPaths;
    if (GConfig)
    {
        GConfig->GetArray(TEXT("GammaFramework.Skills"), TEXT("SkillDataPaths"),
                          SkillScanPaths, GGameIni);
    }

    // Legacy path, kept so Gamma Framework still lists moves without an ini entry.
    if (SkillScanPaths.Num() == 0)
    {
        SkillScanPaths.Add(TEXT("/Game/BPS/ABILITIES/Skills"));
    }

    FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    FARFilter Filter;
    for (const FString& ScanPath : SkillScanPaths)
    {
        Filter.PackagePaths.Add(FName(*ScanPath));
    }
    Filter.bRecursivePaths   = true;
    Filter.bRecursiveClasses = true;
    // BlueprintGeneratedClass gives us the _C class we can LoadObject on
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));

    TArray<FAssetData> Assets;
    AR.Get().GetAssets(Filter, Assets);

    TArray<TPair<FString, FString>> NamePathPairs;
    for (const FAssetData& Asset : Assets)
    {
        // Skip skeleton and abstract base
        FString Name = Asset.AssetName.ToString();
        if (Name.StartsWith(TEXT("SKEL_"))) continue;

        // The _C class path: /Game/.../BP_Move_Tackle.BP_Move_Tackle_C
        FString ClassPath = Asset.GetObjectPathString() + TEXT("_C");

        NamePathPairs.Add({ Name, ClassPath });
    }

    NamePathPairs.Sort([](const TPair<FString,FString>& A, const TPair<FString,FString>& B)
    {
        return A.Key < B.Key;
    });

    for (const auto& Pair : NamePathPairs)
    {
        SkillOptions.Add(MakeShared<FString>(Pair.Key));
        MovePaths.Add(Pair.Value);
    }

    // Init selected move slots to "(none)"
    for (int32 i = 0; i < NUM_MOVE_SLOTS; ++i)
        SelectedSkills[i] = SkillOptions[0];
}
