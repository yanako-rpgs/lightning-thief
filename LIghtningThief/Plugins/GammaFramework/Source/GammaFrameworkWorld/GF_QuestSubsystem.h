// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataAsset.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "GF_ResettableState.h"
#include "GF_QuestSubsystem.generated.h"

// ---------- Optional objective text per stage ----------
USTRUCT(BlueprintType)
struct FGF_QuestObjectiveStages
{
	GENERATED_BODY()

	// StageIndex -> On-screen objective text
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<int32, FText> StageText;
};

// ---------- Optional config data asset ----------
UCLASS(BlueprintType)
class GAMMAFRAMEWORKWORLD_API UGF_QuestConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	// QuestID -> Stage at/after which a quest is considered completed
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, int32> CompleteAtStage;

	// QuestID -> objective text map
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, FGF_QuestObjectiveStages> Objectives;
};

// ---------- Delegates ----------
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnQuestStageChanged, FName, QuestID, int32, NewStage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnQuestCompleted, FName, QuestID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnFlagAdded, FName, Flag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnCurrentQuestChanged, FName, QuestID, int32, Stage);


// ---------- Save object ----------
UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_QuestSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TMap<FName, int32> SavedQuestStages;

	UPROPERTY()
	TArray<FName> SavedFlags; // TSet not supported directly by UPROPERTY

	// Persistent two-way switches (puzzle switches, doors). Unlike flags these can flip back off.
	UPROPERTY()
	TMap<FName, bool> SavedWorldToggles;

};

// ---------- Subsystem ----------
UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_QuestSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
	UPROPERTY(BlueprintAssignable, Category = "Quest|Events")
	FGF_OnCurrentQuestChanged OnCurrentQuestChanged;

	GENERATED_BODY()
public:
	// ===== Events you can bind to in BP or C++ =====
	UPROPERTY(BlueprintAssignable, Category="Quest|Events")
	FGF_OnQuestStageChanged OnQuestStageChanged;

	UPROPERTY(BlueprintAssignable, Category="Quest|Events")
	FGF_OnQuestCompleted OnQuestCompleted;

	UPROPERTY(BlueprintAssignable, Category="Quest|Events")
	FGF_OnFlagAdded OnFlagAdded;

public:
	// ===== Runtime API (Blueprint-callable) =====
	UFUNCTION(BlueprintCallable, Category="Quest")
	void InitializeFromConfig(UGF_QuestConfig* InConfig);

	UFUNCTION(BlueprintCallable, Category="Quest")
	void StartOrSetQuest(FName QuestID, int32 StartStage);

	UFUNCTION(BlueprintCallable, Category="Quest")
	void AdvanceQuest(FName QuestID, int32 NewStage);

	UFUNCTION(BlueprintPure, Category="Quest")
	int32 GetQuestStage(FName QuestID) const;

	UFUNCTION(BlueprintPure, Category="Quest")
	bool IsQuestAtLeast(FName QuestID, int32 MinStage) const;

	UFUNCTION(BlueprintCallable, Category="Quest")
	void AddFlag(FName Flag);

	UFUNCTION(BlueprintPure, Category="Quest")
	bool HasFlag(FName Flag) const;

	// Number of saved story flags. Use this to verify a load actually populated
	// them — do NOT use UObject::GetFlags(), which compiles here but returns the
	// object's RF_ flags and has nothing to do with quests.
	UFUNCTION(BlueprintPure, Category="Quest")
	int32 GetFlagCount() const { return StoryFlags.Num(); }

	// Every saved story flag, for debug UI and logging.
	UFUNCTION(BlueprintPure, Category="Quest")
	TArray<FName> GetAllFlags() const { return StoryFlags.Array(); }

	// Clears a flag from both the saved and temporary sets. Returns true if it was set.
	// Useful for retryable cutscenes: set a flag on entry, remove it when the player fails.
	UFUNCTION(BlueprintCallable, Category="Quest")
	bool RemoveFlag(FName Flag);

	// ===== Debug menu gate =====

	/**
	 * Is the in-game debug menu unlocked for this save?
	 *
	 * Gate every debug tool on this. It is NOT a build-configuration check, and it deliberately
	 * cannot be one: the public build ships as Development, so `#if !UE_BUILD_SHIPPING` is TRUE
	 * there and would protect nothing. A runtime flag is the only thing that separates a tester
	 * from a player in the same binary.
	 *
	 * The unlock lives in the save, which is AES-encrypted in a hidden vault, so a player cannot
	 * hand-author one. Unlocking is done with the console command `gf.Unlock <passphrase>` -- and
	 * the console itself is unbound in shipped builds (Config/DefaultInput.ini), so a tester needs
	 * both the passphrase and the knowledge to re-bind the console key.
	 *
	 * This is obscurity, not security. It stops a curious player, not a determined one. That is
	 * the correct bar here: the cost of being wrong is a spoiled playthrough, not a breach.
	 */
	UFUNCTION(BlueprintPure, Category="Quest|Debug")
	bool IsDebugMenuUnlocked() const;

	/** The flag IsDebugMenuUnlocked() reads. Namespaced so it can never collide with a story flag. */
	static const FName DebugUnlockFlag;

	// ===== Flag name validation =====
	// A flag whose name was BUILT at runtime (Append/StringToName) can come out
	// malformed when the value feeding it is missing - "None.Defeated", ".Defeated",
	// "Foo." and so on. Those are not empty, so the old NAME_None guard let them
	// straight through, and then every OTHER gate built from a missing value asked
	// for the same string and got TRUE. One broken tamer marking every broken
	// tamer as beaten is exactly how a progression gate opens for free.
	// Returns false and fills OutReason for anything malformed.
	UFUNCTION(BlueprintPure, Category="Quest")
	static bool IsFlagNameValid(FName Flag, FString& OutReason);

	// ===== Tamer defeat =====
	// The canonical "<TamerName>.Defeated" flag. Every read and the one write MUST
	// go through here: the bug this replaces had the NPC checking its own TamerName
	// while the battle actor wrote a name fished out of the player pawn's CutsceneActor, so
	// the two could - and did - disagree.
	// Returns NAME_None for an empty tamer name, which AddFlag/HasFlag then reject.
	UFUNCTION(BlueprintPure, Category="Quest|Tamers")
	static FName MakeTamerDefeatedFlag(FName TamerName);

	UFUNCTION(BlueprintCallable, Category="Quest|Tamers")
	void MarkTamerDefeated(FName TamerName);

	UFUNCTION(BlueprintPure, Category="Quest|Tamers")
	bool IsTamerDefeated(FName TamerName) const;

	// Hand-off so the defeat write never has to rediscover who it fought.
	// The overworld NPC calls this the moment the encounter starts, where it still
	// has its own TamerName in hand; the battle actor calls
	// MarkPendingTamerDefeated() when the tamer goes down. No actor-reference
	// chain in between, so nothing about the battle can lose the name.
	UFUNCTION(BlueprintCallable, Category="Quest|Tamers")
	void BeginTamerEncounter(FName TamerName);

	// Promotes the pending tamer into a real ".Defeated" flag. Returns false and
	// logs an error if no encounter was ever opened - which means the flag was NOT
	// written and the tamer is still re-battleable, so it must be loud.
	UFUNCTION(BlueprintCallable, Category="Quest|Tamers")
	bool MarkPendingTamerDefeated();

	// Cleared when the encounter ends without a defeat (player ran, lost, or the
	// dialogue was cancelled), so a later battle cannot inherit a stale name.
	UFUNCTION(BlueprintCallable, Category="Quest|Tamers")
	void ClearPendingTamerEncounter();

	UFUNCTION(BlueprintPure, Category="Quest|Tamers")
	FName GetPendingTamerName() const { return PendingTamerName; }

	// ===== World toggles =====
	// Saved two-way state for switch puzzles (e.g. Voltmere trial arcs). Defaults to false when never set.
	UFUNCTION(BlueprintCallable, Category="Quest|WorldState")
	void SetWorldToggle(FName ToggleID, bool bValue);

	UFUNCTION(BlueprintPure, Category="Quest|WorldState")
	bool GetWorldToggle(FName ToggleID) const;

	// True only if this toggle has ever been written. Lets actors keep their
	// editor-placed default state until the player actually changes them.
	UFUNCTION(BlueprintPure, Category="Quest|WorldState")
	bool HasWorldToggle(FName ToggleID) const;

	// Convenience: inverts the current value and returns the new one
	UFUNCTION(BlueprintCallable, Category="Quest|WorldState")
	bool FlipWorldToggle(FName ToggleID);

	// Convenience for on-screen text if using the config
	UFUNCTION(BlueprintPure, Category="Quest|UI")
	bool GetObjectiveText(FName QuestID, int32 Stage, FText& OutText) const;

	// ===== Save / Load =====
	UFUNCTION(BlueprintCallable, Category="Quest|Save")
	bool SaveStory(const FString& SlotName = TEXT("QuestSlot"), int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category="Quest|Save")
	bool LoadStory(const FString& SlotName = TEXT("QuestSlot"), int32 UserIndex = 0, bool bRebroadcast = true);

	// True when the last LoadStory found a slot that reported as existing but would
	// not load. This is NOT the same as "no save": it means the player HAS a story on
	// disk that could not be read, so the flags in memory are meaningless and must not
	// be written back over it. SaveStory() refuses while this is set.
	//
	// Only reachable in packaged builds - the editor's save system has no integrity
	// check, so "exists" and "loads" are the same question there. Gate the Continue
	// button (or show a warning) on this rather than letting the player play on and
	// discover every gate has reopened.
	UFUNCTION(BlueprintPure, Category="Quest|Save")
	bool DidStoryLoadFail() const { return bStoryLoadFailed; }

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TObjectPtr<UGF_QuestConfig> Config;

	UFUNCTION(BlueprintCallable, Category="Quest")
	void AddTemporaryFlag(FName Flag);

	UFUNCTION(BlueprintPure, Category="Quest")
	bool HasFlagIncludingTemporary(FName Flag) const;

	UFUNCTION(BlueprintCallable, Category="Quest")
	void ConfirmTemporaryFlags();

	UFUNCTION(BlueprintCallable, Category="Quest")
	void ClearTemporaryFlags();

	// IGF_ResettableState - drops the entire story: flags, stages and world toggles.
	// LoadStory() repopulates from disk, so nothing here needs preserving.
	virtual void ResetToBootState() override;




	// ---- Current quest API ----
	// If true, the subsystem auto-sets CurrentQuest to the last quest you advanced/started
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest|Current")
	bool bAutoTrackCurrentQuest = true;

	UFUNCTION(BlueprintCallable, Category = "Quest|Current")
	void SetCurrentQuest(FName QuestID, bool bBroadcast = true);

	UFUNCTION(BlueprintPure, Category = "Quest|Current")
	FName GetCurrentQuest() const { return CurrentQuest; }

	UFUNCTION(BlueprintPure, Category = "Quest|Current")
	int32 GetCurrentQuestStage() const { return GetQuestStage(CurrentQuest); }

	// Convenience: get the objective text for the *current* quest at its current stage
	UFUNCTION(BlueprintPure, Category = "Quest|Current")
	bool GetCurrentObjectiveText(FText& OutText) const;

protected:
	// state
	UPROPERTY()
	TMap<FName, int32> QuestStages;

	UPROPERTY()
	TSet<FName> StoryFlags;

	UPROPERTY()
	TSet<FName> TemporaryFlags;

	UPROPERTY()
	TMap<FName, bool> WorldToggles;

	// Runtime only - deliberately NOT saved. It is live for the length of one
	// encounter and ResetToBootState() clears it.
	UPROPERTY()
	FName PendingTamerName;

	// Set by LoadStory when a slot exists but will not load. See DidStoryLoadFail().
	UPROPERTY()
	bool bStoryLoadFailed = false;



	// helpers
	int32 GetCompleteStageInternal(FName QuestID) const;
	void MaybeComplete(FName QuestID, int32 NewStage);
	void UpdateCurrentQuestAuto(FName QuestID, int32 NewStage);
	void BroadcastCurrentQuest();

	UPROPERTY()
	FName CurrentQuest;

	// Optional: helps pick �most recently updated� quest in auto mode
	UPROPERTY()
	TMap<FName, double> QuestLastUpdatedSeconds; // World time stamp per quest

};