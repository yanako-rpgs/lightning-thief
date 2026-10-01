// Gamma Framework -- the actor that drives a battle.
//
// Not to be confused with AGF_BattleManager beside it, which is the older
// single-battle port. This one sits on the 4v4 turn engine in Turn/.
//
// Owns a UGF_BattleFlowComponent and answers the four events that carry a
// battle from start to outcome. Wild and tamer encounters run through the same
// code path; what differs is the AI profile and whether fleeing is allowed.
//
// The enemy line drives itself. UGF_BattleFlowComponent::bAutoIssueEnemyCommands
// defaults to true, so orders for every enemy seat are filled from EnemyAIProfile
// at lock-in. Only the player side is wired here.
//
// HARNESS MODE
//   bAutoPlayerCommands and bAutoAcknowledge exist so a battle runs end to end
//   with no menus, no animation and no content. That is the state the project is
//   in right now, and it is the only configuration in which the damage pipeline
//   can be verified without a presentation layer that might itself be the bug.
//
//   Turn them off one at a time as the real UI arrives.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Turn/GF_BattleTypes.h"
#include "Turn/GF_BattleAI.h"
// CachedPlan is stored by value and PeekPlan returns one, so the complete type
// is required here -- a forward declaration is not enough.
#include "Turn/GF_BattleResolver.h"
// FGF_CatchResult is broadcast by value on OnClaimResolved, so the complete type
// is required here too.
#include "GF_CatchingLibrary.h"
#include "GF_TurnBattleManager.generated.h"

class UGF_BattleFlowComponent;
class AGF_Creature;
class AGF_Skill;
class UGF_CreatureSpeciesData;

// Declared here rather than reusing FGF_OnBattleEnded, which lives inside
// GF_BattleFlowComponent.h -- and that header is only forward-declared above.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBattleFinished, EGF_BattleOutcome, Outcome);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGF_OnClaimResolved, FGF_CatchResult, Result, FName, CoreName, AGF_Creature*, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGF_OnFleeResolved, bool, bEscaped, float, Chance, AGF_Creature*, Blocker);

UCLASS()
class GAMMAFRAMEWORKBATTLE_API AGF_TurnBattleManager : public AActor
{
	GENERATED_BODY()

public:
	AGF_TurnBattleManager();

	//==================================================================
	// STARTING A BATTLE
	//==================================================================

	/**
	 * Wild encounter. No reserves on the enemy side, fleeing allowed, and the
	 * AI plays badly on purpose -- a wild animal is not running a strategy.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool StartWildBattle(const TArray<AGF_Creature*>& PlayerActive,
	                     const TArray<AGF_Creature*>& PlayerReserves,
	                     const TArray<AGF_Creature*>& WildCreatures);

	/**
	 * Tamer battle. Enemy gets a bench, fleeing is blocked, and the AI is told
	 * how well to play. Difficulty picks how it thinks; MistakeChance picks how
	 * often it throws a good decision away -- that second number is the one to
	 * tune per encounter.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool StartTamerBattle(const TArray<AGF_Creature*>& PlayerActive,
	                      const TArray<AGF_Creature*>& PlayerReserves,
	                      const TArray<AGF_Creature*>& EnemyActive,
	                      const TArray<AGF_Creature*>& EnemyReserves,
	                      EGF_BattleAIDifficulty Difficulty = EGF_BattleAIDifficulty::Smart,
	                      float MistakeChance = 0.15f);

	/** Escape hatch for anything the two helpers above do not cover. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool StartBattleWithConfig(const FGF_BattleStartConfig& Config);

	//==================================================================
	// HARNESS SWITCHES
	//==================================================================

	/** Answer every player command with the first skill at the first living enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle|Harness")
	bool bAutoPlayerCommands = true;

	/**
	 * Resolve and acknowledge TURN steps the frame they arrive.
	 *
	 * Turn this off to pace the fight yourself: a Turn then raises
	 * OnTurnReadyToPresent and waits for FinishTurnPresentation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle|Harness")
	bool bAutoAcknowledge = true;

	/**
	 * Acknowledge the housekeeping steps -- Intro, Downed, RoundEnd,
	 * Replacement, Outcome -- as they arrive.
	 *
	 * Separate from bAutoAcknowledge, and on by default, because turning off
	 * turn pacing should not also make you hand-acknowledge every step in the
	 * battle. The first step a battle raises is Intro, before any turn exists,
	 * so one flag governing both meant the fight froze before it started.
	 *
	 * Turn this off only when you want send-out animations and round-end
	 * messages under your own control; then OnStepReadyToPresent fires and
	 * something must call AcknowledgePresentedStep.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle|Harness")
	bool bAutoAcknowledgeOtherSteps = true;

	/** Auto-fill emptied player seats from the bench, lowest index first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle|Harness")
	bool bAutoReplace = true;

	//==================================================================
	// PRESENTATION
	//
	// With bAutoAcknowledge off, a Turn step raises OnTurnReadyToPresent and
	// then stops. Nothing advances until the Blueprint calls back, so the round
	// runs at exactly the speed of the animations rather than on a timer that
	// hopes they finished.
	//
	//   OnTurnReadyToPresent (Plan)
	//        |  play the attack -- FCTween, flipbook, camera, as long as it takes
	//   ApplyPlannedTurn()          HP moves, OnHealthChanged fires, bars tween
	//        |  FCTween the bars, wait on its Completed pin
	//   FinishTurnPresentation()    the round moves on
	//
	// The plan is handed over before anything is applied, so damage numbers and
	// bar targets are known before a single frame plays.
	//==================================================================

	/**
	 * A turn is planned and waiting to be shown. Raised only when
	 * bAutoAcknowledge is false.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Battle")
	void OnTurnReadyToPresent(const FGF_ActionResolution& Plan);

	/**
	 * A turn is about to play. Raised at the START of TurnStartDelay, before
	 * anything moves.
	 *
	 * This is where "Duskmaw used Gnash!" goes. The plan is already final, so
	 * the message can name the attacker and the move -- ActorDisplayName and
	 * SkillClass are both on it -- and the player gets the whole delay to read
	 * it before the first frame plays.
	 *
	 * Raised for every turn, including the enemy's, and regardless of whether
	 * the skill has its own attack graph.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Battle")
	void OnTurnAnnounced(const FGF_ActionResolution& Plan);

	/**
	 * One seat's outcome, at the moment damage is applied. Raised once per
	 * target, so a spread move raises it four times.
	 *
	 * Everything a floating damage number needs, flattened out of the plan --
	 * the effectiveness flags live on the resolution rather than on the
	 * creature, so OnDamageTaken cannot colour a number by itself.
	 *
	 * Amount is what was actually subtracted, after the spread discount and
	 * Brace. Showing the formula's raw damage instead would disagree with the
	 * health bar on any move that hits more than one seat.
	 *
	 * Check bImmune before bNotEffective: an immunity is zero damage, and
	 * reading it as "not very effective" is the wrong message entirely.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Battle")
	void OnDamageResolved(AGF_Creature* Target, float Amount, bool bWasCritical,
	                      bool bSuperEffective, bool bNotEffective,
	                      bool bImmune, bool bMissed);

	/**
	 * The turn is completely over and the round is about to advance.
	 *
	 * Pairs with OnTurnAnnounced, and like it, fires for EVERY turn -- enemy
	 * turns, and moves whose skill brought its own attack graph. Anything that
	 * is switched on for the duration of a turn and off again afterwards --
	 * attacker and target rings, a spotlight, a message panel -- belongs on
	 * these two rather than on OnTurnReadyToPresent, which is skipped whenever
	 * a skill presents itself.
	 */
	/**
	 * The action has been applied, and the plan now says what actually
	 * happened rather than what was intended.
	 *
	 * Whether a status landed and whether a creature went down are decided
	 * during application, so at announce time they are still false. Result
	 * lines and any reaction to them belong here.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Battle")
	void OnActionResolved(const FGF_ActionResolution& Plan);

	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Battle")
	void OnTurnPresentationFinished();

	/**
	 * End of round: burn, poison, weather, traps, held items and trait heals.
	 *
	 * One entry per creature that something happened to, each already carrying
	 * its own worded Messages -- "Duskmaw is hurt by its burn!" and the like.
	 * Nothing else reports these, so without handling this event a burn drains
	 * HP with no explanation on screen.
	 *
	 * Raised after the ticks are applied. Take a presentation hold here, or in
	 * OnDamageTaken, if the round should wait for them to be read.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Battle")
	void OnRoundEndTicksResolved(const TArray<FGF_RoundEndTick>& Ticks);

	/**
	 * A player seat needs an order. Raised only when bAutoPlayerCommands is
	 * false, once per living seat, in slot order.
	 *
	 * Open the action menu here and return -- the flow waits until something
	 * calls one of the Submit* functions on the flow component. CancelLastCommand
	 * backs out to re-issue an earlier seat, which matters at four seats because
	 * the fourth order always depends on the first three.
	 *
	 * The creature is handed over so a menu does not have to resolve the slot
	 * itself just to read a skill list.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Battle")
	void OnPlayerCommandRequested(const FGF_BattleSlot& Slot, AGF_Creature* Creature);

	/**
	 * Commit the planned turn. HP moves here, which is what starts every health
	 * bar tween on every creature the action touched.
	 *
	 * Call once, after the attack animation and before the bar tween.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool ApplyPlannedTurn();

	/**
	 * Presentation is over -- acknowledge the turn and let the round advance.
	 *
	 * Wire this to the Completed pin of the last tween. One call regardless of
	 * how many seats were hit: a spread skill tweens four bars in parallel, so
	 * the wait is the longest tween rather than the sum of four.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool FinishTurnPresentation();

	/**
	 * Let a skill with its own attack graph perform itself.
	 *
	 * When a move is a GF_Skill that implements OnPlayAttack, the manager
	 * spawns it, hands it the attacker and targets, and wires its impact and
	 * finished callbacks to ApplyPlannedTurn and FinishTurnPresentation. The
	 * Blueprint's OnTurnReadyToPresent is NOT raised for those moves -- the
	 * attack graph is presenting instead.
	 *
	 * Every other move still raises OnTurnReadyToPresent as before, so moves
	 * convert to graphs one at a time with nothing else changing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle|Harness")
	bool bAutoPlayAttackGraphs = true;

	/**
	 * Pause before a turn starts playing, in seconds.
	 *
	 * Without it an attack begins on the same frame the button is clicked,
	 * which reads as the menu and the attack happening at once. A short beat
	 * lets the menu close and the eye settle on the field before anything
	 * moves.
	 *
	 * Applies to every turn and every move, so pacing is one number here rather
	 * than a Delay repeated at the top of each attack graph. Zero disables it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle|Presentation",
	          meta = (ClampMin = "0.0", UIMax = "2.0"))
	float TurnStartDelay = 1.0f;

	//==================================================================
	// COMMAND MENU
	//
	// Everything a battle menu needs, on the one object it already holds a
	// reference to. A widget that has to reach the flow, the board and the
	// creature separately ends up holding three references it did not ask for,
	// any of which can go stale between rounds.
	//==================================================================

	/**
	 * Shortest gap between two accepted menu actions, in seconds.
	 *
	 * Spamming Accept cannot double-issue an order -- the flow refuses a command
	 * for a seat that is not waiting -- but it can create and destroy menus
	 * faster than their animations, which is what actually breaks: focus lands
	 * on a widget that is already leaving, and menus flicker in and out.
	 *
	 * Enforced here rather than in the widgets because every menu action already
	 * passes through this class, and a guard a widget has to remember is a guard
	 * that gets forgotten on the twentieth menu.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle|Menu",
	          meta = (ClampMin = "0.0", UIMax = "1.0"))
	float MenuInputCooldown = 0.2f;

	/**
	 * Would a menu action be accepted right now?
	 *
	 * For grey-ing out a button while it would be refused, so the player sees a
	 * disabled control rather than a press that silently does nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	bool IsMenuInputAllowed() const;

	/**
	 * Refuse menu actions for this long.
	 *
	 * Call it with an animation's length when a menu starts playing one, so the
	 * block lasts exactly as long as the thing it is protecting rather than a
	 * number someone guessed.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Menu")
	void BlockMenuInput(float Seconds);

	/** The seat currently being given an order. Invalid outside the Command phase. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	FGF_BattleSlot GetCommandingSlot() const;

	/** The creature currently being given an order -- for a spotlight or a ring. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	AGF_Creature* GetCommandingCreature() const;

	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	AGF_Creature* GetCreatureInSlot(const FGF_BattleSlot& Slot) const;

	/**
	 * Seats this skill lets the player choose between.
	 *
	 * Empty when there is no choice to make: a Self skill targets its user, a
	 * spread skill reaches everything, and a Single skill with one enemy left
	 * has only one answer. Show a picker only when this returns more than one.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	TArray<FGF_BattleSlot> GetSelectableTargets(int32 SkillIndex) const;

	/** True when GetSelectableTargets would offer a real decision. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	bool NeedsTargetSelection(int32 SkillIndex) const;

	/**
	 * Seats a claim can be spent on -- every enemy still standing.
	 *
	 * The claim counterpart to GetSelectableTargets, so the target menu fills
	 * itself the same way whether it was opened by a skill or by a Core. Always
	 * enemies: a Core is never spent on your own line.
	 *
	 * Empty means the battle is over or the side is already cleared.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	TArray<FGF_BattleSlot> GetClaimableTargets() const;

	/**
	 * Issue a skill for the commanding seat.
	 *
	 * TargetSlot is ignored for shapes that do not take one, so a menu can pass
	 * whatever it has without checking the shape first.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Menu")
	// By value, not by const reference: Blueprint refuses to leave a by-ref
	// struct pin unconnected, and "no target chosen" is the common case -- a
	// 1v1, a self-buff, a spread move. By value the pin can simply be left
	// empty, and an index of INDEX_NONE means the manager picks.
	bool SubmitSkill(int32 SkillIndex, FGF_BattleSlot TargetSlot);

	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Menu")
	bool SubmitBrace();

	/**
	 * Run, as the whole side's turn.
	 *
	 * Same shape as SubmitClaim: orders already given this round are taken back
	 * and every other seat stands down, so the player is either running or
	 * fighting and never both. Refused without touching those orders when the
	 * side cannot run -- a tamer battle, or a trapped creature -- so the menu
	 * that asked stays open.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Menu")
	bool SubmitFlee();

	/** Would SubmitFlee be accepted right now? For greying the button out. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	bool CanFlee() const;

	/**
	 * Is there an earlier seat to go back to?
	 *
	 * False on the first seat of a round -- there is nothing behind it -- so a
	 * Back button can hide itself rather than refusing when pressed.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Menu")
	bool CanGoBack() const;

	/**
	 * Take back the previous seat's order and ask for it again.
	 *
	 * The command request for that seat is raised again, so a menu built on
	 * OnPlayerCommandRequested reopens on its own with no extra wiring.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Menu")
	bool GoBack();

	/**
	 * Hold the turn open until something finishes showing.
	 *
	 * The attack graph owns the main beat, but things that happen DURING the
	 * hit -- a stat arrow, a status icon, a message -- are raised from the
	 * creature and have no way to reach back into that graph. Each one takes a
	 * hold and releases it when done; the turn ends when the attack has
	 * finished AND every hold is released, whichever comes last.
	 *
	 * Holds are counted, so overlapping effects are safe. Always pair them: a
	 * hold that is never released stalls the battle.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Presentation")
	void AddPresentationHold();

	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Presentation")
	void ReleasePresentationHold();

	/** Outstanding holds. Zero means nothing is keeping the turn open. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle|Presentation")
	int32 GetPresentationHolds() const { return PresentationHolds; }

	/**
	 * Take and release a hold without a manager reference.
	 *
	 * For Blueprints the framework raises directly -- the creature especially --
	 * which have no manager pin to hand. Finds the battle in the level and does
	 * nothing outside one, so a creature can call these anywhere safely.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Presentation",
	          meta = (WorldContext = "WorldContextObject"))
	static void HoldTurn(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle|Presentation",
	          meta = (WorldContext = "WorldContextObject"))
	static void ReleaseTurn(const UObject* WorldContextObject);

	/**
	 * A non-turn step is waiting: Intro, Downed, RoundEnd, Replacement or
	 * Outcome. Raised only when bAutoAcknowledgeOtherSteps is false.
	 *
	 * RoundEnd is the one with a rule: call ResolveRoundEndTicks on the flow
	 * before acknowledging, or burn, poison, weather and held items never tick.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GammaFramework|Battle")
	void OnStepReadyToPresent(EGF_BattleStepKind Kind, int32 Token);

	/** Acknowledge the step handed to you by OnStepReadyToPresent. */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool AcknowledgePresentedStep(int32 Token);

	//==================================================================
	// DRIVING IT YOURSELF
	//==================================================================

	/**
	 * Plan, apply and acknowledge the pending Turn step in one call.
	 *
	 * This is what bAutoAcknowledge does internally. Call it from your own
	 * presentation code once the animation has finished, having taken the plan
	 * from PeekPlan() beforehand.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool ResolvePendingTurn();

	/**
	 * Plan the pending turn WITHOUT applying it, so you can drive health bars
	 * toward numbers that are already final.
	 *
	 * Plans exactly once and caches. Calling PlanCurrentTurn yourself as well
	 * would roll accuracy and crits a second time and give you a different
	 * answer than the one that gets applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	FGF_ActionResolution PeekPlan();

	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle")
	UGF_BattleFlowComponent* GetFlow() const { return Flow; }

	/**
	 * Claim a wild creature with this Core.
	 *
	 * The acting seat is never passed in -- it is whichever seat the flow is
	 * currently asking for an order, so a bag widget cannot pass the wrong one.
	 *
	 * TargetSlot is optional. Leave the pin alone and the first living enemy is
	 * claimed, which is the only answer there is in a one-enemy wild encounter.
	 * Connect a seat -- from a target menu, once wild fights have more than one
	 * Vorn in them -- and that seat is claimed instead.
	 *
	 * Returns false if there is no command pending, the target is empty or not an
	 * enemy, or the battle is against a Tamer. Nothing is spent on a false return:
	 * the Core is only consumed once the claim actually resolves.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool SubmitClaim(FName CoreItemName, FGF_BattleSlot TargetSlot);

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Battle")
	FGF_OnBattleFinished OnBattleFinished;

	/**
	 * Hold the round open on a claim until FinishClaimPresentation is called.
	 *
	 * Off by default, so a claim resolves in one frame and the log tells you what
	 * happened. Turn it on once something is listening to OnClaimResolved, or the
	 * round stops dead waiting for a presentation nobody plays.
	 *
	 * The flow never advances on a timer -- it stops and waits to be told the
	 * presentation finished -- so this is how a throw, three wobbles and a seal
	 * get their time without the engine running ahead of them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle")
	bool bHoldRoundForClaimPresentation = false;

	/**
	 * End the fight the moment anything is claimed, even with enemies still up.
	 *
	 * Off by default, because claiming two out of a pack of four is one of the
	 * few things a four-slot board offers that a duel cannot, and a player who
	 * wants out already has Flee.
	 *
	 * Turn it on for an encounter that exists to offer one specific Vorn, where
	 * the rest of the line is scenery and mopping it up is busywork. Set from
	 * the encounter, not globally -- see FGF_EncounterRequest::bEndOnFirstClaim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle")
	bool bEndOnFirstClaim = false;

	/**
	 * The claim's animation is done -- finish the turn.
	 *
	 * Only does anything while a claim is being held. Safe to call unconditionally
	 * at the end of a sequence, and safe to call twice.
	 *
	 * This is where a sealed claim takes the Vorn off the board and ends the
	 * battle, so a listener that never calls it leaves the creature standing.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	void FinishClaimPresentation();

	/**
	 * A claim resolved. The catch has already happened -- this is the replay.
	 *
	 * Result carries the whole verdict: bCaught, and ShakeCount as the number of
	 * wobbles to play before the click or the breakout. Nothing here decides the
	 * outcome, so a listener can take as long as it likes; the turn is already
	 * acknowledged by the time this fires.
	 *
	 * Target may be null if the Vorn left the field between command and resolve.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Battle")
	FGF_OnClaimResolved OnClaimResolved;

	/**
	 * A Flee's turn has begun and its verdict is known.
	 *
	 * Fires before the turn is announced, so escape or stumble effects can start
	 * with it. The messages do not need this: "You ran away!" and the blocked
	 * line arrive through OnActionResolved like every other action's text.
	 *
	 * Blocker is the other side's fastest creature, null on an escape.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Battle")
	FGF_OnFleeResolved OnFleeResolved;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GammaFramework|Battle")
	TObjectPtr<UGF_BattleFlowComponent> Flow;

	// Dynamic delegates require UFUNCTION. Without it these compile clean and
	// then silently never fire, which is the single most expensive way to lose
	// an afternoon on this engine.
	UFUNCTION() void HandleCommandRequested(FGF_BattleSlot Slot);
	UFUNCTION() void HandleStepAwaitingAck(EGF_BattleStepKind Kind, int32 Token);
	UFUNCTION() void HandleReplacementNeeded(EGF_BattleSide Side, const TArray<FGF_BattleSlot>& EmptySlots);
	UFUNCTION() void HandleBattleEnded(EGF_BattleOutcome Outcome);
	UFUNCTION() void HandlePhaseChanged(EGF_BattlePhase OldPhase, EGF_BattlePhase NewPhase);

private:
	void BindFlowEvents();
	void ConfigureCommonAI(FGF_BattleStartConfig& Config);

	/** First living enemy seat, or an invalid slot when the side is wiped. */
	bool FindFirstLivingEnemy(FGF_BattleSlot& OutSlot) const;

	/**
	 * If the pending turn is a Marque, run the claim and acknowledge it here.
	 *
	 * Returns true when it handled the turn, in which case the caller must not
	 * continue into the skill path -- a claim has no plan, no attack graph and
	 * no damage, so PresentPendingTurn would hand the Blueprint an empty
	 * resolution and wait forever for a presentation that never comes.
	 *
	 * The catch resolves completely inside this call. OnClaimResolved is the
	 * replay hook; it is deliberately fired after the acknowledgement so a
	 * listener cannot wedge the round by never finishing.
	 */
	bool TryResolveClaim(int32 Token);

	/**
	 * Every living player seat except Acting passes this round. What makes a
	 * claim or a Flee the whole side's turn. Returns how many stood down.
	 */
	int32 StandDownOtherSeats(const FGF_BattleSlot& Acting);

	/** Applies a resolved claim's outcome: clears the seat, ends or continues. */
	void ApplyClaimOutcome();

	/**
	 * True while a claim is filling every player seat in one burst.
	 *
	 * The flow pumps synchronously: every Submit* and CancelAllCommands asks for
	 * the next command before returning. A claim issues five orders back to back,
	 * so without this the UI is asked for a menu four times mid-burst and the last
	 * one is left on screen with nothing to dismiss it.
	 *
	 * Every seat has an order by the time this clears, so no request is lost --
	 * only the ones that would have been answered a microsecond later anyway.
	 */
	bool bSuppressCommandRequests = false;

	// A claim that is waiting on its presentation. Token is 0 when none is held.
	int32 HeldClaimToken = 0;
	bool  bHeldClaimSealed = false;
	FGF_BattleSlot HeldClaimSlot;

	// PlanCurrentTurn rolls accuracy and crits, so it must run once per turn.
	// Cached here between PeekPlan and ResolvePendingTurn.
	FGF_ActionResolution CachedPlan;
	int32 CachedPlanToken = INDEX_NONE;
	bool  bHasCachedPlan  = false;

	// Guards double-apply: ApplyPlannedTurn subtracts damage, so running it twice
	// on one turn would hit for double.
	bool  bAppliedThisTurn = false;

	// Presentation holds taken by effects that outlive the attack graph.
	int32 PresentationHolds = 0;

	// FinishTurnPresentation was called while holds were outstanding, so the
	// turn is waiting on the last release rather than on the attack.
	bool  bFinishDeferred = false;

	// A housekeeping step -- RoundEnd -- held open while its damage plays out.
	// Acknowledged by the last hold release instead of immediately.
	int32 DeferredStepToken = INDEX_NONE;

	// Round-end ticks are shown one creature at a time; these track how far
	// through the board that sequence is.
	// When menu actions become acceptable again. Compared against the world's
	// unpaused time, so a paused battle does not tick the block away.
	double MenuInputAllowedAt = 0.0;

	bool ConsumeMenuInput();

	TArray<FGF_BattleSlot> PendingRoundEndSlots;
	int32 RoundEndIndex = 0;

	void AdvanceRoundEnd();

	// The real acknowledgement, run once the attack has finished and no holds
	// remain. Split out so both paths end in exactly the same place.
	bool CompleteTurnPresentation();

	// Returns true when the skill performed itself, meaning the Blueprint
	// presentation path should be skipped for this turn.
	bool TryPlayAttackGraph(const FGF_ActionResolution& Plan);

	// Runs after TurnStartDelay, or immediately when that is zero.
	void PresentPendingTurn();

	FTimerHandle TurnStartTimer;

	UFUNCTION() void HandleAttackImpact(AGF_Skill* Skill);
	UFUNCTION() void HandleAttackFinished(AGF_Skill* Skill);
};
