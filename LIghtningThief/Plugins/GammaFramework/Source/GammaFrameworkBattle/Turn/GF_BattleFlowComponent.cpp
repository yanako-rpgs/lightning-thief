#include "GF_BattleFlowComponent.h"

#include "GF_BattleAI.h"
#include "GF_BattleBoard.h"
#include "GF_BattleComponent.h"
#include "GF_BattleResolver.h"
#include "GF_BattleBridge.h"
#include "GF_Creature.h"
#include "GF_SkillDefinition.h"
#include "GF_TurnOrderLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogGF_BattleFlow, Log, All);

/** Every flow log carries the round, because "which round was that" is the first question. */
#define GF_FLOWLOG(Format, ...) \
	do { if (bLogFlow) { UE_LOG(LogGF_BattleFlow, Log, TEXT("[Round %d] ") TEXT(Format), RoundNumber, __VA_ARGS__); } } while (0)

UGF_BattleFlowComponent::UGF_BattleFlowComponent()
{
	// Nothing here ticks. The whole point of the design is that the battle
	// advances when it is told to, not on a clock.
	PrimaryComponentTick.bCanEverTick = false;
}

void UGF_BattleFlowComponent::BeginPlay()
{
	Super::BeginPlay();
}

//======================================================================================
// SETUP
//======================================================================================

bool UGF_BattleFlowComponent::StartBattle(const FGF_BattleStartConfig& Config)
{
	if (Config.PlayerActive.Num() == 0 || Config.EnemyActive.Num() == 0)
	{
		UE_LOG(LogGF_BattleFlow, Error,
			TEXT("StartBattle refused: both sides need at least one creature on the field (player %d, enemy %d)."),
			Config.PlayerActive.Num(), Config.EnemyActive.Num());
		return false;
	}

	Board = NewObject<UGF_BattleBoard>(this);
	Board->InitializeBoard(Config.MaxActiveSlots);
	Board->SetSideLineup(EGF_BattleSide::Player, Config.PlayerActive, Config.PlayerReserves);
	Board->SetSideLineup(EGF_BattleSide::Enemy, Config.EnemyActive, Config.EnemyReserves);

	BattleKind = Config.Kind;
	CurrentWeather = Config.StartingWeather;
	bEscapeBlocked = Config.bEscapeBlocked;

	RoundNumber = 0;
	Outcome = EGF_BattleOutcome::None;
	PendingOutcome = EGF_BattleOutcome::None;
	FailedFleeAttempts = 0;
	bCurrentFleeRolled = false;
	CurrentOrderIndex = INDEX_NONE;
	CurrentTurnContext = FGF_TurnContext();
	CommandingSlot = FGF_BattleSlot();
	IssuedActions.Reset();
	ResolvedActions.Reset();
	CommandQueue.Reset();
	TurnOrder.Reset();
	QueuedStep = EGF_FlowStep::None;
	DownedReturnStep = EGF_FlowStep::None;
	PendingStepKind = EGF_BattleStepKind::None;
	PendingStepToken = 0;

	// Seed the announced list with anything that walked in already down, so a
	// downed party member does not get a death broadcast on round one.
	ReportedDownCreatures.Reset();
	for (const FGF_BattleSlot& Slot : Board->GetOccupiedSlots(EGF_BattleSide::Player))
	{
		AGF_Creature* Creature = Board->GetCreatureInSlot(Slot);
		if (Creature && Creature->IsDowned()) { ReportedDownCreatures.Add(Creature); }
	}
	for (const FGF_BattleSlot& Slot : Board->GetOccupiedSlots(EGF_BattleSide::Enemy))
	{
		AGF_Creature* Creature = Board->GetCreatureInSlot(Slot);
		if (Creature && Creature->IsDowned()) { ReportedDownCreatures.Add(Creature); }
	}

	GF_FLOWLOG("StartBattle: %s, %d vs %d on the field, %d slots a side.",
		BattleKind == EGF_BattleKind::Wild ? TEXT("wild") : TEXT("tamer"),
		Board->CountLivingOnField(EGF_BattleSide::Player),
		Board->CountLivingOnField(EGF_BattleSide::Enemy),
		Board->GetMaxActiveSlots());

	SetPhase(EGF_BattlePhase::Intro);
	RaiseStep(EGF_BattleStepKind::Intro);
	return true;
}

void UGF_BattleFlowComponent::AbortBattle(EGF_BattleOutcome InOutcome)
{
	if (Phase == EGF_BattlePhase::Inactive || Phase == EGF_BattlePhase::Finished)
	{
		return;
	}

	// Drop whatever step was outstanding: its acknowledgement would otherwise
	// arrive after the battle ended and queue a continuation into a dead flow.
	PendingStepKind = EGF_BattleStepKind::None;
	PendingStepToken = 0;
	QueuedStep = EGF_FlowStep::None;

	EndBattle(InOutcome);
}

//======================================================================================
// THE STEP GATE
//======================================================================================

void UGF_BattleFlowComponent::RaiseStep(EGF_BattleStepKind Kind)
{
	PendingStepKind = Kind;
	PendingStepToken = NextStepToken++;

	// Tokens must never be 0: that is the "nothing pending" value, and a wrapped
	// counter handing one out would make a stale acknowledgement look current.
	if (NextStepToken <= 0)
	{
		NextStepToken = 1;
	}

	OnStepAwaitingAcknowledgement.Broadcast(Kind, PendingStepToken);
}

bool UGF_BattleFlowComponent::AcknowledgeStep(int32 StepToken)
{
	return AcknowledgeTurn(StepToken, EGF_TurnResult::Completed);
}

bool UGF_BattleFlowComponent::AcknowledgeCurrentStep()
{
	if (PendingStepKind == EGF_BattleStepKind::None)
	{
		UE_LOG(LogGF_BattleFlow, Warning, TEXT("AcknowledgeCurrentStep with nothing pending. Ignored."));
		return false;
	}
	return AcknowledgeTurn(PendingStepToken, EGF_TurnResult::Completed);
}

bool UGF_BattleFlowComponent::AcknowledgeTurn(int32 StepToken, EGF_TurnResult Result)
{
	if (PendingStepKind == EGF_BattleStepKind::None)
	{
		UE_LOG(LogGF_BattleFlow, Warning,
			TEXT("Acknowledgement for token %d arrived with nothing pending. Ignored."), StepToken);
		return false;
	}

	if (StepToken != PendingStepToken)
	{
		// The usual cause is a presentation callback that fired late -- an
		// animation finishing after the player skipped it. Advancing on it would
		// run two turns at once, which is exactly the class of bug the token is
		// here to stop.
		UE_LOG(LogGF_BattleFlow, Warning,
			TEXT("Stale acknowledgement: token %d, expected %d. Ignored."), StepToken, PendingStepToken);
		return false;
	}

	const EGF_BattleStepKind Kind = PendingStepKind;
	PendingStepKind = EGF_BattleStepKind::None;
	PendingStepToken = 0;

	switch (Kind)
	{
	case EGF_BattleStepKind::Intro:
		Queue(EGF_FlowStep::BeginRound);
		break;

	case EGF_BattleStepKind::Turn:
	{
		if (TurnOrder.IsValidIndex(CurrentOrderIndex))
		{
			TurnOrder[CurrentOrderIndex].bHasActed = true;
			TurnOrder[CurrentOrderIndex].bIsCurrent = false;
		}

		// A Flee ends the battle on the roll made when its turn began, never a
		// fresh one: the message already on screen was built from that roll. Not
		// until the board settles, though -- the escape message and any same-round
		// chip damage still play out.
		if (Result == EGF_TurnResult::Completed
			&& CurrentTurnContext.Action.ActionType == EGF_BattleActionType::Flee
			&& bCurrentFleeRolled)
		{
			if (bCurrentFleeEscaped)
			{
				PendingOutcome = EGF_BattleOutcome::Fled;
			}
			else
			{
				++FailedFleeAttempts;
			}
		}
		bCurrentFleeRolled = false;

		OnTurnEnd.Broadcast(CurrentTurnContext, Result);
		BroadcastTurnOrder();
		Queue(EGF_FlowStep::ScanAfterAction);
		break;
	}

	case EGF_BattleStepKind::Downed:
		Queue(DownedReturnStep);
		DownedReturnStep = EGF_FlowStep::None;
		break;

	case EGF_BattleStepKind::RoundEnd:
		Queue(EGF_FlowStep::ScanAfterRoundEnd);
		break;

	case EGF_BattleStepKind::Replacement:
		Queue(EGF_FlowStep::BeginRound);
		break;

	case EGF_BattleStepKind::Outcome:
		Queue(EGF_FlowStep::FinishBattle);
		break;

	default:
		break;
	}

	Pump();
	return true;
}

//======================================================================================
// THE PUMP
//======================================================================================

void UGF_BattleFlowComponent::Queue(EGF_FlowStep Step)
{
	QueuedStep = Step;
	Pump();
}

void UGF_BattleFlowComponent::Pump()
{
	if (bPumping)
	{
		// Already inside the loop below. The stage that queued this will be
		// picked up on the next iteration rather than nested inside this one.
		return;
	}

	bPumping = true;

	while (QueuedStep != EGF_FlowStep::None)
	{
		const EGF_FlowStep Step = QueuedStep;
		QueuedStep = EGF_FlowStep::None;

		switch (Step)
		{
		case EGF_FlowStep::BeginRound:            BeginRound();            break;
		case EGF_FlowStep::RequestNextCommand:    RequestNextCommand();    break;
		case EGF_FlowStep::LockCommands:          LockCommands();          break;
		case EGF_FlowStep::AdvanceToNextTurn:     AdvanceToNextTurn();     break;
		case EGF_FlowStep::ScanAfterAction:       ScanAfterAction();       break;
		case EGF_FlowStep::ContinueAfterAction:   ContinueAfterAction();   break;
		case EGF_FlowStep::BeginRoundEnd:         BeginRoundEnd();         break;
		case EGF_FlowStep::ScanAfterRoundEnd:     ScanAfterRoundEnd();     break;
		case EGF_FlowStep::ContinueAfterRoundEnd: ContinueAfterRoundEnd(); break;
		case EGF_FlowStep::FinishBattle:          FinishBattle();          break;
		default:                                                           break;
		}
	}

	bPumping = false;
}

//======================================================================================
// PHASE 1 - COMMAND
//======================================================================================

void UGF_BattleFlowComponent::BeginRound()
{
	++RoundNumber;

	// A side wiped out mid-round refills one seat here rather than sitting out
	// the next round with nothing to command.
	ForceFillEmptyLine(EGF_BattleSide::Player);
	ForceFillEmptyLine(EGF_BattleSide::Enemy);

	for (const FGF_BattleSlot& Slot : Board->GetAllLivingSlots())
	{
		if (AGF_Creature* Creature = Board->GetCreatureInSlot(Slot))
		{
			Creature->ResetTurnFlags();
			Creature->bTookDamageThisTurn = false;

			// Brace covers one round. Cleared here rather than at round end so a
			// creature that braced is still protected against the end-of-round
			// chip damage it braced for.
			Creature->bIsBracing = false;
		}
	}

	IssuedActions.Reset();
	ResolvedActions.Reset();
	TurnOrder.Reset();
	CommandQueue.Reset();
	CurrentOrderIndex = INDEX_NONE;
	CurrentTurnContext = FGF_TurnContext();

	// Player seats first, in slot order. Enemy seats only join the queue when a
	// real AI is answering them; otherwise they are filled at lock-in.
	CommandQueue.Append(Board->GetLivingSlots(EGF_BattleSide::Player));
	if (!bAutoIssueEnemyCommands)
	{
		CommandQueue.Append(Board->GetLivingSlots(EGF_BattleSide::Enemy));
	}

	SetPhase(EGF_BattlePhase::Command);
	OnRoundBegin.Broadcast(RoundNumber);
	BroadcastTurnOrder();

	GF_FLOWLOG("Command phase: %d order(s) to issue.", CommandQueue.Num());
	Queue(EGF_FlowStep::RequestNextCommand);
}

void UGF_BattleFlowComponent::RequestNextCommand()
{
	// Seats that went down between orders drop out rather than being asked.
	CommandQueue.RemoveAll([this](const FGF_BattleSlot& Slot) { return !Board->IsSlotLiving(Slot); });

	if (CommandQueue.Num() == 0)
	{
		CommandingSlot = FGF_BattleSlot();
		Queue(EGF_FlowStep::LockCommands);
		return;
	}

	// Peeked, not popped. Submitting removes it; cancelling puts an earlier seat
	// back in front of it.
	CommandingSlot = CommandQueue[0];

	if (CommandingSlot.Side == EGF_BattleSide::Player)
	{
		OnCommandRequested.Broadcast(CommandingSlot);
	}
	else
	{
		OnEnemyCommandRequested.Broadcast(CommandingSlot);
	}
}

bool UGF_BattleFlowComponent::SubmitCommand(const FGF_BattleAction& Action)
{
	return ValidateAndStoreCommand(Action);
}

bool UGF_BattleFlowComponent::ValidateAndStoreCommand(const FGF_BattleAction& Action)
{
	if (Phase != EGF_BattlePhase::Command)
	{
		UE_LOG(LogGF_BattleFlow, Warning, TEXT("Command for %s refused: not in the Command phase."), *Action.Actor.ToString());
		return false;
	}

	if (!CommandQueue.Contains(Action.Actor))
	{
		UE_LOG(LogGF_BattleFlow, Warning,
			TEXT("Command for %s refused: that seat is not waiting for an order."), *Action.Actor.ToString());
		return false;
	}

	if (Action.ActionType == EGF_BattleActionType::None)
	{
		UE_LOG(LogGF_BattleFlow, Warning, TEXT("Command for %s refused: no action type."), *Action.Actor.ToString());
		return false;
	}

	if (Action.ActionType == EGF_BattleActionType::Flee)
	{
		if (!CanFlee(Action.Actor.Side))
		{
			UE_LOG(LogGF_BattleFlow, Warning,
				TEXT("Flee refused: not a wild battle, escape is blocked, or a creature on that side is trapped."));
			return false;
		}
	}

	if (Action.ActionType == EGF_BattleActionType::Marque && BattleKind != EGF_BattleKind::Wild)
	{
		UE_LOG(LogGF_BattleFlow, Warning, TEXT("Marque refused: a claimed creature already has a Tamer."));
		return false;
	}

	if (Action.ActionType == EGF_BattleActionType::Swap)
	{
		const TArray<AGF_Creature*> Bench = Board->GetReserves(Action.Actor.Side);
		if (!Bench.IsValidIndex(Action.SwapToReserveIndex)
			|| Bench[Action.SwapToReserveIndex] == nullptr
			|| Bench[Action.SwapToReserveIndex]->IsDowned())
		{
			UE_LOG(LogGF_BattleFlow, Warning, TEXT("Swap refused: reserve index %d is not a creature that can fight."),
				Action.SwapToReserveIndex);
			return false;
		}
	}

	FGF_BattleAction Stored = Action;

	// Targets are expanded again when the turn actually comes up, because the
	// board will have changed by then. This pass is for the preview and for
	// anything that wants to draw the intended targets during Command.
	if (Stored.Targets.Num() == 0)
	{
		Stored.Targets = UGF_TurnOrderLibrary::ExpandTargets(
			Board, Stored.Actor, Stored.TargetShape, FGF_BattleSlot());
	}

	CommandQueue.Remove(Stored.Actor);
	IssuedActions.Add(Stored);

	OnCommandIssued.Broadcast(Stored.Actor, Stored);
	BroadcastTurnOrder();

	Queue(EGF_FlowStep::RequestNextCommand);
	return true;
}

bool UGF_BattleFlowComponent::SubmitSkillCommand(const FGF_BattleSlot& Slot, int32 SkillIndex, const FGF_BattleSlot& PrimaryTarget)
{
	AGF_Creature* Creature = Board ? Board->GetCreatureInSlot(Slot) : nullptr;
	if (Creature == nullptr)
	{
		return false;
	}

	FGF_BattleAction Action;
	Action.Actor = Slot;
	Action.ActionType = EGF_BattleActionType::Skill;
	Action.SkillIndex = SkillIndex;

	if (Creature->Skills.IsValidIndex(SkillIndex))
	{
		Action.SkillClass = Creature->Skills[SkillIndex];
	}

	if (const AGF_SkillDefinition* SkillCDO = Action.SkillClass ? Action.SkillClass.GetDefaultObject() : nullptr)
	{
		Action.TargetShape = SkillCDO->TargetShape;
	}

	Action.Targets = UGF_TurnOrderLibrary::ExpandTargets(Board, Slot, Action.TargetShape, PrimaryTarget);
	return ValidateAndStoreCommand(Action);
}

bool UGF_BattleFlowComponent::SubmitItemCommand(const FGF_BattleSlot& Slot, FName ItemName, const FGF_BattleSlot& TargetSlot)
{
	FGF_BattleAction Action;
	Action.Actor = Slot;
	Action.ActionType = EGF_BattleActionType::Item;
	Action.ItemName = ItemName;
	Action.TargetShape = EGF_SkillTargetShape::Single;
	Action.Targets = { TargetSlot };
	return ValidateAndStoreCommand(Action);
}

bool UGF_BattleFlowComponent::SubmitSwapCommand(const FGF_BattleSlot& Slot, int32 ReserveIndex)
{
	FGF_BattleAction Action;
	Action.Actor = Slot;
	Action.ActionType = EGF_BattleActionType::Swap;
	Action.SwapToReserveIndex = ReserveIndex;
	Action.TargetShape = EGF_SkillTargetShape::Self;
	Action.Targets = { Slot };
	return ValidateAndStoreCommand(Action);
}

bool UGF_BattleFlowComponent::SubmitBraceCommand(const FGF_BattleSlot& Slot)
{
	FGF_BattleAction Action;
	Action.Actor = Slot;
	Action.ActionType = EGF_BattleActionType::Brace;
	Action.TargetShape = EGF_SkillTargetShape::Self;
	Action.Targets = { Slot };
	return ValidateAndStoreCommand(Action);
}

bool UGF_BattleFlowComponent::SubmitMarqueCommand(const FGF_BattleSlot& Slot, FName CoreItemName, const FGF_BattleSlot& TargetSlot)
{
	FGF_BattleAction Action;
	Action.Actor = Slot;
	Action.ActionType = EGF_BattleActionType::Marque;
	Action.ItemName = CoreItemName;
	Action.TargetShape = EGF_SkillTargetShape::Single;
	Action.Targets = { TargetSlot };
	return ValidateAndStoreCommand(Action);
}

bool UGF_BattleFlowComponent::SubmitFleeCommand(const FGF_BattleSlot& Slot)
{
	FGF_BattleAction Action;
	Action.Actor = Slot;
	Action.ActionType = EGF_BattleActionType::Flee;
	Action.TargetShape = EGF_SkillTargetShape::Self;
	Action.Targets = { Slot };
	return ValidateAndStoreCommand(Action);
}

bool UGF_BattleFlowComponent::CancelLastCommand()
{
	if (Phase != EGF_BattlePhase::Command || IssuedActions.Num() == 0)
	{
		return false;
	}

	// Only the player's own orders can be taken back. Once the queue has moved
	// on to enemy seats there is nothing of the player's left to re-issue.
	if (IssuedActions.Last().Actor.Side != EGF_BattleSide::Player)
	{
		return false;
	}

	const FGF_BattleSlot Slot = IssuedActions.Last().Actor;
	IssuedActions.Pop();

	// Front of the queue, ahead of the seat that was about to be asked, so the
	// re-issue happens immediately and the rest of the order is preserved.
	CommandQueue.Insert(Slot, 0);

	OnCommandCancelled.Broadcast(Slot);
	BroadcastTurnOrder();

	Queue(EGF_FlowStep::RequestNextCommand);
	return true;
}

void UGF_BattleFlowComponent::CancelAllCommands()
{
	if (Phase != EGF_BattlePhase::Command)
	{
		return;
	}

	for (int32 i = IssuedActions.Num() - 1; i >= 0; --i)
	{
		if (IssuedActions[i].Actor.Side == EGF_BattleSide::Player)
		{
			CommandQueue.Insert(IssuedActions[i].Actor, 0);
			OnCommandCancelled.Broadcast(IssuedActions[i].Actor);
			IssuedActions.RemoveAt(i);
		}
	}

	BroadcastTurnOrder();
	Queue(EGF_FlowStep::RequestNextCommand);
}

//======================================================================================
// PHASE 2 - RESOLVE
//======================================================================================

void UGF_BattleFlowComponent::LockCommands()
{
	if (bAutoIssueEnemyCommands)
	{
		for (const FGF_BattleSlot& Slot : Board->GetLivingSlots(EGF_BattleSide::Enemy))
		{
			const bool bAlreadyIssued = IssuedActions.ContainsByPredicate(
				[&Slot](const FGF_BattleAction& Action) { return Action.Actor == Slot; });

			if (!bAlreadyIssued)
			{
				IssuedActions.Add(BuildDefaultEnemyAction(Slot));
			}
		}
	}

	// Any living seat with no order at all passes. Leaving it out entirely would
	// be fine for the sequence but wrong for the ribbon, which should show every
	// creature on the field and what it is doing -- including nothing.
	for (const FGF_BattleSlot& Slot : Board->GetAllLivingSlots())
	{
		const bool bHasOrder = IssuedActions.ContainsByPredicate(
			[&Slot](const FGF_BattleAction& Action) { return Action.Actor == Slot; });

		if (!bHasOrder)
		{
			FGF_BattleAction Pass;
			Pass.Actor = Slot;
			Pass.ActionType = EGF_BattleActionType::Pass;
			Pass.TargetShape = EGF_SkillTargetShape::Self;
			IssuedActions.Add(Pass);
		}
	}

	ResolvedActions = IssuedActions;
	for (FGF_BattleAction& Action : ResolvedActions)
	{
		UGF_TurnOrderLibrary::StampActionForOrdering(Action, Board, CurrentWeather, PriorityConfig);
	}

	// Sort the actions, then build the ribbon from that same order, so entry N
	// and action N are always the same turn.
	UGF_TurnOrderLibrary::SortActions(ResolvedActions);
	TurnOrder = UGF_TurnOrderLibrary::BuildTurnOrder(Board, ResolvedActions);

	CommandingSlot = FGF_BattleSlot();
	CurrentOrderIndex = INDEX_NONE;

	SetPhase(EGF_BattlePhase::Resolving);
	BroadcastTurnOrder();

	GF_FLOWLOG("Turn order locked: %d action(s) this round.", TurnOrder.Num());
	Queue(EGF_FlowStep::AdvanceToNextTurn);
}

void UGF_BattleFlowComponent::AdvanceToNextTurn()
{
	// A loop, not recursion: a spread attack that downs three creature leaves
	// three entries to skip, and each skip must not add a stack frame.
	for (;;)
	{
		++CurrentOrderIndex;

		if (!TurnOrder.IsValidIndex(CurrentOrderIndex))
		{
			CurrentTurnContext = FGF_TurnContext();
			Queue(EGF_FlowStep::BeginRoundEnd);
			return;
		}

		FGF_TurnOrderEntry& Entry = TurnOrder[CurrentOrderIndex];

		if (Entry.bCancelled || !Board->IsSlotLiving(Entry.Slot))
		{
			Entry.bCancelled = true;
			Entry.bHasActed = true;
			Entry.bIsCurrent = false;

			BuildCurrentTurnContext();
			OnTurnEnd.Broadcast(CurrentTurnContext, EGF_TurnResult::SkippedDowned);
			continue;
		}

		Entry.bIsCurrent = true;
		BuildCurrentTurnContext();

		// Re-expand now. The target picked during Command may have been focused
		// down two actions ago; ExpandTargets redirects rather than fizzling.
		if (ResolvedActions.IsValidIndex(CurrentOrderIndex))
		{
			const FGF_BattleSlot Primary = CurrentTurnContext.Action.Targets.Num() > 0
				? CurrentTurnContext.Action.Targets[0]
				: FGF_BattleSlot();

			ResolvedActions[CurrentOrderIndex].Targets = UGF_TurnOrderLibrary::ExpandTargets(
				Board, Entry.Slot, ResolvedActions[CurrentOrderIndex].TargetShape, Primary);

			CurrentTurnContext.Action = ResolvedActions[CurrentOrderIndex];
		}

		// A Flee is decided here, before the turn is raised, because the layer
		// presenting it needs the verdict to say what happened -- and deciding at
		// the acknowledgement would be after the message was already shown.
		bCurrentFleeRolled = false;
		if (CurrentTurnContext.Action.ActionType == EGF_BattleActionType::Flee)
		{
			RollFleeForCurrentTurn();
		}

		BroadcastTurnOrder();
		OnTurnBegin.Broadcast(CurrentTurnContext);
		RaiseStep(EGF_BattleStepKind::Turn);
		return;
	}
}

void UGF_BattleFlowComponent::BuildCurrentTurnContext()
{
	CurrentTurnContext = FGF_TurnContext();

	if (!TurnOrder.IsValidIndex(CurrentOrderIndex))
	{
		return;
	}

	const FGF_TurnOrderEntry& Entry = TurnOrder[CurrentOrderIndex];

	CurrentTurnContext.Slot = Entry.Slot;
	CurrentTurnContext.Creature = Board->GetCreatureInSlot(Entry.Slot);
	CurrentTurnContext.Side = Entry.Side;
	CurrentTurnContext.DisplayName = Entry.DisplayName;
	CurrentTurnContext.RoundNumber = RoundNumber;
	CurrentTurnContext.OrderIndex = CurrentOrderIndex;
	CurrentTurnContext.ActionsThisRound = TurnOrder.Num();

	if (ResolvedActions.IsValidIndex(CurrentOrderIndex))
	{
		CurrentTurnContext.Action = ResolvedActions[CurrentOrderIndex];
	}
}

void UGF_BattleFlowComponent::ScanAfterAction()
{
	if (ScanForNewlyDowned() > 0)
	{
		RaiseDownedStepThenGoTo(EGF_FlowStep::ContinueAfterAction);
		return;
	}

	Queue(EGF_FlowStep::ContinueAfterAction);
}

void UGF_BattleFlowComponent::ContinueAfterAction()
{
	const EGF_BattleOutcome Decided = EvaluateOutcome();
	if (Decided != EGF_BattleOutcome::None)
	{
		EndBattle(Decided);
		return;
	}

	if (PendingOutcome != EGF_BattleOutcome::None)
	{
		EndBattle(PendingOutcome);
		return;
	}

	Queue(EGF_FlowStep::AdvanceToNextTurn);
}

//======================================================================================
// PHASE 3 - END OF ROUND
//======================================================================================

void UGF_BattleFlowComponent::BeginRoundEnd()
{
	SetPhase(EGF_BattlePhase::RoundEnd);
	OnRoundEnd.Broadcast(RoundNumber);

	// Status damage, weather, held items and Uses all tick during this step.
	// The flow does not apply any of them -- it just holds the round open.
	RaiseStep(EGF_BattleStepKind::RoundEnd);
}

void UGF_BattleFlowComponent::ScanAfterRoundEnd()
{
	if (ScanForNewlyDowned() > 0)
	{
		RaiseDownedStepThenGoTo(EGF_FlowStep::ContinueAfterRoundEnd);
		return;
	}

	Queue(EGF_FlowStep::ContinueAfterRoundEnd);
}

void UGF_BattleFlowComponent::ContinueAfterRoundEnd()
{
	// Downed creature leave the field now. Reserves do not follow them in on
	// their own -- that is a decision, and it belongs to whoever owns the side.
	for (EGF_BattleSide Side : { EGF_BattleSide::Player, EGF_BattleSide::Enemy })
	{
		for (const FGF_BattleSlot& Slot : Board->GetOccupiedSlots(Side))
		{
			const AGF_Creature* Creature = Board->GetCreatureInSlot(Slot);
			if (Creature && Creature->IsDowned())
			{
				Board->ClearSlot(Slot);
			}
		}
	}

	const EGF_BattleOutcome Decided = EvaluateOutcome();
	if (Decided != EGF_BattleOutcome::None)
	{
		EndBattle(Decided);
		return;
	}

	if (PendingOutcome != EGF_BattleOutcome::None)
	{
		EndBattle(PendingOutcome);
		return;
	}

	bool bNeedsReplacement = false;
	for (EGF_BattleSide Side : { EGF_BattleSide::Player, EGF_BattleSide::Enemy })
	{
		const TArray<FGF_BattleSlot> Empty = Board->GetEmptySlots(Side);
		if (Empty.Num() > 0 && Board->HasLivingReserve(Side))
		{
			bNeedsReplacement = true;
			OnReplacementNeeded.Broadcast(Side, Empty);
		}
	}

	if (bNeedsReplacement)
	{
		// Held open so a replacement menu can take as long as it needs. Seats
		// left empty stay empty; the next round simply has fewer actions.
		RaiseStep(EGF_BattleStepKind::Replacement);
		return;
	}

	Queue(EGF_FlowStep::BeginRound);
}

//======================================================================================
// DOWNS AND OUTCOME
//======================================================================================

void UGF_BattleFlowComponent::RaiseDownedStepThenGoTo(EGF_FlowStep Next)
{
	DownedReturnStep = Next;
	RaiseStep(EGF_BattleStepKind::Downed);
}

int32 UGF_BattleFlowComponent::ScanForNewlyDowned()
{
	int32 NewDowns = 0;

	for (EGF_BattleSide Side : { EGF_BattleSide::Player, EGF_BattleSide::Enemy })
	{
		for (const FGF_BattleSlot& Slot : Board->GetOccupiedSlots(Side))
		{
			AGF_Creature* Creature = Board->GetCreatureInSlot(Slot);
			if (Creature == nullptr || !Creature->IsDowned() || ReportedDownCreatures.Contains(Creature))
			{
				continue;
			}

			ReportedDownCreatures.Add(Creature);
			++NewDowns;

			// Its queued action never happens. Marked, not removed, so the
			// ribbon keeps its shape.
			for (FGF_TurnOrderEntry& Entry : TurnOrder)
			{
				if (Entry.Slot == Slot && !Entry.bHasActed)
				{
					Entry.bCancelled = true;
				}
			}

			GF_FLOWLOG("%s went down.", *Board->GetDisplayNameForSlot(Slot).ToString());
			OnCreatureDowned.Broadcast(Slot, Creature);
		}
	}

	if (NewDowns > 0)
	{
		BroadcastTurnOrder();
	}

	return NewDowns;
}

EGF_BattleOutcome UGF_BattleFlowComponent::EvaluateOutcome() const
{
	// Victory is checked first. One action can wipe both sides -- Selfdestruct,
	// recoil, end-of-round chip -- and when it does the player has won. Asking
	// about the loss first turns a won battle into a Rout.
	if (!Board->SideHasUsableCreature(EGF_BattleSide::Enemy))
	{
		return EGF_BattleOutcome::Victory;
	}

	if (!Board->SideHasUsableCreature(EGF_BattleSide::Player))
	{
		return EGF_BattleOutcome::Defeat;
	}

	return EGF_BattleOutcome::None;
}

void UGF_BattleFlowComponent::EndBattle(EGF_BattleOutcome InOutcome)
{
	Outcome = InOutcome;
	PendingOutcome = EGF_BattleOutcome::None;

	switch (InOutcome)
	{
	case EGF_BattleOutcome::Victory: SetPhase(EGF_BattlePhase::Victory); break;
	case EGF_BattleOutcome::Defeat:  SetPhase(EGF_BattlePhase::Defeat);  break;
	default:                         SetPhase(EGF_BattlePhase::Ended);   break;
	}

	CurrentTurnContext = FGF_TurnContext();
	CommandingSlot = FGF_BattleSlot();

	GF_FLOWLOG("Battle over: %s.", *UEnum::GetValueAsString(InOutcome));

	OnBattleEnded.Broadcast(InOutcome);
	RaiseStep(EGF_BattleStepKind::Outcome);
}

void UGF_BattleFlowComponent::FinishBattle()
{
	SetPhase(EGF_BattlePhase::Finished);
}

//======================================================================================
// BOARD CHANGES
//======================================================================================

bool UGF_BattleFlowComponent::SendOutReserve(const FGF_BattleSlot& Slot, int32 ReserveIndex)
{
	if (Board == nullptr)
	{
		return false;
	}

	const TArray<AGF_Creature*> Bench = Board->GetReserves(Slot.Side);
	if (!Bench.IsValidIndex(ReserveIndex) || Bench[ReserveIndex] == nullptr)
	{
		return false;
	}

	if (!Board->SendOutToSlot(Slot, Bench[ReserveIndex]))
	{
		return false;
	}

	BroadcastTurnOrder();
	return true;
}

bool UGF_BattleFlowComponent::ExecuteSwap(const FGF_BattleSlot& Slot, int32 ReserveIndex)
{
	if (Board == nullptr || !Board->SwapInReserve(Slot, ReserveIndex))
	{
		return false;
	}

	// The arriving creature acts from next round, so its entry for this round --
	// the Swap itself -- is already spent and nothing else needs cancelling.
	RefreshTurnOrderEntryStates();
	BroadcastTurnOrder();
	return true;
}

void UGF_BattleFlowComponent::ForceFillEmptyLine(EGF_BattleSide Side)
{
	if (Board->CountLivingOnField(Side) > 0 || !Board->HasLivingReserve(Side))
	{
		return;
	}

	// A side with a bench but an empty line would otherwise have no seats to
	// command, so the round would produce no actions and the battle would sit
	// there. One forced send-out is the smallest thing that keeps it moving.
	const TArray<AGF_Creature*> Bench = Board->GetReserves(Side);
	TArray<FGF_BattleSlot> Empty = Board->GetEmptySlots(Side);
	if (Empty.Num() == 0)
	{
		return;
	}

	for (int32 i = 0; i < Bench.Num(); ++i)
	{
		if (Bench[i] != nullptr && !Bench[i]->IsDowned())
		{
			Board->SendOutToSlot(Empty[0], Bench[i]);
			GF_FLOWLOG("Forced send-out into %s: its side had nothing on the field.", *Empty[0].ToString());
			return;
		}
	}
}

//======================================================================================
// QUERIES
//======================================================================================

AGF_Creature* UGF_BattleFlowComponent::GetCommandingCreature() const
{
	return (Board && CommandingSlot.IsValidSlot()) ? Board->GetCreatureInSlot(CommandingSlot) : nullptr;
}

FGF_BattleSlot UGF_BattleFlowComponent::GetSpotlightSlot() const
{
	switch (Phase)
	{
	case EGF_BattlePhase::Command:   return CommandingSlot;
	case EGF_BattlePhase::Resolving: return CurrentTurnContext.Slot;
	default:                         return FGF_BattleSlot();
	}
}

AGF_Creature* UGF_BattleFlowComponent::GetSpotlightCreature() const
{
	const FGF_BattleSlot Slot = GetSpotlightSlot();
	return (Board && Slot.IsValidSlot()) ? Board->GetCreatureInSlot(Slot) : nullptr;
}

FText UGF_BattleFlowComponent::GetSpotlightName() const
{
	return GetDisplayNameForSlot(GetSpotlightSlot());
}

FText UGF_BattleFlowComponent::GetDisplayNameForSlot(const FGF_BattleSlot& Slot) const
{
	return (Board && Slot.IsValidSlot()) ? Board->GetDisplayNameForSlot(Slot) : FText::GetEmpty();
}

bool UGF_BattleFlowComponent::GetCommandForSlot(const FGF_BattleSlot& Slot, FGF_BattleAction& OutAction) const
{
	if (const FGF_BattleAction* Found = IssuedActions.FindByPredicate(
		[&Slot](const FGF_BattleAction& Action) { return Action.Actor == Slot; }))
	{
		OutAction = *Found;
		return true;
	}

	OutAction = FGF_BattleAction();
	return false;
}

TArray<FGF_TurnOrderEntry> UGF_BattleFlowComponent::BuildPreviewTurnOrder() const
{
	return UGF_TurnOrderLibrary::PreviewTurnOrder(Board, IssuedActions, CurrentWeather, PriorityConfig);
}

int32 UGF_BattleFlowComponent::GetActionsRemainingThisRound() const
{
	int32 Remaining = 0;
	for (const FGF_TurnOrderEntry& Entry : TurnOrder)
	{
		if (!Entry.bHasActed && !Entry.bCancelled)
		{
			++Remaining;
		}
	}
	return Remaining;
}

bool UGF_BattleFlowComponent::IsSlotIncapacitated(const FGF_BattleSlot& Slot) const
{
	const AGF_Creature* Creature = Board ? Board->GetCreatureInSlot(Slot) : nullptr;
	if (Creature == nullptr)
	{
		return true;
	}

	// Full paralysis is deliberately absent: it is a roll made at the moment of
	// acting, not a state that can be read off the creature beforehand.
	return Creature->Status == EGF_STATUS::Sleeping
		|| Creature->Status == EGF_STATUS::Frozen
		|| Creature->bIsFlinched
		|| Creature->bTruantLoafingThisTurn;
}

bool UGF_BattleFlowComponent::CanFlee(EGF_BattleSide FleeingSide) const
{
	if (Board == nullptr || BattleKind != EGF_BattleKind::Wild || bEscapeBlocked)
	{
		return false;
	}

	for (const FGF_BattleSlot& Slot : Board->GetLivingSlots(FleeingSide))
	{
		if (FGF_BattleBridge::EscapeBlocked(Board->GetCreatureInSlot(Slot)))
		{
			return false;
		}
	}

	return true;
}

bool UGF_BattleFlowComponent::FindFastestLivingSlot(
	EGF_BattleSide Side, FGF_BattleSlot& OutSlot, float& OutSpeed) const
{
	OutSlot  = FGF_BattleSlot();
	OutSpeed = 0.0f;

	if (Board == nullptr) { return false; }

	bool bFound = false;
	for (const FGF_BattleSlot& Slot : Board->GetLivingSlots(Side))
	{
		const float Speed = UGF_TurnOrderLibrary::GetEffectiveSpeed(Board->GetCreatureInSlot(Slot), CurrentWeather);
		if (!bFound || Speed > OutSpeed)
		{
			OutSlot  = Slot;
			OutSpeed = Speed;
			bFound   = true;
		}
	}
	return bFound;
}

float UGF_BattleFlowComponent::GetFleeChance(EGF_BattleSide FleeingSide) const
{
	if (!CanFlee(FleeingSide))
	{
		return 0.0f;
	}

	FGF_BattleSlot Unused;
	float Ours = 0.0f;
	float Theirs = 0.0f;
	FindFastestLivingSlot(FleeingSide, Unused, Ours);
	FindFastestLivingSlot(UGF_BattleBoard::GetOpposingSide(FleeingSide), Unused, Theirs);

	// Nothing standing on the other side, or nothing there faster than us.
	if (Theirs <= 0.0f || Ours >= Theirs)
	{
		return 1.0f;
	}

	return FMath::Clamp(
		FleeBaseOdds * (Ours / Theirs) + FleeOddsPerFailedAttempt * FailedFleeAttempts,
		0.0f, 1.0f);
}

void UGF_BattleFlowComponent::RollFleeForCurrentTurn()
{
	const EGF_BattleSide Side = CurrentTurnContext.Side;

	CurrentFleeChance = GetFleeChance(Side);

	// Certainty and impossibility are not left to the random draw: a chance of
	// exactly 1 must never fail on an edge of FRand's range.
	bCurrentFleeEscaped = CurrentFleeChance >= 1.0f
		|| (CurrentFleeChance > 0.0f && FMath::FRand() < CurrentFleeChance);

	float Unused = 0.0f;
	FindFastestLivingSlot(UGF_BattleBoard::GetOpposingSide(Side), CurrentFleeBlockerSlot, Unused);

	bCurrentFleeRolled = true;

	GF_FLOWLOG("Flee: %.0f%% odds after %d failed attempt(s) -- %s.",
		CurrentFleeChance * 100.0f, FailedFleeAttempts,
		bCurrentFleeEscaped ? TEXT("got away") : TEXT("caught"));
}

//======================================================================================
// INTERNAL
//======================================================================================

void UGF_BattleFlowComponent::SetPhase(EGF_BattlePhase NewPhase)
{
	if (Phase == NewPhase)
	{
		return;
	}

	const EGF_BattlePhase OldPhase = Phase;
	Phase = NewPhase;
	OnBattlePhaseChanged.Broadcast(OldPhase, NewPhase);
}

void UGF_BattleFlowComponent::RefreshTurnOrderEntryStates()
{
	for (FGF_TurnOrderEntry& Entry : TurnOrder)
	{
		if (!Entry.bHasActed && !Board->IsSlotLiving(Entry.Slot))
		{
			Entry.bCancelled = true;
		}
	}
}

void UGF_BattleFlowComponent::BroadcastTurnOrder()
{
	// One event for both shapes of the ribbon: the live prediction while the
	// player is choosing, the committed sequence once it is locked. A widget
	// binds this once and never asks which phase it is in.
	if (Phase == EGF_BattlePhase::Command)
	{
		OnTurnOrderChanged.Broadcast(BuildPreviewTurnOrder());
	}
	else
	{
		OnTurnOrderChanged.Broadcast(TurnOrder);
	}
}

FGF_BattleAction UGF_BattleFlowComponent::BuildDefaultEnemyAction(const FGF_BattleSlot& Slot) const
{
	return UGF_BattleAILibrary::DecideAction(Board, Slot, EnemyAIProfile, CurrentWeather);
}

//======================================================================================
// RESOLUTION HELPERS
//======================================================================================

UGF_BattleComponent* UGF_BattleFlowComponent::GetDamageComponent()
{
	if (DamageComponent != nullptr)
	{
		return DamageComponent;
	}

	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}

	// Reuse one the owner already has -- a project that put a UGF_BattleComponent
	// on its battle director in the editor should get that one, not a second
	// invisible copy with different debug settings.
	DamageComponent = Owner->FindComponentByClass<UGF_BattleComponent>();

	if (DamageComponent == nullptr)
	{
		DamageComponent = NewObject<UGF_BattleComponent>(Owner, TEXT("GF_BattleFlowDamage"));
		DamageComponent->RegisterComponent();
	}

	return DamageComponent;
}

FGF_ActionResolution UGF_BattleFlowComponent::PlanCurrentTurn()
{
	if (Phase != EGF_BattlePhase::Resolving)
	{
		UE_LOG(LogGF_BattleFlow, Warning,
			TEXT("PlanCurrentTurn outside the Resolving phase. Nothing is acting."));
		return FGF_ActionResolution();
	}

	FGF_ActionResolution Plan = UGF_BattleResolver::PlanAction(
		GetDamageComponent(), Board, CurrentTurnContext.Action, CurrentWeather);

	// The resolver computes nothing for a Flee. The verdict is the flow's, rolled
	// when the turn began, and rides on the plan so the message can be built
	// from the same roll that ends -- or does not end -- the battle.
	if (CurrentTurnContext.Action.ActionType == EGF_BattleActionType::Flee && bCurrentFleeRolled)
	{
		Plan.bFleeEscaped = bCurrentFleeEscaped;
		Plan.FleeChance   = CurrentFleeChance;

		if (!bCurrentFleeEscaped && Board && CurrentFleeBlockerSlot.IsValidSlot())
		{
			Plan.FleeBlocker            = Board->GetCreatureInSlot(CurrentFleeBlockerSlot);
			Plan.FleeBlockerDisplayName = Board->GetDisplayNameForSlot(CurrentFleeBlockerSlot);
		}
	}

	return Plan;
}

TArray<FGF_RoundEndTick> UGF_BattleFlowComponent::ResolveRoundEndTicks()
{
	return UGF_BattleResolver::ResolveRoundEnd(this, Board, CurrentWeather, ChipConfig);
}

FGF_RoundEndTick UGF_BattleFlowComponent::ResolveRoundEndTickForSlot(const FGF_BattleSlot& Slot)
{
	return UGF_BattleResolver::ResolveRoundEndForSlot(this, Board, Slot, CurrentWeather, ChipConfig);
}

TArray<FGF_BattleSlot> UGF_BattleFlowComponent::GetRoundEndSlots() const
{
	return Board ? Board->GetAllLivingSlots() : TArray<FGF_BattleSlot>();
}

#undef GF_FLOWLOG
