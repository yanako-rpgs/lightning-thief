#include "GF_SettingsSubsystem.h"

#include "GF_SettingsSave.h"
#include "GF_GameSettingsConfig.h"
#include "GF_CreatureBridge.h"
// The settings mirror is pushed through FGF_CreatureBridge.

#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameUserSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "AudioDevice.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Styling/SlateColor.h"
#include "TimerManager.h"

namespace
{
	// Seconds of quiet before a changed setting is written to disk. Long enough
	// that dragging a volume slider is a single write, short enough that alt-F4
	// straight after a change still keeps it.
	constexpr float SaveDebounceSeconds = 0.75f;
}

// Diagnostic: what the AUDIO ENGINE actually resolved, as opposed to what we
// asked for. ApplyAudio logging only proves the request was made - this proves
// whether it took effect. Run "gf.DumpAudio" while the sound in question plays.
static FAutoConsoleCommandWithWorld GGEDumpAudio(
	TEXT("gf.DumpAudio"),
	TEXT("Log the sound class volumes the audio engine is currently using."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogTemp, Warning, TEXT("GF_Audio: dump - no world."));
			return;
		}

		FAudioDevice* AudioDevice = World->GetAudioDeviceRaw();
		if (!AudioDevice)
		{
			UE_LOG(LogTemp, Warning, TEXT("GF_Audio: dump - no audio device."));
			return;
		}

		const UGF_GameSettingsConfig* Config = UGF_GameSettingsConfig::Get();

		auto Dump = [AudioDevice](const TSoftObjectPtr<USoundClass>& SoftClass, const TCHAR* Label)
		{
			USoundClass* Class = SoftClass.LoadSynchronous();
			if (!Class)
			{
				UE_LOG(LogTemp, Warning, TEXT("GF_Audio: dump - %s class missing."), Label);
				return;
			}

			// Asset default vs what the device resolved after mixes/overrides.
			const float AssetVolume = Class->Properties.Volume;

			if (FSoundClassProperties* Props = AudioDevice->GetSoundClassCurrentProperties(Class))
			{
				UE_LOG(LogTemp, Warning, TEXT("GF_Audio: dump - %-6s (%s) asset=%.3f RESOLVED=%.3f"),
					Label, *Class->GetName(), AssetVolume, Props->Volume);
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("GF_Audio: dump - %-6s (%s) asset=%.3f but the device has NO properties for it "
						 "(nothing on this class is registered)."),
					Label, *Class->GetName(), AssetVolume);
			}
		};

		Dump(Config->MasterSoundClass, TEXT("Master"));
		Dump(Config->MusicSoundClass,  TEXT("Music"));
		Dump(Config->SFXSoundClass,    TEXT("SFX"));
		Dump(Config->VoiceSoundClass,  TEXT("Voice"));
	}));

// ============================================================
// LIFECYCLE
// ============================================================

void UGF_SettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The mirror used to force the creature manager to initialise first. It no
	// longer can -- that would be a Core -> Creatures dependency. The direction is
	// inverted instead: GammaFrameworkCreatures declares a dependency on THIS
	// subsystem and calls RequestVaultSync() once its hooks are bound, which
	// re-pushes the mirror. Until then PushSettingsMirror is unbound and
	// SyncToVaultSystem is a no-op, exactly as it was when the manager was absent.

	Super::Initialize(Collection);

	ReloadSettings();

	// Sound mix overrides are per-world and do not survive a level transition,
	// so re-apply the audio settings every time a world spins up.
	FWorldDelegates::OnPostWorldInitialization.AddWeakLambda(this,
		[this](UWorld* /*World*/, const UWorld::InitializationValues /*POTENTIALS*/)
		{
			// A widget torn down mid-populate could leave the guard armed, which
			// would silently swallow every later settings change. A level change
			// is a safe point to assume no options screen is mid-populate.
			ClearLoadingUIGuard();

			// Same reasoning for ducks: whatever pushed one (a pickup widget, a
			// heal sequence) did not survive the level change, so its pop is
			// never coming. Starting the new world at full saved volume is the
			// only safe read.
			ActiveDucks.Reset();

			// Not ApplyAudio() directly: the world exists here but its audio
			// device does not yet, and the engine's mix calls no-op without one.
			ApplyAudioWhenReady();
			ApplyDepthOfField();
		});
}

void UGF_SettingsSubsystem::Deinitialize()
{
	FWorldDelegates::OnPostWorldInitialization.RemoveAll(this);

	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().ClearTimer(SaveDebounceHandle);
		GI->GetTimerManager().ClearTimer(AudioGuardHandle);
	}

	// Flush any change that was still inside the debounce window.
	SaveSettings();

	Super::Deinitialize();
}

// ============================================================
// PERSISTENCE
// ============================================================

void UGF_SettingsSubsystem::ReloadSettings()
{
	Save = nullptr;

	if (UGameplayStatics::DoesSaveGameExist(UGF_SettingsSave::SlotName, UGF_SettingsSave::UserIndex))
	{
		Save = Cast<UGF_SettingsSave>(
			UGameplayStatics::LoadGameFromSlot(UGF_SettingsSave::SlotName, UGF_SettingsSave::UserIndex));
	}

	if (!Save)
	{
		Save = Cast<UGF_SettingsSave>(
			UGameplayStatics::CreateSaveGameObject(UGF_SettingsSave::StaticClass()));
		UE_LOG(LogTemp, Warning, TEXT("GF_Audio: no options save found, using DEFAULTS."));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GF_Audio: loaded options save - Master=%.2f Music=%.2f SFX=%.2f"),
			Save->MasterVolume, Save->MusicVolume, Save->SoundEffectsVolume);
	}

	// Warn once about config entries the aspect ratio guard will hide, rather
	// than leaving them to vanish from the menu with no explanation.
	for (const FString& Option : UGF_GameSettingsConfig::Get()->SelectableResolutions)
	{
		if (!IsResolutionAllowed(Option))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("GF_Settings: SelectableResolutions entry '%s' is not the enforced aspect ratio "
					 "and will not appear in the options screen."), *Option);
		}
	}

	SanitizeResolution();

	ApplyAllSettings();
}

void UGF_SettingsSubsystem::SanitizeResolution()
{
	if (!Save || IsResolutionAllowed(Save->Resolution))
	{
		return;
	}

	const UGF_GameSettingsConfig* Config = UGF_GameSettingsConfig::Get();

	// Prefer a real menu entry so the options screen can highlight the active
	// row; fall back to the configured default only if the list is unusable.
	const TArray<FString> Options = GetSelectableResolutions();
	const FString Replacement = (Options.Num() > 0) ? Options[0] : Config->FallbackResolution;

	UE_LOG(LogTemp, Warning,
		TEXT("GF_Settings: saved resolution '%s' is not allowed, falling back to '%s'."),
		*Save->Resolution, *Replacement);

	Save->Resolution = Replacement;
}

bool UGF_SettingsSubsystem::SaveSettings()
{
	if (!Save)
	{
		return false;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().ClearTimer(SaveDebounceHandle);
	}

	const bool bSuccess = UGameplayStatics::SaveGameToSlot(
		Save, UGF_SettingsSave::SlotName, UGF_SettingsSave::UserIndex);

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("GF_Settings: failed to write options to slot '%s'"),
			*UGF_SettingsSave::SlotName);
	}

	return bSuccess;
}

void UGF_SettingsSubsystem::ScheduleSave()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		SaveSettings();
		return;
	}

	FTimerManager& TimerManager = GI->GetTimerManager();
	TimerManager.ClearTimer(SaveDebounceHandle);
	TimerManager.SetTimer(
		SaveDebounceHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]() { SaveSettings(); }),
		SaveDebounceSeconds,
		false);
}

void UGF_SettingsSubsystem::ResetToDefaults()
{
	Save = Cast<UGF_SettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UGF_SettingsSave::StaticClass()));

	// The struct default is 16:9, but the enforced ratio is configurable - do not
	// assume the two agree.
	SanitizeResolution();

	ApplyAllSettings();
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::ApplyAllSettings()
{
	ApplyAudioWhenReady();
	ApplyVideo();
	ApplyDepthOfField();
	ApplyFrameRateLimit();
	ApplyVSync();
	SyncToVaultSystem();

	OnSettingsChanged.Broadcast();
}

void UGF_SettingsSubsystem::HandleSettingChanged()
{
	SyncToVaultSystem();
	OnSettingsChanged.Broadcast();
	ScheduleSave();
}

// ============================================================
// BOX SYSTEM MIRROR
// ============================================================

void UGF_SettingsSubsystem::RequestVaultSync()
{
	SyncToVaultSystem();
}

void UGF_SettingsSubsystem::SyncToVaultSystem()
{
	// No-op until GammaFrameworkCreatures binds the hook. Same observable
	// behaviour as the old "no vault yet, return early" path.
	if (!Save || !FGF_CreatureBridge::PushSettingsMirror)
	{
		return;
	}

	FGF_SettingsMirror Mirror;
	Mirror.TextSpeed          = TextSpeedToString(Save->TextSpeed);
	Mirror.FrameID            = Save->FrameID;
	Mirror.Difficulty         = DifficultyToString(Save->Difficulty);
	Mirror.WindowedModes      = WindowModeToString(Save->WindowMode);
	Mirror.Resolution         = Save->Resolution;
	Mirror.MasterVolume       = Save->MasterVolume;
	Mirror.SoundEffectsVolume = Save->SoundEffectsVolume;
	Mirror.MusicVolume        = Save->MusicVolume;
	Mirror.bEXPShareEnabled   = IsEXPShareActive();
	Mirror.bDOFEnabled        = Save->bDOFEnabled;
	Mirror.bAutoRepelEnabled  = Save->bAutoRepelEnabled;

	FGF_CreatureBridge::PushSettingsMirror(this, Mirror);
}

// ============================================================
// APPLY
// ============================================================

bool UGF_SettingsSubsystem::ApplyAudio(float FadeTimeOverride)
{
	if (!Save)
	{
		return false;
	}

	const UGF_GameSettingsConfig* Config = UGF_GameSettingsConfig::Get();

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		// No world yet (very early startup). OnPostWorldInitialization retries.
		UE_LOG(LogTemp, Warning, TEXT("GF_Audio: skipped - no world yet."));
		return false;
	}

	// The engine's sound mix calls bail out silently on all three of these, so
	// check them ourselves - otherwise "applied" is a lie and the retry timer
	// stops before the volume has actually landed.
	if (!GEngine || !GEngine->UseSound() || !World->bAllowAudioPlayback || !World->GetAudioDeviceRaw())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GF_Audio: skipped - world=%s UseSound=%d bAllowAudioPlayback=%d AudioDevice=%d"),
			*World->GetName(),
			GEngine ? (int32)GEngine->UseSound() : -1,
			(int32)World->bAllowAudioPlayback,
			World->GetAudioDeviceRaw() ? 1 : 0);
		return false;
	}

	USoundMix* Mix = Config->SettingsSoundMix.LoadSynchronous();
	if (!Mix)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GF_Settings: no SettingsSoundMix configured - volume sliders will do nothing. "
				 "Set it in Project Settings > Game > Gamma Framework Settings."));
		return false;
	}

	const float FadeTime = FadeTimeOverride >= 0.f
		? FadeTimeOverride
		: FMath::Max(0.f, Config->VolumeFadeTime);

	// Diagnostics - drop to Verbose once the volume-on-boot issue is closed out.
	UE_LOG(LogTemp, Warning,
		TEXT("GF_Audio: ApplyAudio world=%s mix=%s fade=%.2f saved(Master=%.2f Music=%.2f SFX=%.2f)"),
		*World->GetName(), *Mix->GetName(), FadeTime,
		Save->MasterVolume, Save->MusicVolume, Save->SoundEffectsVolume);

	auto ApplyClass = [&](const TSoftObjectPtr<USoundClass>& SoftClass, float Volume, const TCHAR* Label)
	{
		USoundClass* Class = SoftClass.LoadSynchronous();
		if (!Class)
		{
			UE_LOG(LogTemp, Warning, TEXT("GF_Audio:   %s class FAILED to resolve (%s)"),
				Label, *SoftClass.ToString());
			return;
		}

		const float Final = FMath::Clamp(Volume, 0.f, 1.f);
		UGameplayStatics::SetSoundMixClassOverride(World, Mix, Class, Final, 1.0f, FadeTime, true);
		UE_LOG(LogTemp, Warning, TEXT("GF_Audio:   %s -> %.3f on class %s"), Label, Final, *Class->GetName());
	};

	// Ducking multiplies the SAVED volume rather than replacing it, so a slider
	// at 0 stays silent while ducked and comes back to exactly 0 when it lifts.
	const float MusicDuck = GetMusicDuckMultiplier();
	const float SFXDuck   = GetSFXDuckMultiplier();

	ApplyClass(Config->MasterSoundClass, Save->MasterVolume,               TEXT("Master"));
	ApplyClass(Config->MusicSoundClass,  Save->MusicVolume * MusicDuck,    TEXT("Music"));
	ApplyClass(Config->SFXSoundClass,    Save->SoundEffectsVolume * SFXDuck, TEXT("SFX"));

	if (Config->bVoiceFollowsSFXVolume)
	{
		ApplyClass(Config->VoiceSoundClass, Save->SoundEffectsVolume * SFXDuck, TEXT("Voice"));
	}

	UGameplayStatics::PushSoundMixModifier(World, Mix);
	return true;
}

void UGF_SettingsSubsystem::ApplyAudioWhenReady()
{
	// Fade 0: by the time this lands the OST may already be audible, and a fade
	// would drag the wrong volume out even longer.
	ApplyAudio(0.f);
	StartAudioGuard();
}

void UGF_SettingsSubsystem::StartAudioGuard()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	// Re-arm the startup window on every call, so a level change also re-asserts
	// (a new world can bring a new audio device up with it).
	AudioGuardWarmupTicks = 8;

	if (GI->GetTimerManager().IsTimerActive(AudioGuardHandle))
	{
		return;
	}

	GI->GetTimerManager().SetTimer(AudioGuardHandle, FTimerDelegate::CreateWeakLambda(this,
		[this]() { TickAudioGuard(); }),
		0.25f, true);
}

void UGF_SettingsSubsystem::TickAudioGuard()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	FAudioDevice* AudioDevice = World->GetAudioDeviceRaw();
	if (!AudioDevice)
	{
		// Still starting up. ApplyAudio would no-op anyway, so wait it out.
		return;
	}

	USoundMix* Mix = UGF_GameSettingsConfig::Get()->SettingsSoundMix.LoadSynchronous();
	if (!Mix)
	{
		return;
	}

	// Startup window: re-apply unconditionally, without the "something cleared
	// it" warning - here it is expected that the first applies do not stick.
	if (AudioGuardWarmupTicks > 0)
	{
		--AudioGuardWarmupTicks;
		ApplyAudio(0.f);
		return;
	}

	// Reading a map the audio thread owns. We only test for presence and the
	// worst case is healing one tick early or late, so a lock is not worth it.
	if (AudioDevice->GetSoundMixModifiers().Contains(Mix))
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("GF_Audio: settings mix '%s' is not active - something cleared it. Re-applying "
			 "(Master=%.2f Music=%.2f SFX=%.2f)."),
		*Mix->GetName(),
		Save ? Save->MasterVolume : -1.f,
		Save ? Save->MusicVolume : -1.f,
		Save ? Save->SoundEffectsVolume : -1.f);

	ApplyAudio(0.f);
}

// ============================================================
// DUCKING
// ============================================================

float UGF_SettingsSubsystem::GetMusicDuckMultiplier() const
{
	float Result = 1.0f;
	for (const FGF_AudioDuck& Duck : ActiveDucks)
	{
		Result = FMath::Min(Result, Duck.MusicMultiplier);
	}
	return Result;
}

float UGF_SettingsSubsystem::GetSFXDuckMultiplier() const
{
	float Result = 1.0f;
	for (const FGF_AudioDuck& Duck : ActiveDucks)
	{
		Result = FMath::Min(Result, Duck.SFXMultiplier);
	}
	return Result;
}

void UGF_SettingsSubsystem::PushAudioDuck(FName DuckTag, float MusicMultiplier, float SFXMultiplier, float FadeTime)
{
	// Same tag pushed again replaces the old request - a second item pickup
	// while the first jingle is still playing must not need two pops.
	ActiveDucks.RemoveAll([DuckTag](const FGF_AudioDuck& Duck) { return Duck.Tag == DuckTag; });

	ActiveDucks.Add({
		DuckTag,
		FMath::Max(0.f, MusicMultiplier),
		FMath::Max(0.f, SFXMultiplier)
	});

	ApplyAudio(FadeTime);
}

void UGF_SettingsSubsystem::PopAudioDuck(FName DuckTag, float FadeTime)
{
	if (ActiveDucks.RemoveAll([DuckTag](const FGF_AudioDuck& Duck) { return Duck.Tag == DuckTag; }) > 0)
	{
		ApplyAudio(FadeTime);
	}
}

void UGF_SettingsSubsystem::ClearAllAudioDucks(float FadeTime)
{
	ActiveDucks.Reset();
	ApplyAudio(FadeTime);
}

void UGF_SettingsSubsystem::RestoreVolumeSettings(float FadeTime)
{
	ApplyAudio(FadeTime);
}

void UGF_SettingsSubsystem::ClearSoundMixesKeepingVolume(const UObject* WorldContextObject)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
		: nullptr;

	if (!World)
	{
		return;
	}

	UGameplayStatics::ClearSoundMixModifiers(World);

	// Any duck that was riding on those mixes is gone too, so drop the bookkeeping
	// rather than leaving a request that will never be popped.
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UGF_SettingsSubsystem* Settings = GI->GetSubsystem<UGF_SettingsSubsystem>())
		{
			Settings->ActiveDucks.Reset();

			// Fade 0: the clear was instant, so anything slower is an audible
			// blast of full-volume audio before the player's value lands.
			Settings->ApplyAudio(0.f);
		}
	}
}

void UGF_SettingsSubsystem::ApplyVideo()
{
	if (!Save)
	{
		return;
	}

	// Resizing the PIE viewport or flipping it to fullscreen is disruptive and
	// not what the editor user asked for. Video settings only apply in a real
	// game session.
	if (GIsEditor)
	{
		return;
	}

	// Pin post-process quality to 2 -- the value the EDITOR runs at, and therefore what the
	// game was authored against. Measured side by side, PIE runs sg.PostProcessQuality=2 and
	// a packaged build runs 3, which doubles the DOF blur:
	//
	//                                        PIE     packaged
	//     r.DOF.Kernel.MaxBackgroundRadius   0.012   0.025
	//     r.DOF.Kernel.MaxForegroundRadius   0.012   0.025
	//     r.DOF.Gather.AccumulatorQuality    0       1
	//     r.DOF.Recombine.Quality            0       1
	//
	// (r.DepthOfFieldQuality was 4 in both -- ApplyDepthOfField sets it -- and
	//  r.ScreenPercentage was 100 in both, so neither of those was the difference.)
	//
	// It has to be set from code: [SystemSettings] and a project DefaultDeviceProfiles.ini
	// were both tried and neither beat Scalability for an sg.* group. ECVF_SetByGameSetting
	// does, and writing the sg.* variable re-applies the whole group.
	static IConsoleVariable* PostProcessQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("sg.PostProcessQuality"));

	if (PostProcessQuality)
	{
		PostProcessQuality->Set(2, ECVF_SetByGameSetting);
	}

	UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings();
	if (!UserSettings)
	{
		return;
	}

	int32 Width = 0;
	int32 Height = 0;
	if (ParseResolution(Save->Resolution, Width, Height))
	{
		UserSettings->SetScreenResolution(FIntPoint(Width, Height));
	}

	switch (Save->WindowMode)
	{
	case EGF_WindowMode::Windowed:
		UserSettings->SetFullscreenMode(EWindowMode::Windowed);
		break;
	case EGF_WindowMode::WindowedFullscreen:
		UserSettings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		break;
	case EGF_WindowMode::Fullscreen:
	default:
		UserSettings->SetFullscreenMode(EWindowMode::Fullscreen);
		break;
	}

	UserSettings->ApplyResolutionSettings(false);
	UserSettings->SaveSettings();

	// ApplyResolutionSettings persists the choice (GameUserSettings.ini correctly records
	// FullscreenMode=2 for Windowed) but does NOT reliably restyle the existing OS window
	// when only the MODE changes -- the game keeps rendering fullscreen. "r.setres" does
	// perform the restyle, so drive it explicitly with the same values we just applied.
	if (GEngine && Width > 0 && Height > 0)
	{
		const TCHAR* ModeSuffix =
			(Save->WindowMode == EGF_WindowMode::Windowed)           ? TEXT("w")  :
			(Save->WindowMode == EGF_WindowMode::WindowedFullscreen) ? TEXT("wf") :
			                                                          TEXT("f");

		const FString Cmd = FString::Printf(TEXT("r.setres %dx%d%s"), Width, Height, ModeSuffix);
		const bool bHandled = GEngine->Exec(GetWorld(), *Cmd);

		UE_LOG(LogTemp, Warning, TEXT("GF_Settings: ApplyVideo executing '%s' (handled=%s, world=%s)"),
			*Cmd, bHandled ? TEXT("yes") : TEXT("NO"), GetWorld() ? TEXT("valid") : TEXT("NULL"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("GF_Settings: ApplyVideo did NOT exec r.setres (GEngine=%s, %dx%d)"),
			GEngine ? TEXT("valid") : TEXT("NULL"), Width, Height);
	}
}

void UGF_SettingsSubsystem::ApplyDepthOfField()
{
	if (!Save)
	{
		return;
	}

	const UGF_GameSettingsConfig* Config = UGF_GameSettingsConfig::Get();

	static IConsoleVariable* DOFQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.DepthOfFieldQuality"));

	if (DOFQuality)
	{
		const int32 Value = Save->bDOFEnabled
			? FMath::Clamp(Config->DepthOfFieldQualityWhenEnabled, 1, 4)
			: 0;
		DOFQuality->Set(Value, ECVF_SetByGameSetting);
	}
}

// ============================================================
// GETTERS
// ============================================================

EGF_TextSpeed UGF_SettingsSubsystem::GetTextSpeed() const
{
	return Save ? Save->TextSpeed : EGF_TextSpeed::Normal;
}

float UGF_SettingsSubsystem::GetTypewriterCharsPerSecond() const
{
	const UGF_GameSettingsConfig* Config = UGF_GameSettingsConfig::Get();

	switch (GetTextSpeed())
	{
	case EGF_TextSpeed::Slow:    return Config->SlowCharsPerSecond;
	case EGF_TextSpeed::Fast:    return Config->FastCharsPerSecond;
	case EGF_TextSpeed::Instant: return 0.f;   // callers treat <= 0 as "no animation"
	case EGF_TextSpeed::Normal:
	default:                     return Config->NormalCharsPerSecond;
	}
}

int32 UGF_SettingsSubsystem::GetFrameID() const
{
	return Save ? Save->FrameID : 0;
}

EGF_Difficulty UGF_SettingsSubsystem::GetDifficulty() const
{
	return Save ? Save->Difficulty : EGF_Difficulty::Normal;
}

EGF_WindowMode UGF_SettingsSubsystem::GetWindowMode() const
{
	return Save ? Save->WindowMode : EGF_WindowMode::Fullscreen;
}

FString UGF_SettingsSubsystem::GetResolution() const
{
	return Save ? Save->Resolution : TEXT("1920x1080");
}

bool UGF_SettingsSubsystem::IsDOFEnabled() const
{
	return Save ? Save->bDOFEnabled : false;
}

bool UGF_SettingsSubsystem::IsAutoRepelEnabled() const
{
	return Save ? Save->bAutoRepelEnabled : true;
}

float UGF_SettingsSubsystem::GetMasterVolume() const
{
	return Save ? Save->MasterVolume : 1.0f;
}

float UGF_SettingsSubsystem::GetMusicVolume() const
{
	return Save ? Save->MusicVolume : 0.75f;
}

float UGF_SettingsSubsystem::GetSoundEffectsVolume() const
{
	return Save ? Save->SoundEffectsVolume : 0.75f;
}

bool UGF_SettingsSubsystem::GetEXPShareSetting() const
{
	return Save ? Save->bEXPShareEnabled : true;
}

// ============================================================
// SETTERS
// ============================================================

void UGF_SettingsSubsystem::BeginLoadingUI()
{
	++LoadingUIDepth;
}

void UGF_SettingsSubsystem::EndLoadingUI()
{
	if (LoadingUIDepth <= 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GF_Settings: EndLoadingUI called without a matching BeginLoadingUI - ignoring."));
		return;
	}

	--LoadingUIDepth;
}

void UGF_SettingsSubsystem::ClearLoadingUIGuard()
{
	if (LoadingUIDepth != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GF_Settings: force-clearing a stuck UI loading guard (depth was %d)."), LoadingUIDepth);
	}

	LoadingUIDepth = 0;
}

void UGF_SettingsSubsystem::SetTextSpeed(EGF_TextSpeed NewSpeed)
{
	if (IsLoadingUI() || !Save || Save->TextSpeed == NewSpeed)
	{
		return;
	}

	Save->TextSpeed = NewSpeed;
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::SetFrameID(int32 NewFrameID)
{
	if (IsLoadingUI() || !Save)
	{
		return;
	}

	const int32 Count = GetDialogueFrameCount();
	const int32 Clamped = (Count > 0) ? FMath::Clamp(NewFrameID, 0, Count - 1) : 0;

	if (Save->FrameID == Clamped)
	{
		return;
	}

	Save->FrameID = Clamped;
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::CycleFrameID(int32 Delta)
{
	const int32 Count = GetDialogueFrameCount();
	if (IsLoadingUI() || Count <= 0 || Delta == 0)
	{
		return;
	}

	// Modulo twice so negative deltas wrap to the end instead of going negative.
	const int32 Next = ((GetFrameID() + Delta) % Count + Count) % Count;
	SetFrameID(Next);
}

void UGF_SettingsSubsystem::SetDifficulty(EGF_Difficulty NewDifficulty)
{
	if (IsLoadingUI() || !Save || Save->Difficulty == NewDifficulty)
	{
		return;
	}

	Save->Difficulty = NewDifficulty;

	// The new profile may lock the EXP Share toggle, so the mirrored value has
	// to be recomputed - HandleSettingChanged does that via IsEXPShareActive.
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::SetWindowMode(EGF_WindowMode NewMode)
{
	if (IsLoadingUI() || !Save || Save->WindowMode == NewMode)
	{
		// Diagnostic: window mode changes appeared to do nothing in packaged builds. This
		// tells us whether the setter is reached at all and which guard is swallowing it --
		// in particular, selecting the mode that is ALREADY saved is a silent no-op.
		UE_LOG(LogTemp, Warning,
			TEXT("GF_Settings: SetWindowMode(%d) IGNORED (LoadingUI=%s, Save=%s, current=%d)"),
			static_cast<int32>(NewMode),
			IsLoadingUI() ? TEXT("yes") : TEXT("no"),
			Save ? TEXT("valid") : TEXT("NULL"),
			Save ? static_cast<int32>(Save->WindowMode) : -1);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("GF_Settings: SetWindowMode %d -> %d"),
		static_cast<int32>(Save->WindowMode), static_cast<int32>(NewMode));

	Save->WindowMode = NewMode;
	ApplyVideo();
	HandleSettingChanged();
}

bool UGF_SettingsSubsystem::IsResolutionAllowed(const FString& InResolution) const
{
	int32 Width = 0;
	int32 Height = 0;
	if (!ParseResolution(InResolution, Width, Height))
	{
		return false;
	}

	const UGF_GameSettingsConfig* Config = UGF_GameSettingsConfig::Get();
	if (!Config->bEnforceAspectRatio)
	{
		return true;
	}

	const float Ratio = static_cast<float>(Width) / static_cast<float>(Height);
	return FMath::Abs(Ratio - Config->EnforcedAspectRatio) <= Config->AspectRatioTolerance;
}

void UGF_SettingsSubsystem::SetResolution(const FString& NewResolution)
{
	if (IsLoadingUI())
	{
		return;
	}

	int32 Width = 0;
	int32 Height = 0;
	if (!Save || !ParseResolution(NewResolution, Width, Height))
	{
		UE_LOG(LogTemp, Warning, TEXT("GF_Settings: ignoring malformed resolution '%s'"), *NewResolution);
		return;
	}

	// Single choke point - SetResolutionByIndex routes through here too, so the
	// aspect ratio guarantee holds no matter which path asked.
	if (!IsResolutionAllowed(NewResolution))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GF_Settings: refusing resolution '%s' (%.4f) - only %.4f is allowed. "
				 "Change EnforcedAspectRatio in Project Settings > Game > Gamma Framework Settings to relax this."),
			*NewResolution,
			static_cast<float>(Width) / static_cast<float>(Height),
			UGF_GameSettingsConfig::Get()->EnforcedAspectRatio);
		return;
	}

	const FString Normalized = FString::Printf(TEXT("%dx%d"), Width, Height);
	if (Save->Resolution == Normalized)
	{
		return;
	}

	Save->Resolution = Normalized;
	ApplyVideo();
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::SetDOFEnabled(bool bEnabled)
{
	if (IsLoadingUI() || !Save || Save->bDOFEnabled == bEnabled)
	{
		return;
	}

	Save->bDOFEnabled = bEnabled;
	ApplyDepthOfField();
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::SetAutoRepelEnabled(bool bEnabled)
{
	if (IsLoadingUI() || !Save || Save->bAutoRepelEnabled == bEnabled)
	{
		return;
	}

	// Nothing to apply - the repel step handler reads the flag when a Repel expires.
	Save->bAutoRepelEnabled = bEnabled;
	HandleSettingChanged();
}

TArray<int32> UGF_SettingsSubsystem::GetFrameRateLimitOptions()
{
	// 0 = unlimited, listed last so the safe choices come first.
	return { 30, 60, 120, 144, 0 };
}

TArray<FText> UGF_SettingsSubsystem::GetFrameRateLimitLabels()
{
	TArray<FText> Labels;
	for (const int32 Limit : GetFrameRateLimitOptions())
	{
		Labels.Add(Limit > 0
			? FText::AsNumber(Limit)
			: NSLOCTEXT("GESettings", "FrameRate_Unlimited", "Unlimited"));
	}
	return Labels;
}

int32 UGF_SettingsSubsystem::GetFrameRateLimitIndex() const
{
	if (!Save)
	{
		return 0;
	}

	const int32 Found = GetFrameRateLimitOptions().IndexOfByKey(Save->FrameRateLimit);
	return (Found != INDEX_NONE) ? Found : 0;
}

void UGF_SettingsSubsystem::SetFrameRateLimit(int32 NewLimit)
{
	const int32 Clamped = FMath::Max(NewLimit, 0);   // negatives would mean "stop rendering"
	if (IsLoadingUI() || !Save || Save->FrameRateLimit == Clamped)
	{
		return;
	}

	Save->FrameRateLimit = Clamped;
	ApplyFrameRateLimit();
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::SetFrameRateLimitByIndex(int32 Index)
{
	const TArray<int32> Options = GetFrameRateLimitOptions();
	if (Options.Num() == 0)
	{
		return;
	}

	SetFrameRateLimit(Options[FMath::Clamp(Index, 0, Options.Num() - 1)]);
}

void UGF_SettingsSubsystem::ApplyFrameRateLimit()
{
	if (!Save)
	{
		return;
	}

	// t.MaxFPS is the engine's own limiter and takes 0 as "unlimited", which matches
	// how the option is stored. Set at GameSetting priority so it beats the project
	// defaults but a player typing t.MaxFPS in the console still wins.
	static IConsoleVariable* MaxFPS =
		IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));

	if (MaxFPS)
	{
		MaxFPS->Set(static_cast<float>(Save->FrameRateLimit), ECVF_SetByGameSetting);
	}

	UE_LOG(LogTemp, Warning, TEXT("GF_Settings: frame rate limit set to %s"),
		Save->FrameRateLimit > 0 ? *FString::FromInt(Save->FrameRateLimit) : TEXT("unlimited"));
}

bool UGF_SettingsSubsystem::IsVSyncEnabled() const
{
	return Save ? Save->bVSyncEnabled : true;
}

void UGF_SettingsSubsystem::SetVSyncEnabled(bool bEnabled)
{
	if (IsLoadingUI() || !Save || Save->bVSyncEnabled == bEnabled)
	{
		return;
	}

	Save->bVSyncEnabled = bEnabled;
	ApplyVSync();
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::ApplyVSync()
{
	if (!Save)
	{
		return;
	}

	// This is the same CVar UGameUserSettings drives from its own bUseVSync, so setting
	// it directly is equivalent -- and avoids pulling in GameUserSettings just for one
	// flag when every other setting here lives in GF_SettingsSave.
	static IConsoleVariable* VSync =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));

	if (VSync)
	{
		VSync->Set(Save->bVSyncEnabled ? 1 : 0, ECVF_SetByGameSetting);
	}

	// Keep GameUserSettings agreeing with us. Nothing calls ApplyNonResolutionSettings()
	// today, but if anything ever does it would push its own bUseVSync at the same
	// priority and silently win -- and GameUserSettings.ini currently says False.
	if (UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings())
	{
		UserSettings->SetVSyncEnabled(Save->bVSyncEnabled);
	}

	// NOTE: this cannot take effect in PIE. BaseEngine.ini pins r.VSync=0 under
	// [SystemSettingsEditor], and SystemSettingsIni outranks GameSetting -- so the
	// editor always reports 0 no matter what is set here. Test VSync in a packaged
	// build only.
	UE_LOG(LogTemp, Warning, TEXT("GF_Settings: VSync %s"),
		Save->bVSyncEnabled ? TEXT("on") : TEXT("off"));
}

void UGF_SettingsSubsystem::SetMasterVolume(float NewVolume)
{
	const float Clamped = FMath::Clamp(NewVolume, 0.f, 1.f);
	if (IsLoadingUI() || !Save || FMath::IsNearlyEqual(Save->MasterVolume, Clamped))
	{
		return;
	}

	Save->MasterVolume = Clamped;
	ApplyAudio();
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::SetMusicVolume(float NewVolume)
{
	const float Clamped = FMath::Clamp(NewVolume, 0.f, 1.f);
	if (IsLoadingUI() || !Save || FMath::IsNearlyEqual(Save->MusicVolume, Clamped))
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("GF_Audio: SetMusicVolume(%.2f) called."), Clamped);

	Save->MusicVolume = Clamped;
	ApplyAudio();
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::SetSoundEffectsVolume(float NewVolume)
{
	const float Clamped = FMath::Clamp(NewVolume, 0.f, 1.f);
	if (IsLoadingUI() || !Save || FMath::IsNearlyEqual(Save->SoundEffectsVolume, Clamped))
	{
		return;
	}

	Save->SoundEffectsVolume = Clamped;
	ApplyAudio();
	HandleSettingChanged();
}

void UGF_SettingsSubsystem::SetEXPShareEnabled(bool bEnabled)
{
	if (IsLoadingUI() || !Save || !IsEXPShareToggleAllowed() || Save->bEXPShareEnabled == bEnabled)
	{
		return;
	}

	Save->bEXPShareEnabled = bEnabled;
	HandleSettingChanged();
}

// ============================================================
// DIALOGUE FRAMES
// ============================================================

int32 UGF_SettingsSubsystem::GetDialogueFrameCount() const
{
	return UGF_GameSettingsConfig::Get()->DialogueFrames.Num();
}

bool UGF_SettingsSubsystem::GetDialogueFrame(int32 Index, FGF_DialogueFrame& OutFrame) const
{
	const TArray<FGF_DialogueFrame>& Frames = UGF_GameSettingsConfig::Get()->DialogueFrames;
	if (!Frames.IsValidIndex(Index))
	{
		return false;
	}

	OutFrame = Frames[Index];
	return true;
}

bool UGF_SettingsSubsystem::GetCurrentDialogueFrame(FGF_DialogueFrame& OutFrame) const
{
	return GetDialogueFrame(GetFrameID(), OutFrame);
}

UTexture2D* UGF_SettingsSubsystem::GetDialogueFrameTexture(int32 Index) const
{
	FGF_DialogueFrame Frame;
	if (!GetDialogueFrame(Index, Frame))
	{
		return nullptr;
	}

	return Frame.FrameTexture.LoadSynchronous();
}

FSlateBrush UGF_SettingsSubsystem::GetDialogueFrameBrush(int32 Index) const
{
	FSlateBrush Brush;

	FGF_DialogueFrame Frame;
	if (!GetDialogueFrame(Index, Frame))
	{
		return Brush;
	}

	UTexture2D* Texture = Frame.FrameTexture.LoadSynchronous();
	if (!Texture)
	{
		return Brush;
	}

	Brush.SetResourceObject(Texture);
	Brush.ImageSize = FVector2D(
		static_cast<float>(Texture->GetSizeX()),
		static_cast<float>(Texture->GetSizeY()));
	Brush.DrawAs     = Frame.bUseNineSlice ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
	Brush.Margin     = Frame.NineSliceMargin;
	Brush.TintColor  = FSlateColor(Frame.FrameTint);

	return Brush;
}

FSlateBrush UGF_SettingsSubsystem::GetCurrentDialogueFrameBrush() const
{
	return GetDialogueFrameBrush(GetFrameID());
}

// ============================================================
// DIFFICULTY
// ============================================================

float UGF_SettingsSubsystem::GetEXPMultiplier() const
{
	return UGF_GameSettingsConfig::Get()->GetProfile(GetDifficulty()).EXPMultiplier;
}

int32 UGF_SettingsSubsystem::GetAIDifficultyOffset() const
{
	return UGF_GameSettingsConfig::Get()->GetProfile(GetDifficulty()).AIDifficultyOffset;
}

bool UGF_SettingsSubsystem::IsEXPShareToggleAllowed() const
{
	return UGF_GameSettingsConfig::Get()->GetProfile(GetDifficulty()).bAllowEXPShareToggle;
}

bool UGF_SettingsSubsystem::IsEXPShareActive() const
{
	const FGF_DifficultyProfile& Profile = UGF_GameSettingsConfig::Get()->GetProfile(GetDifficulty());
	return Profile.bAllowEXPShareToggle ? GetEXPShareSetting() : Profile.bForcedEXPShare;
}

float UGF_SettingsSubsystem::GetEXPShareRatio() const
{
	return UGF_GameSettingsConfig::Get()->GetProfile(GetDifficulty()).EXPShareRatio;
}

bool UGF_SettingsSubsystem::IsLevelCapEnforced() const
{
	const FGF_DifficultyProfile& Profile = UGF_GameSettingsConfig::Get()->GetProfile(GetDifficulty());
	return Profile.bEnforceLevelCap && Profile.LevelCapPerEmblemCount.Num() > 0;
}

int32 UGF_SettingsSubsystem::GetEmblemCount() const
{
	// Counting the individual emblem flags is the Creatures side's job now --
	// it owns the save object they live on. The important part of the
	// original fix survives there: count the per-emblem bools rather than
	// trusting the TotalEmblemAmount counter, which is not kept in sync
	// when a trial is beaten and reports 0 for a player who has earned
	// several, pinning the level cap at its first-trial value and blocking
	// all EXP.
	return FGF_CreatureBridge::EmblemCount(this);
}

int32 UGF_SettingsSubsystem::GetLevelCap() const
{
	if (!IsLevelCapEnforced())
	{
		return 100;
	}

	const TArray<int32>& Caps = UGF_GameSettingsConfig::Get()->GetProfile(GetDifficulty()).LevelCapPerEmblemCount;

	// A short array just clamps to its last entry rather than falling off.
	const int32 CapIndex = FMath::Clamp(GetEmblemCount(), 0, Caps.Num() - 1);
	return FMath::Clamp(Caps[CapIndex], 1, 100);
}

bool UGF_SettingsSubsystem::IsLevelCapped(int32 Level) const
{
	return IsLevelCapEnforced() && Level >= GetLevelCap();
}

// ============================================================
// UI HELPERS
// ============================================================

TArray<FText> UGF_SettingsSubsystem::GetTextSpeedLabels()
{
	return {
		GetTextSpeedLabel(EGF_TextSpeed::Slow),
		GetTextSpeedLabel(EGF_TextSpeed::Normal),
		GetTextSpeedLabel(EGF_TextSpeed::Fast),
		GetTextSpeedLabel(EGF_TextSpeed::Instant)
	};
}

TArray<FText> UGF_SettingsSubsystem::GetDifficultyLabels()
{
	return {
		GetDifficultyLabel(EGF_Difficulty::Easy),
		GetDifficultyLabel(EGF_Difficulty::Normal),
		GetDifficultyLabel(EGF_Difficulty::Hard)
	};
}

TArray<FText> UGF_SettingsSubsystem::GetWindowModeLabels()
{
	return {
		GetWindowModeLabel(EGF_WindowMode::Fullscreen),
		GetWindowModeLabel(EGF_WindowMode::WindowedFullscreen),
		GetWindowModeLabel(EGF_WindowMode::Windowed)
	};
}

FText UGF_SettingsSubsystem::GetTextSpeedLabel(EGF_TextSpeed Speed)
{
	switch (Speed)
	{
	case EGF_TextSpeed::Slow:    return NSLOCTEXT("GESettings", "TextSpeed_Slow",    "Slow");
	case EGF_TextSpeed::Fast:    return NSLOCTEXT("GESettings", "TextSpeed_Fast",    "Fast");
	case EGF_TextSpeed::Instant: return NSLOCTEXT("GESettings", "TextSpeed_Instant", "Instant");
	case EGF_TextSpeed::Normal:
	default:                     return NSLOCTEXT("GESettings", "TextSpeed_Normal",  "Normal");
	}
}

FText UGF_SettingsSubsystem::GetDifficultyLabel(EGF_Difficulty Difficulty)
{
	switch (Difficulty)
	{
	case EGF_Difficulty::Easy: return NSLOCTEXT("GESettings", "Difficulty_Easy", "Easy");
	case EGF_Difficulty::Hard: return NSLOCTEXT("GESettings", "Difficulty_Hard", "Hard");
	case EGF_Difficulty::Normal:
	default:                   return NSLOCTEXT("GESettings", "Difficulty_Normal", "Normal");
	}
}

FText UGF_SettingsSubsystem::GetWindowModeLabel(EGF_WindowMode Mode)
{
	switch (Mode)
	{
	case EGF_WindowMode::Windowed:           return NSLOCTEXT("GESettings", "Window_Windowed",   "Windowed");
	case EGF_WindowMode::WindowedFullscreen: return NSLOCTEXT("GESettings", "Window_Borderless", "Borderless");
	case EGF_WindowMode::Fullscreen:
	default:                                 return NSLOCTEXT("GESettings", "Window_Fullscreen", "Fullscreen");
	}
}

TArray<FString> UGF_SettingsSubsystem::GetSelectableResolutions() const
{
	// Filtered, not raw: a non-16:9 entry left in the config list would otherwise
	// show up in the menu and then be refused when picked, which reads as a bug.
	// Dropped entries are logged once by ReloadSettings.
	TArray<FString> Allowed;
	for (const FString& Option : UGF_GameSettingsConfig::Get()->SelectableResolutions)
	{
		if (IsResolutionAllowed(Option))
		{
			Allowed.Add(Option);
		}
	}

	return Allowed;
}

int32 UGF_SettingsSubsystem::GetResolutionIndex() const
{
	const TArray<FString> Options = GetSelectableResolutions();
	const int32 Index = Options.IndexOfByKey(GetResolution());
	return (Index != INDEX_NONE) ? Index : 0;
}

// ============================================================
// INDEX-BASED ACCESSORS
// ============================================================

int32 UGF_SettingsSubsystem::GetTextSpeedIndex() const
{
	return static_cast<int32>(GetTextSpeed());
}

void UGF_SettingsSubsystem::SetTextSpeedByIndex(int32 Index)
{
	const int32 Max = static_cast<int32>(EGF_TextSpeed::Instant);
	SetTextSpeed(static_cast<EGF_TextSpeed>(FMath::Clamp(Index, 0, Max)));
}

int32 UGF_SettingsSubsystem::GetDifficultyIndex() const
{
	return static_cast<int32>(GetDifficulty());
}

void UGF_SettingsSubsystem::SetDifficultyByIndex(int32 Index)
{
	const int32 Max = static_cast<int32>(EGF_Difficulty::Hard);
	SetDifficulty(static_cast<EGF_Difficulty>(FMath::Clamp(Index, 0, Max)));
}

int32 UGF_SettingsSubsystem::GetWindowModeIndex() const
{
	return static_cast<int32>(GetWindowMode());
}

void UGF_SettingsSubsystem::SetWindowModeByIndex(int32 Index)
{
	const int32 Max = static_cast<int32>(EGF_WindowMode::Windowed);
	SetWindowMode(static_cast<EGF_WindowMode>(FMath::Clamp(Index, 0, Max)));
}

void UGF_SettingsSubsystem::SetResolutionByIndex(int32 Index)
{
	// Indexes the same filtered list the menu was populated from, so an index
	// coming back from an option box always lines up with what was displayed.
	const TArray<FString> Options = GetSelectableResolutions();
	if (Options.Num() == 0)
	{
		return;
	}

	SetResolution(Options[FMath::Clamp(Index, 0, Options.Num() - 1)]);
}

int32 UGF_SettingsSubsystem::GetEXPShareIndex() const
{
	// Reports the effective value, so a difficulty that forces the toggle shows
	// the forced state rather than the player's stale preference.
	return IsEXPShareActive() ? 0 : 1;
}

void UGF_SettingsSubsystem::SetEXPShareByIndex(int32 Index)
{
	SetEXPShareEnabled(Index == 0);
}

int32 UGF_SettingsSubsystem::GetDOFIndex() const
{
	return IsDOFEnabled() ? 0 : 1;
}

int32 UGF_SettingsSubsystem::GetAutoRepelIndex() const
{
	return IsAutoRepelEnabled() ? 0 : 1;
}

void UGF_SettingsSubsystem::SetAutoRepelByIndex(int32 Index)
{
	SetAutoRepelEnabled(Index == 0);
}

void UGF_SettingsSubsystem::SetDOFByIndex(int32 Index)
{
	SetDOFEnabled(Index == 0);
}

// ============================================================
// STRING CONVERSION
// ============================================================

FString UGF_SettingsSubsystem::TextSpeedToString(EGF_TextSpeed Speed)
{
	switch (Speed)
	{
	case EGF_TextSpeed::Slow:    return TEXT("Slow");
	case EGF_TextSpeed::Fast:    return TEXT("Fast");
	case EGF_TextSpeed::Instant: return TEXT("Instant");
	case EGF_TextSpeed::Normal:
	default:                     return TEXT("Normal");
	}
}

EGF_TextSpeed UGF_SettingsSubsystem::StringToTextSpeed(const FString& In)
{
	if (In.Equals(TEXT("Slow"),    ESearchCase::IgnoreCase)) return EGF_TextSpeed::Slow;
	if (In.Equals(TEXT("Fast"),    ESearchCase::IgnoreCase)) return EGF_TextSpeed::Fast;
	if (In.Equals(TEXT("Instant"), ESearchCase::IgnoreCase)) return EGF_TextSpeed::Instant;
	return EGF_TextSpeed::Normal;
}

FString UGF_SettingsSubsystem::DifficultyToString(EGF_Difficulty Difficulty)
{
	switch (Difficulty)
	{
	case EGF_Difficulty::Easy: return TEXT("Easy");
	case EGF_Difficulty::Hard: return TEXT("Hard");
	case EGF_Difficulty::Normal:
	default:                   return TEXT("Normal");
	}
}

EGF_Difficulty UGF_SettingsSubsystem::StringToDifficulty(const FString& In)
{
	if (In.Equals(TEXT("Easy"), ESearchCase::IgnoreCase)) return EGF_Difficulty::Easy;
	if (In.Equals(TEXT("Hard"), ESearchCase::IgnoreCase)) return EGF_Difficulty::Hard;
	return EGF_Difficulty::Normal;
}

FString UGF_SettingsSubsystem::WindowModeToString(EGF_WindowMode Mode)
{
	switch (Mode)
	{
	case EGF_WindowMode::Windowed:           return TEXT("Windowed");
	case EGF_WindowMode::WindowedFullscreen: return TEXT("Borderless");
	case EGF_WindowMode::Fullscreen:
	default:                                 return TEXT("Fullscreen");
	}
}

EGF_WindowMode UGF_SettingsSubsystem::StringToWindowMode(const FString& In)
{
	// These strings come from an option box in the settings widget, so they are
	// display text -- they get reworded, and they have been misspelled ("Window
	// Bordless").  Exact matching meant every unrecognised label fell through to
	// Fullscreen, which made the whole setting a silent no-op.  Match loosely on
	// the part that carries the meaning instead.
	FString Key = In.ToLower();
	Key.ReplaceInline(TEXT(" "), TEXT(""));

	// Check borderless first: "Windowed Fullscreen" contains both other keywords.
	if (Key.Contains(TEXT("border")) ||     // Borderless
		Key.Contains(TEXT("bordless")) ||   // known typo in the shipped option box
		Key.Contains(TEXT("windowedfullscreen")))
	{
		return EGF_WindowMode::WindowedFullscreen;
	}
	if (Key.Contains(TEXT("window")))       // Window, Windowed
	{
		return EGF_WindowMode::Windowed;
	}
	return EGF_WindowMode::Fullscreen;
}

bool UGF_SettingsSubsystem::ParseResolution(const FString& In, int32& OutWidth, int32& OutHeight)
{
	FString WidthStr;
	FString HeightStr;

	if (!In.Split(TEXT("x"), &WidthStr, &HeightStr, ESearchCase::IgnoreCase))
	{
		return false;
	}

	WidthStr.TrimStartAndEndInline();
	HeightStr.TrimStartAndEndInline();

	if (!WidthStr.IsNumeric() || !HeightStr.IsNumeric())
	{
		return false;
	}

	const int32 Width  = FCString::Atoi(*WidthStr);
	const int32 Height = FCString::Atoi(*HeightStr);

	if (Width <= 0 || Height <= 0)
	{
		return false;
	}

	OutWidth  = Width;
	OutHeight = Height;
	return true;
}
