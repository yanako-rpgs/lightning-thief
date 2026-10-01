// GF_GameResetLibrary.cpp
#include "GF_GameResetLibrary.h"
#include "GF_ResettableState.h"
#include "GF_CreatureBridge.h"
#include "GF_QuestSubsystem.h"   // the reload has to be able to report whether the STORY came back, not just the party

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/GameInstanceSubsystem.h"

// Resolves the GameInstance and reports loudly if it cannot, since every caller of
// this file is about to change level on the assumption the reset happened.
static UGameInstance* ResolveGameInstance(const UObject* WorldContextObject)
{
    UWorld* World = GEngine
        ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
        : nullptr;

    return World ? World->GetGameInstance() : nullptr;
}

int32 UGF_GameResetLibrary::ResetRuntimeState(const UObject* WorldContextObject)
{
    UGameInstance* GI = ResolveGameInstance(WorldContextObject);
    if (!GI)
    {
        UE_LOG(LogTemp, Error, TEXT("GameReset: no GameInstance - NOTHING was reset. "
            "Whatever comes next will run on the previous session's state."));
        return 0;
    }

    return SweepSubsystems(GI, /*bReloadFromSave=*/true);
}

int32 UGF_GameResetLibrary::ResetForNewGame(const UObject* WorldContextObject)
{
    UGameInstance* GI = ResolveGameInstance(WorldContextObject);
    if (!GI)
    {
        UE_LOG(LogTemp, Error, TEXT("GameReset: no GameInstance - New Game will run on the "
            "previous session's state."));
        return 0;
    }

    // Order is fixed here on purpose. Delete FIRST so nothing downstream can read the
    // old save back; sweep SECOND so the in-memory flags go too. DeleteSave only
    // removes the QuestSlot file - the QuestSubsystem keeps its flags until the sweep
    // clears them, which is exactly how a new game ended up holding the previous
    // playthrough's story and soft-locking on the starter event.
    if (FGF_CreatureBridge::DeleteCreatureSave)
    {
        FGF_CreatureBridge::DeleteCreatureSave(GI);
        UE_LOG(LogTemp, Log, TEXT("GameReset: New Game - save deleted, clearing subsystems."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("GameReset: creature layer not bound - could not "
            "delete the save for New Game."));
    }

    return SweepSubsystems(GI, /*bReloadFromSave=*/false);
}

int32 UGF_GameResetLibrary::SweepSubsystems(UGameInstance* GI, bool bReloadFromSave)
{
    if (!GI)
    {
        UE_LOG(LogTemp, Error, TEXT("GameReset: no GameInstance - NOTHING was reset. "
            "Whatever comes next will run on the previous session's state."));
        return 0;
    }

    // Iterating the live collection rather than a hand-written list is the point: a
    // subsystem added later shows up here automatically instead of being missed.
    // GetSubsystemArrayCopy, not GetSubsystemArray - the latter is deprecated in 5.4+
    // and returns a reference to a temporary. A copy is also the safer choice here
    // because ResetToBootState implementations touch other subsystems.
    const TArray<UGameInstanceSubsystem*> Subsystems = GI->GetSubsystemArrayCopy<UGameInstanceSubsystem>();

    int32 ResetCount   = 0;
    int32 SkippedCount = 0;

    UE_LOG(LogTemp, Log, TEXT("GameReset: sweeping %d GameInstance subsystem(s)..."), Subsystems.Num());

    for (UGameInstanceSubsystem* Subsystem : Subsystems)
    {
        if (!IsValid(Subsystem))
        {
            continue;
        }

        if (IGF_ResettableState* Resettable = Cast<IGF_ResettableState>(Subsystem))
        {
            Resettable->ResetToBootState();
            ++ResetCount;
            UE_LOG(LogTemp, Verbose, TEXT("GameReset:   reset %s"), *Subsystem->GetClass()->GetName());
        }
        else
        {
            // Only OUR subsystems are actionable. The collection also holds engine and
            // plugin ones (ReplaySubsystem, CommonUI, FCTween, Cog, AutoSettingsInput,
            // ...) which we cannot add an interface to and which hold no player state.
            // Warning about those was pure noise - ten red lines and a failed ensure on
            // every reset, which trains you to ignore the one line that matters.
            const bool bIsOurs = Subsystem->GetClass()->GetOutermost()->GetName()
                .StartsWith(TEXT("/Script/GammaFramework"));

            if (!bIsOurs)
            {
                UE_LOG(LogTemp, Verbose, TEXT("GameReset:   skipped external %s"),
                    *Subsystem->GetClass()->GetName());
                continue;
            }

            ++SkippedCount;

            // Loud on purpose, now that it can only fire for a subsystem we own. One of
            // ours that does not implement the interface will silently carry the previous
            // run forward - the exact failure this system exists to prevent. If the answer
            // really is "this one holds nothing", implement it as a documented no-op so
            // the next person does not have to work that out again.
            UE_LOG(LogTemp, Error,
                TEXT("GameReset: %s does NOT implement IGF_ResettableState - its state will "
                     "survive into the next run. Implement the interface (a documented no-op "
                     "is fine) or expect leaked state."),
                *Subsystem->GetClass()->GetName());
        }
    }

    UE_LOG(LogTemp, Log, TEXT("GameReset: %d subsystem(s) reset, %d of ours skipped (external ones ignored)."),
        ResetCount, SkippedCount);

#if !UE_BUILD_SHIPPING
    ensureMsgf(SkippedCount == 0,
        TEXT("GameReset: %d GameInstance subsystem(s) do not implement IGF_ResettableState. "
             "See the log lines above for names."), SkippedCount);
#endif

    // ---- Reload from disk, unless this is New Game. ----
    //
    // The sweep above empties everything. The save file is what puts it back, and the
    // ONLY thing that reads it at boot is CreatureManagerSubsystem::Initialize(), which
    // runs once per process. The main menu's Continue never calls LoadGame - it did not
    // have to, because before this system existed nothing ever cleared the boot load.
    //
    // So a reset without a reload leaves the player with no flags and no party, which
    // reads as "New Game" - the intro replaying over an end-game save.
    if (!bReloadFromSave)
    {
        UE_LOG(LogTemp, Log, TEXT("GameReset: reload skipped (New Game) - state intentionally empty."));
        return ResetCount;
    }

    if (FGF_CreatureBridge::LoadCreatureSave)
    {
        const bool bLoaded = FGF_CreatureBridge::LoadCreatureSave(GI);
        UE_LOG(LogTemp, Log, TEXT("GameReset: reload from save -> %s"),
            bLoaded ? TEXT("OK") : TEXT("NO SAVE FILE - the next run will look like a fresh game"));

        // The sweep above emptied the story. If the reload could not put it back, the
        // player is one Open Level away from a run with every gate reopened, and this
        // is the last place that can say so. Report the flag count either way: "reset
        // OK" next to "0 flags" is the signature of this failure in a tester's log.
        if (UGF_QuestSubsystem* Quest = GI->GetSubsystem<UGF_QuestSubsystem>())
        {
            if (Quest->DidStoryLoadFail())
            {
                UE_LOG(LogTemp, Error,
                    TEXT("GameReset: THE STORY DID NOT RELOAD after the sweep. The player is resuming "
                         "with %d flag(s) instead of their saved story - every gate is open and every "
                         "tamer is re-battleable. Their quest save exists but will not read."),
                    Quest->GetFlagCount());
            }
            else
            {
                UE_LOG(LogTemp, Log, TEXT("GameReset: story restored -> %d flag(s)"), Quest->GetFlagCount());
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("GameReset: creature layer not bound - state was cleared "
            "but NOT reloaded. The next run will look like a fresh save."));
    }

    return ResetCount;
}
