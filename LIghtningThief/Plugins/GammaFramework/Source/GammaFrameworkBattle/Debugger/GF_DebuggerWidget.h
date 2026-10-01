// GF_DebuggerWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Root Slate widget that lives inside the GE Debugger SWindow.
 * Hosts a tab bar that switches between the four debug pages:
 *
 *   [Battle]  [Give Creature]  [Items / Player]  [Quest Tracker]  [Grid]
 *
 * Adding a new tab in the future:
 *   1. Add a static FName for the tab ID below.
 *   2. Add a button in BuildTabBar().
 *   3. Add a case in BuildPageContent().
 */
class GAMMAFRAMEWORKBATTLE_API SGF_DebuggerWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGF_DebuggerWidget)
    {
        _WorldContext = nullptr;
    }
        SLATE_ARGUMENT(UWorld*, WorldContext)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Global weak reference so pages can grab the world context */
    static TWeakPtr<SGF_DebuggerWidget> CurrentInstance;

    UWorld* GetWorldContext() const { return WorldContext.Get(); }

    // ── Tab IDs (accessible by pages if needed) ──────────────────────────────
    static const FName TabBattle;
    static const FName TabGiveCreature;
    static const FName TabItems;
    static const FName TabQuests;
    static const FName TabGrid;

private:
    void          OnTabChanged(FName NewTabId);
    TSharedRef<SWidget> BuildTabBar();
    TSharedRef<SWidget> BuildPageContent(FName TabId);
    TSharedRef<SWidget> MakeTabButton(const FText& Label, FName TabId);
    FReply              OnCloseClicked();
    FSlateColor         GetTabBgColor(FName TabId) const;
    FSlateColor         GetTabFgColor(FName TabId) const;

private:
    TWeakObjectPtr<UWorld>  WorldContext;
    FName                   ActiveTab;
    TSharedPtr<SBox>        ContentArea;
};
