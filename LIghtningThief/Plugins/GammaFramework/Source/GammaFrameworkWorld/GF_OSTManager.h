#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "GF_ResettableState.h"
#include "GF_OSTManager.generated.h"

UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_OSTManager : public UGameInstanceSubsystem, public FTickableGameObject, public IGF_ResettableState
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Music")
	void StartOST(USoundBase* IntroSound, USoundBase* LoopSound, bool bHasIntro, bool bFadeIn, float FadeDuration, float FadeVolume);

	UFUNCTION(BlueprintCallable, Category = "Music")
	void FadeOutOst(float FadeDuration);

	UFUNCTION(BlueprintCallable, Category = "Music")
	void SetPlaybackSpeed(float Speed);

	UFUNCTION(BlueprintPure, Category = "Music")
	float GetPlaybackSpeed() const { return CurrentPlaybackSpeed; }

	UFUNCTION(BlueprintCallable, Category = "Music")
	void StopOST();

	/** Wipes playback state back to defaults: stops the current track, drops the
	 *  pending loop, clears the loop timer and restores 1.0x speed.
	 *
	 *  Call this from New Game / Quit to Title.  This is a GameInstance subsystem,
	 *  so nothing here is reset by a level change -- a run that ended while the
	 *  music was sped up (low HP) would otherwise carry that rate into the next
	 *  playthrough, and a mid-intro quit would leave the loop never firing. */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void ResetPlaybackState();

	// IGF_ResettableState - ResetPlaybackState already does exactly this.
	virtual void ResetToBootState() override { ResetPlaybackState(); }

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UGF_OSTManager, STATGROUP_Tickables); }

private:
	UPROPERTY()
	UAudioComponent* OSTCurrentlyPlaying = nullptr;

	FTimerHandle LoopTimerHandle;

	UPROPERTY()
	USoundBase* PendingLoopSound = nullptr;
	bool bPendingFadeIn = false;
	float PendingFadeDuration = 0.0f;
	float PendingFadeVolume = 1.0f;

	float IntroTimeRemaining = 0.0f;
	bool bWaitingForIntroEnd = false;

	void PlayLoopTrack(USoundBase* LoopSound, bool bFadeIn, float FadeDuration, float FadeVolume);
	void ApplyCurrentSpeed();

	float CurrentPlaybackSpeed = 1.0f;
};