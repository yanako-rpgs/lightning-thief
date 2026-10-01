#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "GF_GridMovementComponent.h"
#include "GF_GridNPCAIComponent.h"
#include "GF_SimpleFollower.generated.h"

UCLASS()
class GAMMAFRAMEWORKWORLD_API AGF_SimpleFollower : public AActor
{
    GENERATED_BODY()

public:
    AGF_SimpleFollower();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UPaperFlipbookComponent* SpriteComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UGF_GridMovementComponent* MovementComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UGF_GridNPCAIComponent* NPCAIComponent;




    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Animations")
    TArray<UPaperFlipbook*> IdleAnimations;  // [0]=North, [1]=South, [2]=East, [3]=West

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Animations")
    TArray<UPaperFlipbook*> WalkAnimations;  // [0]=North, [1]=South, [2]=East, [3]=West

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower")
    int32 FollowDistance = 1;

    UPROPERTY(BlueprintReadWrite, Category = "Follower")
	int32 PartyIndex = -1;

    UFUNCTION(BlueprintCallable, Category = "Follower")
    void SetupFollower(AActor* PlayerActor);

    UFUNCTION(BlueprintCallable, Category = "Follower")
    bool SpawnFollower(AActor* PlayerActor);


private:
    UPaperFlipbookComponent* FollowerSprite = nullptr;

};