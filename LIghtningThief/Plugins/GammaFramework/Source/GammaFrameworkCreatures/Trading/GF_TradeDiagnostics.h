// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureInstanceData.h"
#include "GF_TradeDiagnostics.generated.h"

/**
 * Outcome of a diagnostic run. Built to be readable both in the log and in a
 * debug-menu widget, so Report is pre-formatted and Failures is the same
 * information split per line for list widgets.
 */
USTRUCT(BlueprintType)
struct GAMMAFRAMEWORKCREATURES_API FGF_CodecTestResult
{
    GENERATED_BODY()

    // True only if every check passed. Any failure at all makes this false.
    UPROPERTY(BlueprintReadOnly, Category = "Trading|Diagnostics")
    bool bPassed = false;

    UPROPERTY(BlueprintReadOnly, Category = "Trading|Diagnostics")
    int32 ChecksRun = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Trading|Diagnostics")
    int32 ChecksFailed = 0;

    // One entry per failed check, already phrased for display.
    UPROPERTY(BlueprintReadOnly, Category = "Trading|Diagnostics")
    TArray<FString> Failures;

    // Multi-line human-readable summary of the whole run.
    UPROPERTY(BlueprintReadOnly, Category = "Trading|Diagnostics")
    FString Report;
};

/**
 * Diagnostics for the link-trade wire codec.
 *
 * These exist because compiling proves the codec's API usage is right, not that
 * a Creature survives a round trip intact. A field that silently fails to travel
 * means a player loses data in a trade and nothing errors -- the single worst
 * failure mode this feature has, and one no compiler catches.
 *
 * Field comparison is driven by reflection rather than a hand-written field
 * list. That is deliberate: FGF_CreatureInstanceData's hand-written copy constructor
 * already demonstrated that manual field lists rot silently as the struct grows.
 * A reflection walk covers every field automatically, including ones added after
 * this file was written.
 *
 * Nothing here ships to players -- wire it into the debug menu.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_TradeDiagnostics : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Encode one Creature, decode it back, and compare every reflected property.
     *
     * UniqueID is expected to differ and is reported as an intentional change
     * rather than a failure: a traded Creature must not reuse the sender's
     * identity, or both saves end up holding the same one.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading|Diagnostics", meta = (DisplayName = "Run Codec Round Trip Test"))
    static FGF_CodecTestResult RunCodecRoundTripTest(const FGF_CreatureInstanceData& Creature);

    /**
     * Round-trip every Creature currently in the player's party.
     *
     * More useful than a synthetic case because real party members carry the
     * awkward states a hand-built test would not think to produce: downed, part
     * Uses, held items, eggs, nicknamed, traded-in with a foreign OT.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading|Diagnostics",
        meta = (DisplayName = "Run Codec Party Test", WorldContext = "WorldContextObject"))
    static FGF_CodecTestResult RunCodecPartyTest(const UObject* WorldContextObject);

    /**
     * Confirm the codec REJECTS input it should reject.
     *
     * The round-trip test only proves good data survives. Since the relay is a
     * dumb mailbox that never inspects payloads, this validator is the only
     * thing between a hand-edited blob and the player's save, so "does it say no
     * when it should" is the half that actually protects anyone.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading|Diagnostics", meta = (DisplayName = "Run Codec Tamper Test"))
    static FGF_CodecTestResult RunCodecTamperTest(const FGF_CreatureInstanceData& Creature);

    /** Party round trip plus tamper resistance, merged into one result. */
    UFUNCTION(BlueprintCallable, Category = "Trading|Diagnostics",
        meta = (DisplayName = "Run All Codec Tests", WorldContext = "WorldContextObject"))
    static FGF_CodecTestResult RunAllCodecTests(const UObject* WorldContextObject);

private:
    /** Reflection walk. Appends a line to OutDifferences for each field that differs. */
    static void CompareAllProperties(
        const FGF_CreatureInstanceData& Original,
        const FGF_CreatureInstanceData& Decoded,
        TArray<FString>& OutDifferences,
        int32& OutFieldsCompared);

    static void Accumulate(FGF_CodecTestResult& Target, const FGF_CodecTestResult& Source);
};
