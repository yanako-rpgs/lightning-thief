// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundSubmixSend.h"   // FOnSubmixEnvelopeBP
#include "GF_CallEnvelopeWidget.generated.h"

/**
 * Base class for any widget that needs to react to Creature cry audio.
 * Reparent your existing Blueprint widget to this class, OR embed it as a child widget.
 *
 * Setup:
 *   1. Create a Sound Submix asset (Content Browser -> Sounds -> Sound Submix), name it SM_CreatureCall
 *   2. Assign it to "Call Submix" in the widget Details panel
 *   3. When playing a cry: AudioComponent -> Set Submix Send (SM_CreatureCall, 1.0) -> Play
 *   4. Read "Current Amplitude" (0.0-1.0) in Event Tick to drive your VU needle
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CallEnvelopeWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	// The submix to listen to. Set this via SetCallSubmix() in your parent widget's
	// Event Construct — EditDefaultsOnly doesn't survive child widget instantiation.
	UPROPERTY(BlueprintReadOnly, Category = "Call Audio")
	USoundSubmix* CallSubmix;

	// Call this in your parent widget's Event Construct to assign the submix.
	// Pass your SM_CompendiumCall asset as a direct reference.
	UFUNCTION(BlueprintCallable, Category = "Call Audio")
	void SetCallSubmix(USoundSubmix* Submix);

	// Call this from Blueprint AFTER Play.
	UFUNCTION(BlueprintCallable, Category = "Call Audio")
	void BeginListening();

	// Smoothed amplitude 0.0-1.0. Read this in Event Tick to drive your VU needle / bars.
	UPROPERTY(BlueprintReadOnly, Category = "Call Audio")
	float CurrentAmplitude = 0.0f;

	// How fast the value rises when the cry gets louder
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Call Audio")
	float AttackSpeed = 30.0f;

	// How fast the value falls when the cry gets quieter
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Call Audio")
	float ReleaseSpeed = 8.0f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// Must be UFUNCTION for dynamic delegate binding
	UFUNCTION()
	void OnEnvelopeFollowed(const TArray<float>& Envelope);

	// Stored so we can remove it in NativeDestruct
	FOnSubmixEnvelopeBP EnvelopeDelegate;

	// Written possibly on audio thread, read on game thread
	TAtomic<float> RawAmplitude{ 0.0f };
};
