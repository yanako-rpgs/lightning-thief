#pragma once

#include "CoreMinimal.h"
#include "SaveGameSystem.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGammaFrameworkSaveGuard, Log, All);

/**
 * Gamma Framework's save game system.
 *
 * Every save path in the engine -- UGameplayStatics::SaveGameToSlot, the
 * Blueprint SaveGameToSlot/LoadGameFromSlot/DoesSaveGameExist/DeleteGameInSlot
 * nodes, and the async variants -- goes through
 * IPlatformFeaturesModule::Get().GetSaveGameSystem(). Replacing that one object
 * moves ALL of them at once, which is why none of the five subsystems that save
 * (boxes, quests, berries, trade, options) nor the seven Blueprints that call
 * the save nodes needed a single edit.
 *
 * Two changes versus FGenericSaveGameSystem:
 *
 *   1. Files land in a hidden subfolder of the save directory under hashed
 *      names, so "Saved/SaveGames/CreatureSaveSlot.sav" no longer sits in plain
 *      sight next to a .log the player was told to go find.
 *   2. Contents are AES-256 encrypted with a SHA-1 integrity check, so the file
 *      is not a hex-editable party list and a tampered one is refused instead
 *      of half-deserialised.
 *
 * This is obfuscation, NOT security. The key ships inside the executable
 * because the game has to decrypt on the player's own machine; anyone willing
 * to disassemble it can recover the key. What this actually buys is that saves
 * stop being casually findable, editable, and swappable between players.
 */
class FGF_SaveGuardSystem : public FGenericSaveGameSystem
{
public:
	virtual bool DoesSaveGameExist(const TCHAR* Name, const int32 UserIndex) override;
	virtual ESaveExistsResult DoesSaveGameExistWithResult(const TCHAR* Name, const int32 UserIndex) override;
	virtual bool GetSaveGameNames(TArray<FString>& FoundSaves, const int32 UserIndex) override;
	virtual bool SaveGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex, const TArray<uint8>& Data) override;
	virtual bool LoadGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex, TArray<uint8>& Data) override;
	virtual bool DeleteGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex) override;

#if !UE_BUILD_SHIPPING
	/**
	 * Round-trips a throwaway slot through the whole path -- write, exists,
	 * read back, tamper, delete -- and logs the result. Exposed as the console
	 * command "gf.SaveVaultSelfTest". Not built into Shipping, so a player
	 * cannot use it to confirm what the format is.
	 */
	static void RunSelfTest();
#endif

protected:
	/**
	 * The base class routes its own helpers through this. Overriding it keeps
	 * anything we did not override from writing to the old visible location.
	 */
	virtual FString GetSaveGamePath(const TCHAR* Name) override;

private:
	/** Hashed file name for a slot, without any directory. */
	static FString VaultFileName(const TCHAR* Name, const int32 UserIndex);

	/** Full path to the vault file backing a slot. Lives with the user, not the install. */
	static FString GetVaultPath(const TCHAR* Name, const int32 UserIndex);

	/** Vault path inside the build folder, as shipped before the move. Migration only. */
	static FString GetInstallVaultPath(const TCHAR* Name, const int32 UserIndex);

	/** Where a pre-vault build would have put this slot. Used for migration only. */
	static FString GetLegacyPath(const TCHAR* Name);

	/** Plain save bytes -> on-disk vault file. */
	static bool Encode(const FString& SlotName, const TArray<uint8>& Plain, TArray<uint8>& OutFile);

	/** On-disk vault file -> plain save bytes. False means missing, foreign or tampered. */
	static bool Decode(const TArray<uint8>& FileBytes, FString& OutSlotName, TArray<uint8>& OutPlain);
};
