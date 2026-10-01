// Gamma Framework -- taking the game from the overworld into a battle and back.
//
// The arena is streamed in as a level instance high above the overworld rather
// than loaded as its own map. The overworld therefore stays exactly where it
// was: no saving the player's position, no restoring the world afterwards, and
// no load screen either side of a random encounter.
//
// The player keeps the same pawn and controller throughout. Only the view
// target changes, which is also the transition -- a blend to the arena camera
// IS the intro sweep, and there is no pawn state to put back afterwards.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Turn/GF_BattleTypes.h"
#include "Turn/GF_BattleAI.h"
#include "GF_BattleLauncher.generated.h"

class AGF_BattleArena;
class AGF_TurnBattleManager;
class AGF_Creature;
class UGF_CreatureSpeciesData;


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBattleLaunchFinished, EGF_BattleOutcome, Outcome);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnArenaStaged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGF_OnSendOutCreature, AGF_Creature*, Creature, EGF_BattleSide, Side, int32, SeatIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnBattleConcluded, EGF_BattleOutcome, Outcome);

/**
 * One creature to put on the field, as an encounter describes it rather than
 * as a spawned actor.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_EncounterEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter")
	TObjectPtr<UGF_CreatureSpeciesData> Species = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter",
	          meta = (ClampMin = "1", ClampMax = "100"))
	int32 Level = 5;
};

USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKBATTLE_API FGF_EncounterRequest
{
	GENERATED_BODY()

	/**
	 * The arena to fight in, by streaming level name.
	 *
	 * The level has to be a sub-level of the persistent map, which is the point:
	 * it can be positioned and dressed in the editor against the overworld it
	 * sits above, rather than only existing once the game is running.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter")
	FName ArenaLevelName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter")
	TArray<FGF_EncounterEntry> Enemies;

	/** Wild creatures can be claimed and fled from; a Tamer's cannot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter")
	bool bIsWild = true;

	/**
	 * End the fight as soon as anything is claimed, even with enemies still up.
	 *
	 * For an encounter that exists to offer one particular Vorn, where the rest
	 * of the line is scenery. Leave it off for ordinary packs: claiming two out
	 * of four is worth having, and a player who wants out can Flee.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter")
	bool bEndOnFirstClaim = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter")
	EGF_BattleAIDifficulty Difficulty = EGF_BattleAIDifficulty::Basic;

	/**
	 * Hold the arena open when the fight ends, rather than tearing it down.
	 *
	 * For a victory screen, a level-up sequence, spoils. Without it the camera
	 * blends back and the arena unloads the moment the last creature falls, and
	 * there is nothing left to show anything over.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter")
	bool bWaitOnOutcome = false;

	/**
	 * Stage the arena and stop, rather than starting the fight straight away.
	 *
	 * For a covered-screen transition: the scene is built behind the cover, and
	 * the fight waits for BeginStagedBattle once the reveal has played. Leave
	 * false and the battle starts the moment the arena is ready.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Encounter")
	bool bWaitForReveal = false;
};

UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_BattleLauncher : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Stream in an arena, put both sides on the field and start the fight.
	 *
	 * Returns false only if the request itself is unusable. A battle that is
	 * accepted reports through OnBattleLaunchFinished, including when the arena
	 * fails to load -- so a caller has one place to wait on.
	 */
	/**
	 * Fight this encounter with the player's party.
	 *
	 * The normal way in. The party is stored as data rather than actors, so the
	 * launcher spawns the first few able creatures into the arena's seats and
	 * writes what happened to them back afterwards -- damage taken, status,
	 * levels gained. Nothing to pass and nothing to keep in sync.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool StartEncounter(const FGF_EncounterRequest& Request);

	/**
	 * Fight with specific creatures instead of the party.
	 *
	 * For tests and set pieces. These actors are used as they are and left alone
	 * afterwards -- not written back to the party, not destroyed.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool StartEncounterWith(const FGF_EncounterRequest& Request,
	                        const TArray<AGF_Creature*>& PlayerActive,
	                        const TArray<AGF_Creature*>& PlayerReserves);

	/**
	 * Take game input away from the overworld pawn for the duration.
	 *
	 * Without this the pawn keeps its bindings while the battle runs, so the
	 * same stick or WASD that moves a menu cursor also walks the character
	 * around a field nobody is looking at.
	 *
	 * Possessing a battle pawn would not have fixed this on its own -- it is
	 * the input mode that matters, not who owns the pawn -- and this way there
	 * is no pawn state to restore afterwards.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle")
	bool bTakeInputDuringBattle = true;

	/**
	 * How many of the party stand on the field at once. The rest are the bench.
	 *
	 * Four, to match the board the turn engine builds. Fewer leaves empty seats
	 * that auto-replace fills from the bench on the following round, so a battle
	 * that starts as a 1v1 quietly becomes a 4v4 a round later.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle",
	          meta = (ClampMin = "1", ClampMax = "4"))
	int32 ActiveSeats = 4;

	/**
	 * Bring creatures onto the field one at a time rather than all at once.
	 *
	 * Off by default, and deliberately so: it hides every creature until
	 * OnSendOutCreature reveals it, so turning it on before anything handles
	 * that event makes the whole field invisible. Opt in once the release is
	 * built.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GammaFramework|Battle")
	bool bStaggerSendOut = false;

	/** True from the moment an encounter is accepted until the arena is unloaded. */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle")
	bool IsInBattle() const { return bInBattle; }

	/**
	 * The actor driving the current fight, or null when none is running.
	 *
	 * Ask for it here rather than searching the level for one. The manager is
	 * spawned into the streamed arena, so a Get Actor Of Class is timing
	 * dependent: it finds nothing before the arena has loaded, nothing after it
	 * unloads, and a stale one from the previous fight in between. The launcher
	 * owns the actor and knows when it is valid.
	 *
	 * The launcher itself is a world subsystem, so this is reachable from any
	 * widget without keeping a reference to anything.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle")
	AGF_TurnBattleManager* GetBattleManager() const { return Manager; }

	/**
	 * Party indices of the creatures standing on the player's line right now.
	 *
	 * What an EXP award means by "battlers". AwardEXPFromBattle uses this set
	 * twice: membership decides who takes a full share, and Num() is the divisor
	 * for that share -- so a hardcoded list is wrong in both directions at once.
	 * A literal [0,1,2] on a 4v4 leaves the fourth seat earning the bench rate,
	 * or nothing at all with EXP Share off, while splitting the yield three ways
	 * among the rest and handing out more EXP than the kill was worth.
	 *
	 * Living seats only. A fainted creature is skipped by the payout anyway, and
	 * counting it would shrink everyone else's share to pay a share to nobody.
	 *
	 * Read fresh each time rather than recorded at battle start, so a reserve
	 * swapped in is a battler from the moment it is standing there, and the one
	 * it replaced stops being one.
	 *
	 * Empty if the board cannot be reached, which is not a battle state -- the
	 * caller should not be awarding EXP then.
	 */
	UFUNCTION(BlueprintPure, Category = "GammaFramework|Battle")
	TArray<int32> GetBattlerPartyIndices() const;

	/**
	 * Start a fight that was staged and held. Does nothing unless one is
	 * waiting, so a reveal animation can call it without checking first.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool BeginStagedBattle();

	/**
	 * This creature has finished arriving. Sends out the next, or starts the
	 * fight if that was the last.
	 *
	 * Does nothing when no send-out is in progress, so a release animation can
	 * end with this call unconditionally.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	void NotifySendOutFinished();

	/**
	 * Done looking at the outcome -- take the arena down.
	 *
	 * Does nothing unless a fight is being held, so a victory screen can close
	 * with this call whether or not the encounter asked to wait.
	 */
	UFUNCTION(BlueprintCallable, Category = "GammaFramework|Battle")
	bool DismissOutcome();

	/**
	 * The arena is loaded, both lines are in their seats and the camera is on
	 * it -- but nothing has started yet.
	 *
	 * This is where a screen wipe reveals the battle. The load is asynchronous,
	 * so there is no fixed time to guess at: cover the screen, start the
	 * encounter, and wait for this.
	 *
	 * With bWaitForReveal set, the fight does not begin until BeginStagedBattle
	 * is called, so the reveal can take as long as it likes.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Battle")
	FGF_OnArenaStaged OnArenaStaged;

	/**
	 * Send one creature out. Raised once per creature, in order, before the
	 * fight begins.
	 *
	 * The creature arrives hidden and it is this event's job to reveal it --
	 * throw the Core, play the Call animation, whatever the release looks like.
	 * Call NotifySendOutFinished when that is done and the next one follows.
	 *
	 * Enemies first, then the player's line, so a wild creature is on the field
	 * to be answered rather than appearing alongside.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Battle")
	FGF_OnSendOutCreature OnSendOutCreature;

	/**
	 * The fight is decided, and the arena is still standing.
	 *
	 * Raised only with bWaitOnOutcome. The camera has not moved and nothing has
	 * been unloaded, so a victory screen has something to sit in front of. Call
	 * DismissOutcome when it is done.
	 */
	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Battle")
	FGF_OnBattleConcluded OnBattleConcluded;

	UPROPERTY(BlueprintAssignable, Category = "GammaFramework|Battle")
	FGF_OnBattleLaunchFinished OnBattleLaunchFinished;

private:
	UFUNCTION() void HandleArenaLoaded();
	UFUNCTION() void HandleArenaUnloaded();
	UFUNCTION() void HandleBattleFinished(EGF_BattleOutcome Outcome);

	/**
	 * A player creature just fainted. Writes it straight back to the party record.
	 *
	 * Without this the faint lives only on the battle actor until TeardownArena
	 * runs WritePartyBack, and an encounter that holds its outcome open does not
	 * reach that until the player dismisses the result screen. Everything that
	 * asks the party whether a creature is down -- GiveEXP most of all -- keeps
	 * seeing it standing, and keeps paying it for kills it was not around for.
	 */
	UFUNCTION() void HandleCreatureDowned(FGF_BattleSlot Slot, AGF_Creature* Creature);

	void BeginSendOut();
	void SendOutNext();
	bool GatherPartyForBattle();
	void WritePartyBack();
	void ApplyBattleInputMode(bool bEntering);
	void StageArena();
	bool StartTheFight();
	void BeginBattleInArena();
	void TeardownArena();
	void FinishEncounter(EGF_BattleOutcome Outcome);

	AGF_BattleArena* FindArenaInStreamedLevel() const;

	UPROPERTY() TObjectPtr<AGF_TurnBattleManager> Manager;
	UPROPERTY() TObjectPtr<AGF_BattleArena> Arena;

	UPROPERTY() TArray<TObjectPtr<AGF_Creature>> SpawnedCreatures;

	// Player creatures the launcher spawned from the party, and the party slots
	// they came from. Kept in step so the fight can be written back.
	UPROPERTY() TArray<TObjectPtr<AGF_Creature>> SpawnedPartyCreatures;
	TArray<int32> SpawnedPartyIndices;
	UPROPERTY() TObjectPtr<AActor> PreviousViewTarget;

	// Latent load and unload need distinct ids, or a fast second encounter can
	// have its load cancelled by the previous fight's unload.
	int32 LatentLoadId = 0;

	FGF_EncounterRequest PendingRequest;
	UPROPERTY() TArray<TObjectPtr<AGF_Creature>> PendingPlayerActive;
	UPROPERTY() TArray<TObjectPtr<AGF_Creature>> PendingPlayerReserves;

	bool bInBattle = false;

	// Staged and waiting on a reveal to call BeginStagedBattle.
	bool bAwaitingReveal = false;

	// Creatures still to arrive, in the order they should. Enemies first.
	UPROPERTY() TArray<TObjectPtr<AGF_Creature>> SendOutQueue;
	bool bSendingOut = false;

	// Decided, shown, and waiting on DismissOutcome before the arena goes.
	bool bAwaitingOutcome = false;
	EGF_BattleOutcome HeldOutcome = EGF_BattleOutcome::None;

	UPROPERTY() TArray<TObjectPtr<AGF_Creature>> StagedEnemies;
};
