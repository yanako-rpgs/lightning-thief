// Gamma Framework -- how menu input is damped.
//
// Project Settings -> Game -> Gamma Framework Menu.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "GF_MenuSettings.generated.h"

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Gamma Framework Menu"))
class GAMMAFRAMEWORKWORLD_API UGF_MenuSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGF_MenuSettings();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	/** Never null -- falls back to the CDO. */
	static const UGF_MenuSettings* Get();

	/**
	 * Shortest gap between two damped presses, in seconds.
	 *
	 * Presses closer together than this never reach a widget at all. It exists
	 * because a player mashing through a menu's opening animation can otherwise
	 * land on a button that is still arriving, and open two things at once.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Input",
	          meta = (ClampMin = "0.0", UIMax = "0.5"))
	float MenuInputCooldown = 0.15f;

	/**
	 * The keys the cooldown applies to. Everything else is untouched.
	 *
	 * A list rather than an exclusion, because the keys that commit something --
	 * confirm, cancel, a mouse click -- are few and known, while the keys that
	 * must stay responsive are every direction on every device. Damping
	 * navigation makes menus feel broken rather than protected: a list is walked
	 * by tapping a direction repeatedly, and each tap is a fresh press.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Input")
	TArray<FKey> DampedKeys;
};
