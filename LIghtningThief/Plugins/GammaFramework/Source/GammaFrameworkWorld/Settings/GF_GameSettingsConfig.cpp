#include "GF_GameSettingsConfig.h"

UGF_GameSettingsConfig::UGF_GameSettingsConfig()
{
	// Point the audio sliders at the sound classes/mixes that already exist in
	// /Game/SOUNDS. These are defaults only - override per project in the
	// Project Settings page if the assets ever move.
	SettingsSoundMix  = TSoftObjectPtr<USoundMix>(FSoftObjectPath(TEXT("/Game/SOUNDS/Mix_GF_Master.Mix_GF_Master")));
	MasterSoundClass  = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/SOUNDS/SC_MASTER.SC_MASTER")));
	MusicSoundClass   = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/SOUNDS/SC_MUSIC.SC_MUSIC")));
	SFXSoundClass     = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/SOUNDS/SC_SFX.SC_SFX")));
	VoiceSoundClass   = TSoftObjectPtr<USoundClass>(FSoftObjectPath(TEXT("/Game/SOUNDS/SC_VOICE.SC_VOICE")));

	SelectableResolutions = {
		TEXT("1280x720"),
		TEXT("1600x900"),
		TEXT("1920x1080"),
		TEXT("2560x1440"),
		TEXT("3840x2160")
	};

	// ---- Difficulty defaults ----
	// Level caps track the trial leader aces, so a capped run arrives at each
	// trial roughly level with the leader instead of steamrolling it.
	static const TArray<int32> DefaultCaps = { 15, 19, 24, 29, 31, 33, 42, 46, 58 };

	EasyProfile.EXPMultiplier        = 1.5f;
	EasyProfile.AIDifficultyOffset   = -1;
	EasyProfile.bEnforceLevelCap     = false;
	EasyProfile.bAllowEXPShareToggle = false;
	EasyProfile.bForcedEXPShare      = true;
	EasyProfile.EXPShareRatio        = 0.5f;

	NormalProfile.EXPMultiplier        = 1.0f;
	NormalProfile.AIDifficultyOffset   = 0;
	NormalProfile.bEnforceLevelCap     = false;
	NormalProfile.bAllowEXPShareToggle = true;
	NormalProfile.EXPShareRatio        = 0.25f;

	HardProfile.EXPMultiplier          = 1.0f;
	HardProfile.AIDifficultyOffset     = 1;
	HardProfile.bEnforceLevelCap       = true;
	HardProfile.LevelCapPerEmblemCount  = DefaultCaps;
	HardProfile.bAllowEXPShareToggle   = false;
	HardProfile.bForcedEXPShare        = false;
	HardProfile.EXPShareRatio          = 0.0f;
}

const UGF_GameSettingsConfig* UGF_GameSettingsConfig::Get()
{
	const UGF_GameSettingsConfig* Config = GetDefault<UGF_GameSettingsConfig>();
	return Config ? Config : GetMutableDefault<UGF_GameSettingsConfig>();
}

const FGF_DifficultyProfile& UGF_GameSettingsConfig::GetProfile(EGF_Difficulty Difficulty) const
{
	switch (Difficulty)
	{
	case EGF_Difficulty::Easy: return EasyProfile;
	case EGF_Difficulty::Hard: return HardProfile;
	case EGF_Difficulty::Normal:
	default:                   return NormalProfile;
	}
}
