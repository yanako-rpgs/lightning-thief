// MIT License - Copyright (c) 2022 Jared Cook
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "GF_TweenSubsystem.generated.h"

UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_TweenSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

private:
	UPROPERTY()
	uint64 LastTickedFrame;
	UPROPERTY()
	float LastRealTimeSeconds;

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override;
	virtual bool IsTickableInEditor() const override;
};
