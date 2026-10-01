#include "SGF_CreateCreatureWidget.h"

#include "GF_CreatureSpeciesData.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "IContentBrowserSingleton.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SavePackage.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGF_CreateCreatureWidget"

DEFINE_LOG_CATEGORY_STATIC(LogGF_CreateCreature, Log, All);

const TCHAR* SGF_CreateCreatureWidget::DefaultRootPath = TEXT("/Game/CreatureData");

namespace
{
	const TCHAR* ConfigSection = TEXT("GammaFramework.CreateCreature");

	/** Where the user last typed. Per-user, per-project. */
	const TCHAR* ConfigRootKey = TEXT("RootPath");

	/**
	 * The project's own starting path, from Config/DefaultEditor.ini.
	 *
	 * This indirection is the reason the plugin does not hardcode a game-specific
	 * folder. Godsmarch keeps everything under /Game/Godsmarch so the whole game
	 * cooks as one directory, but the plugin is meant to drop into any project,
	 * and a reusable plugin that creates a folder named after somebody else's
	 * game is a bug in every project except that one.
	 */
	const TCHAR* ConfigProjectDefaultKey = TEXT("DefaultRootPath");
}

//======================================================================================
// CONSTRUCTION
//======================================================================================

void SGF_CreateCreatureWidget::Construct(const FArguments& InArgs)
{
	LoadSavedRootPath(RootPath);

	// Every element except None. None is not a creature type -- it is the absence
	// of one, and it would produce a folder called "None" full of half-made assets.
	if (const UEnum* ElementEnum = StaticEnum<EGF_Element>())
	{
		for (int32 i = 0; i < ElementEnum->NumEnums() - 1; ++i)
		{
			if (ElementEnum->HasMetaData(TEXT("Hidden"), i))
			{
				continue;
			}

			const EGF_Element Value = static_cast<EGF_Element>(ElementEnum->GetValueByIndex(i));
			if (Value != EGF_Element::None)
			{
				ElementOptions.Add(MakeShared<EGF_Element>(Value));
			}
		}
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(16.0f)
		[
			SNew(SVerticalBox)

			// ---- heading --------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				.Text(LOCTEXT("Heading", "Create Creature"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("Subheading",
					"Makes a species data asset, filed under its element. The element folder is created if it does not exist."))
			]

			// ---- root path ------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
			[
				SNew(STextBlock).Text(LOCTEXT("RootLabel", "Creature Data folder"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SAssignNew(RootPathBox, SEditableTextBox)
				.Text(FText::FromString(RootPath))
				.HintText(FText::FromString(DefaultRootPath))
				.ToolTipText(LOCTEXT("RootTip", "Content path the element folders live under. Remembered between sessions."))
				.OnTextChanged(this, &SGF_CreateCreatureWidget::OnRootPathChanged)
				.OnTextCommitted(this, &SGF_CreateCreatureWidget::OnRootPathCommitted)
			]

			// ---- element --------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
			[
				SNew(STextBlock).Text(LOCTEXT("ElementLabel", "Type"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SAssignNew(ElementCombo, SComboBox<TSharedPtr<EGF_Element>>)
				.OptionsSource(&ElementOptions)
				.OnGenerateWidget(this, &SGF_CreateCreatureWidget::OnGenerateElementRow)
				.OnSelectionChanged(this, &SGF_CreateCreatureWidget::OnElementSelected)
				[
					SNew(STextBlock)
					.Text(this, &SGF_CreateCreatureWidget::GetSelectedElementText)
				]
			]

			// ---- name -----------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
			[
				SNew(STextBlock).Text(LOCTEXT("NameLabel", "Creature name"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SAssignNew(NameBox, SEditableTextBox)
				.HintText(LOCTEXT("NameHint", "Ashling"))
				.ToolTipText(LOCTEXT("NameTip", "Stored on the asset as its Species Name. The asset itself is prefixed DA_."))
				.OnTextChanged(this, &SGF_CreateCreatureWidget::OnNameChanged)
			]

			// ---- preview / problem ----------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility(this, &SGF_CreateCreatureWidget::GetPreviewVisibility)
				.Text(this, &SGF_CreateCreatureWidget::GetPreviewText)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FLinearColor(0.85f, 0.55f, 0.25f))
				.Visibility(this, &SGF_CreateCreatureWidget::GetProblemVisibility)
				.Text(this, &SGF_CreateCreatureWidget::GetProblemText)
			]

			// ---- options + button -----------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SAssignNew(OpenAfterCreateCheck, SCheckBox)
				.ToolTipText(LOCTEXT("OpenTip",
					"Off by default so a run of creatures can be typed one after another without the editor opening each one."))
				[
					SNew(STextBlock).Text(LOCTEXT("OpenLabel", "Open the asset after creating"))
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(32.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.IsEnabled(this, &SGF_CreateCreatureWidget::CanCreate)
					.OnClicked(this, &SGF_CreateCreatureWidget::OnCreateClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("CreateButton", "Create Creature"))
					]
				]
			]

			// ---- result ---------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(this, &SGF_CreateCreatureWidget::GetStatusColour)
				.Text(this, &SGF_CreateCreatureWidget::GetStatusText)
			]
		]
	];
}

//======================================================================================
// UI CALLBACKS
//======================================================================================

TSharedRef<SWidget> SGF_CreateCreatureWidget::OnGenerateElementRow(TSharedPtr<EGF_Element> InElement) const
{
	const FText Label = InElement.IsValid()
		? UEnum::GetDisplayValueAsText(*InElement)
		: FText::GetEmpty();

	return SNew(STextBlock).Text(Label);
}

void SGF_CreateCreatureWidget::OnElementSelected(TSharedPtr<EGF_Element> InElement, ESelectInfo::Type)
{
	SelectedElement = InElement;
	StatusText = FText::GetEmpty();
}

FText SGF_CreateCreatureWidget::GetSelectedElementText() const
{
	return SelectedElement.IsValid()
		? UEnum::GetDisplayValueAsText(*SelectedElement)
		: LOCTEXT("PickElement", "Select a type...");
}

void SGF_CreateCreatureWidget::OnNameChanged(const FText& NewText)
{
	CreatureName = NewText.ToString();
	StatusText = FText::GetEmpty();
}

void SGF_CreateCreatureWidget::OnRootPathChanged(const FText& NewText)
{
	RootPath = NewText.ToString();
	StatusText = FText::GetEmpty();
}

void SGF_CreateCreatureWidget::OnRootPathCommitted(const FText& NewText, ETextCommit::Type)
{
	RootPath = NewText.ToString();
	SaveRootPath();
}

//======================================================================================
// VALIDATION
//======================================================================================

FString SGF_CreateCreatureWidget::ElementFolderName(EGF_Element Element)
{
	FString Name = UEnum::GetDisplayValueAsText(Element).ToString();
	Name.RemoveSpacesInline();
	return UPackageTools::SanitizePackageName(Name);
}

FString SGF_CreateCreatureWidget::SanitiseAssetName(const FString& RawName)
{
	FString Result;
	Result.Reserve(RawName.Len());

	// Letters and digits only. A name with a space or an apostrophe in it is a
	// perfectly good display name and a terrible package name, so the two part
	// ways here -- the typed name is kept verbatim as SpeciesName on the asset.
	for (const TCHAR Char : RawName)
	{
		if (FChar::IsAlnum(Char) || Char == TEXT('_'))
		{
			Result.AppendChar(Char);
		}
	}

	// A package name cannot start with a digit.
	while (Result.Len() > 0 && FChar::IsDigit(Result[0]))
	{
		Result.RemoveAt(0, 1, EAllowShrinking::No);
	}

	return Result;
}

FString SGF_CreateCreatureWidget::BuildPackagePath() const
{
	if (!SelectedElement.IsValid())
	{
		return FString();
	}

	const FString AssetName = SanitiseAssetName(CreatureName);
	if (AssetName.IsEmpty())
	{
		return FString();
	}

	FString Root = RootPath.TrimStartAndEnd();
	if (Root.IsEmpty())
	{
		Root = DefaultRootPath;
	}
	Root.RemoveFromEnd(TEXT("/"));

	return FString::Printf(TEXT("%s/%s/DA_%s"),
		*Root, *ElementFolderName(*SelectedElement), *AssetName);
}

bool SGF_CreateCreatureWidget::ValidateInputs(FText& OutReason) const
{
	OutReason = FText::GetEmpty();

	if (!SelectedElement.IsValid())
	{
		OutReason = LOCTEXT("NeedElement", "Pick a type.");
		return false;
	}

	if (CreatureName.TrimStartAndEnd().IsEmpty())
	{
		OutReason = LOCTEXT("NeedName", "Give the creature a name.");
		return false;
	}

	if (SanitiseAssetName(CreatureName).IsEmpty())
	{
		OutReason = LOCTEXT("BadName", "That name has no letters or digits in it to build a file name from.");
		return false;
	}

	const FString PackagePath = BuildPackagePath();

	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		OutReason = FText::Format(
			LOCTEXT("BadPath", "'{0}' is not a valid content path. It should start with /Game/."),
			FText::FromString(RootPath));
		return false;
	}

	// Checked on disk rather than through the asset registry: an asset created
	// outside this session, or one the registry has not scanned yet, still counts
	// as already there. Overwriting a species someone has already filled in is
	// the one genuinely destructive thing this tool could do.
	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutReason = FText::Format(
			LOCTEXT("AlreadyExists", "{0} already exists. Pick another name."),
			FText::FromString(FPackageName::GetLongPackageAssetName(PackagePath)));
		return false;
	}

	return true;
}

bool SGF_CreateCreatureWidget::CanCreate() const
{
	FText Unused;
	return ValidateInputs(Unused);
}

FText SGF_CreateCreatureWidget::GetPreviewText() const
{
	const FString PackagePath = BuildPackagePath();
	if (PackagePath.IsEmpty())
	{
		return FText::GetEmpty();
	}

	return FText::Format(LOCTEXT("PreviewFmt", "Will create:  {0}"), FText::FromString(PackagePath));
}

EVisibility SGF_CreateCreatureWidget::GetPreviewVisibility() const
{
	return BuildPackagePath().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SGF_CreateCreatureWidget::GetProblemText() const
{
	FText Reason;
	ValidateInputs(Reason);
	return Reason;
}

EVisibility SGF_CreateCreatureWidget::GetProblemVisibility() const
{
	FText Reason;
	// Nothing typed yet is not a problem, it is a blank form. Only complain once
	// the user has started filling it in.
	if (!SelectedElement.IsValid() && CreatureName.IsEmpty())
	{
		return EVisibility::Collapsed;
	}

	return ValidateInputs(Reason) ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SGF_CreateCreatureWidget::GetStatusText() const
{
	return StatusText;
}

FSlateColor SGF_CreateCreatureWidget::GetStatusColour() const
{
	return bLastCreateSucceeded
		? FSlateColor(FLinearColor(0.35f, 0.8f, 0.4f))
		: FSlateColor(FLinearColor(0.9f, 0.35f, 0.3f));
}

//======================================================================================
// CREATING
//======================================================================================

FReply SGF_CreateCreatureWidget::OnCreateClicked()
{
	FText Reason;
	if (!ValidateInputs(Reason))
	{
		bLastCreateSucceeded = false;
		StatusText = Reason;
		return FReply::Handled();
	}

	const FString PackagePath = BuildPackagePath();
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);

	UPackage* Package = CreatePackage(*PackagePath);
	if (Package == nullptr)
	{
		bLastCreateSucceeded = false;
		StatusText = LOCTEXT("NoPackage", "Could not create the package.");
		return FReply::Handled();
	}

	Package->FullyLoad();

	UGF_CreatureSpeciesData* NewAsset = NewObject<UGF_CreatureSpeciesData>(
		Package, UGF_CreatureSpeciesData::StaticClass(), *AssetName, RF_Public | RF_Standalone);

	if (NewAsset == nullptr)
	{
		bLastCreateSucceeded = false;
		StatusText = LOCTEXT("NoAsset", "Could not create the asset.");
		return FReply::Handled();
	}

	// The typed name goes on verbatim -- spaces and all. Only the FILE name was
	// sanitised, so "Ember Drake" stays "Ember Drake" everywhere the player sees it.
	NewAsset->SpeciesName = FName(*CreatureName.TrimStartAndEnd());
	NewAsset->PrimaryElement = *SelectedElement;

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	// Saved immediately rather than left dirty. The folder does not exist on disk
	// until something is written into it, and an unsaved asset in a folder that is
	// not there yet is the shape of bug that loses work on an editor crash.
	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	const bool bSaved = UPackage::SavePackage(Package, NewAsset, *FileName, SaveArgs);

	if (!bSaved)
	{
		bLastCreateSucceeded = false;
		StatusText = FText::Format(
			LOCTEXT("SaveFailed", "Created {0} but could not save it to disk. Check the folder is writable."),
			FText::FromString(AssetName));
		return FReply::Handled();
	}

	UE_LOG(LogGF_CreateCreature, Log, TEXT("Created species asset %s"), *PackagePath);

	// Show it, so the next thing the user does is fill it in.
	FContentBrowserModule& ContentBrowser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	ContentBrowser.Get().SyncBrowserToAssets(TArray<UObject*>{ NewAsset });

	if (OpenAfterCreateCheck.IsValid() && OpenAfterCreateCheck->IsChecked() && GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
	}

	bLastCreateSucceeded = true;
	StatusText = FText::Format(
		LOCTEXT("CreatedFmt", "Created {0}"), FText::FromString(PackagePath));

	SaveRootPath();

	// The name is cleared but the type and the folder are not: making a run of
	// creature of the same element is the normal way this gets used, and retyping
	// the type for each one would be the slow part all over again.
	CreatureName.Reset();
	if (NameBox.IsValid())
	{
		NameBox->SetText(FText::GetEmpty());
		FSlateApplication::Get().SetKeyboardFocus(NameBox);
	}

	return FReply::Handled();
}

//======================================================================================
// CONFIG
//======================================================================================

void SGF_CreateCreatureWidget::LoadSavedRootPath(FString& OutRootPath)
{
	OutRootPath = DefaultRootPath;

	if (GConfig == nullptr)
	{
		return;
	}

	// The project gets to name its own starting folder before the plugin default
	// applies -- see ConfigProjectDefaultKey.
	FString ProjectDefault;
	if (GConfig->GetString(ConfigSection, ConfigProjectDefaultKey, ProjectDefault, GEditorIni)
		&& !ProjectDefault.IsEmpty())
	{
		OutRootPath = ProjectDefault;
	}

	// Whatever the user last typed wins over both. Set per-user rather than in
	// the project ini so one person retargeting the tool does not move everyone
	// else's creatures.
	FString Saved;
	if (GConfig->GetString(ConfigSection, ConfigRootKey, Saved, GEditorPerProjectIni) && !Saved.IsEmpty())
	{
		OutRootPath = Saved;
	}
}

void SGF_CreateCreatureWidget::SaveRootPath() const
{
	if (GConfig && !RootPath.IsEmpty())
	{
		GConfig->SetString(ConfigSection, ConfigRootKey, *RootPath, GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
	}
}

#undef LOCTEXT_NAMESPACE
