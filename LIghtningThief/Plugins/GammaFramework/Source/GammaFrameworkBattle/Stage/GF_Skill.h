// Gamma Framework -- the base class every presented skill derives from.
//
// AGF_SkillDefinition is the framework's rules data: power, accuracy, element,
// status, traits. It deliberately carries no presentation, because the plugin
// has to work for any project built on it and AGF_Creature has no components
// for the same reason.
//
// This is where a project's skill presentation lives. Skills are never spawned --
// they are read off the CDO -- so a skill cannot play its own effects. It can
// only say what should play, and the battle presentation graph does the work.

#pragma once

#include "CoreMinimal.h"
#include "GF_SkillDefinition.h"
// EGF_CreatureAnimState lives here, and AttackerAnimation below is one.
#include "GF_CreatureSpeciesData.h"
#include "GF_Skill.generated.h"

class AGF_Skill;
class UNiagaraSystem;
class USoundBase;
class AGF_Creature;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnAttackImpact, AGF_Skill*, Skill);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnAttackFinished, AGF_Skill*, Skill);

UCLASS(Abstract)
class GAMMAFRAMEWORKBATTLE_API AGF_Skill : public AGF_SkillDefinition
{
	GENERATED_BODY()

public:
	// ============================================
	// CAST -- plays on the attacker as the attack animation starts
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|FX|Cast")
	TSoftObjectPtr<UNiagaraSystem> CastEffect;

	/**
	 * Offset from the attacker, authored as if the creature faces right.
	 *
	 * Enemy creatures are mirrored by negating their X scale, so an offset
	 * authored on one side points the wrong way on the other. SpawnSkillEffect
	 * flips X for you -- author once, facing right.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|FX|Cast")
	FVector CastEffectOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|FX|Cast")
	TSoftObjectPtr<USoundBase> CastSound;

	// ============================================
	// IMPACT -- plays on each target when the hit lands
	// ============================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|FX|Impact")
	TSoftObjectPtr<UNiagaraSystem> ImpactEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|FX|Impact")
	FVector ImpactEffectOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|FX|Impact")
	TSoftObjectPtr<USoundBase> ImpactSound;

	/**
	 * Seconds between the attack animation starting and the hit landing.
	 *
	 * This is the beat the battle already reads well at: attacker animates,
	 * then the target reacts and its HP drains. DurationOfAttack on the base
	 * class is how long the whole attack takes; this is where inside it the
	 * damage appears to happen, so it should always be the smaller number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|FX|Impact",
	          meta = (ClampMin = "0.0", UIMax = "3.0"))
	float ImpactDelay = 0.5f;

	/** Uniform scale applied to both effects, for reusing one system at different sizes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|FX",
	          meta = (ClampMin = "0.01"))
	float EffectScale = 1.0f;

	// ============================================
	// LOADERS
	// ============================================
	//
	// The FX above are soft references so a skill CDO does not drag every
	// Niagara system in the game into memory at startup. Soft references give
	// Blueprint a Soft Object Reference pin, which will not connect to a
	// Niagara System pin without a load node in between. These do the load, so
	// the presentation graph gets the plain pin it wants.

	UFUNCTION(BlueprintCallable, Category = "GammaFramework|FX")
	UNiagaraSystem* LoadCastEffect() const;

	UFUNCTION(BlueprintCallable, Category = "GammaFramework|FX")
	UNiagaraSystem* LoadImpactEffect() const;

	UFUNCTION(BlueprintCallable, Category = "GammaFramework|FX")
	USoundBase* LoadCastSound() const;

	UFUNCTION(BlueprintCallable, Category = "GammaFramework|FX")
	USoundBase* LoadImpactSound() const;

	// ============================================
	// PERFORMING THE ATTACK
	// ============================================
	//
	// Skills are read off the class default object, so the CDO is never in the
	// world and Event BeginPlay on a skill Blueprint never fires. PlayAttack
	// spawns a throwaway instance of the skill class purely so its graph can
	// run: it has a world, it can Delay, drive timelines and spawn Niagara, and
	// it destroys itself when the attack ends. The CDO stays the authority on
	// rules -- this copy only performs.

	/**
	 * What the attacker does while performing this move.
	 *
	 * Played automatically before the graph runs, so the ordinary move needs no
	 * wiring at all. Gnash sets Call because it has its own cry and playing both
	 * reads as a stutter; a move that does not move -- Bristle raising its own
	 * Attack -- turns it off below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Attack")
	EGF_CreatureAnimState AttackerAnimation = EGF_CreatureAnimState::Attack;

	/**
	 * Play it at all.
	 *
	 * Turn this off for a move that should look like nothing happened, or for a
	 * graph that wants to time the animation itself -- against a wind-up, or a
	 * second animation partway through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Attack")
	bool bPlayAttackerAnimation = true;

	/** The creature performing the attack. Only valid on a spawned instance. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "GammaFramework|Attack")
	TObjectPtr<AGF_Creature> Attacker;

	/** Every creature reached. One entry for Single, more for spread moves. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "GammaFramework|Attack")
	TArray<TObjectPtr<AGF_Creature>> Targets;

	/** Build the whole attack here -- Niagara, sounds, camera, movement, timing. */
	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Attack")
	void OnPlayAttack();

	/**
	 * Call when the hit visually lands.
	 *
	 * Damage is applied and the target reacts at this moment, so place it on the
	 * frame the attack connects rather than at the start. Calling it again does
	 * nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Attack")
	void NotifyImpact();

	/**
	 * Call when the attack is completely finished, including recovery. The turn
	 * does not continue until this happens.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Attack")
	void NotifyFinished();

	UFUNCTION(BlueprintPure, Category = "GammaFramework|Attack")
	bool HasImpacted() const { return bImpacted; }

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Attack")
	FGF_OnAttackImpact OnImpact;

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Attack")
	FGF_OnAttackFinished OnFinished;

	/**
	 * Spawn a performing copy of a skill and run its OnPlayAttack.
	 *
	 * Returns null if the skill class has no OnPlayAttack implemented, which is
	 * the signal to fall back to simple timing rather than an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Attack")
	static AGF_Skill* PlayAttack(TSubclassOf<AGF_SkillDefinition> SkillClass,
	                             AGF_Creature* InAttacker,
	                             const TArray<AGF_Creature*>& InTargets);

	/**
	 * Does this skill class actually implement OnPlayAttack?
	 *
	 * A Blueprint that derives from GF_Skill but never added the event would
	 * spawn, do nothing and never finish -- so callers check first and use the
	 * ordinary presentation path instead.
	 */
	static bool ImplementsPlayAttack(TSubclassOf<AGF_SkillDefinition> SkillClass);

	/**
	 * Seconds a graph may run before the battle gives up on it.
	 *
	 * A graph that misses NotifyFinished on some branch would otherwise stall
	 * the fight with no error. This ends the turn and says so in the log, which
	 * turns a hang into a fixable warning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Attack",
	          meta = (ClampMin = "1.0"))
	float SafetyTimeout = 12.0f;

private:
	bool bImpacted = false;
	bool bFinished = false;
	FTimerHandle SafetyTimer;

	void HandleSafetyTimeout();
};
