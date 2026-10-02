// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook
#include "Tween/GF_TweenSubsystem.h"

#include "Tween/GF_Tween.h"
#include "Kismet/GameplayStatics.h"

void UGF_TweenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LastTickedFrame = GFrameCounter;

#if ENGINE_MAJOR_VERSION < 5
	if(GetWorld() != nullptr)
	{
		LastRealTimeSeconds = GetWorld()->RealTimeSeconds;
	}
#endif
	
#if WITH_EDITOR
	FGF_Tween::ClearActiveTweens();
#endif
}

void UGF_TweenSubsystem::Deinitialize()
{
	Super::Deinitialize();
#if WITH_EDITOR
	FGF_Tween::CheckTweenCapacity();
	FGF_Tween::ClearActiveTweens();
#endif
}

void UGF_TweenSubsystem::Tick(float DeltaTime)
{
	if (LastTickedFrame < GFrameCounter)
	{
		LastTickedFrame = GFrameCounter;

		if (GetWorld() != nullptr)
		{
#if ENGINE_MAJOR_VERSION < 5
			float DeltaRealTimeSeconds = GetWorld()->RealTimeSeconds - LastRealTimeSeconds;
			FGF_Tween::Update(DeltaRealTimeSeconds, GetWorld()->DeltaTimeSeconds, GetWorld()->IsPaused());
			LastRealTimeSeconds = GetWorld()->RealTimeSeconds;
#else
			FGF_Tween::Update(GetWorld()->DeltaRealTimeSeconds, GetWorld()->DeltaTimeSeconds, GetWorld()->IsPaused());
#endif
		}
	}
}

ETickableTickType UGF_TweenSubsystem::GetTickableTickType() const
{
	return ETickableTickType::Always;
}

TStatId UGF_TweenSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FGF_Tween, STATGROUP_Tickables);
}

bool UGF_TweenSubsystem::IsTickableWhenPaused() const
{
	return true;
}

bool UGF_TweenSubsystem::IsTickableInEditor() const
{
	return false;
}