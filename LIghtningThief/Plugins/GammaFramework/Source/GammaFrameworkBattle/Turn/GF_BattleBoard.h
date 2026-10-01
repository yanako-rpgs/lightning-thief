#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GF_BattleTypes.h"
#include "GF_BattleBoard.generated.h"

class AGF_Creature;
class UPaperSprite;

/**
 * Who is standing where.
 *
 * The board owns the seat -> creature mapping and nothing else. It has no phase,
 * no turn order and no opinion about how a turn resolves — ask it a question
 * about the field and it answers, which is what lets the flow component stay a
 * state machine instead of also being a lookup table.
 *
 * Four active seats per side by default, drawn from a party of six. Empty seats
 * are legal: a side that only has two creature left fields two, and the other
 * two slots hold null. Nothing auto-fills them mid-round.
 */
UCLASS(BlueprintType)
class GAMMAFRAMEWORKBATTLE_API UGF_BattleBoard : public UObject
{
	GENERATED_BODY()

public:
	/** Wipe the board and size both active lines to InMaxActiveSlots. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Board")
	void InitializeBoard(int32 InMaxActiveSlots = 4);

	/**
	 * Fill one side.
	 *
	 * Active is taken in order into slots 0..n-1 and truncated at MaxActiveSlots;
	 * anything left over is prepended to Reserves rather than dropped, so passing
	 * a whole party of six here does the sensible thing.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Board")
	void SetSideLineup(EGF_BattleSide Side, const TArray<AGF_Creature*>& Active, const TArray<AGF_Creature*>& Reserves);

	//==================================================================================
	// SEAT ACCESS
	//==================================================================================

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	int32 GetMaxActiveSlots() const { return MaxActiveSlots; }

	/** Null when the seat is empty or the slot is out of range. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	AGF_Creature* GetCreatureInSlot(const FGF_BattleSlot& Slot) const;

	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Board")
	void SetCreatureInSlot(const FGF_BattleSlot& Slot, AGF_Creature* Creature);

	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Board")
	void ClearSlot(const FGF_BattleSlot& Slot);

	/** False when the creature is not on the field. OutSlot is left invalid in that case. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	bool FindSlotForCreature(const AGF_Creature* Creature, FGF_BattleSlot& OutSlot) const;

	//==================================================================================
	// QUERIES
	//==================================================================================

	/** Every seat on this side holding a creature, downed or not. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	TArray<FGF_BattleSlot> GetOccupiedSlots(EGF_BattleSide Side) const;

	/** Every seat on this side holding a creature that is still up. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	TArray<FGF_BattleSlot> GetLivingSlots(EGF_BattleSide Side) const;

	/** Both sides' living seats, player line first. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	TArray<FGF_BattleSlot> GetAllLivingSlots() const;

	/** Living seats on the side opposite the one given. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	TArray<FGF_BattleSlot> GetLivingOpponentsOf(const FGF_BattleSlot& Slot) const;

	/** Living seats on the same side as the one given. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	TArray<FGF_BattleSlot> GetLivingAlliesOf(const FGF_BattleSlot& Slot, bool bIncludeSelf = true) const;

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	int32 CountLivingOnField(EGF_BattleSide Side) const;

	/**
	 * Can this side still fight — counting reserves?
	 *
	 * This is the question the win check asks, not "is the field empty". A side
	 * whose four actives are all down but who still has two in the party has not
	 * lost; it has to send them out.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	bool SideHasUsableCreature(EGF_BattleSide Side) const;

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	TArray<AGF_Creature*> GetReserves(EGF_BattleSide Side) const;

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	bool HasLivingReserve(EGF_BattleSide Side) const;

	/** Seats on this side that are empty and could take a reserve. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	TArray<FGF_BattleSlot> GetEmptySlots(EGF_BattleSide Side) const;

	//==================================================================================
	// MUTATION
	//==================================================================================

	/**
	 * Bring a reserve into a seat, sending whoever was there back to reserves.
	 * Refuses a downed reserve and an out-of-range index.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Board")
	bool SwapInReserve(const FGF_BattleSlot& Slot, int32 ReserveIndex);

	/** Put a creature into an empty seat without displacing anyone. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Board")
	bool SendOutToSlot(const FGF_BattleSlot& Slot, AGF_Creature* Creature);

	//==================================================================================
	// PRESENTATION HELPERS
	//==================================================================================

	/**
	 * What to call the creature in this seat.
	 *
	 * Two unnicknamed creature of the same species on the same side come back as
	 * "Ashling A" and "Ashling B". Without this the turn ribbon shows four
	 * identical names and the player cannot tell which one is about to act, which
	 * is the exact confusion four-a-side introduces.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	FText GetDisplayNameForSlot(const FGF_BattleSlot& Slot) const;

	/** Vault icon for the seat — the unique variant when the creature is unique. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	UPaperSprite* GetIconForSlot(const FGF_BattleSlot& Slot) const;

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	static EGF_BattleSide GetOpposingSide(EGF_BattleSide Side);

	/** True when the seat holds a creature that is on the field and not down. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Board")
	bool IsSlotLiving(const FGF_BattleSlot& Slot) const;

private:
	const TArray<TObjectPtr<AGF_Creature>>& GetActiveLine(EGF_BattleSide Side) const;
	TArray<TObjectPtr<AGF_Creature>>& GetActiveLineMutable(EGF_BattleSide Side);
	TArray<TObjectPtr<AGF_Creature>>& GetReserveLineMutable(EGF_BattleSide Side);

	UPROPERTY()
	int32 MaxActiveSlots = 4;

	UPROPERTY()
	TArray<TObjectPtr<AGF_Creature>> PlayerActive;

	UPROPERTY()
	TArray<TObjectPtr<AGF_Creature>> EnemyActive;

	UPROPERTY()
	TArray<TObjectPtr<AGF_Creature>> PlayerReserve;

	UPROPERTY()
	TArray<TObjectPtr<AGF_Creature>> EnemyReserve;
};
