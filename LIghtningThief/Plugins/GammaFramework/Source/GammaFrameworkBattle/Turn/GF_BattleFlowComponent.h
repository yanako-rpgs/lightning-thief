#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GF_BattleTypes.h"
#include "GF_BattleAI.h"
#include "GF_BattleResolver.h"
#include "GF_WeatherTypes.h"
#include "GF_BattleFlowComponent.generated.h"

class AGF_Creature;
class AGF_SkillDefinition;
class UGF_BattleBoard;
class UGF_BattleComponent;

//======================================================================================
// DELEGATES
//======================================================================================

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnBattlePhaseChanged, EGF_BattlePhase, OldPhase, EGF_BattlePhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBattleRound, int32, RoundNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnTurnOrderChanged, const TArray<FGF_TurnOrderEntry>&, TurnOrder);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBattleSlotEvent, FGF_BattleSlot, Slot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnCommandIssued, FGF_BattleSlot, Slot, const FGF_BattleAction&, Action);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnTurnBegin, const FGF_TurnContext&, Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnTurnEnd, const FGF_TurnContext&, Context, EGF_TurnResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnCreatureDowned, FGF_BattleSlot, Slot, AGF_Creature*, Creature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnStepAwaitingAck, EGF_BattleStepKind, StepKind, int32, StepToken);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnReplacementNeeded, EGF_BattleSide, Side, const TArray<FGF_BattleSlot>&, EmptySlots);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBattleEnded, EGF_BattleOutcome, Outcome);

/**
 * Internal continuation marker.
 *
 * Not a UENUM and not exposed. The flow never calls its next stage directly --
 * it queues one of these and lets Pump() run them one at a time. See the
 * re-entrancy note on Pump().
 */
enum class EGF_FlowStep : uint8
{
	None,
	BeginRound,
	RequestNextCommand,
	LockCommands,
	AdvanceToNextTurn,
	ScanAfterAction,
	ContinueAfterAction,
	BeginRoundEnd,
	ScanAfterRoundEnd,
	ContinueAfterRoundEnd,
	FinishBattle,
};

//======================================================================================
// START CONFIG
//======================================================================================

USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_BattleStartConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	EGF_BattleKind Kind = EGF_BattleKind::Wild;

	/** Seats 0..3, in order. Anything past the fourth goes to the bench. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	TArray<AGF_Creature*> PlayerActive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	TArray<AGF_Creature*> PlayerReserves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	TArray<AGF_Creature*> EnemyActive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	TArray<AGF_Creature*> EnemyReserves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxActiveSlots = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	EGF_WeatherType StartingWeather = EGF_WeatherType::None;

	/** Scripted no-run battles. Flee is refused outright when set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle")
	bool bEscapeBlocked = false;
};

//======================================================================================
// THE FLOW
//======================================================================================

/**
 * The battle state machine: phases, rounds, commands, turn order, outcome.
 *
 * ---------------------------------------------------------------------------
 * What it does NOT do
 * ---------------------------------------------------------------------------
 *
 * It never applies damage, never plays an animation, never moves a camera and
 * never touches a timer. It sequences. When a creature's turn comes up it
 * broadcasts OnTurnBegin with everything about that turn and then STOPS, until
 * something calls AcknowledgeStep with the token it handed out.
 *
 * That is the fix for the delay-driven flow. A machine that takes 900ms to play
 * a hit animation acknowledges 900ms later; a machine that takes 200ms
 * acknowledges after 200ms. Neither one can have the next turn start on top of
 * the last one, because the next turn is not scheduled -- it is waiting.
 *
 * ---------------------------------------------------------------------------
 * The round
 * ---------------------------------------------------------------------------
 *
 *   Intro     -> one step, acknowledge when the send-out sequence is done
 *   Command   -> one OnCommandRequested per living player seat, in slot order.
 *                Answer each with a Submit*Command call. CancelLastCommand
 *                backs out to re-issue an earlier one.
 *   Resolving -> commands locked, order built and broadcast, then one
 *                OnTurnBegin / AcknowledgeStep pair per action.
 *   RoundEnd  -> one step for status damage, weather, held items and Uses.
 *                Send out replacements here.
 *   Victory / Defeat / Ended -> one step, then Finished.
 *
 * Every action costs its creature its one turn for the round, items included.
 * Four creature a side means up to eight actions in a round.
 */
UCLASS(ClassGroup = (GammaFramework), meta = (BlueprintSpawnableComponent))
class GAMMAFRAMEWORKBATTLE_API UGF_BattleFlowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGF_BattleFlowComponent();

	//==================================================================================
	// EVENTS
	//==================================================================================

	/** Fired on every phase transition. Bind this instead of polling GetPhase. */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnBattlePhaseChanged OnBattlePhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnBattleRound OnRoundBegin;

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnBattleRound OnRoundEnd;

	/**
	 * This seat needs an order. Open the action menu for it.
	 * GetCommandingCreature() is valid from here until the command is submitted.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnBattleSlotEvent OnCommandRequested;

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnCommandIssued OnCommandIssued;

	/** A command was backed out of. The slot is about to be asked again. */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnBattleSlotEvent OnCommandCancelled;

	/**
	 * An enemy seat needs an order, and bAutoIssueEnemyCommands is off.
	 * A tamer AI binds this and answers with SubmitCommand.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnBattleSlotEvent OnEnemyCommandRequested;

	/**
	 * The order ribbon changed.
	 *
	 * Fires with the live preview after every command during the Command phase,
	 * with the committed sequence at lock-in, and again whenever an entry is
	 * cancelled or marked as acted. One binding drives the whole widget.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnTurnOrderChanged OnTurnOrderChanged;

	/**
	 * A creature's turn has started. Play it, then acknowledge.
	 *
	 * The context carries the acting creature, its disambiguated name, its
	 * action and its targets -- everything an on-screen callout or a dialogue
	 * trigger needs, without reaching back into the component.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnTurnBegin OnTurnBegin;

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnTurnEnd OnTurnEnd;

	/** One broadcast per creature that went down, before the Downed step is raised. */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnCreatureDowned OnCreatureDowned;

	/**
	 * Empty seats and a living bench. Raised during RoundEnd; answer with
	 * SendOutReserve before acknowledging the step, or decline and leave the
	 * seat empty.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnReplacementNeeded OnReplacementNeeded;

	/**
	 * The flow has stopped and is waiting to be told the presentation finished.
	 *
	 * Bind this once for a generic "am I busy" HUD state. The specific events
	 * above fire first and are the ones worth acting on.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnStepAwaitingAck OnStepAwaitingAcknowledgement;

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework | Battle | Events")
	FGF_OnBattleEnded OnBattleEnded;

	//==================================================================================
	// SETUP
	//==================================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | Config")
	FGF_ActionPriorityConfig PriorityConfig;

	/**
	 * Fill enemy orders at lock-in using EnemyAIProfile.
	 *
	 * On by default -- the AI is real, not a placeholder, so a battle runs end to
	 * end with nothing else wired up. Turn it off only to drive the enemy line
	 * from somewhere else entirely; OnEnemyCommandRequested is raised per enemy
	 * seat instead and something must answer with SubmitCommand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | Config")
	bool bAutoIssueEnemyCommands = true;

	/**
	 * How well the opposing side plays. Set this per encounter: wild creature
	 * want Random or Basic, ordinary tamers Smart, Trial Leaders Expert with
	 * items and swapping switched on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | Config")
	FGF_BattleAIProfile EnemyAIProfile;

	/** Burn, poison and weather chip fractions used by the round-end pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | Config")
	FGF_ChipDamageConfig ChipConfig;

	/** Verbose per-transition logging under LogGF_BattleFlow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | Config")
	bool bLogFlow = true;

	UPROPERTY(BlueprintReadWrite, Category = "GammaFramework | Battle")
	EGF_WeatherType CurrentWeather = EGF_WeatherType::None;

	/**
	 * The component that owns the damage formula, found on this actor or created
	 * on demand.
	 *
	 * The formula lives on a UActorComponent rather than in a static library, so
	 * something has to hold an instance. Rather than making every caller find one,
	 * the flow keeps the canonical one and hands it out.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	UGF_BattleComponent* GetDamageComponent();

	/**
	 * Plan the turn that is resolving right now.
	 *
	 * The one-node version of PlanAction: it already knows the board, the action
	 * and the weather. Show the result, apply it, then acknowledge.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	FGF_ActionResolution PlanCurrentTurn();

	/**
	 * Run the end-of-round ticks. Call this during the RoundEnd step, before
	 * acknowledging it.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	TArray<FGF_RoundEndTick> ResolveRoundEndTicks();

	/**
	 * The end-of-round tick for one seat, so a caller can pace them.
	 *
	 * The same work ResolveRoundEndTicks does for that creature, in the same
	 * order -- one at a time, so each can be shown before the next begins.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Resolve")
	FGF_RoundEndTick ResolveRoundEndTickForSlot(const FGF_BattleSlot& Slot);

	/** Seats that may tick this round, in the order they should be shown. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Resolve")
	TArray<FGF_BattleSlot> GetRoundEndSlots() const;

	/** Sets the board up and moves to Intro. Acknowledge the Intro step to start round 1. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Flow")
	bool StartBattle(const FGF_BattleStartConfig& Config);

	/** Stop the battle now, whatever phase it is in. Raises OnBattleEnded. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Flow")
	void AbortBattle(EGF_BattleOutcome Outcome = EGF_BattleOutcome::Aborted);

	//==================================================================================
	// THE STEP GATE
	//==================================================================================

	/**
	 * Tell the flow the presentation for the pending step has finished.
	 *
	 * @param StepToken  The token from OnStepAwaitingAcknowledgement. A stale
	 *                   token is ignored and logged rather than advancing the
	 *                   battle twice -- which is what a late animation callback
	 *                   arriving after a skip would otherwise do.
	 * @return True if this call actually advanced the flow.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Flow")
	bool AcknowledgeStep(int32 StepToken);

	/** AcknowledgeStep with whatever token is currently pending. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Flow")
	bool AcknowledgeCurrentStep();

	/**
	 * Acknowledge a Turn step and say how it actually went.
	 *
	 * The flow does not decide that a sleeping creature loses its turn -- the
	 * layer that performed the action does, and reports it here so OnTurnEnd
	 * carries the truth. AcknowledgeStep on a Turn step reports Completed.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Flow")
	bool AcknowledgeTurn(int32 StepToken, EGF_TurnResult Result = EGF_TurnResult::Completed);

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Flow")
	bool IsAwaitingAcknowledgement() const { return PendingStepKind != EGF_BattleStepKind::None; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Flow")
	EGF_BattleStepKind GetPendingStepKind() const { return PendingStepKind; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Flow")
	int32 GetPendingStepToken() const { return PendingStepToken; }

	//==================================================================================
	// ISSUING COMMANDS
	//==================================================================================

	/** The general form. The Submit* helpers below build the action for you. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	bool SubmitCommand(const FGF_BattleAction& Action);

	/**
	 * @param SkillIndex  0-3 into the creature's Skills array.
	 * @param PrimaryTarget  Ignored for spread shapes; the shape is read off the skill.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	bool SubmitSkillCommand(const FGF_BattleSlot& Slot, int32 SkillIndex, const FGF_BattleSlot& PrimaryTarget);

	/** Costs this creature its action for the round. That is the point of the rule. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	bool SubmitItemCommand(const FGF_BattleSlot& Slot, FName ItemName, const FGF_BattleSlot& TargetSlot);

	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	bool SubmitSwapCommand(const FGF_BattleSlot& Slot, int32 ReserveIndex);

	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	bool SubmitBraceCommand(const FGF_BattleSlot& Slot);

	/** Wild battles only. CoreItemName is the empty Core being spent. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	bool SubmitMarqueCommand(const FGF_BattleSlot& Slot, FName CoreItemName, const FGF_BattleSlot& TargetSlot);

	/** Wild battles only, and party-wide -- see CanFlee and GetFleeChance. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	bool SubmitFleeCommand(const FGF_BattleSlot& Slot);

	/**
	 * Back out of the last command and ask that seat again.
	 *
	 * At four slots the fourth order almost always depends on the first three,
	 * so this is a normal part of commanding a round, not an undo for mistakes.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	bool CancelLastCommand();

	/** Throw the whole round's orders away and start commanding from seat 0. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Commands")
	void CancelAllCommands();

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Commands")
	bool GetCommandForSlot(const FGF_BattleSlot& Slot, FGF_BattleAction& OutAction) const;

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Commands")
	TArray<FGF_BattleAction> GetIssuedCommands() const { return IssuedActions; }

	/** Seats still waiting for an order this round, in the order they will be asked. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Commands")
	TArray<FGF_BattleSlot> GetPendingCommandSlots() const { return CommandQueue; }

	//==================================================================================
	// WHOSE TURN IS IT
	//==================================================================================

	/**
	 * The seat currently acting. Invalid outside the Resolving phase.
	 *
	 * Pair with GetCommandingSlot for the Command phase, or use
	 * GetSpotlightSlot when you just want "who is the screen about".
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	FGF_BattleSlot GetCurrentSlot() const { return CurrentTurnContext.Slot; }

	/** The creature currently acting. Null outside the Resolving phase. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	AGF_Creature* GetCurrentCreature() const { return CurrentTurnContext.Creature; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	EGF_BattleSide GetCurrentSide() const { return CurrentTurnContext.Side; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	FGF_BattleAction GetCurrentAction() const { return CurrentTurnContext.Action; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	FGF_TurnContext GetCurrentTurnContext() const { return CurrentTurnContext; }

	/** The seat being asked for an order. Invalid outside the Command phase. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	FGF_BattleSlot GetCommandingSlot() const { return CommandingSlot; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	AGF_Creature* GetCommandingCreature() const;

	/**
	 * The creature the screen is about right now: the one being commanded during
	 * Command, the one acting during Resolving, null otherwise.
	 *
	 * This is the one to bind an on-screen "who is going" indicator to. Binding
	 * the other two means writing the same phase check in every widget.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	AGF_Creature* GetSpotlightCreature() const;

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	FGF_BattleSlot GetSpotlightSlot() const;

	/** Disambiguated name of the spotlight creature, ready to print. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Current")
	FText GetSpotlightName() const;

	//==================================================================================
	// READING THE BATTLE
	//==================================================================================

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	EGF_BattlePhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	int32 GetRoundNumber() const { return RoundNumber; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	EGF_BattleKind GetBattleKind() const { return BattleKind; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	EGF_BattleOutcome GetOutcome() const { return Outcome; }

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	UGF_BattleBoard* GetBoard() const { return Board; }

	/** The committed sequence. Empty until commands lock. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	TArray<FGF_TurnOrderEntry> GetTurnOrder() const { return TurnOrder; }

	/** Live prediction for the ribbon during the Command phase. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | State")
	TArray<FGF_TurnOrderEntry> BuildPreviewTurnOrder() const;

	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	FText GetDisplayNameForSlot(const FGF_BattleSlot& Slot) const;

	/** Actions in this round that have not resolved yet, cancelled ones excluded. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	int32 GetActionsRemainingThisRound() const;

	//==================================================================================
	// BOARD CHANGES DURING A BATTLE
	//==================================================================================

	/**
	 * Put a reserve into an empty seat. Legal during RoundEnd and Command.
	 *
	 * Reserves never auto-fill mid-round, by design -- a seat emptied on the
	 * second of eight actions stays empty until the round is over. The one
	 * exception is a side whose whole line is gone at the start of a round; see
	 * ForceFillEmptyLine.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Board")
	bool SendOutReserve(const FGF_BattleSlot& Slot, int32 ReserveIndex);

	/**
	 * Carry out a Swap order. Call this while the Swap turn is resolving, before
	 * acknowledging it -- the flow sequences the swap, it does not perform it.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework | Battle | Board")
	bool ExecuteSwap(const FGF_BattleSlot& Slot, int32 ReserveIndex);

	/**
	 * Advisory: is this creature unable to act right now -- asleep, frozen,
	 * flinched, fully paralysed, loafing?
	 *
	 * The flow does not call this. It is here so the layer performing the turn
	 * asks one question instead of five, and reports the answer back through
	 * AcknowledgeTurn.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | State")
	bool IsSlotIncapacitated(const FGF_BattleSlot& Slot) const;

	/**
	 * Can this side run at all right now?
	 *
	 * A wild battle, not a scripted no-run fight, and no living creature on the
	 * side held by a trapping skill. One trapped creature holds the whole side:
	 * running would leave it behind in a fight it cannot leave. Checked when the
	 * Flee is ordered, so a trapped party finds out before the turn is spent.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Flow")
	bool CanFlee(EGF_BattleSide FleeingSide) const;

	/**
	 * The odds a Flee would escape with if it were rolled now, 0..1.
	 *
	 * Certain when the fleeing side's fastest creature is at least as fast as the
	 * other side's. Otherwise FleeBaseOdds scaled by the speed ratio, plus
	 * FleeOddsPerFailedAttempt for each Flee already failed this battle -- so a
	 * slow party is never stuck for good, it just has to keep trying. With the
	 * defaults, a party at half the speed escapes at 25%, 50%, 75%, then always.
	 *
	 * 0 when CanFlee is false.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Flow")
	float GetFleeChance(EGF_BattleSide FleeingSide) const;

	/**
	 * Flees that did not get away this battle. Reset when a battle starts.
	 *
	 * One count, not one per side: only the player's side flees. The enemy AI
	 * never orders a Flee, and a tamer battle refuses one.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework | Battle | Flow")
	int32 GetFailedFleeAttempts() const { return FailedFleeAttempts; }

	/** Escape odds for a slower side before any failed attempts, scaled by the speed ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | Flee", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FleeBaseOdds = 0.5f;

	/** Added to the odds for every Flee that has already failed this battle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework | Battle | Flee", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FleeOddsPerFailedAttempt = 0.25f;

protected:
	virtual void BeginPlay() override;

	/**
	 * The enemy's order for a seat. Defers to UGF_BattleAILibrary.
	 *
	 * Virtual so a project can override one seat's behaviour -- a scripted boss
	 * that always opens the same way -- without replacing the whole AI.
	 */
	virtual FGF_BattleAction BuildDefaultEnemyAction(const FGF_BattleSlot& Slot) const;

private:
	//==================================================================================
	// FLOW STAGES
	//==================================================================================

	/**
	 * Runs queued continuations one at a time.
	 *
	 * Every stage below queues its successor instead of calling it. Without that,
	 * a listener that acknowledges synchronously inside OnTurnBegin would grow
	 * the call stack by one frame per turn and never unwind -- and a round of
	 * eight actions across a long battle is enough to matter.
	 */
	void Pump();
	void Queue(EGF_FlowStep Step);

	void BeginRound();
	void RequestNextCommand();
	void LockCommands();
	void AdvanceToNextTurn();
	void ScanAfterAction();
	void ContinueAfterAction();
	void BeginRoundEnd();
	void ScanAfterRoundEnd();
	void ContinueAfterRoundEnd();
	void FinishBattle();

	void SetPhase(EGF_BattlePhase NewPhase);
	void RaiseStep(EGF_BattleStepKind Kind);

	/**
	 * Raise a Downed step, remembering where to continue once it is acknowledged.
	 * Both the post-action scan and the post-round-end scan land here, and they
	 * resume at different places.
	 */
	void RaiseDownedStepThenGoTo(EGF_FlowStep Next);

	/** Broadcast newly downed creature and cancel their queued entries. Returns how many. */
	int32 ScanForNewlyDowned();

	/** Victory / Defeat / None, checked in that order. */
	EGF_BattleOutcome EvaluateOutcome() const;
	void EndBattle(EGF_BattleOutcome InOutcome);

	/** Send out one reserve per side whose whole line is empty, so a round always has actions. */
	void ForceFillEmptyLine(EGF_BattleSide Side);

	void RefreshTurnOrderEntryStates();
	void BroadcastTurnOrder();

	bool ValidateAndStoreCommand(const FGF_BattleAction& Action);

	/** Fill in the turn context for the entry at CurrentOrderIndex. */
	void BuildCurrentTurnContext();

	//==================================================================================
	// STATE
	//==================================================================================

	UPROPERTY()
	TObjectPtr<UGF_BattleBoard> Board = nullptr;

	UPROPERTY()
	TObjectPtr<UGF_BattleComponent> DamageComponent = nullptr;

	UPROPERTY()
	EGF_BattlePhase Phase = EGF_BattlePhase::Inactive;

	UPROPERTY()
	EGF_BattleKind BattleKind = EGF_BattleKind::Wild;

	UPROPERTY()
	EGF_BattleOutcome Outcome = EGF_BattleOutcome::None;

	UPROPERTY()
	int32 RoundNumber = 0;

	UPROPERTY()
	bool bEscapeBlocked = false;

	/** Orders issued this round, in the order they were issued (so cancel pops the last). */
	UPROPERTY()
	TArray<FGF_BattleAction> IssuedActions;

	/** Seats still to be asked, front first. */
	UPROPERTY()
	TArray<FGF_BattleSlot> CommandQueue;

	UPROPERTY()
	FGF_BattleSlot CommandingSlot;

	UPROPERTY()
	TArray<FGF_TurnOrderEntry> TurnOrder;

	/**
	 * This round's actions in resolution order.
	 *
	 * Index-aligned with TurnOrder on purpose: entry N of the ribbon and action
	 * N of the round are the same turn, so nothing has to look an action up by
	 * slot while the board is changing underneath it.
	 */
	UPROPERTY()
	TArray<FGF_BattleAction> ResolvedActions;

	UPROPERTY()
	int32 CurrentOrderIndex = INDEX_NONE;

	UPROPERTY()
	FGF_TurnContext CurrentTurnContext;

	/**
	 * Creature already announced as downed, so each one is announced once.
	 *
	 * Keyed by creature rather than by seat: a seat can be refilled by a swap,
	 * and a seat-keyed list would either re-announce the newcomer or swallow its
	 * death later in the same battle.
	 */
	UPROPERTY()
	TArray<TObjectPtr<AGF_Creature>> ReportedDownCreatures;

	/** Set when a Flee succeeds, consumed at the next outcome check. */
	UPROPERTY()
	EGF_BattleOutcome PendingOutcome = EGF_BattleOutcome::None;

	UPROPERTY()
	int32 FailedFleeAttempts = 0;

	/**
	 * This turn's Flee verdict. Rolled in AdvanceToNextTurn, before the turn is
	 * raised, then read twice: by PlanCurrentTurn for the message, and at the
	 * acknowledgement for the ending. One roll, two readers.
	 */
	bool bCurrentFleeRolled = false;
	bool bCurrentFleeEscaped = false;
	float CurrentFleeChance = 0.0f;
	FGF_BattleSlot CurrentFleeBlockerSlot;

	/** The living seat with the highest effective Speed. False when the side is empty. */
	bool FindFastestLivingSlot(EGF_BattleSide Side, FGF_BattleSlot& OutSlot, float& OutSpeed) const;

	void RollFleeForCurrentTurn();

	EGF_BattleStepKind PendingStepKind = EGF_BattleStepKind::None;
	int32 PendingStepToken = 0;
	int32 NextStepToken = 1;

	EGF_FlowStep QueuedStep = EGF_FlowStep::None;
	EGF_FlowStep DownedReturnStep = EGF_FlowStep::None;
	bool bPumping = false;
};
