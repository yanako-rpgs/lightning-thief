// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_GameVersion.generated.h"

/**
 * The build number, in one place that C++ can actually read.
 *
 * WHY THIS EXISTS
 * ---------------
 * The version used to live in exactly two places, neither reachable from code:
 * a hand-typed literal inside W_GF_PressStart, and Scripts/patch_version.txt for
 * the build scripts. The consequence was that nothing the game produced -- log
 * lines, the diagnostics file testers are asked to send, a crash report, a
 * network request -- could say which build produced it. Every bug report then
 * started with a round trip to establish what the tester was even running.
 *
 * Putting it in ini fixes that for every one of those at once, and lets the
 * main menu bind to a value rather than carry a literal somebody has to remember
 * to retype each release.
 *
 * KEEPING IT HONEST
 * -----------------
 * This is now the source of truth for the DISPLAYED version.
 * Scripts/patch_version.txt is still what Ship_Patch.bat reads, so until that
 * script is taught to write this key, the two are updated by hand and can
 * drift. A version number that lies is worse than no version number, so change
 * them together.
 *
 * This is NOT a compatibility gate. Nothing should branch on it. Feature gating
 * has its own deliberately separate numbers -- UGF_CreatureCodec::TradeContentVersion
 * for whether asset paths still resolve, and UGF_GiftSubsystem::GiftClientVersion
 * for whether the client has the code a gift needs. Both are decoupled from the
 * build number on purpose, so that a cosmetic patch does not break trading.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Gamma Framework Version"))
class GAMMAFRAMEWORKWORLD_API UGF_GameVersionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	/** Never null -- falls back to the CDO. */
	static const UGF_GameVersionSettings* Get();

	/**
	 * Displayed build number, e.g. "1.13.1". Free-form: nothing parses it, and
	 * nothing should start.
	 */
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
