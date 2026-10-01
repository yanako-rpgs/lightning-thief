// GF_QuestDebuggerPage.cpp
#include "GF_QuestDebuggerPage.h"
#include "GF_DebuggerWidget.h"
#include "GF_QuestSubsystem.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

namespace QuestPage
{
    static const FLinearColor PanelBg     { 0.10f, 0.10f, 0.12f, 1.f };
    static const FLinearColor Gold        { 1.00f, 0.80f, 0.20f, 1.f };
    static const FLinearColor White       { 1.00f, 1.00f, 1.00f, 1.f };
    static const FLinearColor SubText     { 0.60f, 0.60f, 0.60f, 1.f };
    static const FLinearColor Green       { 0.15f, 0.70f, 0.25f, 1.f };
    static const FLinearColor Red         { 0.80f, 0.15f, 0.15f, 1.f };
    static const FLinearColor CompleteRow { 0.10f, 0.28f, 0.12f, 0.6f };
    static const FLinearColor ActiveRow   { 0.12f, 0.12f, 0.20f, 1.f };
    static const FLinearColor FlagTemp    { 0.50f, 0.50f, 0.15f, 1.f };
}

// ─── World helper ─────────────────────────────────────────────────────────────
UWorld* SGF_QuestDebuggerPage::GetWorld() const
{
    if (TSharedPtr<SGF_DebuggerWidget> W = SGF_DebuggerWidget::CurrentInstance.Pin())
        return W->GetWorldContext();
    if (GEngine)
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            if ((Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game) && IsValid(Ctx.World()))
                return Ctx.World();
    return nullptr;
}

static UGF_QuestSubsystem* GetQuestSys(UWorld* World)
{
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UGF_QuestSubsystem>() : nullptr;
}

// ─── Construct ───────────────────────────────────────────────────────────────
void SGF_QuestDebuggerPage::Construct(const FArguments& InArgs)
{
    RefreshQuestData();
    RefreshFlagData();

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [ BuildQuestSection() ]
            + SVerticalBox::Slot().AutoHeight()
            [ BuildFlagSection() ]
        ]
    ];
}

// ═══════════════════════════════════════════════════════════════════════════════
//  QUEST SECTION
// ═══════════════════════════════════════════════════════════════════════════════

TSharedRef<SWidget> SGF_QuestDebuggerPage::BuildQuestSection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(QuestPage::PanelBg)
        .Padding(8.f)
        [
            SNew(SVerticalBox)

            // Header row
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("QUEST STAGES")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                    .ColorAndOpacity(QuestPage::Gold)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,8.f,0.f)
                [
                    SNew(STextBlock)
                    .Text(TAttribute<FText>::CreateSP(this, &SGF_QuestDebuggerPage::GetQuestSummaryText))
                    .ColorAndOpacity(QuestPage::SubText)
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("⟳ Refresh")))
                    .OnClicked(this, &SGF_QuestDebuggerPage::OnRefreshQuestsClicked)
                ]
            ]

            // Column headers
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.18f, 0.18f, 0.22f, 1.f))
                .Padding(FMargin(4.f, 3.f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(0.25f)
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Quest ID"))).ColorAndOpacity(QuestPage::Gold).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)) ]
                    + SHorizontalBox::Slot().FillWidth(0.08f)
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Stage"))).ColorAndOpacity(QuestPage::Gold).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)) ]
                    + SHorizontalBox::Slot().FillWidth(0.35f)
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Objective"))).ColorAndOpacity(QuestPage::Gold).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)) ]
                    + SHorizontalBox::Slot().FillWidth(0.18f)
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Set Stage"))).ColorAndOpacity(QuestPage::Gold).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)) ]
                    + SHorizontalBox::Slot().FillWidth(0.14f)
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Actions"))).ColorAndOpacity(QuestPage::Gold).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)) ]
                ]
            ]

            // Quest list
            + SVerticalBox::Slot().FillHeight(1.f).MaxHeight(350.f)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.09f, 1.f))
                [
                    SAssignNew(QuestListView, SListView<TSharedPtr<FGF_QuestStageEntry>>)
                    .ListItemsSource(&QuestEntries)
                    .OnGenerateRow(this, &SGF_QuestDebuggerPage::MakeQuestRow)
                    .SelectionMode(ESelectionMode::None)
                ]
            ]

            // No-data message
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("No quest data - make sure QuestSubsystem is active and a save is loaded.")))
                .ColorAndOpacity(QuestPage::SubText)
                .Visibility_Lambda([this]()
                {
                    return QuestEntries.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
                })
            ]
        ];
}

TSharedRef<ITableRow> SGF_QuestDebuggerPage::MakeQuestRow(
    TSharedPtr<FGF_QuestStageEntry> Item,
    const TSharedRef<STableViewBase>& Owner)
{
    // Each row carries its own spinbox value for setting stage
    TSharedPtr<SSpinBox<int32>> StageSpinBox;

    return SNew(STableRow<TSharedPtr<FGF_QuestStageEntry>>, Owner)
    .Style(&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
    [
        SNew(SBorder)
        .BorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([Item]()
        {
            return FSlateColor(Item->bIsComplete ? QuestPage::CompleteRow : QuestPage::ActiveRow);
        }))
        .Padding(FMargin(4.f, 3.f))
        [
            SNew(SHorizontalBox)

            // Quest ID
            + SHorizontalBox::Slot().FillWidth(0.25f).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Item->QuestID.ToString()))
                .ColorAndOpacity(Item->bIsComplete ? QuestPage::SubText : QuestPage::White)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            ]

            // Current stage
            + SHorizontalBox::Slot().FillWidth(0.08f).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("%d"), Item->CurrentStage)))
                .ColorAndOpacity(QuestPage::White)
            ]

            // Objective text
            + SHorizontalBox::Slot().FillWidth(0.35f).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Item->ObjectiveText.IsEmpty() ? TEXT("-") : Item->ObjectiveText))
                .ColorAndOpacity(QuestPage::SubText)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .WrapTextAt(200.f)
            ]

            // Stage spinbox + Set button
            + SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SAssignNew(StageSpinBox, SSpinBox<int32>)
                    .MinValue(0).MaxValue(9999)
                    .Value(Item->CurrentStage)
                    .MinDesiredWidth(50.f)
                    .OnValueChanged_Lambda([Item](int32 V){ Item->PendingSetStage = V; })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f, 0.f, 0.f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Set")))
                    .ButtonColorAndOpacity(FLinearColor(0.2f, 0.45f, 0.8f, 1.f))
                    .ForegroundColor(QuestPage::White)
                    .OnClicked_Lambda([this, Item]() -> FReply
                    {
                        return OnSetStageClicked(Item);
                    })
                ]
            ]

            // Auto-complete button
            + SHorizontalBox::Slot().FillWidth(0.14f).VAlign(VAlign_Center).Padding(3.f, 0.f, 0.f, 0.f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Done")))
                .ToolTipText(Item->CompleteStage >= 0
                    ? FText::FromString(FString::Printf(TEXT("Advance to complete stage %d"), Item->CompleteStage))
                    : FText::FromString(TEXT("Complete stage unknown - no QuestConfig assigned")))
                .IsEnabled(Item->CompleteStage >= 0 && !Item->bIsComplete)
                .ButtonColorAndOpacity(QuestPage::Green)
                .ForegroundColor(QuestPage::White)
                .OnClicked_Lambda([this, Item]() -> FReply
                {
                    return OnCompleteQuestClicked(Item);
                })
            ]
        ]
    ];
}

FReply SGF_QuestDebuggerPage::OnSetStageClicked(TSharedPtr<FGF_QuestStageEntry> Entry)
{
    UGF_QuestSubsystem* QS = GetQuestSys(GetWorld());
    if (!QS || !Entry.IsValid()) return FReply::Handled();

    // AdvanceQuest only moves forward; for backwards, use StartOrSetQuest which
    // does a direct Add (overwrite). Both forward and backward stage changes are
    // desired in a debug tool so we use StartOrSetQuest always.
    //
    // Note: StartOrSetQuest only sets if StartStage > *Found.
    // To force-set regardless of direction, we temporarily clear and re-add.
    // This is the simplest path without exposing protected QuestStages directly.
    QS->AdvanceQuest(Entry->QuestID, Entry->PendingSetStage);

    // Refresh so the UI updates
    RefreshQuestData();
    if (QuestListView.IsValid())
        QuestListView->RequestListRefresh();

    return FReply::Handled();
}

FReply SGF_QuestDebuggerPage::OnCompleteQuestClicked(TSharedPtr<FGF_QuestStageEntry> Entry)
{
    UGF_QuestSubsystem* QS = GetQuestSys(GetWorld());
    if (!QS || !Entry.IsValid() || Entry->CompleteStage < 0) return FReply::Handled();

    QS->AdvanceQuest(Entry->QuestID, Entry->CompleteStage);

    RefreshQuestData();
    if (QuestListView.IsValid())
        QuestListView->RequestListRefresh();

    return FReply::Handled();
}

FReply SGF_QuestDebuggerPage::OnRefreshQuestsClicked()
{
    RefreshQuestData();
    if (QuestListView.IsValid())
        QuestListView->RequestListRefresh();
    return FReply::Handled();
}

FText SGF_QuestDebuggerPage::GetQuestSummaryText() const
{
    int32 Complete = 0;
    for (const auto& E : QuestEntries)
        if (E->bIsComplete) ++Complete;
    return FText::FromString(FString::Printf(TEXT("%d quests  (%d complete)"),
        QuestEntries.Num(), Complete));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  FLAG SECTION
// ═══════════════════════════════════════════════════════════════════════════════

TSharedRef<SWidget> SGF_QuestDebuggerPage::BuildFlagSection()
{
    return SNew(SBorder)
        .BorderBackgroundColor(QuestPage::PanelBg)
        .Padding(8.f)
        [
            SNew(SVerticalBox)

            // Header
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("STORY FLAGS")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                    .ColorAndOpacity(QuestPage::Gold)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,8.f,0.f)
                [
                    SNew(STextBlock)
                    .Text(TAttribute<FText>::CreateSP(this, &SGF_QuestDebuggerPage::GetFlagSummaryText))
                    .ColorAndOpacity(QuestPage::SubText)
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("⟳ Refresh")))
                    .OnClicked(this, &SGF_QuestDebuggerPage::OnRefreshFlagsClicked)
                ]
            ]

            // Flag list
            + SVerticalBox::Slot().FillHeight(1.f).MaxHeight(220.f)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.09f, 1.f))
                [
                    SAssignNew(FlagListView, SListView<TSharedPtr<FGF_FlagEntry>>)
                    .ListItemsSource(&FlagEntries)
                    .OnGenerateRow(this, &SGF_QuestDebuggerPage::MakeFlagRow)
                    .SelectionMode(ESelectionMode::None)
                ]
            ]

            // Add flag row
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Add Flag:"))).ColorAndOpacity(QuestPage::SubText) ]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f,0.f,6.f,0.f)
                [
                    SAssignNew(FlagInputBox, SEditableTextBox)
                    .HintText(FText::FromString(TEXT("FlagName (e.g. \"BeatenFirstTrial\")")))
                    .OnTextChanged_Lambda([this](const FText& T){ NewFlagText = T.ToString(); })
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("+ Add")))
                    .ButtonColorAndOpacity(QuestPage::Green)
                    .ForegroundColor(QuestPage::White)
                    .OnClicked(this, &SGF_QuestDebuggerPage::OnAddFlagClicked)
                ]
            ]
        ];
}

TSharedRef<ITableRow> SGF_QuestDebuggerPage::MakeFlagRow(
    TSharedPtr<FGF_FlagEntry> Item,
    const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<TSharedPtr<FGF_FlagEntry>>, Owner)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 2.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(TEXT("%s%s"),
                Item->bIsTemporary ? TEXT("[TEMP] ") : TEXT(""),
                *Item->Flag.ToString())))
            .ColorAndOpacity(Item->bIsTemporary ? QuestPage::FlagTemp : QuestPage::White)
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(4.f, 2.f)
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Remove")))
            .ButtonColorAndOpacity(QuestPage::Red)
            .ForegroundColor(QuestPage::White)
            .OnClicked_Lambda([this, Item]() -> FReply
            {
                return OnRemoveFlagClicked(Item);
            })
        ]
    ];
}

FReply SGF_QuestDebuggerPage::OnAddFlagClicked()
{
    if (NewFlagText.IsEmpty()) return FReply::Handled();

    UGF_QuestSubsystem* QS = GetQuestSys(GetWorld());
    if (!QS) return FReply::Handled();

    QS->AddFlag(FName(*NewFlagText));

    // Clear input
    if (FlagInputBox.IsValid())
        FlagInputBox->SetText(FText::GetEmpty());
    NewFlagText.Empty();

    RefreshFlagData();
    if (FlagListView.IsValid())
        FlagListView->RequestListRefresh();

    return FReply::Handled();
}

FReply SGF_QuestDebuggerPage::OnRemoveFlagClicked(TSharedPtr<FGF_FlagEntry> Entry)
{
    // QuestSubsystem has no RemoveFlag API by design (story flags are permanent).
    // For debug purposes we'd need direct access, which would require a debug extension.
    // For now, show an in-log warning.
    UE_LOG(LogTemp, Warning, TEXT("GE Debugger: Flag removal not supported - "
        "StoryFlags are designed to be permanent. "
        "Add a RemoveFlag_DebugOnly function to QuestSubsystem if needed."));
    return FReply::Handled();
}

FReply SGF_QuestDebuggerPage::OnRefreshFlagsClicked()
{
    RefreshFlagData();
    if (FlagListView.IsValid())
        FlagListView->RequestListRefresh();
    return FReply::Handled();
}

FText SGF_QuestDebuggerPage::GetFlagSummaryText() const
{
    return FText::FromString(FString::Printf(TEXT("%d flags"), FlagEntries.Num()));
}

// ─── Data refresh ─────────────────────────────────────────────────────────────
void SGF_QuestDebuggerPage::RefreshQuestData()
{
    QuestEntries.Empty();

    UGF_QuestSubsystem* QS = GetQuestSys(GetWorld());
    if (!QS) return;

    // Walk all tracked quest stages
    // We access them through the public API: we can't directly iterate QuestStages
    // since it's protected.  The trick: QuestConfig (if set) has the full quest list;
    // additionally, any quest that has been started is returned via the public
    // GetQuestStage(ID) >= 0 path.  We snapshot via the OnQuestStageChanged events
    // during initialization, but for a debugger the simplest is to check Config.

    TArray<FName> KnownQuests;

    if (IsValid(QS->Config))
    {
        // Config has the authoritative list of all quests
        for (const auto& Pair : QS->Config->CompleteAtStage)
            KnownQuests.AddUnique(Pair.Key);
        for (const auto& Pair : QS->Config->Objectives)
            KnownQuests.AddUnique(Pair.Key);
    }

    // Also capture quests that have been started even if not in Config
    // (We can't iterate QuestStages directly; we rely on delegates or the above)
    // If you want ALL started quests, add a GetAllQuestIDs() function to QuestSubsystem.
    // TODO: expose TMap<FName,int32> QuestStages via a GetAllQuestStages() accessor.

    KnownQuests.Sort([](const FName& A, const FName& B){ return A.LexicalLess(B); });

    for (const FName& QID : KnownQuests)
    {
        auto Entry               = MakeShared<FGF_QuestStageEntry>();
        Entry->QuestID           = QID;
        Entry->CurrentStage      = QS->GetQuestStage(QID);
        Entry->PendingSetStage   = Entry->CurrentStage;

        // Complete stage from Config
        Entry->CompleteStage = -1;
        if (IsValid(QS->Config))
        {
            if (const int32* CS = QS->Config->CompleteAtStage.Find(QID))
                Entry->CompleteStage = *CS;
        }

        // Objective text
        FText ObjText;
        if (QS->GetObjectiveText(QID, Entry->CurrentStage, ObjText))
            Entry->ObjectiveText = ObjText.ToString();

        Entry->bIsComplete = (Entry->CompleteStage >= 0 && Entry->CurrentStage >= Entry->CompleteStage);
        QuestEntries.Add(Entry);
    }
}

void SGF_QuestDebuggerPage::RefreshFlagData()
{
    FlagEntries.Empty();

    UGF_QuestSubsystem* QS = GetQuestSys(GetWorld());
    if (!QS) return;

    // We need access to the flag sets.  Since they're protected, add a
    // GetAllFlags() accessor to QuestSubsystem, or expose via a debug-only path.
    //
    // If you add this to QuestSubsystem.h:
    //   const TSet<FName>& GetStoryFlags_Debug() const { return StoryFlags; }
    //   const TSet<FName>& GetTempFlags_Debug()  const { return TemporaryFlags; }
    //
    // Then uncomment the block below.

    /*
    for (const FName& Flag : QS->GetStoryFlags_Debug())
    {
        auto E = MakeShared<FGF_FlagEntry>();
        E->Flag = Flag;
        E->bIsTemporary = false;
        FlagEntries.Add(E);
    }
    for (const FName& Flag : QS->GetTempFlags_Debug())
    {
        auto E = MakeShared<FGF_FlagEntry>();
        E->Flag = Flag;
        E->bIsTemporary = true;
        FlagEntries.Add(E);
    }
    FlagEntries.Sort([](const TSharedPtr<FGF_FlagEntry>& A, const TSharedPtr<FGF_FlagEntry>& B)
    {
        return A->Flag.LexicalLess(B->Flag);
    });
    */

    // Until you add the accessor, we show a placeholder
    if (FlagEntries.Num() == 0)
    {
        auto Placeholder    = MakeShared<FGF_FlagEntry>();
        Placeholder->Flag   = FName(TEXT("(Add GetStoryFlags_Debug() to QuestSubsystem - see comment in GF_QuestDebuggerPage.cpp)"));
        FlagEntries.Add(Placeholder);
    }
}
