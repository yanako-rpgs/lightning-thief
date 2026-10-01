#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_DownedBurstLibrary.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UPaperFlipbookComponent;
class UTexture2D;

/**
 * Everything Niagara needs to rebuild the CURRENT frame of a flipbook out of particles.
 *
 * A Paper2D sprite is a quad cut out of a shared atlas texture, so "draw one particle per
 * opaque pixel" needs two things that only the sprite asset knows: which texture it landed
 * in, and which rectangle of that texture is this particular frame. Both are in here, plus
 * the size of the quad in local units so the particle grid can be laid out on top of it.
 */
USTRUCT(BlueprintType)
struct FGF_GEFlipbookFrameSample
{
	GENERATED_BODY()

	/** The atlas page this frame was baked into. Feed straight to a Sample Texture module. */
	UPROPERTY(BlueprintReadOnly, Category = "Down Burst")
	TObjectPtr<UTexture2D> Texture = nullptr;

	/** Top-left of this frame inside Texture, normalised 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Down Burst")
	FVector2D UVMin = FVector2D::ZeroVector;

	/** Width/height of this frame inside Texture, normalised 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Down Burst")
	FVector2D UVSize = FVector2D::ZeroVector;

	/**
	 * Size of the sprite quad in the flipbook component's LOCAL space (Unreal units).
	 * X is local X, Y is local Z -- Paper2D lays its quads out in the XZ plane, so a particle
	 * grid built from this must use X/Z and leave local Y at zero.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Down Burst")
	FVector2D LocalSize = FVector2D::ZeroVector;

	/** Centre of that quad relative to the component's pivot, same XZ convention as LocalSize. */
	UPROPERTY(BlueprintReadOnly, Category = "Down Burst")
	FVector2D LocalCenter = FVector2D::ZeroVector;

	/** True pixel dimensions of the frame. One particle per pixel means this many particles. */
	UPROPERTY(BlueprintReadOnly, Category = "Down Burst")
	FIntPoint FrameSizeInPixels = FIntPoint::ZeroValue;

	/**
	 * FrameSizeInPixels scaled down proportionally to respect a particle budget. Use this as
	 * the emitter's grid dimensions -- it keeps the sprite's aspect ratio, so the silhouette
	 * still reads correctly even when the budget forces a coarser grid.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Down Burst")
	FIntPoint GridResolution = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Down Burst")
	bool bValid = false;
};

/**
 * Turns a battle flipbook into a cloud of particles that flies at the ball.
 *
 * The flipbook itself is never touched -- this reads the frame that is on screen right now,
 * hands its texture and atlas rectangle to a Niagara system as user parameters, and hides the
 * sprite. Niagara spawns a grid over the quad, samples the texture at each grid point, kills
 * the particles that land on transparent pixels, and what survives is a pixel-accurate copy of
 * the Creature that you can then blow apart however you like.
 *
 * The Niagara system is expected to declare these User parameters (exact names, exact types):
 *
 *   User.SpriteTexture     Texture Object    the atlas page
 *   User.SpriteUVMin       Vector2D          frame rect, normalised
 *   User.SpriteUVSize      Vector2D          frame rect, normalised
 *   User.SpriteLocalSize   Vector2D          quad size, local units (X, Z)
 *   User.SpriteLocalCenter Vector2D          quad centre vs pivot (X, Z)
 *   User.GridResolution    Vector2D          how many grid points to spawn on each axis
 *   User.TargetLocation    Vector            where the motes converge, WORLD space
 *   User.BurstDuration     float             seconds the whole effect should take
 *
 * Anything the system does not declare is simply skipped with a Niagara log warning, so a
 * half-built system still runs -- it just ignores the parameters it has not got yet.
 */
UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_DownBurstLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Reads the frame a flipbook component is currently showing.
	 *
	 * Safe to call when the flipbook has finished playing -- it clamps to the last frame rather
	 * than returning nothing, which is exactly the case that matters here because the burst
	 * fires at the END of the down animation.
	 *
	 * @param FlipbookComponent   The battle sprite.
	 * @param MaxParticles        Budget used to fill in GridResolution. 0 means "no limit".
	 * @param OutSample           Filled in on success; zeroed with bValid=false on failure.
	 * @return True if a usable frame was found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Down Burst", meta = (DisplayName = "Get Flipbook Frame Sample"))
	static bool GetFlipbookFrameSample(UPaperFlipbookComponent* FlipbookComponent, int32 MaxParticles, FGF_GEFlipbookFrameSample& OutSample);

	/**
	 * One-call version: sample the frame, spawn the burst on top of the sprite, hide the sprite.
	 *
	 * The Niagara component is attached to the flipbook component rather than placed in the
	 * world, which matters more than it looks: the battle sprites are authored pre-tilted (their
	 * RelativeRotation reclines them to face the fixed camera), and attaching means the particle
	 * grid inherits that tilt for free. Build the grid in world space instead and the silhouette
	 * comes out sheared.
	 *
	 * @param FlipbookComponent    The battle sprite to dissolve.
	 * @param BurstSystem          Niagara system declaring the User parameters listed above.
	 * @param TargetWorldLocation  Where the motes converge -- the ball, in world space.
	 * @param BurstDuration        Seconds the effect should run for.
	 * @param MaxParticles         Particle budget; drives GridResolution.
	 * @param bHideFlipbook        Hide the sprite once the burst is running. Almost always yes.
	 * @return The spawned component, or null if the sample or the spawn failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Down Burst", meta = (DisplayName = "Spawn Flipbook Down Burst", AdvancedDisplay = "MaxParticles,bHideFlipbook"))
	static UNiagaraComponent* SpawnFlipbookDownBurst(
		UPaperFlipbookComponent* FlipbookComponent,
		UNiagaraSystem* BurstSystem,
		FVector TargetWorldLocation,
		float BurstDuration = 1.0f,
		int32 MaxParticles = 20000,
		bool bHideFlipbook = true);

	/**
	 * Pushes an already-sampled frame onto an existing Niagara component.
	 *
	 * Only needed if you are driving the component yourself -- for instance re-sampling every
	 * frame so the particles track a still-animating sprite before the burst kicks in.
	 * Set parameters BEFORE activating, or spawn-time modules read the defaults instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Down Burst")
	static void ApplyFrameSampleToNiagara(UNiagaraComponent* NiagaraComponent, const FGF_GEFlipbookFrameSample& Sample);
};
