// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook
#include "Tween/GF_TweenBPActionVector.h"

#include "Tween/GF_Tween.h"

UGF_TweenBPActionVector* UGF_TweenBPActionVector::TweenVector(FVector Start, FVector End, float DurationSecs, EGF_Ease EaseType,
	float EaseParam1, float EaseParam2, float Delay, int Loops, float LoopDelay, bool bYoyo, float YoyoDelay,
	bool bCanTickDuringPause, bool bUseGlobalTimeDilation)
{
	UGF_TweenBPActionVector* BlueprintNode = NewObject<UGF_TweenBPActionVector>();
	BlueprintNode->SetSharedTweenProperties(
		DurationSecs, Delay, Loops, LoopDelay, bYoyo, YoyoDelay, bCanTickDuringPause, bUseGlobalTimeDilation);
	BlueprintNode->EaseType = EaseType;
	BlueprintNode->Start = Start;
	BlueprintNode->End = End;
	BlueprintNode->EaseParam1 = EaseParam1;
	BlueprintNode->EaseParam2 = EaseParam2;
	return BlueprintNode;
}

UGF_TweenBPActionVector* UGF_TweenBPActionVector::TweenVectorCustomCurve(FVector Start, FVector End, float DurationSecs,
	UCurveFloat* Curve, float Delay, int Loops, float LoopDelay, bool bYoyo, float YoyoDelay, bool bCanTickDuringPause,
	bool bUseGlobalTimeDilation)
{
	UGF_TweenBPActionVector* BlueprintNode = NewObject<UGF_TweenBPActionVector>();
	BlueprintNode->SetSharedTweenProperties(
		DurationSecs, Delay, Loops, LoopDelay, bYoyo, YoyoDelay, bCanTickDuringPause, bUseGlobalTimeDilation);
	BlueprintNode->CustomCurve = Curve;
	BlueprintNode->bUseCustomCurve = true;
	BlueprintNode->Start = Start;
	BlueprintNode->End = End;
	BlueprintNode->EaseParam1 = 0;
	BlueprintNode->EaseParam2 = 0;
	return BlueprintNode;
}

FGF_TweenInstance* UGF_TweenBPActionVector::CreateTween()
{
	return FGF_Tween::Play(
		Start, End, [&](FVector t) { ApplyEasing.Broadcast(t); }, DurationSecs, EaseType);
}

FGF_TweenInstance* UGF_TweenBPActionVector::CreateTweenCustomCurve()
{
	return FGF_Tween::Play(
		0, 1,
		[&](float t)
		{
			float EasedTime = CustomCurve->GetFloatValue(t);
			FVector EasedValue = FMath::Lerp(Start, End, EasedTime);
			ApplyEasing.Broadcast(EasedValue);
		},
		DurationSecs, EaseType);
}