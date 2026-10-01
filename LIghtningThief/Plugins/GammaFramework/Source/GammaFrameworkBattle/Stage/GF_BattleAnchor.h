// Gamma Framework -- a placeable marker for one seat on the battle field.
//
// Battles were spawning down a straight line computed in code, which reads
// badly in a 2.5D view: four creatures in a row occlude each other and there is
// no sense of a formation. Rather than tune numbers in C++ and rebuild to see
// them, drop one of these per seat and drag it around in the viewport.
//
// Anchors are optional. A level with none still fights -- GetSlotTransform
// falls back to a computed wedge -- so this never becomes a thing you must
// remember to place before a battle will work.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Turn/GF_BattleTypes.h"
#include "GF_BattleAnchor.generated.h"

class UBillboardComponent;
class UArrowComponent;

UCLASS(Blueprintable, meta = (DisplayName = "Battle Anchor"))
class GAMMAFRAMEWORKBATTLE_API AGF_BattleAnchor : public AActor
{
	GENERATED_BODY()

public:
	AGF_BattleAnchor();

	/** Which side of the field this seat belongs to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle")
	EGF_BattleSide Side = EGF_BattleSide::Player;

	/** Position in that side's active line, 0..3, matching FGF_BattleSlot::Index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle",
	          meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
	int32 Index = 0;

	/**
	 * Where the creature in this seat should stand.
	 *
	 * Returns the matching anchor's transform if one is placed, otherwise the
	 * computed fallback. bOutFromAnchor says which happened, so a caller can
	 * warn about a half-placed level rather than silently spawning some
	 * creatures at authored spots and the rest at defaults.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle",
	          meta = (WorldContext = "WorldContextObject"))
	static FTransform GetSlotTransform(const UObject* WorldContextObject,
	                                   EGF_BattleSide InSide, int32 InIndex,
	                                   bool& bOutFromAnchor);

	/**
	 * The formation used when a seat has no anchor placed.
	 *
	 * A staggered wedge rather than a row: each rank steps back from the enemy
	 * and sideways in depth, so no creature sits directly behind another.
	 */
	static FTransform DefaultSlotTransform(EGF_BattleSide InSide, int32 InIndex);

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TObjectPtr<UBillboardComponent> Billboard;

	UPROPERTY(Transient)
	TObjectPtr<UArrowComponent> Arrow;
#endif

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
#endif
};
