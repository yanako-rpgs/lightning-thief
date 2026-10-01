#include "SGF_DialoguePreviewWidget.h"
#include "GammaFrameworkEditor.h"          // FGF_GammaFrameworkEditorModule - source file state
#include "Dialogue/GF_DialogueAsset.h"
#include "Dialogue/GF_DialogueTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Styling/AppStyle.h"

// ─── Tab name constants ───────────────────────────────────────────────────────

const FName SGF_DialoguePreviewWidget::TabScript   = "Script";
const FName SGF_DialoguePreviewWidget::TabNodes    = "Nodes";
const FName SGF_DialoguePreviewWidget::TabSimulate = "Simulate";

// ─── Colors ───────────────────────────────────────────────────────────────────

namespace GEDialogueStyle
{
    static const FLinearColor BgDark       { 0.05f, 0.05f, 0.05f, 1.f };
    static const FLinearColor BgMid        { 0.08f, 0.08f, 0.08f, 1.f };
    static const FLinearColor BgPanel      { 0.10f, 0.10f, 0.10f, 1.f };
    static const FLinearColor TabActive    { 0.18f, 0.52f, 0.90f, 1.f };
    static const FLinearColor TabInactive  { 0.15f, 0.15f, 0.15f, 1.f };
    static const FLinearColor TextWhite    { 1.00f, 1.00f, 1.00f, 1.f };
    static const FLinearColor TextGray     { 0.55f, 0.55f, 0.55f, 1.f };
    static const FLinearColor TypeMessage  { 0.30f, 0.80f, 0.40f, 1.f }; // green
    static const FLinearColor TypeEvent    { 0.95f, 0.60f, 0.20f, 1.f }; // orange
    static const FLinearColor TypeChoice   { 0.70f, 0.40f, 0.95f, 1.f }; // purple
    static const FLinearColor TypeHide     { 0.50f, 0.50f, 0.50f, 1.f }; // gray
    static const FLinearColor BoxBorder    { 0.20f, 0.20f, 0.20f, 1.f };
    static const FLinearColor Accent       { 0.18f, 0.52f, 0.90f, 1.f };
    static const FLinearColor ButtonBg     { 0.20f, 0.20f, 0.20f, 1.f };
    static const FLinearColor ChoiceBg     { 0.12f, 0.24f, 0.40f, 1.f };
    static const FLinearColor DotActive    { 0.20f, 0.85f, 0.30f, 1.f }; // green = watching
    static const FLinearColor DotInactive  { 0.30f, 0.30f, 0.30f, 1.f }; // gray  = no file
}

// ─── Construct ────────────────────────────────────────────────────────────────

void SGF_DialoguePreviewWidget::Construct(const FArguments& InArgs)
{
    ActiveTab = TabScript;

    RefreshAssetList();

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("NoBrush"))
        .Padding(0)
        [
            SNew(SVerticalBox)

            // ── Source file / reimport row ─────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8, 8, 8, 4)
            [
                SNew(SHorizontalBox)

                // Watch indicator: small colored square (avoids Unicode font issues)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0, 0, 6, 0)
                [
                    SNew(SBox)
                    .WidthOverride(10)
                    .HeightOverride(10)
                    .ToolTipText(FText::FromString(TEXT("Green = file watcher active")))
                    [
                        SNew(SBorder)
                        .BorderBackgroundColor(this, &SGF_DialoguePreviewWidget::GetWatchIndicatorColor)
                        .Padding(0)
                    ]
                ]

                // Source file name label
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(this, &SGF_DialoguePreviewWidget::GetSourceFileLabel)
                    .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
                ]

                // Browse button
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(6, 0, 4, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Browse...")))
                    .OnClicked(this, &SGF_DialoguePreviewWidget::OnBrowseSourceClicked)
                    .ToolTipText(FText::FromString(TEXT("Select the master .gfdlg file to watch and reimport from")))
                ]

                // Reimport All button
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Reimport All")))
                    .OnClicked(this, &SGF_DialoguePreviewWidget::OnReimportAllClicked)
                    .ButtonColorAndOpacity(FSlateColor(GEDialogueStyle::Accent))
                    .ForegroundColor(FLinearColor::White)
                    .ToolTipText(FText::FromString(TEXT("Reimport all @dialogue sections from the source file")))
                    .IsEnabled_Lambda([]() { return FGF_GammaFrameworkEditorModule::CanReimportAll(); })
                ]
            ]

            // ── Asset picker row ───────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8, 0, 8, 4)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0, 0, 8, 0)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Asset:")))
                    .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
                ]

                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .VAlign(VAlign_Center)
                [
                    SAssignNew(AssetCombo, SComboBox<TSharedPtr<FAssetData>>)
                    .OptionsSource(&AssetList)
                    .OnSelectionChanged(this, &SGF_DialoguePreviewWidget::OnAssetSelected)
                    .OnGenerateWidget(this, &SGF_DialoguePreviewWidget::MakeAssetComboRow)
                    [
                        SNew(STextBlock)
                        .Text(this, &SGF_DialoguePreviewWidget::GetSelectedAssetLabel)
                        .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextWhite))
                    ]
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(8, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Refresh")))
                    .OnClicked(this, &SGF_DialoguePreviewWidget::OnRefreshClicked)
                    .ToolTipText(FText::FromString(TEXT("Reload asset list from AssetRegistry")))
                ]
            ]

            // ── Tab bar ────────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8, 4, 8, 0)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Script")))
                    .OnClicked(this, &SGF_DialoguePreviewWidget::OnTabClicked, TabScript)
                    .ButtonColorAndOpacity(this, &SGF_DialoguePreviewWidget::GetTabBgColor, TabScript)
                    .ForegroundColor(this, &SGF_DialoguePreviewWidget::GetTabFgColor, TabScript)
                    .ToolTipText(FText::FromString(TEXT("Full screenplay view - see all nodes and branches at once")))
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(4, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Nodes")))
                    .OnClicked(this, &SGF_DialoguePreviewWidget::OnTabClicked, TabNodes)
                    .ButtonColorAndOpacity(this, &SGF_DialoguePreviewWidget::GetTabBgColor, TabNodes)
                    .ForegroundColor(this, &SGF_DialoguePreviewWidget::GetTabFgColor, TabNodes)
                    .ToolTipText(FText::FromString(TEXT("Table view of all nodes with raw indices")))
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(4, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Simulate")))
                    .OnClicked(this, &SGF_DialoguePreviewWidget::OnTabClicked, TabSimulate)
                    .ButtonColorAndOpacity(this, &SGF_DialoguePreviewWidget::GetTabBgColor, TabSimulate)
                    .ForegroundColor(this, &SGF_DialoguePreviewWidget::GetTabFgColor, TabSimulate)
                    .ToolTipText(FText::FromString(TEXT("Step through dialogue interactively")))
                ]
            ]

            // ── Separator ──────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 4, 0, 0)
            [
                SNew(SSeparator)
                .Orientation(Orient_Horizontal)
            ]

            // ── Tab content area ───────────────────────────────────────────
            + SVerticalBox::Slot()
            .FillHeight(1.f)
            [
                SAssignNew(TabContent, SBox)
                [
                    BuildTabContent(ActiveTab)
                ]
            ]
        ]
    ];
}

// ─── Module-delegating methods ────────────────────────────────────────────────

void SGF_DialoguePreviewWidget::NotifySourceFileChanged()
{
    // Rebuild the active tab content since assets may have been reimported
    if (TabContent.IsValid())
    {
        TabContent->SetContent(BuildTabContent(ActiveTab));
    }

    RebuildNodeRows();
    if (NodeListView.IsValid())
    {
        NodeListView->RequestListRefresh();
    }
}

FReply SGF_DialoguePreviewWidget::OnBrowseSourceClicked()
{
    FGF_GammaFrameworkEditorModule::BrowseAndSetSourceFile();
    return FReply::Handled();
}

FReply SGF_DialoguePreviewWidget::OnReimportAllClicked()
{
    FGF_GammaFrameworkEditorModule::ReimportAllDialogue();
    return FReply::Handled();
}

FText SGF_DialoguePreviewWidget::GetSourceFileLabel() const
{
    const FString& Path = FGF_GammaFrameworkEditorModule::GetWatchedSourceFile();
    if (Path.IsEmpty())
    {
        return FText::FromString(TEXT("No source file - use GE menu or Browse to set one"));
    }
    // Show filename + last reimport status
    const FString& Status = FGF_GammaFrameworkEditorModule::GetLastReimportStatus();
    if (!Status.IsEmpty())
    {
        return FText::FromString(FString::Printf(TEXT("%s  |  %s"),
            *FPaths::GetCleanFilename(Path), *Status));
    }
    return FText::FromString(FPaths::GetCleanFilename(Path));
}

FSlateColor SGF_DialoguePreviewWidget::GetWatchIndicatorColor() const
{
    return FGF_GammaFrameworkEditorModule::GetWatchedSourceFile().IsEmpty()
        ? FSlateColor(GEDialogueStyle::DotInactive)
        : FSlateColor(GEDialogueStyle::DotActive);
}

// ─── Asset picker ─────────────────────────────────────────────────────────────

void SGF_DialoguePreviewWidget::RefreshAssetList()
{
    AssetList.Empty();

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Found;
    ARM.Get().GetAssetsByClass(UGF_DialogueAsset::StaticClass()->GetClassPathName(), Found);

    for (const FAssetData& AD : Found)
    {
        AssetList.Add(MakeShared<FAssetData>(AD));
    }

    if (AssetCombo.IsValid())
    {
        AssetCombo->RefreshOptions();
    }
}

void SGF_DialoguePreviewWidget::OnAssetSelected(TSharedPtr<FAssetData> Item, ESelectInfo::Type)
{
    SelectedAsset = Item;
    ActiveAsset   = Item.IsValid() ? Cast<UGF_DialogueAsset>(Item->GetAsset()) : nullptr;

    // Rebuild the active tab with the new asset
    if (TabContent.IsValid())
    {
        TabContent->SetContent(BuildTabContent(ActiveTab));
    }

    RebuildNodeRows();
    if (NodeListView.IsValid())
    {
        NodeListView->RequestListRefresh();
    }

    SimNodeIndex = 0;
    SimRefresh();
}

TSharedRef<SWidget> SGF_DialoguePreviewWidget::MakeAssetComboRow(TSharedPtr<FAssetData> Item)
{
    FString Label = Item.IsValid() ? Item->AssetName.ToString() : TEXT("(none)");
    return SNew(STextBlock)
        .Text(FText::FromString(Label))
        .Margin(FMargin(4, 2));
}

FText SGF_DialoguePreviewWidget::GetSelectedAssetLabel() const
{
    if (SelectedAsset.IsValid())
    {
        return FText::FromName(SelectedAsset->AssetName);
    }
    return FText::FromString(TEXT("- select an asset -"));
}

FReply SGF_DialoguePreviewWidget::OnRefreshClicked()
{
    RefreshAssetList();
    return FReply::Handled();
}

// ─── Tab system ───────────────────────────────────────────────────────────────

FReply SGF_DialoguePreviewWidget::OnTabClicked(FName Tab)
{
    if (ActiveTab == Tab) return FReply::Handled();
    ActiveTab = Tab;

    if (TabContent.IsValid())
    {
        TabContent->SetContent(BuildTabContent(Tab));
    }

    return FReply::Handled();
}

FSlateColor SGF_DialoguePreviewWidget::GetTabBgColor(FName Tab) const
{
    return (ActiveTab == Tab)
        ? FSlateColor(GEDialogueStyle::TabActive)
        : FSlateColor(GEDialogueStyle::TabInactive);
}

FSlateColor SGF_DialoguePreviewWidget::GetTabFgColor(FName Tab) const
{
    return FSlateColor(GEDialogueStyle::TextWhite);
}

TSharedRef<SWidget> SGF_DialoguePreviewWidget::BuildTabContent(FName Tab)
{
    if (Tab == TabScript)   return BuildScriptTab();
    if (Tab == TabSimulate) return BuildSimulateTab();
    return BuildNodesTab();
}

// ─── Script tab (full screenplay view) ───────────────────────────────────────

// Forward-declared here; defined in Nodes section below
static FString EventDescription(const FGF_DialogueEvent& Ev);

TSharedRef<SWidget> SGF_DialoguePreviewWidget::BuildScriptTab()
{
    UGF_DialogueAsset* Asset = ActiveAsset.Get();

    if (!Asset || Asset->Nodes.Num() == 0)
    {
        return SNew(SBox)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Select a dialogue asset above to view the script.")))
                .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
            ];
    }

    TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);

    for (int32 i = 0; i < Asset->Nodes.Num(); ++i)
    {
        Scroll->AddSlot()
        .Padding(FMargin(10, 6, 10, 0))
        [
            BuildNodeCard(Asset->Nodes[i], i)
        ];
    }

    // Bottom padding
    Scroll->AddSlot().Padding(FMargin(0, 10)) [ SNew(SBox) ];

    return Scroll;
}

TSharedRef<SWidget> SGF_DialoguePreviewWidget::BuildNodeCard(
    const FGF_DialogueNode& Node, int32 Index)
{
    // Determine type colors / labels
    FString    TypeLabel;
    FLinearColor TypeColor;
    switch (Node.Type)
    {
        case EGF_DialogueNodeType::Message: TypeLabel="MSG";    TypeColor=GEDialogueStyle::TypeMessage; break;
        case EGF_DialogueNodeType::Choice:  TypeLabel="CHOICE"; TypeColor=GEDialogueStyle::TypeChoice;  break;
        case EGF_DialogueNodeType::Event:   TypeLabel="EVENT";  TypeColor=GEDialogueStyle::TypeEvent;   break;
        case EGF_DialogueNodeType::Hide:    TypeLabel="HIDE";   TypeColor=GEDialogueStyle::TypeHide;    break;
        default:                            TypeLabel="?";      TypeColor=GEDialogueStyle::TextGray;    break;
    }

    auto NextText = [](int32 Idx) -> FString
    {
        return (Idx >= 0)
            ? FString::Printf(TEXT("-> Node %d"), Idx)
            : TEXT("-> END");
    };

    // ── Card outer border ─────────────────────────────────────────────────────
    TSharedRef<SVerticalBox> CardBody = SNew(SVerticalBox);

    // ── Header row: [index emblem]  [type emblem]  [speaker]  ──────────────────
    {
        FString Speaker = Node.SpeakerName.ToString();

        CardBody->AddSlot()
        .AutoHeight()
        .Padding(0, 0, 0, 6)
        [
            SNew(SHorizontalBox)

            // Index emblem (gray pill)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0, 0, 6, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(GEDialogueStyle::BgMid)
                .Padding(FMargin(5, 2))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("%d"), Index)))
                    .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
                ]
            ]

            // Type emblem (colored)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0, 0, 8, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(TypeColor)
                .Padding(FMargin(5, 2))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TypeLabel))
                    .ColorAndOpacity(FSlateColor(FLinearColor::Black))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                ]
            ]

            // Speaker (bold, accent color) — only for message nodes
            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Speaker))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                .ColorAndOpacity(FSlateColor(Speaker.IsEmpty()
                    ? GEDialogueStyle::TextGray
                    : GEDialogueStyle::Accent))
            ]
        ];
    }

    // ── Node-type specific body ───────────────────────────────────────────────

    if (Node.Type == EGF_DialogueNodeType::Message)
    {
        // Message text (wrapped, large)
        CardBody->AddSlot()
        .AutoHeight()
        .Padding(0, 0, 0, 8)
        [
            SNew(STextBlock)
            .Text(Node.DialogueText)
            .AutoWrapText(true)
            .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextWhite))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
        ];

        // Next node indicator
        CardBody->AddSlot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(NextText(Node.NextNodeIndex)))
            .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
            .Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
        ];
    }
    else if (Node.Type == EGF_DialogueNodeType::Choice)
    {
        // Each choice is a colored row with its jump target
        for (int32 c = 0; c < Node.Choices.Num(); ++c)
        {
            const FGF_DialogueChoice& Ch = Node.Choices[c];
            FString ChoiceNext = NextText(Ch.NextNodeIndex);

            CardBody->AddSlot()
            .AutoHeight()
            .Padding(0, 2)
            [
                SNew(SHorizontalBox)

                // Option marker
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0, 0, 6, 0)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT(">")))
                    .ColorAndOpacity(FSlateColor(GEDialogueStyle::TypeChoice))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                ]

                // Choice text
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(Ch.ChoiceText)
                    .AutoWrapText(false)
                    .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextWhite))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                ]

                // Jump target
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(10, 0, 0, 0)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(ChoiceNext))
                    .ColorAndOpacity(FSlateColor(GEDialogueStyle::Accent))
                    .Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
                ]
            ];
        }
    }
    else if (Node.Type == EGF_DialogueNodeType::Event)
    {
        // Event description
        CardBody->AddSlot()
        .AutoHeight()
        .Padding(0, 0, 0, 4)
        [
            SNew(STextBlock)
            .Text(FText::FromString(EventDescription(Node.Event)))
            .ColorAndOpacity(FSlateColor(GEDialogueStyle::TypeEvent))
            .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
        ];

        // PartyFull branch (GiveCreature)
        if (Node.Event.Type == EGF_DialogueEventType::GiveCreature &&
            Node.Event.PartyFullNodeIndex >= 0)
        {
            CardBody->AddSlot()
            .AutoHeight()
            .Padding(0, 0, 0, 2)
            [
                SNew(STextBlock)
                .Text(FText::FromString(
                    FString::Printf(TEXT("Party full -> Node %d"), Node.Event.PartyFullNodeIndex)))
                .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
            ];
        }

        // Normal next
        CardBody->AddSlot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(NextText(Node.NextNodeIndex)))
            .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
            .Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
        ];
    }
    else if (Node.Type == EGF_DialogueNodeType::Hide)
    {
        FString HideDesc = Node.ResumeAfterSeconds > 0.f
            ? FString::Printf(TEXT("Auto-resume after %.1f seconds"), Node.ResumeAfterSeconds)
            : TEXT("Manual resume (ResumeDialogue)");

        CardBody->AddSlot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(HideDesc))
            .ColorAndOpacity(FSlateColor(GEDialogueStyle::TypeHide))
            .Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
        ];
    }

    // ── Ensnare in a card: dark background + colored left accent bar ────────────
    return SNew(SBorder)
        .BorderBackgroundColor(GEDialogueStyle::BgPanel)
        .Padding(0)
        [
            SNew(SHorizontalBox)

            // Colored left accent bar (4px wide, full type color)
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SBox)
                .WidthOverride(4)
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(TypeColor)
                    .Padding(0)
                ]
            ]

            // Card content area
            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            .Padding(FMargin(12, 10))
            [
                CardBody
            ]
        ];
}

// ─── Nodes tab ────────────────────────────────────────────────────────────────

static FString EventDescription(const FGF_DialogueEvent& Ev)
{
    switch (Ev.Type)
    {
        case EGF_DialogueEventType::GiveCreature:
            return FString::Printf(TEXT("[GiveCreature] %s Lv.%d"),
                *Ev.CreatureSpeciesName.ToString(), Ev.GiftCreatureLevel);
        case EGF_DialogueEventType::GiveItem:
            return FString::Printf(TEXT("[GiveItem] %s x%d"), *Ev.ItemID.ToString(), Ev.ItemQuantity);
        case EGF_DialogueEventType::SetQuestFlag:
            return FString::Printf(TEXT("[Flag] %s = %s"),
                *Ev.QuestFlagName.ToString(), Ev.bQuestFlagValue ? TEXT("true") : TEXT("false"));
        case EGF_DialogueEventType::StartBattle:
            return TEXT("[StartBattle]");
        case EGF_DialogueEventType::OpenShop:
            return FString::Printf(TEXT("[Shop] %s"), *Ev.ShopID.ToString());
        case EGF_DialogueEventType::ContinueFlow:
            return FString::Printf(TEXT("[ContinueFlow] %s"), *Ev.EventName.ToString());
        case EGF_DialogueEventType::CustomEvent:
            return FString::Printf(TEXT("[Event] %s"), *Ev.EventName.ToString());
        default:
            return TEXT("[Event]");
    }
}

void SGF_DialoguePreviewWidget::RebuildNodeRows()
{
    NodeRows.Empty();

    UGF_DialogueAsset* Asset = ActiveAsset.Get();
    if (!Asset) return;

    for (int32 i = 0; i < Asset->Nodes.Num(); ++i)
    {
        const FGF_DialogueNode& Node = Asset->Nodes[i];
        auto Row = MakeShared<FGF_NodeRow>();
        Row->Index = i;

        switch (Node.Type)
        {
            case EGF_DialogueNodeType::Message:
                Row->TypeLabel   = TEXT("MSG");
                Row->TypeColor   = GEDialogueStyle::TypeMessage;
                Row->Speaker     = Node.SpeakerName.ToString();
                Row->TextPreview = Node.DialogueText.ToString().Left(80);
                if (Node.DialogueText.ToString().Len() > 80) Row->TextPreview += TEXT("...");
                break;

            case EGF_DialogueNodeType::Choice:
                Row->TypeLabel   = TEXT("CHOICE");
                Row->TypeColor   = GEDialogueStyle::TypeChoice;
                Row->TextPreview = FString::Printf(TEXT("%d option(s)"), Node.Choices.Num());
                for (const FGF_DialogueChoice& Ch : Node.Choices)
                {
                    Row->TextPreview += FString::Printf(TEXT("  [%s->%d]"),
                        *Ch.ChoiceText.ToString(), Ch.NextNodeIndex);
                }
                break;

            case EGF_DialogueNodeType::Event:
                Row->TypeLabel   = TEXT("EVENT");
                Row->TypeColor   = GEDialogueStyle::TypeEvent;
                Row->TextPreview = EventDescription(Node.Event);
                break;

            case EGF_DialogueNodeType::Hide:
                Row->TypeLabel   = TEXT("HIDE");
                Row->TypeColor   = GEDialogueStyle::TypeHide;
                Row->TextPreview = Node.ResumeAfterSeconds > 0.f
                    ? FString::Printf(TEXT("%.1fs auto-resume"), Node.ResumeAfterSeconds)
                    : TEXT("manual resume");
                break;
        }

        Row->NextLabel = (Node.NextNodeIndex >= 0)
            ? FString::Printf(TEXT("-> %d"), Node.NextNodeIndex)
            : TEXT("-> END");

        NodeRows.Add(Row);
    }
}

TSharedRef<SWidget> SGF_DialoguePreviewWidget::BuildNodesTab()
{
    RebuildNodeRows();

    if (!ActiveAsset.IsValid())
    {
        return SNew(SBox)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Select a dialogue asset above to view nodes.")))
                .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
            ];
    }

    return SNew(SScrollBox)
    + SScrollBox::Slot()
    [
        SAssignNew(NodeListView, SListView<TSharedPtr<FGF_NodeRow>>)
        .ListItemsSource(&NodeRows)
        .OnGenerateRow(this, &SGF_DialoguePreviewWidget::MakeNodeRow)
        .SelectionMode(ESelectionMode::Single)
        .HeaderRow(
            SNew(SHeaderRow)
            + SHeaderRow::Column("Idx")
                .DefaultLabel(FText::FromString(TEXT("#")))
                .FixedWidth(36)
            + SHeaderRow::Column("Type")
                .DefaultLabel(FText::FromString(TEXT("Type")))
                .FixedWidth(70)
            + SHeaderRow::Column("Speaker")
                .DefaultLabel(FText::FromString(TEXT("Speaker")))
                .FixedWidth(110)
            + SHeaderRow::Column("Text")
                .DefaultLabel(FText::FromString(TEXT("Content")))
                .FillWidth(1.f)
            + SHeaderRow::Column("Next")
                .DefaultLabel(FText::FromString(TEXT("Next")))
                .FixedWidth(64)
        )
    ];
}

TSharedRef<ITableRow> SGF_DialoguePreviewWidget::MakeNodeRow(
    TSharedPtr<FGF_NodeRow> Item, const TSharedRef<STableViewBase>& Owner)
{
    if (!Item.IsValid())
    {
        return SNew(STableRow<TSharedPtr<FGF_NodeRow>>, Owner);
    }

    return SNew(STableRow<TSharedPtr<FGF_NodeRow>>, Owner)
    .Padding(FMargin(2, 2))
    [
        SNew(SHorizontalBox)

        // Index
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SBox)
            .WidthOverride(36)
            [
                SNew(STextBlock)
                .Text(FText::AsNumber(Item->Index))
                .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
                .Justification(ETextJustify::Right)
                .Margin(FMargin(0, 0, 6, 0))
            ]
        ]

        // Type emblem
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SBox)
            .WidthOverride(70)
            [
                SNew(SBorder)
                .BorderBackgroundColor(Item->TypeColor)
                .Padding(FMargin(4, 2))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Item->TypeLabel))
                    .ColorAndOpacity(FSlateColor(FLinearColor::Black))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                ]
            ]
        ]

        // Speaker
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(6, 0)
        [
            SNew(SBox)
            .WidthOverride(110)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Item->Speaker))
                .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
            ]
        ]

        // Content
        + SHorizontalBox::Slot()
        .FillWidth(1.f)
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Item->TextPreview))
            .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextWhite))
            .AutoWrapText(false)
        ]

        // Next
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(6, 0, 0, 0)
        [
            SNew(SBox)
            .WidthOverride(64)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Item->NextLabel))
                .ColorAndOpacity(FSlateColor(GEDialogueStyle::Accent))
            ]
        ]
    ];
}

// ─── Simulate tab ─────────────────────────────────────────────────────────────

TSharedRef<SWidget> SGF_DialoguePreviewWidget::BuildSimulateTab()
{
    SimNodeIndex = 0;

    return SNew(SVerticalBox)

    // Dialogue box area
    + SVerticalBox::Slot()
    .FillHeight(1.f)
    .Padding(16, 12)
    [
        SNew(SBorder)
        .BorderBackgroundColor(GEDialogueStyle::BoxBorder)
        .Padding(1.f)
        [
            SNew(SBorder)
            .BorderBackgroundColor(GEDialogueStyle::BgPanel)
            .Padding(16)
            [
                SNew(SVerticalBox)

                // Speaker name
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 8)
                [
                    SAssignNew(SimSpeakerText, STextBlock)
                    .Text(FText::GetEmpty())
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                    .ColorAndOpacity(FSlateColor(GEDialogueStyle::Accent))
                ]

                // Body text
                + SVerticalBox::Slot()
                .FillHeight(1.f)
                [
                    SAssignNew(SimBodyText, STextBlock)
                    .Text(FText::FromString(TEXT("Select a dialogue asset to begin.")))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextWhite))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                ]

                // Choices
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 12, 0, 0)
                [
                    SAssignNew(SimChoicesBox, SVerticalBox)
                ]
            ]
        ]
    ]

    // Control row
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(16, 0, 16, 12)
    [
        SNew(SHorizontalBox)

        // Status label (node index info)
        + SHorizontalBox::Slot()
        .FillWidth(1.f)
        .VAlign(VAlign_Center)
        [
            SAssignNew(SimStatusText, STextBlock)
            .Text(FText::GetEmpty())
            .ColorAndOpacity(FSlateColor(GEDialogueStyle::TextGray))
        ]

        // Reset button
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(8, 0, 4, 0)
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Reset")))
            .OnClicked(FOnClicked::CreateLambda([this]()
            {
                SimReset();
                return FReply::Handled();
            }))
        ]

        // Advance button
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Advance")))
            .ButtonColorAndOpacity(FSlateColor(GEDialogueStyle::Accent))
            .ForegroundColor(FLinearColor::White)
            .OnClicked(this, &SGF_DialoguePreviewWidget::OnSimAdvance)
        ]
    ];
}

void SGF_DialoguePreviewWidget::SimReset()
{
    SimNodeIndex = 0;
    SimRefresh();
}

void SGF_DialoguePreviewWidget::SimRefresh()
{
    if (!SimBodyText.IsValid()) return;

    UGF_DialogueAsset* Asset = ActiveAsset.Get();

    if (!Asset || Asset->Nodes.Num() == 0)
    {
        SimSpeakerText->SetText(FText::GetEmpty());
        SimBodyText->SetText(FText::FromString(TEXT("No asset loaded.")));
        SimStatusText->SetText(FText::GetEmpty());
        SimChoicesBox->ClearChildren();
        return;
    }

    SimNodeIndex = FMath::Clamp(SimNodeIndex, 0, Asset->Nodes.Num() - 1);
    const FGF_DialogueNode& Node = Asset->Nodes[SimNodeIndex];

    SimChoicesBox->ClearChildren();

    switch (Node.Type)
    {
        case EGF_DialogueNodeType::Message:
        {
            SimSpeakerText->SetText(Node.SpeakerName);
            SimBodyText->SetText(Node.DialogueText);
            break;
        }

        case EGF_DialogueNodeType::Choice:
        {
            SimSpeakerText->SetText(Node.SpeakerName);
            SimBodyText->SetText(Node.DialogueText.IsEmpty()
                ? FText::FromString(TEXT("(make a choice)"))
                : Node.DialogueText);

            for (int32 i = 0; i < Node.Choices.Num(); ++i)
            {
                const FGF_DialogueChoice& Choice = Node.Choices[i];
                const int32 ChoiceIdx = i;

                SimChoicesBox->AddSlot()
                .AutoHeight()
                .Padding(0, 3)
                [
                    SNew(SButton)
                    .Text(Choice.ChoiceText)
                    .ButtonColorAndOpacity(FSlateColor(GEDialogueStyle::ChoiceBg))
                    .ForegroundColor(FLinearColor::White)
                    .OnClicked(FOnClicked::CreateLambda([this, ChoiceIdx]()
                    {
                        return OnSimChoice(ChoiceIdx);
                    }))
                ];
            }
            break;
        }

        case EGF_DialogueNodeType::Event:
        {
            SimSpeakerText->SetText(FText::GetEmpty());
            SimBodyText->SetText(FText::FromString(
                FString::Printf(TEXT("EVENT: %s"), *EventDescription(Node.Event))));
            break;
        }

        case EGF_DialogueNodeType::Hide:
        {
            SimSpeakerText->SetText(FText::GetEmpty());
            SimBodyText->SetText(FText::FromString(
                Node.ResumeAfterSeconds > 0.f
                    ? FString::Printf(TEXT("[ Hidden - auto-resume after %.1fs ]"), Node.ResumeAfterSeconds)
                    : TEXT("[ Hidden - waiting for manual ResumeDialogue() ]")));
            break;
        }
    }

    const FString NextStr = (Node.NextNodeIndex >= 0)
        ? FString::Printf(TEXT("-> Node %d"), Node.NextNodeIndex)
        : TEXT("-> END");

    SimStatusText->SetText(FText::FromString(
        FString::Printf(TEXT("Node %d / %d  |  %s"),
            SimNodeIndex, Asset->Nodes.Num() - 1, *NextStr)));
}

FReply SGF_DialoguePreviewWidget::OnSimAdvance()
{
    UGF_DialogueAsset* Asset = ActiveAsset.Get();
    if (!Asset || !Asset->Nodes.IsValidIndex(SimNodeIndex))
    {
        return FReply::Handled();
    }

    const FGF_DialogueNode& Node = Asset->Nodes[SimNodeIndex];

    if (Node.Type == EGF_DialogueNodeType::Choice)
    {
        return FReply::Handled();
    }

    const int32 Next = Node.NextNodeIndex;
    if (Next >= 0 && Asset->Nodes.IsValidIndex(Next))
    {
        SimNodeIndex = Next;
        SimRefresh();
    }
    else
    {
        SimSpeakerText->SetText(FText::GetEmpty());
        SimBodyText->SetText(FText::FromString(TEXT("--- Dialogue End ---")));
        SimStatusText->SetText(FText::FromString(TEXT("Reached end. Press Reset to replay.")));
        SimChoicesBox->ClearChildren();
    }

    return FReply::Handled();
}

FReply SGF_DialoguePreviewWidget::OnSimChoice(int32 ChoiceIdx)
{
    UGF_DialogueAsset* Asset = ActiveAsset.Get();
    if (!Asset || !Asset->Nodes.IsValidIndex(SimNodeIndex)) return FReply::Handled();

    const FGF_DialogueNode& Node = Asset->Nodes[SimNodeIndex];
    if (!Node.Choices.IsValidIndex(ChoiceIdx)) return FReply::Handled();

    const int32 Next = Node.Choices[ChoiceIdx].NextNodeIndex;
    if (Next >= 0 && Asset->Nodes.IsValidIndex(Next))
    {
        SimNodeIndex = Next;
        SimRefresh();
    }
    else
    {
        SimSpeakerText->SetText(FText::GetEmpty());
        SimBodyText->SetText(FText::FromString(TEXT("--- Dialogue End ---")));
        SimStatusText->SetText(FText::FromString(TEXT("Reached end. Press Reset to replay.")));
        SimChoicesBox->ClearChildren();
    }

    return FReply::Handled();
}
