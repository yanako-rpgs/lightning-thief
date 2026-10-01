#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Factories/Factory.h"
#include "EditorReimportHandler.h"
#include "GF_DialogueAssetFactory.generated.h"

/**
 * GF_DialogueAssetFactory
 *
 * Registers the .gfdlg extension with the Unreal asset pipeline.
 * Drag a .gfdlg file into the Content Browser to import it as a UGF_DialogueAsset.
 * Right-click → Reimport to re-parse the source file after edits.
 */
UCLASS()
class UGF_DialogueAssetFactory : public UFactory, public FReimportHandler
{
    GENERATED_BODY()

public:
    UGF_DialogueAssetFactory();

    // UFactory interface
    virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent,
                                       FName InName, EObjectFlags Flags,
                                       const FString& Filename,
                                       const TCHAR* Parms,
                                       FFeedbackContext* Warn,
                                       bool& bOutOperationCanceled) override;

    virtual bool FactoryCanImport(const FString& Filename) override;

    // FReimportHandler interface
    virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
    virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
    virtual EReimportResult::Type Reimport(UObject* Obj) override;
    virtual int32 GetPriority() const override { return ImportPriority; }

private:
    bool ImportFromText(class UGF_DialogueAsset* Asset, const FString& FileText, const FString& SourcePath);
};

#endif // WITH_EDITOR
