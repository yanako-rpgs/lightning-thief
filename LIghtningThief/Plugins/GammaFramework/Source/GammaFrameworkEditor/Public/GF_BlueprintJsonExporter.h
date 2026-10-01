#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UWidget;
class FJsonObject;
class FJsonValue;
struct FAssetData;
struct FEdGraphPinType;

/**
 * What to put in the file. Everything is on by default; the switches exist so a
 * caller that only wants, say, the widget hierarchy is not made to read a
 * megabyte of node graph to find it.
 */
struct FGF_BlueprintExportOptions
{
    /** Event graph, functions, macros, and any collapsed sub-graphs. */
    bool bIncludeGraphs = true;

    /** The UMG hierarchy: widgets, their slots, animations, and property bindings. */
    bool bIncludeWidgetTree = true;

    /** Per-object property dumps. Only properties that differ from the archetype. */
    bool bIncludeProperties = true;

    /** Component templates from the Simple Construction Script (actor blueprints). */
    bool bIncludeComponents = true;

    /** Pins the node hides from the user and that are neither wired nor overridden. */
    bool bIncludeHiddenPins = false;

    /** Indented output. Off gives one long line -- smaller, still valid JSON. */
    bool bPretty = true;
};

/**
 * FGF_BlueprintJsonExporter
 *
 * Reads a blueprint asset and writes what it contains as JSON: the widget tree,
 * the variables, and every node and wire in every graph.
 *
 * .uasset is a binary package, so the only way to answer "what does this widget
 * actually do" is to open it in the editor and look. That is fine for a person
 * and useless for anything else -- diffs, code review, a script that checks
 * every button has a hover sound, an AI assistant being asked to change a
 * graph it cannot read. This turns the asset into text without changing it.
 *
 * The export is one-way and read-only. Nothing here writes to a package.
 *
 * Two ways in:
 *   - Gamma > Blueprints > Export to JSON... in the editor, on the content
 *     browser selection.
 *   - UGF_ExportBlueprintsCommandlet, for a headless batch run.
 *
 * Property dumps are diffed against each object's archetype, so what lands in
 * the file is what somebody changed, not the several hundred inherited
 * defaults they left alone.
 */
class FGF_BlueprintJsonExporter
{
public:
    /** One blueprint as a JSON object. Null if the blueprint is null. */
    static TSharedPtr<FJsonObject> ExportBlueprint(UBlueprint* Blueprint, const FGF_BlueprintExportOptions& Options);

    /** Same, already serialized. Empty string on failure. */
    static FString ExportBlueprintToString(UBlueprint* Blueprint, const FGF_BlueprintExportOptions& Options);

    /** Same, written to disk as UTF-8. */
    static bool ExportBlueprintToFile(UBlueprint* Blueprint, const FString& OutFilePath, const FGF_BlueprintExportOptions& Options);

    /**
     * Loads and exports a batch, one <AssetName>.json per asset under OutDir.
     * Returns how many files were written; OutWrittenFiles gets their paths.
     */
    static int32 ExportAssets(
        const TArray<FAssetData>&          Assets,
        const FString&                     OutDir,
        const FGF_BlueprintExportOptions&  Options,
        TArray<FString>&                   OutWrittenFiles);

    /** The same batch as a single JSON array, written to one file. */
    static bool ExportAssetsCombined(
        const TArray<FAssetData>&          Assets,
        const FString&                     OutFilePath,
        const FGF_BlueprintExportOptions&  Options);

    /**
     * Every blueprint under the given long package paths ("/Game/UI"), or the
     * whole project when PackagePaths is empty. bWidgetsOnly narrows it to
     * widget blueprints.
     */
    static void FindBlueprints(const TArray<FString>& PackagePaths, bool bWidgetsOnly, TArray<FAssetData>& OutAssets);

    /** Where exports go when nobody says otherwise: <Project>/Saved/BlueprintJSON. */
    static FString DefaultOutputDir();

private:
    // ----- pieces of the file -------------------------------------------------
    static TSharedPtr<FJsonObject>        ExportGraph(UEdGraph* Graph, const FString& GraphKind, const FGF_BlueprintExportOptions& Options);
    static TSharedPtr<FJsonObject>        ExportNode(UEdGraphNode* Node, const FGF_BlueprintExportOptions& Options);
    static TSharedPtr<FJsonObject>        ExportPin(UEdGraphPin* Pin);
    static TSharedPtr<FJsonObject>        ExportWidget(UWidget* Widget, const FGF_BlueprintExportOptions& Options);

    // ----- shared helpers -----------------------------------------------------
    /** Properties of Object that differ from its archetype, written onto Out. */
    static void    WriteChangedProperties(UObject* Object, const TSharedRef<FJsonObject>& Out);
    /** A pin type as one readable token: "exec", "object:/Script/UMG.Button", "array<struct:Vector>". */
    static FString PinTypeToString(const FEdGraphPinType& PinType);
    static FString ObjectPath(const UObject* Object);
};
