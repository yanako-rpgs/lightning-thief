// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_OSTManager.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundCue.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void UGF_OSTManager::StartOST(USoundBase* IntroSound, USoundBase* LoopSound, bool bHasIntro, bool bFadeIn, float FadeDuration, float FadeVolume)
{
	if (OSTCurrentlyPlaying && OSTCurrentlyPlaying->IsPlaying() && (OSTCurrentlyPlaying->Sound == IntroSound || OSTCurrentlyPlaying->Sound == LoopSound))
	{
		return;
	}

	// Stop any pending intro wait
	bWaitingForIntroEnd = false;
	IntroTimeRemaining = 0.0f;

	// Remove the current OST so there aren't conflicts or 2 songs playing at once
	if (OSTCurrentlyPlaying)
	{
		OSTCurrentlyPlaying->Stop();
		OSTCurrentlyPlaying->DestroyComponent();
		OSTCurrentlyPlaying = nullptr;
	}

	if (!LoopSound) return;

	// Spawn the new Audio BGM
	OSTCurrentlyPlaying = UGameplayStatics::CreateSound2D(this, bHasIntro ? IntroSound : LoopSound);
	if (!OSTCurrentlyPlaying) return;

	OSTCurrentlyPlaying->bAutoDestroy = false;
	OSTCurrentlyPlaying->RegisterComponent();

	// Apply current playback speed to new track
	ApplyCurrentSpeed();

	if (bHasIntro && IntroSound)
	{
		OSTCurrentlyPlaying->SetSound(IntroSound);
		OSTCurrentlyPlaying->Play();

		float IntroDuration = IntroSound->GetDuration();
		IntroDuration -= 0.0f; // Calibration offset

		float TimeDilation = GetWorld()->GetWorldSettings()->TimeDilation;
		if (TimeDilation >= 0)
		{
			IntroDuration /= TimeDilation;
		}



		// Store pending loop info
		PendingLoopSound = LoopSound;
		bPendingFadeIn = bFadeIn;
		PendingFadeDuration = FadeDuration;
		PendingFadeVolume = FadeVolume;

		// Use real-time tracking instead of timer (immune to time dilation)
		IntroTimeRemaining = IntroDuration;
		bWaitingForIntroEnd = true;
	}
	else
	{
		PlayLoopTrack(LoopSound, bFadeIn, FadeDuration, FadeVolume);
	}
}

void UGF_OSTManager::Tick(float DeltaTime)
{
    // Auto-sync music speed with global time dilation (works with Slomo command!)
    if (UWorld* World = GetWorld())
    {
        float TimeDilation = World->GetWorldSettings()->TimeDilation;

        if (!FMath::IsNearlyEqual(TimeDilation, CurrentPlaybackSpeed, 0.01f))
        {
            SetPlaybackSpeed(TimeDilation);
        }
    }

    // Intro→loop transition (unchanged)
    if (bWaitingForIntroEnd)
    {
        float RealDelta = FApp::GetDeltaTime();
        IntroTimeRemaining -= RealDelta;

        if (IntroTimeRemaining <= 0.0f)
        {
            bWaitingForIntroEnd = false;
            PlayLoopTrack(PendingLoopSound, bPendingFadeIn, PendingFadeDuration, PendingFadeVolume);
            PendingLoopSound = nullptr;
        }
    }
}

void UGF_OSTManager::SetPlaybackSpeed(float Speed)
{
	CurrentPlaybackSpeed = FMath::Clamp(Speed, 0.5f, 3.0f);
	ApplyCurrentSpeed();
	UE_LOG(LogTemp, Log, TEXT("Music speed: %.1fx"), CurrentPlaybackSpeed);
}

void UGF_OSTManager::ApplyCurrentSpeed()
{
	if (OSTCurrentlyPlaying)
	{
		OSTCurrentlyPlaying->SetPitchMultiplier(CurrentPlaybackSpeed);
	}
}

void UGF_OSTManager::PlayLoopTrack(USoundBase* LoopSound, bool bFadeIn, float FadeDuration, float FadeVolume)
{
	if (!OSTCurrentlyPlaying) return;

	OSTCurrentlyPlaying->SetSound(LoopSound);

	// Apply current speed to the loop
	ApplyCurrentSpeed();

	if (bFadeIn)
	{
		OSTCurrentlyPlaying->FadeIn(FadeDuration, FadeVolume);
	}
	else
	{
		OSTCurrentlyPlaying->Play();
	}
}

void UGF_OSTManager::FadeOutOst(float FadeDuration)
{
	if (OSTCurrentlyPlaying)
	{
		OSTCurrentlyPlaying->FadeOut(FadeDuration, 0);
	}
}

void UGF_OSTManager::StopOST()
{
	bWaitingForIntroEnd = false;

	if (OSTCurrentlyPlaying)
	{
		OSTCurrentlyPlaying->Stop();
		OSTCurrentlyPlaying->DestroyComponent();
		OSTCurrentlyPlaying = nullptr;
	}
}

void UGF_OSTManager::ResetPlaybackState()
{
	// Stops and destroys the component and clears bWaitingForIntroEnd.
	StopOST();

	// The loop timer belongs to the world being torn down, so guard the lookup --
	// this can be called mid-teardown when GetWorld() is already null.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimerHandle);
	}
	LoopTimerHandle.Invalidate();

	PendingLoopSound    = nullptr;
	bPendingFadeIn      = false;
	PendingFadeDuration = 0.0f;
	PendingFadeVolume   = 1.0f;

	IntroTimeRemaining  = 0.0f;

	// Field only -- OSTCurrentlyPlaying is already null, so there is nothing to
	// apply the rate to. The next StartOST picks this up at normal speed.
	CurrentPlaybackSpeed = 1.0f;
}