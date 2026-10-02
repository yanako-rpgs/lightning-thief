// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook
#include "Tween/GF_TweenUObject.h"

UGF_TweenUObject::UGF_TweenUObject()
{
	Tween = nullptr;
}
void UGF_TweenUObject::BeginDestroy()
{
	if (Tween != nullptr)
	{
		Tween->Destroy();
		Tween = nullptr;
	}
	UObject::BeginDestroy();
}

void UGF_TweenUObject::SetTweenInstance(FGF_TweenInstance* InTween)
{
	this->Tween = InTween;
	// destroy when we are destroyed
	this->Tween->SetAutoDestroy(false);
}

void UGF_TweenUObject::Destroy()
{
	this->Tween->Destroy();
	this->Tween = nullptr;
	ConditionalBeginDestroy();
}
