// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook

#include "Tween/GF_TweenInstanceVector.h"

void FGF_TweenInstanceVector::Initialize(
	FVector InStart, FVector InEnd, TFunction<void(FVector)> InOnUpdate, float InDurationSecs, EGF_Ease InEaseType)
{
	this->StartValue = InStart;
	this->EndValue = InEnd;
	this->OnUpdate = MoveTemp(InOnUpdate);
	this->InitializeSharedMembers(InDurationSecs, InEaseType);
}

void FGF_TweenInstanceVector::ApplyEasing(float EasedPercent)
{
	OnUpdate(FMath::Lerp<FVector>(StartValue, EndValue, EasedPercent));
}
