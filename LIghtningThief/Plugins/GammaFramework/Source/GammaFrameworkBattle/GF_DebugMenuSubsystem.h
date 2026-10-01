// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_CreatureSpeciesData.h"
#include "GF_CreatureInstanceData.h"
#include "GF_TamerMaster.h"
#include "Sound/SoundBase.h"
#include "GF_ResettableState.h"
#include "GF_DebugMenuSubsystem.generated.h"

/**
 * Debug Menu Subsystem for testing Creature functionality
 *
 * Console Commands:
 * - gf.commandmenu : Opens the debug menu widget
 * - gf.givecreature <SpeciesName> <Level> : Gives a Creature directly
 * - gf.wildbattle <SpeciesName> <Level> : Starts a wild battle
 * - gf.grid [on|off|coords|walkable|occupied|slopes|range <N>] : Grid debug overlay
 */
UCLASS()
class GAMMAFRAMEWORKBATTLE_API UGF_DebugMenuSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// IGF_ResettableState - the open widget belongs to the outgoing world, and the
	// staged tamer battle would otherwise fire into the next run.
	virtual void ResetToBootState() override;

	//--------------------
	// DEBUG WIDGET
	//--------------------

	// Reference to the debug menu widget class
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Menu")
	TSubclassOf<UUserWidget> DebugMenuWidgetClass;

	// Current open debug menu widget instance
	UPROPERTY()
	UUserWidget* DebugMenuWidget;

	// Set the debug menu widget class (call this from your Game Instance Init event)
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	void SetDebugMenuWidgetClass(TSubclassOf<UUserWidget> InWidgetClass);

	// Open the debug menu
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	void OpenDebugMenu();

	// Close the debug menu
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	void CloseDebugMenu();

	// Toggle debug menu on/off
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	void ToggleDebugMenu();

	//--------------------
	// CREATURE OPERATIONS
	//--------------------

	/**
	 * Every species name, from the manager's registry index. Loads NOTHING.
	 * Build the debug picker from this and call FindCreatureByName on the one the
	 * user clicks. Sorted alphabetically.
	 */
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	TArray<FName> GetAllCreatureSpeciesNames();

	/**
	 * Get all Creature species data assets from the content folder.
	 *
	 * EXPENSIVE -- loads all 118 (~4.18 GB, since each hard-references its full
	 * sprite set) purely to populate a picker list. Use GetAllCreatureSpeciesNames()
	 * instead; this stays only so the existing debug widget keeps working until it
	 * is rewired, and it logs a warning when it runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Debug Menu", meta = (DeprecatedFunction, DeprecationMessage = "Use GetAllCreatureSpeciesNames() and resolve the selected entry with FindCreatureByName()."))
	TArray<UGF_CreatureSpeciesData*> GetAllCreatureSpecies();

	// Give a Creature to the player's party
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	bool GiveCreature(UGF_CreatureSpeciesData* SpeciesData, int32 Level = 5);

	// Start a wild battle with specified Creature
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	bool StartWildBattle(UGF_CreatureSpeciesData* SpeciesData, int32 Level = 5);

	// Start a wild battle with a random Creature
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	bool StartRandomWildBattle(int32 MinLevel = 2, int32 MaxLevel = 10);

	//--------------------
	// HELPER FUNCTIONS
	//--------------------

	// Find Creature species by name (e.g., "Budling")
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	UGF_CreatureSpeciesData* FindCreatureByName(const FString& SpeciesName);

	// Get a random Creature species
	UFUNCTION(BlueprintCallable, Category = "Debug Menu")
	UGF_CreatureSpeciesData* GetRandomCreatureSpecies();

	//--------------------
	// TAMER BATTLE STAGING
	// Written by C++ debugger UI, read by Debug_StartTamerBattle in BP_BattleManager
	//--------------------

	// The tamer blueprint class to fight
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	TSubclassOf<AGF_TamerMaster> PendingTamerClass;

	// AI difficulty override for the battle
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	EGF_TamerAIDifficulty PendingTamerDifficulty = EGF_TamerAIDifficulty::Smart;

	// Battle transition type � stored as uint8 because BattleTransition is a BP-only enum.
	// In BP, cast this to your EBattleTransition enum before passing to StartTamerBattle.
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	uint8 PendingBattleTransition = 0;

	// Battle map (soft ref � matched to how StartTamerBattle takes its Map pin)
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	TSoftObjectPtr<UWorld> PendingTamerMap;

	// Intro music track (nullptr = no intro)
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	TSoftObjectPtr<USoundBase> PendingTamerIntroSound;

	// Loop music track
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	TSoftObjectPtr<USoundBase> PendingTamerLoopSound;

	// Whether the OST has a separate intro clip
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	bool PendingTamerHasIntro = false;

	// Whether to fade in the music at battle start
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	bool PendingTamerFadeIn = false;

	// Fade duration in seconds
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	float PendingTamerFadeDuration = 0.f;

	// Fade target volume (0�1)
	UPROPERTY(BlueprintReadWrite, Category = "Debug|Battle")
	float PendingTamerFadeVolume = 1.f;

private:
	// Console command handlers
	void OpenDebugMenuCommand();
	void GiveCreatureCommand(const TArray<FString>& Args);
	void WildBattleCommand(const TArray<FString>& Args);

	// Names only. The old TArray<UGF_CreatureSpeciesData*> here held all 118 assets
	// resident for the lifetime of the game instance the first time the debug menu
	// was opened -- a full sprite library, kept alive by a debug picker.
	TArray<FName> CachedSpeciesNames;

	// Flag to check if we've scanned for Creature yet
	bool bHasScannedCreature = false;
};