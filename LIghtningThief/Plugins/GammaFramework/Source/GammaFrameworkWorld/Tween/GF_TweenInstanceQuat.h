// MIT License - Copyright (c) 2022 Jared Cook
#pragma once

#include "Tween/GF_TweenInstance.h"
class GAMMAFRAMEWORKWORLD_API FGF_TweenInstanceQuat : public FGF_TweenInstance
{
public:
	FQuat StartValue;
	FQuat EndValue;
	TFunction<void(FQuat)> OnUpdate;

	void Initialize(FQuat InStart, FQuat InEnd, TFunction<void(FQuat)> InOnUpdate, float InDurationSecs, EGF_Ease InEaseType);

protected:
	virtual void ApplyEasing(float EasedPercent) override;
};
