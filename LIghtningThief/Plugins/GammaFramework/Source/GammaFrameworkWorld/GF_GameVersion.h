// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_GameVersion.generated.h"

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Gamma Framework Version"))
class GAMMAFRAMEWORKWORLD_API UGF_GameVersionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	/** Never null -- falls back to the CDO. */
	static const UGF_GameVersionSettings* Get();

	UPROPERTY(config, EditAnywhere, Category = "Version")
	FString BuildVersion = TEXT("0.0.0");
};

/**
 * Blueprint access to the build number, so the main menu can bind to it instead
 * of carrying a literal.
 */
UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_GameVersionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The raw version, e.g. "1.13.1". */
	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Version")
	static FString GetBuildVersion();

	/** Ready to drop on a menu, e.g. "v1.13.1". */
	UFUNCTION(BlueprintPure, Category = "Gamma Framework|Version")
	static FText GetBuildVersionDisplayText();
};
