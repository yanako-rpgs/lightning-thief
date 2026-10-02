#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GF_SettingsTypes.h"
#include "GF_GameSettingsConfig.generated.h"

class USoundClass;
class USoundMix;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Gamma Framework Settings"))
class GAMMAFRAMEWORKWORLD_API UGF_GameSettingsConfig : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGF_GameSettingsConfig();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	// Never null - falls back to the CDO.
	static const UGF_GameSettingsConfig* Get();

	UPROPERTY(config, EditAnywhere, Category = "Dialogue Frames")
	TArray<FGF_DialogueFrame> DialogueFrames;

	UPROPERTY(config, EditAnywhere, Category = "Text Speed", meta = (ClampMin = "1.0"))
	float SlowCharsPerSecond = 15.0f;

	UPROPERTY(config, EditAnywhere, Category = "Text Speed", meta = (ClampMin = "1.0"))
	float NormalCharsPerSecond = 30.0f;

	UPROPERTY(config, EditAnywhere, Category = "Text Speed", meta = (ClampMin = "1.0"))
	float FastCharsPerSecond = 60.0f;

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

	UPROPERTY(config, EditAnywhere, Category = "Audio")
	bool bVoiceFollowsSFXVolume = true;

	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0"))
	float VolumeFadeTime = 0.1f;

	UPROPERTY(config, EditAnywhere, Category = "Video")
	TArray<FString> SelectableResolutions;

	UPROPERTY(config, EditAnywhere, Category = "Video")
	bool bEnforceAspectRatio = true;

	UPROPERTY(config, EditAnywhere, Category = "Video", meta = (EditCondition = "bEnforceAspectRatio", ClampMin = "0.1"))
	float EnforcedAspectRatio = 16.0f / 9.0f;

	UPROPERTY(config, EditAnywhere, Category = "Video", meta = (EditCondition = "bEnforceAspectRatio", ClampMin = "0.0"))
	float AspectRatioTolerance = 0.01f;

	UPROPERTY(config, EditAnywhere, Category = "Video")
	FString FallbackResolution = TEXT("1920x1080");

	UPROPERTY(config, EditAnywhere, Category = "Video", meta = (ClampMin = "1", ClampMax = "4"))
	int32 DepthOfFieldQualityWhenEnabled = 2;

	UPROPERTY(config, EditAnywhere, Category = "Difficulty")
	FGF_DifficultyProfile EasyProfile;

	UPROPERTY(config, EditAnywhere, Category = "Difficulty")
	FGF_DifficultyProfile NormalProfile;

	UPROPERTY(config, EditAnywhere, Category = "Difficulty")
	FGF_DifficultyProfile HardProfile;

	// Returns the profile for a difficulty. Never fails.
	const FGF_DifficultyProfile& GetProfile(EGF_Difficulty Difficulty) const;
};
