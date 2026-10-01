// CreatureSpeciesDataThumbnailRenderer.h
#pragma once
#include "ThumbnailRendering/ThumbnailRenderer.h"
#include "GF_CreatureSpeciesDataThumbnailRenderer.generated.h"

UCLASS()
class GAMMAFRAMEWORKEDITOR_API UGF_CreatureSpeciesDataThumbnailRenderer : public UThumbnailRenderer
{
    GENERATED_BODY()
public:
    virtual bool CanVisualizeAsset(UObject* Object) override;
    virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
        FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;
};
