#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GF_SettingsTypes.h"
#include "GF_SettingsSave.generated.h"

/**
 * Options save. Deliberately a separate slot from "CreatureSaveSlot" so the
 * player's volume / resolution / text speed survive New Game and Delete Save,
 * and so the options screen works on the title screen before any game save has
 * been loaded.
 *
 * UGF_SettingsSubsystem mirrors every field onto UGF_VaultSystem after load,
 * so Blueprints that already read VaultSystem->TextSpeed etc. keep working.
 */
UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_SettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	static const FString SlotName;
	static const int32   UserIndex = 0;

	// Fast by default — the intended reading pace for the game. Slow/Normal exist
	// for players who want them, not as the out-of-the-box experience.
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	EGF_TextSpeed TextSpeed = EGF_TextSpeed::Fast;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	int32 FrameID = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	EGF_Difficulty Difficulty = EGF_Difficulty::Normal;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	EGF_WindowMode WindowMode = EGF_WindowMode::Fullscreen;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	FString Resolution = TEXT("1920x1080");

	// Frames per second, or 0 for unlimited. Defaults to 60: this is a fixed-camera 2D
	// game that will happily render 180+ fps, which on a laptop or handheld just means
	// heat, fan noise and battery drain for no visible benefit.
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	int32 FrameRateLimit = 60;

	// On by default. Scrolling a tile grid past the screen edge is about the worst case
	// there is for tearing, and the input latency VSync adds does not matter in a game
	// with grid-quantised movement.
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	bool bVSyncEnabled = true;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float MasterVolume = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float SoundEffectsVolume = 0.75f;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float MusicVolume = 0.75f;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	bool bEXPShareEnabled = true;

	// On by default: depth of field is part of the game's art direction, not an
	// opt-in extra. The option exists so players can turn it off for performance
	// or comfort — it must never be off just because nobody touched the menu.
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	bool bDOFEnabled = true;

	// When a Repel runs out, automatically spend another one from the bag instead
	// of dropping the player back to encounters. On by default: it is pure QoL and
	// only ever consumes an item the player already chose to carry. Turning it off
	// restores the vanilla behaviour of the Repel simply expiring.
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	bool bAutoRepelEnabled = true;
};
