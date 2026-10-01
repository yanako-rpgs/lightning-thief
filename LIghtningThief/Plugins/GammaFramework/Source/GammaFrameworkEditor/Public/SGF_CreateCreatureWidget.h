#pragma once

#include "CoreMinimal.h"
#include "GF_ElementTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SEditableTextBox;
class SCheckBox;
template <typename OptionType> class SComboBox;

/**
 * Create Creature.
 *
 * A species data asset needs a name and an element before it is worth anything,
 * and it needs to live somewhere consistent. Making one by hand is four steps in
 * the content browser -- right-click, pick the class off a long list, rename it,
 * then find the right folder -- repeated once per creature, sixty times.
 *
 * This is those four steps as two fields and a button.
 *
 * ---------------------------------------------------------------------------
 * Where assets go
 * ---------------------------------------------------------------------------
 *
 *   <Root>/<Element>/DA_<Name>
 *   /Game/CreatureData/Ember/DA_Ashling
 *
 * Filed by element on purpose: it is the one property that never changes over a
 * creature's life, so the folder never needs to move. Filing by evolution stage
 * or by region means moving assets when the design shifts, and moving a UAsset
 * leaves a redirector behind.
 *
 * The element folder is created if it does not exist.
 */
class SGF_CreateCreatureWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGF_CreateCreatureWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Default when nothing has been saved yet. */
	static const TCHAR* DefaultRootPath;

private:
	//==================================================================================
	// UI CALLBACKS
	//==================================================================================

	TSharedRef<SWidget> OnGenerateElementRow(TSharedPtr<EGF_Element> InElement) const;
	void OnElementSelected(TSharedPtr<EGF_Element> InElement, ESelectInfo::Type SelectInfo);
	FText GetSelectedElementText() const;

	void OnNameChanged(const FText& NewText);
	void OnRootPathChanged(const FText& NewText);
	void OnRootPathCommitted(const FText& NewText, ETextCommit::Type CommitType);

	FReply OnCreateClicked();
	bool CanCreate() const;

	/** The full path the button would create, or empty when the inputs are incomplete. */
	FText GetPreviewText() const;
	EVisibility GetPreviewVisibility() const;

	/** Why the button is disabled. Empty when it is not. */
	FText GetProblemText() const;
	EVisibility GetProblemVisibility() const;

	FText GetStatusText() const;
	FSlateColor GetStatusColour() const;

	//==================================================================================
	// WORK
	//==================================================================================

	/** Folder name for an element -- the display name, stripped to something a path accepts. */
	static FString ElementFolderName(EGF_Element Element);

	/** "Ember Drake" -> "EmberDrake". Empty when nothing usable survives. */
	static FString SanitiseAssetName(const FString& RawName);

	/** Package path this widget's current inputs point at. Empty when incomplete. */
	FString BuildPackagePath() const;

	/** Fills OutReason and returns false when the inputs cannot produce an asset. */
	bool ValidateInputs(FText& OutReason) const;

	static void LoadSavedRootPath(FString& OutRootPath);
	void SaveRootPath() const;

	//==================================================================================
	// STATE
	//==================================================================================

	TArray<TSharedPtr<EGF_Element>> ElementOptions;
	TSharedPtr<EGF_Element> SelectedElement;

	TSharedPtr<SComboBox<TSharedPtr<EGF_Element>>> ElementCombo;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> RootPathBox;
	TSharedPtr<SCheckBox> OpenAfterCreateCheck;

	FString RootPath;
	FString CreatureName;

	/** Result of the last create. Green on success, red on failure. */
	FText StatusText;
	bool bLastCreateSucceeded = false;
};
