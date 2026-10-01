// GF_ItemDebuggerPage.cpp
#include "GF_ItemDebuggerPage.h"
#include "GF_DebuggerWidget.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_ItemInventorySystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSearchBox.h"

namespace ItemPage
{
    static const FLinearColor PanelBg  { 0.10f, 0.10f, 0.12f, 1.f };
    static const FLinearColor Green    { 0.15f, 0.70f, 0.25f, 1.f };
    static const FLinearColor Gold     { 1.00f, 0.80f, 0.20f, 1.f };
    static const FLinearColor White    { 1.00f, 1.00f, 1.00f, 1.f };
    static const FLinearColor SubText  { 0.60f, 0.60f, 0.60f, 1.f };
    static const FLinearColor DividerColor { 0.20f, 0.20f, 0.24f, 1.f };
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
UWorld* SGF_ItemDebuggerPage::GetWorld() const
{
    if (TSharedPtr<SGF_DebuggerWidget> W = SGF_DebuggerWidget::CurrentInstance.Pin())
        return W->GetWorldContext();
    if (GEngine)
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            if ((Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game) && IsValid(Ctx.World()))
                return Ctx.World();
    return nullptr;
}

static FString CategoryName(EGF_ItemCategory Cat)
{
    switch (Cat)
    {
    case EGF_ItemCategory::Items:     return TEXT("Items");
    case EGF_ItemCategory::Cores: return TEXT("Cores");
    case EGF_ItemCategory::Tomes:       return TEXT("Tomes & HMs");
    case EGF_ItemCategory::Berries:   return TEXT("Berries");
    case EGF_ItemCategory::KeyItems:  return TEXT("Key Items");
    case EGF_ItemCategory::Other:     return TEXT("Other");
    default:                       return TEXT("All");
    }
}

// ─── Construct ───────────────────────────────────────────────────────────────
void SGF_ItemDebuggerPage::Construct(const FArguments& InArgs)
{
    // Category combo options
    CategoryOptions.Add(MakeShared<FString>(TEXT("All")));
    CategoryOptions.Add(MakeShared<FString>(TEXT("Items")));
    CategoryOptions.Add(MakeShared<FString>(TEXT("Cores")));
    CategoryOptions.Add(MakeShared<FString>(TEXT("Tomes & HMs")));
    CategoryOptions.Add(MakeShared<FString>(TEXT("Berries")));
    CategoryOptions.Add(MakeShared<FString>(TEXT("Key Items")));
    CategoryOptions.Add(MakeShared<FString>(TEXT("Other")));

    LoadItemAssets();

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [ BuildGiveItemSection() ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [ BuildMoneySection() ]
            + SVerticalBox::Slot().AutoHeight()
            [ BuildPlayerNameSection() ]
        ]
    ];
}

// ═══════════════════════════════════════════════════════════════════════════════
//  GIVE ITEM SECTION
// ═══════════════════════════════════════════════════════════════════════════════

TSharedRef<SWidget> SGF_ItemDebuggerPage::BuildGiveItemSection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(ItemPage::PanelBg)
        .Padding(8.f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("GIVE ITEM")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                .ColorAndOpacity(ItemPage::Gold)
            ]

            // Filters row
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Category:"))).ColorAndOpacity(ItemPage::SubText) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,10.f,0.f)
                [
                    SAssignNew(CategoryComboBox, STextComboBox)
                    .OptionsSource(&CategoryOptions)
                    .InitiallySelectedItem(CategoryOptions[0])
                    .OnSelectionChanged(this, &SGF_ItemDebuggerPage::OnCategoryChanged)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Search:"))).ColorAndOpacity(ItemPage::SubText) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SSearchBox)
                    .OnTextChanged(this, &SGF_ItemDebuggerPage::OnSearchChanged)
                    .HintText(FText::FromString(TEXT("Filter items...")))
                ]
            ]

            // List + side panel
            + SVerticalBox::Slot().FillHeight(1.f).MaxHeight(220.f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.09f, 1.f))
                    [
                        SAssignNew(ItemListView, SListView<TSharedPtr<FGF_ItemEntry>>)
                        .ListItemsSource(&FilteredItems)
                        .OnGenerateRow(this, &SGF_ItemDebuggerPage::MakeItemRow)
                        .OnSelectionChanged(this, &SGF_ItemDebuggerPage::OnItemSelected)
                        .SelectionMode(ESelectionMode::Single)
                        .ItemHeight(36.f)
                    ]
                ]

                + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                    [
                        SNew(STextBlock)
                        .Text(TAttribute<FText>::CreateSP(this, &SGF_ItemDebuggerPage::GetItemStatusText))
                        .ColorAndOpacity(ItemPage::White)
                        .WrapTextAt(180.f)
                    ]

                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                        [ SNew(STextBlock).Text(FText::FromString(TEXT("Qty:"))).ColorAndOpacity(ItemPage::SubText) ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SAssignNew(QuantitySpinBox, SSpinBox<int32>)
                            .MinValue(1).MaxValue(999).Value(1).MinDesiredWidth(60.f)
                            .OnValueChanged_Lambda([this](int32 V){ GiveQuantity = V; })
                        ]
                    ]

                    + SVerticalBox::Slot().FillHeight(1.f)

                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Give Item")))
                        .ButtonColorAndOpacity(ItemPage::Green)
                        .ForegroundColor(ItemPage::White)
                        .OnClicked(this, &SGF_ItemDebuggerPage::OnGiveItemClicked)
                    ]
                ]
            ]
        ];
}

TSharedRef<ITableRow> SGF_ItemDebuggerPage::MakeItemRow(
    TSharedPtr<FGF_ItemEntry> Item,
    const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<TSharedPtr<FGF_ItemEntry>>, Owner)
    [
        SNew(SHorizontalBox)

        // Icon
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.f, 1.f)
        [
            SNew(SBox).WidthOverride(32.f).HeightOverride(32.f)
            [
                Item->IconBrush.IsValid()
                    ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Item->IconBrush.Get()))
                    : StaticCastSharedRef<SWidget>(SNew(SBox))
            ]
        ]

        // Display name
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4.f, 2.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Item->DisplayName))
            .ColorAndOpacity(ItemPage::White)
        ]

        // Category label
        + SHorizontalBox::Slot().AutoWidth().Padding(4.f, 2.f).VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString(CategoryName(Item->Category)))
            .ColorAndOpacity(ItemPage::SubText)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
        ]
    ];
}

void SGF_ItemDebuggerPage::OnItemSelected(TSharedPtr<FGF_ItemEntry> Item, ESelectInfo::Type)
{
    SelectedItem    = Item;
    ItemStatusMsg   = Item.IsValid() ? FString::Printf(TEXT("%s selected."), *Item->DisplayName) : TEXT("");
}

FText SGF_ItemDebuggerPage::GetItemStatusText() const
{
    return FText::FromString(ItemStatusMsg.IsEmpty() ? TEXT("Select an item.") : ItemStatusMsg);
}

FReply SGF_ItemDebuggerPage::OnGiveItemClicked()
{
    UWorld* World = GetWorld();
    if (!World) { ItemStatusMsg = TEXT("ERROR: No world."); return FReply::Handled(); }
    if (!SelectedItem.IsValid()) { ItemStatusMsg = TEXT("Select an item first!"); return FReply::Handled(); }

    UGameInstance* GI = World->GetGameInstance();
    UGF_CreatureManagerSubsystem* CreatureMgr = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
    if (!CreatureMgr || !CreatureMgr->Inventory)
    {
        ItemStatusMsg = TEXT("ERROR: Inventory not found.");
        return FReply::Handled();
    }

    const bool bOk = CreatureMgr->Inventory->AddItemByName(SelectedItem->Category, SelectedItem->ItemName, GiveQuantity);
    ItemStatusMsg = bOk
        ? FString::Printf(TEXT("Gave %dx %s"), GiveQuantity, *SelectedItem->DisplayName)
        : FString::Printf(TEXT("ERROR: Could not add %s"), *SelectedItem->DisplayName);

    return FReply::Handled();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MONEY SECTION
// ═══════════════════════════════════════════════════════════════════════════════

TSharedRef<SWidget> SGF_ItemDebuggerPage::BuildMoneySection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(ItemPage::PanelBg)
        .Padding(8.f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("PLAYER MONEY")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                .ColorAndOpacity(ItemPage::Gold)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,8.f,0.f)
                [
                    SNew(STextBlock)
                    .Text(TAttribute<FText>::CreateSP(this, &SGF_ItemDebuggerPage::GetCurrentMoneyText))
                    .ColorAndOpacity(ItemPage::White)
                ]
            ]

            // Set money row
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Set to:"))).ColorAndOpacity(ItemPage::SubText) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
                [
                    SAssignNew(SetMoneySpinBox, SSpinBox<int32>)
                    .MinValue(0).MaxValue(999999).Value(0).MinDesiredWidth(90.f)
                    .OnValueChanged_Lambda([this](int32 V){ NewMoneyAmount = V; })
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Set Money")))
                    .ButtonColorAndOpacity(FLinearColor(0.2f, 0.45f, 0.8f, 1.f))
                    .ForegroundColor(ItemPage::White)
                    .OnClicked(this, &SGF_ItemDebuggerPage::OnSetMoneyClicked)
                ]
            ]

            // Add money row
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Add:"))).ColorAndOpacity(ItemPage::SubText) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
                [
                    SAssignNew(AddMoneySpinBox, SSpinBox<int32>)
                    .MinValue(1).MaxValue(999999).Value(1000).MinDesiredWidth(90.f)
                    .OnValueChanged_Lambda([this](int32 V){ AddMoneyAmount = V; })
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("+ Add Money")))
                    .ButtonColorAndOpacity(ItemPage::Green)
                    .ForegroundColor(ItemPage::White)
                    .OnClicked(this, &SGF_ItemDebuggerPage::OnAddMoneyClicked)
                ]
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(TAttribute<FText>::CreateSP(this, &SGF_ItemDebuggerPage::GetMoneyStatusText))
                .ColorAndOpacity(ItemPage::White)
            ]
        ];
}

FText SGF_ItemDebuggerPage::GetCurrentMoneyText() const
{
    UWorld* World = GetWorld();
    if (!World) return FText::FromString(TEXT("Current: $---"));
    UGameInstance* GI = World->GetGameInstance();
    UGF_CreatureManagerSubsystem* CreatureMgr = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
    if (!CreatureMgr || !CreatureMgr->Inventory) return FText::FromString(TEXT("Current: $---"));
    return FText::FromString(FString::Printf(TEXT("Current: $%d"), CreatureMgr->Inventory->Money));
}

FReply SGF_ItemDebuggerPage::OnSetMoneyClicked()
{
    UWorld* World = GetWorld();
    if (!World) { MoneyStatusMsg = TEXT("ERROR: No world."); return FReply::Handled(); }
    UGameInstance* GI = World->GetGameInstance();
    UGF_CreatureManagerSubsystem* CreatureMgr = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
    if (!CreatureMgr || !CreatureMgr->Inventory) { MoneyStatusMsg = TEXT("ERROR: No inventory."); return FReply::Handled(); }

    CreatureMgr->Inventory->Money = FMath::Clamp(NewMoneyAmount, 0, 999999);
    MoneyStatusMsg = FString::Printf(TEXT("Money set to $%d"), CreatureMgr->Inventory->Money);
    return FReply::Handled();
}

FReply SGF_ItemDebuggerPage::OnAddMoneyClicked()
{
    UWorld* World = GetWorld();
    if (!World) { MoneyStatusMsg = TEXT("ERROR: No world."); return FReply::Handled(); }
    UGameInstance* GI = World->GetGameInstance();
    UGF_CreatureManagerSubsystem* CreatureMgr = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
    if (!CreatureMgr || !CreatureMgr->Inventory) { MoneyStatusMsg = TEXT("ERROR: No inventory."); return FReply::Handled(); }

    CreatureMgr->Inventory->Money = FMath::Clamp(CreatureMgr->Inventory->Money + AddMoneyAmount, 0, 999999);
    MoneyStatusMsg = FString::Printf(TEXT("Money is now $%d"), CreatureMgr->Inventory->Money);
    return FReply::Handled();
}

FText SGF_ItemDebuggerPage::GetMoneyStatusText() const { return FText::FromString(MoneyStatusMsg); }

// ═══════════════════════════════════════════════════════════════════════════════
//  PLAYER NAME SECTION
// ═══════════════════════════════════════════════════════════════════════════════

TSharedRef<SWidget> SGF_ItemDebuggerPage::BuildPlayerNameSection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(ItemPage::PanelBg)
        .Padding(8.f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("PLAYER NAME")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                .ColorAndOpacity(ItemPage::Gold)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
            [
                SNew(STextBlock)
                .Text(TAttribute<FText>::CreateSP(this, &SGF_ItemDebuggerPage::GetCurrentNameText))
                .ColorAndOpacity(ItemPage::White)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("New name:"))).ColorAndOpacity(ItemPage::SubText) ]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f,0.f,6.f,0.f)
                [
                    SAssignNew(NameInputBox, SEditableTextBox)
                    .HintText(FText::FromString(TEXT("Enter name...")))
                    .OnTextChanged_Lambda([this](const FText& T){ NewPlayerName = T.ToString(); })
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Set Name")))
                    .ButtonColorAndOpacity(FLinearColor(0.2f, 0.45f, 0.8f, 1.f))
                    .ForegroundColor(ItemPage::White)
                    .OnClicked(this, &SGF_ItemDebuggerPage::OnSetNameClicked)
                ]
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(TAttribute<FText>::CreateSP(this, &SGF_ItemDebuggerPage::GetNameStatusText))
                .ColorAndOpacity(ItemPage::White)
            ]
        ];
}

FText SGF_ItemDebuggerPage::GetCurrentNameText() const
{
    UWorld* World = GetWorld();
    if (!World) return FText::FromString(TEXT("Current: ---"));
    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC) return FText::FromString(TEXT("Current: ---"));
    APlayerState* PS = PC->GetPlayerState<APlayerState>();
    FString Name = PS ? PS->GetPlayerName() : TEXT("---");
    return FText::FromString(FString::Printf(TEXT("Current: %s"), *Name));
}

FReply SGF_ItemDebuggerPage::OnSetNameClicked()
{
    UWorld* World = GetWorld();
    if (!World) { NameStatusMsg = TEXT("ERROR: No world."); return FReply::Handled(); }
    if (NewPlayerName.IsEmpty()) { NameStatusMsg = TEXT("Enter a name first."); return FReply::Handled(); }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC) { NameStatusMsg = TEXT("ERROR: No PlayerController."); return FReply::Handled(); }
    APlayerState* PS = PC->GetPlayerState<APlayerState>();
    if (!PS) { NameStatusMsg = TEXT("ERROR: No PlayerState."); return FReply::Handled(); }

    // Sets the runtime player name (shown in UMG HUD etc.)
    PS->SetPlayerName(NewPlayerName);

    // TODO: If you store the player name in a custom save game / subsystem,
    // also update it here. E.g.: MySaveSubsystem->PlayerName = NewPlayerName;

    NameStatusMsg = FString::Printf(TEXT("Name set to '%s'"), *NewPlayerName);
    return FReply::Handled();
}

FText SGF_ItemDebuggerPage::GetNameStatusText() const { return FText::FromString(NameStatusMsg); }

// ─── Category / search ────────────────────────────────────────────────────────
void SGF_ItemDebuggerPage::OnCategoryChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type)
{
    if (!NewSel.IsValid()) return;
    const FString& S = *NewSel;
    if      (S == TEXT("Items"))      ActiveCategory = EGF_ItemCategory::Items;
    else if (S == TEXT("Cores")) ActiveCategory = EGF_ItemCategory::Cores;
    else if (S == TEXT("Tomes & HMs"))  ActiveCategory = EGF_ItemCategory::Tomes;
    else if (S == TEXT("Berries"))    ActiveCategory = EGF_ItemCategory::Berries;
    else if (S == TEXT("Key Items"))  ActiveCategory = EGF_ItemCategory::KeyItems;
    else if (S == TEXT("Other"))      ActiveCategory = EGF_ItemCategory::Other;
    else                              ActiveCategory = EGF_ItemCategory::MAX; // All

    ApplyItemFilter();
}

void SGF_ItemDebuggerPage::OnSearchChanged(const FText& NewText)
{
    SearchFilter = NewText.ToString();
    ApplyItemFilter();
}

TSharedRef<SWidget> SGF_ItemDebuggerPage::MakeCategoryWidget(TSharedPtr<FString> Item)
{
    return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
}

// ─── Data loading ─────────────────────────────────────────────────────────────
void SGF_ItemDebuggerPage::LoadItemAssets()
{
    AllItems.Empty();

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

        auto Entry         = MakeShared<FGF_ItemEntry>();
        Entry->ItemData    = Item;
        Entry->ItemName    = Item->ItemName;
        Entry->Category    = Item->Category;
        Entry->DisplayName = Item->DisplayName.ToString().IsEmpty()
            ? Item->ItemName.ToString()
            : Item->DisplayName.ToString();

        // Build icon brush from the item's icon texture
        if (IsValid(Item->Icon))
        {
            Entry->IconBrush = MakeShared<FSlateImageBrush>(
                Item->Icon, FVector2D(32.f, 32.f));
        }

        AllItems.Add(Entry);
    }

    AllItems.Sort([](const TSharedPtr<FGF_ItemEntry>& A, const TSharedPtr<FGF_ItemEntry>& B)
    {
        return A->DisplayName < B->DisplayName;
    });

    ApplyItemFilter();
}

void SGF_ItemDebuggerPage::ApplyItemFilter()
{
    FilteredItems.Empty();
    for (const auto& E : AllItems)
    {
        if (ActiveCategory != EGF_ItemCategory::MAX && E->Category != ActiveCategory) continue;
        if (!SearchFilter.IsEmpty() && !E->DisplayName.Contains(SearchFilter, ESearchCase::IgnoreCase)) continue;
        FilteredItems.Add(E);
    }
    if (ItemListView.IsValid())
        ItemListView->RequestListRefresh();
}