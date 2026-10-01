#pragma once

#include "CoreMinimal.h"
#include "GF_ElementTypes.h"
#include "GF_BattleTypes.generated.h"

class AGF_Creature;
class AGF_SkillDefinition;
class UPaperSprite;

/**
 * ---------------------------------------------------------------------------
 * The vocabulary the 4v4 turn engine speaks.
 * ---------------------------------------------------------------------------
 *
 * Everything in the flow layer addresses a SLOT, never "the player's creature".
 * That single decision is what makes four-a-side work: the old single-battle
 * code had PlayerCreature and EnemyCreature as fields, so widening it to four
 * meant touching every call site. A slot is (Side, Index), the board maps slots
 * to actors, and the engine never holds a bare creature pointer as state.
 */

//======================================================================================
// SIDES AND SLOTS
//======================================================================================

UENUM(BlueprintType)
enum class EGF_BattleSide : uint8
{
	Player UMETA(DisplayName = "Player"),
	Enemy  UMETA(DisplayName = "Enemy"),
};

/**
 * One seat on the field. Side plus a zero-based index into that side's active line.
 *
 * An invalid slot (Index == INDEX_NONE) is the "nobody" value — GetCurrentSlot()
 * returns one outside the Resolving phase rather than lying about who is acting.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_BattleSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	EGF_BattleSide Side = EGF_BattleSide::Player;

	/** Position in the side's active line, 0..MaxActiveSlots-1. INDEX_NONE means no slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	int32 Index = INDEX_NONE;

	FGF_BattleSlot() = default;
	FGF_BattleSlot(EGF_BattleSide InSide, int32 InIndex)
		: Side(InSide), Index(InIndex) {}

	bool IsValidSlot() const { return Index >= 0; }

	bool operator==(const FGF_BattleSlot& Other) const
	{
		return Side == Other.Side && Index == Other.Index;
	}

	bool operator!=(const FGF_BattleSlot& Other) const { return !(*this == Other); }

	FString ToString() const
	{
		return FString::Printf(TEXT("%s[%d]"),
			Side == EGF_BattleSide::Player ? TEXT("Player") : TEXT("Enemy"), Index);
	}
};

FORCEINLINE uint32 GetTypeHash(const FGF_BattleSlot& Slot)
{
	return HashCombine(::GetTypeHash(static_cast<uint8>(Slot.Side)), ::GetTypeHash(Slot.Index));
}

//======================================================================================
// PHASES
//======================================================================================

/**
 * Where the battle is right now.
 *
 * The engine only ever moves forward through these, and every transition
 * broadcasts OnBattlePhaseChanged. Bind that instead of polling.
 */
UENUM(BlueprintType)
enum class EGF_BattlePhase : uint8
{
	/** No battle running. StartBattle() moves out of this. */
	Inactive UMETA(DisplayName = "Inactive"),

	/** Battle start: intro sequence, creatures sent out, opening dialogue. */
	Intro UMETA(DisplayName = "Intro"),

	/** Player is deciding. One command requested per living slot, in slot order. */
	Command UMETA(DisplayName = "Command"),

	/** Commands are locked, turn order is built, actions are playing out. */
	Resolving UMETA(DisplayName = "Resolving"),

	/** All actions spent. Status damage, weather, held items, Uses tick down here. */
	RoundEnd UMETA(DisplayName = "Round End"),

	/** The opposing side has no creature left that can fight. */
	Victory UMETA(DisplayName = "Victory"),

	/** The player has no creature left that can fight. Rout. */
	Defeat UMETA(DisplayName = "Defeat"),

	/** Someone successfully fled, or the wild target was claimed with a Core. */
	Ended UMETA(DisplayName = "Ended"),

	/** Outcome has been acknowledged. Safe to tear the battle scene down. */
	Finished UMETA(DisplayName = "Finished"),
};

/** Why the battle stopped. Read this in OnBattleEnded. */
UENUM(BlueprintType)
enum class EGF_BattleOutcome : uint8
{
	None      UMETA(DisplayName = "None"),
	Victory   UMETA(DisplayName = "Victory"),
	Defeat    UMETA(DisplayName = "Defeat"),
	Fled      UMETA(DisplayName = "Fled"),
	Claimed   UMETA(DisplayName = "Claimed"),
	Aborted   UMETA(DisplayName = "Aborted"),
};

UENUM(BlueprintType)
enum class EGF_BattleKind : uint8
{
	/** Against wild Vorn. Flee and Marque are legal. */
	Wild  UMETA(DisplayName = "Wild"),

	/** Against a Tamer. Flee and Marque are refused. */
	Tamer UMETA(DisplayName = "Tamer"),
};

//======================================================================================
// ACTIONS
//======================================================================================

/**
 * The action menu, one entry per row of the design's action list.
 *
 * Every one of these costs the acting creature its ONE action for the round.
 * That is the whole reason Item is in this enum rather than being a free
 * side-channel: a bag item spent on slot 2 is slot 2's turn, and the turn
 * order ribbon shows it in the sequence like any attack.
 */
UENUM(BlueprintType)
enum class EGF_BattleActionType : uint8
{
	/** No order issued yet. Not a legal locked command. */
	None   UMETA(DisplayName = "None"),

	/** Use one of the creature's four known skills. */
	Skill  UMETA(DisplayName = "Skill"),

	/** Use an item from the shared bag on a target. Costs this creature's action. */
	Item   UMETA(DisplayName = "Item"),

	/** Claim a wild Vorn. Wild battles only, requires an empty Core. */
	Marque UMETA(DisplayName = "Marque"),

	/** Bring in a reserve. The arriving creature acts from next round. */
	Swap   UMETA(DisplayName = "Swap"),

	/** Halve incoming damage this round, +1 Poise. */
	Brace  UMETA(DisplayName = "Brace"),

	/** Party-wide escape attempt. Wild only, Speed-checked against the fastest enemy. */
	Flee   UMETA(DisplayName = "Flee"),

	/** Nothing this creature can do — locked in, incapacitated, or no legal order. */
	Pass   UMETA(DisplayName = "Pass"),
};

/**
 * One creature's order for one round.
 *
 * Built during Command, sorted during Resolving, then handed back to the
 * presentation layer in the turn context so it can play the right sequence.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_BattleAction
{
	GENERATED_BODY()

	/** Who is acting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	FGF_BattleSlot Actor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	EGF_BattleActionType ActionType = EGF_BattleActionType::None;

	/** Slot 0-3 in the creature's Skills array. INDEX_NONE for Last Resort and non-skill actions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	int32 SkillIndex = INDEX_NONE;

	/** Resolved skill class. Set even for Last Resort, so the presentation layer never re-derives it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	TSubclassOf<AGF_SkillDefinition> SkillClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	EGF_SkillTargetShape TargetShape = EGF_SkillTargetShape::Single;

	/**
	 * Every seat this action reaches, already expanded from the target shape.
	 * A spread skill lists all four; the presentation layer loops it and applies
	 * the multi-target damage discount per entry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	TArray<FGF_BattleSlot> Targets;

	/** Bag item for Item, or the Core grade for Marque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	FName ItemName = NAME_None;

	/** Index into the side's reserves for Swap. INDEX_NONE otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	int32 SwapToReserveIndex = INDEX_NONE;

	/**
	 * Priority bracket, resolved at lock-in. Higher goes first regardless of Speed.
	 * Skills take it from the skill asset; the other action types take it from the
	 * flow component's priority config.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 Priority = 0;

	/** Effective Speed at lock-in, after paralysis and weather. Used for ordering only. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float ResolvedSpeed = 0.0f;

	/**
	 * Pre-rolled tiebreak, so a Speed tie resolves by a value decided ONCE rather
	 * than by a coin flip inside the sort comparator. A comparator that returns a
	 * different answer for the same pair produces a garbage order, and it only
	 * shows up as a rare mis-sequenced round that is near-impossible to reproduce.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 TieBreak = 0;

	bool IsValidAction() const
	{
		return ActionType != EGF_BattleActionType::None && Actor.IsValidSlot();
	}
};

//======================================================================================
// TURN ORDER
//======================================================================================

/**
 * One entry in the order ribbon across the top of the screen.
 *
 * Carries everything a UMG widget needs — icon, name, side, position — so the
 * widget binds one array and never reaches back into the board for details.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_TurnOrderEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_BattleSlot Slot;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TObjectPtr<AGF_Creature> Creature = nullptr;

	/**
	 * What to print. Already disambiguated: two unnicknamed creatures of the same
	 * species on the same side come back as "Ashling A" and "Ashling B", because
	 * the whole point of the ribbon is knowing WHICH one is about to act.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TObjectPtr<UPaperSprite> Icon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	EGF_BattleSide Side = EGF_BattleSide::Player;

	/** None during the Command phase preview — the order is not committed yet. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	EGF_BattleActionType ActionType = EGF_BattleActionType::None;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 Priority = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	float ResolvedSpeed = 0.0f;

	/** Zero-based position in the resolved sequence. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 OrderIndex = INDEX_NONE;

	/** This creature's action has already played out. Grey the entry, don't remove it. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bHasActed = false;

	/**
	 * The creature went down (or left the field) before its turn came up, so the
	 * action will be skipped.
	 *
	 * Marked rather than removed on purpose. Deleting the entry makes the whole
	 * ribbon shuffle left mid-round, and the player loses track of a sequence they
	 * were reading a second ago. Grey it out instead — the position stays put and
	 * the reason is legible.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bCancelled = false;

	/** True for the entry currently resolving. Exactly one at a time, or none. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	bool bIsCurrent = false;
};

//======================================================================================
// TURN CONTEXT
//======================================================================================

/** How a single creature's turn finished. */
UENUM(BlueprintType)
enum class EGF_TurnResult : uint8
{
	/** The action played out. */
	Completed          UMETA(DisplayName = "Completed"),

	/** The creature was down or off the field when its turn came up. */
	SkippedDowned      UMETA(DisplayName = "Skipped - Downed"),

	/** Asleep, frozen, flinched, fully paralysed, loafing. */
	SkippedIncapacitated UMETA(DisplayName = "Skipped - Incapacitated"),

	/** No legal order existed for this slot. */
	SkippedNoAction    UMETA(DisplayName = "Skipped - No Action"),

	/** The battle ended part-way through the round. */
	Interrupted        UMETA(DisplayName = "Interrupted"),
};

/**
 * Everything about the turn that is happening right now.
 *
 * Handed to OnTurnBegin and OnTurnEnd. Dialogue triggers, camera moves and
 * on-screen stat callouts all read this rather than querying the component,
 * so a listener that runs one frame late still sees the turn it was told about.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_TurnContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_BattleSlot Slot;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	TObjectPtr<AGF_Creature> Creature = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	EGF_BattleSide Side = EGF_BattleSide::Player;

	/** Disambiguated name, same rules as the ribbon entry. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	FGF_BattleAction Action;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 RoundNumber = 0;

	/** Position in this round's sequence. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 OrderIndex = INDEX_NONE;

	/** How many actions this round has in total, cancelled ones included. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle")
	int32 ActionsThisRound = 0;
};

//======================================================================================
// STEP GATE
//======================================================================================

/**
 * What the engine is currently waiting to be told is finished.
 *
 * The flow NEVER advances on a timer. It stops at each of these points, hands
 * out a token, and waits for AcknowledgeStep. Presentation decides how long an
 * animation, a dialogue box or a camera move actually took — which means a slow
 * machine plays the same battle, just slower, instead of the flow running ahead
 * of a delay that did not fire on time.
 */
UENUM(BlueprintType)
enum class EGF_BattleStepKind : uint8
{
	/** Nothing pending. The engine is either waiting on player input or idle. */
	None      UMETA(DisplayName = "None"),

	/** Intro sequence: send-outs, opening dialogue. */
	Intro     UMETA(DisplayName = "Intro"),

	/** A creature's turn has begun. Play the action, then acknowledge. */
	Turn      UMETA(DisplayName = "Turn"),

	/** One or more creatures went down. Play the down sequence, then acknowledge. */
	Downed    UMETA(DisplayName = "Downed"),

	/** End of round: status damage, weather, held items, Uses. */
	RoundEnd  UMETA(DisplayName = "Round End"),

	/**
	 * Seats emptied by the round and a bench that can fill them. Call
	 * SendOutReserve for each seat you want filled, then acknowledge.
	 */
	Replacement UMETA(DisplayName = "Replacement"),

	/** Victory / defeat / flee sequence. */
	Outcome   UMETA(DisplayName = "Outcome"),
};

//======================================================================================
// CONFIG
//======================================================================================

/**
 * Priority brackets for the non-skill actions.
 *
 * Skills read Priority off their own asset (-7..+7). These do not have an asset,
 * so they live here where they can be tuned per project without a recompile.
 * Defaults follow the genre: you get out, get healed or get claimed before the
 * attacks land.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_ActionPriorityConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "-7", ClampMax = "7"))
	int32 FleePriority = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "-7", ClampMax = "7"))
	int32 SwapPriority = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "-7", ClampMax = "7"))
	int32 ItemPriority = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "-7", ClampMax = "7"))
	int32 MarquePriority = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "-7", ClampMax = "7"))
	int32 BracePriority = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "-7", ClampMax = "7"))
	int32 PassPriority = -7;
};
