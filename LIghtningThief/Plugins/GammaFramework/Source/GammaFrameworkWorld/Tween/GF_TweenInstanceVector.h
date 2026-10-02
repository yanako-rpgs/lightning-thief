// MIT License - Copyright (c) 2022 Jared Cook
#pragma once

#include "Tween/GF_TweenInstance.h"
class GAMMAFRAMEWORKWORLD_API FGF_TweenInstanceVector : public FGF_TweenInstance
{
public:
	FVector StartValue;
	FVector EndValue;
	TFunction<void(FVector)> OnUpdate;

	void Initialize(FVector InStart, FVector InEnd, TFunction<void(FVector)> InOnUpdate, float InDurationSecs, EGF_Ease InEaseType);

protected:
	virtual void ApplyEasing(float EasedPercent) override;
};
