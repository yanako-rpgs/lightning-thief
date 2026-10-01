// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GF_CreatureInstanceData.h"
#include "GF_CreatureCodec.generated.h"

/**
 * Why a decode attempt failed. Anything other than Success means OutCreature was
 * left untouched -- the caller's Creature is never half-overwritten by a bad blob.
 */
UENUM(BlueprintType)
enum class EGF_TradeCodecResult : uint8
{
    // Blob decoded and passed every legality check. OutCreature is populated.
    Success                 UMETA(DisplayName = "Success"),

    // Caller handed us an empty / whitespace-only string.
    EmptyPayload            UMETA(DisplayName = "Empty Payload"),

    // Not valid Base64, or absurdly large. Usually a truncated copy/paste.
    BadEncoding             UMETA(DisplayName = "Bad Encoding"),

    // Decoded fine but is not one of our blobs (wrong magic number).
    BadMagic                UMETA(DisplayName = "Not A Creature Blob"),

    // Our blob, but from a codec revision this build cannot read.
    UnsupportedVersion      UMETA(DisplayName = "Unsupported Codec Version"),

    // Our blob, our codec, but the other player is on a different game build.
    // Species and move references travel as asset paths, so cross-build trades
    // would resolve to the wrong asset or to nothing at all.
    ContentMismatch         UMETA(DisplayName = "Game Version Mismatch"),

    // Truncated, corrupted, or deliberately malformed payload.
    CorruptPayload          UMETA(DisplayName = "Corrupt Payload"),

    // Structurally fine but describes an illegal Creature (hacked stats, bad
    // asset path, injection characters in the nickname, and so on).
    FailedValidation        UMETA(DisplayName = "Failed Validation")
};

/**
 * Wire codec for a single Creature.
 *
 * Turns an FGF_CreatureInstanceData into a self-describing Base64 string and back,
 * so a Creature can be handed to any transport (the link-trade relay, a debug
 * paste box, a file) without that transport knowing anything about Creature.
 *
 * Design notes worth knowing before you touch this:
 *
 * - Serialization goes through the SaveGame archive path, NOT a hand-written
 *   field list. Every field on FGF_CreatureInstanceData is already marked SaveGame,
 *   so a field added to the struct is picked up here automatically. This is
 *   deliberate: the struct's hand-written copy constructor already proved that
 *   manual field lists rot silently, and a dropped field over the wire means a
 *   player's Creature quietly loses data in a trade.
 *
 * - Properties are written tagged (name + type per field), so a blob written by
 *   an older build that lacked a field still loads: the missing tag just leaves
 *   the default. That gives forward tolerance within a content version.
 *
 * - Species and moves are TSoftObjectPtr / TSoftClassPtr, which serialize as
 *   asset path strings. They only resolve if both players run the same content.
 *   TradeContentVersion is the guard for that and MUST be bumped whenever the
 *   species or move assets change in a way that renames or repaths anything.
 *
 * - Everything decoded is hostile until proven otherwise. A blob arrives from
 *   another player's machine, so ValidateCreature runs on every decode and the
 *   caller cannot skip it.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_CreatureCodec : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    //--------------------
    // FORMAT CONSTANTS
    //--------------------

    // 'G','E','T','P' -- legacy wire tag, kept so older payloads still decode. Little-endian on all our
    // targets (the project is Windows-only), so a raw uint32 compare is fine.
    static constexpr uint32 CodecMagic = 0x50544547;

    // Bump when the envelope layout below changes. Blobs carrying a higher
    // version than this build knows are rejected rather than guessed at.
    static constexpr uint16 CodecVersion = 1;

    // Content-compatibility tag. This is NOT the game's patch version: bump it
    // only when species/move/item assets move or get renamed, because that is
    // what actually breaks the soft asset paths inside a blob. Leaving it alone
    // across a cosmetic patch lets testers on adjacent builds still trade.
    static const TCHAR* TradeContentVersion;

    // Hard caps applied to any incoming blob before we allocate anything for it.
    // A remote peer picks these numbers, so they are attack surface, not hints.
    static constexpr int32 MaxEncodedChars = 256 * 1024;
    static constexpr int32 MaxUncompressedBytes = 1024 * 1024;

    // Gameplay legality bounds.
    static constexpr int32 MaxLevel = 100;
    static constexpr int32 MaxPotential = 31;
    static constexpr int32 MaxTrainingPerStat = 255;
    static constexpr int32 MaxTrainingTotal = 510;
    static constexpr int32 MaxSkills = 4;
    static constexpr int32 MaxBond = 255;
    static constexpr int32 MaxNameChars = 12;

    // Tamer-memo place names are prose ("Fernhollow Woods", "Sanctuary Couple"), so
    // they get a longer allowance than a 12-character nickname -- but the same
    // character rules, because they end up in the same text boxes.
    static constexpr int32 MaxLocationChars = 48;

    //--------------------
    // ENCODE / DECODE
    //--------------------

    /**
     * Pack a Creature into a Base64 string suitable for the wire.
     * Validates before encoding, so we never hand a peer something we would
     * ourselves reject -- that turns "their game rejected my Creature" bugs into
     * local failures we can actually see.
     *
     * @param OutError  Human-readable reason on failure. Safe to show in UI.
     * @return true if OutEncoded was written.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading|Codec", meta = (DisplayName = "Encode Creature To String"))
    static bool EncodeCreature(const FGF_CreatureInstanceData& Creature, FString& OutEncoded, FString& OutError);

    /**
     * Unpack a Creature received from another player.
     * OutCreature is only written on Success -- on any failure the caller's
     * existing value is left exactly as it was.
     *
     * @param OutError  Human-readable reason on failure. Safe to show in UI.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading|Codec", meta = (DisplayName = "Decode Creature From String"))
    static EGF_TradeCodecResult DecodeCreature(const FString& Encoded, FGF_CreatureInstanceData& OutCreature, FString& OutError);

    /**
     * Legality gate for a Creature that came from outside this save file.
     *
     * This is the only thing standing between a hand-edited blob and the
     * player's party, because the relay is a dumb mailbox that never inspects
     * payloads. Erring toward rejection is correct here: a false reject costs
     * one retried trade, a false accept permanently corrupts a save.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading|Codec", meta = (DisplayName = "Validate Traded Creature"))
    static bool ValidateCreature(const FGF_CreatureInstanceData& Creature, FString& OutError);

    /** The content-compatibility tag this build will accept blobs from. */
    UFUNCTION(BlueprintPure, Category = "Trading|Codec")
    static FString GetTradeContentVersion();

    /**
     * Rejects names that are over-long or carry characters the dialogue system
     * treats as markup. A name flows straight into dialogue text, and the parser
     * reads {Token} and [flow] control sequences, so any name chosen on another
     * player's machine is a text-injection vector into our own UI.
     *
     * Public because tamer names arrive over the wire OUTSIDE a Creature blob
     * -- the relay exchanges them at connect time, before anyone has chosen a
     * Creature -- and they need exactly the same filtering as a nickname.
     */
    static bool IsSafeTradeName(const FName& Name, const TCHAR* FieldLabel, FString& OutError);
};
