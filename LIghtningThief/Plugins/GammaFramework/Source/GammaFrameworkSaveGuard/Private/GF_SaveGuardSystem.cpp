#include "GF_SaveGuardSystem.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AES.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <fileapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY(LogGammaFrameworkSaveGuard);

namespace
{
	/**
	 * Dot-prefixed so it is hidden on Mac/Linux by convention, and given the
	 * Windows hidden attribute on creation. It sits under ProjectSavedDir, which
	 * is the project's own Saved/ in the editor and
	 * %LOCALAPPDATA%\GammaFramework\Saved\ once packaged -- so editor testing and
	 * a player's real save stay on separate files without any extra work.
	 */
	const TCHAR* GVaultFolder = TEXT(".ged");

	/** Mixed into the file name hash so the names are not a plain MD5 of the slot. */
	const TCHAR* GNameSalt = TEXT("ge-vault-2f81c4");

	const uint32 GVaultMagic   = 0x31534547; // 'GES1'
	const uint32 GVaultVersion = 1;

	/** magic(4) + version(4) + payload size(4) + SHA-1(20) */
	const int32 GVaultHeaderSize = 32;
	const int32 GVaultHashOffset = 12;

	/**
	 * Saves written by a build from before this system existed are read once and
	 * rewritten into the vault, so the early-access testers keep their runs. Safe
	 * to turn off after everyone has been through one patch.
	 */
	constexpr bool bMigrateLegacySaves = true;

	void MarkPathHidden(const FString& InPath)
	{
#if PLATFORM_WINDOWS
		FString FullPath = FPaths::ConvertRelativePathToFull(InPath);
		FPaths::RemoveDuplicateSlashes(FullPath);
		// SetFileAttributes rejects a trailing separator on a directory.
		while (FullPath.EndsWith(TEXT("/")) || FullPath.EndsWith(TEXT("\\")))
		{
			FullPath.LeftChopInline(1);
		}
		// FILE_ATTRIBUTE_DIRECTORY cannot be set or cleared and is ignored here.
		::SetFileAttributesW(*FullPath, FILE_ATTRIBUTE_HIDDEN);
#endif
	}

	/**
	 * Where saves actually live: tied to the USER, not the install.
	 *
	 * Deliberately NOT FPaths::ProjectSavedDir(). For a staged build that
	 * resolves to <BuildFolder>/<Project>/Saved/, so shipping each patch as its
	 * own folder ("Patch_1.0.5", "Patch_1.0.6", ...) handed every player a fresh
	 * empty save directory and no Continue button. This path does not move when
	 * the build folder does, so a save made in 1.0.5 is still there in 1.0.6.
	 */
	const FString& GetVaultDir()
	{
		static const FString VaultDir = []()
		{
			FString Root = FString(FPlatformProcess::UserSettingsDir());
			if (Root.IsEmpty())
			{
				// No user dir on this platform. Falling back to the install-relative
				// path restores the old broken-across-patches behaviour, but that
				// beats not saving at all.
				Root = FPaths::ProjectSavedDir();
			}
			else
			{
				Root = Root / FApp::GetProjectName() / TEXT("Saved");
			}

			const FString Dir = Root / GVaultFolder / TEXT("");
			IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
			MarkPathHidden(Dir);
			return Dir;
		}();
		return VaultDir;
	}

	/** Where the vault used to sit: inside the build folder. Read for migration only. */
	const FString& GetInstallVaultDir()
	{
		static const FString Dir = FPaths::ProjectSavedDir() / GVaultFolder / TEXT("");
		return Dir;
	}

	/**
	 * Split across two arrays plus an index term so the 32 key bytes never appear
	 * as one contiguous run in the shipped executable. It raises the effort of
	 * finding the key; it does not make finding it impossible.
	 */
	const FAES::FAESKey& GetVaultKey()
	{
		static const FAES::FAESKey Key = []()
		{
			static const uint8 A[FAES::FAESKey::KeySize] = {
				0x4C, 0xD1, 0x27, 0x93, 0x6A, 0xBE, 0x05, 0xF7,
				0x38, 0x9C, 0xE2, 0x41, 0x7D, 0x0B, 0xA6, 0x54,
				0xC3, 0x19, 0x8F, 0x62, 0xD5, 0x2A, 0x74, 0xEB,
				0x16, 0xB0, 0x5D, 0xA9, 0x3E, 0x87, 0xF1, 0x2C,
			};
			static const uint8 B[FAES::FAESKey::KeySize] = {
				0x9A, 0x35, 0xC8, 0x1E, 0x77, 0x02, 0xAD, 0x5B,
				0xE4, 0x60, 0x11, 0xCF, 0x8A, 0x36, 0x59, 0xD2,
				0x0E, 0xB7, 0x43, 0x25, 0x9C, 0x68, 0xF0, 0x17,
				0xA1, 0x4F, 0xD8, 0x3B, 0x72, 0xE6, 0x05, 0xBC,
			};

			FAES::FAESKey Out;
			for (int32 Index = 0; Index < FAES::FAESKey::KeySize; ++Index)
			{
				Out.Key[Index] = uint8(A[Index] ^ B[Index] ^ uint8(0xA7 + Index * 31));
			}
			return Out;
		}();
		return Key;
	}
}

// ============================================================
// PATHS
// ============================================================

FString FGF_SaveGuardSystem::VaultFileName(const TCHAR* Name, const int32 UserIndex)
{
	// One-way name. The real slot name travels inside the encrypted payload, so
	// GetSaveGameNames can still answer without the directory listing spelling
	// out "CreatureSaveSlot" for anyone who opens the folder.
	const FString Seed = FString::Printf(TEXT("%s|%d|%s"), Name, UserIndex, GNameSalt);
	return FMD5::HashAnsiString(*Seed) + TEXT(".dat");
}

FString FGF_SaveGuardSystem::GetVaultPath(const TCHAR* Name, const int32 UserIndex)
{
	return GetVaultDir() + VaultFileName(Name, UserIndex);
}

FString FGF_SaveGuardSystem::GetInstallVaultPath(const TCHAR* Name, const int32 UserIndex)
{
	return GetInstallVaultDir() + VaultFileName(Name, UserIndex);
}

FString FGF_SaveGuardSystem::GetLegacyPath(const TCHAR* Name)
{
	return FString::Printf(TEXT("%sSaveGames/%s.sav"), *FPaths::ProjectSavedDir(), Name);
}

FString FGF_SaveGuardSystem::GetSaveGamePath(const TCHAR* Name)
{
	return GetVaultPath(Name, 0);
}

// ============================================================
// FILE FORMAT
// ============================================================

bool FGF_SaveGuardSystem::Encode(const FString& SlotName, const TArray<uint8>& Plain, TArray<uint8>& OutFile)
{
	// Payload = [name len][name utf8][data len][data]
	FTCHARToUTF8 NameUtf8(*SlotName);
	const uint32 NameLen = uint32(NameUtf8.Length());
	const uint32 DataLen = uint32(Plain.Num());

	TArray<uint8> Payload;
	Payload.Reserve(8 + int32(NameLen) + Plain.Num() + int32(FAES::AESBlockSize));
	Payload.Append(reinterpret_cast<const uint8*>(&NameLen), 4);
	Payload.Append(reinterpret_cast<const uint8*>(NameUtf8.Get()), int32(NameLen));
	Payload.Append(reinterpret_cast<const uint8*>(&DataLen), 4);
	Payload.Append(Plain.GetData(), Plain.Num());

	const uint32 PayloadSize = uint32(Payload.Num());

	uint8 Hash[20];
	FSHA1::HashBuffer(Payload.GetData(), uint64(Payload.Num()), Hash);

	// AES only works on whole blocks. Pad up and keep the true length in the
	// header so Decode can trim the padding back off.
	const int32 Padded = Align(Payload.Num(), int32(FAES::AESBlockSize));
	Payload.SetNumZeroed(Padded);
	FAES::EncryptData(Payload.GetData(), uint64(Padded), GetVaultKey());

	OutFile.Reset(GVaultHeaderSize + Padded);
	OutFile.Append(reinterpret_cast<const uint8*>(&GVaultMagic), 4);
	OutFile.Append(reinterpret_cast<const uint8*>(&GVaultVersion), 4);
	OutFile.Append(reinterpret_cast<const uint8*>(&PayloadSize), 4);
	OutFile.Append(Hash, 20);
	OutFile.Append(Payload.GetData(), Padded);
	return true;
}

bool FGF_SaveGuardSystem::Decode(const TArray<uint8>& FileBytes, FString& OutSlotName, TArray<uint8>& OutPlain)
{
	if (FileBytes.Num() < GVaultHeaderSize)
	{
		return false;
	}

	uint32 Magic = 0;
	uint32 Version = 0;
	uint32 PayloadSize = 0;
	FMemory::Memcpy(&Magic, FileBytes.GetData(), 4);
	FMemory::Memcpy(&Version, FileBytes.GetData() + 4, 4);
	FMemory::Memcpy(&PayloadSize, FileBytes.GetData() + 8, 4);

	if (Magic != GVaultMagic || Version != GVaultVersion)
	{
		return false;
	}

	const int32 Padded = FileBytes.Num() - GVaultHeaderSize;
	if (Padded <= 0 || (Padded % int32(FAES::AESBlockSize)) != 0 || int32(PayloadSize) > Padded)
	{
		return false;
	}

	TArray<uint8> Payload;
	Payload.Append(FileBytes.GetData() + GVaultHeaderSize, Padded);
	FAES::DecryptData(Payload.GetData(), uint64(Padded), GetVaultKey());
	Payload.SetNum(int32(PayloadSize));

	uint8 Hash[20];
	FSHA1::HashBuffer(Payload.GetData(), uint64(Payload.Num()), Hash);
	if (FMemory::Memcmp(Hash, FileBytes.GetData() + GVaultHashOffset, 20) != 0)
	{
		// Edited, truncated, or written with a different key. Refusing here beats
		// feeding a half-valid archive into USaveGame serialisation and crashing
		// somewhere much further along with no clue why.
		return false;
	}

	int32 Offset = 0;
	auto ReadU32 = [&Payload, &Offset](uint32& Out) -> bool
	{
		if (Offset + 4 > Payload.Num())
		{
			return false;
		}
		FMemory::Memcpy(&Out, Payload.GetData() + Offset, 4);
		Offset += 4;
		return true;
	};

	uint32 NameLen = 0;
	if (!ReadU32(NameLen) || Offset + int32(NameLen) > Payload.Num())
	{
		return false;
	}
	FUTF8ToTCHAR NameConv(reinterpret_cast<const UTF8CHAR*>(Payload.GetData() + Offset), int32(NameLen));
	OutSlotName = FString(NameConv.Length(), NameConv.Get());
	Offset += int32(NameLen);

	uint32 DataLen = 0;
	if (!ReadU32(DataLen) || Offset + int32(DataLen) > Payload.Num())
	{
		return false;
	}
	OutPlain.Reset(int32(DataLen));
	OutPlain.Append(Payload.GetData() + Offset, int32(DataLen));
	return true;
}

// ============================================================
// ISaveGameSystem
// ============================================================

bool FGF_SaveGuardSystem::SaveGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex, const TArray<uint8>& Data)
{
	TArray<uint8> FileBytes;
	if (!Encode(Name, Data, FileBytes))
	{
		UE_LOG(LogGammaFrameworkSaveGuard, Error, TEXT("Could not encode save slot '%s'."), Name);
		return false;
	}

	const FString Path = GetVaultPath(Name, UserIndex);

	// Write beside the real file and move it into place. A crash or power cut
	// part-way through a direct write would leave a truncated vault file, and
	// Decode rejects those wholesale -- that is a lost playthrough, not a lost
	// save. The move is the only step that can be interrupted, and it is atomic.
	const FString TempPath = Path + TEXT(".w");
	if (!FFileHelper::SaveArrayToFile(FileBytes, *TempPath))
	{
		UE_LOG(LogGammaFrameworkSaveGuard, Error, TEXT("Could not write save slot '%s'."), Name);
		return false;
	}

	IFileManager::Get().Delete(*Path, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
	if (!IFileManager::Get().Move(*Path, *TempPath, /*Replace=*/true, /*EvenIfReadOnly=*/true))
	{
		IFileManager::Get().Delete(*TempPath, false, true, true);
		UE_LOG(LogGammaFrameworkSaveGuard, Error, TEXT("Could not commit save slot '%s'."), Name);
		return false;
	}

	MarkPathHidden(Path);
	return true;
}

bool FGF_SaveGuardSystem::LoadGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex, TArray<uint8>& Data)
{
	TArray<uint8> FileBytes;

	if (FFileHelper::LoadFileToArray(FileBytes, *GetVaultPath(Name, UserIndex)))
	{
		FString StoredName;
		if (Decode(FileBytes, StoredName, Data))
		{
			return true;
		}

		UE_LOG(LogGammaFrameworkSaveGuard, Error,
			TEXT("Save slot '%s' exists but failed its integrity check - reporting it as missing."), Name);
		return false;
	}

	// A vault file sitting in the build folder: written by the first shipped
	// version of this system, before saves moved to the user directory. Lift it
	// out so the player keeps it across the next patch.
	const FString InstallVaultPath = GetInstallVaultPath(Name, UserIndex);
	if (FFileHelper::LoadFileToArray(FileBytes, *InstallVaultPath))
	{
		FString StoredName;
		if (Decode(FileBytes, StoredName, Data))
		{
			if (SaveGame(false, Name, UserIndex, Data))
			{
				IFileManager::Get().Delete(*InstallVaultPath, false, true, true);
				UE_LOG(LogGammaFrameworkSaveGuard, Log,
					TEXT("Moved save slot '%s' out of the build folder and into the user directory."), Name);
			}
			return true;
		}
	}

	if (bMigrateLegacySaves)
	{
		const FString LegacyPath = GetLegacyPath(Name);
		if (FFileHelper::LoadFileToArray(FileBytes, *LegacyPath))
		{
			// LoadFileToArray returns TRUE for a zero-byte file, with an empty array.
			// Migrating that produced a vault entry with a perfectly valid header and
			// no payload, and from then on the slot answered "I exist" to
			// DoesSaveGameExist forever while LoadGameFromSlot returned null every
			// single time - a permanently unreadable save that looks present. The
			// legacy file was deleted in the same breath, so there was nothing left to
			// recover from. An empty or header-less file is not a save; leave it alone
			// and report the slot as missing so the game takes its "no save" path.
			if (FileBytes.Num() == 0)
			{
				UE_LOG(LogGammaFrameworkSaveGuard, Warning,
					TEXT("Legacy save '%s' is 0 bytes - NOT migrating it. Leaving the file in place "
					     "and reporting the slot as missing."), Name);
				return false;
			}

			Data = MoveTemp(FileBytes);

			// Only drop the old file once the vault copy is safely down AND reads back.
			// Deleting on the strength of the write alone is what made a bad migration
			// unrecoverable.
			if (SaveGame(false, Name, UserIndex, Data))
			{
				TArray<uint8> Verify;
				TArray<uint8> VerifyBytes;
				FString VerifyName;

				const bool bReadsBack =
					FFileHelper::LoadFileToArray(VerifyBytes, *GetVaultPath(Name, UserIndex))
					&& Decode(VerifyBytes, VerifyName, Verify)
					&& Verify == Data;

				if (bReadsBack)
				{
					IFileManager::Get().Delete(*LegacyPath, false, true, true);
					UE_LOG(LogGammaFrameworkSaveGuard, Log, TEXT("Migrated save slot '%s' into the vault."), Name);
				}
				else
				{
					UE_LOG(LogGammaFrameworkSaveGuard, Error,
						TEXT("Migrated '%s' into the vault but it did not read back - KEEPING the "
						     "legacy file so the run is not lost."), Name);
				}
			}
			return true;
		}
	}

	return false;
}

ISaveGameSystem::ESaveExistsResult FGF_SaveGuardSystem::DoesSaveGameExistWithResult(const TCHAR* Name, const int32 UserIndex)
{
	// Header check only. Verifying the SHA-1 here would mean decrypting the whole
	// box save every time a menu asks whether a save exists; a bad file is caught
	// on the load that follows.
	if (IFileManager::Get().FileSize(*GetVaultPath(Name, UserIndex)) >= GVaultHeaderSize)
	{
		return ESaveExistsResult::OK;
	}

	// Both of these are migration sources rather than live locations, but the
	// Continue button asks this question before anything calls LoadGame, so they
	// have to count as "a save exists" or the player is offered New Game only.
	if (IFileManager::Get().FileSize(*GetInstallVaultPath(Name, UserIndex)) >= GVaultHeaderSize)
	{
		return ESaveExistsResult::OK;
	}

	// Strictly greater than zero. FileSize returns -1 for "not there" and 0 for an
	// empty file, and >= 0 counted the empty one as a save - so an abandoned 0-byte
	// .sav made the whole slot report as present while nothing could ever load it.
	if (bMigrateLegacySaves && IFileManager::Get().FileSize(*GetLegacyPath(Name)) > 0)
	{
		return ESaveExistsResult::OK;
	}

	return ESaveExistsResult::DoesNotExist;
}

bool FGF_SaveGuardSystem::DoesSaveGameExist(const TCHAR* Name, const int32 UserIndex)
{
	return DoesSaveGameExistWithResult(Name, UserIndex) == ESaveExistsResult::OK;
}

bool FGF_SaveGuardSystem::DeleteGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex)
{
	const bool bDeletedVault = IFileManager::Get().Delete(*GetVaultPath(Name, UserIndex), false, true, true);

	// New Game has to clear every migration source too, or the old run reappears
	// the next time that slot is read.
	const bool bDeletedInstall = IFileManager::Get().Delete(*GetInstallVaultPath(Name, UserIndex), false, true, true);

	bool bDeletedLegacy = false;
	if (bMigrateLegacySaves)
	{
		bDeletedLegacy = IFileManager::Get().Delete(*GetLegacyPath(Name), false, true, true);
	}

	return bDeletedVault || bDeletedInstall || bDeletedLegacy;
}

bool FGF_SaveGuardSystem::GetSaveGameNames(TArray<FString>& FoundSaves, const int32 UserIndex)
{
	// The file names are hashes, so the only way to answer this is to open each
	// one and read the slot name back out of the payload.
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *GetVaultDir(), TEXT("*.dat"));

	for (const FString& File : FoundFiles)
	{
		TArray<uint8> FileBytes;
		if (!FFileHelper::LoadFileToArray(FileBytes, *(GetVaultDir() + File)))
		{
			continue;
		}

		FString StoredName;
		TArray<uint8> Ignored;
		if (Decode(FileBytes, StoredName, Ignored) && !StoredName.IsEmpty())
		{
			FoundSaves.AddUnique(StoredName);
		}
	}

	if (bMigrateLegacySaves)
	{
		TArray<FString> LegacyFiles;
		IFileManager::Get().FindFiles(LegacyFiles, *(FPaths::ProjectSavedDir() / TEXT("SaveGames/")), TEXT("*.sav"));
		for (const FString& File : LegacyFiles)
		{
			FoundSaves.AddUnique(FPaths::GetBaseFilename(File));
		}
	}

	return true;
}

// ============================================================
// SELF TEST
// ============================================================

#if !UE_BUILD_SHIPPING
void FGF_SaveGuardSystem::RunSelfTest()
{
	const TCHAR* TestSlot = TEXT("GEVaultSelfTest");
	FGF_SaveGuardSystem System;

	TArray<uint8> Written;
	Written.SetNumUninitialized(1000);
	for (int32 Index = 0; Index < Written.Num(); ++Index)
	{
		Written[Index] = uint8(Index * 7 + 13);
	}

	const FString Path = GetVaultPath(TestSlot, 0);
	UE_LOG(LogGammaFrameworkSaveGuard, Display, TEXT("SelfTest: vault path is %s"), *Path);

	bool bOk = true;
	auto Check = [&bOk](bool bCondition, const TCHAR* What)
	{
		UE_LOG(LogGammaFrameworkSaveGuard, Display, TEXT("SelfTest: %-24s %s"), What, bCondition ? TEXT("PASS") : TEXT("FAIL"));
		bOk &= bCondition;
	};

	Check(System.SaveGame(false, TestSlot, 0, Written), TEXT("save"));
	Check(System.DoesSaveGameExist(TestSlot, 0), TEXT("exists"));

	TArray<uint8> ReadBack;
	Check(System.LoadGame(false, TestSlot, 0, ReadBack), TEXT("load"));
	Check(ReadBack == Written, TEXT("round trip matches"));

	// The bytes on disk must not be the bytes we handed in.
	TArray<uint8> OnDisk;
	FFileHelper::LoadFileToArray(OnDisk, *Path);
	Check(OnDisk.Num() > 0, TEXT("file readable"));
	Check(FMemory::Memcmp(OnDisk.GetData() + GVaultHeaderSize, Written.GetData(),
		FMath::Min(Written.Num(), FMath::Max(0, OnDisk.Num() - GVaultHeaderSize))) != 0, TEXT("payload encrypted"));

	TArray<FString> Names;
	System.GetSaveGameNames(Names, 0);
	Check(Names.Contains(TestSlot), TEXT("name listed"));

	// Flip one byte of ciphertext; the SHA-1 must catch it. The delete first is
	// not incidental: Windows refuses an overwrite of a file that already
	// carries FILE_ATTRIBUTE_HIDDEN unless the create call repeats the
	// attribute, which FFileHelper does not do.
	TArray<uint8> Tampered = OnDisk;
	if (Tampered.Num() > GVaultHeaderSize)
	{
		Tampered[GVaultHeaderSize] ^= 0xFF;
		IFileManager::Get().Delete(*Path, false, true, true);
		Check(FFileHelper::SaveArrayToFile(Tampered, *Path), TEXT("tamper written"));

		TArray<uint8> Ignored;
		Check(!System.LoadGame(false, TestSlot, 0, Ignored), TEXT("tamper rejected"));
	}

	Check(System.DeleteGame(false, TestSlot, 0), TEXT("delete"));
	Check(!System.DoesSaveGameExist(TestSlot, 0), TEXT("gone after delete"));

	UE_LOG(LogGammaFrameworkSaveGuard, Display, TEXT("SelfTest: OVERALL %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
}

static FAutoConsoleCommand GVaultSelfTestCmd(
	TEXT("gf.SaveVaultSelfTest"),
	TEXT("Round-trips a throwaway slot through the save vault and logs the result."),
	FConsoleCommandDelegate::CreateStatic(&FGF_SaveGuardSystem::RunSelfTest));
#endif
