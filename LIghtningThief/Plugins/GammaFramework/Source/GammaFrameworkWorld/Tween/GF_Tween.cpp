// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook
#include "Tween/GF_Tween.h"

#include "Tween/GF_TweenManager.h"

DEFINE_LOG_CATEGORY(LogGFTween)

const int DEFAULT_FLOAT_TWEEN_CAPACITY = 50;
const int DEFAULT_VECTOR_TWEEN_CAPACITY = 50;
const int DEFAULT_VECTOR2D_TWEEN_CAPACITY = 50;
const int DEFAULT_QUAT_TWEEN_CAPACITY = 10;

FGF_TweenManager<FGF_TweenInstanceFloat>* FGF_Tween::FloatTweenManager = nullptr;
FGF_TweenManager<FGF_TweenInstanceVector>* FGF_Tween::VectorTweenManager = nullptr;
FGF_TweenManager<FGF_TweenInstanceVector2D>* FGF_Tween::Vector2DTweenManager = nullptr;
FGF_TweenManager<FGF_TweenInstanceQuat>* FGF_Tween::QuatTweenManager = nullptr;

int FGF_Tween::NumReservedFloat = DEFAULT_FLOAT_TWEEN_CAPACITY;
int FGF_Tween::NumReservedVector = DEFAULT_VECTOR_TWEEN_CAPACITY;
int FGF_Tween::NumReservedVector2D = DEFAULT_VECTOR2D_TWEEN_CAPACITY;
int FGF_Tween::NumReservedQuat = DEFAULT_QUAT_TWEEN_CAPACITY;

void FGF_Tween::Initialize()
{
	FloatTweenManager = new FGF_TweenManager<FGF_TweenInstanceFloat>(DEFAULT_FLOAT_TWEEN_CAPACITY);
	VectorTweenManager = new FGF_TweenManager<FGF_TweenInstanceVector>(DEFAULT_VECTOR_TWEEN_CAPACITY);
	Vector2DTweenManager = new FGF_TweenManager<FGF_TweenInstanceVector2D>(DEFAULT_VECTOR2D_TWEEN_CAPACITY);
	QuatTweenManager = new FGF_TweenManager<FGF_TweenInstanceQuat>(DEFAULT_QUAT_TWEEN_CAPACITY);
	
	NumReservedFloat = DEFAULT_FLOAT_TWEEN_CAPACITY;
	NumReservedVector = DEFAULT_VECTOR_TWEEN_CAPACITY;
	NumReservedVector2D = DEFAULT_VECTOR2D_TWEEN_CAPACITY;
	NumReservedQuat = DEFAULT_QUAT_TWEEN_CAPACITY;
}

void FGF_Tween::Deinitialize()
{
	delete FloatTweenManager;
	delete VectorTweenManager;
	delete Vector2DTweenManager;
	delete QuatTweenManager;
}

void FGF_Tween::EnsureCapacity(int NumFloatTweens, int NumVectorTweens, int NumVector2DTweens, int NumQuatTweens)
{
	FloatTweenManager->EnsureCapacity(NumFloatTweens);
	VectorTweenManager->EnsureCapacity(NumVectorTweens);
	Vector2DTweenManager->EnsureCapacity(NumVector2DTweens);
	QuatTweenManager->EnsureCapacity(NumQuatTweens);
	
	NumReservedFloat = FloatTweenManager->GetCurrentCapacity();
	NumReservedVector = VectorTweenManager->GetCurrentCapacity();
	NumReservedVector2D = Vector2DTweenManager->GetCurrentCapacity();
	NumReservedQuat = QuatTweenManager->GetCurrentCapacity();
}

void FGF_Tween::EnsureCapacity(int NumTweens)
{
	EnsureCapacity(NumTweens, NumTweens, NumTweens, NumTweens);
}

void FGF_Tween::Update(float UnscaledDeltaSeconds, float DilatedDeltaSeconds, bool bIsGamePaused)
{
	FloatTweenManager->Update(UnscaledDeltaSeconds, DilatedDeltaSeconds, bIsGamePaused);
	VectorTweenManager->Update(UnscaledDeltaSeconds, DilatedDeltaSeconds, bIsGamePaused);
	Vector2DTweenManager->Update(UnscaledDeltaSeconds, DilatedDeltaSeconds, bIsGamePaused);
	QuatTweenManager->Update(UnscaledDeltaSeconds, DilatedDeltaSeconds, bIsGamePaused);
}

void FGF_Tween::ClearActiveTweens()
{
	FloatTweenManager->ClearActiveTweens();
	VectorTweenManager->ClearActiveTweens();
	Vector2DTweenManager->ClearActiveTweens();
	QuatTweenManager->ClearActiveTweens();
}

int FGF_Tween::CheckTweenCapacity()
{
	if(FloatTweenManager->GetCurrentCapacity() > NumReservedFloat)
	{
		UE_LOG(LogGFTween, Warning, TEXT("Consider increasing initial capacity for Float tweens with FGF_Tween::EnsureCapacity(). %d were initially reserved, but now there are %d in memory."),
			NumReservedFloat, FloatTweenManager->GetCurrentCapacity());
	}
	if(VectorTweenManager->GetCurrentCapacity() > NumReservedVector)
	{
		UE_LOG(LogGFTween, Warning, TEXT("Consider increasing initial capacity for Vector (3d vector) tweens with FGF_Tween::EnsureCapacity(). %d were initially reserved, but now there are %d in memory."),
			NumReservedVector, VectorTweenManager->GetCurrentCapacity());
	}
	if(Vector2DTweenManager->GetCurrentCapacity() > NumReservedVector2D)
	{
		UE_LOG(LogGFTween, Warning, TEXT("Consider increasing initial capacity for Vector2D tweens with FGF_Tween::EnsureCapacity(). %d were initially reserved, but now there are %d in memory."),
			NumReservedVector2D, Vector2DTweenManager->GetCurrentCapacity());
	}
	if(QuatTweenManager->GetCurrentCapacity() > NumReservedQuat)
	{
		UE_LOG(LogGFTween, Warning, TEXT("Consider increasing initial capacity for Quaternion tweens with FGF_Tween::EnsureCapacity(). %d were initially reserved, but now there are %d in memory."),
			NumReservedQuat, QuatTweenManager->GetCurrentCapacity());
	}

	return FloatTweenManager->GetCurrentCapacity() + VectorTweenManager->GetCurrentCapacity() +  Vector2DTweenManager->GetCurrentCapacity() + QuatTweenManager->GetCurrentCapacity();
}

float FGF_Tween::Ease(float t, EGF_Ease EaseType)
{
	return FGF_Easing::Ease(t, EaseType);
}

FGF_TweenInstanceFloat* FGF_Tween::Play(float Start, float End, TFunction<void(float)> OnUpdate, float DurationSecs, EGF_Ease EaseType)
{
	FGF_TweenInstanceFloat* NewTween = FloatTweenManager->CreateTween();
	NewTween->Initialize(Start, End, MoveTemp(OnUpdate), DurationSecs, EaseType);
	return NewTween;
}

FGF_TweenInstanceVector* FGF_Tween::Play(
	FVector Start, FVector End, TFunction<void(FVector)> OnUpdate, float DurationSecs, EGF_Ease EaseType)
{
	FGF_TweenInstanceVector* NewTween = VectorTweenManager->CreateTween();
	NewTween->Initialize(Start, End, MoveTemp(OnUpdate), DurationSecs, EaseType);
	return NewTween;
}

FGF_TweenInstanceVector2D* FGF_Tween::Play(
	FVector2D Start, FVector2D End, TFunction<void(FVector2D)> OnUpdate, float DurationSecs, EGF_Ease EaseType)
{
	FGF_TweenInstanceVector2D* NewTween = Vector2DTweenManager->CreateTween();
	NewTween->Initialize(Start, End, MoveTemp(OnUpdate), DurationSecs, EaseType);
	return NewTween;
}

FGF_TweenInstanceQuat* FGF_Tween::Play(FQuat Start, FQuat End, TFunction<void(FQuat)> OnUpdate, float DurationSecs, EGF_Ease EaseType)
{
	FGF_TweenInstanceQuat* NewTween = QuatTweenManager->CreateTween();
	NewTween->Initialize(Start, End, MoveTemp(OnUpdate), DurationSecs, EaseType);
	return NewTween;
}
