// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook

#include "Tween/GF_TweenBlueprintLibrary.h"

#include "Tween/GF_Tween.h"

float UGF_TweenBlueprintLibrary::Ease(float t, EGF_Ease EaseType)

{
	return FGF_Easing::Ease(t, EaseType);
}

float UGF_TweenBlueprintLibrary::EaseWithParams(float t, EGF_Ease EaseType, float Param1, float Param2)
{
	return FGF_Easing::EaseWithParams(t, EaseType, Param1, Param2);
}

void UGF_TweenBlueprintLibrary::EnsureTweenCapacity(
	int NumFloatTweens, int NumVectorTweens, int NumVector2DTweens, int NumQuatTweens)
{
	FGF_Tween::EnsureCapacity(NumFloatTweens, NumVectorTweens, NumVector2DTweens, NumQuatTweens);
}
