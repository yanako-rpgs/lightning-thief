#include "GammaFrameworkEditor.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_CreatureSpeciesDataThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "Dialogue/GF_DialogueAsset.h"
#include "Dialogue/GF_DialogueParser.h"
#include "SGF_DialoguePreviewWidget.h"
#include "SGF_CreateCreatureWidget.h"
#include "GF_BlueprintJsonExporter.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Blueprint.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FGF_GammaFrameworkEditorModule"

// ── Static members ────────────────────────────────────────────────────────────

FString                                FGF_GammaFrameworkEditorModule::WatchedSourceFile;
FString                                FGF_GammaFrameworkEditorModule::LastReimportStatus;
TWeakPtr<SWindow>                      FGF_GammaFrameworkEditorModule::DialogueEditorWindow;
TWeakPtr<SGF_DialoguePreviewWidget>    FGF_GammaFrameworkEditorModule::DialoguePreviewWidget;
TWeakPtr<SWindow>                      FGF_GammaFrameworkEditorModule::CreateCreatureWindow;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void FGF_GammaFrameworkEditorModule::StartupModule()
{
    UThumbnailManager::Get().RegisterCustomRenderer(
        UGF_CreatureSpeciesData::StaticClass(),
        UGF_CreatureSpeciesDataThumbnailRenderer::StaticClass());

    // Defer menu registration until UToolMenus is fully initialized
    // (fires after the Level Editor has registered its own menus)
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FGF_GammaFrameworkEditorModule::RegisterMenus));
}

void FGF_GammaFrameworkEditorModule::ShutdownModule()
{
    StopWatchingFile();
    CloseDialogueEditorWindow();

    UToolMenus::UnRegisterStartupCallback(this);

    if (UObjectInitialized() && UToolMenus::IsToolMenuUIEnabled())
    {
        UToolMenus::Get()->RemoveMenu("LevelEditor.MainMenu.GammaFramework");
    }

    if (UObjectInitialized())
    {
        UThumbnailManager::Get().UnregisterCustomRenderer(UGF_CreatureSpeciesData::StaticClass());
    }
}

// ── Menu registration ─────────────────────────────────────────────────────────

void FGF_GammaFrameworkEditorModule::RegisterMenus()
{
    // Add a top-level "Gamma" pulldown to the Level Editor menu bar.
    // Named for the framework, not for Gamma Framework -- the project this was
    // extracted from. The menu id changed with it, so ShutdownModule removes
    // "LevelEditor.MainMenu.GammaFramework" to match.
    UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
    FToolMenuSection& TopSection = MainMenu->FindOrAddSection("GammaFramework");
    TopSection.AddSubMenu(
        "GammaFramework",
        LOCTEXT("GammaMenuLabel", "Gamma"),
        LOCTEXT("GammaMenuTip", "Gamma Framework tools"),
        FNewToolMenuDelegate::CreateRaw(this, &FGF_GammaFrameworkEditorModule::FillGammaMenu),
        false
    );
}

void FGF_GammaFrameworkEditorModule::FillGammaMenu(UToolMenu* Menu)
{
    FToolMenuSection& CreatureSection = Menu->AddSection(
        "GFCreatures",
        LOCTEXT("GFCreatureSectionLabel", "Creatures"));

    CreatureSection.AddEntry(FToolMenuEntry::InitMenuEntry(
        "CreateCreature",
        LOCTEXT("CreateCreatureLabel", "Create Creature..."),
        LOCTEXT("CreateCreatureTip", "Name a creature, pick its type, and get a species data asset filed under that type"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.DataAsset"),
        FUIAction(FExecuteAction::CreateStatic(&FGF_GammaFrameworkEditorModule::OpenCreateCreatureWindow))
    ));

    FToolMenuSection& Section = Menu->AddSection(
        "GEDialogue",
        LOCTEXT("GEDlgSectionLabel", "Dialogue"));

    // Dialogue Editor window
    Section.AddEntry(FToolMenuEntry::InitMenuEntry(
        "DialogueEditor",
        LOCTEXT("DialogueEditorLabel", "Dialogue Editor"),
        LOCTEXT("DialogueEditorTip", "Open the Gamma Framework Dialogue Editor & Previewer"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"),
        FUIAction(FExecuteAction::CreateStatic(&FGF_GammaFrameworkEditorModule::OpenDialogueEditorWindow))
    ));

    // Browse to set source .gfdlg
    Section.AddEntry(FToolMenuEntry::InitMenuEntry(
        "BrowseSource",
        LOCTEXT("BrowseSourceLabel", "Set Source File..."),
        LOCTEXT("BrowseSourceTip", "Select the master .gfdlg file to watch for changes and reimport from"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "ContentBrowser.ImportPackage"),
        FUIAction(FExecuteAction::CreateStatic(&FGF_GammaFrameworkEditorModule::BrowseAndSetSourceFile))
    ));

    // Reimport All
    Section.AddEntry(FToolMenuEntry::InitMenuEntry(
        "ReimportAll",
        LOCTEXT("ReimportAllLabel", "Reimport All Dialogue"),
        LOCTEXT("ReimportAllTip", "Reimport all @dialogue sections from the source .gfdlg file into their assets"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.ReimportAsset"),
        FUIAction(
            FExecuteAction::CreateStatic(&FGF_GammaFrameworkEditorModule::ReimportAllDialogue),
            FCanExecuteAction::CreateStatic(&FGF_GammaFrameworkEditorModule::CanReimportAll)
        )
    ));

    // Create Missing Assets
    Section.AddEntry(FToolMenuEntry::InitMenuEntry(
        "CreateMissing",
        LOCTEXT("CreateMissingLabel", "Create Missing Assets"),
        LOCTEXT("CreateMissingTip", "Create new data assets for @dialogue sections that don't have a matching asset yet"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.SaveAsset"),
        FUIAction(
            FExecuteAction::CreateStatic(&FGF_GammaFrameworkEditorModule::CreateMissingAssets),
            FCanExecuteAction::CreateStatic(&FGF_GammaFrameworkEditorModule::CanReimportAll)
        )
    ));

    FToolMenuSection& BlueprintSection = Menu->AddSection(
        "GFBlueprints",
        LOCTEXT("GFBlueprintSectionLabel", "Blueprints"));

    BlueprintSection.AddEntry(FToolMenuEntry::InitMenuEntry(
        "ExportBlueprintsToJson",
        LOCTEXT("ExportBlueprintsLabel", "Export to JSON"),
        LOCTEXT("ExportBlueprintsTip", "Write the selected blueprints out as JSON: widget tree, variables, and every node and wire. Exports all widget blueprints when nothing is selected"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.SaveAsset"),
        FUIAction(FExecuteAction::CreateStatic(&FGF_GammaFrameworkEditorModule::ExportSelectedBlueprintsToJson))
    ));
}

// ── Blueprint -> JSON ─────────────────────────────────────────────────────────

void FGF_GammaFrameworkEditorModule::ExportSelectedBlueprintsToJson()
{
    FContentBrowserModule& ContentBrowser =
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

    TArray<FAssetData> Selected;
    ContentBrowser.Get().GetSelectedAssets(Selected);

    // Only the blueprints out of whatever happened to be selected -- picking a
    // folder full of textures should not be an error, it should just mean
    // "none of these".
    TArray<FAssetData> Blueprints;
    for (const FAssetData& Asset : Selected)
    {
        if (const UClass* AssetClass = Asset.GetClass())
        {
            if (AssetClass->IsChildOf(UBlueprint::StaticClass()))
            {
                Blueprints.Add(Asset);
            }
        }
    }

    // Nothing usable selected: fall back to every widget blueprint in the
    // project, which is the case this tool gets opened for most often.
    const bool bExportedEverything = Blueprints.Num() == 0;
    if (bExportedEverything)
    {
        FGF_BlueprintJsonExporter::FindBlueprints({}, /*bWidgetsOnly*/ true, Blueprints);
    }

    const FString OutDir = FGF_BlueprintJsonExporter::DefaultOutputDir();

    TArray<FString> WrittenFiles;
    FGF_BlueprintExportOptions Options;
    const int32 Written = FGF_BlueprintJsonExporter::ExportAssets(Blueprints, OutDir, Options, WrittenFiles);

    const FText Message = Written > 0
        ? FText::Format(
            LOCTEXT("ExportBlueprintsDone", "Exported {0} blueprint(s) to\n{1}"),
            FText::AsNumber(Written), FText::FromString(OutDir))
        : LOCTEXT("ExportBlueprintsNone", "No blueprints were exported.");

    FNotificationInfo Info(Message);
    Info.ExpireDuration = 6.0f;
    Info.bUseSuccessFailIcons = true;

    if (Written > 0)
    {
        // A path in a toast is not much use on its own.
        Info.Hyperlink = FSimpleDelegate::CreateLambda([OutDir]()
        {
            FPlatformProcess::ExploreFolder(*OutDir);
        });
        Info.HyperlinkText = LOCTEXT("ExportBlueprintsOpenFolder", "Open folder");
    }

    TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
    if (Notification.IsValid())
    {
        Notification->SetCompletionState(Written > 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
    }
}

// ── Create Creature window ────────────────────────────────────────────────────

void FGF_GammaFrameworkEditorModule::OpenCreateCreatureWindow()
{
    // One window only. Two of these open at once would each hold their own idea
    // of the root path, and whichever was typed in last would win the config.
    if (TSharedPtr<SWindow> Existing = CreateCreatureWindow.Pin())
    {
        Existing->BringToFront();
        return;
    }

    TSharedRef<SWindow> NewWindow = SNew(SWindow)
        .Title(LOCTEXT("CreateCreatureTitle", "Create Creature"))
        .ClientSize(FVector2D(460, 460))
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        .SizingRule(ESizingRule::UserSized);

    NewWindow->SetContent(SNew(SGF_CreateCreatureWidget));
    FSlateApplication::Get().AddWindow(NewWindow);

    CreateCreatureWindow = NewWindow;

    NewWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>&)
    {
        CreateCreatureWindow.Reset();
    }));
}

void FGF_GammaFrameworkEditorModule::CloseCreateCreatureWindow()
{
    if (TSharedPtr<SWindow> Win = CreateCreatureWindow.Pin())
        Win->RequestDestroyWindow();

    CreateCreatureWindow.Reset();
}

// ── Window open / close ───────────────────────────────────────────────────────

void FGF_GammaFrameworkEditorModule::OpenDialogueEditorWindow()
{
    if (DialogueEditorWindow.IsValid())
    {
        if (TSharedPtr<SWindow> Win = DialogueEditorWindow.Pin())
            Win->BringToFront();
        return;
    }

    TSharedRef<SGF_DialoguePreviewWidget> NewWidget = SNew(SGF_DialoguePreviewWidget);

    TSharedRef<SWindow> NewWindow = SNew(SWindow)
        .Title(LOCTEXT("DialogueEditorTitle", "Dialogue Editor \u2014 Gamma Framework"))
        .ClientSize(FVector2D(960, 720))
        .SupportsMaximize(true)
        .SupportsMinimize(true)
        .IsTopmostWindow(false)
        .SizingRule(ESizingRule::UserSized);

    NewWindow->SetContent(NewWidget);
    FSlateApplication::Get().AddWindow(NewWindow);

    DialogueEditorWindow  = NewWindow;
    DialoguePreviewWidget = NewWidget;

    NewWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>&)
    {
        DialoguePreviewWidget.Reset();
        DialogueEditorWindow.Reset();
    }));
}

void FGF_GammaFrameworkEditorModule::CloseDialogueEditorWindow()
{
    if (TSharedPtr<SWindow> Win = DialogueEditorWindow.Pin())
        Win->RequestDestroyWindow();

    DialoguePreviewWidget.Reset();
    DialogueEditorWindow.Reset();
}

// ── Source file / file watcher ────────────────────────────────────────────────

void FGF_GammaFrameworkEditorModule::BrowseAndSetSourceFile()
{
    IDesktopPlatform* DP = FDesktopPlatformModule::Get();
    if (!DP) return;

    TArray<FString> OutFiles;
    const void* ParentHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

    DP->OpenFileDialog(
        ParentHandle,
        TEXT("Select master .gfdlg dialogue file"),
        FPaths::ProjectContentDir(),
        TEXT(""),
        TEXT("Gamma Framework Dialogue (*.gfdlg;*.gedlg)|*.gfdlg;*.gedlg|All Files (*.*)|*.*"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (OutFiles.Num() > 0)
    {
        // Get the module instance to start the watcher (non-static method)
        FGF_GammaFrameworkEditorModule* Mod =
            FModuleManager::GetModulePtr<FGF_GammaFrameworkEditorModule>("GammaFrameworkEditor");
        if (Mod)
        {
            Mod->StartWatchingFile(FPaths::ConvertRelativePathToFull(OutFiles[0]));
        }

        // If the window is open, refresh its display
        if (TSharedPtr<SGF_DialoguePreviewWidget> Widget = DialoguePreviewWidget.Pin())
        {
            Widget->NotifySourceFileChanged();
        }
    }
}

void FGF_GammaFrameworkEditorModule::ReimportAllDialogue()
{
    if (!WatchedSourceFile.IsEmpty())
    {
        DoReimportFromFile(WatchedSourceFile);

        // Refresh the window node list if it's open
        if (TSharedPtr<SGF_DialoguePreviewWidget> Widget = DialoguePreviewWidget.Pin())
        {
            Widget->NotifySourceFileChanged();
        }
    }
}

void FGF_GammaFrameworkEditorModule::CreateMissingAssets()
{
    if (WatchedSourceFile.IsEmpty()) return;

    FString FileText;
    if (!FFileHelper::LoadFileToString(FileText, *WatchedSourceFile)) return;

    TArray<FGF_DialogueSection> Sections = FGF_DialogueParser::SplitIntoSections(FileText);
    if (Sections.IsEmpty())
    {
        LastReimportStatus = TEXT("No @dialogue sections found in source file.");
        return;
    }

    // Build set of existing asset names for quick lookup
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> AllAssets;
    ARM.Get().GetAssetsByClass(UGF_DialogueAsset::StaticClass()->GetClassPathName(), AllAssets);

    // Derive package path from the source .gfdlg file's folder
    // e.g. Content/DialogueForEntireGame/Foo.gfdlg -> /Game/DialogueForEntireGame
    FString PackagePath;
    const FString SourceDir = FPaths::GetPath(WatchedSourceFile);
    if (!FPackageName::TryConvertFilenameToLongPackageName(SourceDir, PackagePath))
    {
        // Fallback: ask user
        IDesktopPlatform* DP = FDesktopPlatformModule::Get();
        FString OutFolder;
        const bool bPicked = DP->OpenDirectoryDialog(
            FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
            TEXT("Choose Content folder for new dialogue assets"),
            FPaths::ProjectContentDir(),
            OutFolder
        );
        if (!bPicked || OutFolder.IsEmpty()) return;

        if (!FPackageName::TryConvertFilenameToLongPackageName(OutFolder, PackagePath))
        {
            LastReimportStatus = TEXT("ERROR: Folder must be inside the project Content directory.");
            return;
        }
    }

    // Find sections with no matching asset
    int32 Created = 0;
    int32 Skipped = 0;

    for (const FGF_DialogueSection& Section : Sections)
    {
        // Check if a matching asset already exists
        bool bExists = false;
        for (const FAssetData& AD : AllAssets)
        {
            UGF_DialogueAsset* Candidate = Cast<UGF_DialogueAsset>(AD.GetAsset());
            if (!Candidate) continue;
            if (Candidate->DialogueID.ToString().Equals(Section.DialogueID, ESearchCase::IgnoreCase) ||
                AD.AssetName.ToString().Equals(Section.DialogueID, ESearchCase::IgnoreCase))
            {
                bExists = true;
                break;
            }
        }

        if (bExists) { ++Skipped; continue; }

        // Create new package + asset
        const FString AssetName    = UPackageTools::SanitizePackageName(Section.DialogueID);
        const FString FullPkgName  = PackagePath / AssetName;

        UPackage* Package = CreatePackage(*FullPkgName);
        Package->FullyLoad();

        UGF_DialogueAsset* Asset = NewObject<UGF_DialogueAsset>(
            Package, *AssetName, RF_Public | RF_Standalone);

        Asset->SourceFilePath = WatchedSourceFile;
        if (!FGF_DialogueParser::ParseIntoAsset(Section.SectionText, Asset))
        {
            UE_LOG(LogTemp, Error, TEXT("GE DialogueEditor: Failed to parse section '%s'"), *Section.DialogueID);
            continue;
        }

        FAssetRegistryModule::AssetCreated(Asset);
        Asset->MarkPackageDirty();

        const FString PackageFile = FPackageName::LongPackageNameToFilename(
            FullPkgName, FPackageName::GetAssetPackageExtension());

        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(Package, Asset, *PackageFile, SaveArgs);

        ++Created;
    }

    LastReimportStatus = FString::Printf(
        TEXT("Created %d new asset(s)  (%d already existed)"), Created, Skipped);
    UE_LOG(LogTemp, Log, TEXT("GE DialogueEditor: %s"), *LastReimportStatus);

    // Refresh the window asset list
    if (TSharedPtr<SGF_DialoguePreviewWidget> Widget = DialoguePreviewWidget.Pin())
    {
        Widget->NotifySourceFileChanged();
    }
}

bool FGF_GammaFrameworkEditorModule::CanReimportAll()
{
    return !WatchedSourceFile.IsEmpty();
}

void FGF_GammaFrameworkEditorModule::StartWatchingFile(const FString& FilePath)
{
    StopWatchingFile();
    WatchedSourceFile = FilePath;

    FString Dir = FPaths::GetPath(FilePath);
    FDirectoryWatcherModule& DWM =
        FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
    IDirectoryWatcher* DW = DWM.Get();
    if (DW)
    {
        DW->RegisterDirectoryChangedCallback_Handle(
            Dir,
            IDirectoryWatcher::FDirectoryChanged::CreateRaw(
                this, &FGF_GammaFrameworkEditorModule::OnDirectoryChanged),
            DirectoryWatcherHandle,
            IDirectoryWatcher::WatchOptions::IgnoreChangesInSubtree
        );
        UE_LOG(LogTemp, Log, TEXT("GE DialogueEditor: Watching '%s'"), *FPaths::GetCleanFilename(FilePath));
    }
}

void FGF_GammaFrameworkEditorModule::StopWatchingFile()
{
    if (DirectoryWatcherHandle.IsValid() && !WatchedSourceFile.IsEmpty())
    {
        FDirectoryWatcherModule* DWM =
            FModuleManager::GetModulePtr<FDirectoryWatcherModule>("DirectoryWatcher");
        if (DWM)
        {
            IDirectoryWatcher* DW = DWM->Get();
            if (DW)
            {
                FString Dir = FPaths::GetPath(WatchedSourceFile);
                DW->UnregisterDirectoryChangedCallback_Handle(Dir, DirectoryWatcherHandle);
            }
        }
        DirectoryWatcherHandle.Reset();
    }
    WatchedSourceFile.Empty();
}

void FGF_GammaFrameworkEditorModule::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
    for (const FFileChangeData& Change : Changes)
    {
        FString ChangedPath = FPaths::ConvertRelativePathToFull(Change.Filename);
        FString WatchedPath = FPaths::ConvertRelativePathToFull(WatchedSourceFile);

        if (ChangedPath.Equals(WatchedPath, ESearchCase::IgnoreCase))
        {
            UE_LOG(LogTemp, Log, TEXT("GE DialogueEditor: Source file changed, auto-reimporting..."));
            DoReimportFromFile(WatchedSourceFile);

            if (TSharedPtr<SGF_DialoguePreviewWidget> Widget = DialoguePreviewWidget.Pin())
            {
                Widget->NotifySourceFileChanged();
            }
            break;
        }
    }
}

void FGF_GammaFrameworkEditorModule::DoReimportFromFile(const FString& FilePath)
{
    FString FileText;
    if (!FFileHelper::LoadFileToString(FileText, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("GE DialogueEditor: Could not read '%s'"), *FilePath);
        return;
    }

    TArray<FGF_DialogueSection> Sections = FGF_DialogueParser::SplitIntoSections(FileText);
    if (Sections.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GE DialogueEditor: No @dialogue sections found in '%s'"), *FilePath);
        return;
    }

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> AllAssets;
    ARM.Get().GetAssetsByClass(UGF_DialogueAsset::StaticClass()->GetClassPathName(), AllAssets);

    int32 Reimported = 0;
    int32 Missing    = 0;

    for (const FGF_DialogueSection& Section : Sections)
    {
        UGF_DialogueAsset* Asset = nullptr;
        for (const FAssetData& AD : AllAssets)
        {
            UGF_DialogueAsset* Candidate = Cast<UGF_DialogueAsset>(AD.GetAsset());
            if (!Candidate) continue;

            // Primary match: DialogueID on the asset matches the section name
            const bool bMatchesID   = Candidate->DialogueID.ToString().Equals(
                Section.DialogueID, ESearchCase::IgnoreCase);
            // Fallback: asset package name matches (catches assets whose data was wiped by hot-reload)
            const bool bMatchesName = AD.AssetName.ToString().Equals(
                Section.DialogueID, ESearchCase::IgnoreCase);

            if (bMatchesID || bMatchesName)
            {
                Asset = Candidate;
                break;
            }
        }

        if (!Asset)
        {
            UE_LOG(LogTemp, Warning, TEXT("GE DialogueEditor: No asset for section '%s' - skipping"), *Section.DialogueID);
            ++Missing;
            continue;
        }

        Asset->SourceFilePath = FilePath;
        if (FGF_DialogueParser::ParseIntoAsset(Section.SectionText, Asset))
        {
            Asset->MarkPackageDirty();
            ++Reimported;
        }
    }

    if (Missing > 0)
    {
        LastReimportStatus = FString::Printf(
            TEXT("Reimported %d / %d  (%d not found - use GE > Create Missing Assets)"),
            Reimported, Sections.Num(), Missing);
    }
    else
    {
        LastReimportStatus = FString::Printf(TEXT("Reimported %d / %d sections"), Reimported, Sections.Num());
    }
    UE_LOG(LogTemp, Log, TEXT("GE DialogueEditor: %s"), *LastReimportStatus);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGF_GammaFrameworkEditorModule, GammaFrameworkEditor)
