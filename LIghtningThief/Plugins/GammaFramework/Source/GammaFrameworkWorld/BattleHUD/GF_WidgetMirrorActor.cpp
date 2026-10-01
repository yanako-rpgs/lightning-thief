#include "GF_WidgetMirrorActor.h"
#include "Slate/WidgetRenderer.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetRenderingLibrary.h"

static const FVector2D WidgetDrawSize(1920.f, 1080.f);

AGF_WidgetMirrorActor::AGF_WidgetMirrorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	RootComponent = DisplayMesh;

	WidgetRenderer = nullptr;
}

void AGF_WidgetMirrorActor::BeginPlay()
{
	Super::BeginPlay();

	WidgetRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	WidgetRenderTarget->RenderTargetFormat = RTF_RGBA16f;
	WidgetRenderTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	WidgetRenderTarget->bAutoGenerateMips = false;
	WidgetRenderTarget->InitAutoFormat((int32)WidgetDrawSize.X, (int32)WidgetDrawSize.Y);
	WidgetRenderTarget->UpdateResourceImmediate(true);

	WidgetRenderer = new FWidgetRenderer(true, false);

	UMaterialInterface* ExistingMat = DisplayMesh->GetMaterial(0);
	if (ExistingMat)
	{
		UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(ExistingMat, this);
		DynMat->SetTextureParameterValue(TEXT("WidgetTex"), WidgetRenderTarget);
		DisplayMesh->SetMaterial(0, DynMat);
	}
}

void AGF_WidgetMirrorActor::BeginDestroy()
{
	if (WidgetRenderer)
	{
		BeginCleanup(WidgetRenderer);
		WidgetRenderer = nullptr;
	}

	Super::BeginDestroy();
}

void AGF_WidgetMirrorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (BattleHUDWidget && WidgetRenderer && WidgetRenderTarget)
	{
		// Clear the render target before drawing to prevent ghosting
		UKismetRenderingLibrary::ClearRenderTarget2D(this, WidgetRenderTarget, FLinearColor(0.f, 0.f, 0.f, 0.f));

		WidgetRenderer->DrawWidget(
			WidgetRenderTarget,
			BattleHUDWidget->TakeWidget(),
			WidgetDrawSize,
			DeltaTime,
			false
		);
	}
}