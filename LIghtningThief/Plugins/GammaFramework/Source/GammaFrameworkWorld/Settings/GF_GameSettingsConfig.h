#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GF_SettingsTypes.h"
#include "GF_GameSettingsConfig.generated.h"

class USoundClass;
class USoundMix;

// Project-wide configuration behind the options screen.
// Edit in Project Settings > Game > Gamma Framework Settings.
//
// Nothing here is player state. The player's chosen values live in
// UGF_SettingsSubsystem / its "GEOptions" save slot; this asset only describes
// what the options MEAN (which textures, how fast "Fast" is, what Hard does).
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Gamma Framework Settings"))
class GAMMAFRAMEWORKWORLD_API UGF_GameSettingsConfig : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGF_GameSettingsConfig();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	// Never null - falls back to the CDO.
	static const UGF_GameSettingsConfig* Get();

	// --------------------------------------------------------
	// DIALOGUE FRAMES
	// --------------------------------------------------------

	// Every dialogue box skin the player can choose between.
	// The saved FrameID is an index into this array, so only ever append.
	UPROPERTY(config, EditAnywhere, Category = "Dialogue Frames")
	TArray<FGF_DialogueFrame> DialogueFrames;

	// --------------------------------------------------------
	// TEXT SPEED
	// --------------------------------------------------------
	// Visible characters revealed per second for each speed setting.
	// Instant is not listed - it bypasses the typewriter entirely.

	UPROPERTY(config, EditAnywhere, Category = "Text Speed", meta = (ClampMin = "1.0"))
	float SlowCharsPerSecond = 15.0f;

	UPROPERTY(config, EditAnywhere, Category = "Text Speed", meta = (ClampMin = "1.0"))
	float NormalCharsPerSecond = 30.0f;

	UPROPERTY(config, EditAnywhere, Category = "Text Speed", meta = (ClampMin = "1.0"))
	float FastCharsPerSecond = 60.0f;

	// --------------------------------------------------------
	// AUDIO
	// --------------------------------------------------------
	// Volume sliders are applied with SetSoundMixClassOverride, which needs one
	// sound mix to hang the overrides on plus the sound classes to override.
	// Defaults point at the existing /Game/SOUNDS assets.

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundMix> SettingsSoundMix;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundClass> MasterSoundClass;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundClass> MusicSoundClass;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundClass> SFXSoundClass;

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundClass> VoiceSoundClass;

	// Cries and other voice-class audio track the SFX slider. Turn off to leave
	// the voice class alone.
	UPROPERTY(config, EditAnywhere, Category = "Audio")
	bool bVoiceFollowsSFXVolume = true;

	// Seconds the volume change fades over. Keep small so sliders feel live.
	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0"))
	float VolumeFadeTime = 0.1f;

	// --------------------------------------------------------
	// VIDEO
	// --------------------------------------------------------

	// Resolutions offered in the options screen, "WIDTHxHEIGHT".
	// Validated against EnforcedAspectRatio on startup - entries that do not
	// match are logged and dropped from the menu.
	UPROPERTY(config, EditAnywhere, Category = "Video")
	TArray<FString> SelectableResolutions;

	// When true, any resolution that is not EnforcedAspectRatio is refused,
	// whichever code path asks for it.
	UPROPERTY(config, EditAnywhere, Category = "Video")
	bool bEnforceAspectRatio = true;

	// Width divided by height. 16:9 = 1.777778, 16:10 = 1.6, 21:9 = 2.370.
	UPROPERTY(config, EditAnywhere, Category = "Video", meta = (EditCondition = "bEnforceAspectRatio", ClampMin = "0.1"))
	float EnforcedAspectRatio = 16.0f / 9.0f;

	// How far from EnforcedAspectRatio still counts as a match. 0.01 accepts
	// every common 16:9 mode while still rejecting 16:10 and 21:9.
	UPROPERTY(config, EditAnywhere, Category = "Video", meta = (EditCondition = "bEnforceAspectRatio", ClampMin = "0.0"))
	float AspectRatioTolerance = 0.01f;

	// Used when a saved or requested resolution is rejected and the configured
	// list has no valid entry to fall back to.
	UPROPERTY(config, EditAnywhere, Category = "Video")
	FString FallbackResolution = TEXT("1920x1080");

	// Console variable value used for r.DepthOfFieldQuality when DOF is enabled.
	UPROPERTY(config, EditAnywhere, Category = "Video", meta = (ClampMin = "1", ClampMax = "4"))
	int32 DepthOfFieldQualityWhenEnabled = 2;

	// --------------------------------------------------------
	// DIFFICULTY
	// --------------------------------------------------------

	UPROPERTY(config, EditAnywhere, Category = "Difficulty")
	FGF_DifficultyProfile EasyProfile;

	UPROPERTY(config, EditAnywhere, Category = "Difficulty")
	FGF_DifficultyProfile NormalProfile;

	UPROPERTY(config, EditAnywhere, Category = "Difficulty")
	FGF_DifficultyProfile HardProfile;

	// Returns the profile for a difficulty. Never fails.
	const FGF_DifficultyProfile& GetProfile(EGF_Difficulty Difficulty) const;
};
