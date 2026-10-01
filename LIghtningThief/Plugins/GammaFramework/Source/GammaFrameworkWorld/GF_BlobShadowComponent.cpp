#include "GF_BlobShadowComponent.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"

UGF_BlobShadowComponent::UGF_BlobShadowComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    // Read the owner AFTER it has moved this frame. On a tick group earlier than this the
    // height trace samples last frame position, and a creature at the top of a jump arc gets
    // a shadow one frame out of step with it.
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

    // Decals project along their own -X, so pitching the component down by 90 aims that axis
    // at the floor. Same value ADecalActor uses for exactly the same reason.
    SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));

    // Blobs are small and they are the thing anchoring the creature to the ground, so they
    // must not be the first decal the renderer drops when the screen gets busy.
    FadeScreenSize = 0.0f;

    DecalSize = FVector(ProjectionDepth, ManualRadius, ManualRadius);
    DecalColor = ShadowColor;
}

void UGF_BlobShadowComponent::OnRegister()
{
    Super::OnRegister();

    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }

    // Measuring here rather than only at BeginPlay is what makes the blob visible in the
    // Blueprint viewport, which is the only place anyone is realistically going to sit and
    // dial FootprintRatio in.
    SetDecalColor(ShadowColor);
    RefreshSize();
}

void UGF_BlobShadowComponent::BeginPlay()
{
    Super::BeginPlay();

    // The owner yaws to face its grid direction and the sprite billboards on top of that.
    // Neither should be able to tip the projection off vertical, so the blob keeps its own
    // world rotation and just points at the floor forever.
    SetUsingAbsoluteRotation(true);
    SetWorldRotation(FRotator(-90.0f, 0.0f, 0.0f));

    SetDecalColor(ShadowColor);

    if (bDisableDecalsOnSprite)
    {
        if (UPaperFlipbookComponent* FlipbookComponent = ResolveFlipbook())
        {
            FlipbookComponent->SetReceivesDecals(false);
        }
    }

    // A creature spawned by the battle manager gets its flipbook assigned after the component
    // is up, so a failed measurement here is expected rather than an error. Retry on tick and
    // stop as soon as there is a sprite to read.
    if (!RefreshSize() && bAutoSize)
    {
        bAwaitingMeasurement = true;
    }

    if (bAwaitingMeasurement || bFadeWithHeight)
    {
        SetComponentTickEnabled(true);
    }
}

void UGF_BlobShadowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bAwaitingMeasurement && RefreshSize())
    {
        bAwaitingMeasurement = false;
    }

    if (bFadeWithHeight)
    {
        UpdateHeightFade();
        return;
    }

    if (!bAwaitingMeasurement)
    {
        // Nothing left to do every frame. Sizing only ever changes through RefreshSize().
        SetComponentTickEnabled(false);
    }
}

UPaperFlipbookComponent* UGF_BlobShadowComponent::ResolveFlipbook() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    if (UPaperFlipbookComponent* Picked = Cast<UPaperFlipbookComponent>(SourceFlipbook.GetComponent(Owner)))
    {
        return Picked;
    }

    // Most creature Blueprints have exactly one flipbook, so leaving the picker empty and
    // taking whatever is there is the common case rather than a fallback.
    return Owner->FindComponentByClass<UPaperFlipbookComponent>();
}

float UGF_BlobShadowComponent::MeasureSpriteWidth() const
{
    UPaperFlipbookComponent* FlipbookComponent = ResolveFlipbook();
    if (!IsValid(FlipbookComponent))
    {
        return 0.0f;
    }

    UPaperFlipbook* Book = MeasureFlipbook ? MeasureFlipbook.Get() : FlipbookComponent->GetFlipbook();
    if (!IsValid(Book))
    {
        return 0.0f;
    }

    double MinX = TNumericLimits<double>::Max();
    double MaxX = TNumericLimits<double>::Lowest();

    // Union of every frame rather than the frame on screen. A book whose sprites are cropped
    // per-frame would otherwise breathe, and one stable footprint for the whole animation is
    // both cheaper and what the eye expects from a shadow.
    const int32 NumFrames = Book->GetNumFrames();
    for (int32 Frame = 0; Frame < NumFrames; ++Frame)
    {
        UPaperSprite* Sprite = Book->GetSpriteAtFrame(Frame);
        if (!IsValid(Sprite))
        {
            // Keyframes are allowed to hold nothing -- that is how a blink or a gap is authored.
            continue;
        }

        // BakedRenderData is the sprite render triangles: XY is the quad corner in local XZ
        // relative to the pivot. Deliberately NOT GetSourceSize(), which lives behind
        // WITH_EDITORONLY_DATA and would compile in the editor then vanish when packaged.
        for (const FVector4& Vertex : Sprite->BakedRenderData)
        {
            MinX = FMath::Min(MinX, Vertex.X);
            MaxX = FMath::Max(MaxX, Vertex.X);
        }
    }

    if (MaxX <= MinX)
    {
        return 0.0f;
    }

    // Push the local width through the full component transform and take its horizontal
    // length. Scale is the part that matters day to day, but this also stays honest if a
    // sprite is ever tilted such that its width axis leans out of the ground plane.
    const FVector WorldWidth = FlipbookComponent->GetComponentTransform().TransformVector(
        FVector(static_cast<float>(MaxX - MinX), 0.0f, 0.0f));

    return static_cast<float>(WorldWidth.Size2D());
}

bool UGF_BlobShadowComponent::RefreshSize()
{
    bool bMeasured = false;
    float Radius = ManualRadius;

    if (bAutoSize)
    {
        const float SpriteWidth = MeasureSpriteWidth();
        if (SpriteWidth > KINDA_SMALL_NUMBER)
        {
            Radius = SpriteWidth * 0.5f * FootprintRatio;
            bMeasured = true;
        }
    }

    BaseRadius = FMath::Clamp(Radius * ShadowScale, MinRadius, MaxRadius);
    ApplyRadius(BaseRadius, ProjectionDepth);

    return bMeasured;
}

void UGF_BlobShadowComponent::SetShadowScale(float NewScale)
{
    ShadowScale = FMath::Max(NewScale, 0.0f);
    RefreshSize();
}

void UGF_BlobShadowComponent::ApplyRadius(float WorldRadius, float WorldDepth)
{
    // DecalSize is in local space and the component transform is applied on top of it, so a
    // scaled owner would otherwise scale the blob a second time on top of the sprite width we
    // already measured in world units. Divide it back out and the radius means what it says.
    const FVector Scale = GetComponentScale();
    const float ScaleX = FMath::Max(FMath::Abs(static_cast<float>(Scale.X)), KINDA_SMALL_NUMBER);
    const float ScaleY = FMath::Max(FMath::Abs(static_cast<float>(Scale.Y)), KINDA_SMALL_NUMBER);
    const float ScaleZ = FMath::Max(FMath::Abs(static_cast<float>(Scale.Z)), KINDA_SMALL_NUMBER);

    // Y and Z get the SAME radius. The blob is a circle on the ground; the tilted camera is
    // what turns it into an ellipse on screen, and squashing it here would do that twice.
    DecalSize = FVector(WorldDepth / ScaleX, WorldRadius / ScaleY, WorldRadius / ScaleZ);
    MarkRenderStateDirty();

    CurrentRadius = WorldRadius;
}

bool UGF_BlobShadowComponent::MeasureHeightAboveGround(float& OutHeight) const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const FVector Start = GetComponentLocation();
    const FVector End = Start - FVector(0.0f, 0.0f, MaxTraceDistance);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(GF_BlobShadowGround), /*bTraceComplex=*/false, GetOwner());

    FHitResult Hit;
    if (!World->LineTraceSingleByChannel(Hit, Start, End, GroundTraceChannel, Params))
    {
        return false;
    }

    OutHeight = static_cast<float>(Start.Z - Hit.ImpactPoint.Z);
    return true;
}

void UGF_BlobShadowComponent::UpdateHeightFade()
{
    float Height = 0.0f;
    if (!MeasureHeightAboveGround(Height))
    {
        // Nothing underneath -- a creature out over a ledge, or collision that is simply not
        // there. Leave the blob at full strength rather than popping it, since the projection
        // box will find a floor on its own if one comes back into range.
        SetDecalColor(ShadowColor);
        ApplyRadius(BaseRadius, ProjectionDepth);
        return;
    }

    const float Span = FMath::Max(FadeEndHeight - FadeStartHeight, 1.0f);
    const float Alpha = FMath::Clamp((Height - FadeStartHeight) / Span, 0.0f, 1.0f);

    FLinearColor Faded = ShadowColor;
    Faded.A *= FMath::Lerp(1.0f, MinHeightOpacity, Alpha);
    SetDecalColor(Faded);

    // Grow the projection box to reach whatever floor the trace actually found. Without this
    // the blob would simply disappear the moment a jump arc took the creature further up than
    // ProjectionDepth, which is the one case this whole feature exists to handle.
    const float Depth = FMath::Max(ProjectionDepth, Height + ProjectionDepth * 0.5f);

    ApplyRadius(BaseRadius * FMath::Lerp(1.0f, MinHeightScale, Alpha), Depth);
}

#if WITH_EDITOR
void UGF_BlobShadowComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Every tuning knob on this component is cheap to re-apply, so re-apply all of them and
    // let the viewport show the result while the value is still under the cursor.
    SetDecalColor(ShadowColor);
    RefreshSize();
}
#endif
