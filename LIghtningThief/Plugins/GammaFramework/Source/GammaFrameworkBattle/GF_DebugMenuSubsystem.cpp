// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_DebugMenuSubsystem.h"
#include "GF_CreatureManagerSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Debugger/GF_DebuggerCommands.h"
#include "GF_GridDebugComponent.h"
#include "Engine/Engine.h"

void UGF_DebugMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UGF_DebuggerCommands::RegisterCommands();
	// Register console commands
	IConsoleManager& ConsoleManager = IConsoleManager::Get();

	// gf.commandmenu - Opens the debug menu
	ConsoleManager.RegisterConsoleCommand(
		TEXT("gf.commandmenu"),
		TEXT("Opens the Creature debug menu"),
		FConsoleCommandDelegate::CreateUObject(this, &UGF_DebugMenuSubsystem::OpenDebugMenuCommand),
		ECVF_Default
	);

	// gf.givecreature <SpeciesName> <Level> - Give Creature directly
	ConsoleManager.RegisterConsoleCommand(
		TEXT("gf.givecreature"),
		TEXT("Give a Creature to your party. Usage: gf.givecreature Budling 5"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UGF_DebugMenuSubsystem::GiveCreatureCommand),
		ECVF_Default
	);

	// gf.wildbattle <SpeciesName> <Level> - Start wild battle directly
	ConsoleManager.RegisterConsoleCommand(
		TEXT("gf.wildbattle"),
		TEXT("Start a wild battle. Usage: gf.wildbattle Scampling 5"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UGF_DebugMenuSubsystem::WildBattleCommand),
		ECVF_Default
	);

	// gf.setcoins <N> - set Game Corner coins on the LIVE subsystem value
	// (the one UI/shops read; the VaultSystem copy is only touched at save/load)
	ConsoleManager.RegisterConsoleCommand(
		TEXT("gf.setcoins"),
		TEXT("Set Game Corner coins (live value). Usage: gf.setcoins 3000"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (!World || Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("DebugMenu: Usage: gf.setcoins <amount>"));
					return;
				}
				UGameInstance* GI = World->GetGameInstance();
				UGF_CreatureManagerSubsystem* PMS = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
				if (!PMS)
				{
					UE_LOG(LogTemp, Warning, TEXT("DebugMenu: CreatureManagerSubsystem not found"));
					return;
				}
				PMS->GameCornerCoins = FMath::Clamp(FCString::Atoi(*Args[0]), 0, PMS->MaxGameCornerCoins);
				UE_LOG(LogTemp, Log, TEXT("DebugMenu: GameCornerCoins = %d"), PMS->GameCornerCoins);
			}),
		ECVF_Default
	);

	// gf.grid - grid debug overlay on the player pawn.
	// The component is NOT placed on the player pawn, so this attaches one at runtime the
	// first time it is called. That keeps testers from needing an editor to see tile
	// data, and keeps the tick cost at zero until someone asks for it.
	// Draw calls compile out in Shipping (ENABLE_DRAW_DEBUG) - tester builds are
	// Development (Scripts/Ship_Patch.bat -clientconfig=Development) so this works there.
	ConsoleManager.RegisterConsoleCommand(
		TEXT("gf.grid"),
		TEXT("Grid debug overlay. Usage: gf.grid (toggle) | gf.grid on|off | gf.grid coords|walkable|occupied|slopes | gf.grid range <N>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				auto Report = [](const FString& Msg)
				{
					UE_LOG(LogTemp, Log, TEXT("gf.grid: %s"), *Msg);
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Green, FString::Printf(TEXT("gf.grid: %s"), *Msg));
					}
				};

				if (!World)
				{
					return;
				}

				// Shared with the GE Debugger's Grid tab so both drive the same component.
				UGF_GridDebugComponent* Debug = UGF_GridDebugComponent::GetOrCreateForPlayer(World);
				if (!Debug)
				{
					Report(TEXT("no player pawn yet"));
					return;
				}

				const FString Arg = Args.Num() > 0 ? Args[0].ToLower() : FString();

				if (Arg.IsEmpty())
				{
					Debug->bShowDebugGrid = !Debug->bShowDebugGrid;
					Report(Debug->bShowDebugGrid ? TEXT("ON") : TEXT("OFF"));
					return;
				}

				if (Arg == TEXT("on") || Arg == TEXT("1") || Arg == TEXT("true"))
				{
					Debug->bShowDebugGrid = true;
					Report(TEXT("ON"));
				}
				else if (Arg == TEXT("off") || Arg == TEXT("0") || Arg == TEXT("false"))
				{
					Debug->bShowDebugGrid = false;
					Report(TEXT("OFF"));
				}
				else if (Arg == TEXT("coords"))
				{
					Debug->bShowTileCoordinates = !Debug->bShowTileCoordinates;
					Report(FString::Printf(TEXT("coordinates %s"), Debug->bShowTileCoordinates ? TEXT("ON") : TEXT("OFF")));
				}
				else if (Arg == TEXT("walkable"))
				{
					Debug->bShowWalkableTiles = !Debug->bShowWalkableTiles;
					Report(FString::Printf(TEXT("walkable %s"), Debug->bShowWalkableTiles ? TEXT("ON") : TEXT("OFF")));
				}
				else if (Arg == TEXT("occupied"))
				{
					Debug->bShowOccupiedTiles = !Debug->bShowOccupiedTiles;
					Report(FString::Printf(TEXT("occupied %s"), Debug->bShowOccupiedTiles ? TEXT("ON") : TEXT("OFF")));
				}
				else if (Arg == TEXT("slopes"))
				{
					Debug->bShowSlopeTiles = !Debug->bShowSlopeTiles;
					Report(FString::Printf(TEXT("slopes %s"), Debug->bShowSlopeTiles ? TEXT("ON") : TEXT("OFF")));
				}
				else if (Arg == TEXT("range"))
				{
					if (Args.Num() < 2)
					{
						Report(TEXT("usage: gf.grid range <1-50>"));
						return;
					}
					const int32 NewRange = FMath::Clamp(FCString::Atoi(*Args[1]), 1, 50);
					Debug->DebugRangeX = NewRange;
					Debug->DebugRangeY = NewRange;
					Report(FString::Printf(TEXT("range %d"), NewRange));
				}
				else
				{
					Report(TEXT("unknown arg - try: on, off, coords, walkable, occupied, slopes, range <N>"));
					return;
				}

				// Any sub-toggle implies you want to see the overlay.
				if (!Debug->bShowDebugGrid && Arg != TEXT("off"))
				{
					Debug->bShowDebugGrid = true;
				}
			}),
		ECVF_Default
	);

	UE_LOG(LogTemp, Log, TEXT("DebugMenuSubsystem: Console commands registered (gf.commandmenu, gf.givecreature, gf.wildbattle, gf.setcoins, gf.grid)"));
}

void UGF_DebugMenuSubsystem::ResetToBootState()
{
	// The widget was created against the world that is going away.
	if (DebugMenuWidget)
	{
		CloseDebugMenu();
	}
	DebugMenuWidget = nullptr;

	// Staged tamer battle. Left set, this fires into the next run the moment
	// something calls Debug_StartTamerBattle.
	PendingTamerClass      = nullptr;
	PendingTamerDifficulty = EGF_TamerAIDifficulty::Smart;
	PendingBattleTransition  = 0;
	PendingTamerMap        = nullptr;
	PendingTamerIntroSound = nullptr;
	PendingTamerLoopSound  = nullptr;
	PendingTamerHasIntro   = false;
	PendingTamerFadeIn     = false;
	PendingTamerFadeDuration = 0.f;
	PendingTamerFadeVolume   = 1.f;

	// DebugMenuWidgetClass is set once from the Game Instance and is configuration,
	// not run state - deliberately left alone. Same for the species cache.
}

void UGF_DebugMenuSubsystem::Deinitialize()
{
	// Unregister console commands
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	ConsoleManager.UnregisterConsoleObject(TEXT("gf.commandmenu"));
	ConsoleManager.UnregisterConsoleObject(TEXT("gf.givecreature"));
	ConsoleManager.UnregisterConsoleObject(TEXT("gf.wildbattle"));
	ConsoleManager.UnregisterConsoleObject(TEXT("gf.setcoins"));
	ConsoleManager.UnregisterConsoleObject(TEXT("gf.grid"));

	// Close debug menu if open
	if (DebugMenuWidget)
	{
		CloseDebugMenu();
	}

	UGF_DebuggerCommands::UnregisterCommands();

	Super::Deinitialize();
}

//--------------------
// DEBUG WIDGET
//--------------------

void UGF_DebugMenuSubsystem::SetDebugMenuWidgetClass(TSubclassOf<UUserWidget> InWidgetClass)
{
	DebugMenuWidgetClass = InWidgetClass;
	UE_LOG(LogTemp, Log, TEXT("DebugMenu: Widget class set to %s"), InWidgetClass ? *InWidgetClass->GetName() : TEXT("None"));
}

void UGF_DebugMenuSubsystem::OpenDebugMenu()
{
	UE_LOG(LogTemp, Log, TEXT("DebugMenu: OpenDebugMenu called"));

	// Don't open if already open
	if (DebugMenuWidget && DebugMenuWidget->IsInViewport())
	{
		UE_LOG(LogTemp, Warning, TEXT("DebugMenu: Menu is already open!"));
		return;
	}

	// Check if widget class is set
	if (!DebugMenuWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("========================================"));
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Widget class is not set!"));
		UE_LOG(LogTemp, Error, TEXT("========================================"));
		UE_LOG(LogTemp, Error, TEXT("To fix this:"));
		UE_LOG(LogTemp, Error, TEXT("1. Open your Game Instance blueprint"));
		UE_LOG(LogTemp, Error, TEXT("2. In Event Init, add:"));
		UE_LOG(LogTemp, Error, TEXT("   - Get Subsystem (Debug Menu Subsystem)"));
		UE_LOG(LogTemp, Error, TEXT("   - Set Debug Menu Widget Class"));
		UE_LOG(LogTemp, Error, TEXT("   - Select your W_DebugMenu widget"));
		UE_LOG(LogTemp, Error, TEXT("========================================"));
		return;
	}

	// Get world
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Cannot get World!"));
		return;
	}

	// Get player controller
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Cannot get PlayerController!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("DebugMenu: Creating widget from class: %s"), *DebugMenuWidgetClass->GetName());

	// Create widget
	DebugMenuWidget = CreateWidget<UUserWidget>(PlayerController, DebugMenuWidgetClass);
	if (!DebugMenuWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Failed to create widget!"));
		return;
	}

	// Add to viewport
	DebugMenuWidget->AddToViewport(999);

	// Set input mode to UI only
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(DebugMenuWidget->TakeWidget());
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;

	UE_LOG(LogTemp, Warning, TEXT("DebugMenu: Menu opened successfully!"));
}

void UGF_DebugMenuSubsystem::CloseDebugMenu()
{
	if (DebugMenuWidget && DebugMenuWidget->IsInViewport())
	{
		DebugMenuWidget->RemoveFromParent();
		DebugMenuWidget = nullptr;

		// Restore input mode
		UWorld* World = GetWorld();
		if (World)
		{
			APlayerController* PlayerController = World->GetFirstPlayerController();
			if (PlayerController)
			{
				PlayerController->SetInputMode(FInputModeGameOnly());
				PlayerController->bShowMouseCursor = false;
			}
		}

		UE_LOG(LogTemp, Log, TEXT("DebugMenu: Menu closed!"));
	}
}

void UGF_DebugMenuSubsystem::ToggleDebugMenu()
{
	if (DebugMenuWidget && DebugMenuWidget->IsInViewport())
	{
		CloseDebugMenu();
	}
	else
	{
		OpenDebugMenu();
	}
}

//--------------------
// CREATURE OPERATIONS
//--------------------

TArray<FName> UGF_DebugMenuSubsystem::GetAllCreatureSpeciesNames()
{
	if (bHasScannedCreature && CachedSpeciesNames.Num() > 0)
	{
		return CachedSpeciesNames;
	}

	CachedSpeciesNames.Reset();

	// Straight off the manager's registry index -- no assets are loaded.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (const UGF_CreatureManagerSubsystem* Manager = GI->GetSubsystem<UGF_CreatureManagerSubsystem>())
		{
			for (const FGF_SpeciesSummary& Summary : Manager->GetAllSpeciesSummaries())
			{
				if (!Summary.SpeciesName.IsNone())
				{
					CachedSpeciesNames.Add(Summary.SpeciesName);
				}
			}
		}
	}

	CachedSpeciesNames.Sort([](const FName& A, const FName& B)
	{
		return A.ToString() < B.ToString();
	});

	bHasScannedCreature = true;

	UE_LOG(LogTemp, Log, TEXT("DebugMenu: %d species names indexed (0 assets loaded)."), CachedSpeciesNames.Num());

	return CachedSpeciesNames;
}

TArray<UGF_CreatureSpeciesData*> UGF_DebugMenuSubsystem::GetAllCreatureSpecies()
{
	UE_LOG(LogTemp, Warning,
		TEXT("DebugMenu: GetAllCreatureSpecies() is loading every species (~4 GB). ")
		TEXT("Rewire the debug picker to GetAllCreatureSpeciesNames() + FindCreatureByName()."));

	TArray<UGF_CreatureSpeciesData*> Result;

	UGameInstance* GI = GetGameInstance();
	UGF_CreatureManagerSubsystem* Manager = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
	if (!Manager)
	{
		return Result;
	}

	for (const FName& Name : GetAllCreatureSpeciesNames())
	{
		if (UGF_CreatureSpeciesData* Species = Manager->GetCreatureSpeciesData(Name))
		{
			Result.Add(Species);
		}
	}
	return Result;
}

bool UGF_DebugMenuSubsystem::GiveCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level)
{
	if (!SpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: SpeciesData is null!"));
		return false;
	}

	// Get Creature Manager
	UGF_CreatureManagerSubsystem* CreatureManager = GetGameInstance()->GetSubsystem<UGF_CreatureManagerSubsystem>();
	if (!CreatureManager)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Cannot get CreatureManagerSubsystem!"));
		return false;
	}

	// Create Creature instance data
	FGF_CreatureInstanceData NewCreature;
	NewCreature.Initialize(SpeciesData, Level, TArray<TSubclassOf<AGF_SkillDefinition>>(), FName(TEXT("DEBUG")), 0);


	// Add to party
	bool bSuccess = CreatureManager->AddToParty(NewCreature);

	if (bSuccess)
	{
		// Save game
		CreatureManager->SaveGame();

		UE_LOG(LogTemp, Warning, TEXT("DebugMenu: Gave %s (Level %d) to party!"),
			*SpeciesData->SpeciesName.ToString(), Level);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Failed to add Creature to party (party might be full)"));
	}

	return bSuccess;
}

bool UGF_DebugMenuSubsystem::StartWildBattle(UGF_CreatureSpeciesData* SpeciesData, int32 Level)
{
	if (!SpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: SpeciesData is null!"));
		return false;
	}

	// Get world
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Cannot get World!"));
		return false;
	}

	// Find BP_BattleManager in the world
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), FoundActors);

	AActor* BattleManager = nullptr;
	for (AActor* Actor : FoundActors)
	{
		if (Actor->GetClass()->GetName().Contains(TEXT("BP_BattleManager")))
		{
			BattleManager = Actor;
			break;
		}
	}

	if (!BattleManager)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Cannot find BP_BattleManager in world! Make sure you're in a map with a Battle Manager."));
		return false;
	}

	// Create wild Creature instance data
	FGF_CreatureInstanceData WildCreature;
	WildCreature.Initialize(SpeciesData, Level, TArray<TSubclassOf<AGF_SkillDefinition>>(), NAME_None, 0);

	// Call StartWildBattle on BP_BattleManager
	// We need to call this via blueprint since your StartWildBattle uses EBattleTransition
	// Instead, we'll set the wild Creature data and trigger the battle through blueprint

	// Store wild Creature in a variable that your BP_BattleManager can access
	UFunction* SpawnWildCreatureFunc = BattleManager->FindFunction(FName(TEXT("SpawnWildCreature")));
	if (SpawnWildCreatureFunc)
	{
		struct FGF_SpawnWildCreatureParams
		{
			UGF_CreatureSpeciesData* WildCreatureToSpawn;
		};

		FGF_SpawnWildCreatureParams Params;
		Params.WildCreatureToSpawn = SpeciesData;

		BattleManager->ProcessEvent(SpawnWildCreatureFunc, &Params);

		UE_LOG(LogTemp, Warning, TEXT("DebugMenu: Starting wild battle with %s (Level %d)!"),
			*SpeciesData->SpeciesName.ToString(), Level);

		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: BP_BattleManager doesn't have SpawnWildCreature function!"));
		return false;
	}
}

bool UGF_DebugMenuSubsystem::StartRandomWildBattle(int32 MinLevel, int32 MaxLevel)
{
	// Get random Creature
	UGF_CreatureSpeciesData* RandomCreature = GetRandomCreatureSpecies();
	if (!RandomCreature)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Failed to get random Creature!"));
		return false;
	}

	// Random level in range
	int32 RandomLevel = FMath::RandRange(MinLevel, MaxLevel);

	UE_LOG(LogTemp, Log, TEXT("DebugMenu: Starting random battle: %s (Level %d)"),
		*RandomCreature->SpeciesName.ToString(), RandomLevel);

	return StartWildBattle(RandomCreature, RandomLevel);
}

//--------------------
// HELPER FUNCTIONS
//--------------------

UGF_CreatureSpeciesData* UGF_DebugMenuSubsystem::FindCreatureByName(const FString& SpeciesName)
{
	// Used to call GetAllCreatureSpecies() and linear-search it, so finding one
	// species loaded all 118. The manager resolves a single asset by name.
	UGameInstance* GI = GetGameInstance();
	UGF_CreatureManagerSubsystem* Manager = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
	if (!Manager)
	{
		return nullptr;
	}

	if (UGF_CreatureSpeciesData* Exact = Manager->GetCreatureSpeciesData(FName(*SpeciesName)))
	{
		return Exact;
	}

	// FName comparison is already case-insensitive, so reaching here means the name
	// genuinely is not in the index.
	UE_LOG(LogTemp, Warning, TEXT("DebugMenu: Could not find Creature with name '%s'"), *SpeciesName);
	return nullptr;
}

UGF_CreatureSpeciesData* UGF_DebugMenuSubsystem::GetRandomCreatureSpecies()
{
	const TArray<FName> Names = GetAllCreatureSpeciesNames();
	if (Names.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: No Creature species found!"));
		return nullptr;
	}

	UGameInstance* GI = GetGameInstance();
	UGF_CreatureManagerSubsystem* Manager = GI ? GI->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
	if (!Manager)
	{
		return nullptr;
	}

	// Pick the name first, then load one asset -- not load 118 and then pick.
	return Manager->GetCreatureSpeciesData(Names[FMath::RandRange(0, Names.Num() - 1)]);
}

//--------------------
// CONSOLE COMMAND HANDLERS
//--------------------

void UGF_DebugMenuSubsystem::OpenDebugMenuCommand()
{
	OpenDebugMenu();
}

void UGF_DebugMenuSubsystem::GiveCreatureCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("DebugMenu: Usage: gf.givecreature <SpeciesName> <Level>"));
		return;
	}

	FString SpeciesName = Args[0];
	int32 Level = (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : 5;

	// Clamp level
	Level = FMath::Clamp(Level, 1, 100);

	// Find Creature
	UGF_CreatureSpeciesData* SpeciesData = FindCreatureByName(SpeciesName);
	if (!SpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Creature '%s' not found!"), *SpeciesName);
		return;
	}

	// Give Creature
	GiveCreature(SpeciesData, Level);
}

void UGF_DebugMenuSubsystem::WildBattleCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("DebugMenu: Usage: gf.wildbattle <SpeciesName> <Level>"));
		return;
	}

	FString SpeciesName = Args[0];
	int32 Level = (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : 5;

	// Clamp level
	Level = FMath::Clamp(Level, 1, 100);

	// Find Creature
	UGF_CreatureSpeciesData* SpeciesData = FindCreatureByName(SpeciesName);
	if (!SpeciesData)
	{
		UE_LOG(LogTemp, Error, TEXT("DebugMenu: Creature '%s' not found!"), *SpeciesName);
		return;
	}

	// Start battle
	StartWildBattle(SpeciesData, Level);
}