#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_MeleeStagingLibrary.generated.h"

class UPaperFlipbook;
class UPaperFlipbookComponent;

/**
 * How much room a battle sprite actually takes up on screen, in world units.
 *
 * The only honest source for this is the sprite QUAD, not the actor: an AGF_Creature's root is
 * a bare scene component with the flipbook hung off a pivot below it, so the actor's location
 * says nothing about where the artwork sits, and GetActorBounds folds in every collision shape,
 * spotlight and widget the creature is carrying. Duskmaw and Ashcoil would come out the same
 * size. Measuring the quad gives the silhouette and nothing else.
 */
USTRUCT(BlueprintType)
struct FGF_SpriteFootprint
{
	GENERATED_BODY()

	/** Half the sprite's width along its own right axis, world units. This is the number that decides how far away to land. */
	UPROPERTY(BlueprintReadOnly, Category = "Staging")
	float HalfWidth = 0.0f;

	/** Full height of the quad, world units. Useful for aiming an arc over the target's head. */
	UPROPERTY(BlueprintReadOnly, Category = "Staging")
	float Height = 0.0f;

	/** Centre of the quad in world space. */
	UPROPERTY(BlueprintReadOnly, Category = "Staging")
	FVector Center = FVector::ZeroVector;

	/** Bottom-centre of the quad in world space -- where the creature is standing, whatever its pivot says. */
	UPROPERTY(BlueprintReadOnly, Category = "Staging")
	FVector Feet = FVector::ZeroVector;

	/** The sprite's right direction in world space, flattened to horizontal. The screen-left/right axis. */
	UPROPERTY(BlueprintReadOnly, Category = "Staging")
	FVector RightAxis = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Staging")
	bool bValid = false;
};

/**
 * Works out where a creature has to stand for a contact move to read as a contact move.
 *
 * The problem this solves is that "move to the target's location" puts the attacker INSIDE the
 * target, and "move to the target's location minus 60 units" works for one pair of creatures and
 * breaks for every other pair, because the flipbooks are all different sizes. A leaping Gnash
 * has to land a distance away that is the sum of two silhouettes plus a gap -- and both halves
 * of that sum are unknown until runtime, when you know who is fighting whom.
 *
 * So: measure both quads, add the halves, place the attacker's FEET rather than its origin. Feet
 * matter because the pivots are inconsistent between sprites; landing by origin makes a tall
 * creature float and a short one sink.
 */
UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_MeleeStagingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Measures the sprite quad a flipbook component is drawing, in world space.
	 *
	 * @param FlipbookComponent  The battle sprite to measure.
	 * @param bUseWidestFrame    Union of EVERY frame instead of the frame on screen right now.
	 *                           Leave this on. A flipbook's bounds are the CURRENT keyframe's
	 *                           bounds, so measuring a playing animation gives a width that
	 *                           breathes with the idle bob -- and a landing spot that slides
	 *                           around depending on which frame you happened to ask on.
	 * @param MeasureFlipbook    Measure this flipbook instead of the one currently assigned.
	 *                           Pass the creature's idle animation to get a stable answer even
	 *                           while it is mid-attack with a limb thrown out wide.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Staging", meta = (AdvancedDisplay = "MeasureFlipbook"))
	static FGF_SpriteFootprint GetSpriteFootprint(
		UPaperFlipbookComponent* FlipbookComponent,
		bool bUseWidestFrame = true,
		UPaperFlipbook* MeasureFlipbook = nullptr);

	/**
	 * Where the attacking ACTOR should end up so that it stands beside the target, not on it.
	 *
	 * Returns a location for the attacker's actor root, already corrected for the offset between
	 * that root and the artwork, so it can be fed straight to SetActorLocation or used as a tween
	 * target. The attacker keeps the side it started on -- a creature on the left of the screen
	 * lands on the target's left -- so nothing teleports across the field before it leaps.
	 *
	 * @param AttackerFlipbook  Sprite of the creature doing the leaping.
	 * @param TargetFlipbook    Sprite of the creature being hit.
	 * @param Gap               Clear air between the two silhouettes, world units. This is the
	 *                          only number to tune by eye; everything else is measured.
	 * @param bMatchTargetGround Stand on the target's ground line rather than keeping the
	 *                          attacker's own. On for a 4v4 board where the rows sit at
	 *                          different heights -- otherwise a back-row attacker leaps at a
	 *                          front-row target and hangs in the air above it.
	 * @param DepthBias         Nudge toward the camera so the attacker draws in front of the
	 *                          target rather than intersecting it. A few units is plenty.
	 * @param bUseWidestFrame   See GetSpriteFootprint. Leave on.
	 * @param AttackerHalfWidthOverride  Non-zero replaces the measured half-width. For sprites
	 *                          whose frames carry a lot of transparent padding, or a tail that
	 *                          should not count as body.
	 * @param TargetHalfWidthOverride    Same, for the target.
	 * @param SideOverride      -1 or +1 forces which side of the target to land on, in screen
	 *                          terms. 0 keeps the side the attacker already stands on.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Staging",
		meta = (AdvancedDisplay = "DepthBias,bUseWidestFrame,AttackerHalfWidthOverride,TargetHalfWidthOverride,SideOverride"))
	static FVector GetMeleeApproachLocation(
		UPaperFlipbookComponent* AttackerFlipbook,
		UPaperFlipbookComponent* TargetFlipbook,
		float Gap = 12.0f,
		bool bMatchTargetGround = true,
		float DepthBias = 2.0f,
		bool bUseWidestFrame = true,
		float AttackerHalfWidthOverride = 0.0f,
		float TargetHalfWidthOverride = 0.0f,
		float SideOverride = 0.0f);

	/**
	 * A point on a parabola from Start to End, for the hop itself.
	 *
	 * Drive Alpha 0..1 from a tween and feed the result to SetActorLocation. Separating the arc
	 * from the destination is deliberate: the destination is computed once when the move starts,
	 * so the target moving or fainting mid-leap cannot drag the attacker off course.
	 *
	 * @param ArcHeight  Peak of the hop above the straight line, world units. Scale it off the
	 *                   target's footprint Height to clear a big creature and stay low over a
	 *                   small one.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Staging")
	static FVector GetLeapArcLocation(FVector Start, FVector End, float Alpha, float ArcHeight = 40.0f);
};
