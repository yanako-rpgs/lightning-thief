#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "GF_RouteData.h"
#include "GF_RouteVolume.generated.h"

/**
 * Place this in the world to define a route/area boundary.
 * When the player walks inside it, the RouteSubsystem is updated automatically.
 *
 * Usage:
 *   1. Drop a BP_RouteVolume into the level
 *   2. Set RouteData to your route data asset
 *   3. Resize the box to cover the area
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API AGF_RouteVolume : public AActor
{
    GENERATED_BODY()

public:
    AGF_RouteVolume();

    /** The route data asset for this area */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Route")
    UGF_RouteData* AreaData = nullptr;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere)
    UBoxComponent* Vault;

    UFUNCTION()
    void OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);

    /** Next-tick catch-up for a player who was already inside when this volume spawned. */
    void CheckAlreadyInsideOnSpawn();
};
