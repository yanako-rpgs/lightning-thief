// Gamma Framework -- spawning battle effects at the right place and facing.
//
// The awkward part is not spawning a Niagara system, which Blueprint already
// does in one node. It is that enemy creatures are mirrored by negating their
// X scale, so an offset authored against the player's line points backwards on
// the enemy's, and a directional effect plays facing away from its target.
//
// These helpers take the offset in "facing right" space and do the flip, so an
// effect is authored once on a skill and looks correct from either side.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_BattleFX.generated.h"

class AGF_Creature;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;

UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_BattleFX : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Spawn an effect at a creature, mirrored to that creature's facing.
	 *
	 * Offset is authored as if the creature faces right. For a creature that
	 * faces left -- anything not flagged isPlayerCreature -- X is negated and
	 * the effect is yawed 180 so directional systems travel the right way.
	 *
	 * Safe to call with a null system or creature: it does nothing and returns
	 * null, so a skill with no effect authored yet does not need a Branch in
	 * front of every call.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|FX",
	          meta = (DefaultToSelf = "WorldContextObject", HidePin = "WorldContextObject"))
	static UNiagaraComponent* SpawnEffectAtCreature(const UObject* WorldContextObject,
	                                                UNiagaraSystem* System,
	                                                AGF_Creature* Creature,
	                                                FVector Offset,
	                                                float Scale = 1.0f);

	/** Where an effect on this creature ends up, without spawning anything. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|FX")
	static FVector GetEffectLocation(AGF_Creature* Creature, FVector Offset);

	/** True when the creature is drawn facing left, i.e. mirrored. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|FX")
	static bool IsCreatureMirrored(AGF_Creature* Creature);

	/** Play a sound at a creature's position. Null-safe like the effect version. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|FX",
	          meta = (DefaultToSelf = "WorldContextObject", HidePin = "WorldContextObject"))
	static void PlaySoundAtCreature(const UObject* WorldContextObject,
	                                USoundBase* Sound,
	                                AGF_Creature* Creature);
};
