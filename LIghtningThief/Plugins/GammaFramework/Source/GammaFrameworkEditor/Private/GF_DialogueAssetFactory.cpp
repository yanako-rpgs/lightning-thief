#include "GF_DialogueAssetFactory.h"

#if WITH_EDITOR

// Moved from the GammaFramework runtime module into GammaFrameworkEditor: UHT parses inside
// #if WITH_EDITOR, so it still tried to resolve the UFactory parent class and failed for the
// non-editor target. These are now cross-module includes and need the folder prefix.
#include "Dialogue/GF_DialogueAsset.h"
#include "Dialogue/GF_DialogueParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "PackageTools.h"

UGF_DialogueAssetFactory::UGF_DialogueAssetFactory()
{
    SupportedClass = UGF_DialogueAsset::StaticClass();
    bCreateNew     = false;
    bEditorImport  = true;
    bText          = true;
    Formats.Add(TEXT("gfdlg;Gamma Framework Dialogue Script"));
    Formats.Add(TEXT("gedlg;Gamma Framework Dialogue Script (legacy extension)"));
}

// ============================================================
// IMPORT
// ============================================================

bool UGF_DialogueAssetFactory::FactoryCanImport(const FString& Filename)
{
    const FString Ext = FPaths::GetExtension(Filename);
    return Ext.Equals(TEXT("gfdlg"), ESearchCase::IgnoreCase)
        || Ext.Equals(TEXT("gedlg"), ESearchCase::IgnoreCase);
}

UObject* UGF_DialogueAssetFactory::FactoryCreateFile(UClass* InClass, UObject* InParent,
                                                      FName InName, EObjectFlags Flags,
                                                      const FString& Filename,
                                                      const TCHAR* Parms,
                                                      FFeedbackContext* Warn,
                                                      bool& bOutOperationCanceled)
{
    FString FileText;
    if (!FFileHelper::LoadFileToString(FileText, *Filename))
    {
        UE_LOG(LogTemp, Error, TEXT("GF_DialogueAssetFactory: Could not read '%s'"), *Filename);
        return nullptr;
    }

    // Check for multi-section file (@dialogue blocks)
    TArray<FGF_DialogueSection> Sections = FGF_DialogueParser::SplitIntoSections(FileText);

    if (Sections.Num() == 0)
    {
        // ── Single-section file ──────────────────────────────────────────────
        UGF_DialogueAsset* Asset = NewObject<UGF_DialogueAsset>(InParent, InName, Flags);
        if (!ImportFromText(Asset, FileText, Filename))
        {
            return nullptr;
        }
        bOutOperationCanceled = false;
        return Asset;
    }

    // ── Multi-section file — one asset per @dialogue block ───────────────────

    // The package path where siblings will be created (same folder as the import destination)
    const FString PackagePath = FPackageName::GetLongPackagePath(
        InParent->GetOutermost()->GetName());

    UGF_DialogueAsset* FirstAsset = nullptr;

    for (int32 i = 0; i < Sections.Num(); ++i)
    {
        const FGF_DialogueSection& Section = Sections[i];
        const FString AssetName = Section.DialogueID;

        UGF_DialogueAsset* Asset = nullptr;

        if (i == 0)
        {
            // The first asset uses the object UBT already set up for us (InParent, InName)
            // Override the name with the section's DialogueID
            Asset = NewObject<UGF_DialogueAsset>(InParent,
                *UPackageTools::SanitizePackageName(AssetName), Flags);
            FirstAsset = Asset;
        }
        else
        {
            // Subsequent sections: create their own packages as siblings
            const FString PackageName = PackagePath / UPackageTools::SanitizePackageName(AssetName);
            UPackage* Package = CreatePackage(*PackageName);
            Package->FullyLoad();

            Asset = NewObject<UGF_DialogueAsset>(Package,
                *UPackageTools::SanitizePackageName(AssetName),
                Flags | RF_Public | RF_Standalone);
        }

        if (!Asset || !ImportFromText(Asset, Section.SectionText, Filename))
        {
            UE_LOG(LogTemp, Error, TEXT("GF_DialogueAssetFactory: Failed to parse section '%s'"), *AssetName);
            continue;
        }

        // For sibling assets, notify the asset registry and save
        if (i > 0)
        {
            FAssetRegistryModule::AssetCreated(Asset);
            Asset->MarkPackageDirty();

            const FString PackageFilename = FPackageName::LongPackageNameToFilename(
                Asset->GetOutermost()->GetName(),
                FPackageName::GetAssetPackageExtension());

            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            UPackage::SavePackage(Asset->GetOutermost(), Asset, *PackageFilename, SaveArgs);
        }
    }

    bOutOperationCanceled = false;
    return FirstAsset;
}

// ============================================================
// REIMPORT
// ============================================================

bool UGF_DialogueAssetFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
    UGF_DialogueAsset* Asset = Cast<UGF_DialogueAsset>(Obj);
    if (Asset && !Asset->SourceFilePath.IsEmpty())
    {
        OutFilenames.Add(Asset->SourceFilePath);
        return true;
    }
    return false;
}

void UGF_DialogueAssetFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
    UGF_DialogueAsset* Asset = Cast<UGF_DialogueAsset>(Obj);
    if (Asset && NewReimportPaths.Num() > 0)
    {
        Asset->SourceFilePath = NewReimportPaths[0];
    }
}

EReimportResult::Type UGF_DialogueAssetFactory::Reimport(UObject* Obj)
{
    UGF_DialogueAsset* Asset = Cast<UGF_DialogueAsset>(Obj);
    if (!Asset || Asset->SourceFilePath.IsEmpty())
    {
        return EReimportResult::Failed;
    }

    if (!FPaths::FileExists(Asset->SourceFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("GF_DialogueAssetFactory: Source file not found: %s"), *Asset->SourceFilePath);
        return EReimportResult::Failed;
    }

    FString FileText;
    if (!FFileHelper::LoadFileToString(FileText, *Asset->SourceFilePath))
    {
        return EReimportResult::Failed;
    }

    TArray<FGF_DialogueSection> Sections = FGF_DialogueParser::SplitIntoSections(FileText);

    if (Sections.Num() == 0)
    {
        // Single-section — reimport directly
        return ImportFromText(Asset, FileText, Asset->SourceFilePath)
            ? EReimportResult::Succeeded : EReimportResult::Failed;
    }

    // Multi-section — find and reimport only the matching section.
    // Match by DialogueID first; fall back to asset name in case DialogueID was wiped.
    const FString ThisID   = Asset->DialogueID.ToString();
    const FString ThisName = Asset->GetName();

    for (const FGF_DialogueSection& Section : Sections)
    {
        const bool bMatchID   = !ThisID.IsEmpty() &&
            Section.DialogueID.Equals(ThisID, ESearchCase::IgnoreCase);
        const bool bMatchName = Section.DialogueID.Equals(ThisName, ESearchCase::IgnoreCase);

        if (bMatchID || bMatchName)
        {
            return ImportFromText(Asset, Section.SectionText, Asset->SourceFilePath)
                ? EReimportResult::Succeeded : EReimportResult::Failed;
        }
    }

    UE_LOG(LogTemp, Error, TEXT("GF_DialogueAssetFactory: Could not find section '%s' in '%s' during reimport"),
        *ThisName, *Asset->SourceFilePath);
    return EReimportResult::Failed;
}

// ============================================================
// SHARED IMPORT LOGIC
// ============================================================

bool UGF_DialogueAssetFactory::ImportFromText(UGF_DialogueAsset* Asset,
                                               const FString& FileText,
                                               const FString& SourcePath)
{
    Asset->SourceFilePath = SourcePath;

    // Default DialogueID to filename stem — ParseIntoAsset will override if @id is present
    Asset->DialogueID = FName(*FPaths::GetBaseFilename(SourcePath));

    if (!FGF_DialogueParser::ParseIntoAsset(FileText, Asset))
    {
        return false;
    }

    Asset->MarkPackageDirty();
    return true;
}

#endif // WITH_EDITOR
