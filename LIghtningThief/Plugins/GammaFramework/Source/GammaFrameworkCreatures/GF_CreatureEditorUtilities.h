#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureEditorUtilities.generated.h"

UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureEditorUtilities : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Creature|Editor", meta = (DevelopmentOnly))
	static UObject* CreateCreatureSpeciesAsset(const FString& AssetName, const FString& PackagePath);
};