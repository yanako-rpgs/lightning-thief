// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GF_GridMovementComponent.h"
#include "GF_GridNPCAIComponent.h"
#include "PaperFlipbookComponent.h"
#include "GF_Follower.generated.h"

UENUM (BlueprintType)
enum class EGF_Animations : uint8
{
	WalkDown UMETA(DisplayName = "Down"),
	WalkLeft UMETA(DisplayName = "Left"),
	WalkRight UMETA(DisplayName = "Right"),
	WalkUp UMETA(DisplayName = "Up")

};

UCLASS()
class GAMMAFRAMEWORKWORLD_API AGF_Follower : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AGF_Follower();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USceneComponent* SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UGF_GridMovementComponent* MovementComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UGF_GridNPCAIComponent* AIComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UPaperFlipbookComponent* SpriteComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower Animations")
	TMap<EGF_Animations, UPaperFlipbook*> FollowerAnimations;

    /**
     * Vertical offset applied to the sprite so it sits on top of the floor tile
     * instead of half-underground. Tune this per Blueprint child to match the
     * height of each Creature's sprite sheet (e.g. 8.0, 16.0, 24.0).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower", meta = (ClampMin = "-256.0", ClampMax = "256.0"))
    float FollowerZOffset = 16.0f;

	UFUNCTION(BlueprintCallable, Category = "Follower")
    void SetupFollower(AActor* PlayerActor);

	UFUNCTION(BlueprintCallable, Category = "Setup")
	void InitializeAnimations();

    /** Teleports the follower to the tile directly behind the player on spawn. */
    UFUNCTION(BlueprintCallable, Category = "Follower")
    void SpawnBehindPlayer(AActor* Player);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    virtual void Tick(float DeltaTime) override;

private:
    /**
     * Called one tick after BeginPlay so that the player's GridMovementComponent
     * has finished its own BeginPlay (including the MovementHistory seed) before
     * we try to read the floor Z from it.
     */
    UFUNCTION()
    void DeferredInitialization();

    FTimerHandle DeferredInitTimerHandle;

};
