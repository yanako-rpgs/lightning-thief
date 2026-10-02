// MIT License - Copyright (c) 2022 Jared Cook
#pragma once

#include "Tween/GF_TweenInstance.h"
class GAMMAFRAMEWORKWORLD_API FGF_TweenInstanceVector2D : public FGF_TweenInstance
{
public:
	FVector2D StartValue;
	FVector2D EndValue;
	TFunction<void(FVector2D)> OnUpdate;

	void Initialize(
		FVector2D InStart, FVector2D InEnd, TFunction<void(FVector2D)> InOnUpdate, float InDurationSecs, EGF_Ease InEaseType);

protected:
	virtual void ApplyEasing(float EasedPercent) override;
};
