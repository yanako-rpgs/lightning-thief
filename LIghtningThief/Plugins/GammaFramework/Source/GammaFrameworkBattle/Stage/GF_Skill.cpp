#include "GF_Skill.h"

#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "GF_Creature.h"
#include "Engine/World.h"
#include "TimerManager.h"

// LoadSynchronous rather than an async request: these are resolved at the
// moment a turn begins presenting, the battle is already paused waiting on a
// step token, and a hitch there is preferable to an effect that arrives after
// the hit it belongs to.

UNiagaraSystem* AGF_Skill::LoadCastEffect() const
{
	return CastEffect.IsNull() ? nullptr : CastEffect.LoadSynchronous();
}

UNiagaraSystem* AGF_Skill::LoadImpactEffect() const
{
	return ImpactEffect.IsNull() ? nullptr : ImpactEffect.LoadSynchronous();
}

USoundBase* AGF_Skill::LoadCastSound() const
{
	return CastSound.IsNull() ? nullptr : CastSound.LoadSynchronous();
}

USoundBase* AGF_Skill::LoadImpactSound() const
{
	return ImpactSound.IsNull() ? nullptr : ImpactSound.LoadSynchronous();
}

//======================================================================
// Performing the attack
//======================================================================

void AGF_Skill::NotifyImpact()
{
	// Guarded so a looping sequence cannot apply damage twice. A move that
	// genuinely hits more than once is the resolver's job via isLoopingSkill,
	// not the presentation graph's.
	if (bImpacted) { return; }
	bImpacted = true;

	OnImpact.Broadcast(this);
}

void AGF_Skill::NotifyFinished()
{
	if (bFinished) { return; }
	bFinished = true;

	// An attack that finishes without ever landing still has to apply damage.
	// Otherwise a graph missing a Notify Impact node silently turns the move
	// into a no-op, which is very hard to spot from the battle alone.
	if (!bImpacted)
	{
		NotifyImpact();
	}

	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(SafetyTimer);
	}

	OnFinished.Broadcast(this);

	Destroy();
}

bool AGF_Skill::ImplementsPlayAttack(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
	if (!SkillClass || !SkillClass->IsChildOf(AGF_Skill::StaticClass())) { return false; }

	// The base declares the event, so the function always resolves. What says
	// whether anyone implemented it is WHERE it resolves to: a Blueprint that
	// added the event owns its own copy, and the lookup finds that instead.
	const UFunction* Fn = SkillClass->FindFunctionByName(TEXT("OnPlayAttack"));
	return Fn && Fn->GetOuterUClass() != AGF_Skill::StaticClass();
}

void AGF_Skill::HandleSafetyTimeout()
{
	if (bFinished) { return; }

	UE_LOG(LogTemp, Warning,
		TEXT("Skill '%s' ran for %.1fs without calling Notify Finished. Ending the turn. ")
		TEXT("Check every branch of its On Play Attack graph reaches that node."),
		*Name.ToString(), SafetyTimeout);

	NotifyFinished();
}

AGF_Skill* AGF_Skill::PlayAttack(TSubclassOf<AGF_SkillDefinition> SkillClass,
                                 AGF_Creature* InAttacker,
                                 const TArray<AGF_Creature*>& InTargets)
{
	if (!SkillClass || !InAttacker) { return nullptr; }

	// Only AGF_Skill subclasses can perform. A plain AGF_SkillDefinition has no
	// presentation graph, so there is nothing to spawn.
	if (!SkillClass->IsChildOf(AGF_Skill::StaticClass())) { return nullptr; }

	UWorld* World = InAttacker->GetWorld();
	if (!World) { return nullptr; }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// The attacker's LOCATION, not its transform. Seats are rotated to face
	// across the field, so spawning with the attacker's rotation turned every
	// attack's art by however far its anchor happened to be yawed -- a flipbook
	// authored facing right arrived on its side.
	//
	// A graph that wants to aim at something rotates itself; it has Attacker and
	// Targets to work from, and an unrotated start is the one it can reason about.
	AGF_Skill* Performer = World->SpawnActor<AGF_Skill>(
		SkillClass, InAttacker->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!Performer) { return nullptr; }

	Performer->Attacker = InAttacker;
	Performer->Targets.Reset(InTargets.Num());
	for (AGF_Creature* T : InTargets)
	{
		Performer->Targets.Add(T);
	}

	// Where everything the graph spawns will end up. An attack whose effects are
	// invisible is almost always aiming at a location nobody can see -- most
	// often the world origin, which an empty Targets array produces silently.
	UE_LOG(LogTemp, Log,
		TEXT("PlayAttack '%s': attacker at %s, %d target(s), first at %s"),
		*Performer->Name.ToString(),
		*InAttacker->GetActorLocation().ToCompactString(),
		Performer->Targets.Num(),
		Performer->Targets.Num() > 0 && Performer->Targets[0]
			? *Performer->Targets[0]->GetActorLocation().ToCompactString()
			: TEXT("NONE"));

	// A graph that misses NotifyFinished on one branch would stall the fight
	// with no error at all, which is the worst thing this could do. Started
	// before the graph runs, so an immediate hang is covered too.
	if (Performer->SafetyTimeout > 0.f)
	{
		World->GetTimerManager().SetTimer(
			Performer->SafetyTimer, Performer, &AGF_Skill::HandleSafetyTimeout,
			Performer->SafetyTimeout, /*bLoop*/ false);
	}

	// Before the graph, so a move that wants to override the timing can simply
	// turn it off rather than fight an animation that has already started.
	if (Performer->bPlayAttackerAnimation)
	{
		InAttacker->PlayAnimationState(Performer->AttackerAnimation);
	}

	// Set up first, then run: OnPlayAttack may call NotifyImpact on its first
	// node, so everything it reads has to already be in place.
	Performer->OnPlayAttack();

	return Performer;
}
