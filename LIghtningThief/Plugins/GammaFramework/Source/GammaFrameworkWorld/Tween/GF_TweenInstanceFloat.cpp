// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook

#include "Tween/GF_TweenInstanceFloat.h"

void FGF_TweenInstanceFloat::Initialize(
	float InStart, float InEnd, TFunction<void(float)> InOnUpdate, float InDurationSecs, EGF_Ease InEaseType)
{
	this->StartValue = InStart;
	this->EndValue = InEnd;
	this->OnUpdate = MoveTemp(InOnUpdate);
	this->InitializeSharedMembers(InDurationSecs, InEaseType);
}

void FGF_TweenInstanceFloat::ApplyEasing(float EasedPercent)
{
	OnUpdate(FMath::Lerp<float>(StartValue, EndValue, EasedPercent));
}
