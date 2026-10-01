#include "GF_TurnBattleManager.h"

#include "EngineUtils.h"
#include "TimerManager.h"

#include "Engine/GameInstance.h"
#include "Turn/GF_BattleFlowComponent.h"
#include "Turn/GF_BattleBoard.h"
#include "Turn/GF_BattleResolver.h"
#include "GF_Creature.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_Skill.h"
#include "GF_SkillDefinition.h"


AGF_TurnBattleManager::AGF_TurnBattleManager()
{
	PrimaryActorTick.bCanEverTick = false;

	Flow = CreateDefaultSubobject<UGF_BattleFlowComponent>(TEXT("BattleFlow"));
}

void AGF_TurnBattleManager::BeginPlay()
{
	Super::BeginPlay();
	BindFlowEvents();
}

void AGF_TurnBattleManager::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(TurnStartTimer);
	}

	if (Flow)
	{
		Flow->OnCommandRequested.RemoveAll(this);
		Flow->OnStepAwaitingAcknowledgement.RemoveAll(this);
		Flow->OnReplacementNeeded.RemoveAll(this);
		Flow->OnBattleEnded.RemoveAll(this);
		Flow->OnBattlePhaseChanged.RemoveAll(this);
	}
	Super::EndPlay(Reason);
}

void AGF_TurnBattleManager::BindFlowEvents()
{
	if (!Flow) { return; }

	// Bound before any StartBattle call, so the Intro step is not missed.
	Flow->OnCommandRequested.AddDynamic(this, &AGF_TurnBattleManager::HandleCommandRequested);
	Flow->OnStepAwaitingAcknowledgement.AddDynamic(this, &AGF_TurnBattleManager::HandleStepAwaitingAck);
	Flow->OnReplacementNeeded.AddDynamic(this, &AGF_TurnBattleManager::HandleReplacementNeeded);
	Flow->OnBattleEnded.AddDynamic(this, &AGF_TurnBattleManager::HandleBattleEnded);
	Flow->OnBattlePhaseChanged.AddDynamic(this, &AGF_TurnBattleManager::HandlePhaseChanged);
}

//======================================================================
// Starting
//======================================================================

void AGF_TurnBattleManager::ConfigureCommonAI(FGF_BattleStartConfig& Config)
{
	Config.MaxActiveSlots  = 4;
	Config.StartingWeather = EGF_WeatherType::None;
}

bool AGF_TurnBattleManager::StartWildBattle(const TArray<AGF_Creature*>& PlayerActive,
                                        const TArray<AGF_Creature*>& PlayerReserves,
                                        const TArray<AGF_Creature*>& WildCreatures)
{
	if (!Flow) { return false; }

	FGF_BattleStartConfig Config;
	ConfigureCommonAI(Config);

	Config.Kind           = EGF_BattleKind::Wild;
	Config.PlayerActive   = PlayerActive;
	Config.PlayerReserves = PlayerReserves;
	Config.EnemyActive    = WildCreatures;
	Config.EnemyReserves.Reset();     // wild creatures have no bench
	Config.bEscapeBlocked = false;    // you can always run from a wild fight

	// A wild animal is not running a strategy. Basic picks reasonable moves and
	// misplays often enough that early encounters stay winnable with a Gloamhound.
	Flow->EnemyAIProfile.Difficulty    = EGF_BattleAIDifficulty::Basic;
	Flow->EnemyAIProfile.MistakeChance = 0.25f;
	Flow->EnemyAIProfile.bMaySwap      = false;

	return StartBattleWithConfig(Config);
}

bool AGF_TurnBattleManager::StartTamerBattle(const TArray<AGF_Creature*>& PlayerActive,
                                         const TArray<AGF_Creature*>& PlayerReserves,
                                         const TArray<AGF_Creature*>& EnemyActive,
                                         const TArray<AGF_Creature*>& EnemyReserves,
                                         EGF_BattleAIDifficulty Difficulty,
                                         float MistakeChance)
{
	if (!Flow) { return false; }

	FGF_BattleStartConfig Config;
	ConfigureCommonAI(Config);

	Config.Kind           = EGF_BattleKind::Tamer;
	Config.PlayerActive   = PlayerActive;
	Config.PlayerReserves = PlayerReserves;
	Config.EnemyActive    = EnemyActive;
	Config.EnemyReserves  = EnemyReserves;
	Config.bEscapeBlocked = true;     // you do not walk away from a person

	Flow->EnemyAIProfile.Difficulty    = Difficulty;
	Flow->EnemyAIProfile.MistakeChance = MistakeChance;
	Flow->EnemyAIProfile.bMaySwap      = true;

	return StartBattleWithConfig(Config);
}

bool AGF_TurnBattleManager::StartBattleWithConfig(const FGF_BattleStartConfig& Config)
{
	if (!Flow) { return false; }

	bHasCachedPlan  = false;
	CachedPlanToken = INDEX_NONE;

	const bool bStarted = Flow->StartBattle(Config);

	return bStarted;
}

//======================================================================
// Command phase
//======================================================================

bool AGF_TurnBattleManager::FindFirstLivingEnemy(FGF_BattleSlot& OutSlot) const
{
	if (!Flow) { return false; }

	UGF_BattleBoard* Board = Flow->GetBoard();
	if (!Board) { return false; }

	const TArray<FGF_BattleSlot> Living = Board->GetLivingSlots(EGF_BattleSide::Enemy);
	if (Living.Num() == 0) { return false; }

	OutSlot = Living[0];
	return true;
}

void AGF_TurnBattleManager::HandleCommandRequested(FGF_BattleSlot Slot)
{
	// Mid-claim. Every seat is about to be filled in this same burst, so raising
	// a menu here would leave one on screen that nothing ever answers.
	if (bSuppressCommandRequests)
	{
		return;
	}

	if (!bAutoPlayerCommands)
	{
		// Hand it to the UI and stop. The flow waits until something calls a
		// Submit* function, exactly like a Turn step waits for
		// FinishTurnPresentation.
		//
		// Raised as an implementable event rather than left to the Blueprint to
		// bind OnCommandRequested itself: that delegate lives on the flow
		// component and this class already binds it, so a second binding in a
		// subclass is easy to get wrong and easy to forget entirely.
		OnPlayerCommandRequested(Slot, Flow->GetCommandingCreature());
		return;
	}

	FGF_BattleSlot Target;
	if (!FindFirstLivingEnemy(Target))
	{
		Flow->SubmitBraceCommand(Slot);   // nothing to hit; do not stall the phase
		return;
	}

	Flow->SubmitSkillCommand(Slot, /*SkillIndex*/ 0, Target);
}

//======================================================================
// Resolve loop
//======================================================================

FGF_ActionResolution AGF_TurnBattleManager::PeekPlan()
{
	if (!Flow) { return FGF_ActionResolution(); }

	const int32 Token = Flow->GetPendingStepToken();

	// One plan per turn. A second PlanCurrentTurn would re-roll accuracy and
	// crits, so the numbers shown would not be the numbers applied.
	if (!bHasCachedPlan || CachedPlanToken != Token)
	{
		CachedPlan      = Flow->PlanCurrentTurn();
		CachedPlanToken = Token;
		bHasCachedPlan  = true;
	}

	return CachedPlan;
}

bool AGF_TurnBattleManager::ApplyPlannedTurn()
{
	if (!Flow || !bHasCachedPlan)
	{
		// Applying twice would subtract the damage a second time, so a missing
		// plan is a hard no rather than a silent re-plan.
		UE_LOG(LogTemp, Warning,
			TEXT("BattleManager: ApplyPlannedTurn with no plan pending. Call it once, "
			     "from OnTurnReadyToPresent, before FinishTurnPresentation."));
		return false;
	}

	UGF_BattleResolver::ApplyResolution(CachedPlan);
	bAppliedThisTurn = true;

	// ApplyResolution writes back into the plan, so from here it describes what
	// happened rather than what was planned.
	OnActionResolved(CachedPlan);

	// After the resolution, so HP has already moved and a number appears with
	// the bar rather than ahead of it.
	for (const FGF_TargetResolution& T : CachedPlan.Targets)
	{
		if (!T.Creature) { continue; }

		OnDamageResolved(T.Creature, T.DamageToApply,
		                 T.Damage.bWasCritical,
		                 T.Damage.bSuperEffective,
		                 T.Damage.bNotEffective,
		                 T.Damage.bImmune,
		                 T.Damage.bMissed);
	}

	return true;
}

bool AGF_TurnBattleManager::FinishTurnPresentation()
{
	if (!Flow) { return false; }

	// Forgetting ApplyPlannedTurn would advance the round with nothing having
	// happened, so cover it rather than letting the fight quietly go nowhere.
	if (!bAppliedThisTurn)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("BattleManager: FinishTurnPresentation before ApplyPlannedTurn. "
			     "Applying now so the turn is not lost."));
		ApplyPlannedTurn();
	}

	// The attack is done, but something raised during the hit is still showing.
	// Remember that the attack finished and let the last release close the turn.
	if (PresentationHolds > 0)
	{
		bFinishDeferred = true;
		return true;
	}

	return CompleteTurnPresentation();
}

bool AGF_TurnBattleManager::CompleteTurnPresentation()
{
	if (!Flow) { return false; }

	const int32 Token = CachedPlanToken;

	bHasCachedPlan    = false;
	CachedPlanToken   = INDEX_NONE;
	bAppliedThisTurn  = false;
	bFinishDeferred   = false;
	PresentationHolds = 0;
	DeferredStepToken = INDEX_NONE;

	// Before the acknowledgement, so anything switched on for the turn is off
	// again by the time the next one is announced.
	OnTurnPresentationFinished();

	return Flow->AcknowledgeTurn(Token, EGF_TurnResult::Completed);
}

void AGF_TurnBattleManager::PresentPendingTurn()
{
	if (!Flow) { return; }

	const FGF_ActionResolution Plan = PeekPlan();

	// A skill that brought its own attack graph performs itself. Doing this
	// here rather than in the Blueprint keeps the wiring -- collect targets,
	// spawn, bind impact and finished -- out of a graph where it is fiddly to
	// build and easy to get subtly wrong.
	if (bAutoPlayAttackGraphs && TryPlayAttackGraph(Plan))
	{
		return;
	}

	// Presentation: hand the plan over and stop. The numbers are final, so the
	// Blueprint can size damage popups and bar targets before playing a single
	// frame -- then it drives ApplyPlannedTurn and FinishTurnPresentation at
	// its own pace.
	OnTurnReadyToPresent(Plan);
}

//======================================================================
// Attack graphs
//======================================================================

bool AGF_TurnBattleManager::TryPlayAttackGraph(const FGF_ActionResolution& Plan)
{
	if (!Plan.ActorCreature || !Plan.SkillClass) { return false; }

	// Only skills that actually implement OnPlayAttack can perform. Everything
	// else -- the C++ test skills, and any move not converted yet -- falls
	// through to the Blueprint presentation path unchanged.
	if (!Plan.SkillClass->IsChildOf(AGF_Skill::StaticClass())) { return false; }

	// A GF_Skill Blueprint that never implemented OnPlayAttack would spawn, do
	// nothing, and never call NotifyFinished -- hanging the battle on a graph
	// that looks fine because it is empty. Use the ordinary path instead.
	if (!AGF_Skill::ImplementsPlayAttack(Plan.SkillClass))
	{
		// Almost always a learnset still pointing at the C++ scaffolding rather
		// than the Blueprint that has the graph. Both are GF_Skill classes with
		// the same name in the picker, so the wrong one is easy to choose and
		// silent once chosen -- the move works, it just never performs.
		UE_LOG(LogTemp, Log,
			TEXT("BattleManager: '%s' has no On Play Attack graph, using the default ")
			TEXT("presentation. If it should have one, the creature knows a different ")
			TEXT("class than the Blueprint you built."),
			*Plan.SkillClass->GetName());
		return false;
	}

	TArray<AGF_Creature*> TargetCreatures;
	TargetCreatures.Reserve(Plan.Targets.Num());
	for (const FGF_TargetResolution& T : Plan.Targets)
	{
		if (T.Creature)
		{
			TargetCreatures.Add(T.Creature);
		}
	}

	AGF_Skill* Performer = AGF_Skill::PlayAttack(
		Plan.SkillClass, Plan.ActorCreature, TargetCreatures);
	if (!Performer) { return false; }

	// Bound after the spawn, which is why an attack graph must not call
	// NotifyImpact on its very first node. PlayAttack applies the impact itself
	// if a graph finishes without one, so the worst case is damage landing late
	// rather than a turn that never resolves.
	Performer->OnImpact.AddDynamic(this, &AGF_TurnBattleManager::HandleAttackImpact);
	Performer->OnFinished.AddDynamic(this, &AGF_TurnBattleManager::HandleAttackFinished);

	return true;
}

void AGF_TurnBattleManager::AdvanceRoundEnd()
{
	if (!Flow) { return; }

	// Walk forward until a creature actually has something to show, or the
	// board runs out. Seats with nothing to report would otherwise each cost a
	// beat of silence.
	while (PendingRoundEndSlots.IsValidIndex(RoundEndIndex))
	{
		const FGF_BattleSlot Slot = PendingRoundEndSlots[RoundEndIndex];
		++RoundEndIndex;

		FGF_RoundEndTick Tick = Flow->ResolveRoundEndTickForSlot(Slot);
		if (!Tick.bAnythingHappened)
		{
			continue;
		}

		// Handed over as a one-element array so the same Blueprint graph handles
		// a paced round and a batched one without changing.
		TArray<FGF_RoundEndTick> Single;
		Single.Add(Tick);
		OnRoundEndTicksResolved(Single);

		// Something is showing this tick. The next one waits for it.
		if (PresentationHolds > 0)
		{
			return;
		}
	}

	// The board is done. Now the round can advance.
	PendingRoundEndSlots.Reset();
	RoundEndIndex = 0;

	if (DeferredStepToken != INDEX_NONE)
	{
		const int32 Token = DeferredStepToken;
		DeferredStepToken = INDEX_NONE;
		Flow->AcknowledgeStep(Token);
	}
}

void AGF_TurnBattleManager::HandleAttackImpact(AGF_Skill* Skill)
{
	ApplyPlannedTurn();
}

void AGF_TurnBattleManager::HandleAttackFinished(AGF_Skill* Skill)
{
	FinishTurnPresentation();
}

//======================================================================
// Presentation holds
//======================================================================

void AGF_TurnBattleManager::AddPresentationHold()
{
	++PresentationHolds;
}

void AGF_TurnBattleManager::ReleasePresentationHold()
{
	if (PresentationHolds <= 0)
	{
		// More releases than holds means an unbalanced graph. Warned rather
		// than silently clamped, because the next symptom is a turn ending
		// early, which is much harder to trace back to here.
		UE_LOG(LogTemp, Warning,
			TEXT("BattleManager: ReleasePresentationHold with no hold outstanding."));
		PresentationHolds = 0;
		return;
	}

	--PresentationHolds;

	if (PresentationHolds > 0) { return; }

	// The attack already finished and was waiting on this one. Close the turn.
	if (bFinishDeferred)
	{
		CompleteTurnPresentation();
		return;
	}

	// A round-end tick was being shown. Move to the next creature, or finish
	// the step if that was the last of them.
	if (!PendingRoundEndSlots.IsEmpty() || DeferredStepToken != INDEX_NONE)
	{
		AdvanceRoundEnd();
	}
}

void AGF_TurnBattleManager::HoldTurn(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World) { return; }

	for (TActorIterator<AGF_TurnBattleManager> It(const_cast<UWorld*>(World)); It; ++It)
	{
		It->AddPresentationHold();
		return;
	}
}

void AGF_TurnBattleManager::ReleaseTurn(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World) { return; }

	for (TActorIterator<AGF_TurnBattleManager> It(const_cast<UWorld*>(World)); It; ++It)
	{
		It->ReleasePresentationHold();
		return;
	}
}

bool AGF_TurnBattleManager::ResolvePendingTurn()
{
	if (!Flow) { return false; }

	const int32 Token = Flow->GetPendingStepToken();

	FGF_ActionResolution Plan = PeekPlan();
	UGF_BattleResolver::ApplyResolution(Plan);

	bHasCachedPlan  = false;
	CachedPlanToken = INDEX_NONE;

	return Flow->AcknowledgeTurn(Token, EGF_TurnResult::Completed);
}

TArray<FGF_BattleSlot> AGF_TurnBattleManager::GetClaimableTargets() const
{
	TArray<FGF_BattleSlot> Targets;

	UGF_BattleBoard* Board = Flow ? Flow->GetBoard() : nullptr;
	if (!Board) { return Targets; }

	for (int32 i = 0; i < Board->GetMaxActiveSlots(); ++i)
	{
		const FGF_BattleSlot Slot(EGF_BattleSide::Enemy, i);

		if (Board->IsSlotLiving(Slot))
		{
			Targets.Add(Slot);
		}
	}

	return Targets;
}

bool AGF_TurnBattleManager::SubmitClaim(FName CoreItemName, FGF_BattleSlot TargetSlot)
{
	if (!Flow) { return false; }

	const FGF_BattleSlot Slot = Flow->GetCommandingSlot();
	if (!Slot.IsValidSlot())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SubmitClaim: no seat is being asked for an order right now."));
		return false;
	}

	FGF_BattleSlot Target = TargetSlot;

	// An untouched pin arrives with Index == INDEX_NONE, which is the struct's own
	// "nobody" value. That is the signal to pick, not a caller mistake.
	if (!Target.IsValidSlot())
	{
		if (!FindFirstLivingEnemy(Target))
		{
			UE_LOG(LogTemp, Warning, TEXT("SubmitClaim: nothing left on the field to claim."));
			return false;
		}
	}
	else if (Target.Side != EGF_BattleSide::Enemy)
	{
		// Claiming your own seat is not a thing. Caught here rather than in the
		// flow so the message names the actual mistake.
		UE_LOG(LogTemp, Warning,
			TEXT("SubmitClaim: %s is not an enemy seat. A claim targets the other line."),
			*Target.ToString());
		return false;
	}
	else if (!Flow->GetBoard() || !Flow->GetBoard()->IsSlotLiving(Target))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SubmitClaim: %s is empty or already down -- nothing there to claim."),
			*Target.ToString());
		return false;
	}

	// A claim is the whole side's turn, not one seat's.
	//
	// Four seats ordering independently means the other three can knock the
	// target out before or after the throw -- the claim is spent, the Vorn is
	// down, and the player never chose either of those things. Priority alone
	// does not fix it: the claim goes first, then three attacks land on whatever
	// is left standing. So every other seat passes, and orders already issued
	// this round are taken back.
	//
	// The flow pumps synchronously, so the cancel and all five submissions below
	// each ask for the next command before returning. Held shut until the burst
	// is done, or the UI opens a menu it will never be asked to close.
	TGuardValue<bool> SuppressMenus(bSuppressCommandRequests, true);

	Flow->CancelAllCommands();

	// Returns false against a Tamer -- the flow refuses a Marque outside a wild
	// battle. Nothing has been spent either way; the Core goes when it resolves.
	const bool bAccepted = Flow->SubmitMarqueCommand(Slot, CoreItemName, Target);

	if (!bAccepted)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SubmitClaim: %s refused for %s -- a Marque is only legal in a wild battle."),
			*CoreItemName.ToString(), *Slot.ToString());

		// CancelAllCommands already queued a re-request, so the seats are handed
		// back on their own and the round is not stranded -- the player simply
		// re-issues from the first seat. Only reachable in a Tamer battle, where
		// the bag should not have offered a Core in the first place.
		return false;
	}

	const int32 Passed = StandDownOtherSeats(Slot);

	UE_LOG(LogTemp, Log,
		TEXT("Claim: %s throws %s; %d other seat(s) stand down for the round."),
		*Slot.ToString(), *CoreItemName.ToString(), Passed);

	return true;
}

bool AGF_TurnBattleManager::TryResolveClaim(int32 Token)
{
	if (!Flow) { return false; }

	const FGF_TurnContext Ctx = Flow->GetCurrentTurnContext();

	if (Ctx.Action.ActionType != EGF_BattleActionType::Marque)
	{
		return false;
	}

	// From here on the turn belongs to us. Every path below acknowledges, or the
	// round stops dead on a step nobody answers.

	UGF_CreatureManagerSubsystem* Creatures =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;

	AGF_Creature* Target = (Ctx.Action.Targets.Num() > 0 && Flow->GetBoard())
		? Flow->GetBoard()->GetCreatureInSlot(Ctx.Action.Targets[0])
		: nullptr;

	if (!Creatures || !Target)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Claim: refused -- %s. Core '%s' is NOT spent."),
			!Creatures ? TEXT("no CreatureManager subsystem") : TEXT("target left the field"),
			*Ctx.Action.ItemName.ToString());

		Flow->AcknowledgeTurn(Token, EGF_TurnResult::SkippedNoAction);
		return true;
	}

	const FGF_CreatureInstanceData WildData = Target->ExportToInstanceData();

	// The situational cores read this. The flow's round number is 1-based, which
	// is what Sudden and Aeon both expect -- 0 means "not tracked" and would
	// quietly cost them their bonus.
	FGF_CatchContext Context;
	Context.TurnCount       = Ctx.RoundNumber;
	Context.bIsUnderwater   = false;   // no terrain source yet
	Context.bIsNightOrCave  = false;   // no time-of-day source yet
	// bSpeciesAlreadyCaught is filled in by AttemptCatch -- only the subsystem
	// can see the Compendium, so setting it here would just be overwritten.

	const float StatusModifier =
		UGF_CatchingLibrary::GetStatusModifier(static_cast<uint8>(WildData.StatusCondition));

	FGF_CatchResult Result;
	FGF_CreatureInstanceData Caught;

	// The return value is "was it caught", NOT "did it run" -- a creature that
	// breaks free returns false having already spent the Core. Only a genuine
	// refusal leaves the Core untouched, and those are the cases where the
	// result comes back completely untouched: no roll was ever made.
	Creatures->AttemptCatch(
		WildData, Ctx.Action.ItemName, StatusModifier, Context, Result, Caught);

	if (Result.ModifiedCatchRate <= 0.0f && !Result.bCaught)
	{
		// Nothing was rolled, so nothing was spent: no such Core in the bag, no
		// ItemDataManager, or the name does not resolve to an item asset.
		UE_LOG(LogTemp, Warning,
			TEXT("Claim: refused before rolling -- no '%s' in the bag, or no ItemData asset "
			     "with ItemName '%s' and Category Cores."),
			*Ctx.Action.ItemName.ToString(), *Ctx.Action.ItemName.ToString());

		Flow->AcknowledgeTurn(Token, EGF_TurnResult::SkippedNoAction);
		return true;
	}

	UE_LOG(LogTemp, Log,
		TEXT("Claim: %s with %s -- %s (shakes %d%s, rate %.1f, %.0f%% odds)"),
		*Target->GetName(),
		*Ctx.Action.ItemName.ToString(),
		Result.bCaught ? TEXT("SEALED") : TEXT("broke out"),
		Result.ShakeCount,
		Result.bCriticalCapture ? TEXT(", critical") : TEXT(""),
		Result.ModifiedCatchRate,
		Result.CatchProbability * 100.f);

	// Remember what has to happen once the presentation is done, whether that is
	// on the next line or thirty seconds of flipbooks later.
	HeldClaimToken   = Token;
	bHeldClaimSealed = Result.bCaught;
	HeldClaimSlot    = Ctx.Action.Targets[0];

	UE_LOG(LogTemp, Log, TEXT("Claim: %s %s"),
		*Target->GetName(),
		Result.bCaught ? TEXT("sealed.") : TEXT("broke free."));

	// Broadcast before anything is torn down. A listener playing the seal needs
	// the target still on the board and the arena still standing.
	OnClaimResolved.Broadcast(Result, Ctx.Action.ItemName, Target);

	if (!bHoldRoundForClaimPresentation)
	{
		ApplyClaimOutcome();
	}

	return true;
}

void AGF_TurnBattleManager::FinishClaimPresentation()
{
	// No claim pending is the normal case at the end of most sequences, so this
	// is a quiet no-op rather than a warning.
	if (HeldClaimToken == 0) { return; }

	ApplyClaimOutcome();
}

void AGF_TurnBattleManager::ApplyClaimOutcome()
{
	if (!Flow) { return; }

	const int32 Token = HeldClaimToken;
	const bool bSealed = bHeldClaimSealed;
	const FGF_BattleSlot ClaimedSlot = HeldClaimSlot;

	// Cleared first. Everything below can re-enter through the flow, and a stale
	// token would let a second call run the outcome twice.
	HeldClaimToken = 0;
	bHeldClaimSealed = false;
	HeldClaimSlot = FGF_BattleSlot();

	if (!bSealed)
	{
		// Still standing. Ordinary turn, round carries on.
		Flow->AcknowledgeTurn(Token, EGF_TurnResult::Completed);
		return;
	}

	// The creature belongs to the player now, so it comes off the board --
	// otherwise the round carries on attacking something already in the party,
	// and EvaluateOutcome never sees the enemy side empty.
	Flow->GetBoard()->ClearSlot(ClaimedSlot);

	const bool bSideCleared = !Flow->GetBoard()->SideHasUsableCreature(EGF_BattleSide::Enemy);

	if (bSideCleared || bEndOnFirstClaim)
	{
		UE_LOG(LogTemp, Log, TEXT("Claim: ending the battle as Claimed -- %s."),
			bSideCleared
				? TEXT("that was the last enemy")
				: TEXT("the encounter ends on the first claim"));

		// Claimed, not Victory. AbortBattle drops the outstanding step itself, so
		// this replaces the acknowledgement rather than following it -- acknowledging
		// first would let the flow reach its own outcome check and call the same
		// battle a Victory before this line ever ran.
		Flow->AbortBattle(EGF_BattleOutcome::Claimed);
		return;
	}

	Flow->AcknowledgeTurn(Token, EGF_TurnResult::Completed);
}

void AGF_TurnBattleManager::HandleStepAwaitingAck(EGF_BattleStepKind Kind, int32 Token)
{
	if (!Flow) { return; }

	if (Kind == EGF_BattleStepKind::Turn)
	{
		// Before anything else. A claim is not a skill: it has no plan to peek,
		// no attack graph to play and nothing for the damage pipeline to do, so
		// it must leave the turn path here rather than fall through it.
		if (TryResolveClaim(Token))
		{
			return;
		}

		// A seat that stood down for a claim has nothing to show. Announcing it
		// and waiting out the turn delay tells the player three times over that
		// three of their creatures did nothing, which is both slow and wrong --
		// they did not fail to act, they were never asked to.
		if (Flow->GetCurrentTurnContext().Action.ActionType == EGF_BattleActionType::Pass)
		{
			Flow->AcknowledgeTurn(Token, EGF_TurnResult::SkippedNoAction);
			return;
		}

		// A Flee then runs the ordinary turn path below -- announce, present,
		// apply, finish -- so its message reaches the HUD the way every other
		// action's does. This only tells effects the verdict up front. PeekPlan
		// caches, so the presentation reads this same plan.
		if (Flow->GetCurrentTurnContext().Action.ActionType == EGF_BattleActionType::Flee)
		{
			const FGF_ActionResolution Plan = PeekPlan();
			OnFleeResolved.Broadcast(Plan.bFleeEscaped, Plan.FleeChance, Plan.FleeBlocker);
		}

		if (bAutoAcknowledge)
		{
			// Harness: plan, apply and acknowledge in one frame.
			ResolvePendingTurn();
			return;
		}

		// Announce first, then wait. The plan is final by now, so the message
		// can name the attacker and the move, and the delay below is the time
		// the player gets to read it.
		OnTurnAnnounced(PeekPlan());

		// A beat before anything moves. The command was issued on this frame, so
		// presenting immediately reads as the menu and the attack happening at
		// once with nothing in between to register.
		if (TurnStartDelay > 0.f && GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(
				TurnStartTimer, this, &AGF_TurnBattleManager::PresentPendingTurn,
				TurnStartDelay, /*bLoop*/ false);
			return;
		}

		PresentPendingTurn();
		return;
	}

	// Everything below is housekeeping: Intro, Downed, RoundEnd, Replacement,
	// Outcome. Governed by its own flag, because the first step a battle raises
	// is Intro -- before any turn exists -- so tying it to turn pacing meant
	// switching off bAutoAcknowledge froze the fight before it began.
	if (!bAutoAcknowledgeOtherSteps)
	{
		OnStepReadyToPresent(Kind, Token);
		return;
	}

	if (Kind == EGF_BattleStepKind::RoundEnd)
	{
		// Must run before acknowledging: this is what applies burn, poison,
		// weather and held-item ticks.
		//
		// Those subtract HP the same way an attack does, so a creature reacts
		// and its bar starts moving -- and if anything took a hold while doing
		// so, the round has to wait for it. Acknowledging here regardless is
		// what made a burn kill look like a creature dying at full health: the
		// HP was gone in data before the bar had a frame to move.
		// One creature at a time. Resolving the whole board at once is correct
		// but unreadable: every bar drains together and every line arrives in the
		// same frame, which at four seats a side is a wall of text.
		PendingRoundEndSlots = Flow->GetRoundEndSlots();
		RoundEndIndex = 0;
		DeferredStepToken = Token;

		AdvanceRoundEnd();
		return;
	}

	Flow->AcknowledgeStep(Token);
}

bool AGF_TurnBattleManager::AcknowledgePresentedStep(int32 Token)
{
	return Flow ? Flow->AcknowledgeStep(Token) : false;
}

//======================================================================
// Replacements and outcome
//======================================================================

void AGF_TurnBattleManager::HandleReplacementNeeded(EGF_BattleSide Side,
                                                const TArray<FGF_BattleSlot>& EmptySlots)
{
	// The enemy fills its own seats.
	if (Side != EGF_BattleSide::Player || !bAutoReplace || !Flow) { return; }

	// Send out the lowest reserve index that still takes. SendOutReserve
	// refuses a downed or already-deployed creature, so walking the index
	// upward is enough -- and declining entirely is legal: the seat stays empty
	// and the battle continues a creature short.
	for (const FGF_BattleSlot& Slot : EmptySlots)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			if (Flow->SendOutReserve(Slot, Index)) { break; }
		}
	}
}

void AGF_TurnBattleManager::HandleBattleEnded(EGF_BattleOutcome Outcome)
{
	bHasCachedPlan  = false;
	CachedPlanToken = INDEX_NONE;

	OnBattleFinished.Broadcast(Outcome);
}

void AGF_TurnBattleManager::HandlePhaseChanged(EGF_BattlePhase OldPhase, EGF_BattlePhase NewPhase)
{
	// Kept deliberately empty. The debugger already reports phase changes by
	// polling, and duplicating that here would double every log line.
}

//======================================================================
// Command menu
//======================================================================

bool AGF_TurnBattleManager::IsMenuInputAllowed() const
{
	const UWorld* World = GetWorld();
	if (!World) { return true; }

	return World->GetTimeSeconds() >= MenuInputAllowedAt;
}

void AGF_TurnBattleManager::BlockMenuInput(float Seconds)
{
	if (const UWorld* World = GetWorld())
	{
		// Extends rather than replaces: two things blocking at once should end
		// with the later of the two, not whichever asked last.
		MenuInputAllowedAt = FMath::Max(MenuInputAllowedAt,
			World->GetTimeSeconds() + FMath::Max(0.f, Seconds));
	}
}

bool AGF_TurnBattleManager::ConsumeMenuInput()
{
	if (!IsMenuInputAllowed()) { return false; }

	BlockMenuInput(MenuInputCooldown);
	return true;
}

FGF_BattleSlot AGF_TurnBattleManager::GetCommandingSlot() const
{
	return Flow ? Flow->GetCommandingSlot() : FGF_BattleSlot();
}

AGF_Creature* AGF_TurnBattleManager::GetCommandingCreature() const
{
	return Flow ? Flow->GetCommandingCreature() : nullptr;
}

AGF_Creature* AGF_TurnBattleManager::GetCreatureInSlot(const FGF_BattleSlot& Slot) const
{
	const UGF_BattleBoard* Board = Flow ? Flow->GetBoard() : nullptr;
	return Board ? Board->GetCreatureInSlot(Slot) : nullptr;
}

TArray<FGF_BattleSlot> AGF_TurnBattleManager::GetSelectableTargets(int32 SkillIndex) const
{
	TArray<FGF_BattleSlot> Choices;

	const UGF_BattleBoard* Board = Flow ? Flow->GetBoard() : nullptr;
	AGF_Creature* Commander = GetCommandingCreature();
	if (!Board || !Commander) { return Choices; }

	if (!Commander->Skills.IsValidIndex(SkillIndex)) { return Choices; }

	const TSubclassOf<AGF_SkillDefinition> SkillClass = Commander->Skills[SkillIndex];
	const AGF_SkillDefinition* CDO = SkillClass ? SkillClass.GetDefaultObject() : nullptr;
	if (!CDO) { return Choices; }

	// Only a Single-target skill asks a question. A Self skill answers itself,
	// and a spread skill reaches everything whether the player likes it or not
	// -- offering a picker for either would be a decision that changes nothing.
	if (CDO->TargetShape != EGF_SkillTargetShape::Single)
	{
		return Choices;
	}

	const FGF_BattleSlot Actor = GetCommandingSlot();
	const EGF_BattleSide Opposing = (Actor.Side == EGF_BattleSide::Player)
		? EGF_BattleSide::Enemy : EGF_BattleSide::Player;

	return Board->GetLivingSlots(Opposing);
}

bool AGF_TurnBattleManager::NeedsTargetSelection(int32 SkillIndex) const
{
	// One living enemy is not a choice. Asking anyway costs the player a click
	// on every turn of every 1v1 in the game.
	return GetSelectableTargets(SkillIndex).Num() > 1;
}

bool AGF_TurnBattleManager::SubmitSkill(int32 SkillIndex, FGF_BattleSlot TargetSlot)
{
	if (!Flow || !ConsumeMenuInput()) { return false; }

	const FGF_BattleSlot Actor = GetCommandingSlot();

	// A menu that skipped the picker has nothing sensible to pass, so fall back
	// to the first living enemy rather than making every caller handle it.
	FGF_BattleSlot Target = TargetSlot;
	if (!Target.IsValidSlot())
	{
		const TArray<FGF_BattleSlot> Choices = GetSelectableTargets(SkillIndex);
		if (Choices.Num() > 0)
		{
			Target = Choices[0];
		}
	}

	return Flow->SubmitSkillCommand(Actor, SkillIndex, Target);
}

bool AGF_TurnBattleManager::SubmitBrace()
{
	if (!Flow || !ConsumeMenuInput()) { return false; }
	return Flow->SubmitBraceCommand(GetCommandingSlot());
}

bool AGF_TurnBattleManager::SubmitFlee()
{
	if (!Flow || !ConsumeMenuInput()) { return false; }

	const FGF_BattleSlot Slot = Flow->GetCommandingSlot();
	if (!Slot.IsValidSlot())
	{
		UE_LOG(LogTemp, Warning, TEXT("SubmitFlee: no seat is being asked for an order right now."));
		return false;
	}

	// Checked before anything is taken back. SubmitClaim cancels first and lets a
	// refusal re-ask every seat from the top, which is fine for a refusal that
	// only a Tamer battle can produce. A trapped creature is an ordinary thing to
	// run into, and wiping the orders already given because the player pressed
	// Flee would punish them for asking.
	if (!Flow->CanFlee(Slot.Side))
	{
		UE_LOG(LogTemp, Log,
			TEXT("SubmitFlee: %s cannot run -- not a wild battle, or a creature on the side is trapped."),
			*Slot.ToString());
		return false;
	}

	// The whole side's turn, for the reason a claim is: one seat running while
	// three attack is not a choice anyone makes on purpose. Held shut through the
	// burst because the flow pumps synchronously -- see SubmitClaim.
	TGuardValue<bool> SuppressMenus(bSuppressCommandRequests, true);

	Flow->CancelAllCommands();

	if (!Flow->SubmitFleeCommand(Slot))
	{
		// CanFlee passed a line ago, so this is not expected. CancelAllCommands has
		// already queued a re-request, so the round is handed back rather than stranded.
		UE_LOG(LogTemp, Warning, TEXT("SubmitFlee: the flow refused a Flee that CanFlee allowed."));
		return false;
	}

	const int32 Passed = StandDownOtherSeats(Slot);

	UE_LOG(LogTemp, Log,
		TEXT("Flee: %s runs at %.0f%% odds; %d other seat(s) stand down for the round."),
		*Slot.ToString(), Flow->GetFleeChance(Slot.Side) * 100.0f, Passed);

	return true;
}

bool AGF_TurnBattleManager::CanFlee() const
{
	return Flow && Flow->CanFlee(EGF_BattleSide::Player);
}

int32 AGF_TurnBattleManager::StandDownOtherSeats(const FGF_BattleSlot& Acting)
{
	UGF_BattleBoard* Board = Flow ? Flow->GetBoard() : nullptr;
	if (!Board) { return 0; }

	int32 Passed = 0;
	for (const FGF_BattleSlot& Other : Board->GetLivingSlots(Acting.Side))
	{
		if (Other == Acting) { continue; }

		FGF_BattleAction Pass;
		Pass.Actor       = Other;
		Pass.ActionType  = EGF_BattleActionType::Pass;
		Pass.TargetShape = EGF_SkillTargetShape::Self;
		Pass.Targets     = { Other };

		if (Flow->SubmitCommand(Pass)) { ++Passed; }
	}
	return Passed;
}

bool AGF_TurnBattleManager::CanGoBack() const
{
	// Nothing has been ordered yet this round, so there is nothing behind the
	// current seat to take back.
	return Flow && Flow->GetIssuedCommands().Num() > 0;
}

bool AGF_TurnBattleManager::GoBack()
{
	if (!CanGoBack() || !ConsumeMenuInput()) { return false; }

	// The flow re-raises the command request for the seat it hands back, so a
	// menu built on OnPlayerCommandRequested reopens by itself.
	return Flow->CancelLastCommand();
}
