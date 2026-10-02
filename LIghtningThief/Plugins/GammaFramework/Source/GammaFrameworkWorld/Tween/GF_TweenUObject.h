// MIT License - Copyright (c) 2022 Jared Cook
#pragma once
#include "Tween/GF_TweenInstance.h"

#include "GF_TweenUObject.generated.h"

/**
 * @brief Use this to wrap an FGF_TweenInstance inside a UObject, so that it's destroyed when its outer object is destroyed
 */
UCLASS()
class UGF_TweenUObject : public UObject
{
	GENERATED_BODY()

public:
	FGF_TweenInstance* Tween;

	UGF_TweenUObject();
	virtual void BeginDestroy() override;

	void SetTweenInstance(FGF_TweenInstance* InTween);
	/**
	 * @brief Stop the tween immediately and mark this object for destruction
	 */
	void Destroy();
};
