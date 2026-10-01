#pragma once

#include "CoreMinimal.h"
#include "Components/DecalComponent.h"
#include "Engine/EngineTypes.h"
#include "GF_BlobShadowComponent.generated.h"

class UPaperFlipbook;
class UPaperFlipbookComponent;

/**
 * The soft dark blob a sprite stands on, sized off the sprite itself.
 *
 * Flipbooks cannot cast a usable shadow: the quad is authored pre-tilted to face the fixed
 * camera, so a real cast shadow comes out as a slanted rectangle rather than a creature. Every
 * 2.5D game that looks right about this - Dragon Quest, Octopath - fakes it with a soft ellipse
 * on the ground instead, and that is all this is: a downward-projecting decal whose radius is
 * measured from the sprite at spawn.
 *
 * Measuring is the whole point. With a hundred-odd species there is no world in which anyone
 * hand-tunes a shadow per creature, so RefreshSize() reads the sprite quad width out of the
 * flipbook and scales the blob to match. FootprintRatio pulls it in a bit because a silhouette
 * is always wider than the feet holding it up, and ShadowScale is the per-species escape hatch
 * for the outliers - the wide-winged bird, the long thin serpent - that the automatic number
 * gets wrong.
 *
 * Two things worth knowing before you tune anything:
 *
 *  - The blob is authored ROUND, not oval. The battle camera is tilted, so a circle on the
 *    ground already reads as an ellipse on screen. Squashing it here squashes it twice.
 *  - Deferred decals only land on surfaces that write to the GBuffer. An Unlit floor receives
 *    nothing at all, and the blob will simply be missing rather than wrong.
 *
 * Setup: drop it on the creature Blueprint, point SourceFlipbook at the sprite, point
 * MeasureFlipbook at the IDLE book, and call RefreshSize() once the flipbook has been assigned.
 * If the sprite is not ready yet at BeginPlay this retries on tick until it is, so the call is
 * belt-and-braces rather than required.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMMAFRAMEWORKWORLD_API UGF_BlobShadowComponent : public UDecalComponent
{
    GENERATED_BODY()

public:
    UGF_BlobShadowComponent();

    virtual void OnRegister() override;
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // ---------------------------------------------------------------- sizing

    /** The sprite to measure. Falls back to the first flipbook component on the actor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (UseComponentPicker, AllowedClasses = "/Script/Paper2D.PaperFlipbookComponent"))
    FComponentReference SourceFlipbook;

    /**
     * Which book to measure, rather than whatever happens to be playing.
     *
     * Point this at Idle. Measuring the live flipbook means an attack animation that lunges
     * forward widens the shadow for the duration of the lunge, and the blob visibly pumps.
     * Left empty this measures whatever the component holds when RefreshSize() runs, which is
     * usually Idle anyway but is not guaranteed to be.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow")
    TObjectPtr<UPaperFlipbook> MeasureFlipbook;

    /** Off means "use ManualRadius and never look at the sprite". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow")
    bool bAutoSize = true;

    /**
     * Blob diameter as a fraction of the sprite width.
     *
     * Below 1 because a silhouette is measured at its widest point - horns, wings, a tail -
     * and the shadow wants to sit under the part actually touching the ground. 0.8 reads well
     * on a normal quadruped; drop it for anything top-heavy.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "0.05", ClampMax = "2.0"))
    float FootprintRatio = 0.8f;

    /**
     * Per-creature multiplier on top of the measured size. This is the one to drive from
     * species data - leave FootprintRatio as the global rule and fix outliers here.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "0.0"))
    float ShadowScale = 1.0f;

    /** World-space blob radius used when bAutoSize is off, or when measuring fails. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "0.0"))
    float ManualRadius = 32.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", AdvancedDisplay, meta = (ClampMin = "0.0"))
    float MinRadius = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", AdvancedDisplay, meta = (ClampMin = "0.0"))
    float MaxRadius = 1024.0f;

    /**
     * How far the decal box reaches above and below the component, in world units.
     *
     * This is what makes small hops free: the box is centred on the component and projects
     * both ways, so a creature that leaves the floor still has ground inside its own projection
     * volume and keeps its shadow without a single trace being fired. Only raise it if blobs
     * vanish mid-jump.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow", meta = (ClampMin = "1.0"))
    float ProjectionDepth = 128.0f;

    /**
     * Stops the creature sprite from receiving its own shadow.
     *
     * The projection box is centred on the component, so it reaches up as far as it reaches
     * down, and a sprite standing in it is a decal receiver like any other surface - the blob
     * lands on the creature and washes it out. Shrinking ProjectionDepth until the sprite
     * escapes is the wrong trade: the upward half is what keeps the blob alive through a hop
     * and the downward half is what finds the floor on a slope. Take the sprite out of the
     * receiver set instead and the depth is free to be generous.
     *
     * This only fixes it at runtime. Untick "Receives Decals" on the flipbook component in the
     * Blueprint as well if you want the viewport to match.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow")
    bool bDisableDecalsOnSprite = true;

    // ----------------------------------------------------------------- look

    /**
     * Tint and strength, pushed into the DecalColor of this component.
     *
     * The material reads this through a Decal Color node, which is why every creature can share
     * ONE material instance - no dynamic material per creature, no per-instance parameters. RGB
     * tints the blob (near-black, or a cold blue if the scene lighting wants it) and A is the
     * strength the gradient is multiplied by.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow")
    FLinearColor ShadowColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.75f);

    // --------------------------------------------------------------- height

    /**
     * Shrink and fade the blob as the creature climbs away from the floor.
     *
     * Costs one downward trace per creature per frame, so it is off by default - a grounded
     * battler does not need it. Turn it on for anything that hovers or has a real jump arc,
     * where a shadow that stays hard and full size under a floating creature is the single
     * thing that most gives the trick away.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Height")
    bool bFadeWithHeight = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Height", meta = (EditCondition = "bFadeWithHeight"))
    TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

    /** Height at which fading starts. Below this the blob is at full strength. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Height", meta = (EditCondition = "bFadeWithHeight", ClampMin = "0.0"))
    float FadeStartHeight = 16.0f;

    /** Height at which the blob has reached MinHeightOpacity and MinHeightScale. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Height", meta = (EditCondition = "bFadeWithHeight", ClampMin = "1.0"))
    float FadeEndHeight = 256.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Height", meta = (EditCondition = "bFadeWithHeight", ClampMin = "0.0", ClampMax = "1.0"))
    float MinHeightOpacity = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Height", meta = (EditCondition = "bFadeWithHeight", ClampMin = "0.05", ClampMax = "1.0"))
    float MinHeightScale = 0.55f;

    /** How far down to look for a floor before giving up and leaving the blob alone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blob Shadow|Height", AdvancedDisplay, meta = (EditCondition = "bFadeWithHeight", ClampMin = "1.0"))
    float MaxTraceDistance = 2000.0f;

    // ------------------------------------------------------------------ API

    /**
     * Re-measures the sprite and resizes the blob.
     *
     * Call it after assigning the creature flipbook. Safe to call at any time and safe to call
     * repeatedly - it is a handful of float compares over one book of frames, not per-frame
     * work, and it is the only thing that ever writes DecalSize.
     *
     * @return True if the size came from a real measurement rather than the ManualRadius fallback.
     */
    UFUNCTION(BlueprintCallable, Category = "Blob Shadow")
    bool RefreshSize();

    /** Sets the per-creature multiplier and resizes in one call. */
    UFUNCTION(BlueprintCallable, Category = "Blob Shadow")
    void SetShadowScale(float NewScale);

    /** The blob current radius in world units, after every multiplier and clamp. */
    UFUNCTION(BlueprintPure, Category = "Blob Shadow")
    float GetShadowRadius() const { return CurrentRadius; }

    /**
     * The widest the measure book ever gets, in world units. 0 if nothing could be measured.
     *
     * Exposed because it is genuinely useful elsewhere - centring a health bar, sizing a
     * targeting reticle, spacing creatures apart on a battle line.
     */
    UFUNCTION(BlueprintPure, Category = "Blob Shadow")
    float MeasureSpriteWidth() const;

private:
    /**
     * Writes DecalSize from a world radius and projection depth, cancelling out the scale of
     * this component so both arguments mean world units regardless of how the owner is scaled.
     */
    void ApplyRadius(float WorldRadius, float WorldDepth);

    /** Resolves SourceFlipbook, falling back to the first flipbook component on the actor. */
    UPaperFlipbookComponent* ResolveFlipbook() const;

    /** Traces down for the floor. Returns false if there is nothing under us. */
    bool MeasureHeightAboveGround(float& OutHeight) const;

    void UpdateHeightFade();

    /** Radius before the height fade shrinks it, so the fade never compounds frame on frame. */
    float BaseRadius = 0.0f;

    float CurrentRadius = 0.0f;

    /** BeginPlay ran before the creature had its flipbook, so tick is retrying the measurement. */
    bool bAwaitingMeasurement = false;
};
