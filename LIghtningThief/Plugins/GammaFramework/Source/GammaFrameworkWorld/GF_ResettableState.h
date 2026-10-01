// GF_ResettableState.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GF_ResettableState.generated.h"

UINTERFACE(MinimalAPI)
class UGF_ResettableState : public UInterface
{
    GENERATED_BODY()
};

/**
 * Implemented by every UGameInstanceSubsystem that holds state.
 *
 * WHY THIS EXISTS
 * ---------------
 * OpenLevel rebuilds the world but does NOT touch GameInstance subsystems, so
 * anything they hold survives New Game, Quit to Title, Soft Reset and Load Game.
 * Historically each of those was a separate Blueprint graph that remembered its
 * own subset of things to clear, which is why the same leak kept resurfacing at
 * each new entry point (story flags into run 2, the Compendium flag across a soft
 * reset, and so on).
 *
 * The fix is to stop resetting subtractively ("undo what changed" - unbounded,
 * grows with every cutscene you add) and reset reconstructively ("discard
 * everything, then reload from disk" - bounded by the subsystem count, which is
 * a number you control).
 *
 * THE CONTRACT
 * ------------
 * ResetToBootState() must leave the subsystem exactly as it is immediately after
 * construction, BEFORE any save is read. Saved state is not preserved here - the
 * load path repopulates it. Clearing more than necessary is safe; clearing less
 * is a progression bug.
 *
 * A no-op implementation is a perfectly good answer (see UGF_ItemDataManager and
 * UGF_SettingsSubsystem) - but it has to be written down, because
 * UGF_GameResetLibrary::ResetRuntimeState() logs an error for every subsystem
 * that does NOT implement this. That error is the whole point: the day someone
 * adds subsystem number ten, the first soft reset says so, instead of a tester
 * finding it three weeks later.
 */
class GAMMAFRAMEWORKWORLD_API IGF_ResettableState
{
    GENERATED_BODY()

public:
    /** Return to freshly-constructed state. Called before the world is reopened. */
    virtual void ResetToBootState() = 0;
};
