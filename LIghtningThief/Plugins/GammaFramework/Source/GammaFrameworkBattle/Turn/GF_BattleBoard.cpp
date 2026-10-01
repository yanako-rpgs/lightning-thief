#include "GF_BattleBoard.h"

#include "GF_Creature.h"
#include "PaperSprite.h"

namespace
{
	/** A creature that is on the field and can still take a turn. */
	FORCEINLINE bool IsCreatureUsable(const AGF_Creature* Creature)
	{
		return Creature != nullptr && !Creature->IsDowned();
	}
}

void UGF_BattleBoard::InitializeBoard(int32 InMaxActiveSlots)
{
	MaxActiveSlots = FMath::Max(1, InMaxActiveSlots);

	PlayerActive.Reset();
	EnemyActive.Reset();
	PlayerReserve.Reset();
	EnemyReserve.Reset();

	PlayerActive.SetNum(MaxActiveSlots);
	EnemyActive.SetNum(MaxActiveSlots);
}

void UGF_BattleBoard::SetSideLineup(EGF_BattleSide Side, const TArray<AGF_Creature*>& Active, const TArray<AGF_Creature*>& Reserves)
{
	if (PlayerActive.Num() != MaxActiveSlots || EnemyActive.Num() != MaxActiveSlots)
	{
		InitializeBoard(MaxActiveSlots);
	}

	TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLineMutable(Side);
	TArray<TObjectPtr<AGF_Creature>>& Bench = GetReserveLineMutable(Side);

	Line.Reset();
	Line.SetNum(MaxActiveSlots);
	Bench.Reset();

	// Anything past the fourth seat is bench, not a discard. Handing this the
	// whole party of six is the common call and it should not silently lose two.
	for (int32 i = 0; i < Active.Num(); ++i)
	{
		if (i < MaxActiveSlots)
		{
			Line[i] = Active[i];

			// Told here, so anything hanging off the creature -- a health bar
			// that must not overlap its neighbour, a ring, a camera -- knows its
			// seat the moment it has one, rather than having to ask the board at
			// a time when the board may not be built yet.
			if (Active[i]) { Active[i]->BattleSeatIndex = i; }
		}
		else
		{
			Bench.Add(Active[i]);
			if (Active[i]) { Active[i]->BattleSeatIndex = INDEX_NONE; }
		}
	}

	for (AGF_Creature* Reserve : Reserves)
	{
		Bench.Add(Reserve);
		if (Reserve) { Reserve->BattleSeatIndex = INDEX_NONE; }
	}
}

AGF_Creature* UGF_BattleBoard::GetCreatureInSlot(const FGF_BattleSlot& Slot) const
{
	const TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLine(Slot.Side);
	return Line.IsValidIndex(Slot.Index) ? Line[Slot.Index].Get() : nullptr;
}

void UGF_BattleBoard::SetCreatureInSlot(const FGF_BattleSlot& Slot, AGF_Creature* Creature)
{
	TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLineMutable(Slot.Side);

	// The creature leaving the seat no longer has one, and the one arriving
	// does. Swaps and replacements come through here, so both stay true.
	if (Line.IsValidIndex(Slot.Index) && Line[Slot.Index])
	{
		Line[Slot.Index]->BattleSeatIndex = INDEX_NONE;
	}
	if (Creature) { Creature->BattleSeatIndex = Slot.Index; }
	if (Line.IsValidIndex(Slot.Index))
	{
		Line[Slot.Index] = Creature;
	}
}

void UGF_BattleBoard::ClearSlot(const FGF_BattleSlot& Slot)
{
	SetCreatureInSlot(Slot, nullptr);
}

bool UGF_BattleBoard::FindSlotForCreature(const AGF_Creature* Creature, FGF_BattleSlot& OutSlot) const
{
	OutSlot = FGF_BattleSlot();

	if (Creature == nullptr)
	{
		return false;
	}

	const EGF_BattleSide Sides[] = { EGF_BattleSide::Player, EGF_BattleSide::Enemy };
	for (EGF_BattleSide Side : Sides)
	{
		const TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLine(Side);
		for (int32 i = 0; i < Line.Num(); ++i)
		{
			if (Line[i].Get() == Creature)
			{
				OutSlot = FGF_BattleSlot(Side, i);
				return true;
			}
		}
	}

	return false;
}

TArray<FGF_BattleSlot> UGF_BattleBoard::GetOccupiedSlots(EGF_BattleSide Side) const
{
	TArray<FGF_BattleSlot> Result;
	const TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLine(Side);
	for (int32 i = 0; i < Line.Num(); ++i)
	{
		if (Line[i] != nullptr)
		{
			Result.Emplace(Side, i);
		}
	}
	return Result;
}

TArray<FGF_BattleSlot> UGF_BattleBoard::GetLivingSlots(EGF_BattleSide Side) const
{
	TArray<FGF_BattleSlot> Result;
	const TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLine(Side);
	for (int32 i = 0; i < Line.Num(); ++i)
	{
		if (IsCreatureUsable(Line[i].Get()))
		{
			Result.Emplace(Side, i);
		}
	}
	return Result;
}

TArray<FGF_BattleSlot> UGF_BattleBoard::GetAllLivingSlots() const
{
	TArray<FGF_BattleSlot> Result = GetLivingSlots(EGF_BattleSide::Player);
	Result.Append(GetLivingSlots(EGF_BattleSide::Enemy));
	return Result;
}

TArray<FGF_BattleSlot> UGF_BattleBoard::GetLivingOpponentsOf(const FGF_BattleSlot& Slot) const
{
	return GetLivingSlots(GetOpposingSide(Slot.Side));
}

TArray<FGF_BattleSlot> UGF_BattleBoard::GetLivingAlliesOf(const FGF_BattleSlot& Slot, bool bIncludeSelf) const
{
	TArray<FGF_BattleSlot> Result = GetLivingSlots(Slot.Side);
	if (!bIncludeSelf)
	{
		Result.Remove(Slot);
	}
	return Result;
}

int32 UGF_BattleBoard::CountLivingOnField(EGF_BattleSide Side) const
{
	int32 Count = 0;
	for (const TObjectPtr<AGF_Creature>& Creature : GetActiveLine(Side))
	{
		if (IsCreatureUsable(Creature.Get()))
		{
			++Count;
		}
	}
	return Count;
}

bool UGF_BattleBoard::SideHasUsableCreature(EGF_BattleSide Side) const
{
	if (CountLivingOnField(Side) > 0)
	{
		return true;
	}
	return HasLivingReserve(Side);
}

TArray<AGF_Creature*> UGF_BattleBoard::GetReserves(EGF_BattleSide Side) const
{
	const TArray<TObjectPtr<AGF_Creature>>& Bench =
		(Side == EGF_BattleSide::Player) ? PlayerReserve : EnemyReserve;

	TArray<AGF_Creature*> Result;
	Result.Reserve(Bench.Num());
	for (const TObjectPtr<AGF_Creature>& Creature : Bench)
	{
		Result.Add(Creature.Get());
	}
	return Result;
}

bool UGF_BattleBoard::HasLivingReserve(EGF_BattleSide Side) const
{
	const TArray<TObjectPtr<AGF_Creature>>& Bench =
		(Side == EGF_BattleSide::Player) ? PlayerReserve : EnemyReserve;

	for (const TObjectPtr<AGF_Creature>& Creature : Bench)
	{
		if (IsCreatureUsable(Creature.Get()))
		{
			return true;
		}
	}
	return false;
}

TArray<FGF_BattleSlot> UGF_BattleBoard::GetEmptySlots(EGF_BattleSide Side) const
{
	TArray<FGF_BattleSlot> Result;
	const TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLine(Side);
	for (int32 i = 0; i < Line.Num(); ++i)
	{
		if (Line[i] == nullptr)
		{
			Result.Emplace(Side, i);
		}
	}
	return Result;
}

bool UGF_BattleBoard::SwapInReserve(const FGF_BattleSlot& Slot, int32 ReserveIndex)
{
	TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLineMutable(Slot.Side);
	TArray<TObjectPtr<AGF_Creature>>& Bench = GetReserveLineMutable(Slot.Side);

	if (!Line.IsValidIndex(Slot.Index) || !Bench.IsValidIndex(ReserveIndex))
	{
		return false;
	}

	if (!IsCreatureUsable(Bench[ReserveIndex].Get()))
	{
		return false;
	}

	TObjectPtr<AGF_Creature> Incoming = Bench[ReserveIndex];
	TObjectPtr<AGF_Creature> Outgoing = Line[Slot.Index];

	Line[Slot.Index] = Incoming;

	// Keep the bench position rather than appending, so a party ordering the
	// player set up in the menu survives a round of swapping.
	Bench[ReserveIndex] = Outgoing;
	if (Outgoing == nullptr)
	{
		Bench.RemoveAt(ReserveIndex);
	}

	return true;
}

bool UGF_BattleBoard::SendOutToSlot(const FGF_BattleSlot& Slot, AGF_Creature* Creature)
{
	TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLineMutable(Slot.Side);
	if (!Line.IsValidIndex(Slot.Index) || Line[Slot.Index] != nullptr || Creature == nullptr)
	{
		return false;
	}

	Line[Slot.Index] = Creature;
	GetReserveLineMutable(Slot.Side).Remove(Creature);
	return true;
}

FText UGF_BattleBoard::GetDisplayNameForSlot(const FGF_BattleSlot& Slot) const
{
	const AGF_Creature* Creature = GetCreatureInSlot(Slot);
	if (Creature == nullptr)
	{
		return FText::GetEmpty();
	}

	const FName BaseName = Creature->Name;

	// Which seats on this side share the name, in slot order.
	const TArray<TObjectPtr<AGF_Creature>>& Line = GetActiveLine(Slot.Side);
	TArray<int32> Matching;
	for (int32 i = 0; i < Line.Num(); ++i)
	{
		if (Line[i] != nullptr && Line[i]->Name == BaseName)
		{
			Matching.Add(i);
		}
	}

	if (Matching.Num() <= 1)
	{
		return FText::FromName(BaseName);
	}

	const int32 Ordinal = Matching.IndexOfByKey(Slot.Index);
	if (Ordinal == INDEX_NONE)
	{
		return FText::FromName(BaseName);
	}

	// A, B, C, D — and past Z it falls back to a number rather than emitting
	// punctuation nobody can read.
	const FString Suffix = (Ordinal < 26)
		? FString::Chr(static_cast<TCHAR>(TEXT('A') + Ordinal))
		: FString::FromInt(Ordinal + 1);

	return FText::FromString(FString::Printf(TEXT("%s %s"), *BaseName.ToString(), *Suffix));
}

UPaperSprite* UGF_BattleBoard::GetIconForSlot(const FGF_BattleSlot& Slot) const
{
	const AGF_Creature* Creature = GetCreatureInSlot(Slot);
	if (Creature == nullptr)
	{
		return nullptr;
	}

	if (Creature->isUnique && Creature->UniqueDisplayIcon1 != nullptr)
	{
		return Creature->UniqueDisplayIcon1;
	}
	return Creature->DisplayIcon1;
}

EGF_BattleSide UGF_BattleBoard::GetOpposingSide(EGF_BattleSide Side)
{
	return (Side == EGF_BattleSide::Player) ? EGF_BattleSide::Enemy : EGF_BattleSide::Player;
}

bool UGF_BattleBoard::IsSlotLiving(const FGF_BattleSlot& Slot) const
{
	return IsCreatureUsable(GetCreatureInSlot(Slot));
}

const TArray<TObjectPtr<AGF_Creature>>& UGF_BattleBoard::GetActiveLine(EGF_BattleSide Side) const
{
	return (Side == EGF_BattleSide::Player) ? PlayerActive : EnemyActive;
}

TArray<TObjectPtr<AGF_Creature>>& UGF_BattleBoard::GetActiveLineMutable(EGF_BattleSide Side)
{
	return (Side == EGF_BattleSide::Player) ? PlayerActive : EnemyActive;
}

TArray<TObjectPtr<AGF_Creature>>& UGF_BattleBoard::GetReserveLineMutable(EGF_BattleSide Side)
{
	return (Side == EGF_BattleSide::Player) ? PlayerReserve : EnemyReserve;
}
