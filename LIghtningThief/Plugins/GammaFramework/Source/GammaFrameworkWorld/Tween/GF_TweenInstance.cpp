// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook
#include "Tween/GF_TweenInstance.h"

#include "Tween/GF_TweenUObject.h"

FGF_TweenInstance* FGF_TweenInstance::SetDelay(float InDelaySecs)
{
	this->DelaySecs = InDelaySecs;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetLoops(int InNumLoops)
{
	this->NumLoops = InNumLoops;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetLoopDelay(float InLoopDelaySecs)
{
	this->LoopDelaySecs = InLoopDelaySecs;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetYoyo(bool bInShouldYoyo)
{
	this->bShouldYoyo = bInShouldYoyo;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetYoyoDelay(float InYoyoDelaySecs)
{
	this->YoyoDelaySecs = InYoyoDelaySecs;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetTimeMultiplier(float InTimeMultiplier)
{
	this->TimeMultiplier = FMath::Abs(InTimeMultiplier);
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetEaseParam1(float InEaseParam1)
{
	this->EaseParam1 = InEaseParam1;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetEaseParam2(float InEaseParam2)
{
	this->EaseParam2 = InEaseParam2;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetCanTickDuringPause(bool bInCanTickDuringPause)
{
	this->bCanTickDuringPause = bInCanTickDuringPause;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetUseGlobalTimeDilation(bool bInUseGlobalTimeDilation)
{
	this->bUseGlobalTimeDilation = bInUseGlobalTimeDilation;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetAutoDestroy(bool bInShouldAutoDestroy)
{
	this->bShouldAutoDestroy = bInShouldAutoDestroy;
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetOnYoyo(TFunction<void()> Handler)
{
	this->OnYoyo = MoveTemp(Handler);
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetOnLoop(TFunction<void()> Handler)
{
	this->OnLoop = MoveTemp(Handler);
	return this;
}

FGF_TweenInstance* FGF_TweenInstance::SetOnComplete(TFunction<void()> Handler)
{
	this->OnComplete = MoveTemp(Handler);
	return this;
}

void FGF_TweenInstance::InitializeSharedMembers(float InDurationSecs, EGF_Ease InEaseType)
{
	checkf(InDurationSecs > 0, TEXT("Tween received duration <= 0"));
	if (InDurationSecs <= 0)
	{
		this->DurationSecs = .001f;
	}
	else
	{
		this->DurationSecs = InDurationSecs;
	}
	this->EaseType = InEaseType;
	Counter = 0;
	DelayCounter = 0;
	bShouldAutoDestroy = true;
	bIsActive = true;
	bIsPaused = false;
	bShouldYoyo = false;
	bIsPlayingYoyo = false;
	bCanTickDuringPause = false;
	bUseGlobalTimeDilation = true;

	NumLoops = 1;
	NumLoopsCompleted = 0;
	DelaySecs = 0;
	LoopDelaySecs = 0;
	YoyoDelaySecs = 0;
	EaseParam1 = 0;
	EaseParam2 = 0;
	TimeMultiplier = 1.0f;

	DelayState = EGF_TweenDelayState::None;

#if ENGINE_MAJOR_VERSION < 5
	OnYoyo = nullptr;
	OnLoop = nullptr;
	OnComplete = nullptr;
#else
	OnYoyo.Reset();
	OnLoop.Reset();
	OnComplete.Reset();
#endif
}

void FGF_TweenInstance::Start()
{
	DelayCounter = DelaySecs;
	if (DelayCounter > 0)
	{
		DelayState = EGF_TweenDelayState::Start;
	}
	else
	{
		DelayState = EGF_TweenDelayState::None;
	}
}

void FGF_TweenInstance::Restart()
{
	if (bIsActive)
	{
		Counter = 0;
		bIsPlayingYoyo = false;
		NumLoopsCompleted = 0;
		Unpause();
		Start();
	}
}

void FGF_TweenInstance::Destroy()
{
	// mark for recycling
	bIsActive = false;

#if ENGINE_MAJOR_VERSION < 5
	OnLoop  = nullptr;
	OnYoyo  = nullptr;
	OnComplete = nullptr;
#else
	OnLoop.Reset();
	OnYoyo.Reset();
	OnComplete.Reset();
#endif
}

UGF_TweenUObject* FGF_TweenInstance::CreateUObject(UObject* Outer)
{
	UGF_TweenUObject* Wrapper = NewObject<UGF_TweenUObject>(Outer);
	Wrapper->SetTweenInstance(this);
	return Wrapper;
}

void FGF_TweenInstance::Pause()
{
	bIsPaused = true;
}

void FGF_TweenInstance::Unpause()
{
	bIsPaused = false;
}

void FGF_TweenInstance::Update(float UnscaledDeltaSeconds, float DilatedDeltaSeconds, bool bIsGamePaused)
{
	if (bIsPaused || !bIsActive || bIsGamePaused && !bCanTickDuringPause)
	{
		return;
	}

	float DeltaTime = bUseGlobalTimeDilation ? DilatedDeltaSeconds : UnscaledDeltaSeconds;
	DeltaTime *= TimeMultiplier;

	if (DelayCounter > 0)
	{
		DelayCounter -= DeltaTime;
		if (DelayCounter <= 0)
		{
			switch (DelayState)
			{
				case EGF_TweenDelayState::Loop:
					if (OnLoop)
					{
						OnLoop();
					}
					break;
				case EGF_TweenDelayState::Yoyo:
					if (OnYoyo)
					{
						OnYoyo();
					}
					break;
			}
		}
	}
	else
	{
		if (bIsPlayingYoyo)
		{
			Counter -= DeltaTime;
		}
		else
		{
			Counter += DeltaTime;
		}

		Counter = FMath::Clamp<float>(Counter, 0, DurationSecs);

		ApplyEasing(FGF_Easing::EaseWithParams(Counter / DurationSecs, EaseType, EaseParam1, EaseParam2));

		if (bIsPlayingYoyo)
		{
			if (Counter <= 0)
			{
				CompleteLoop();
			}
		}
		else
		{
			if (Counter >= DurationSecs)
			{
				if (bShouldYoyo)
				{
					StartYoyo();
				}
				else
				{
					CompleteLoop();
				}
			}
		}
	}
}

void FGF_TweenInstance::CompleteLoop()
{
	++NumLoopsCompleted;
	if (NumLoops < 0 || NumLoopsCompleted < NumLoops)
	{
		StartNewLoop();
	}
	else
	{
		if (OnComplete)
		{
			OnComplete();
		}
		if (bShouldAutoDestroy)
		{
			Destroy();
		}
		else
		{
			Pause();
		}
	}
}

void FGF_TweenInstance::StartNewLoop()
{
	DelayCounter = LoopDelaySecs;
	Counter = 0;
	bIsPlayingYoyo = false;
	if (DelayCounter > 0)
	{
		DelayState = EGF_TweenDelayState::Loop;
	}
	else
	{
		if (OnLoop)
		{
			OnLoop();
		}
	}
}

void FGF_TweenInstance::StartYoyo()
{
	bIsPlayingYoyo = true;
	DelayCounter = YoyoDelaySecs;
	if (DelayCounter > 0)
	{
		DelayState = EGF_TweenDelayState::Yoyo;
	}
	else
	{
		if (OnYoyo)
		{
			OnYoyo();
		}
	}
}