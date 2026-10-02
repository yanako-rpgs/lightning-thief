// MIT License - Copyright (c) 2022 Jared Cook
#pragma once

#include "Tween/GF_TweenInstance.h"
class GAMMAFRAMEWORKWORLD_API FGF_TweenInstanceFloat : public FGF_TweenInstance
{
public:
	float StartValue;
	float EndValue;
	TFunction<void(float)> OnUpdate;

	void Initialize(float InStart, float InEnd, TFunction<void(float)> InOnUpdate, float InDurationSecs, EGF_Ease InEaseType);

protected:
	virtual void ApplyEasing(float EasedPercent) override;
};
