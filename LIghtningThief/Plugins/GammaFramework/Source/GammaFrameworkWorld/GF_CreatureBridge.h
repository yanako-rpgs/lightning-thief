#pragma once

#include "CoreMinimal.h"
#include "GF_GridWorldSubsystem.h"   // FGF_GridCoordinate
#include "GF_CreatureBridge.generated.h"

/**
 * The one-way valve between GammaFrameworkWorld and GammaFrameworkCreatures.
 *
 * World is the genre-agnostic half: grid movement, dialogue, quests, settings,
 * items, boot. Creatures is the collector half. Creatures depends on World;
 * World must never depend on Creatures, or the two cannot be used separately and
 * UBT rejects the circular module reference outright.
 *
 * A handful of World systems legitimately need to ask the creature layer a
 * question -- does the lead creature's trait change the encounter rate, what is
 * the player's gender, hand this creature over from a dialogue event. Every one
 * of those goes through a hook here.
 *
 * GammaFrameworkCreatures binds the hooks in its module startup. If the
 * Creatures module is absent or has not started yet, every hook is unbound and
 * the Get* accessors below return a harmless default -- World keeps working, it
 * simply behaves as though there are no creatures in the game. That is the
 * whole point: a project that wants only the grid/dialogue/quest tech can
 * disable GammaFrameworkCreatures and nothing here breaks.
 *
 * Do NOT add a hook for something Creatures could pull from World instead.
 * Creatures is allowed to call into World directly; only the reverse needs this.
 */

/**
 * Which avatar the player picked.
 *
 * Lives in the World module rather than with the save object that stores it, because the
 * dialogue system substitutes gendered pronouns and titles into text and must
 * be able to name this type without depending on the creature layer.
 */
UENUM(BlueprintType)
enum class EGF_PlayerGender : uint8
{
	Boy  UMETA(DisplayName = "Boy"),
	Girl UMETA(DisplayName = "Girl"),
};

/**
 * The subset of settings that gets mirrored into the creature save file.
 *
 * It lives here rather than in the vault because the settings subsystem is the
 * thing that owns these values; the save file is only a copy so that a loaded
 * game restores the player's options along with their party.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKWORLD_API FGF_SettingsMirror
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString TextSpeed;
	UPROPERTY(BlueprintReadOnly) int32   FrameID = 0;
	UPROPERTY(BlueprintReadOnly) FString Difficulty;
	UPROPERTY(BlueprintReadOnly) FString WindowedModes;
	UPROPERTY(BlueprintReadOnly) FString Resolution;
	UPROPERTY(BlueprintReadOnly) float   MasterVolume = 1.f;
	UPROPERTY(BlueprintReadOnly) float   SoundEffectsVolume = 1.f;
	UPROPERTY(BlueprintReadOnly) float   MusicVolume = 1.f;
	UPROPERTY(BlueprintReadOnly) bool    bEXPShareEnabled = false;
	UPROPERTY(BlueprintReadOnly) bool    bDOFEnabled = false;
	UPROPERTY(BlueprintReadOnly) bool    bAutoRepelEnabled = false;
};

struct GAMMAFRAMEWORKWORLD_API FGF_CreatureBridge
{
	// ── Grid ─────────────────────────────────────────────────────────────────

	/** Wild encounter rate multiplier from the lead creature's trait. 1.0 = no change. */
	static TFunction<float(const UObject* WorldContext)> GetEncounterRateMultiplier;

	/** Remember where the player entered a dungeon, so an escape rope can return them. */
	static TFunction<void(const UObject* WorldContext, FGF_GridCoordinate ReturnTile)> RegisterDungeonEntrance;
	static TFunction<void(const UObject* WorldContext)> ClearDungeonState;

	// ── Save lifecycle ───────────────────────────────────────────────────────

	static TFunction<void(const UObject* WorldContext)> DeleteCreatureSave;
	static TFunction<bool(const UObject* WorldContext)> LoadCreatureSave;

	// ── Player profile, stored inside the creature save ───────────────────────

	static TFunction<uint8(const UObject* WorldContext)> GetPlayerGender;
	static TFunction<int32(const UObject* WorldContext)> GetEmblemCount;
	static TFunction<void(const UObject* WorldContext, const FGF_SettingsMirror&)> PushSettingsMirror;

	// ── Dialogue ─────────────────────────────────────────────────────────────

	/** Give the player a creature by species name. Returns false if it could not be granted. */
	static TFunction<bool(const UObject* WorldContext, FName SpeciesName, int32 Level, int32& OutVaultPageIndex)> GiveCreature;
	static TFunction<FString(const UObject* WorldContext, int32 VaultPageIndex)> GetVaultPageName;

	// ── Safe accessors ───────────────────────────────────────────────────────
	// Prefer these at call sites: they fold the "is the hook bound" check into
	// the call, so World code never needs to know the Creatures module exists.

	static float   EncounterRateMultiplier(const UObject* WorldContext);
	static uint8   PlayerGender(const UObject* WorldContext);
	static int32   EmblemCount(const UObject* WorldContext);
	static bool    IsCreatureLayerPresent();
};
