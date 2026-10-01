#include "GF_CreatureEditorUtilities.h"
#include "GF_CreatureSpeciesData.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "UObject/SavePackage.h"
#endif

UObject* UGF_CreatureEditorUtilities::CreateCreatureSpeciesAsset(const FString& AssetName, const FString& PackagePath)
{
#if WITH_EDITOR
	FString FullPackagePath = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*FullPackagePath);

	UGF_CreatureSpeciesData* NewAsset = NewObject<UGF_CreatureSpeciesData>(
		Package,
		UGF_CreatureSpeciesData::StaticClass(),
		*AssetName,
		RF_Public | RF_Standalone
	);

	if (NewAsset)
	{
		// Set default values
		NewAsset->SpeciesName = FName(*AssetName.Replace(TEXT("DA_"), TEXT("")));

		// Mark package as dirty and save
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(NewAsset);

		// Save the package
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		FString PackageFileName = FPackageName::LongPackageNameToFilename(
			FullPackagePath,
			FPackageName::GetAssetPackageExtension()
		);
		UPackage::SavePackage(Package, NewAsset, *PackageFileName, SaveArgs);

		return NewAsset;
	}
#endif
	return nullptr;
}