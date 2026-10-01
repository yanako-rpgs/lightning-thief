#pragma once

#include "CoreMinimal.h"

/**
 * Diagnostic logging that survives a Shipping build.
 *
 * UE_LOG is compiled out entirely in Shipping. Verified against the binary: every one of
 * this project's log strings ("ILLEGAL MOVE", "Tamer %s had its ace in the lead slot",
 * "CreatureManager: Battle started") is present in the Development exe and absent from the
 * Shipping one. bUseLoggingInShipping would restore it, but it is marked
 * [RequiresUniqueBuildEnvironment] and this project builds against an installed engine --
 * the same wall that makes bUseGameplayDebugger silently do nothing.
 *
 * Since tester log forensics is how most bugs here actually get diagnosed, the forensic
 * log sites write through GF_DIAG instead. It writes to a file directly, so no build
 * configuration can strip it.
 *
 * This is deliberately NOT a replacement for UE_LOG. Use it only where the line would be
 * the thing you go looking for in a tester's log; routine chatter stays on UE_LOG so the
 * diagnostics file stays readable.
 *
 * Outside Shipping, GF_DIAG also emits to UE_LOG so the editor Output Log is unchanged.
 */
class GAMMAFRAMEWORKWORLD_API FGF_DiagLog
{
public:
	/** Append one line. Thread-safe. Silently does nothing if the file cannot be opened. */
	static void Write(const FString& Line);

	/** Full path of the diagnostics file, for pointing testers at it. */
	static FString GetLogPath();

	/** Close the file. Called on shutdown; safe to call more than once. */
	static void Shutdown();
};

#if UE_BUILD_SHIPPING
	#define GF_DIAG(Format, ...) \
		FGF_DiagLog::Write(FString::Printf(TEXT(Format), ##__VA_ARGS__))
#else
	#define GF_DIAG(Format, ...) \
		do \
		{ \
			const FString GF_DiagLine__ = FString::Printf(TEXT(Format), ##__VA_ARGS__); \
			FGF_DiagLog::Write(GF_DiagLine__); \
			UE_LOG(LogTemp, Log, TEXT("%s"), *GF_DiagLine__); \
		} while (0)
#endif
