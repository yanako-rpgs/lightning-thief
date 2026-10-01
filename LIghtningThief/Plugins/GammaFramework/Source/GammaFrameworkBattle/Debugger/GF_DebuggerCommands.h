// GF_DebuggerCommands.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "GF_DebuggerCommands.generated.h"

class IConsoleVariable;
class SWindow;

/**
 * Registers and manages the Gamma Framework Slate debug window.
 *
 * HOW TO HOOK UP:
 *   In UGF_DebugMenuSubsystem::Initialize()    → add: UGF_DebuggerCommands::RegisterCommands();
 *   In UGF_DebugMenuSubsystem::Deinitialize()  → add: UGF_DebuggerCommands::UnregisterCommands();
 *
 * Console usage in-game or PIE:
 *   gf.debugger 1   — opens the floating debugger window
 *   gf.debugger 0   — closes it
 *   gf.dumpeps      — logs every party Creature's APs, EPs, affinity and computed stats
 */
UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_DebuggerCommands : public UObject
{
    GENERATED_BODY()

public:
    static void RegisterCommands();
    static void UnregisterCommands();

    static void OpenDebugger();
    static void CloseDebugger();
    static bool IsDebuggerOpen();

    static void RebindCVarCallback(IConsoleVariable* CVar);

    /** Logs every party Creature's APs, EPs, affinity and computed stats (gf.dumpeps). */
    static void DumpPartyGrowth(UWorld* World);

private:
    static TWeakPtr<SWindow>                    DebuggerWindow;
    static TWeakPtr<class SGF_DebuggerWidget>   DebuggerWidgetRef;
    static IConsoleVariable*                    ShowDebuggerCVar;
    static IConsoleObject*                      DumpGrowthCommand;
};
