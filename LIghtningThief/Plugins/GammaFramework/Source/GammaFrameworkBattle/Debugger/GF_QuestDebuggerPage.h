// GF_QuestDebuggerPage.h
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/** One row in the Quest Stages table */
struct FGF_QuestStageEntry
{
    FName  QuestID;
    int32  CurrentStage  = 0;
    int32  CompleteStage = -1;  // -1 = unknown / no config
    FString ObjectiveText;
    bool   bIsComplete   = false;

    int32 PendingSetStage = 0;  // what the spinbox currently shows
};

/** One row in the Story Flags list */
struct FGF_FlagEntry
{
    FName   Flag;
    bool    bIsTemporary = false;
};

/**
 * Quest Tracker debug page.
 *
 * ┌─────────────────────────────────────────────────────────────┐
 * │  QUEST STAGES                         [⟳ Refresh]          │
 * │  ┌──────────────────────────────────────────────────────┐  │
 * │  │  QuestID       Stage  Objective  [Set:__] [✔ Done]  │  │
 * │  │  ...                                                 │  │
 * │  └──────────────────────────────────────────────────────┘  │
 * ├─────────────────────────────────────────────────────────────┤
 * │  STORY FLAGS                          [⟳ Refresh]          │
 * │  [Flag list]                                                │
 * │  Add flag: [_____________]  [+ Add]                        │
 * └─────────────────────────────────────────────────────────────┘
 */
class GAMMAFRAMEWORKBATTLE_API SGF_QuestDebuggerPage : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGF_QuestDebuggerPage) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // ── Data refresh ─────────────────────────────────────────────────────────
    void RefreshQuestData();
    void RefreshFlagData();

    // ── UI builders ──────────────────────────────────────────────────────────
    TSharedRef<SWidget> BuildQuestSection();
    TSharedRef<SWidget> BuildFlagSection();

    // ── Quest list ────────────────────────────────────────────────────────────
    TSharedRef<ITableRow> MakeQuestRow(TSharedPtr<FGF_QuestStageEntry> Item,
                                        const TSharedRef<STableViewBase>& Owner);
    FReply OnSetStageClicked(TSharedPtr<FGF_QuestStageEntry> Entry);
    FReply OnCompleteQuestClicked(TSharedPtr<FGF_QuestStageEntry> Entry);
    FSlateColor GetQuestRowColor(TSharedPtr<FGF_QuestStageEntry> Entry) const;

    // ── Flag list ─────────────────────────────────────────────────────────────
    TSharedRef<ITableRow> MakeFlagRow(TSharedPtr<FGF_FlagEntry> Item,
                                       const TSharedRef<STableViewBase>& Owner);
    FReply OnAddFlagClicked();
    FReply OnRemoveFlagClicked(TSharedPtr<FGF_FlagEntry> Entry);

    // ── Refresh buttons ───────────────────────────────────────────────────────
    FReply OnRefreshQuestsClicked();
    FReply OnRefreshFlagsClicked();

    // ── Summary text ──────────────────────────────────────────────────────────
    FText GetQuestSummaryText() const;
    FText GetFlagSummaryText() const;

    UWorld* GetWorld() const;

private:
    // Quests
    TArray<TSharedPtr<FGF_QuestStageEntry>>  QuestEntries;
    TSharedPtr<SListView<TSharedPtr<FGF_QuestStageEntry>>> QuestListView;

    // Flags
    TArray<TSharedPtr<FGF_FlagEntry>>  FlagEntries;
    TSharedPtr<SListView<TSharedPtr<FGF_FlagEntry>>> FlagListView;

    // Add-flag input
    FString NewFlagText;
    TSharedPtr<SEditableTextBox> FlagInputBox;

    FString StatusMessage;
};
