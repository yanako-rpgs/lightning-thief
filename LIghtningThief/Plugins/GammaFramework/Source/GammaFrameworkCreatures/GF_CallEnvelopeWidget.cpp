// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CallEnvelopeWidget.h"

void UGF_CallEnvelopeWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT("[CallVU] NativeConstruct - CallSubmix: %s"),
		CallSubmix ? *CallSubmix->GetName() : TEXT("NULL"));

	EnvelopeDelegate.BindDynamic(this, &UGF_CallEnvelopeWidgetBase::OnEnvelopeFollowed);

	UE_LOG(LogTemp, Warning, TEXT("[CallVU] Delegate bound: %s"),
		EnvelopeDelegate.IsBound() ? TEXT("YES") : TEXT("NO"));
}

void UGF_CallEnvelopeWidgetBase::SetCallSubmix(USoundSubmix* Submix)
{
	CallSubmix = Submix;
	UE_LOG(LogTemp, Warning, TEXT("[CallVU] SetCallSubmix - %s"),
		CallSubmix ? *CallSubmix->GetName() : TEXT("NULL"));
}

void UGF_CallEnvelopeWidgetBase::BeginListening()
{
	UE_LOG(LogTemp, Warning, TEXT("[CallVU] BeginListening called - CallSubmix: %s"),
		CallSubmix ? *CallSubmix->GetName() : TEXT("NULL"));

	if (!CallSubmix)
	{
		return;
	}

	CallSubmix->StartEnvelopeFollowing(this);
	CallSubmix->AddEnvelopeFollowerDelegate(this, EnvelopeDelegate);

	UE_LOG(LogTemp, Warning, TEXT("[CallVU] StartEnvelopeFollowing + AddDelegate done"));
}

void UGF_CallEnvelopeWidgetBase::NativeDestruct()
{
	if (CallSubmix)
	{
		CallSubmix->RemoveEnvelopeFollowerDelegate(this, EnvelopeDelegate);
		CallSubmix->StopEnvelopeFollowing(this);
	}

	Super::NativeDestruct();
}

void UGF_CallEnvelopeWidgetBase::OnEnvelopeFollowed(const TArray<float>& Envelope)
{
	UE_LOG(LogTemp, Warning, TEXT("[CallVU] OnEnvelopeFollowed fired! Num=%d, Val[0]=%.4f"),
		Envelope.Num(), Envelope.Num() > 0 ? Envelope[0] : -1.f);

	if (Envelope.Num() == 0)
	{
		return;
	}

	float Sum = 0.0f;
	for (float Value : Envelope)
	{
		Sum += Value;
	}

	RawAmplitude.Store(Sum / Envelope.Num());
}

void UGF_CallEnvelopeWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const float Target = RawAmplitude.Load();
	const float Speed = (Target > CurrentAmplitude) ? AttackSpeed : ReleaseSpeed;
	CurrentAmplitude = FMath::FInterpTo(CurrentAmplitude, Target, InDeltaTime, Speed);
}
