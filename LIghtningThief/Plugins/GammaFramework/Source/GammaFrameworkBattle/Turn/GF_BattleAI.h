#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_BattleTypes.h"
#include "GF_WeatherTypes.h"
#include "GF_BattleAI.generated.h"

class AGF_Creature;
class AGF_SkillDefinition;
class UGF_BattleBoard;

/**
 * ---------------------------------------------------------------------------
 * Deciding an enemy's order on a four-wide board.
 * ---------------------------------------------------------------------------
 *
 * The interesting question changed when the board did. In a one-on-one fight
 * "which skill" is the whole decision, because there is only ever one target.
 * At four-a-side the decision is a PAIR -- which skill, at which seat -- and
 * the second half is where the difficulty actually lives:
 *
 *   - a weak AI spreads its damage across four healthy enemies and kills none
 *   - a strong AI concentrates and removes a quarter of your output permanently
 *
 * So the tiers below are mostly about targeting, not about skill choice.
 */

//======================================================================================
// DIFFICULTY
//======================================================================================

UENUM(BlueprintType)
enum class EGF_BattleAIDifficulty : uint8
{
	/** Any usable skill at any living seat. Genuinely random, for early wild encounters. */
	Random UMETA(DisplayName = "Random"),

	/** Highest base power, still a random target. Hits hard, aims nowhere. */
	Basic  UMETA(DisplayName = "Basic"),

	/**
	 * Scores every (skill, target) pair on estimated damage: element matchup,
	 * STAB, the Physical/Magic split against the right defence, stat stages.
	 * Picks the best pair. Will not waste a status skill on an already-statused
	 * target. Does not yet think about finishing anything off.
	 */
	Smart  UMETA(DisplayName = "Smart"),

	/**
	 * Smart, plus the things that make four-a-side hard: it focus-fires anything
	 * it can remove this round, it values a spread skill by what it does to the
	 * whole line rather than to one seat, it heals, and it swaps out of a bad
	 * matchup. This is the Trial Leader tier.
	 */
	Expert UMETA(DisplayName = "Expert"),
};

/**
 * One opponent's competence and permissions.
 *
 * Difficulty is the tier; the rest are per-tamer switches, because "as clever as
 * a Trial Leader but with no potions" is a real and common setup.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_BattleAIProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | AI")
	EGF_BattleAIDifficulty Difficulty = EGF_BattleAIDifficulty::Basic;

	/**
	 * Chance per decision of taking the second-best option instead of the best.
	 *
	 * A tier that always plays perfectly reads as unfair rather than hard, and it
	 * is also predictable -- a player who knows the AI is optimal can plan around
	 * it exactly. A small wobble costs almost nothing in difficulty and removes
	 * that. Ignored by Random and Basic, which are already noise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MistakeChance = 0.15f;

	/** Multiplier on a pair's score when the hit would finish the target. Expert only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | AI", meta = (ClampMin = "1.0"))
	float FinishingBlowBias = 2.0f;

	/** Expert only. Empty means this tamer carries nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | AI")
	TArray<FName> HealItems;

	/** Fraction of MaxHP below which Expert will consider spending an item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | AI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealThreshold = 0.3f;

	/** Expert only. Off for wild creature, which have no bench to swap to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | AI")
	bool bMaySwap = false;

	/**
	 * Index into the side's reserves of this tamer's ace, or -1.
	 *
	 * The ace is never swapped to while anything else can still fight. It comes
	 * out last, and only last.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | AI", meta = (ClampMin = "-1"))
	int32 AceReserveIndex = -1;

	/**
	 * Fired when every skill is out of Uses. Leave it unset and the AI passes
	 * instead, which is survivable but looks broken.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | AI")
	TSubclassOf<AGF_SkillDefinition> LastResortSkill;
};

/** One scored option, kept so the debugger can show why the AI did what it did. */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_AIScoredOption
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle | AI")
	int32 SkillIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle | AI")
	TSubclassOf<AGF_SkillDefinition> SkillClass;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle | AI")
	FGF_BattleSlot Target;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle | AI")
	float Score = 0.0f;

	/** Rough HP the hit would take off the primary target. */
	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle | AI")
	float EstimatedDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle | AI")
	float TypeEffectiveness = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle | AI")
	bool bWouldFinish = false;

	UPROPERTY(BlueprintReadOnly, Category = "GammaFramework | Battle | AI")
	FString Reason;
};

//======================================================================================
// THE AI
//======================================================================================

UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_BattleAILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Pick this seat's order for the round.
	 *
	 * Always returns something legal. Worst case that is a Pass, which is better
	 * than an invalid order the flow has to reject mid-round.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | AI")
	static FGF_BattleAction DecideAction(
		const UGF_BattleBoard* Board,
		const FGF_BattleSlot& Slot,
		const FGF_BattleAIProfile& Profile,
		EGF_WeatherType Weather = EGF_WeatherType::None);

	/**
	 * Every (skill, target) pair this seat could take, scored, best first.
	 *
	 * Exposed so the runtime debugger can print the AI's reasoning. Reading the
	 * ranking is the only practical way to tell a bad decision from a bad score.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | AI")
	static TArray<FGF_AIScoredOption> ScoreAllOptions(
		const UGF_BattleBoard* Board,
		const FGF_BattleSlot& Slot,
		const FGF_BattleAIProfile& Profile,
		EGF_WeatherType Weather = EGF_WeatherType::None);

	/**
	 * Rough damage in HP, without running the real formula.
	 *
	 * Deliberately an estimate. The real formula spawns a skill actor per call,
	 * and scoring four skills against four targets would mean sixteen spawns per
	 * enemy per round -- 128 a round with a full enemy line. This is the classic
	 * damage shape with element, STAB and stat stages, which ranks options in the
	 * same order the real formula would without the cost.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | AI")
	static float EstimateDamage(
		const AGF_Creature* Attacker,
		const AGF_Creature* Defender,
		TSubclassOf<AGF_SkillDefinition> SkillClass,
		EGF_WeatherType Weather = EGF_WeatherType::None);

	/** Slot indices whose skill has a Use left and a class set. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | AI")
	static TArray<int32> GetUsableSkillIndices(const AGF_Creature* Creature);

private:
	static FGF_BattleAction MakeSkillAction(const FGF_BattleSlot& Slot, const UGF_BattleBoard* Board,
		int32 SkillIndex, TSubclassOf<AGF_SkillDefinition> SkillClass, const FGF_BattleSlot& Target);

	static FGF_BattleAction MakePass(const FGF_BattleSlot& Slot);

	/** Expert: is anyone on our line hurt enough to be worth an item this round? */
	static bool ConsiderHealItem(const UGF_BattleBoard* Board, const FGF_BattleSlot& Slot,
		const FGF_BattleAIProfile& Profile, FGF_BattleAction& OutAction);

	/** Expert: would a bench creature do meaningfully better than this one? */
	static bool ConsiderSwap(const UGF_BattleBoard* Board, const FGF_BattleSlot& Slot,
		const FGF_BattleAIProfile& Profile, EGF_WeatherType Weather, float CurrentBestScore,
		FGF_BattleAction& OutAction);
};
