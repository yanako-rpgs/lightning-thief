#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetRegistry/AssetData.h"
#include "Dialogue/GF_DialogueTypes.h"   // FGF_DialogueNode, FGF_DialogueEvent, etc.

class UGF_DialogueAsset;

/**
 * SGF_DialoguePreviewWidget
 *
 * Slate widget for the Gamma Framework Dialogue Editor window.
 * Accessible via: GE menu → Dialogue Editor
 *
 * File watching and reimport state live in FGF_GammaFrameworkEditorModule
 * so they persist even when this window is closed.
 *
 * Features:
 *  - Source file row: shows watched file, Browse and Reimport buttons
 *  - Asset picker: lists all UGF_DialogueAsset objects in the project
 *  - Nodes tab: all nodes in a scrollable list (index, type, speaker, text, next)
 *  - Simulate tab: walks through the dialogue interactively
 */
class SGF_DialoguePreviewWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGF_DialoguePreviewWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Called by the module when the source file or a reimport completes. */
    void NotifySourceFileChanged();

private:

    // ── Asset picker ──────────────────────────────────────────────────────────

    TArray<TSharedPtr<FAssetData>>    AssetList;
    TSharedPtr<FAssetData>            SelectedAsset;
    TWeakObjectPtr<UGF_DialogueAsset> ActiveAsset;
    TSharedPtr<SComboBox<TSharedPtr<FAssetData>>> AssetCombo;

    void    RefreshAssetList();
    void    OnAssetSelected(TSharedPtr<FAssetData> Item, ESelectInfo::Type Reason);
    TSharedRef<SWidget> MakeAssetComboRow(TSharedPtr<FAssetData> Item);
    FText   GetSelectedAssetLabel() const;
    FReply  OnRefreshClicked();

    // ── Source file strip (state owned by module) ─────────────────────────────

    FReply  OnBrowseSourceClicked();
    FReply  OnReimportAllClicked();
    FText   GetSourceFileLabel() const;
    FSlateColor GetWatchIndicatorColor() const;

    // ── Tab system ────────────────────────────────────────────────────────────

    static const FName TabScript;
    static const FName TabNodes;
    static const FName TabSimulate;

    FName               ActiveTab;
    TSharedPtr<SBox>    TabContent;

    FReply              OnTabClicked(FName Tab);
    FSlateColor         GetTabBgColor(FName Tab) const;
    FSlateColor         GetTabFgColor(FName Tab) const;
    TSharedRef<SWidget> BuildTabContent(FName Tab);

    // ── Script tab (full screenplay view) ────────────────────────────────────

    TSharedRef<SWidget> BuildScriptTab();
    TSharedRef<SWidget> BuildNodeCard(const FGF_DialogueNode& Node, int32 Index);

    // ── Nodes tab ─────────────────────────────────────────────────────────────

    struct FGF_NodeRow
    {
        int32        Index;
        FString      TypeLabel;
        FLinearColor TypeColor;
        FString      Speaker;
        FString      TextPreview;
        FString      NextLabel;
    };

    TArray<TSharedPtr<FGF_NodeRow>>                    NodeRows;
    TSharedPtr<SListView<TSharedPtr<FGF_NodeRow>>>     NodeListView;

    void                        RebuildNodeRows();
    TSharedRef<SWidget>         BuildNodesTab();
    TSharedRef<ITableRow>       MakeNodeRow(TSharedPtr<FGF_NodeRow> Item,
                                            const TSharedRef<STableViewBase>& Owner);

    // ── Simulate tab ──────────────────────────────────────────────────────────

    int32                       SimNodeIndex = 0;
    TSharedPtr<STextBlock>      SimSpeakerText;
    TSharedPtr<STextBlock>      SimBodyText;
    TSharedPtr<STextBlock>      SimStatusText;
    TSharedPtr<SVerticalBox>    SimChoicesBox;

    TSharedRef<SWidget>  BuildSimulateTab();
    void                 SimReset();
    void                 SimRefresh();
    FReply               OnSimAdvance();
    FReply               OnSimChoice(int32 ChoiceIdx);
};
