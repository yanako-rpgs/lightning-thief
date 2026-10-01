// GF_BattleDebuggerPage.h
#pragma once

#include "CoreMinimal.h"
#include "GF_ElementTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STileView.h"   // TSharedPtr<STileView<...>> member below
#include "Widgets/Input/SSpinBox.h"    // TSharedPtr<SSpinBox<int32>> member below
#include "Widgets/Input/STextComboBox.h" // TSharedPtr<STextComboBox> members below
#include "Brushes/SlateImageBrush.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_TamerMaster.h"

// ─── Display entry structs ────────────────────────────────────────────────────

/** One row in the wild-battle Creature picker */
struct FGF_BattleCreatureEntry
{
    TWeakObjectPtr<UGF_CreatureSpeciesData> SpeciesData;
    FString         DisplayName;     // "001 - Budling"
    EGF_Element    PrimaryElement   = EGF_Element::None;
    EGF_Element    SecondaryElement = EGF_Element::None;

    /** Slate brush built from DisplayIcon1->GetSourceTexture(). Kept alive here. */
    TSharedPtr<FSlateImageBrush> IconBrush;
};

/** One row in the tamer picker */
struct FGF_TamerEntry
{
    TSubclassOf<AGF_TamerMaster> TamerClass;
    FString DisplayName;    // e.g. "BP_GF_TamerRichBoy"
};

/**
 * Battle debug page.
 *
 * ┌─────────────────────────────────────────────┐
 * │  WILD BATTLE                                │
 * │  [Type filter bar]                          │
 * │  [Creature list]   Level: [__]  Map: [v]     │
 * │                   [▶ Start Wild Battle]     │
 * ├─────────────────────────────────────────────┤
 * │  TAMER BATTLE                             │
 * │  [Tamer list]   Difficulty: [v]           │
 * │                   [▶ Start Tamer Battle]  │
 * └─────────────────────────────────────────────┘
 */
class GAMMAFRAMEWORKBATTLE_API SGF_BattleDebuggerPage : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGF_BattleDebuggerPage) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // ── Data loading ─────────────────────────────────────────────────────────
    void LoadCreatureAssets();
    void LoadMapAssets();
    void LoadSoundAssets();
    void LoadSkillAssets();
    void LoadTamerClasses();
    void ApplyTypeFilter();

    // ── Wild battle section ──────────────────────────────────────────────────
    TSharedRef<SWidget> BuildWildBattleSection();
    TSharedRef<SWidget> BuildTypeFilterBar();
    TSharedRef<SWidget> BuildMoveOverrideRow(int32 SlotIndex);
    TSharedRef<ITableRow> MakeCreatureTile(TSharedPtr<FGF_BattleCreatureEntry> Item,
                                         const TSharedRef<STableViewBase>& Owner);
    void     OnTypeFilterClicked(EGF_Element Type);
    void     OnCreatureSelected(TSharedPtr<FGF_BattleCreatureEntry> Item, ESelectInfo::Type SelectType);
    void     OnWildSkillChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type, int32 Slot);
    FReply   OnStartWildBattle();
    FText    GetWildStatusText() const;
    FSlateColor GetTypeButtonColor(EGF_Element Type) const;

    // ── Tamer battle section ───────────────────────────────────────────────
    TSharedRef<SWidget> BuildTamerBattleSection();
    TSharedRef<ITableRow> MakeTamerRow(TSharedPtr<FGF_TamerEntry> Item,
                                          const TSharedRef<STableViewBase>& Owner);
    void     OnTamerSelected(TSharedPtr<FGF_TamerEntry> Item, ESelectInfo::Type SelectType);
    FReply   OnStartTamerBattle();
    FReply   OnRefreshTamers();
    FText    GetTamerStatusText() const;
    TSharedRef<SWidget> MakeDifficultyWidget(TSharedPtr<FString> Item);
    void     OnDifficultyChanged(TSharedPtr<FString> NewSel, ESelectInfo::Type);

    // ── Helpers ──────────────────────────────────────────────────────────────
    static FLinearColor TypeToColor(EGF_Element Type);
    static FString      TypeToString(EGF_Element Type);
    UWorld* GetWorld() const;

private:
    // ── Wild battle state ─────────────────────────────────────────────────────
    TArray<TSharedPtr<FGF_BattleCreatureEntry>>  AllCreature;
    TArray<TSharedPtr<FGF_BattleCreatureEntry>>  FilteredCreature;
    TSharedPtr<FGF_BattleCreatureEntry>           SelectedCreature;
    TSharedPtr<STileView<TSharedPtr<FGF_BattleCreatureEntry>>> CreatureListView;

    EGF_Element  ActiveTypeFilter = EGF_Element::None; // None means "All"
    bool          bShowAll         = true;

    int32  WildLevel = 5;
    TSharedPtr<SSpinBox<int32>> LevelSpinBox;

    // Map selection
    TArray<TSharedPtr<FString>> MapOptions;   // display names
    TArray<FString>             MapPaths;     // package paths
    TSharedPtr<FString>         SelectedMapOption;
    TSharedPtr<STextComboBox>   MapComboBox;

    // Sound selection (OST_ assets only)
    TArray<TSharedPtr<FString>> SoundOptions;   // display names ("(none)" + OST_ assets)
    TArray<FString>             SoundPaths;     // soft object paths
    TSharedPtr<FString>         SelectedIntroSound;
    TSharedPtr<FString>         SelectedLoopSound;
    TSharedPtr<STextComboBox>   IntroSoundComboBox;
    TSharedPtr<STextComboBox>   LoopSoundComboBox;

    // ── Tamer battle state ──────────────────────────────────────────────────
    TArray<TSharedPtr<FGF_TamerEntry>>            TamerEntries;
    TSharedPtr<FGF_TamerEntry>                    SelectedTamer;
    TSharedPtr<SListView<TSharedPtr<FGF_TamerEntry>>> TamerListView;

    EGF_TamerAIDifficulty             SelectedDifficulty = EGF_TamerAIDifficulty::Smart;
    TArray<TSharedPtr<FString>>      DifficultyOptions;
    TSharedPtr<STextComboBox>        DifficultyComboBox;

    // Status text
    FString WildStatusMessage;
    FString TamerStatusMessage;

    // ── Wild move overrides ───────────────────────────────────────────────────
    static const int32 NUM_WILD_MOVE_SLOTS = 4;
    TArray<TSharedPtr<FString>>  SkillOptions;
    TArray<FString>              MovePaths;
    TSharedPtr<FString>          SelectedWildSkills[NUM_WILD_MOVE_SLOTS];
    TSharedPtr<STextComboBox>    WildSkillComboBoxes[NUM_WILD_MOVE_SLOTS];
};
