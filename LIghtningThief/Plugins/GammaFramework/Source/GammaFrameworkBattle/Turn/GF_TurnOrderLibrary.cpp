#include "GF_TurnOrderLibrary.h"

#include "GF_BattleBoard.h"
#include "GF_Creature.h"
#include "GF_CreatureStatStageComponent.h"
#include "GF_CreatureTraits.h"
#include "GF_SkillDefinition.h"

float UGF_TurnOrderLibrary::GetEffectiveSpeed(const AGF_Creature* Creature, EGF_WeatherType Weather)
{
	if (Creature == nullptr)
	{
		return 0.0f;
	}

	float Speed = Creature->CurrentStats.Speed;

	if (const UGF_CreatureStatStageComponent* Stages = Creature->FindComponentByClass<UGF_CreatureStatStageComponent>())
	{
		Speed *= Stages->GetStatMultiplier(EGF_StatStages::SpeedUp);
	}

	// Currentborne in rain, Sunfed in harsh sun. Read CurrentTrait, not the
	// instance data -- Trace overwrites it mid-fight.
	Speed *= UGF_CreatureTraitLibrary::GetSpeedMultiplier(Creature->CurrentTrait, Weather);

	// Paralysis halves what is left, so a +2 paralysed creature is still slower
	// than the same creature unparalysed.
	if (Creature->Status == EGF_STATUS::Paralyzed)
	{
		Speed *= 0.5f;
	}

	return FMath::Max(0.0f, Speed);
}

float UGF_TurnOrderLibrary::GetFastestSpeedOnSide(const UGF_BattleBoard* Board, EGF_BattleSide Side, EGF_WeatherType Weather)
{
	if (Board == nullptr)
	{
		return 0.0f;
	}

	float Fastest = 0.0f;
	for (const FGF_BattleSlot& Slot : Board->GetLivingSlots(Side))
	{
		Fastest = FMath::Max(Fastest, GetEffectiveSpeed(Board->GetCreatureInSlot(Slot), Weather));
	}
	return Fastest;
}

int32 UGF_TurnOrderLibrary::GetSkillPriority(TSubclassOf<AGF_SkillDefinition> SkillClass)
{
	if (!SkillClass)
	{
		return 0;
	}

	const AGF_SkillDefinition* SkillCDO = SkillClass.GetDefaultObject();
	return SkillCDO ? SkillCDO->Priority : 0;
}

int32 UGF_TurnOrderLibrary::GetActionPriority(const FGF_BattleAction& Action, const FGF_ActionPriorityConfig& Config)
{
	switch (Action.ActionType)
	{
	case EGF_BattleActionType::Skill:  return GetSkillPriority(Action.SkillClass);
	case EGF_BattleActionType::Flee:   return Config.FleePriority;
	case EGF_BattleActionType::Swap:   return Config.SwapPriority;
	case EGF_BattleActionType::Item:   return Config.ItemPriority;
	case EGF_BattleActionType::Marque: return Config.MarquePriority;
	case EGF_BattleActionType::Brace:  return Config.BracePriority;
	case EGF_BattleActionType::Pass:   return Config.PassPriority;
	default:                           return 0;
	}
}

void UGF_TurnOrderLibrary::StampActionForOrdering(
	FGF_BattleAction& Action,
	const UGF_BattleBoard* Board,
	EGF_WeatherType Weather,
	const FGF_ActionPriorityConfig& Config,
	int32 TieBreakOverride)
{
	Action.Priority = GetActionPriority(Action, Config);

	const AGF_Creature* Creature = Board ? Board->GetCreatureInSlot(Action.Actor) : nullptr;
	Action.ResolvedSpeed = GetEffectiveSpeed(Creature, Weather);

	Action.TieBreak = (TieBreakOverride == INDEX_NONE)
		? FMath::RandRange(0, MAX_int32 - 1)
		: TieBreakOverride;
}

bool UGF_TurnOrderLibrary::CompareActions(const FGF_BattleAction& A, const FGF_BattleAction& B)
{
	if (A.Priority != B.Priority)
	{
		return A.Priority > B.Priority;
	}

	if (!FMath::IsNearlyEqual(A.ResolvedSpeed, B.ResolvedSpeed))
	{
		return A.ResolvedSpeed > B.ResolvedSpeed;
	}

	if (A.TieBreak != B.TieBreak)
	{
		return A.TieBreak > B.TieBreak;
	}

	// Identical tiebreaks only happen when both were overridden to the same
	// value. Fall through to the seat so the sort stays a strict weak ordering
	// rather than depending on the incoming array order.
	if (A.Actor.Side != B.Actor.Side)
	{
		return A.Actor.Side == EGF_BattleSide::Player;
	}
	return A.Actor.Index < B.Actor.Index;
}

FGF_TurnOrderEntry UGF_TurnOrderLibrary::MakeEntry(const UGF_BattleBoard* Board, const FGF_BattleAction& Action, int32 OrderIndex)
{
	FGF_TurnOrderEntry Entry;
	Entry.Slot = Action.Actor;
	Entry.Side = Action.Actor.Side;
	Entry.ActionType = Action.ActionType;
	Entry.Priority = Action.Priority;
	Entry.ResolvedSpeed = Action.ResolvedSpeed;
	Entry.OrderIndex = OrderIndex;

	if (Board != nullptr)
	{
		Entry.Creature = Board->GetCreatureInSlot(Action.Actor);
		Entry.DisplayName = Board->GetDisplayNameForSlot(Action.Actor);
		Entry.Icon = Board->GetIconForSlot(Action.Actor);
		Entry.bCancelled = !Board->IsSlotLiving(Action.Actor);
	}

	return Entry;
}

void UGF_TurnOrderLibrary::SortActions(TArray<FGF_BattleAction>& Actions)
{
	Actions.Sort(&UGF_TurnOrderLibrary::CompareActions);
}

TArray<FGF_TurnOrderEntry> UGF_TurnOrderLibrary::BuildTurnOrder(const UGF_BattleBoard* Board, const TArray<FGF_BattleAction>& StampedActions)
{
	TArray<FGF_BattleAction> Sorted = StampedActions;
	Sorted.Sort(&UGF_TurnOrderLibrary::CompareActions);

	TArray<FGF_TurnOrderEntry> Result;
	Result.Reserve(Sorted.Num());
	for (int32 i = 0; i < Sorted.Num(); ++i)
	{
		Result.Add(MakeEntry(Board, Sorted[i], i));
	}
	return Result;
}

TArray<FGF_TurnOrderEntry> UGF_TurnOrderLibrary::PreviewTurnOrder(
	const UGF_BattleBoard* Board,
	const TArray<FGF_BattleAction>& IssuedActions,
	EGF_WeatherType Weather,
	const FGF_ActionPriorityConfig& Config)
{
	TArray<FGF_TurnOrderEntry> Empty;
	if (Board == nullptr)
	{
		return Empty;
	}

	TArray<FGF_BattleAction> Provisional;

	for (const FGF_BattleSlot& Slot : Board->GetAllLivingSlots())
	{
		const FGF_BattleAction* Issued = IssuedActions.FindByPredicate(
			[&Slot](const FGF_BattleAction& Candidate) { return Candidate.Actor == Slot; });

		FGF_BattleAction Provisioned;
		if (Issued != nullptr)
		{
			Provisioned = *Issued;
		}
		else
		{
			// No order yet. Rank it on Speed alone at the neutral bracket rather
			// than hiding it -- a seat that is missing from the ribbon is more
			// confusing than one that moves once its order lands.
			Provisioned.Actor = Slot;
			Provisioned.ActionType = EGF_BattleActionType::None;
		}

		// Tiebreak is the seat, not a roll: the preview must not reshuffle on
		// every redraw while the player is reading it.
		const int32 StableTieBreak = (Slot.Side == EGF_BattleSide::Player ? 100 : 0) - Slot.Index;
		StampActionForOrdering(Provisioned, Board, Weather, Config, StableTieBreak);

		// An unissued seat has no bracket to claim yet.
		if (Issued == nullptr)
		{
			Provisioned.Priority = 0;
		}

		Provisional.Add(Provisioned);
	}

	return BuildTurnOrder(Board, Provisional);
}

TArray<FGF_BattleSlot> UGF_TurnOrderLibrary::ExpandTargets(
	const UGF_BattleBoard* Board,
	const FGF_BattleSlot& ActorSlot,
	EGF_SkillTargetShape Shape,
	const FGF_BattleSlot& PrimaryTarget)
{
	TArray<FGF_BattleSlot> Result;
	if (Board == nullptr)
	{
		return Result;
	}

	switch (Shape)
	{
	case EGF_SkillTargetShape::Self:
		Result.Add(ActorSlot);
		break;

	case EGF_SkillTargetShape::AllEnemies:
		Result = Board->GetLivingOpponentsOf(ActorSlot);
		break;

	case EGF_SkillTargetShape::AllAllies:
		Result = Board->GetLivingAlliesOf(ActorSlot, true);
		break;

	case EGF_SkillTargetShape::Everyone:
		Result = Board->GetAllLivingSlots();
		break;

	case EGF_SkillTargetShape::Single:
	default:
		if (PrimaryTarget.IsValidSlot() && Board->IsSlotLiving(PrimaryTarget))
		{
			Result.Add(PrimaryTarget);
		}
		else
		{
			// The chosen target went down before the action resolved. Redirect to
			// the first living opponent rather than fizzling -- at four-a-side a
			// focused-down target is the common case, not an edge case.
			const TArray<FGF_BattleSlot> Fallback = Board->GetLivingOpponentsOf(ActorSlot);
			if (Fallback.Num() > 0)
			{
				Result.Add(Fallback[0]);
			}
		}
		break;
	}

	return Result;
}

bool UGF_TurnOrderLibrary::EqualBattleSlot(const FGF_BattleSlot& A, const FGF_BattleSlot& B)
{
	return A == B;
}

bool UGF_TurnOrderLibrary::NotEqualBattleSlot(const FGF_BattleSlot& A, const FGF_BattleSlot& B)
{
	return A != B;
}

bool UGF_TurnOrderLibrary::IsValidBattleSlot(const FGF_BattleSlot& Slot)
{
	return Slot.IsValidSlot();
}

FGF_BattleSlot UGF_TurnOrderLibrary::MakeBattleSlot(EGF_BattleSide Side, int32 Index)
{
	return FGF_BattleSlot(Side, Index);
}

FString UGF_TurnOrderLibrary::BattleSlotToString(const FGF_BattleSlot& Slot)
{
	return Slot.ToString();
}

int32 UGF_TurnOrderLibrary::FindEntryForSlot(const TArray<FGF_TurnOrderEntry>& TurnOrder, const FGF_BattleSlot& Slot)
{
	return TurnOrder.IndexOfByPredicate(
		[&Slot](const FGF_TurnOrderEntry& Entry) { return Entry.Slot == Slot; });
}
