// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook
#include "Tween/GF_TweenBPAction.h"

#include "Tween/GF_Tween.h"

void UGF_TweenBPAction::Activate()
{
	if (TweenInstance != nullptr)
	{
		// restart the tween
		TweenInstance->Destroy();
		TweenInstance = nullptr;
	}
	if (DurationSecs <= 0)
	{
		FFrame::KismetExecutionMessage(TEXT("Duration must be more than 0"), ELogVerbosity::Error);
		return;
	}
	if (bUseCustomCurve)
	{
		if (CustomCurve != nullptr)
		{
			EaseType = EGF_Ease::Linear;
			TweenInstance = CreateTweenCustomCurve();
		}
		else
		{
			FFrame::KismetExecutionMessage(TEXT("No Custom Curve defined for custom curve task"), ELogVerbosity::Error);
			return;
		}
	}
	else
	{
		TweenInstance = CreateTween();
	}
	if (TweenInstance == nullptr)
	{
		FFrame::KismetExecutionMessage(TEXT("Tween Instance was not created in child class"), ELogVerbosity::Error);
		return;
	}
	TweenInstance->SetDelay(Delay)
		->SetLoops(Loops)
		->SetLoopDelay(LoopDelay)
		->SetYoyo(bYoyo)
		->SetYoyoDelay(YoyoDelay)
		->SetCanTickDuringPause(bCanTickDuringPause)
		->SetUseGlobalTimeDilation(bUseGlobalTimeDilation)
		// we will tell it when to be destroyed on complete, so that we control when
		// the tween goes invalid and it can't get recycled by doing something unexpected in BPs
		->SetAutoDestroy(false)
		->SetEaseParam1(EaseParam1)
		->SetEaseParam2(EaseParam2);

	if (OnLoop.IsBound())
	{
		TweenInstance->SetOnLoop([&]() { OnLoop.Broadcast(); });
	}
	if (OnYoyo.IsBound())
	{
		TweenInstance->SetOnYoyo([&]() { OnYoyo.Broadcast(); });
	}
	if (OnComplete.IsBound())
	{
		TweenInstance->SetOnComplete(
			[&]()
			{
				OnComplete.Broadcast();
				Stop();
			});
	}
}

FGF_TweenInstance* UGF_TweenBPAction::CreateTween()
{
	// override in specific data type tasks
	return nullptr;
}

FGF_TweenInstance* UGF_TweenBPAction::CreateTweenCustomCurve()
{
	return nullptr;
}

void UGF_TweenBPAction::SetSharedTweenProperties(float InDurationSecs, float InDelay, int InLoops, float InLoopDelay, bool InbYoyo,
	float InYoyoDelay, bool bInCanTickDuringPause, bool bInUseGlobalTimeDilation)
{
	TweenInstance = nullptr;
	bUseCustomCurve = false;
	CustomCurve = nullptr;
	DurationSecs = InDurationSecs;
	Delay = InDelay;
	Loops = InLoops;
	LoopDelay = InLoopDelay;
	bYoyo = InbYoyo;
	YoyoDelay = InYoyoDelay;
	bCanTickDuringPause = bInCanTickDuringPause;
	bUseGlobalTimeDilation = bInUseGlobalTimeDilation;
}

void UGF_TweenBPAction::BeginDestroy()
{
	Super::BeginDestroy();
	if (TweenInstance != nullptr)
	{
		TweenInstance->Destroy();
		TweenInstance = nullptr;
	}
}

void UGF_TweenBPAction::Pause()
{
	if (TweenInstance)
	{
		TweenInstance->Pause();
	}
}

void UGF_TweenBPAction::Unpause()
{
	if (TweenInstance)
	{
		TweenInstance->Unpause();
	}
}

void UGF_TweenBPAction::Restart()
{
	if (TweenInstance)
	{
		TweenInstance->Restart();
	}
}

void UGF_TweenBPAction::Stop()
{
	if (TweenInstance)
	{
		TweenInstance->Destroy();
		TweenInstance = nullptr;
		SetReadyToDestroy();
#if ENGINE_MAJOR_VERSION < 5
		MarkPendingKill();
#else
		MarkAsGarbage();
#endif
	}
}

void UGF_TweenBPAction::SetTimeMultiplier(float Multiplier)
{
	if (TweenInstance)
	{
		TweenInstance->SetTimeMultiplier(Multiplier);
	}
}
