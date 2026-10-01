#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Styling/SlateBrush.h"
#include "GF_SettingsTypes.h"
#include "GF_ResettableState.h"
#include "GF_SettingsSubsystem.generated.h"

class UGF_SettingsSave;
class UTexture2D;

// Fires whenever any setting changes (and once after load, on Initialize).
// Bind in widgets that need to react live - the dialogue widget uses this to
// pick up text speed and frame changes without being reopened.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnSettingsChanged);

/**
 * Player options: text speed, dialogue frame, difficulty, video and audio.
 *
 * This subsystem owns the values. It persists them to its own "GEOptions" save
 * slot and mirrors them onto UGF_VaultSystem so existing Blueprint reads of
 * VaultSystem->TextSpeed / FrameID / MasterVolume / ... keep working. The mirror
 * is one-way (subsystem -> VaultSystem): loading a game save never clobbers the
 * player's machine settings.
 *
 * Setters apply their effect immediately and schedule a debounced write to
 * disk, so an options screen does not need an explicit Apply button. Call
 * SaveSettings() directly if you want to force the write (e.g. before quitting).
 */
UCLASS(BlueprintType)
class GAMMAFRAMEWORKWORLD_API UGF_SettingsSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// IGF_ResettableState - DELIBERATE NO-OP.
	// These are the player's OPTIONS (text speed, volume, button config), not run
	// state. They outlive any single playthrough by design, and wiping them on a
	// soft reset would silently reset the player's settings every time they use it.
	// New Game already re-stamps them onto the fresh save via SyncToVaultSystem().
	virtual void ResetToBootState() override {}

	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FGF_OnSettingsChanged OnSettingsChanged;

	// --------------------------------------------------------
	// GETTERS
	// --------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Settings|Text")
	EGF_TextSpeed GetTextSpeed() const;

	// Characters revealed per second for the current text speed.
	// Returns 0 for Instant - callers must treat <= 0 as "no typewriter".
	UFUNCTION(BlueprintPure, Category = "Settings|Text")
	float GetTypewriterCharsPerSecond() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Dialogue Frame")
	int32 GetFrameID() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	EGF_Difficulty GetDifficulty() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Video")
	EGF_WindowMode GetWindowMode() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Video")
	FString GetResolution() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Video")
	bool IsDOFEnabled() const;

	// True when an expiring Repel should be replaced from the bag automatically.
	UFUNCTION(BlueprintPure, Category = "Settings|Gameplay")
	bool IsAutoRepelEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetMasterVolume() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetMusicVolume() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetSoundEffectsVolume() const;

	// The raw saved toggle. Gameplay should call IsEXPShareActive() instead,
	// which also honours difficulty profiles that lock the toggle.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	bool GetEXPShareSetting() const;

	// --------------------------------------------------------
	// SETTERS
	// Each applies immediately, mirrors to VaultSystem, broadcasts
	// OnSettingsChanged and schedules a save.
	// --------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Settings|Text")
	void SetTextSpeed(EGF_TextSpeed NewSpeed);

	UFUNCTION(BlueprintCallable, Category = "Settings|Dialogue Frame")
	void SetFrameID(int32 NewFrameID);

	// Wraps around the ends, so a left/right option box can just call
	// CycleFrameID(-1) / CycleFrameID(1).
	UFUNCTION(BlueprintCallable, Category = "Settings|Dialogue Frame")
	void CycleFrameID(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Settings|Difficulty")
	void SetDifficulty(EGF_Difficulty NewDifficulty);

	UFUNCTION(BlueprintCallable, Category = "Settings|Video")
	void SetWindowMode(EGF_WindowMode NewMode);

	// Accepts "WIDTHxHEIGHT". Ignored if it cannot be parsed, or if aspect ratio
	// enforcement is on and it is not the enforced ratio.
	UFUNCTION(BlueprintCallable, Category = "Settings|Video")
	void SetResolution(const FString& NewResolution);

	// False when the string is malformed, or when aspect ratio enforcement is on
	// and this resolution is a different shape. Always true when enforcement is
	// off and the string parses.
	UFUNCTION(BlueprintPure, Category = "Settings|Video")
	bool IsResolutionAllowed(const FString& InResolution) const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Video")
	void SetDOFEnabled(bool bEnabled);

	// Frames per second, or 0 for unlimited. Values outside GetFrameRateLimitOptions()
	// are accepted -- the list is only what the menu offers.
	UFUNCTION(BlueprintCallable, Category = "Settings|Video")
	void SetFrameRateLimit(int32 NewLimit);

	// Indexes GetFrameRateLimitOptions(), so an index from an option box always lines
	// up with the label that was displayed.
	UFUNCTION(BlueprintCallable, Category = "Settings|Video")
	void SetFrameRateLimitByIndex(int32 Index);

	// The cap values the menu offers, in display order. 0 means unlimited.
	UFUNCTION(BlueprintPure, Category = "Settings|Video")
	static TArray<int32> GetFrameRateLimitOptions();

	// Display labels matching GetFrameRateLimitOptions() one-for-one.
	UFUNCTION(BlueprintPure, Category = "Settings|Video")
	static TArray<FText> GetFrameRateLimitLabels();

	// Index of the saved cap within GetFrameRateLimitOptions(), for initialising the
	// option box. Returns 0 if the saved value is not one of the offered options.
	UFUNCTION(BlueprintPure, Category = "Settings|Video")
	int32 GetFrameRateLimitIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Video")
	void SetVSyncEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Settings|Video")
	bool IsVSyncEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMasterVolume(float NewVolume);

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMusicVolume(float NewVolume);

	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetSoundEffectsVolume(float NewVolume);

	// --------------------------------------------------------
	// DUCKING
	// --------------------------------------------------------
	// The volume sliders work by stamping SetSoundMixClassOverride onto the
	// settings sound mix. That means ANY "Clear Sound Mix Class Override" /
	// "Clear Sound Mix Modifiers" node in Blueprint throws the player's chosen
	// volume away and the class snaps back to its asset default - which is why a
	// music slider at 0 jumps back to full when a heal or item jingle finishes.
	//
	// Use these instead of pushing and clearing your own duck mix. A duck is a
	// MULTIPLIER on the saved volume, so 0 stays 0 and the player's value is
	// never overwritten - unducking just re-stamps what they picked.
	//
	// Ducks are tagged. Pushing the same tag twice replaces the request rather
	// than stacking, and while several tags are active the quietest one wins, so
	// grabbing an item mid-heal does not leave the music stuck low.

	// MusicMultiplier/SFXMultiplier scale the saved volumes (1.0 = untouched).
	// FadeTime < 0 uses VolumeFadeTime from the config.
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio", meta = (AdvancedDisplay = "3"))
	void PushAudioDuck(FName DuckTag, float MusicMultiplier = 0.25f, float SFXMultiplier = 1.0f, float FadeTime = -1.0f);

	// Removes one duck request. Silently does nothing if that tag is not active.
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio", meta = (AdvancedDisplay = "1"))
	void PopAudioDuck(FName DuckTag, float FadeTime = -1.0f);

	// Drops every duck and restores the player's volumes. Safety valve for a duck
	// whose owner was destroyed before it could pop.
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio", meta = (AdvancedDisplay = "0"))
	void ClearAllAudioDucks(float FadeTime = -1.0f);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	bool IsAudioDucked() const { return ActiveDucks.Num() > 0; }

	// Drop-in replacement for the engine's "Clear Sound Mix Modifiers" node.
	// Same single world-context pin, so you can delete the old node and drop this
	// one onto the same wires. Clears every mix exactly like the engine node, then
	// immediately re-stamps the player's saved volumes - which the engine node
	// throws away, because the sliders live on the settings mix it just cleared.
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Clear Sound Mixes (Keep Volume)"))
	static void ClearSoundMixesKeepingVolume(const UObject* WorldContextObject);

	// Re-pushes the settings mix and re-stamps the player's saved volumes.
	// Call this immediately after any Clear/Pop Sound Mix node you keep, and the
	// slider values come straight back instead of snapping to the class default.
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio", meta = (AdvancedDisplay = "0"))
	void RestoreVolumeSettings(float FadeTime = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "Settings|Gameplay")
	void SetAutoRepelEnabled(bool bEnabled);

	// No-op when the active difficulty profile locks the toggle.
	UFUNCTION(BlueprintCallable, Category = "Settings|Difficulty")
	void SetEXPShareEnabled(bool bEnabled);

	// --------------------------------------------------------
	// PERSISTENCE
	// --------------------------------------------------------

	// --------------------------------------------------------
	// UI LOADING GUARD
	// --------------------------------------------------------
	// While armed, every setter is a no-op. This exists because UINavOptionBox
	// broadcasts OnValueChanged from NativePreConstruct with its designer-authored
	// OptionIndex (LastOptionIndex starts at -1, so the first Update always counts
	// as a change). Without a guard those bogus events overwrite real settings with
	// designer defaults before the options screen has even read them.
	//
	// PreConstruct on the boxes runs before the options widget's own Construct, so
	// arming this from inside the options widget is TOO LATE. Call BeginLoadingUI
	// from whatever opens the screen, immediately BEFORE Create Widget, and
	// EndLoadingUI once your populate/LoadSettings pass has finished.
	//
	// Re-entrant: nested Begin/End calls are counted, so it is safe to wrap an
	// inner populate step that also guards itself.

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void BeginLoadingUI();

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void EndLoadingUI();

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	bool IsLoadingUI() const { return LoadingUIDepth > 0; }

	// Force the guard off. Only needed if a widget was destroyed mid-populate and
	// left it armed - a stuck guard silently swallows every settings change.
	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void ClearLoadingUIGuard();

	// Force an immediate write. Setters already schedule this automatically.
	UFUNCTION(BlueprintCallable, Category = "Settings")
	bool SaveSettings();

	// Reload from disk, discarding unsaved changes.
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ReloadSettings();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetToDefaults();

	// Re-runs every apply step (audio, video, DOF) and re-broadcasts.
	// Useful after a level load if something reset the sound mix.
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplyAllSettings();

	// Copies the current values onto UGF_VaultSystem. Called automatically on
	// every change and by CreatureManagerSubsystem after a game save is loaded.
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SyncToVaultSystem();

	// --------------------------------------------------------
	// DIALOGUE FRAME LOOKUP
	// --------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Settings|Dialogue Frame")
	int32 GetDialogueFrameCount() const;

	// False when Index is out of range or the config array is empty.
	UFUNCTION(BlueprintPure, Category = "Settings|Dialogue Frame")
	bool GetDialogueFrame(int32 Index, FGF_DialogueFrame& OutFrame) const;

	UFUNCTION(BlueprintPure, Category = "Settings|Dialogue Frame")
	bool GetCurrentDialogueFrame(FGF_DialogueFrame& OutFrame) const;

	// Loads and returns the texture for a frame. Null if unset or out of range.
	UFUNCTION(BlueprintPure, Category = "Settings|Dialogue Frame")
	UTexture2D* GetDialogueFrameTexture(int32 Index) const;

	// Ready-to-use brush (nine-slice applied) for a UImage. Empty brush when the
	// index is invalid, so an unconfigured frame list just shows nothing.
	UFUNCTION(BlueprintPure, Category = "Settings|Dialogue Frame")
	FSlateBrush GetDialogueFrameBrush(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Settings|Dialogue Frame")
	FSlateBrush GetCurrentDialogueFrameBrush() const;

	// --------------------------------------------------------
	// DIFFICULTY HOOKS (read by gameplay code)
	// --------------------------------------------------------

	// Multiplier applied to every EXP award.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	float GetEXPMultiplier() const;

	// Steps to shift a tamer's configured AI difficulty by.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	int32 GetAIDifficultyOffset() const;

	// True when EXP Share should distribute EXP to non-battlers right now.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	bool IsEXPShareActive() const;

	// False when the current difficulty locks the EXP Share option - grey the
	// row out in the options screen.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	bool IsEXPShareToggleAllowed() const;

	// Fraction of a battler's EXP that non-battlers receive.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	float GetEXPShareRatio() const;

	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	bool IsLevelCapEnforced() const;

	// Emblems earned, used to index the level cap table.
	// Counts the per-emblem bools rather than trusting VaultSystem::TotalEmblemAmount,
	// which is not reliably incremented when a trial is beaten. Returns whichever
	// source is higher so this stays correct if the counter is ever fixed.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	int32 GetEmblemCount() const;

	// Highest level a party Creature may reach right now, from the player's emblem
	// count. Returns 100 when no cap applies.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	int32 GetLevelCap() const;

	// True when this Creature has hit the cap and should stop gaining EXP.
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	bool IsLevelCapped(int32 Level) const;

	// --------------------------------------------------------
	// OPTION LISTS / LABELS (for populating the options screen)
	// --------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	static TArray<FText> GetTextSpeedLabels();

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	static TArray<FText> GetDifficultyLabels();

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	static TArray<FText> GetWindowModeLabels();

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	static FText GetTextSpeedLabel(EGF_TextSpeed Speed);

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	static FText GetDifficultyLabel(EGF_Difficulty Difficulty);

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	static FText GetWindowModeLabel(EGF_WindowMode Mode);

	// Resolutions offered in the menu, as configured in Project Settings.
	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	TArray<FString> GetSelectableResolutions() const;

	// Index of the current resolution inside GetSelectableResolutions(),
	// or 0 when the saved value is not in the list.
	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	int32 GetResolutionIndex() const;

	// --------------------------------------------------------
	// INDEX-BASED ACCESSORS
	// UINavOptionBox works in OptionIndex ints, so these pair straight with
	// SetOptionIndex / OnValueChanged without an int->byte->enum conversion.
	// Indices match the order of the matching GetXLabels() array.
	// Out-of-range values are clamped, never ignored.
	// --------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	int32 GetTextSpeedIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void SetTextSpeedByIndex(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	int32 GetDifficultyIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void SetDifficultyByIndex(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	int32 GetWindowModeIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void SetWindowModeByIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void SetResolutionByIndex(int32 Index);

	// Yes/No rows: index 0 = Yes, index 1 = No (the order the option box art
	// reads left-to-right). Use these for the EXP Share and DOF boxes.
	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	int32 GetEXPShareIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void SetEXPShareByIndex(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	int32 GetDOFIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void SetDOFByIndex(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Settings|UI")
	int32 GetAutoRepelIndex() const;

	UFUNCTION(BlueprintCallable, Category = "Settings|UI")
	void SetAutoRepelByIndex(int32 Index);

	// --------------------------------------------------------
	// STRING CONVERSION
	// These match the FString values stored on UGF_VaultSystem, so Blueprints
	// that read VaultSystem directly see the same spellings.
	// --------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Settings|Conversion")
	static FString TextSpeedToString(EGF_TextSpeed Speed);

	UFUNCTION(BlueprintPure, Category = "Settings|Conversion")
	static EGF_TextSpeed StringToTextSpeed(const FString& In);

	UFUNCTION(BlueprintPure, Category = "Settings|Conversion")
	static FString DifficultyToString(EGF_Difficulty Difficulty);

	UFUNCTION(BlueprintPure, Category = "Settings|Conversion")
	static EGF_Difficulty StringToDifficulty(const FString& In);

	UFUNCTION(BlueprintPure, Category = "Settings|Conversion")
	static FString WindowModeToString(EGF_WindowMode Mode);

	UFUNCTION(BlueprintPure, Category = "Settings|Conversion")
	static EGF_WindowMode StringToWindowMode(const FString& In);

	// Parses "WIDTHxHEIGHT". Returns false and leaves the outputs untouched on
	// a malformed string.
	static bool ParseResolution(const FString& In, int32& OutWidth, int32& OutHeight);

private:
	UPROPERTY()
	TObjectPtr<UGF_SettingsSave> Save;

	// Applied effects, split so setters only redo the work they need to.
	// FadeTime < 0 means "use VolumeFadeTime from the config".
	// Returns false when the audio device was not ready, so the volume did NOT
	// actually land - the engine's sound mix calls no-op silently in that case.
	bool ApplyAudio(float FadeTime = -1.0f);

	// Applies now if it can, and starts the guard below either way.
	void ApplyAudioWhenReady();

	// The volume sliders are class overrides hung on the settings sound mix, so
	// ANY "Clear Sound Mix Modifiers" call anywhere in the project silently drops
	// the player's volume back to the sound class defaults - with no error, and
	// nothing to restore it until the next settings change. That is a landmine
	// for a project this size, so instead of trusting every Blueprint to behave,
	// this watches whether the settings mix is still active and re-applies the
	// moment it is not. It also covers the startup case, where a world exists
	// well before its audio device does and the first apply is thrown away.
	void StartAudioGuard();
	void TickAudioGuard();
	FTimerHandle AudioGuardHandle;

	// Guard ticks left in the startup re-assert window. The audio device pointer
	// goes non-null BEFORE the mixer actually starts (the boot log shows the
	// apply landing ~1ms ahead of XAudio2 init), and FAudioDevice::IsInitialized()
	// is protected, so a game module cannot ask whether it is ready. Instead the
	// guard simply re-applies every tick for the first couple of seconds, which
	// covers the device coming up underneath us regardless of the exact ordering.
	int32 AudioGuardWarmupTicks = 0;
	void ApplyVideo();
	void ApplyDepthOfField();
	void ApplyFrameRateLimit();
	void ApplyVSync();

	// Shared tail of every setter.
	void HandleSettingChanged();

	// Snaps a saved resolution back into range if it fails IsResolutionAllowed -
	// e.g. a save written before enforcement was turned on, or hand-edited.
	void SanitizeResolution();

	// > 0 while a settings UI is populating itself. See BeginLoadingUI.
	int32 LoadingUIDepth = 0;

	// One outstanding PushAudioDuck request. Plain struct, no UPROPERTY needed -
	// it holds no object references.
	struct FGF_AudioDuck
	{
		FName Tag;
		float MusicMultiplier;
		float SFXMultiplier;
	};

	TArray<FGF_AudioDuck> ActiveDucks;

	// Quietest active request, or 1.0 when nothing is ducking.
	float GetMusicDuckMultiplier() const;
	float GetSFXDuckMultiplier() const;

	// Debounced disk write - avoids hammering the file while a slider is dragged.
	void ScheduleSave();
	FTimerHandle SaveDebounceHandle;

	// Re-push the settings mirror. GammaFrameworkCreatures calls this once its
	// FGF_CreatureBridge hooks are bound, replacing the old InitializeDependency
	// ordering guarantee that Core can no longer express.
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Settings")
	void RequestVaultSync();
};
