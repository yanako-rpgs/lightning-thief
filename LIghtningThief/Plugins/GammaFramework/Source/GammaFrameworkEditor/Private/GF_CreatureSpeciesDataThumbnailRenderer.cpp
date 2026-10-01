// CreatureSpeciesDataThumbnailRenderer.cpp
#include "GF_CreatureSpeciesDataThumbnailRenderer.h"
#include "GF_CreatureSpeciesData.h"

#include "PaperSprite.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"   // FCanvas -- Canvas->DrawItem() needs the complete type
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"

static void DrawSpriteRegionToCanvas(
    FCanvas* Canvas,
    UTexture2D* Texture,
    const FVector2D& SrcUV,     // in pixels
    const FVector2D& SrcSize,   // in pixels
    int32 X, int32 Y, uint32 W, uint32 H)
{
    if (!Canvas || !Texture || SrcSize.X <= 0.f || SrcSize.Y <= 0.f) return;

    const float TexW = static_cast<float>(Texture->GetSizeX());
    const float TexH = static_cast<float>(Texture->GetSizeY());

    // Normalize UVs to 0..1 for FCanvasTileItem
    const FVector2D UV0(SrcUV.X / TexW,              SrcUV.Y / TexH);
    const FVector2D UV1((SrcUV.X + SrcSize.X)/TexW, (SrcUV.Y + SrcSize.Y)/TexH);

    // Letterbox to preserve aspect ratio of the source rect
    const float srcAspect = SrcSize.X / SrcSize.Y;
    float drawW = static_cast<float>(W);
    float drawH = static_cast<float>(H);
    const float dstAspect = drawW / drawH;

    if (dstAspect > srcAspect) {
        drawW = drawH * srcAspect;
    } else {
        drawH = drawW / srcAspect;
    }
    const float dx = X + (W - drawW) * 0.5f;
    const float dy = Y + (H - drawH) * 0.5f;

    FCanvasTileItem TileItem(FVector2D(dx, dy), Texture->GetResource(),
                             FVector2D(drawW, drawH), UV0, UV1, FLinearColor::White);
    TileItem.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(TileItem);
}

bool UGF_CreatureSpeciesDataThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
    const UGF_CreatureSpeciesData* Species = Cast<UGF_CreatureSpeciesData>(Object);
    return Species && Species->DisplayIcon1 != nullptr;
}

void UGF_CreatureSpeciesDataThumbnailRenderer::Draw(
    UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
    FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)

{
    const UGF_CreatureSpeciesData* Species = CastChecked<UGF_CreatureSpeciesData>(Object);
    if (!Species->DisplayIcon1) return;

    UPaperSprite* Sprite = Species->DisplayIcon1;

    // Prefer baked texture; fall back to source texture
    UTexture2D* Tex = Cast<UTexture2D>(Sprite->GetBakedTexture());
    if (!Tex) {
        Tex = Sprite->GetSourceTexture();
    }
    if (!Tex) return;

    const FVector2D srcUV   = Sprite->GetSourceUV();   // pixels in the texture
    const FVector2D srcSize = Sprite->GetSourceSize(); // pixels in the texture

    DrawSpriteRegionToCanvas(Canvas, Tex, srcUV, srcSize, X, Y, Width, Height);
}
