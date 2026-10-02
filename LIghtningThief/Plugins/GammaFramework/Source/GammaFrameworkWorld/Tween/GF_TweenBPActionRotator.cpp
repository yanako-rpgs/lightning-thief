// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook
#include "Tween/GF_TweenBPActionRotator.h"

#include "Tween/GF_Tween.h"

UGF_TweenBPActionRotator* UGF_TweenBPActionRotator::TweenRotator(FRotator Start, FRotator End, float DurationSecs, EGF_Ease EaseType,
	float EaseParam1, float EaseParam2, float Delay, int Loops, float LoopDelay, bool bYoyo, float YoyoDelay,
	bool bCanTickDuringPause, bool bUseGlobalTimeDilation)
{
	UGF_TweenBPActionRotator* BlueprintNode = NewObject<UGF_TweenBPActionRotator>();
	BlueprintNode->SetSharedTweenProperties(
		DurationSecs, Delay, Loops, LoopDelay, bYoyo, YoyoDelay, bCanTickDuringPause, bUseGlobalTimeDilation);
	BlueprintNode->EaseType = EaseType;
	BlueprintNode->Start = Start.Quaternion();
	BlueprintNode->End = End.Quaternion();
	BlueprintNode->EaseParam1 = EaseParam1;
	BlueprintNode->EaseParam2 = EaseParam2;
	return BlueprintNode;
}

UGF_TweenBPActionRotator* UGF_TweenBPActionRotator::TweenRotatorCustomCurve(FRotator Start, FRotator End, float DurationSecs,
	UCurveFloat* Curve, float Delay, int Loops, float LoopDelay, bool bYoyo, float YoyoDelay, bool bCanTickDuringPause,
	bool bUseGlobalTimeDilation)
{
	UGF_TweenBPActionRotator* BlueprintNode = NewObject<UGF_TweenBPActionRotator>();
	BlueprintNode->SetSharedTweenProperties(
		DurationSecs, Delay, Loops, LoopDelay, bYoyo, YoyoDelay, bCanTickDuringPause, bUseGlobalTimeDilation);
	BlueprintNode->CustomCurve = Curve;
	BlueprintNode->bUseCustomCurve = true;
	BlueprintNode->Start = Start.Quaternion();
	BlueprintNode->End = End.Quaternion();
	BlueprintNode->EaseParam1 = 0;
	BlueprintNode->EaseParam2 = 0;
	return BlueprintNode;
}

FGF_TweenInstance* UGF_TweenBPActionRotator::CreateTween()
{
	return FGF_Tween::Play(
		Start, End, [&](FQuat t) { ApplyEasing.Broadcast(t.Rotator()); }, DurationSecs, EaseType);
}

FGF_TweenInstance* UGF_TweenBPActionRotator::CreateTweenCustomCurve()
{
	return FGF_Tween::Play(
		0, 1,
		[&](float t)
		{
			float EasedTime = CustomCurve->GetFloatValue(t);
			FQuat EasedValue = FMath::Lerp(Start, End, EasedTime);
			ApplyEasing.Broadcast(EasedValue.Rotator());
		},
		DurationSecs, EaseType);
}