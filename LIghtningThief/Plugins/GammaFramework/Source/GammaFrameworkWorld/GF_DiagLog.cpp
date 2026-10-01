#include "GF_DiagLog.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection GDiagLock;
	TUniquePtr<FArchive> GDiagFile;
	bool bGDiagOpenAttempted = false;

	// Serialize as UTF-8 with no BOM. Everything written here is ASCII by convention (see the
	// note in the header about log formatting), but going through the converter means a stray
	// non-ASCII character in a Creature or tamer name cannot corrupt the file.
	void WriteUtf8(FArchive& Ar, const FString& Text)
	{
		FTCHARToUTF8 Converted(*Text);
		Ar.Serialize(const_cast<ANSICHAR*>(Converted.Get()), Converted.Length());
	}
}

FString FGF_DiagLog::GetLogPath()
{
	return FPaths::Combine(FPaths::ProjectLogDir(), TEXT("GammaFramework-Diagnostics.log"));
}

void FGF_DiagLog::Write(const FString& Line)
{
	FScopeLock Lock(&GDiagLock);

	if (!bGDiagOpenAttempted)
	{
		// Only ever tried once. If the directory is read-only (some players install under
		// Program Files) every later call turns into a cheap null check rather than a
		// failed file open per line.
		bGDiagOpenAttempted = true;

		const FString Path = GetLogPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);

		// Fresh file each session rather than append, matching how the engine's own log
		// behaves -- a tester sends the log for the run that went wrong, and an
		// ever-growing file would eventually be too big to send at all.
		GDiagFile.Reset(IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_AllowRead));

		if (GDiagFile)
		{
			WriteUtf8(*GDiagFile, FString::Printf(
				TEXT("=== Gamma Framework diagnostics | session started %s ===\r\n"),
				*FDateTime::Now().ToString()));
			GDiagFile->Flush();
		}
	}

	if (!GDiagFile)
	{
		return;
	}

	WriteUtf8(*GDiagFile, FString::Printf(TEXT("[%s] %s\r\n"),
		*FDateTime::Now().ToString(TEXT("%H:%M:%S")), *Line));

	// Flushed per line on purpose. These sites are rare (a few dozen a session), and the
	// logs that matter most are the ones written immediately before a crash or a hang --
	// exactly the ones a buffer would lose.
	GDiagFile->Flush();
}

void FGF_DiagLog::Shutdown()
{
	FScopeLock Lock(&GDiagLock);

	if (GDiagFile)
	{
		GDiagFile->Flush();
		GDiagFile->Close();
		GDiagFile.Reset();
	}
}
