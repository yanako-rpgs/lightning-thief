#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GF_WidgetMirrorActor.generated.h"

class FWidgetRenderer;
class UTextureRenderTarget2D;
class UUserWidget;
class UStaticMeshComponent;

UCLASS()
class GAMMAFRAMEWORKWORLD_API AGF_WidgetMirrorActor : public AActor
{
	GENERATED_BODY()

public:
	AGF_WidgetMirrorActor();

protected:
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;

public:
	virtual void Tick(float DeltaTime) override;

	// Set this from Blueprint after your HUD widget is created
	UPROPERTY(BlueprintReadWrite, Category = "Widget Mirror")
	UUserWidget* BattleHUDWidget;

	// The 3D plane mesh - material on Element 0 must have a WidgetTex texture parameter
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Widget Mirror")
	UStaticMeshComponent* DisplayMesh;

private:
	UPROPERTY()
	UTextureRenderTarget2D* WidgetRenderTarget;

	FWidgetRenderer* WidgetRenderer;
};
