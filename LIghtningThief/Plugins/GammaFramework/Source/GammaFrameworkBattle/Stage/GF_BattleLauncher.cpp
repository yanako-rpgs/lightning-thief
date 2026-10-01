#include "GF_BattleLauncher.h"

#include "GF_BattleArena.h"
#include "GF_TurnBattleManager.h"
#include "Turn/GF_BattleFlowComponent.h"
#include "Turn/GF_BattleBoard.h"
#include "GF_Creature.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_CreatureInstanceData.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

bool UGF_BattleLauncher::StartEncounter(const FGF_EncounterRequest& Request)
{
	// The party is data, not actors. Spawning is deferred until the arena is
	// staged, so the creatures appear in their seats rather than at the origin
	// and then being moved.
	return StartEncounterWith(Request, TArray<AGF_Creature*>(), TArray<AGF_Creature*>());
}

bool UGF_BattleLauncher::StartEncounterWith(const FGF_EncounterRequest& Request,
                                            const TArray<AGF_Creature*>& PlayerActive,
                                            const TArray<AGF_Creature*>& PlayerReserves)
{
	if (bInBattle)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleLauncher: already in a battle."));
		return false;
	}

	if (Request.ArenaLevelName.IsNone() || Request.Enemies.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("BattleLauncher: refused -- needs an arena and at least one enemy."));
		return false;
	}

	UWorld* World = GetWorld();
	if (!World) { return false; }

	PendingRequest = Request;

	PendingPlayerActive.Reset();
	for (AGF_Creature* C : PlayerActive)   { PendingPlayerActive.Add(C); }
	PendingPlayerReserves.Reset();
	for (AGF_Creature* C : PlayerReserves) { PendingPlayerReserves.Add(C); }

	// Claimed before the load finishes. A second encounter arriving mid-stream
	// would otherwise be accepted and fight in a half-built arena.
	bInBattle = true;

	FLatentActionInfo Info;
	Info.CallbackTarget    = this;
	Info.ExecutionFunction = TEXT("HandleArenaLoaded");
	Info.UUID              = ++LatentLoadId;
	Info.Linkage           = 0;

	UGameplayStatics::LoadStreamLevel(World, Request.ArenaLevelName,
		/*bMakeVisibleAfterLoad*/ true, /*bShouldBlockOnLoad*/ false, Info);
	return true;
}

void UGF_BattleLauncher::HandleArenaLoaded()
{
	Arena = FindArenaInStreamedLevel();
	if (!Arena)
	{
		// The level came in but has nothing to fight in. Named loudly because
		// the fix is one actor, and the symptom otherwise is a black screen.
		UE_LOG(LogTemp, Error,
			TEXT("BattleLauncher: arena level '%s' has no AGF_BattleArena actor in it."),
			*PendingRequest.ArenaLevelName.ToString());
		FinishEncounter(EGF_BattleOutcome::Aborted);
		return;
	}

	StageArena();
}

AGF_BattleArena* UGF_BattleLauncher::FindArenaInStreamedLevel() const
{
	UWorld* World = const_cast<UWorld*>(GetWorld());
	ULevelStreaming* Streaming = World
		? UGameplayStatics::GetStreamingLevel(World, PendingRequest.ArenaLevelName)
		: nullptr;

	const ULevel* Loaded = Streaming ? Streaming->GetLoadedLevel() : nullptr;
	if (!Loaded) { return nullptr; }

	for (AActor* Actor : Loaded->Actors)
	{
		if (AGF_BattleArena* Found = Cast<AGF_BattleArena>(Actor))
		{
			return Found;
		}
	}
	return nullptr;
}

void UGF_BattleLauncher::StageArena()
{
	UWorld* World = GetWorld();
	UGF_CreatureManagerSubsystem* Creatures = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;

	if (!World || !Creatures || !Arena)
	{
		FinishEncounter(EGF_BattleOutcome::Aborted);
		return;
	}

	// --- the player's line ----------------------------------------------
	// Nothing passed in means fight with the party, which is the normal case.
	if (PendingPlayerActive.Num() == 0 && !GatherPartyForBattle())
	{
		UE_LOG(LogTemp, Error,
			TEXT("BattleLauncher: the party has nobody able to fight."));
		FinishEncounter(EGF_BattleOutcome::Aborted);
		return;
	}

	// Creatures handed in explicitly are moved rather than respawned, so they
	// keep whatever state the caller had already given them.
	for (int32 i = 0; i < PendingPlayerActive.Num(); ++i)
	{
		if (AGF_Creature* C = PendingPlayerActive[i])
		{
			C->SetActorTransform(Arena->GetSeatTransform(EGF_BattleSide::Player, i));
		}
	}

	// --- the far side is spawned fresh ----------------------------------
	TArray<AGF_Creature*> Enemies;
	for (int32 i = 0; i < PendingRequest.Enemies.Num(); ++i)
	{
		const FGF_EncounterEntry& Entry = PendingRequest.Enemies[i];
		if (!Entry.Species) { continue; }

		FGF_CreatureInstanceData Data;
		Data.Initialize(Entry.Species, Entry.Level,
		                TArray<TSubclassOf<AGF_SkillDefinition>>(), NAME_None, 0);

		const FTransform Seat = Arena->GetSeatTransform(EGF_BattleSide::Enemy, i);

		if (AGF_Creature* Spawned = Creatures->SpawnCreatureFromData(
				Data, Seat.GetLocation(), Seat.Rotator(),
				/*bIsPlayer*/ false, /*bIsWild*/ PendingRequest.bIsWild))
		{
			Enemies.Add(Spawned);
			SpawnedCreatures.Add(Spawned);
		}
	}

	if (Enemies.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("BattleLauncher: no enemy creature could be spawned."));
		FinishEncounter(EGF_BattleOutcome::Aborted);
		return;
	}

	// --- the camera -----------------------------------------------------
	// The blend IS the transition. Nothing else has to animate the change from
	// overworld to battle.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		PreviousViewTarget = PC->GetViewTarget();
		PC->SetViewTargetWithBlend(Arena->GetViewTarget(), Arena->CameraBlendTime);
	}

	ApplyBattleInputMode(true);

	StagedEnemies.Reset();
	for (AGF_Creature* E : Enemies) { StagedEnemies.Add(E); }

	// Everything is in place and nothing has started. A covered screen reveals
	// here; an uncovered one just carries straight on.
	OnArenaStaged.Broadcast();

	if (PendingRequest.bWaitForReveal)
	{
		bAwaitingReveal = true;
		return;
	}

	BeginSendOut();
}

void UGF_BattleLauncher::BeginSendOut()
{
	if (!bStaggerSendOut)
	{
		if (!StartTheFight())
		{
			FinishEncounter(EGF_BattleOutcome::Aborted);
		}
		return;
	}

	// Enemies first, then the player's line: a wild creature should be on the
	// field to be answered rather than arriving alongside the answer.
	SendOutQueue.Reset();
	for (AGF_Creature* E : StagedEnemies)       { if (E) { SendOutQueue.Add(E); } }
	for (AGF_Creature* P : PendingPlayerActive) { if (P) { SendOutQueue.Add(P); } }

	// Hidden until each one's turn to arrive. The event that reveals them is
	// also the one that animates the release, so nothing appears un-announced.
	for (AGF_Creature* C : SendOutQueue)
	{
		C->SetActorHiddenInGame(true);
	}

	bSendingOut = true;
	SendOutNext();
}

void UGF_BattleLauncher::SendOutNext()
{
	if (SendOutQueue.Num() == 0)
	{
		bSendingOut = false;
		if (!StartTheFight())
		{
			FinishEncounter(EGF_BattleOutcome::Aborted);
		}
		return;
	}

	AGF_Creature* Next = SendOutQueue[0];
	SendOutQueue.RemoveAt(0);

	if (!Next)
	{
		SendOutNext();
		return;
	}

	const EGF_BattleSide Side = Next->isPlayerCreature
		? EGF_BattleSide::Player : EGF_BattleSide::Enemy;

	const int32 SeatIndex = Side == EGF_BattleSide::Player
		? PendingPlayerActive.IndexOfByKey(Next)
		: StagedEnemies.IndexOfByKey(Next);

	OnSendOutCreature.Broadcast(Next, Side, FMath::Max(0, SeatIndex));
}

void UGF_BattleLauncher::NotifySendOutFinished()
{
	// Silent when nothing is arriving, so a release animation can end with this
	// call whether or not the encounter staggered its send-out.
	if (!bSendingOut) { return; }

	SendOutNext();
}

bool UGF_BattleLauncher::BeginStagedBattle()
{
	// Silent when nothing is waiting, so a reveal animation can end with this
	// call whether or not the encounter asked to be held.
	if (!bAwaitingReveal) { return false; }

	bAwaitingReveal = false;
	BeginSendOut();
	return true;
}

bool UGF_BattleLauncher::StartTheFight()
{
	UWorld* World = GetWorld();
	if (!World) { return false; }

	TArray<AGF_Creature*> Enemies;
	for (AGF_Creature* E : StagedEnemies) { if (E) { Enemies.Add(E); } }
	if (Enemies.Num() == 0) { return false; }

	// Reused if one is already in the level, so its Blueprint's presentation
	// wiring is whatever the project set up rather than a bare default.
	Manager = nullptr;
	for (TActorIterator<AGF_TurnBattleManager> It(World); It; ++It) { Manager = *It; break; }
	if (!Manager)
	{
		Manager = World->SpawnActor<AGF_TurnBattleManager>(
			AGF_TurnBattleManager::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	}

	if (!Manager) { return false; }

	Manager->OnBattleFinished.AddDynamic(this, &UGF_BattleLauncher::HandleBattleFinished);

	// Raised once per creature that goes down, while the fight is still running --
	// the only moment a faint can be recorded in time to affect the rest of it.
	if (UGF_BattleFlowComponent* Flow = Manager->GetFlow())
	{
		Flow->OnCreatureDowned.AddUniqueDynamic(this, &UGF_BattleLauncher::HandleCreatureDowned);
	}

	// Carried across before the fight starts, so a claim on the very first round
	// already knows whether this encounter ends on it. OR rather than assign: a
	// manager ticked in the level stays ticked, so the flag works from either end
	// instead of one silently clearing the other.
	if (PendingRequest.bEndOnFirstClaim)
	{
		Manager->bEndOnFirstClaim = true;
	}

	TArray<AGF_Creature*> Active;
	for (AGF_Creature* C : PendingPlayerActive)   { if (C) { Active.Add(C); } }
	TArray<AGF_Creature*> Reserves;
	for (AGF_Creature* C : PendingPlayerReserves) { if (C) { Reserves.Add(C); } }

	return PendingRequest.bIsWild
		? Manager->StartWildBattle(Active, Reserves, Enemies)
		: Manager->StartTamerBattle(Active, Reserves, Enemies, TArray<AGF_Creature*>(),
		                            PendingRequest.Difficulty);
}

void UGF_BattleLauncher::HandleBattleFinished(EGF_BattleOutcome Outcome)
{
	// The arena is still standing at this point and the camera has not moved.
	// Tearing it down here is what makes a battle end instantly with nothing to
	// show a result over.
	if (PendingRequest.bWaitOnOutcome)
	{
		bAwaitingOutcome = true;
		HeldOutcome = Outcome;
		OnBattleConcluded.Broadcast(Outcome);
		return;
	}

	FinishEncounter(Outcome);
}

void UGF_BattleLauncher::HandleCreatureDowned(FGF_BattleSlot Slot, AGF_Creature* Creature)
{
	// Enemies are not party members and have no record to write back to.
	if (Slot.Side != EGF_BattleSide::Player || !Creature) { return; }

	// SpawnedPartyCreatures and SpawnedPartyIndices are filled together in
	// GatherPartyForBattle and stay in step, so this is the one place that can
	// map a battle actor back to the slot it came from without guessing.
	const int32 Spawned = SpawnedPartyCreatures.IndexOfByPredicate(
		[Creature](const TObjectPtr<AGF_Creature>& C) { return C == Creature; });
	if (Spawned == INDEX_NONE || !SpawnedPartyIndices.IsValidIndex(Spawned)) { return; }

	UWorld* World = GetWorld();
	UGF_CreatureManagerSubsystem* Creatures = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
	if (!Creatures) { return; }

	// The same merge WritePartyBack performs at teardown: battle-volatile fields
	// only, so nothing this writes can roll back EXP or a level gained mid-fight.
	// Running it early just means bIsDowned is true for the rest of the battle.
	Creatures->UpdatePartyFromActor(Creature, SpawnedPartyIndices[Spawned]);
}

TArray<int32> UGF_BattleLauncher::GetBattlerPartyIndices() const
{
	TArray<int32> Indices;

	const UGF_BattleFlowComponent* Flow = Manager ? Manager->GetFlow() : nullptr;
	const UGF_BattleBoard* Board = Flow ? Flow->GetBoard() : nullptr;
	if (!Board)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GetBattlerPartyIndices: no board. Nothing is on the field to award EXP to."));
		return Indices;
	}

	for (const FGF_BattleSlot& Slot : Board->GetLivingSlots(EGF_BattleSide::Player))
	{
		const AGF_Creature* Creature = Board->GetCreatureInSlot(Slot);
		if (!Creature) { continue; }

		// SpawnedPartyCreatures and SpawnedPartyIndices are filled together in
		// GatherPartyForBattle and stay in step, so this maps a battle actor back
		// to the party slot it came from without guessing -- the same lookup
		// HandleCreatureDowned relies on.
		const int32 Spawned = SpawnedPartyCreatures.IndexOfByPredicate(
			[Creature](const TObjectPtr<AGF_Creature>& C) { return C == Creature; });
		if (Spawned == INDEX_NONE || !SpawnedPartyIndices.IsValidIndex(Spawned)) { continue; }

		Indices.AddUnique(SpawnedPartyIndices[Spawned]);
	}

	return Indices;
}

bool UGF_BattleLauncher::DismissOutcome()
{
	// Silent when nothing is held, so a victory screen can close with this call
	// whether or not the encounter asked to wait.
	if (!bAwaitingOutcome) { return false; }

	bAwaitingOutcome = false;
	FinishEncounter(HeldOutcome);
	return true;
}

bool UGF_BattleLauncher::GatherPartyForBattle()
{
	UWorld* World = GetWorld();
	UGF_CreatureManagerSubsystem* Creatures = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
	if (!Creatures || !Arena) { return false; }

	const int32 PartySize = Creatures->GetPartySize();
	const int32 Seats = FMath::Clamp(ActiveSeats, 1, 4);

	for (int32 PartyIndex = 0; PartyIndex < PartySize; ++PartyIndex)
	{
		FGF_CreatureInstanceData Data;
		if (!Creatures->GetPartyCreatureData(PartyIndex, Data)) { continue; }

		// A downed creature is still in the party and still on the bench. It just
		// cannot be the one sent out first.
		const bool bAble = Data.CurrentHP > 0.f;

		const bool bTakesASeat = bAble && PendingPlayerActive.Num() < Seats;

		// The bench is spawned too, so a swap mid-battle has an actor to bring in
		// rather than one to conjure -- but it waits at the arena origin, which is
		// where the seats are measured from and therefore in shot.
		const FTransform Where = bTakesASeat
			? Arena->GetSeatTransform(EGF_BattleSide::Player, PendingPlayerActive.Num())
			: FTransform(Arena->GetActorLocation());

		AGF_Creature* Spawned = Creatures->SpawnPartyCreature(
			PartyIndex, Where.GetLocation(), Where.Rotator(), /*bIsPlayerCreature*/ true);
		if (!Spawned) { continue; }

		// Hidden until something sends it out. Four benched creatures standing on
		// top of each other in the middle of the arena reads as a bug, and it is
		// the swap code's job to reveal one when it arrives.
		if (!bTakesASeat)
		{
			Spawned->SetActorHiddenInGame(true);
		}

		SpawnedPartyCreatures.Add(Spawned);
		SpawnedPartyIndices.Add(PartyIndex);

		if (bTakesASeat) { PendingPlayerActive.Add(Spawned); }
		else             { PendingPlayerReserves.Add(Spawned); }
	}

	return PendingPlayerActive.Num() > 0;
}

void UGF_BattleLauncher::WritePartyBack()
{
	UWorld* World = GetWorld();
	UGF_CreatureManagerSubsystem* Creatures = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
	if (!Creatures) { return; }

	// Before the actors are destroyed. Everything the fight did to them --
	// damage, status, levels, spent Uses -- lives on those actors and nowhere
	// else until this runs.
	for (int32 i = 0; i < SpawnedPartyCreatures.Num(); ++i)
	{
		if (AGF_Creature* C = SpawnedPartyCreatures[i])
		{
			Creatures->UpdatePartyFromActor(C, SpawnedPartyIndices[i]);
		}
	}
}

void UGF_BattleLauncher::ApplyBattleInputMode(bool bEntering)
{
	if (!bTakeInputDuringBattle) { return; }

	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!PC) { return; }

	if (bEntering)
	{
		// The pawn keeps its bindings otherwise, and menu navigation walks the
		// character around the overworld while nobody is looking at it.
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->DisableInput(PC);
		}

		// Game and UI, not UI only. UINavigation binds its navigation input on
		// the player controller, so UI-only mode starves it and the menus stop
		// answering the keyboard and the pad.
		//
		// The pawn is what must not act on that input, and disabling it above is
		// what makes this safe: the controller still hears everything, the
		// character just cannot walk on it.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;

		// Streaming volumes follow the VIEW, not the pawn. Moving the camera to
		// an arena far above the world therefore unloads the level the player is
		// standing in -- and the player falls through the floor that just left.
		PC->bIsUsingStreamingVolumes = false;
		return;
	}

	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->EnableInput(PC);
	}

	// The view is back on the overworld, so the volumes will pick the right
	// level again rather than the one under the arena.
	PC->bIsUsingStreamingVolumes = true;

	// Game and UI rather than Game only, so an overworld HUD stays clickable.
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);
}

void UGF_BattleLauncher::FinishEncounter(EGF_BattleOutcome Outcome)
{
	UWorld* World = GetWorld();

	// Camera first. The player should be looking at the overworld again before
	// the arena disappears out from under the view.
	if (World)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (PreviousViewTarget)
			{
				PC->SetViewTargetWithBlend(PreviousViewTarget,
					Arena ? Arena->CameraBlendTime : 0.5f);
			}
		}
	}

	ApplyBattleInputMode(false);

	if (Manager)
	{
		Manager->OnBattleFinished.RemoveDynamic(this, &UGF_BattleLauncher::HandleBattleFinished);

		if (UGF_BattleFlowComponent* Flow = Manager->GetFlow())
		{
			Flow->OnCreatureDowned.RemoveDynamic(this, &UGF_BattleLauncher::HandleCreatureDowned);
		}
	}

	TeardownArena();

	bInBattle = false;
	bAwaitingReveal = false;
	bAwaitingOutcome = false;
	HeldOutcome = EGF_BattleOutcome::None;
	bSendingOut = false;
	SendOutQueue.Reset();
	StagedEnemies.Reset();
	Manager = nullptr;
	Arena = nullptr;
	PreviousViewTarget = nullptr;
	PendingPlayerActive.Reset();
	PendingPlayerReserves.Reset();

	OnBattleLaunchFinished.Broadcast(Outcome);
}

void UGF_BattleLauncher::TeardownArena()
{
	// Only what this launcher spawned. The player's creatures belong to the
	// party and outlive the fight.
	// The party first, while its actors still exist.
	WritePartyBack();

	for (AGF_Creature* C : SpawnedPartyCreatures)
	{
		if (C) { C->Destroy(); }
	}
	SpawnedPartyCreatures.Reset();
	SpawnedPartyIndices.Reset();

	for (AGF_Creature* C : SpawnedCreatures)
	{
		if (C) { C->Destroy(); }
	}
	SpawnedCreatures.Reset();

	UWorld* World = GetWorld();
	if (World && !PendingRequest.ArenaLevelName.IsNone())
	{
		FLatentActionInfo Info;
		Info.CallbackTarget    = this;
		Info.ExecutionFunction = TEXT("HandleArenaUnloaded");
		Info.UUID              = ++LatentLoadId;
		Info.Linkage           = 0;

		UGameplayStatics::UnloadStreamLevel(World, PendingRequest.ArenaLevelName,
			Info, /*bShouldBlockOnUnload*/ false);
	}
}

void UGF_BattleLauncher::HandleArenaUnloaded()
{
	// Nothing to do. The callback exists because UnloadStreamLevel is latent
	// and wants somewhere to land.
}
