// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_CreatureCodec.h"

#include "GF_SkillDefinition.h"
#include "GF_CreatureSpeciesData.h"

#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/SoftObjectPath.h"

// Bump ONLY when species/move/item assets are renamed or moved. See the header.
const TCHAR* UGF_CreatureCodec::TradeContentVersion = TEXT("GE-1.0.0");

namespace
{
    // Every asset a blob may reference has to live under the game's own content
    // root. A blob is authored by a remote peer, so without this a crafted path
    // could aim our resolver at engine or plugin content and produce a Creature
    // whose species asset is something that was never meant to be one.
    const TCHAR* GameContentRoot = TEXT("/Game/");

    // Longest content-version tag we will read off the wire, so a malformed
    // length field cannot make us allocate on a hostile peer's say-so.
    constexpr int32 MaxContentVersionChars = 64;

    void WriteBoundedAsciiString(FMemoryWriter& Writer, const FString& Value)
    {
        const FTCHARToUTF8 Converted(*Value);
        int32 Length = Converted.Length();
        Writer << Length;
        if (Length > 0)
        {
            Writer.Serialize(const_cast<ANSICHAR*>(Converted.Get()), Length);
        }
    }

    bool ReadBoundedAsciiString(FMemoryReader& Reader, int32 MaxChars, FString& OutValue)
    {
        int32 Length = 0;
        Reader << Length;
        if (Reader.IsError() || Length < 0 || Length > MaxChars)
        {
            return false;
        }

        if (Length == 0)
        {
            OutValue.Reset();
            return true;
        }

        TArray<uint8> Bytes;
        Bytes.SetNumUninitialized(Length + 1);
        Reader.Serialize(Bytes.GetData(), Length);
        if (Reader.IsError())
        {
            return false;
        }

        Bytes[Length] = 0;
        OutValue = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData())));
        return true;
    }

    // Shared by the species reference and every move reference.
    bool IsAcceptableAssetPath(const FSoftObjectPath& Path, const TCHAR* FieldLabel, FString& OutError)
    {
        if (Path.IsNull())
        {
            OutError = FString::Printf(TEXT("%s is empty."), FieldLabel);
            return false;
        }

        const FString AsString = Path.ToString();
        if (!AsString.StartsWith(GameContentRoot))
        {
            OutError = FString::Printf(TEXT("%s points outside game content (%s)."), FieldLabel, *AsString);
            return false;
        }

        return true;
    }
}

bool UGF_CreatureCodec::IsSafeTradeName(const FName& Name, const TCHAR* FieldLabel, FString& OutError)
{
    // NAME_None is legitimate: a Creature with no nickname falls back to its
    // species name, and some tamers genuinely have no OT string.
    if (Name.IsNone())
    {
        return true;
    }

    const FString AsString = Name.ToString();

    if (AsString.Len() > MaxNameChars)
    {
        OutError = FString::Printf(TEXT("%s is %d characters, limit is %d."),
            FieldLabel, AsString.Len(), MaxNameChars);
        return false;
    }

    for (const TCHAR Char : AsString)
    {
        // Control characters would corrupt any text box they land in.
        if (Char < 0x20 || Char == 0x7F)
        {
            OutError = FString::Printf(TEXT("%s contains a control character."), FieldLabel);
            return false;
        }

        // Our dialogue parser treats {Token} as a substitution and [flow] as a
        // control sequence. A name is drawn through that parser, so letting a
        // remote peer choose these characters lets them inject markup into our
        // own UI text -- reject rather than escape, because the name has no
        // legitimate reason to contain them.
        if (Char == TEXT('{') || Char == TEXT('}') || Char == TEXT('[') || Char == TEXT(']'))
        {
            OutError = FString::Printf(TEXT("%s contains a reserved dialogue character."), FieldLabel);
            return false;
        }
    }

    return true;
}

bool UGF_CreatureCodec::ValidateCreature(const FGF_CreatureInstanceData& Creature, FString& OutError)
{
    OutError.Reset();

    // Species.
    if (!IsAcceptableAssetPath(Creature.SpeciesData.ToSoftObjectPath(), TEXT("Species reference"), OutError))
    {
        return false;
    }

    // Level and experience.
    if (Creature.Level < 1 || Creature.Level > MaxLevel)
    {
        OutError = FString::Printf(TEXT("Level %d is outside 1-%d."), Creature.Level, MaxLevel);
        return false;
    }

    if (Creature.CurrentEXP < 0.f || !FMath::IsFinite(Creature.CurrentEXP))
    {
        OutError = TEXT("Experience is negative or not a finite number.");
        return false;
    }

    // HP. MaxHP of 1 is legal -- that is Husk.
    if (!FMath::IsFinite(Creature.MaxHP) || Creature.MaxHP < 1.f)
    {
        OutError = TEXT("Max HP is not a positive finite number.");
        return false;
    }

    if (!FMath::IsFinite(Creature.CurrentHP) || Creature.CurrentHP < 0.f || Creature.CurrentHP > Creature.MaxHP)
    {
        OutError = FString::Printf(TEXT("Current HP %.0f is outside 0-%.0f."), Creature.CurrentHP, Creature.MaxHP);
        return false;
    }

    // Potentials.
    const TArray<TPair<const TCHAR*, int32>> Potentials = {
        { TEXT("HP"),              Creature.HP_Potential },
        { TEXT("Attack"),          Creature.Attack_Potential },
        { TEXT("Defense"),         Creature.Defense_Potential },
        { TEXT("Special Attack"),  Creature.Magic_Potential },
        { TEXT("Special Defense"), Creature.Poise_Potential },
        { TEXT("Speed"),           Creature.Speed_Potential }
    };

    for (const TPair<const TCHAR*, int32>& Potential : Potentials)
    {
        if (Potential.Value < 0 || Potential.Value > MaxPotential)
        {
            OutError = FString::Printf(TEXT("%s Potential of %d is outside 0-%d."), Potential.Key, Potential.Value, MaxPotential);
            return false;
        }
    }

    // Training, per stat and in total.
    const TArray<TPair<const TCHAR*, int32>> Training = {
        { TEXT("HP"),              Creature.HP_Training },
        { TEXT("Attack"),          Creature.Attack_Training },
        { TEXT("Defense"),         Creature.Defense_Training },
        { TEXT("Special Attack"),  Creature.Magic_Training },
        { TEXT("Special Defense"), Creature.Poise_Training },
        { TEXT("Speed"),           Creature.Speed_Training }
    };

    int32 TotalTraining = 0;
    for (const TPair<const TCHAR*, int32>& TrainingValue : Training)
    {
        if (TrainingValue.Value < 0 || TrainingValue.Value > MaxTrainingPerStat)
        {
            OutError = FString::Printf(TEXT("%s TrainingValue of %d is outside 0-%d."), TrainingValue.Key, TrainingValue.Value, MaxTrainingPerStat);
            return false;
        }
        TotalTraining += TrainingValue.Value;
    }

    if (TotalTraining > MaxTrainingTotal)
    {
        OutError = FString::Printf(TEXT("Total Training of %d exceed the %d cap."), TotalTraining, MaxTrainingTotal);
        return false;
    }

    // Skills. An egg has none yet; anything else needs at least one, or it would
    // arrive unable to act and unable to LastResort out of it.
    if (Creature.Skills.Num() > MaxSkills)
    {
        OutError = FString::Printf(TEXT("Knows %d moves, limit is %d."), Creature.Skills.Num(), MaxSkills);
        return false;
    }

    if (!Creature.bIsEgg && Creature.Skills.Num() < 1)
    {
        OutError = TEXT("Knows no moves.");
        return false;
    }

    for (int32 Index = 0; Index < Creature.Skills.Num(); ++Index)
    {
        const FString Label = FString::Printf(TEXT("Skill %d"), Index + 1);
        if (!IsAcceptableAssetPath(Creature.Skills[Index].ToSoftObjectPath(), *Label, OutError))
        {
            return false;
        }
    }

    // Uses arrays are indexed in lockstep with Skills everywhere else in the game,
    // so a mismatched length here would read out of bounds later.
    if (Creature.CurrentUses.Num() != Creature.Skills.Num() || Creature.MaxUses.Num() != Creature.Skills.Num())
    {
        OutError = FString::Printf(TEXT("Uses arrays (%d current, %d max) do not match %d moves."),
            Creature.CurrentUses.Num(), Creature.MaxUses.Num(), Creature.Skills.Num());
        return false;
    }

    for (int32 Index = 0; Index < Creature.Skills.Num(); ++Index)
    {
        if (Creature.MaxUses[Index] < 1 || Creature.MaxUses[Index] > 255)
        {
            OutError = FString::Printf(TEXT("Skill %d has a max Uses of %d."), Index + 1, Creature.MaxUses[Index]);
            return false;
        }

        if (Creature.CurrentUses[Index] < 0 || Creature.CurrentUses[Index] > Creature.MaxUses[Index])
        {
            OutError = FString::Printf(TEXT("Skill %d has %d/%d Uses."),
                Index + 1, Creature.CurrentUses[Index], Creature.MaxUses[Index]);
            return false;
        }
    }

    // Bond.
    if (Creature.Bond < 0 || Creature.Bond > MaxBond)
    {
        OutError = FString::Printf(TEXT("Bond of %d is outside 0-%d."), Creature.Bond, MaxBond);
        return false;
    }

    // Names, including the injection check.
    if (!IsSafeTradeName(Creature.Nickname, TEXT("Nickname"), OutError)) { return false; }
    if (!IsSafeTradeName(Creature.OriginalTamerName, TEXT("Original tamer name"), OutError)) { return false; }
    if (!IsSafeTradeName(Creature.CurrentTamerName, TEXT("Current tamer name"), OutError)) { return false; }

    // Egg bookkeeping. EggSpeciesName drives what actually hatches, so an egg
    // without one would hatch into nothing.
    if (Creature.bIsEgg)
    {
        if (Creature.EggCyclesRemaining < 0)
        {
            OutError = TEXT("Egg has a negative cycle count.");
            return false;
        }

        if (Creature.EggSpeciesName.IsNone())
        {
            OutError = TEXT("Egg does not say what it hatches into.");
            return false;
        }

        if (Creature.EggUniqueRolls < 1)
        {
            OutError = TEXT("Egg has fewer than one unique roll.");
            return false;
        }
    }

    // Sleep counter. Non-zero sleep on a Creature that is not asleep would tick
    // down against nothing and desync the status UI.
    if (Creature.SleepCounter < 0)
    {
        OutError = TEXT("Sleep counter is negative.");
        return false;
    }

    // Tamer memo. All of this is display-only, but it is display-only text chosen
    // on someone else's machine and it lands in a UI text block, so it gets the same
    // treatment as a nickname.
    if (Creature.MetLevel < 0 || Creature.MetLevel > MaxLevel)
    {
        OutError = FString::Printf(TEXT("Met level %d is outside 0-%d."), Creature.MetLevel, MaxLevel);
        return false;
    }

    if (!Creature.MetRoutePath.IsNull()
        && !IsAcceptableAssetPath(Creature.MetRoutePath, TEXT("Met location reference"), OutError))
    {
        return false;
    }

    if (!Creature.MetLocationOverride.IsEmpty())
    {
        if (Creature.MetLocationOverride.Len() > MaxLocationChars)
        {
            OutError = FString::Printf(TEXT("Met location is %d characters, limit is %d."),
                Creature.MetLocationOverride.Len(), MaxLocationChars);
            return false;
        }

        for (const TCHAR Char : Creature.MetLocationOverride)
        {
            if (Char < 0x20 || Char == 0x7F)
            {
                OutError = TEXT("Met location contains a control character.");
                return false;
            }

            // Same dialogue-markup rejection as IsSafeTradeName -- see the comment there.
            if (Char == TEXT('{') || Char == TEXT('}') || Char == TEXT('[') || Char == TEXT(']'))
            {
                OutError = TEXT("Met location contains a reserved dialogue character.");
                return false;
            }
        }
    }

    // The memo note is attacker-controlled text that ends up in the same panels
    // and text boxes as everything above, so it gets the same treatment. Added
    // with the field itself rather than afterwards: a new player-visible string
    // that skips this list is exactly how an injection hole opens up quietly.
    if (!Creature.MemoNote.IsEmpty())
    {
        if (Creature.MemoNote.Len() > MaxLocationChars)
        {
            OutError = FString::Printf(TEXT("Memo note is %d characters, limit is %d."),
                Creature.MemoNote.Len(), MaxLocationChars);
            return false;
        }

        for (const TCHAR Char : Creature.MemoNote)
        {
            if (Char < 0x20 || Char == 0x7F)
            {
                OutError = TEXT("Memo note contains a control character.");
                return false;
            }

            if (Char == TEXT('{') || Char == TEXT('}') || Char == TEXT('[') || Char == TEXT(']'))
            {
                OutError = TEXT("Memo note contains a reserved dialogue character.");
                return false;
            }
        }
    }

    return true;
}

bool UGF_CreatureCodec::EncodeCreature(const FGF_CreatureInstanceData& Creature, FString& OutEncoded, FString& OutError)
{
    OutEncoded.Reset();
    OutError.Reset();

    // Validate before sending. A Creature we would reject on arrival is one the
    // peer will reject too, and a failure here is far easier to diagnose than a
    // mystery rejection on someone else's machine.
    if (!ValidateCreature(Creature, OutError))
    {
        OutError = FString::Printf(TEXT("Refusing to encode an invalid Creature: %s"), *OutError);
        return false;
    }

    TArray<uint8> Raw;
    {
        FMemoryWriter RawWriter(Raw, /*bIsPersistent=*/true);
        RawWriter.ArIsSaveGame = true;

        FObjectAndNameAsStringProxyArchive Ar(RawWriter, /*bInLoadIfFindFails=*/false);
        Ar.ArIsSaveGame = true;
        Ar.ArNoDelta = true;

        // SerializeItem wants a mutable pointer even when saving, and only reads
        // through it in that direction. Taking a local copy instead would run the
        // struct's hand-written copy constructor, which silently omits fields --
        // precisely the data loss this codec exists to avoid.
        FGF_CreatureInstanceData* Mutable = const_cast<FGF_CreatureInstanceData*>(&Creature);
        FGF_CreatureInstanceData::StaticStruct()->SerializeItem(Ar, Mutable, nullptr);

        if (RawWriter.IsError())
        {
            OutError = TEXT("Failed to serialize the Creature.");
            return false;
        }
    }

    const int32 UncompressedSize = Raw.Num();
    if (UncompressedSize <= 0)
    {
        OutError = TEXT("Serialization produced no data.");
        return false;
    }

    int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, UncompressedSize);
    TArray<uint8> Compressed;
    Compressed.SetNumUninitialized(CompressedSize);

    if (!FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Raw.GetData(), UncompressedSize))
    {
        OutError = TEXT("Failed to compress the Creature.");
        return false;
    }
    Compressed.SetNum(CompressedSize);

    TArray<uint8> Envelope;
    {
        FMemoryWriter Writer(Envelope, /*bIsPersistent=*/true);

        uint32 Magic = CodecMagic;
        uint16 Version = CodecVersion;
        int32 RawSize = UncompressedSize;
        int32 PackedSize = Compressed.Num();

        Writer << Magic;
        Writer << Version;
        WriteBoundedAsciiString(Writer, TradeContentVersion);
        Writer << RawSize;
        Writer << PackedSize;
        Writer.Serialize(Compressed.GetData(), PackedSize);
    }

    OutEncoded = FBase64::Encode(Envelope);
    return true;
}

EGF_TradeCodecResult UGF_CreatureCodec::DecodeCreature(const FString& Encoded, FGF_CreatureInstanceData& OutCreature, FString& OutError)
{
    OutError.Reset();

    const FString Trimmed = Encoded.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        OutError = TEXT("Nothing to decode.");
        return EGF_TradeCodecResult::EmptyPayload;
    }

    if (Trimmed.Len() > MaxEncodedChars)
    {
        OutError = FString::Printf(TEXT("Payload is %d characters, limit is %d."), Trimmed.Len(), MaxEncodedChars);
        return EGF_TradeCodecResult::BadEncoding;
    }

    TArray<uint8> Envelope;
    if (!FBase64::Decode(Trimmed, Envelope))
    {
        OutError = TEXT("Payload is not valid Base64. It may have been truncated in transit.");
        return EGF_TradeCodecResult::BadEncoding;
    }

    FMemoryReader Reader(Envelope, /*bIsPersistent=*/true);

    uint32 Magic = 0;
    Reader << Magic;
    if (Reader.IsError() || Magic != CodecMagic)
    {
        OutError = TEXT("Payload is not a Gamma Framework Creature.");
        return EGF_TradeCodecResult::BadMagic;
    }

    uint16 Version = 0;
    Reader << Version;
    if (Reader.IsError() || Version == 0 || Version > CodecVersion)
    {
        OutError = FString::Printf(TEXT("Payload uses codec version %u; this build reads up to %u."),
            Version, CodecVersion);
        return EGF_TradeCodecResult::UnsupportedVersion;
    }

    FString ContentVersion;
    if (!ReadBoundedAsciiString(Reader, MaxContentVersionChars, ContentVersion))
    {
        OutError = TEXT("Payload header is malformed.");
        return EGF_TradeCodecResult::CorruptPayload;
    }

    if (ContentVersion != TradeContentVersion)
    {
        // Worth naming both sides: this is the failure testers will hit most, and
        // "you two are on different builds" is only actionable if we say so.
        OutError = FString::Printf(
            TEXT("The other player is on game content '%s'; this build is '%s'. Both players need the same version to trade."),
            *ContentVersion, TradeContentVersion);
        return EGF_TradeCodecResult::ContentMismatch;
    }

    int32 RawSize = 0;
    Reader << RawSize;
    if (Reader.IsError() || RawSize <= 0 || RawSize > MaxUncompressedBytes)
    {
        OutError = TEXT("Payload declares an implausible size.");
        return EGF_TradeCodecResult::CorruptPayload;
    }

    int32 PackedSize = 0;
    Reader << PackedSize;
    // Bounding against what is actually left in the buffer stops a crafted length
    // from making us allocate on a remote peer's say-so.
    const int64 BytesRemaining = static_cast<int64>(Envelope.Num()) - Reader.Tell();
    if (Reader.IsError() || PackedSize <= 0 || static_cast<int64>(PackedSize) > BytesRemaining)
    {
        OutError = TEXT("Payload declares an implausible compressed size.");
        return EGF_TradeCodecResult::CorruptPayload;
    }

    TArray<uint8> Compressed;
    Compressed.SetNumUninitialized(PackedSize);
    Reader.Serialize(Compressed.GetData(), PackedSize);
    if (Reader.IsError())
    {
        OutError = TEXT("Payload ended early.");
        return EGF_TradeCodecResult::CorruptPayload;
    }

    TArray<uint8> Raw;
    Raw.SetNumUninitialized(RawSize);
    if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawSize, Compressed.GetData(), PackedSize))
    {
        OutError = TEXT("Payload could not be decompressed.");
        return EGF_TradeCodecResult::CorruptPayload;
    }

    FGF_CreatureInstanceData Decoded;
    {
        FMemoryReader RawReader(Raw, /*bIsPersistent=*/true);
        RawReader.ArIsSaveGame = true;

        FObjectAndNameAsStringProxyArchive Ar(RawReader, /*bInLoadIfFindFails=*/false);
        Ar.ArIsSaveGame = true;

        FGF_CreatureInstanceData::StaticStruct()->SerializeItem(Ar, &Decoded, nullptr);

        if (RawReader.IsError())
        {
            OutError = TEXT("Creature data is corrupt.");
            return EGF_TradeCodecResult::CorruptPayload;
        }
    }

    if (!ValidateCreature(Decoded, OutError))
    {
        return EGF_TradeCodecResult::FailedValidation;
    }

    // A traded Creature must not reuse the sender's identity, or both saves end up
    // holding the same UniqueID and anything keyed on it sees one Creature twice.
    Decoded.UniqueID = FGuid::NewGuid();

    // Copy assignment here is the compiler-generated memberwise one, which copies
    // every field. Constructing a copy instead would run the hand-written copy
    // constructor and drop whatever it forgets.
    OutCreature = Decoded;
    return EGF_TradeCodecResult::Success;
}

FString UGF_CreatureCodec::GetTradeContentVersion()
{
    return FString(TradeContentVersion);
}
