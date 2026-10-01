#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class SWindow;
class SGF_DialoguePreviewWidget;
class SGF_CreateCreatureWidget;
struct FFileChangeData;

/**
 * FGF_GammaFrameworkEditorModule
 *
 * Editor-only module. Adds:
 *   - Thumbnail renderer for UGF_CreatureSpeciesData
 *   - Top-level "Gamma" menu in the Level Editor menu bar with:
 *
 *     Creatures
 *       • Create Creature… (name + type -> a species data asset, filed by element)
 *
 *     Dialogue
 *       • Dialogue Editor  (opens the preview/simulate window)
 *       • Set Source File… (picks a master .gfdlg, starts file watcher)
 *       • Reimport All Dialogue (reimports all @dialogue sections immediately)
 *
 *     Blueprints
 *       • Export to JSON (writes the content browser selection out as text)
 */
class FGF_GammaFrameworkEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    // ── Create Creature window ────────────────────────────────────────────────
    static void OpenCreateCreatureWindow();
    static void CloseCreateCreatureWindow();

    // ── Blueprint -> JSON ─────────────────────────────────────────────────────
    static void ExportSelectedBlueprintsToJson();

    // ── Dialogue editor window ────────────────────────────────────────────────
    static void OpenDialogueEditorWindow();
    static void CloseDialogueEditorWindow();

    // ── Source file / file watcher (callable from menu AND the editor window) ─
    static void          BrowseAndSetSourceFile();
    static void          ReimportAllDialogue();
    static void          CreateMissingAssets();
    static bool          CanReimportAll();
    static const FString& GetWatchedSourceFile()    { return WatchedSourceFile; }
    static const FString& GetLastReimportStatus()   { return LastReimportStatus; }

private:
    // Menu registration (called via UToolMenus startup callback)
    void RegisterMenus();
    void FillGammaMenu(UToolMenu* Menu);

    // File watcher (instance — needs non-static context for delegate lifetime)
    void StartWatchingFile(const FString& FilePath);
    void StopWatchingFile();
    void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);

    // Shared reimport implementation
    static void DoReimportFromFile(const FString& FilePath);

    // ── Static state ──────────────────────────────────────────────────────────
    static FString                       WatchedSourceFile;
    static FString                       LastReimportStatus;
    static TWeakPtr<SWindow>             DialogueEditorWindow;
    static TWeakPtr<SGF_DialoguePreviewWidget> DialoguePreviewWidget;
    static TWeakPtr<SWindow>             CreateCreatureWindow;

    FDelegateHandle DirectoryWatcherHandle;
};
