// MIT License - Copyright (c) 2022 Jared Cook
#pragma once
#include "Tween/GF_TweenInstance.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "GF_TweenBPAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_TweenEventOutputPin);

UCLASS(Abstract, BlueprintType, meta = (ExposedAsyncProxy = AsyncTask))
class GAMMAFRAMEWORKWORLD_API UGF_TweenBPAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:
	float DurationSecs;
	EGF_Ease EaseType;
	float Delay;
	int Loops;
	float LoopDelay;
	bool bYoyo;
	float YoyoDelay;
	bool bCanTickDuringPause;
	bool bUseGlobalTimeDilation;
	float EaseParam1;
	float EaseParam2;

	bool bUseCustomCurve;
	UPROPERTY()
	UCurveFloat* CustomCurve;

	FGF_TweenInstance* TweenInstance = nullptr;

	UPROPERTY(BlueprintAssignable, AdvancedDisplay)
	FGF_TweenEventOutputPin OnLoop;

	UPROPERTY(BlueprintAssignable, AdvancedDisplay)
	FGF_TweenEventOutputPin OnYoyo;

	UPROPERTY(BlueprintAssignable, AdvancedDisplay)
	FGF_TweenEventOutputPin OnComplete;

	virtual void Activate() override;
	virtual FGF_TweenInstance* CreateTween();
	virtual FGF_TweenInstance* CreateTweenCustomCurve();
	virtual void SetSharedTweenProperties(float InDurationSecs, float InDelay, int InLoops, float InLoopDelay, bool InbYoyo,
		float InYoyoDelay, bool bInCanTickDuringPause, bool bInUseGlobalTimeDilation);
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Tween")
	void Pause();
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Tween")
	void Unpause();
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Tween")
	void Restart();
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Tween")
	void Stop();
	UFUNCTION(BlueprintCallable, Category = "Gamma Framework|Tween")
	void SetTimeMultiplier(float Multiplier);
};
