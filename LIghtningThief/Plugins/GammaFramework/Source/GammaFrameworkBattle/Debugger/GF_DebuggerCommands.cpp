// GF_DebuggerCommands.cpp
#include "GF_DebuggerCommands.h"
#include "GF_DebuggerWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "HAL/IConsoleManager.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_CreatureStatLibrary.h"

// ─── Static member definitions ───────────────────────────────────────────────
TWeakPtr<SWindow>              UGF_DebuggerCommands::DebuggerWindow;
TWeakPtr<SGF_DebuggerWidget>   UGF_DebuggerCommands::DebuggerWidgetRef;
IConsoleVariable*              UGF_DebuggerCommands::ShowDebuggerCVar = nullptr;
IConsoleObject*                UGF_DebuggerCommands::DumpGrowthCommand = nullptr;

// ─── CVar callback ────────────────────────────────────────────────────────────
void UGF_DebuggerCommands::RebindCVarCallback(IConsoleVariable* CVar)
{
    if (!CVar) return;
    CVar->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
    {
        if (Variable->GetInt() > 0)
            UGF_DebuggerCommands::OpenDebugger();
        else
            UGF_DebuggerCommands::CloseDebugger();
    }));
}

// ─── Register / Unregister ───────────────────────────────────────────────────
void UGF_DebuggerCommands::RegisterCommands()
{
    ShowDebuggerCVar = IConsoleManager::Get().RegisterConsoleVariable(
        TEXT("gf.debugger"),
        0,
        TEXT("Show the Gamma Framework debugger window. 1 = open, 0 = close."),
        ECVF_Cheat
    );

    RebindCVarCallback(ShowDebuggerCVar);

    DumpGrowthCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("gf.dumpeps"),
        TEXT("Logs every party Creature's APs, EPs, affinity and computed stats."),
        FConsoleCommandWithWorldDelegate::CreateStatic(&UGF_DebuggerCommands::DumpPartyGrowth),
        ECVF_Cheat
    );

    UE_LOG(LogTemp, Log, TEXT("GE Debugger: console commands 'gf.debugger' and 'gf.dumpeps' registered."));
}

void UGF_DebuggerCommands::UnregisterCommands()
{
    CloseDebugger();

    if (ShowDebuggerCVar)
    {
        IConsoleManager::Get().UnregisterConsoleObject(ShowDebuggerCVar);
        ShowDebuggerCVar = nullptr;
    }

    if (DumpGrowthCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(DumpGrowthCommand);
        DumpGrowthCommand = nullptr;
    }
}

// ─── AP / EP dump ───────────────────────────────────────────────────────────────────────
void UGF_DebuggerCommands::DumpPartyGrowth(UWorld* World)
{
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    UGF_CreatureManagerSubsystem* Mgr = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
    if (!Mgr)
    {
        UE_LOG(LogTemp, Warning, TEXT("gf.dumpeps: CreatureManagerSubsystem not available."));
        return;
    }

    const int32 PartySize = Mgr->GetPartySize();
    UE_LOG(LogTemp, Display, TEXT("=== gf.dumpeps: %d Creature in party ==="), PartySize);

    for (int32 i = 0; i < PartySize; ++i)
    {
        FGF_CreatureInstanceData Mon;
        if (!Mgr->GetPartyCreatureData(i, Mon))
        {
            continue;
        }

        const int32 TotalEP = Mon.GetTotalEP();

        const FGF_CreatureCurrentStats Stats = UGF_CreatureStatLibrary::CalculateCreatureStats(Mon);

        // Identity + battle state on the header line: this is the line that answers
        // "is the party array itself right?" when a screen shows the wrong Creature.
        UE_LOG(LogTemp, Display, TEXT("[%d] %s Lv%d  HP:%.0f  ID:%d%s%s"),
            i,
            *Mon.GetDisplayName().ToString(),
            Mon.Level,
            Mon.CurrentHP,
            Mon.CreatureID,
            Mon.bIsDowned ? TEXT("  [FAINTED]") : TEXT(""),
            Mon.bIsEgg ? TEXT("  [EGG]") : TEXT(""));
        UE_LOG(LogTemp, Display, TEXT("     EPs   HP:%3d Atk:%3d Def:%3d Mag:%3d Poi:%3d Spe:%3d  (spent %d/%d)"),
            Mon.HP_EP, Mon.Attack_EP, Mon.Defense_EP,
            Mon.Magic_EP, Mon.Poise_EP, Mon.Speed_EP, TotalEP, Mon.GetEPBudget());
        UE_LOG(LogTemp, Display, TEXT("     APs   HP:%3d Atk:%3d Def:%3d Mag:%3d Poi:%3d Spe:%3d  (affinity %d/%d)"),
            Mon.HP_AP, Mon.Attack_AP, Mon.Defense_AP,
            Mon.Magic_AP, Mon.Poise_AP, Mon.Speed_AP, Mon.Affinity, FGF_CreatureInstanceData::MaxAffinity);
        UE_LOG(LogTemp, Display, TEXT("     Stats HP:%.0f/%.0f Atk:%.0f Def:%.0f SpA:%.0f SpD:%.0f Spe:%.0f"),
            Mon.CurrentHP, Stats.MaxHP, Stats.Attack, Stats.Defense,
            Stats.Magic, Stats.Poise, Stats.Speed);
    }
}

// ─── Open ─────────────────────────────────────────────────────────────────────
void UGF_DebuggerCommands::OpenDebugger()
{
    if (IsDebuggerOpen())
    {
        if (TSharedPtr<SWindow> Win = DebuggerWindow.Pin())
            Win->BringToFront();
        return;
    }

    // Resolve world context — prefer PIE, fall back to Game
    UWorld* World = nullptr;
    if (GEngine)
    {
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            if (Ctx.WorldType == EWorldType::PIE && IsValid(Ctx.World()))
            {
                World = Ctx.World();
                break;
            }
        }
        if (!World)
        {
            for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            {
                if ((Ctx.WorldType == EWorldType::Game ||
                     Ctx.WorldType == EWorldType::GamePreview) && IsValid(Ctx.World()))
                {
                    World = Ctx.World();
                    break;
                }
            }
        }
    }

    // Build widget + window
    TSharedRef<SGF_DebuggerWidget> NewWidget = SNew(SGF_DebuggerWidget).WorldContext(World);

    TSharedRef<SWindow> NewWindow = SNew(SWindow)
        .Title(FText::FromString(TEXT("Gamma Framework Debugger")))
        .ClientSize(FVector2D(1150, 800))
        .SupportsMaximize(true)
        .SupportsMinimize(true)
        .IsTopmostWindow(false)
        .CreateTitleBar(true)
        .SizingRule(ESizingRule::UserSized);

    NewWindow->SetContent(NewWidget);
    FSlateApplication::Get().AddWindow(NewWindow);

    DebuggerWindow    = NewWindow;
    DebuggerWidgetRef = NewWidget;

    // When the OS close button is pressed, sync the CVar so re-opening works
    NewWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>&)
    {
        if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("gf.debugger")))
        {
            CVar->SetOnChangedCallback(FConsoleVariableDelegate()); // temp remove
            CVar->Set(0, ECVF_SetByConsole);
            UGF_DebuggerCommands::RebindCVarCallback(CVar);
        }
        DebuggerWidgetRef.Reset();
        DebuggerWindow.Reset();
    }));

    UE_LOG(LogTemp, Log, TEXT("GE Debugger: window opened (World: %s)"),
        World ? *World->GetName() : TEXT("NULL"));
}

// ─── Close ───────────────────────────────────────────────────────────────────
void UGF_DebuggerCommands::CloseDebugger()
{
    if (!IsDebuggerOpen()) return;

    if (TSharedPtr<SWindow> Win = DebuggerWindow.Pin())
        Win->RequestDestroyWindow();

    DebuggerWidgetRef.Reset();
    DebuggerWindow.Reset();

    UE_LOG(LogTemp, Log, TEXT("GE Debugger: window closed."));
}

bool UGF_DebuggerCommands::IsDebuggerOpen()
{
    return DebuggerWindow.IsValid() && DebuggerWidgetRef.IsValid();
}
