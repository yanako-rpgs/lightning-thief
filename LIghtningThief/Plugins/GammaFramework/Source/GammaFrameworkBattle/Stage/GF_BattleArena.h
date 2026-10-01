// Gamma Framework -- everything a battle needs from the level it is fought in.
//
// One of these is placed in each arena level. The launcher finds it after the
// level streams in and reads the camera, the seats and where the Tamer stands
// off it, so adding an arena is duplicating a level and moving things about
// rather than wiring anything up.
//
// Arenas are streamed in far above the overworld rather than loaded as their
// own map, so the overworld stays put underneath and nothing has to be saved
// and restored around a fight.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Turn/GF_BattleTypes.h"
#include "GF_BattleArena.generated.h"

class UCameraComponent;
class ACameraActor;
class AGF_BattleAnchor;
class UBillboardComponent;

UCLASS(Blueprintable, meta = (DisplayName = "Battle Arena"))
class GAMMAFRAMEWORKBATTLE_API AGF_BattleArena : public AActor
{
	GENERATED_BODY()

public:
	AGF_BattleArena();

	/**
	 * The shot. Frame it in the viewport like any other camera.
	 *
	 * A component rather than a separate CameraActor, because the arena and its
	 * camera are one thing: move the arena and the shot comes with it, and there
	 * is no second actor to forget to assign. SetViewTargetWithBlend uses this
	 * automatically -- an actor with a camera component IS a camera.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GammaFramework|Arena")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * An external camera to use instead.
	 *
	 * For the arena whose shot needs to live somewhere else -- on a rig, on a
	 * spline, on something that moves. Empty means use the component above.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "GammaFramework|Arena")
	TObjectPtr<ACameraActor> CameraOverride;

	/** Seconds to blend from the overworld view to the arena, and back again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Arena",
	          meta = (ClampMin = "0.0", UIMax = "3.0"))
	float CameraBlendTime = 0.6f;

	/** Where the Tamer stands. Behind their own line, facing the far side. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "GammaFramework|Arena")
	TObjectPtr<AActor> PlayerStandPoint;

	/**
	 * Where a creature in this seat belongs.
	 *
	 * Reads the AGF_BattleAnchor actors in the arena's own level. Seats with no
	 * anchor placed fall back to the computed wedge, so a half-dressed arena
	 * still fights.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Arena")
	FTransform GetSeatTransform(EGF_BattleSide Side, int32 Index) const;

	/** Where the camera should sit -- the camera actor, or this actor if none. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Arena")
	AActor* GetViewTarget() const;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TObjectPtr<UBillboardComponent> Billboard;
#endif
};
