// MIT License - Copyright (c) 2022 Jared Cook

#pragma once
#include "Tween/GF_Easing.h"
#include "Tween/GF_TweenInstance.h"
#include "Tween/GF_TweenInstanceFloat.h"
#include "Tween/GF_TweenInstanceQuat.h"
#include "Tween/GF_TweenInstanceVector.h"
#include "Tween/GF_TweenInstanceVector2D.h"
#include "Tween/GF_TweenManager.h"

GAMMAFRAMEWORKWORLD_API DECLARE_LOG_CATEGORY_EXTERN(LogGFTween, Log, All)

class GAMMAFRAMEWORKWORLD_API FGF_Tween
{
private:
	static FGF_TweenManager<FGF_TweenInstanceFloat>* FloatTweenManager;
	static FGF_TweenManager<FGF_TweenInstanceVector>* VectorTweenManager;
	static FGF_TweenManager<FGF_TweenInstanceVector2D>* Vector2DTweenManager;
	static FGF_TweenManager<FGF_TweenInstanceQuat>* QuatTweenManager;

	static int NumReservedFloat;
	static int NumReservedVector;
	static int NumReservedVector2D;
	static int NumReservedQuat;

public:
	static void Initialize();
	static void Deinitialize();

	/**
	 * @brief Ensure there are at least this many tweens in the recycle pool. Call this at game startup to increase your initial
	 * capacity for each type of tween, if you know you will be needing more and don't want to allocate memory during the game.
	 */
	static void EnsureCapacity(int NumFloatTweens, int NumVectorTweens, int NumVector2DTweens, int NumQuatTweens);
	/**
	 * @brief Add more tweens to the recycle pool. Call this at game startup to increase your initial capacity if you know you will
	 * be needing more and don't want to allocate memory during the game.
	 */
	static void EnsureCapacity(int NumTweens);
	static void Update(float UnscaledDeltaSeconds, float DilatedDeltaSeconds, bool bIsGamePaused);
	static void ClearActiveTweens();

	/**
	 * @brief compare the current reserved memory for tweens against the initial capacity, to tell the developer if initial capacity needs to be increased
	 */
	static int CheckTweenCapacity();

	/**
	 * @brief Convenience function for FGF_Easing::Ease()
	 */
	static float Ease(float t, EGF_Ease EaseType);

	static FGF_TweenInstanceFloat* Play(
		float Start, float End, TFunction<void(float)> OnUpdate, float DurationSecs, EGF_Ease EaseType = EGF_Ease::OutQuad);

	static FGF_TweenInstanceVector* Play(
		FVector Start, FVector End, TFunction<void(FVector)> OnUpdate, float DurationSecs, EGF_Ease EaseType = EGF_Ease::OutQuad);

	static FGF_TweenInstanceVector2D* Play(FVector2D Start, FVector2D End, TFunction<void(FVector2D)> OnUpdate, float DurationSecs,
		EGF_Ease EaseType = EGF_Ease::OutQuad);

	static FGF_TweenInstanceQuat* Play(
		FQuat Start, FQuat End, TFunction<void(FQuat)> OnUpdate, float DurationSecs, EGF_Ease EaseType = EGF_Ease::OutQuad);
};
