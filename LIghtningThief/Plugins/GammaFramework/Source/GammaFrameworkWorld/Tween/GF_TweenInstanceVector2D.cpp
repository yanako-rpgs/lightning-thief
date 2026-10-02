// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook

#include "Tween/GF_TweenInstanceVector2D.h"

void FGF_TweenInstanceVector2D::Initialize(
	FVector2D InStart, FVector2D InEnd, TFunction<void(FVector2D)> InOnUpdate, float InDurationSecs, EGF_Ease InEaseType)
{
	this->StartValue = InStart;
	this->EndValue = InEnd;
	this->OnUpdate = MoveTemp(InOnUpdate);
	this->InitializeSharedMembers(InDurationSecs, InEaseType);
}

void FGF_TweenInstanceVector2D::ApplyEasing(float EasedPercent)
{
	OnUpdate(FMath::Lerp<FVector2D>(StartValue, EndValue, EasedPercent));
}
