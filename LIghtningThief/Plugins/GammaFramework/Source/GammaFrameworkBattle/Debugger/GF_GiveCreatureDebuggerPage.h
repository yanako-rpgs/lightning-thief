// GF_GiveCreatureDebuggerPage.h
#pragma once

#include "CoreMinimal.h"
#include "GF_ElementTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STileView.h"   // TSharedPtr<STileView<...>> member below
#include "Widgets/Input/SSpinBox.h"    // TSharedPtr<SSpinBox<int32>> member below
#include "Widgets/Input/SCheckBox.h"     // TSharedPtr<SCheckBox> member below
#include "Widgets/Input/STextComboBox.h" // TSharedPtr<STextComboBox> members below
#include "Brushes/SlateImageBrush.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_SkillDefinition.h"

/** One row in the Give-Creature species picker */
struct FGF_GiveCreatureEntry
{
    TWeakObjectPtr<UGF_CreatureSpeciesData> SpeciesData;
    FString         DisplayName;
    EGF_Element    PrimaryElement   = EGF_Element::None;
    EGF_Element    SecondaryElement = EGF_Element::None;

    /** Slate brush built from DisplayIcon1->GetSourceTexture(). Kept alive here. */
    TSharedPtr<FSlateImageBrush> IconBrush;
};

/** One entry in the held-item dropdown */
struct FGF_HeldItemOption
{
    FName   ItemName;
    FString DisplayName;
};

/**
 * Give Creature debug page.
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │  [Type filter bar]                                      │
 * │  [Creature species list]   "Voltkit is selected!"        │
 * │                                                         │
 * │  ┌─ Options ──────────────────────────────────────────┐ │
 * │  │  Level: [__]  Unique: [ ]  Held Item: [v]          │ │
 * │  │  Skill 1: [v]  Skill 2: [v]  Skill 3: [v]  Skill 4:[v]│ │
 * │  │  (leave empty to use level-up defaults)            │ │
 * │  └────────────────────────────────────────────────────┘ │
 * │                              [▶  Receive Creature]       │
 * └─────────────────────────────────────────────────────────┘
 */
class GAMMAFRAMEWORKBATTLE_API SGF_GiveCreatureDebuggerPage : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGF_GiveCreatureDebuggerPage) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // ── Data loading ─────────────────────────────────────────────────────────
    void LoadCreatureAssets();
    void LoadSkillAssets();
    void LoadHeldItemAssets();
    void ApplyTypeFilter();

    // ── UI builders ──────────────────────────────────────────────────────────
    TSharedRef<SWidget> BuildTypeFilterBar();
    TSharedRef<SWidget> BuildOptionPanel();
    TSharedRef<SWidget> BuildMoveOverrideRow(int32 SlotIndex);

    // ── List callbacks ───────────────────────────────────────────────────────
    TSharedRef<ITableRow> MakeCreatureTile(TSharedPtr<FGF_GiveCreatureEntry> Item,
                                          const TSharedRef<STableViewBase>& Owner);
    void OnCreatureSelected(TSharedPtr<FGF_GiveCreatureEntry> Item, ESelectInfo::Type);

    // ── Type filter ──────────────────────────────────────────────────────────
    void   OnTypeFilterClicked(EGF_Element Type);
    FSlateColor GetTypeButtonColor(EGF_Element Type) const;

    // ── Held item ────────────────────────────────────────────────────────────
    TSharedRef<SWidget>  MakeHeldItemWidget(TSharedPtr<FString> Item);
    void OnHeldItemChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type);

    // ── Skill combo helpers ───────────────────────────────────────────────────
    TSharedRef<SWidget> MakeSkillWidget(TSharedPtr<FString> Item);
    void OnSkillChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type, int32 Slot);

    // ── Receive button ───────────────────────────────────────────────────────
    FReply OnReceiveCreature();

    // ── Status / display text ─────────────────────────────────────────────────
    FText GetSelectionText() const;
    FText GetStatusText() const;

    // ── Shared helpers ────────────────────────────────────────────────────────
    static FLinearColor TypeToColor(EGF_Element Type);
    static FString      TypeToString(EGF_Element Type);
    UWorld* GetWorld() const;

private:
    // Creature list
    TArray<TSharedPtr<FGF_GiveCreatureEntry>>  AllCreature;
    TArray<TSharedPtr<FGF_GiveCreatureEntry>>  FilteredCreature;
    TSharedPtr<FGF_GiveCreatureEntry>           SelectedCreature;
    TSharedPtr<STileView<TSharedPtr<FGF_GiveCreatureEntry>>> CreatureListView;

    EGF_Element ActiveTypeFilter = EGF_Element::None;
    bool         bShowAll         = true;

    // Options
    int32  GiveLevel = 5;
    bool   bIsUnique  = false;
    TSharedPtr<SSpinBox<int32>> LevelSpinBox;
    TSharedPtr<SCheckBox>       UniqueCheckBox;

    // Held item
    TArray<TSharedPtr<FString>>   HeldItemDisplayOptions; // What shows in combo
    TArray<FName>                 HeldItemNames;           // Parallel array of FNames
    TSharedPtr<FString>           SelectedHeldItem;
    TSharedPtr<STextComboBox>     HeldItemComboBox;

    // Skill overrides (4 slots, nullptr = use defaults)
    static const int32 NUM_MOVE_SLOTS = 4;
    TArray<TSharedPtr<FString>>   SkillOptions;        // Display names
    TArray<FString>                MovePaths;          // Class path strings
    TSharedPtr<FString>           SelectedSkills[NUM_MOVE_SLOTS];
    TSharedPtr<STextComboBox>     SkillComboBoxes[NUM_MOVE_SLOTS];

    // Status message
    FString StatusMessage;
};
