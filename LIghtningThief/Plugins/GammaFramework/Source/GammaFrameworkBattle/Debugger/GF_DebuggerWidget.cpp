// GF_DebuggerWidget.cpp
#include "GF_DebuggerWidget.h"
#include "GF_BattleDebuggerPage.h"
#include "GF_GiveCreatureDebuggerPage.h"
#include "GF_ItemDebuggerPage.h"
#include "GF_QuestDebuggerPage.h"
#include "GF_GridDebuggerPage.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

// ─── Statics ─────────────────────────────────────────────────────────────────
TWeakPtr<SGF_DebuggerWidget> SGF_DebuggerWidget::CurrentInstance;

const FName SGF_DebuggerWidget::TabBattle       = TEXT("Battle");
const FName SGF_DebuggerWidget::TabGiveCreature  = TEXT("GiveCreature");
const FName SGF_DebuggerWidget::TabItems        = TEXT("Items");
const FName SGF_DebuggerWidget::TabQuests       = TEXT("Quests");
const FName SGF_DebuggerWidget::TabGrid         = TEXT("Grid");

// ─── Common style helpers ────────────────────────────────────────────────────
namespace GEDebugStyle
{
    static FLinearColor BgDark       { 0.08f, 0.08f, 0.10f, 1.f };
    static FLinearColor BgMid        { 0.12f, 0.12f, 0.14f, 1.f };
    static FLinearColor TabActive    { 0.18f, 0.52f, 0.90f, 1.f };
    static FLinearColor TabInactive  { 0.14f, 0.14f, 0.16f, 1.f };
    static FLinearColor Gold         { 1.00f, 0.80f, 0.20f, 1.f };
    static FLinearColor White        { 1.00f, 1.00f, 1.00f, 1.f };
    static FLinearColor SubText      { 0.65f, 0.65f, 0.65f, 1.f };
}

// ─── Construct ───────────────────────────────────────────────────────────────
void SGF_DebuggerWidget::Construct(const FArguments& InArgs)
{
    WorldContext    = InArgs._WorldContext;
    ActiveTab       = TabBattle;
    CurrentInstance = SharedThis(this);

    ChildSlot
    [
        SNew(SVerticalBox)

        // ── Header / tab bar ─────────────────────────────────────────────────
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SBorder)
            .BorderBackgroundColor(GEDebugStyle::BgDark)
            .Padding(FMargin(8.f, 5.f))
            [
                SNew(SHorizontalBox)

                // Title
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.f, 0.f, 16.f, 0.f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("GE Debugger")))
                    .ColorAndOpacity(GEDebugStyle::Gold)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                ]

                // Tab buttons
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .VAlign(VAlign_Center)
                [
                    BuildTabBar()
                ]

                // Close button
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(8.f, 0.f, 0.f, 0.f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("")))
                    .ButtonColorAndOpacity(FLinearColor(0.6f, 0.1f, 0.1f, 1.f))
                    .ForegroundColor(GEDebugStyle::White)
                    .OnClicked(this, &SGF_DebuggerWidget::OnCloseClicked)
                ]
            ]
        ]

        // ── Page content area ────────────────────────────────────────────────
        + SVerticalBox::Slot()
        .FillHeight(1.f)
        [
            SNew(SBorder)
            .BorderBackgroundColor(GEDebugStyle::BgMid)
            .Padding(0.f)
            [
                SAssignNew(ContentArea, SBox)
                [
                    BuildPageContent(ActiveTab)
                ]
            ]
        ]
    ];
}

// ─── Tab bar ─────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SGF_DebuggerWidget::BuildTabBar()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
        [ MakeTabButton(FText::FromString(TEXT("Battle")),       TabBattle)      ]
        + SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
        [ MakeTabButton(FText::FromString(TEXT("Give Creature")),  TabGiveCreature) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
        [ MakeTabButton(FText::FromString(TEXT("Items / Player")), TabItems)      ]
        + SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
        [ MakeTabButton(FText::FromString(TEXT("Quests")),        TabQuests)      ]
        + SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
        [ MakeTabButton(FText::FromString(TEXT("Grid")),          TabGrid)        ];
}

TSharedRef<SWidget> SGF_DebuggerWidget::MakeTabButton(const FText& Label, FName TabId)
{
    return SNew(SButton)
        .Text(Label)
        .ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &SGF_DebuggerWidget::GetTabBgColor, TabId))
        .ForegroundColor(TAttribute<FSlateColor>::CreateSP(this, &SGF_DebuggerWidget::GetTabFgColor, TabId))
        .OnClicked_Lambda([this, TabId]() -> FReply
        {
            OnTabChanged(TabId);
            return FReply::Handled();
        });
}

FSlateColor SGF_DebuggerWidget::GetTabBgColor(FName TabId) const
{
    return (ActiveTab == TabId) ? FSlateColor(GEDebugStyle::TabActive) : FSlateColor(GEDebugStyle::TabInactive);
}

FSlateColor SGF_DebuggerWidget::GetTabFgColor(FName TabId) const
{
    return (ActiveTab == TabId) ? FSlateColor(GEDebugStyle::White) : FSlateColor(GEDebugStyle::SubText);
}

void SGF_DebuggerWidget::OnTabChanged(FName NewTabId)
{
    if (ActiveTab == NewTabId) return;
    ActiveTab = NewTabId;
    if (ContentArea.IsValid())
        ContentArea->SetContent(BuildPageContent(ActiveTab));
}

// ─── Page factory ─────────────────────────────────────────────────────────────
TSharedRef<SWidget> SGF_DebuggerWidget::BuildPageContent(FName TabId)
{
    if (TabId == TabBattle)       return SNew(SGF_BattleDebuggerPage);
    if (TabId == TabGiveCreature)  return SNew(SGF_GiveCreatureDebuggerPage);
    if (TabId == TabItems)        return SNew(SGF_ItemDebuggerPage);
    if (TabId == TabQuests)       return SNew(SGF_QuestDebuggerPage);
    if (TabId == TabGrid)         return SNew(SGF_GridDebuggerPage);

    return SNew(STextBlock)
        .Text(FText::FromString(TEXT("Unknown tab")))
        .ColorAndOpacity(FLinearColor::Red);
}

// ─── Close button ─────────────────────────────────────────────────────────────
FReply SGF_DebuggerWidget::OnCloseClicked()
{
    TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
    if (ParentWindow.IsValid())
        ParentWindow->RequestDestroyWindow();
    return FReply::Handled();
}
