// GF_GameResetLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_GameResetLibrary.generated.h"

/**
 * The single entry point every "start over" path goes through.
 *
 * New Game, Quit to Title, Soft Reset and Load Game are the same operation with
 * different endings. Giving each its own Blueprint graph is what let state leak
 * between runs, so all four should call ResetRuntimeState() and differ only in
 * what they do afterwards:
 *
 *   Soft Reset    -> ResetRuntimeState, then OpenLevel.
 *   Quit to Title -> ResetRuntimeState, then OpenLevel to the title screen.
 *   Load Game     -> ResetRuntimeState (the reload is built in).
 *   New Game      -> ResetForNewGame, then OpenLevel.
 *
 * Two nodes, not one with an ordering rule. New Game wants "wipe and stay wiped";
 * everything else wants "wipe and reload". Expressing that as ResetRuntimeState
 * plus a separately-ordered DeleteSave shipped a soft lock: with DeleteSave second,
 * the reset reloaded the save it was about to delete and the new run inherited the
 * whole previous story (DeleteSave removes the QuestSlot FILE, not the in-memory
 * flags). Each node now owns its own ordering so a caller cannot get it wrong.
 *
 * See IGF_ResettableState for why this is a sweep rather than a checklist.
 */
UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_GameResetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Clears every GameInstance subsystem back to its freshly-constructed state,
     * then RELOADS from the save file so memory matches disk again.
     *
     * The reload is part of the same call on purpose: clearing without reloading
     * leaves the player with no flags and no party, which presents as a brand new
     * game (the intro replaying over an end-game save). There is no flow that
     * wants the clear on its own, so there is no pin to get it wrong with.
     *
     * Does NOT change level - callers decide what happens next.
     *
     * Logs an error naming any subsystem that does not implement
     * IGF_ResettableState, and returns how many were reset.
     */
    UFUNCTION(BlueprintCallable, Category = "Game Flow", meta = (WorldContext = "WorldContextObject"))
    static int32 ResetRuntimeState(const UObject* WorldContextObject);

    /**
     * New Game: deletes the save, THEN clears every subsystem, and deliberately does
     * NOT reload afterwards. Leaves the process exactly as if the game had launched
     * with no save file present.
     *
     * Use this INSTEAD OF ResetRuntimeState on the New Game button - do not call both,
     * and do not pair it with a separate Delete Save node. It does the whole thing in
     * the only order that is correct.
     *
     * Does NOT change level - the caller decides what happens next.
     */
    UFUNCTION(BlueprintCallable, Category = "Game Flow", meta = (WorldContext = "WorldContextObject"))
    static int32 ResetForNewGame(const UObject* WorldContextObject);

private:
    /** Shared sweep. bReloadFromSave is what separates New Game from everything else. */
    static int32 SweepSubsystems(class UGameInstance* GI, bool bReloadFromSave);
};
