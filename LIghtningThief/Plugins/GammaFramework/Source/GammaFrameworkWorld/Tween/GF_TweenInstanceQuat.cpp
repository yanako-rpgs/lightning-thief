// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook

#include "Tween/GF_TweenInstanceQuat.h"

void FGF_TweenInstanceQuat::Initialize(
	FQuat InStart, FQuat InEnd, TFunction<void(FQuat)> InOnUpdate, float InDurationSecs, EGF_Ease InEaseType)
{
	this->StartValue = InStart;
	this->EndValue = InEnd;
	this->OnUpdate = MoveTemp(InOnUpdate);
	this->InitializeSharedMembers(InDurationSecs, InEaseType);
}

void FGF_TweenInstanceQuat::ApplyEasing(float EasedPercent)
{
	OnUpdate(FQuat::Slerp(StartValue, EndValue, EasedPercent));
}
