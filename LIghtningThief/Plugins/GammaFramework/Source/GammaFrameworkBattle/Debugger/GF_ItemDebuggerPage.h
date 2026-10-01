// GF_ItemDebuggerPage.h
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SSpinBox.h"     // TSharedPtr<SSpinBox<int32>> members below
#include "Widgets/Input/STextComboBox.h" // TSharedPtr<STextComboBox> member below
#include "Brushes/SlateImageBrush.h"
#include "GF_ItemEnums.h"
#include "GF_ItemData.h"

struct FGF_ItemEntry
{
    TWeakObjectPtr<UGF_ItemData>      ItemData;
    TSharedPtr<FSlateImageBrush>   IconBrush;
    FString  DisplayName;
    FName    ItemName;
    EGF_ItemCategory Category = EGF_ItemCategory::Items;
};

/**
 * Items / Player debug page.
 *
 * ┌───────────────────────────────────────┐
 * │  GIVE ITEM                            │
 * │  Category: [v]  Search: [__________] │
 * │  [Item list]       Qty: [__]          │
 * │                    [Give Item]        │
 * ├───────────────────────────────────────┤
 * │  PLAYER MONEY                         │
 * │  Current: $____    New: [___]         │
 * │  [Set Money]  [+Add Money]            │
 * ├───────────────────────────────────────┤
 * │  PLAYER NAME                          │
 * │  Current: ____    New: [__________]  │
 * │  [Set Name]                           │
 * └───────────────────────────────────────┘
 */
class GAMMAFRAMEWORKBATTLE_API SGF_ItemDebuggerPage : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGF_ItemDebuggerPage) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // ── Data loading ─────────────────────────────────────────────────────────
    void LoadItemAssets();
    void ApplyItemFilter();

    // ── UI builders ──────────────────────────────────────────────────────────
    TSharedRef<SWidget> BuildGiveItemSection();
    TSharedRef<SWidget> BuildMoneySection();
    TSharedRef<SWidget> BuildPlayerNameSection();

    // ── List callbacks ───────────────────────────────────────────────────────
    TSharedRef<ITableRow> MakeItemRow(TSharedPtr<FGF_ItemEntry> Item, const TSharedRef<STableViewBase>& Owner);
    void OnItemSelected(TSharedPtr<FGF_ItemEntry> Item, ESelectInfo::Type);

    // ── Category filter ───────────────────────────────────────────────────────
    TSharedRef<SWidget> MakeCategoryWidget(TSharedPtr<FString> Item);
    void OnCategoryChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type);

    // ── Search filter ─────────────────────────────────────────────────────────
    void OnSearchChanged(const FText& NewText);

    // ── Actions ───────────────────────────────────────────────────────────────
    FReply OnGiveItemClicked();
    FReply OnSetMoneyClicked();
    FReply OnAddMoneyClicked();
    FReply OnSetNameClicked();

    // ── Display text ──────────────────────────────────────────────────────────
    FText GetCurrentMoneyText() const;
    FText GetCurrentNameText() const;
    FText GetItemStatusText() const;
    FText GetMoneyStatusText() const;
    FText GetNameStatusText() const;

    UWorld* GetWorld() const;

private:
    // Items
    TArray<TSharedPtr<FGF_ItemEntry>>  AllItems;
    TArray<TSharedPtr<FGF_ItemEntry>>  FilteredItems;
    TSharedPtr<FGF_ItemEntry>          SelectedItem;
    TSharedPtr<SListView<TSharedPtr<FGF_ItemEntry>>> ItemListView;

    FString         SearchFilter;
    EGF_ItemCategory   ActiveCategory = EGF_ItemCategory::MAX; // MAX = all categories

    TArray<TSharedPtr<FString>> CategoryOptions;
    TSharedPtr<STextComboBox>   CategoryComboBox;

    // Give item
    int32  GiveQuantity = 1;
    TSharedPtr<SSpinBox<int32>> QuantitySpinBox;

    // Money
    int32  NewMoneyAmount    = 0;
    int32  AddMoneyAmount    = 1000;
    TSharedPtr<SSpinBox<int32>> SetMoneySpinBox;
    TSharedPtr<SSpinBox<int32>> AddMoneySpinBox;

    // Name
    FString NewPlayerName;
    TSharedPtr<SEditableTextBox> NameInputBox;

    // Status messages
    FString ItemStatusMsg;
    FString MoneyStatusMsg;
    FString NameStatusMsg;
};