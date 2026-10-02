// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_TradeDiagnostics.h"

#include "GF_CreatureCodec.h"
#include "GF_CreatureManagerSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTradeCodec, Log, All);

#if !UE_BUILD_SHIPPING
// Lets the codec be exercised from the PIE console with a real save loaded,
// before any trade UI or Blueprint wiring exists. Diagnostics only -- kept out
// of shipping builds so players cannot invoke it.
static FAutoConsoleCommandWithWorld GTestTradeCodecCommand(
    TEXT("GE.Trade.TestCodec"),
    TEXT("Round-trips every party Creature through the link-trade codec and runs tamper-resistance checks. Results go to the log."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        // RunAllCodecTests already logs the verdict and any failures.
        UGF_TradeDiagnostics::RunAllCodecTests(World);
    }));
#endif

namespace
{
    // Fields that are SUPPOSED to change across the wire. Anything not listed
    // here must survive a round trip byte for byte.
    bool IsIntentionallyRegenerated(const FProperty& Property)
    {
        // A traded Creature gets a fresh identity on arrival so the sender's copy
        // and the receiver's copy are never the same Creature.
        return Property.GetFName() == GET_MEMBER_NAME_CHECKED(FGF_CreatureInstanceData, UniqueID);
    }

    FString DescribeValue(const FProperty& Property, const void* Container)
    {
        FString Text;
        Property.ExportTextItem_Direct(
            Text,
            Property.ContainerPtrToValuePtr<void>(Container),
            /*DefaultValue=*/nullptr,
            /*Parent=*/nullptr,
            PPF_None);

        // Keep log lines and debug-menu rows readable when a soft asset path or
        // a move array expands to something enormous.
        constexpr int32 MaxShownChars = 120;
        if (Text.Len() > MaxShownChars)
        {
            Text = Text.Left(MaxShownChars) + TEXT("...");
        }

        return Text;
    }

    void AddFailure(FGF_CodecTestResult& Result, const FString& Message)
    {
        Result.Failures.Add(Message);
        ++Result.ChecksFailed;
    }

    // A tamper case passes when decoding FAILS. Anything that decodes cleanly
    // here is a hole in the validator.
    void ExpectRejected(
        FGF_CodecTestResult& Result,
        const FString& CaseName,
        const FString& Payload)
    {
        ++Result.ChecksRun;

        FGF_CreatureInstanceData Decoded;
        FString Error;
        const EGF_TradeCodecResult Outcome = UGF_CreatureCodec::DecodeCreature(Payload, Decoded, Error);

        if (Outcome == EGF_TradeCodecResult::Success)
        {
            AddFailure(Result, FString::Printf(
                TEXT("ACCEPTED a payload it should have rejected: %s"), *CaseName));
        }
    }
}

void UGF_TradeDiagnostics::CompareAllProperties(
    const FGF_CreatureInstanceData& Original,
    const FGF_CreatureInstanceData& Decoded,
    TArray<FString>& OutDifferences,
    int32& OutFieldsCompared)
{
    OutFieldsCompared = 0;

    for (TFieldIterator<FProperty> It(FGF_CreatureInstanceData::StaticStruct()); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property == nullptr)
        {
            continue;
        }

        if (IsIntentionallyRegenerated(*Property))
        {
            continue;
        }

        ++OutFieldsCompared;

        // ArrayDim covers C-style fixed arrays. The struct has none today, but
        // walking it costs nothing and stops this from quietly under-testing if
        // one is ever added.
        for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
        {
            const bool bMatches = Property->Identical_InContainer(
                &Original, &Decoded, ArrayIndex, PPF_DeepComparison);

            if (!bMatches)
            {
                const FString Label = Property->ArrayDim > 1
                    ? FString::Printf(TEXT("%s[%d]"), *Property->GetName(), ArrayIndex)
                    : Property->GetName();

                OutDifferences.Add(FString::Printf(
                    TEXT("%s: sent '%s', received '%s'"),
                    *Label,
                    *DescribeValue(*Property, &Original),
                    *DescribeValue(*Property, &Decoded)));
            }
        }
    }
}

FGF_CodecTestResult UGF_TradeDiagnostics::RunCodecRoundTripTest(const FGF_CreatureInstanceData& Creature)
{
    FGF_CodecTestResult Result;
    TArray<FString> Lines;

    const FString Label = Creature.GetDisplayName().ToString();
    Lines.Add(FString::Printf(TEXT("Round trip: %s (Lv %d)"), *Label, Creature.Level));

    // Encode.
    ++Result.ChecksRun;
    FString Encoded;
    FString Error;
    if (!UGF_CreatureCodec::EncodeCreature(Creature, Encoded, Error))
    {
        AddFailure(Result, FString::Printf(TEXT("%s: encode failed - %s"), *Label, *Error));
        Lines.Add(FString::Printf(TEXT("  ENCODE FAILED: %s"), *Error));
        Result.bPassed = false;
        Result.Report = FString::Join(Lines, TEXT("\n"));
        UE_LOG(LogTradeCodec, Error, TEXT("%s"), *Result.Report);
        return Result;
    }

    Lines.Add(FString::Printf(TEXT("  Encoded to %d characters."), Encoded.Len()));

    // Decode.
    ++Result.ChecksRun;
    FGF_CreatureInstanceData Decoded;
    const EGF_TradeCodecResult Outcome = UGF_CreatureCodec::DecodeCreature(Encoded, Decoded, Error);
    if (Outcome != EGF_TradeCodecResult::Success)
    {
        AddFailure(Result, FString::Printf(TEXT("%s: decode failed - %s"), *Label, *Error));
        Lines.Add(FString::Printf(TEXT("  DECODE FAILED: %s"), *Error));
        Result.bPassed = false;
        Result.Report = FString::Join(Lines, TEXT("\n"));
        UE_LOG(LogTradeCodec, Error, TEXT("%s"), *Result.Report);
        return Result;
    }

    // Compare every field.
    ++Result.ChecksRun;
    TArray<FString> Differences;
    int32 FieldsCompared = 0;
    CompareAllProperties(Creature, Decoded, Differences, FieldsCompared);

    Lines.Add(FString::Printf(TEXT("  Compared %d fields."), FieldsCompared));

    if (Differences.Num() > 0)
    {
        AddFailure(Result, FString::Printf(
            TEXT("%s: %d field(s) did not survive the round trip."), *Label, Differences.Num()));

        for (const FString& Difference : Differences)
        {
            Lines.Add(FString::Printf(TEXT("  LOST %s"), *Difference));
            Result.Failures.Add(FString::Printf(TEXT("%s -> %s"), *Label, *Difference));
        }
    }
    else
    {
        Lines.Add(TEXT("  All fields survived."));
    }

    // Sanity check the identity regeneration actually happened. If this ever
    // stops firing, two saves end up holding the same Creature identity.
    ++Result.ChecksRun;
    if (Decoded.UniqueID == Creature.UniqueID)
    {
        AddFailure(Result, FString::Printf(
            TEXT("%s: UniqueID was not regenerated on decode."), *Label));
        Lines.Add(TEXT("  UniqueID was NOT regenerated (both saves would share an identity)."));
    }

    Result.bPassed = (Result.ChecksFailed == 0);
    Result.Report = FString::Join(Lines, TEXT("\n"));

    UE_LOG(LogTradeCodec, Log, TEXT("%s"), *Result.Report);
    return Result;
}

FGF_CodecTestResult UGF_TradeDiagnostics::RunCodecTamperTest(const FGF_CreatureInstanceData& Creature)
{
    FGF_CodecTestResult Result;
    TArray<FString> Lines;
    Lines.Add(TEXT("Tamper resistance:"));

    // A clean blob to corrupt in various ways.
    FString GoodPayload;
    FString Error;
    if (!UGF_CreatureCodec::EncodeCreature(Creature, GoodPayload, Error))
    {
        AddFailure(Result, FString::Printf(
            TEXT("Cannot run tamper tests - encoding a valid Creature failed: %s"), *Error));
        Result.Report = FString::Join(Lines, TEXT("\n"));
        return Result;
    }

    // --- Malformed input ---
    ExpectRejected(Result, TEXT("empty string"), TEXT(""));
    ExpectRejected(Result, TEXT("whitespace only"), TEXT("   \n\t  "));
    ExpectRejected(Result, TEXT("not Base64"), TEXT("this is definitely not base64 !!!"));
    ExpectRejected(Result, TEXT("Base64 of unrelated text"),
        FBase64::Encode(FString(TEXT("hello world, not a creature"))));

    // --- Truncation, the most likely real-world corruption ---
    if (GoodPayload.Len() > 8)
    {
        ExpectRejected(Result, TEXT("payload truncated to half"),
            GoodPayload.Left(GoodPayload.Len() / 2));
    }

    // --- Byte-level corruption of a structurally valid blob ---
    // Corrupting the zlib stream makes the engine's own decompressor log a
    // Warning and an Error from LogCompression. That is the test PASSING: zlib's
    // checksum caught the corruption. LogCompression is declared inside a
    // private engine .cpp so it cannot be suppressed by symbol from here, hence
    // this note rather than a silencer -- muting engine error logs from a
    // diagnostic would hide real corruption later.
    UE_LOG(LogTradeCodec, Log,
        TEXT("  (the next LogCompression Warning/Error is EXPECTED - deliberately corrupted payload)"));

    TArray<uint8> Bytes;
    if (FBase64::Decode(GoodPayload, Bytes) && Bytes.Num() > 8)
    {
        // Wrong magic: should be caught before anything else is trusted.
        {
            TArray<uint8> Tampered = Bytes;
            Tampered[0] ^= 0xFF;
            ExpectRejected(Result, TEXT("corrupted magic number"), FBase64::Encode(Tampered));
        }

        // Corrupted compressed payload: zlib's checksum should catch this.
        {
            TArray<uint8> Tampered = Bytes;
            Tampered[Tampered.Num() - 1] ^= 0xFF;
            ExpectRejected(Result, TEXT("corrupted compressed data"), FBase64::Encode(Tampered));
        }
    }

    // --- Illegal Creature that are structurally perfect ---
    // These matter most: a cheater's blob is well-formed by construction, so
    // only the legality rules stand between it and the player's save.

    // Fills an existing instance by ASSIGNMENT rather than returning one by
    // value. FGF_CreatureInstanceData declares a copy constructor, which suppresses
    // its implicit move constructor, so returning by value would fall back to
    // that hand-written copy constructor whenever NRVO does not kick in -- and
    // it omits fields. Assignment is the compiler-generated memberwise one,
    // which copies everything.
    auto MakeVariant = [&Creature](FGF_CreatureInstanceData& OutVariant)
    {
        OutVariant = Creature;
    };

    // Encoding validates, so an invalid Creature should fail to encode at all.
    auto ExpectEncodeRejected = [&Result](const FString& CaseName, const FGF_CreatureInstanceData& Bad)
    {
        ++Result.ChecksRun;
        FString Ignored;
        FString WhyRejected;
        if (UGF_CreatureCodec::EncodeCreature(Bad, Ignored, WhyRejected))
        {
            AddFailure(Result, FString::Printf(
                TEXT("ENCODED an illegal Creature it should have rejected: %s"), *CaseName));
        }
    };

    {
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.Level = 255;
        ExpectEncodeRejected(TEXT("level 255"), Bad);
    }

    {
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.HP_AP = 999;
        ExpectEncodeRejected(TEXT("AP of 999"), Bad);
    }

    {
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.HP_EP = Bad.GetEPBudget() + 1;
        ExpectEncodeRejected(TEXT("more EP than the level has earned"), Bad);
    }

    {
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.Affinity = FGF_CreatureInstanceData::MaxAffinity + 1;
        ExpectEncodeRejected(TEXT("affinity over max"), Bad);
    }

    {
        // The dialogue-injection case. A nickname is drawn through the dialogue
        // parser, which reads {Token} and [flow], so a peer-chosen nickname is a
        // way into our own UI text.
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.Nickname = FName(TEXT("{Vault}"));
        ExpectEncodeRejected(TEXT("nickname containing a dialogue token"), Bad);
    }

    {
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.Nickname = FName(TEXT("[flow]"));
        ExpectEncodeRejected(TEXT("nickname containing a flow control sequence"), Bad);
    }

    {
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.Nickname = FName(TEXT("ThisNicknameIsFarTooLongToBeReal"));
        ExpectEncodeRejected(TEXT("over-long nickname"), Bad);
    }

    {
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.CurrentHP = Bad.MaxHP + 9999.f;
        ExpectEncodeRejected(TEXT("current HP above max HP"), Bad);
    }

    {
        // Uses arrays are indexed in lockstep with Skills everywhere else, so a
        // mismatched length would read out of bounds later.
        FGF_CreatureInstanceData Bad;
        MakeVariant(Bad);
        Bad.CurrentUses.Add(99);
        ExpectEncodeRejected(TEXT("Uses array longer than move list"), Bad);
    }

    Result.bPassed = (Result.ChecksFailed == 0);

    Lines.Add(FString::Printf(TEXT("  %d checks, %d failed."), Result.ChecksRun, Result.ChecksFailed));
    for (const FString& Failure : Result.Failures)
    {
        Lines.Add(FString::Printf(TEXT("  %s"), *Failure));
    }
    Result.Report = FString::Join(Lines, TEXT("\n"));

    UE_LOG(LogTradeCodec, Log, TEXT("%s"), *Result.Report);
    return Result;
}

FGF_CodecTestResult UGF_TradeDiagnostics::RunCodecPartyTest(const UObject* WorldContextObject)
{
    FGF_CodecTestResult Result;
    TArray<FString> Lines;

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    UGF_CreatureManagerSubsystem* Manager = GameInstance
        ? GameInstance->GetSubsystem<UGF_CreatureManagerSubsystem>()
        : nullptr;

    if (Manager == nullptr)
    {
        AddFailure(Result, TEXT("Creature manager subsystem is unavailable."));
        Result.Report = TEXT("Party test: Creature manager subsystem is unavailable.");
        UE_LOG(LogTradeCodec, Error, TEXT("%s"), *Result.Report);
        return Result;
    }

    const int32 PartySize = Manager->GetPartySize();
    if (PartySize <= 0)
    {
        // Not a failure. An empty party means the test could not run, and
        // reporting that as a pass would be worse than saying nothing.
        Result.bPassed = false;
        Result.Report = TEXT("Party test: party is empty, nothing to round trip. Catch something first.");
        UE_LOG(LogTradeCodec, Warning, TEXT("%s"), *Result.Report);
        return Result;
    }

    Lines.Add(FString::Printf(TEXT("Party round trip: %d Creature"), PartySize));

    for (int32 Index = 0; Index < PartySize; ++Index)
    {
        FGF_CreatureInstanceData Creature;
        if (!Manager->GetPartyCreatureData(Index, Creature))
        {
            AddFailure(Result, FString::Printf(TEXT("Could not read party slot %d."), Index));
            ++Result.ChecksRun;
            continue;
        }

        const FGF_CodecTestResult SlotResult = RunCodecRoundTripTest(Creature);
        Accumulate(Result, SlotResult);
        Lines.Add(SlotResult.Report);
    }

    Result.bPassed = (Result.ChecksFailed == 0);
    Result.Report = FString::Join(Lines, TEXT("\n"));
    return Result;
}

FGF_CodecTestResult UGF_TradeDiagnostics::RunAllCodecTests(const UObject* WorldContextObject)
{
    FGF_CodecTestResult Result;
    TArray<FString> Lines;

    const FGF_CodecTestResult PartyResult = RunCodecPartyTest(WorldContextObject);
    Accumulate(Result, PartyResult);
    Lines.Add(PartyResult.Report);

    // Tamper tests need one valid Creature to build a good blob from, so they
    // piggyback on the lead party member.
    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    UGF_CreatureManagerSubsystem* Manager = GameInstance
        ? GameInstance->GetSubsystem<UGF_CreatureManagerSubsystem>()
        : nullptr;

    FGF_CreatureInstanceData Lead;
    if (Manager && Manager->GetPartySize() > 0 && Manager->GetPartyCreatureData(0, Lead))
    {
        const FGF_CodecTestResult TamperResult = RunCodecTamperTest(Lead);
        Accumulate(Result, TamperResult);
        Lines.Add(TamperResult.Report);
    }
    else
    {
        Lines.Add(TEXT("Tamper tests skipped: no lead Creature to build a payload from."));
    }

    Result.bPassed = (Result.ChecksFailed == 0);

    Lines.Add(FString::Printf(
        TEXT("=== %s: %d checks, %d failed ==="),
        Result.bPassed ? TEXT("PASS") : TEXT("FAIL"),
        Result.ChecksRun,
        Result.ChecksFailed));

    // Report keeps the full detail for a debug-menu widget, but only the verdict
    // goes to the log here. The sub-tests already logged their own detail as they
    // ran, and re-logging the aggregate printed everything twice.
    Result.Report = FString::Join(Lines, TEXT("\n"));

    UE_LOG(LogTradeCodec, Display, TEXT("=== Codec tests %s: %d checks, %d failed ==="),
        Result.bPassed ? TEXT("PASS") : TEXT("FAIL"),
        Result.ChecksRun,
        Result.ChecksFailed);

    for (const FString& Failure : Result.Failures)
    {
        UE_LOG(LogTradeCodec, Error, TEXT("  %s"), *Failure);
    }

    return Result;
}

void UGF_TradeDiagnostics::Accumulate(FGF_CodecTestResult& Target, const FGF_CodecTestResult& Source)
{
    Target.ChecksRun += Source.ChecksRun;
    Target.ChecksFailed += Source.ChecksFailed;
    Target.Failures.Append(Source.Failures);
}
