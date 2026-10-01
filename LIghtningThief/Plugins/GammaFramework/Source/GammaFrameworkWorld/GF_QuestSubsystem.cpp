// Fill out your copyright notice in the Description page of Project Settings.


#include "GF_QuestSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

void UGF_QuestSubsystem::InitializeFromConfig(UGF_QuestConfig* InConfig)
{
	Config = InConfig;
}

void UGF_QuestSubsystem::StartOrSetQuest(FName QuestID, int32 StartStage)
{
	int32* Found = QuestStages.Find(QuestID);
	const bool bNew = (Found == nullptr);
	if (bNew || (StartStage > *Found))
	{
		QuestStages.Add(QuestID, StartStage);
		QuestLastUpdatedSeconds.Add(QuestID, UGameplayStatics::GetTimeSeconds(GetWorld()));
		OnQuestStageChanged.Broadcast(QuestID, StartStage);
		MaybeComplete(QuestID, StartStage);
		UpdateCurrentQuestAuto(QuestID, StartStage);
	}
}

void UGF_QuestSubsystem::AdvanceQuest(FName QuestID, int32 NewStage)
{
	int32& Stage = QuestStages.FindOrAdd(QuestID, 0);
	if (NewStage > Stage)
	{
		Stage = NewStage;
		QuestLastUpdatedSeconds.Add(QuestID, UGameplayStatics::GetTimeSeconds(GetWorld()));
		OnQuestStageChanged.Broadcast(QuestID, NewStage);
		MaybeComplete(QuestID, NewStage);
		UpdateCurrentQuestAuto(QuestID, NewStage);
	}
}

int32 UGF_QuestSubsystem::GetQuestStage(FName QuestID) const
{
	if (const int32* Stage = QuestStages.Find(QuestID))
	{
		return *Stage;
	}
	return 0;
}

bool UGF_QuestSubsystem::IsQuestAtLeast(FName QuestID, int32 MinStage) const
{
	return GetQuestStage(QuestID) >= MinStage;
}


void UGF_QuestSubsystem::SetCurrentQuest(FName QuestID, bool bBroadcast)
{
	CurrentQuest = QuestID;
	if (bBroadcast)
	{
		BroadcastCurrentQuest();
	}
}

bool UGF_QuestSubsystem::GetCurrentObjectiveText(FText& OutText) const
{
	if (!CurrentQuest.IsNone())
	{
		const int32 Stage = GetQuestStage(CurrentQuest);
		return GetObjectiveText(CurrentQuest, Stage, OutText);
	}
	return false;
}

void UGF_QuestSubsystem::UpdateCurrentQuestAuto(FName QuestID, int32 /*NewStage*/)
{
	if (!bAutoTrackCurrentQuest) return;


	if (CurrentQuest.IsNone())
	{
		SetCurrentQuest(QuestID, /*bBroadcast*/true);
		return;
	}


	const double* UpdatedTime = QuestLastUpdatedSeconds.Find(QuestID);
	const double* CurrentTime = QuestLastUpdatedSeconds.Find(CurrentQuest);
	if (UpdatedTime && (!CurrentTime || *UpdatedTime >= *CurrentTime))
	{
		SetCurrentQuest(QuestID, /*bBroadcast*/true);
	}
}

// Flag lifetime is hard to debug because a missing flag looks identical to a
// flag that was never set. These logs make the whole sequence visible: search
// the output for "QuestFlag" to get an ordered history of every add, clear,
// promote and failed lookup.

// A flag name is only trustworthy if every dot-separated segment carries meaning.
// The names that actually broke things were assembled at runtime from a value that
// had gone missing: Append("", ".Defeated") -> ".Defeated", and
// Append(ToString(NAME_None), ".Defeated") -> "None.Defeated". Neither is empty, so
// the old IsNone() guard passed them, and once ONE of them was in StoryFlags every
// other gate built from a missing value asked for the identical string and got true.
bool UGF_QuestSubsystem::IsFlagNameValid(FName Flag, FString& OutReason)
{
	if (Flag.IsNone())
	{
		OutReason = TEXT("the name is empty");
		return false;
	}

	const FString AsString = Flag.ToString();

	if (AsString.TrimStartAndEnd().IsEmpty())
	{
		OutReason = TEXT("the name is whitespace only");
		return false;
	}

	TArray<FString> Segments;
	AsString.ParseIntoArray(Segments, TEXT("."), /*InCullEmpty=*/false);

	for (const FString& Segment : Segments)
	{
		const FString Trimmed = Segment.TrimStartAndEnd();

		if (Trimmed.IsEmpty())
		{
			OutReason = FString::Printf(
				TEXT("'%s' has an empty segment - whatever was appended to build it was blank"),
				*AsString);
			return false;
		}

		// "None" is what FName::ToString() produces for an unset value, so a segment
		// that reads None means the variable feeding this name was never assigned.
		if (Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			OutReason = FString::Printf(
				TEXT("'%s' contains a 'None' segment - the variable used to build it was unset"),
				*AsString);
			return false;
		}
	}

	return true;
}

namespace
{
	// Some HasFlag call sites sit on NPC patrol/tick paths, so a single misconfigured
	// actor could otherwise write the same error thousands of times a second and cost
	// more than the bug does. Shout once per distinct bad name, then drop to Verbose.
	// Game thread only, which is where every flag query lives.
	bool ShouldShoutAboutBadFlagName(FName Flag)
	{
		static TSet<FName> AlreadyReported;

		bool bAlreadyThere = false;
		AlreadyReported.Add(Flag, &bAlreadyThere);
		return !bAlreadyThere;
	}
}

void UGF_QuestSubsystem::AddTemporaryFlag(FName Flag)
{
	// Same trap as AddFlag - a malformed field would be promoted into StoryFlags by
	// ConfirmTemporaryFlags and poison every unconfigured gate from then on.
	FString Reason;
	if (!IsFlagNameValid(Flag, Reason))
	{
		UE_LOG(LogTemp, Error, TEXT("QuestFlag: REJECTED AddTemporaryFlag - %s. Nothing was recorded; fix the caller."), *Reason);
		return;
	}

	if (!TemporaryFlags.Contains(Flag))
	{
		TemporaryFlags.Add(Flag);
		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: +TEMP '%s' (temp now %d, story %d)"),
			*Flag.ToString(), TemporaryFlags.Num(), StoryFlags.Num());
		OnFlagAdded.Broadcast(Flag);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: +TEMP '%s' ignored, already present"), *Flag.ToString());
	}
}

bool
UGF_QuestSubsystem::HasFlagIncludingTemporary(FName Flag) const
{
	// See HasFlag - an unauthored requirement must not read as satisfied.
	FString Reason;
	if (!IsFlagNameValid(Flag, Reason))
	{
		if (ShouldShoutAboutBadFlagName(Flag))
		{
			UE_LOG(LogTemp, Error, TEXT("QuestFlag: HasFlagIncludingTemporary asked for a malformed name - %s. Returning false."), *Reason);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("QuestFlag: malformed name again - %s"), *Reason);
		}
		return false;
	}

	const bool bInStory = StoryFlags.Contains(Flag);
	const bool bInTemp  = TemporaryFlags.Contains(Flag);

	// Only log the misses - a passing check every frame would drown the log.
	if (!bInStory && !bInTemp)
	{
		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: MISS '%s' (temp set has %d, story set has %d)"),
			*Flag.ToString(), TemporaryFlags.Num(), StoryFlags.Num());
	}

	return bInStory || bInTemp;
}

void
UGF_QuestSubsystem::ConfirmTemporaryFlags()
{
	if (TemporaryFlags.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: promoting %d temporary flag(s) to story"), TemporaryFlags.Num());
	}

	for (const FName& Flag : TemporaryFlags)
	{
		StoryFlags.Add(Flag);
	}

	TemporaryFlags.Empty();
}

void UGF_QuestSubsystem::ClearTemporaryFlags()
{
	// The prime suspect for a temporary flag vanishing between being set and
	// being checked. LoadStory calls this too, so a mid-gameplay LoadGame will
	// show up here.
	if (TemporaryFlags.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: DISCARDING %d temporary flag(s) without promoting them"),
			TemporaryFlags.Num());

		for (const FName& Flag : TemporaryFlags)
		{
			UE_LOG(LogTemp, Warning, TEXT("QuestFlag:   discarded '%s'"), *Flag.ToString());
		}
	}

	TemporaryFlags.Empty();
}



void UGF_QuestSubsystem::ResetToBootState()
{
	// Everything here is saved state, so it is safe to drop wholesale - LoadStory()
	// rebuilds it. This is the flag set that used to survive a soft reset and leave
	// the player holding progression they had not saved (the Compendium case).
	UE_LOG(LogTemp, Log, TEXT("QuestFlag: RESET - dropping %d story flag(s), %d temp, %d stage(s), %d toggle(s)"),
		StoryFlags.Num(), TemporaryFlags.Num(), QuestStages.Num(), WorldToggles.Num());

	StoryFlags.Empty();
	TemporaryFlags.Empty();
	QuestStages.Empty();
	WorldToggles.Empty();
	CurrentQuest = NAME_None;

	// An encounter opened in the run being thrown away must not be able to write a
	// flag into the run that replaces it.
	PendingTamerName = NAME_None;

	// The latch describes the LAST load, and a reset is always followed by either a
	// fresh load (which re-decides it) or New Game (where there is nothing to protect).
	bStoryLoadFailed = false;
}

void UGF_QuestSubsystem::BroadcastCurrentQuest()
{
	const int32 Stage = GetQuestStage(CurrentQuest);
	OnCurrentQuestChanged.Broadcast(CurrentQuest, Stage);
}

void UGF_QuestSubsystem::AddFlag(FName Flag)
{
	// A blank flag field in a dialogue node or quest step arrives here as None.
	// Letting it into the set makes every unconfigured HasFlag() check in the game
	// start returning true - which is exactly how the Fernhollow mart clerk (whose
	// PostProgressionFlagRequired was never set) picked its empty post-progression
	// item list and shipped an empty shop.
	FString Reason;
	if (!IsFlagNameValid(Flag, Reason))
	{
		UE_LOG(LogTemp, Error,
			TEXT("QuestFlag: REJECTED AddFlag - %s. NOTHING was recorded, so whatever this was "
			     "meant to gate is still open. Fix the caller."), *Reason);
		return;
	}

	if (!StoryFlags.Contains(Flag))
	{
		StoryFlags.Add(Flag);
		// AddTemporaryFlag logged its adds from the start and this one never did,
		// which made "the flag was never added" and "the flag was added under a
		// different name" look identical in a tester's log. Print the exact string
		// stored, so it can be compared character-for-character with what the gate
		// asks for - a trailing space or a stale tamer name reads the same as a
		// missing flag from the outside.
		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: +'%s' (story now %d)"),
			*Flag.ToString(), StoryFlags.Num());
		OnFlagAdded.Broadcast(Flag);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: +'%s' ignored, already present"), *Flag.ToString());
	}
}

bool UGF_QuestSubsystem::HasFlag(FName Flag) const
{
	// An unset requirement is not a satisfied requirement. Answering false here
	// means a gate whose flag was never authored stays closed instead of opening
	// for everyone.
	FString Reason;
	if (!IsFlagNameValid(Flag, Reason))
	{
		if (ShouldShoutAboutBadFlagName(Flag))
		{
			UE_LOG(LogTemp, Error, TEXT("QuestFlag: HasFlag asked for a malformed name - %s. Returning false."), *Reason);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("QuestFlag: malformed name again - %s"), *Reason);
		}
		return false;
	}

	return StoryFlags.Contains(Flag);
}

// ---------- Tamer defeat ----------
FName UGF_QuestSubsystem::MakeTamerDefeatedFlag(FName TamerName)
{
	// An unnamed tamer gets NAME_None rather than the string "None.Defeated".
	// AddFlag/HasFlag reject NAME_None loudly, which is the whole point: a tamer
	// with no name must fail visibly instead of sharing one anonymous flag with
	// every other unnamed tamer in the game.
	if (TamerName.IsNone())
	{
		return NAME_None;
	}

	return FName(*(TamerName.ToString() + TEXT(".Defeated")));
}

void UGF_QuestSubsystem::MarkTamerDefeated(FName TamerName)
{
	if (TamerName.IsNone())
	{
		UE_LOG(LogTemp, Error,
			TEXT("QuestFlag: MarkTamerDefeated called with NO tamer name. The defeat was NOT "
			     "recorded and this tamer can be fought again. Whoever called this needs to pass "
			     "the overworld NPC's TamerName."));
		return;
	}

	const FName Flag = MakeTamerDefeatedFlag(TamerName);
	UE_LOG(LogTemp, Warning, TEXT("QuestFlag: tamer '%s' defeated -> writing '%s'"),
		*TamerName.ToString(), *Flag.ToString());

	AddFlag(Flag);
}

bool UGF_QuestSubsystem::IsTamerDefeated(FName TamerName) const
{
	if (TamerName.IsNone())
	{
		// Deliberately NOT "defeated". An unnamed tamer reading as beaten is how a
		// tamer ends up unfightable and stuck in a post-battle branch whose dialogue
		// asset was never set.
		UE_LOG(LogTemp, Error,
			TEXT("QuestFlag: IsTamerDefeated called with NO tamer name - answering false. "
			     "Set TamerName on the overworld NPC instance."));
		return false;
	}

	return HasFlag(MakeTamerDefeatedFlag(TamerName));
}

void UGF_QuestSubsystem::BeginTamerEncounter(FName TamerName)
{
	if (TamerName.IsNone())
	{
		UE_LOG(LogTemp, Error,
			TEXT("QuestFlag: BeginTamerEncounter with NO tamer name - this battle's result "
			     "cannot be recorded. Set TamerName on the overworld NPC instance."));
		PendingTamerName = NAME_None;
		return;
	}

	if (!PendingTamerName.IsNone() && PendingTamerName != TamerName)
	{
		// Not fatal, but it means a previous encounter ended without either a defeat
		// or a ClearPendingTamerEncounter - worth seeing before it matters.
		UE_LOG(LogTemp, Warning,
			TEXT("QuestFlag: BeginTamerEncounter('%s') replacing a still-pending encounter with '%s'"),
			*TamerName.ToString(), *PendingTamerName.ToString());
	}

	PendingTamerName = TamerName;
	UE_LOG(LogTemp, Warning, TEXT("QuestFlag: tamer encounter opened for '%s'"), *TamerName.ToString());
}

bool UGF_QuestSubsystem::MarkPendingTamerDefeated()
{
	if (PendingTamerName.IsNone())
	{
		UE_LOG(LogTemp, Error,
			TEXT("QuestFlag: MarkPendingTamerDefeated with no open encounter. The defeat was NOT "
			     "recorded and the tamer stays re-battleable. The overworld NPC must call "
			     "BeginTamerEncounter(TamerName) before starting the battle."));
		return false;
	}

	MarkTamerDefeated(PendingTamerName);
	PendingTamerName = NAME_None;
	return true;
}

void UGF_QuestSubsystem::ClearPendingTamerEncounter()
{
	if (!PendingTamerName.IsNone())
	{
		UE_LOG(LogTemp, Log, TEXT("QuestFlag: tamer encounter for '%s' closed without a defeat"),
			*PendingTamerName.ToString());
	}

	PendingTamerName = NAME_None;
}

bool UGF_QuestSubsystem::RemoveFlag(FName Flag)
{
	const int32 Removed = StoryFlags.Remove(Flag) + TemporaryFlags.Remove(Flag);

	if (Removed > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: -REMOVED '%s'"), *Flag.ToString());
	}

	return Removed > 0;
}

// ---------- Debug menu gate ----------

const FName UGF_QuestSubsystem::DebugUnlockFlag(TEXT("debug.MenuUnlocked"));

bool UGF_QuestSubsystem::IsDebugMenuUnlocked() const
{
	return StoryFlags.Contains(DebugUnlockFlag) || TemporaryFlags.Contains(DebugUnlockFlag);
}

namespace
{
	/**
	 * Split so the passphrase never appears as one contiguous run of text in the shipped exe --
	 * the same reasoning as the save vault key. Someone running `strings` on the binary should not
	 * be handed it. It raises the effort; it does not make it impossible.
	 */
	FString GEDebug_BuildPassphrase()
	{
		return FString(TEXT("gamma")) + TEXT("-woods-") + TEXT("2026");
	}

	UGF_QuestSubsystem* GEDebug_GetQuestSubsystem(UWorld* World)
	{
		if (!World) { return nullptr; }
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UGF_QuestSubsystem>();
		}
		return nullptr;
	}
}

/**
 * gf.Unlock <passphrase>   -- unlock the debug menu for THIS save
 * gf.Unlock                -- report current state
 * gf.Unlock off            -- lock it again
 *
 * Deliberately NOT wrapped in #if !UE_BUILD_SHIPPING. The public build ships as Development, so
 * that guard is true there and would compile this in anyway -- while ALSO stripping it from a
 * future Shipping build, which is where testers would still want it. The runtime flag is the gate;
 * the build configuration is not, and pretending otherwise is how a debug menu reaches players.
 *
 * The unlock persists in the save's story flags, so it survives a session but not a new game, and
 * it rides inside the encrypted vault where a player cannot author it by hand.
 */
static FAutoConsoleCommandWithWorldAndArgs GEDebugUnlockCmd(
	TEXT("gf.Unlock"),
	TEXT("Unlock the in-game debug menu for this save: gf.Unlock <passphrase> | off"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGF_QuestSubsystem* Quest = GEDebug_GetQuestSubsystem(World);
			if (!Quest)
			{
				UE_LOG(LogTemp, Error, TEXT("gf.Unlock: no QuestSubsystem."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("gf.Unlock: debug menu is currently %s for this save."),
					Quest->IsDebugMenuUnlocked() ? TEXT("UNLOCKED") : TEXT("locked"));
				return;
			}

			if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				Quest->RemoveFlag(UGF_QuestSubsystem::DebugUnlockFlag);
				UE_LOG(LogTemp, Warning, TEXT("gf.Unlock: debug menu locked. Save to make it stick."));
				return;
			}

			if (Args[0] != GEDebug_BuildPassphrase())
			{
				// Says nothing about what was wrong, on purpose.
				UE_LOG(LogTemp, Warning, TEXT("gf.Unlock: no."));
				return;
			}

			Quest->AddFlag(UGF_QuestSubsystem::DebugUnlockFlag);
			UE_LOG(LogTemp, Warning,
				TEXT("gf.Unlock: debug menu UNLOCKED for this save. Save the game to persist it; ")
				TEXT("a new game starts locked again."));
		}));

// ---------- World toggles ----------
void UGF_QuestSubsystem::SetWorldToggle(FName ToggleID, bool bValue)
{
	WorldToggles.Add(ToggleID, bValue);
}

bool UGF_QuestSubsystem::GetWorldToggle(FName ToggleID) const
{
	if (const bool* Value = WorldToggles.Find(ToggleID))
	{
		return *Value;
	}
	return false;
}

bool UGF_QuestSubsystem::HasWorldToggle(FName ToggleID) const
{
	return WorldToggles.Contains(ToggleID);
}

bool UGF_QuestSubsystem::FlipWorldToggle(FName ToggleID)
{
	const bool bNewValue = !GetWorldToggle(ToggleID);
	WorldToggles.Add(ToggleID, bNewValue);
	return bNewValue;
}

bool UGF_QuestSubsystem::GetObjectiveText(FName QuestID, int32 Stage, FText& OutText) const
{
	if (!Config) return false;
	if (const FGF_QuestObjectiveStages* Obj = Config->Objectives.Find(QuestID))
	{
		if (const FText* Txt = Obj->StageText.Find(Stage))
		{
			OutText = *Txt;
			return true;
		}
	}
	return false;
}

int32 UGF_QuestSubsystem::GetCompleteStageInternal(FName QuestID) const
{
	if (!Config) return TNumericLimits<int32>::Max();
	if (const int32* Found = Config->CompleteAtStage.Find(QuestID))
	{
		return *Found;
	}
	return TNumericLimits<int32>::Max();
}

void UGF_QuestSubsystem::MaybeComplete(FName QuestID, int32 NewStage)
{
	const int32 CompleteAt = GetCompleteStageInternal(QuestID);
	if (NewStage >= CompleteAt)
	{
		OnQuestCompleted.Broadcast(QuestID);
	}
}

// ---------- Save / Load ----------
bool UGF_QuestSubsystem::SaveStory(const FString& SlotName, int32 UserIndex)
{


	// Saving IS the commit point for temporary flags - that is what "temporary" means
	// here. This used to live in UGF_CreatureManagerSubsystem::SaveGame() only, so the
	// other save path (ManualSave -> SaveToDisk -> here) wrote the file with the temp
	// set still unpromoted and silently threw it away. Doing it here means every save
	// path commits the same thing, and there is no ordering for a caller to get wrong.
	ConfirmTemporaryFlags();

	// If the last load failed, what is in memory is NOT this player's story - it is
	// whatever survived a load that never happened. Writing it would destroy the file
	// that would not load, which is the only copy of their run and may well be
	// recoverable. Refuse until a load succeeds.
	if (bStoryLoadFailed)
	{
		UE_LOG(LogTemp, Error,
			TEXT("QuestFlag: REFUSING to save slot '%s' - the last load of this slot FAILED, so the "
			     "story in memory is not the player's. The file on disk is being preserved. "
			     "Ask the player for their log and their Saved/.ged folder."), *SlotName);
		return false;
	}

	// Last line of defence against the failure this system keeps producing: a save
	// that runs before the story has been loaded, and writes an empty set over a real
	// playthrough. There is no legitimate caller for that - New Game deletes the slot
	// first, so on that path there is no file here to protect. If the in-memory story
	// is completely empty and the file on disk is not, something ran out of order.
	// Refuse, loudly, and keep the player's progress.
	const bool bMemoryEmpty = (StoryFlags.Num() == 0) && (QuestStages.Num() == 0) && (WorldToggles.Num() == 0);

	if (bMemoryEmpty && UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		if (const UGF_QuestSaveGame* Existing = Cast<UGF_QuestSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex)))
		{
			const int32 ExistingCount = Existing->SavedFlags.Num() + Existing->SavedQuestStages.Num() + Existing->SavedWorldToggles.Num();
			if (ExistingCount > 0)
			{
				UE_LOG(LogTemp, Error,
					TEXT("QuestFlag: REFUSED to overwrite slot '%s' - memory holds nothing but the file "
					     "holds %d entr(ies). Something saved before the story was loaded. The file was "
					     "left alone; the save is being reported as failed."),
					*SlotName, ExistingCount);
				return false;
			}
		}
	}

	UGF_QuestSaveGame* SaveObj = Cast<UGF_QuestSaveGame>(UGameplayStatics::CreateSaveGameObject(UGF_QuestSaveGame::StaticClass()));
	if (!SaveObj) return false;

	SaveObj->SavedQuestStages = QuestStages;
	SaveObj->SavedFlags = StoryFlags.Array();
	SaveObj->SavedWorldToggles = WorldToggles;



	return UGameplayStatics::SaveGameToSlot(SaveObj, SlotName, UserIndex);
}

// ---------- Diagnostics ----------
// Flag bugs are reported as "it just didn't work", and the difference between
// "never written", "written under a different name" and "written then lost on save"
// is invisible from the outside. These print the live set so a tester can paste it
// back. Development builds keep logging, so this reaches the tester's console.
namespace
{
	UGF_QuestSubsystem* FindLiveQuestSubsystem()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.OwningGameInstance)
			{
				if (UGF_QuestSubsystem* Quest = Context.OwningGameInstance->GetSubsystem<UGF_QuestSubsystem>())
				{
					return Quest;
				}
			}
		}

		return nullptr;
	}

	FAutoConsoleCommand GDumpFlagsCommand(
		TEXT("gf.DumpFlags"),
		TEXT("Print every story flag currently held in memory, plus the pending tamer encounter."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UGF_QuestSubsystem* Quest = FindLiveQuestSubsystem();
			if (!Quest)
			{
				UE_LOG(LogTemp, Error, TEXT("gf.DumpFlags: no QuestSubsystem - are you at the main menu?"));
				return;
			}

			TArray<FName> Flags = Quest->GetAllFlags();
			Flags.Sort(FNameLexicalLess());

			UE_LOG(LogTemp, Warning, TEXT("=== gf.DumpFlags: %d story flag(s) ==="), Flags.Num());
			for (const FName& Flag : Flags)
			{
				UE_LOG(LogTemp, Warning, TEXT("    %s"), *Flag.ToString());
			}
			UE_LOG(LogTemp, Warning, TEXT("=== pending tamer encounter: '%s' ==="),
				*Quest->GetPendingTamerName().ToString());
		}));

	FAutoConsoleCommand GDumpTamerFlagsCommand(
		TEXT("gf.DumpTamerFlags"),
		TEXT("Print only the '<Tamer>.Defeated' flags."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UGF_QuestSubsystem* Quest = FindLiveQuestSubsystem();
			if (!Quest)
			{
				UE_LOG(LogTemp, Error, TEXT("gf.DumpTamerFlags: no QuestSubsystem - are you at the main menu?"));
				return;
			}

			TArray<FName> Flags = Quest->GetAllFlags();
			Flags.Sort(FNameLexicalLess());

			int32 Count = 0;
			UE_LOG(LogTemp, Warning, TEXT("=== gf.DumpTamerFlags ==="));
			for (const FName& Flag : Flags)
			{
				if (Flag.ToString().EndsWith(TEXT(".Defeated")))
				{
					UE_LOG(LogTemp, Warning, TEXT("    %s"), *Flag.ToString());
					++Count;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("=== %d tamer(s) recorded as defeated ==="), Count);
		}));
}

bool UGF_QuestSubsystem::LoadStory(const FString& SlotName, int32 UserIndex, bool bRebroadcast)
{
	// Loading replaces StoryFlags wholesale and drops every temporary flag. If
	// this fires mid-gameplay it will silently undo anything set since the last
	// save, so it is worth seeing in the log even on the normal startup path.
	UE_LOG(LogTemp, Warning, TEXT("QuestFlag: LoadStory('%s') - replacing %d story flag(s)"),
		*SlotName, StoryFlags.Num());

	ClearTemporaryFlags();

	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		// A missing slot means "there is no story to restore", which is a reset --
		// not a no-op.  New Game deletes QuestSlot and then re-enters through here,
		// so returning early without clearing left the previous playthrough's flags,
		// stages and world toggles live in memory for the next run.
		QuestStages.Empty();
		StoryFlags.Empty();
		WorldToggles.Empty();

		// Genuinely absent is not a failure - there is nothing to protect.
		bStoryLoadFailed = false;

		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: LoadStory('%s') - no save slot, story is now empty"), *SlotName);
		return false;
	}

	if (UGF_QuestSaveGame* SaveObj = Cast<UGF_QuestSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex)))
	{
		bStoryLoadFailed = false;
		QuestStages = SaveObj->SavedQuestStages;
		StoryFlags = TSet<FName>(SaveObj->SavedFlags);
		WorldToggles = SaveObj->SavedWorldToggles;

		// The AddFlag guard only stops NEW bad flags. Saves already in testers' hands
		// can hold ones written before the guard existed - a bare None, or a runtime
		// name that came out as "None.Defeated" because the value used to build it was
		// missing. Scrub them on the way in: an affected save otherwise keeps reporting
		// "already defeated" for every tamer whose name failed to resolve, which puts
		// the player into a post-battle branch whose dialogue was never authored.
		TArray<FName> Malformed;
		for (const FName& Flag : StoryFlags)
		{
			FString Reason;
			if (!IsFlagNameValid(Flag, Reason))
			{
				Malformed.Add(Flag);
				UE_LOG(LogTemp, Error, TEXT("QuestFlag: REPAIR - dropping '%s' from save slot '%s' (%s)"),
					*Flag.ToString(), *SlotName, *Reason);
			}
		}

		for (const FName& Flag : Malformed)
		{
			StoryFlags.Remove(Flag);
		}

		if (Malformed.Num() > 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("QuestFlag: REPAIR - removed %d malformed flag(s) from '%s'. Anything they were "
				     "standing in for is now replayable, which is the safe direction."),
				Malformed.Num(), *SlotName);
		}

		UE_LOG(LogTemp, Warning, TEXT("QuestFlag: LoadStory('%s') restored %d story flag(s), %d stage(s), %d toggle(s)"),
			*SlotName, StoryFlags.Num(), QuestStages.Num(), WorldToggles.Num());

		if (bRebroadcast)
		{

			for (const auto& Pair : QuestStages)
			{
				OnQuestStageChanged.Broadcast(Pair.Key, Pair.Value);
				MaybeComplete(Pair.Key, Pair.Value);
			}
			for (const FName& Flag : StoryFlags)
			{
				OnFlagAdded.Broadcast(Flag);
			}
		}
		return true;
	}

	// THE SLOT EXISTS AND WOULD NOT LOAD.
	//
	// This branch used to be a bare `return false` with no log and no clearing, which
	// made it the single most damaging line in the save system - and one that CANNOT
	// happen in the editor, which is why it went unreproduced for so long:
	//
	//   Editor   (FGenericSaveGameSystem): DoesSaveGameExist is a file-size test and
	//            LoadGame just reads the bytes. Exists therefore implies loads, so
	//            this branch is unreachable.
	//   Packaged (FGF_SaveGuardSystem):     DoesSaveGameExist checks a 32-byte header
	//            (or an install-vault / legacy file), while LoadGame verifies magic,
	//            version, AES and a SHA-1. Exists does NOT imply loads.
	//
	// Reaching here after a soft reset meant ResetToBootState() had already emptied
	// StoryFlags and nothing put them back: the player resumed with ZERO flags, every
	// gate reopened, and no line anywhere said so.
	//
	// Latch it. StoryFlags is left EXACTLY as the caller left it rather than being
	// invented, and SaveStory() refuses to write while the latch is set, so the file
	// that would not load is not then overwritten by the empty run that resulted.
	bStoryLoadFailed = true;

	UE_LOG(LogTemp, Error,
		TEXT("QuestFlag: LoadStory('%s') FAILED - the slot reports as existing but would not load "
		     "(corrupt, truncated, written by a different build, or an empty legacy .sav that was "
		     "migrated into the vault). The story was NOT restored and saving is now blocked so the "
		     "file on disk is preserved. In-memory story is %d flag(s), %d stage(s), %d toggle(s)."),
		*SlotName, StoryFlags.Num(), QuestStages.Num(), WorldToggles.Num());

	return false;
}