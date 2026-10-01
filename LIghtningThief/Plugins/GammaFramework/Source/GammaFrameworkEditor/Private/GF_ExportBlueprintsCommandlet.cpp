#include "GF_ExportBlueprintsCommandlet.h"
#include "GF_BlueprintJsonExporter.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogGFExportBlueprints, Log, All);

namespace
{
    /** "-Path=/Game/A,/Game/B" into two entries. Empty when the switch is absent. */
    void ParseList(const FString& Params, const TCHAR* Key, TArray<FString>& Out)
    {
        FString Value;
        if (FParse::Value(*Params, Key, Value))
        {
            Value.ParseIntoArray(Out, TEXT(","), /*CullEmpty*/ true);
            for (FString& Entry : Out)
            {
                Entry.TrimStartAndEndInline();
            }
        }
    }
}

UGF_ExportBlueprintsCommandlet::UGF_ExportBlueprintsCommandlet()
{
    IsClient      = false;
    IsServer      = false;
    IsEditor      = true;
    LogToConsole  = true;
}

int32 UGF_ExportBlueprintsCommandlet::Main(const FString& Params)
{
    FGF_BlueprintExportOptions Options;
    Options.bIncludeGraphs     = !FParse::Param(*Params, TEXT("NoGraphs"));
    Options.bIncludeProperties = !FParse::Param(*Params, TEXT("NoProperties"));
    Options.bIncludeWidgetTree = !FParse::Param(*Params, TEXT("NoWidgetTree"));
    Options.bIncludeHiddenPins =  FParse::Param(*Params, TEXT("HiddenPins"));
    Options.bPretty            = !FParse::Param(*Params, TEXT("Compact"));

    const bool bWidgetsOnly = FParse::Param(*Params, TEXT("WidgetsOnly"));
    const bool bCombine     = FParse::Param(*Params, TEXT("Combine"));

    TArray<FString> Paths;
    TArray<FString> NamedAssets;
    ParseList(Params, TEXT("Path="),  Paths);
    ParseList(Params, TEXT("Asset="), NamedAssets);

    FString OutTarget;
    if (!FParse::Value(*Params, TEXT("Out="), OutTarget))
    {
        OutTarget = bCombine
            ? FGF_BlueprintJsonExporter::DefaultOutputDir() / TEXT("Blueprints.json")
            : FGF_BlueprintJsonExporter::DefaultOutputDir();
    }

    // ----- work out what to export -------------------------------------------
    TArray<FAssetData> Assets;

    if (NamedAssets.Num() > 0)
    {
        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
        AssetRegistry.SearchAllAssets(/*bSynchronousSearch*/ true);

        for (const FString& PackageName : NamedAssets)
        {
            TArray<FAssetData> Found;
            AssetRegistry.GetAssetsByPackageName(FName(*PackageName), Found);
            if (Found.Num() == 0)
            {
                UE_LOG(LogGFExportBlueprints, Warning, TEXT("No asset at %s"), *PackageName);
            }
            Assets.Append(Found);
        }
    }
    else
    {
        FGF_BlueprintJsonExporter::FindBlueprints(Paths, bWidgetsOnly, Assets);
    }

    if (Assets.Num() == 0)
    {
        UE_LOG(LogGFExportBlueprints, Error, TEXT("Nothing matched. Check -Path / -Asset."));
        return 1;
    }

    UE_LOG(LogGFExportBlueprints, Display, TEXT("Exporting %d blueprint(s) to %s"), Assets.Num(), *OutTarget);

    // ----- write it ----------------------------------------------------------
    if (bCombine)
    {
        if (!FGF_BlueprintJsonExporter::ExportAssetsCombined(Assets, OutTarget, Options))
        {
            UE_LOG(LogGFExportBlueprints, Error, TEXT("Could not write %s"), *OutTarget);
            return 1;
        }
        UE_LOG(LogGFExportBlueprints, Display, TEXT("Wrote %s"), *OutTarget);
        return 0;
    }

    TArray<FString> WrittenFiles;
    const int32 Written = FGF_BlueprintJsonExporter::ExportAssets(Assets, OutTarget, Options, WrittenFiles);

    for (const FString& File : WrittenFiles)
    {
        UE_LOG(LogGFExportBlueprints, Display, TEXT("  %s"), *File);
    }

    // Say how many of the found assets actually made it to disk, not how many
    // were asked for: a blueprint that failed to load is worth noticing.
    UE_LOG(LogGFExportBlueprints, Display, TEXT("Wrote %d of %d."), Written, Assets.Num());

    return Written == Assets.Num() ? 0 : 1;
}
