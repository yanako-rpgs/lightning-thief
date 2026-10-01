#include "GF_BattleFX.h"

#include "GF_Creature.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

bool UGF_BattleFX::IsCreatureMirrored(AGF_Creature* Creature)
{
	// Player creatures face right; everything opposing them faces left. This
	// matches the mirroring the creature Blueprint applies to its own sprite in
	// OnCreatureInitialized, so effects and bodies agree on which way is
	// forward.
	return Creature ? !Creature->isPlayerCreature : false;
}

FVector UGF_BattleFX::GetEffectLocation(AGF_Creature* Creature, FVector Offset)
{
	if (!Creature) { return FVector::ZeroVector; }

	if (IsCreatureMirrored(Creature))
	{
		Offset.X = -Offset.X;
	}

	return Creature->GetActorLocation() + Offset;
}

UNiagaraComponent* UGF_BattleFX::SpawnEffectAtCreature(const UObject* WorldContextObject,
                                                       UNiagaraSystem* System,
                                                       AGF_Creature* Creature,
                                                       FVector Offset,
                                                       float Scale)
{
	// Null-safe on purpose: most skills will have no effect authored for a
	// while, and a battle should still play out rather than log an error per
	// hit.
	if (!System || !Creature) { return nullptr; }

	UWorld* World = Creature->GetWorld();
	if (!World) { return nullptr; }

	const bool bMirrored = IsCreatureMirrored(Creature);
	const FVector Location = GetEffectLocation(Creature, Offset);
	const FRotator Rotation(0.f, bMirrored ? 180.f : 0.f, 0.f);

	UNiagaraComponent* Spawned = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, System, Location, Rotation,
		FVector(Scale), /*bAutoDestroy*/ true, /*bAutoActivate*/ true);

	return Spawned;
}

void UGF_BattleFX::PlaySoundAtCreature(const UObject* WorldContextObject,
                                       USoundBase* Sound,
                                       AGF_Creature* Creature)
{
	if (!Sound || !Creature) { return; }

	UGameplayStatics::PlaySoundAtLocation(Creature, Sound, Creature->GetActorLocation());
}
