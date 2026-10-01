// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Interfaces/IHttpRequest.h"
#include "GF_ResettableState.h"
#include "GF_CreatureInstanceData.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_GiftSubsystem.generated.h"

/**
 * What a code the player typed actually is.
 *
 * One text box serves both features, so something has to decide where a code is
 * going. The rule is deliberately mechanical rather than a mode toggle: the
 * player should not have to know which kind of code they were handed.
 */
UENUM(BlueprintType)
enum class EGF_CodeKind : uint8
{
    /** Not usable as either. Too short, too long, or has punctuation in it. */
    Invalid  UMETA(DisplayName = "Invalid"),

    /** All digits -- a link code for a player-to-player trade. */
    LinkCode UMETA(DisplayName = "Link Code"),

    /** Contains at least one letter -- a Mystery Gift code. */
    GiftCode UMETA(DisplayName = "Gift Code")
};

/**
 * Where a gift claim currently is. Drives the UI directly.
 */
UENUM(BlueprintType)
enum class EGF_GiftState : uint8
{
    /** Nothing in flight. */
    Idle     UMETA(DisplayName = "Idle"),

    /** Talking to the relay. One request, no polling -- there is nothing to wait for. */
    Claiming UMETA(DisplayName = "Claiming"),

    /** The Creature is in the player's storage. See GetReceivedCreature(). */
    Received UMETA(DisplayName = "Received"),

    /** Nothing was given. GetLastError() is safe to show the player. */
    Failed   UMETA(DisplayName = "Failed")
};

/**
 * Local half of the once-only safeguard, plus this installation's identity.
 *
 * Kept in its own slot rather than in the story save on purpose. A gift is
 * meant to be claimed once per person, so the record has to survive starting a
 * new game -- which is exactly what a quest flag would not do.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_GiftSave : public USaveGame
{
    GENERATED_BODY()

public:
    static const FString SlotName;
    static const int32   UserIndex;

    /**
     * Stable random identity for this installation, generated on first use.
     *
     * Deliberately NOT the player's tamer ID. That is a RandRange(0, 999999),
     * so across a few thousand players birthday collisions are not a curiosity
     * but a certainty -- roughly n^2/2000000 of them -- and every collision is a
     * real player wrongly told they already claimed. A GUID cannot collide.
     */
    UPROPERTY()
    FGuid InstallId;

    /** Gift codes already received here. Upper-cased on write. */
    UPROPERTY()
    TArray<FString> ClaimedGiftCodes;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnGiftStateChanged, EGF_GiftState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnGiftReceived, const FGF_CreatureInstanceData&, Received);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnGiftFailed, const FString&, Reason);

/**
 * Mystery Gift: one pre-authored Creature, distributed to many players at once.
 *
 * WHY THIS IS NOT A TRADE
 * -----------------------
 * The obvious build is a "bot" that plays the trade protocol on the other side
 * of a link code. It does not work: a trade session pairs exactly two peers, so
 * one event code would serve one player and 409 everyone else, and a trade also
 * requires the player to give something up. A gift is one-way, has no partner,
 * and has no capacity -- so it is a plain HTTP GET against a code, and none of
 * the session machinery in UGF_TradeSubsystem is involved.
 *
 * THE PROTOCOL (must stay in lockstep with Tools/TradeRelay/worker.js)
 * -------------------------------------------------------------------
 *   GET <RelayBaseUrl>/v1/gift/<code>?claimId=&contentVersion=
 *       -> 200 { giftName, payload, contentVersion, firstIssue }
 *       -> 404 { error: "gift_not_found" }
 *       -> 409 { error: "already_claimed" | "version_mismatch" }
 *       -> 410 { error: "gift_expired" | "gift_inactive" }
 *
 * THE ONCE-ONLY SAFEGUARD IS TWO LAYERS
 * -------------------------------------
 * Server: one Durable Object per (gift, player), refusing a second issue.
 * Client: UGF_GiftSave::ClaimedGiftCodes, so the option greys out and an
 *         offline retry cannot re-fire.
 *
 * Neither is airtight on its own and they are not fully independent -- both are
 * anchored to a file the player owns. Deleting GiftSlot.sav mints a new
 * InstallId and re-opens the gift. That is the same class of hole as the
 * save-backup duping already accepted for trading, and closing it properly
 * needs real accounts. What this DOES stop is the accidental double-claim, the
 * same save claiming from several machines, and the honest majority.
 *
 * WHAT THE RELAY IS NOT
 * ---------------------
 * Still a mailbox. It stores and bounds the blob; it never parses it. The
 * receiving client is the only thing checking legality, which is why the payload
 * goes through UGF_CreatureCodec::DecodeCreature exactly like a traded Creature --
 * same nickname injection filter, same asset-path guard, same stat legality, and
 * a fresh UniqueID per recipient so thousands of players do not all end up
 * holding the same identity.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_GiftSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
    GENERATED_BODY()

public:
    /**
     * What this build of the client is CAPABLE of receiving. Hand-bumped.
     *
     * Separate from UGF_CreatureCodec::TradeContentVersion, which answers a
     * different question: that one is about whether asset paths inside a blob
     * still resolve, and is deliberately NOT tied to the build number so testers
     * on adjacent patches can still trade. This one is about whether the client
     * has the CODE a gift depends on.
     *
     * The distinction matters because tagged serialization fails silently in
     * this direction. A client that does not know a property just seeks past it
     * (UStruct::SerializeVersionedTaggedProperties), so a gift relying on a
     * field the receiver lacks decodes cleanly and quietly does the wrong thing
     * -- a Creature flagged bCannotEvolve arriving at a build without that field
     * is simply an ordinary Creature that evolves. Nothing errors, and updating
     * afterwards does not repair it, because the flag was never stored.
     *
     * BUMP THIS whenever the client gains a capability a gift might depend on --
     * a new FGF_CreatureInstanceData field, a new validation rule, a new mechanic.
     * Then publish gifts that need it with a matching minClientVersion, and old
     * clients are turned away BEFORE their claim is spent rather than being
     * handed something they cannot represent.
     *
     * 1 - first release. Includes FGF_CreatureInstanceData::bCannotEvolve.
     */
    static constexpr int32 GiftClientVersion = 1;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * IGF_ResettableState -- abandon an in-flight claim only.
     *
     * The claimed-code list is deliberately NOT cleared. Starting a new game is
     * not a second entitlement to the same event Creature, and wiping the list
     * here would make "New Game" a one-click gift farm.
     */
    virtual void ResetToBootState() override;

    //--------------------
    // EVENTS
    //--------------------

    UPROPERTY(BlueprintAssignable, Category = "Gift")
    FGF_OnGiftStateChanged OnGiftStateChanged;

    /** The Creature is already in the party or a box by the time this fires. */
    UPROPERTY(BlueprintAssignable, Category = "Gift")
    FGF_OnGiftReceived OnGiftReceived;

    /** Nothing was given. Reason is safe to show the player. */
    UPROPERTY(BlueprintAssignable, Category = "Gift")
    FGF_OnGiftFailed OnGiftFailed;

    //--------------------
    // FLOW
    //--------------------

    /**
     * Single entry point for a SHARED code box, so one widget serves both
     * features with no mode to pick.
     *
     * Only for the shared case. It has to guess where an unlabelled code is
     * going, and the only thing it can go on is the code's shape -- so gift
     * codes must contain a letter here. If the screen instead has a separate
     * Mystery Gift button, wire that at ClaimGift directly and the codes can be
     * whatever you like, digits included.
     *
     * @return false if the code is unusable or the chosen path refused it;
     *         GetLastError() explains which.
     */
    UFUNCTION(BlueprintCallable, Category = "Gift")
    bool SubmitCode(const FString& Code);

    /**
     * Claim a gift. One request, no polling.
     *
     * Accepts ANY valid 4-16 character code, including an all-numeric one. The
     * "must contain a letter" rule exists only in SubmitCode, which has to work
     * out where an unlabelled code is going -- wire a dedicated Mystery Gift
     * button straight here and gift codes can be pure digits, which is what a
     * numeric keypad needs.
     *
     * On success the Creature is written to storage and flushed to disk BEFORE
     * the local claim is recorded, so a crash mid-claim leaves the gift
     * claimable again rather than consumed and lost.
     *
     * @return false if a claim is already running, the code is unusable, or it
     *         was already claimed on this installation.
     */
    UFUNCTION(BlueprintCallable, Category = "Gift")
    bool ClaimGift(const FString& GiftCode);

    //--------------------
    // QUERIES
    //--------------------

    /** Decide which feature a typed code belongs to. Pure -- safe for live UI hints. */
    UFUNCTION(BlueprintPure, Category = "Gift")
    static EGF_CodeKind ClassifyCode(const FString& Code);

    /** 4-16 alphanumerics with at least one letter. Digits alone are a link code. */
    UFUNCTION(BlueprintPure, Category = "Gift")
    static bool IsGiftCode(const FString& Code);

    /** Local record only. The server has the authoritative answer. */
    UFUNCTION(BlueprintPure, Category = "Gift")
    bool HasClaimedGift(const FString& GiftCode) const;

    UFUNCTION(BlueprintPure, Category = "Gift")
    EGF_GiftState GetGiftState() const { return State; }

    UFUNCTION(BlueprintPure, Category = "Gift")
    bool IsClaimInProgress() const { return State == EGF_GiftState::Claiming; }

    UFUNCTION(BlueprintPure, Category = "Gift")
    FString GetLastError() const { return LastError; }

    /**
     * One line describing where the claim is, ready to put on screen.
     *
     * Lives here rather than in the widget so every screen agrees on the wording
     * -- the same reason UGF_TradeSubsystem::GetTradeStatusText exists. In the
     * Failed state this is the specific reason ("You have already received this
     * gift."), never a generic failure, because the whole point is telling a
     * player why the same code stopped working rather than letting it look
     * broken.
     */
    UFUNCTION(BlueprintPure, Category = "Gift")
    FText GetGiftStatusText() const;

    /** Event name from the server, e.g. "Gamma Fest 2026". Empty if unnamed. */
    UFUNCTION(BlueprintPure, Category = "Gift")
    FString GetLastGiftName() const { return LastGiftName; }

    /** The Creature received. Valid once the state is Received -- for the cutscene. */
    UFUNCTION(BlueprintPure, Category = "Gift")
    const FGF_CreatureInstanceData& GetReceivedCreature() const { return ReceivedCreature; }

    UFUNCTION(BlueprintPure, Category = "Gift")
    bool DidGiftGoToParty() const { return bWentToParty; }

    /** Vault it landed in, or -1 when it went to the party. */
    UFUNCTION(BlueprintPure, Category = "Gift")
    int32 GetGiftVaultPageIndex() const { return ReceivedVaultPageIndex; }

    /**
     * Set {GiftName}, {GiftCreature} and {GiftDestination} for the dialogue that
     * announces the gift.
     *
     * MUST be called immediately before every StartDialogue, not once at claim
     * time. UGF_DialogueSubsystem wipes all variables when a dialogue ends, so
     * setting them once gives a correct first line and blanks in every line
     * after it. This is the easiest node in the whole feature to forget.
     */
    UFUNCTION(BlueprintCallable, Category = "Gift")
    void PublishGiftDialogueTokens() const;

    /**
     * Forget a local claim so the same build can test the flow twice.
     *
     * The SERVER still refuses, which is the point -- this exists to test the
     * client path, not to re-open a gift. Debug builds only.
     */
    UFUNCTION(BlueprintCallable, Category = "Gift", meta = (DevelopmentOnly))
    void DebugForgetClaim(const FString& GiftCode);

private:
    //--------------------
    // STATE
    //--------------------

    UPROPERTY(Transient)
    EGF_GiftState State = EGF_GiftState::Idle;

    UPROPERTY(Transient)
    FGF_CreatureInstanceData ReceivedCreature;

    FString ActiveGiftCode;
    FString LastError;
    FString LastGiftName;

    bool bWentToParty = false;
    int32 ReceivedVaultPageIndex = INDEX_NONE;
    int32 ReceivedSlotIndex = INDEX_NONE;

    /** Guards against a second request going out while one is in flight. */
    bool bRequestInFlight = false;

    //--------------------
    // INTERNALS
    //--------------------

    void SetState(EGF_GiftState NewState);
    void FailClaim(const FString& Reason);

    void SendClaim(const FString& Code);
    void HandleClaimResponse(int32 ResponseCode, const FString& Body);

    /** Decode, validate, hand over, flush, and only then record the claim. */
    bool ReceiveGiftPayload(const FString& Payload, FString& OutError);

    /**
     * Per-gift opaque claim identity: MD5(code : InstallId).
     *
     * Hashed with the code rather than sent raw so the relay cannot correlate
     * one player across separate giveaways. It never needs to -- every lookup it
     * does is already scoped to a single gift.
     */
    FString BuildClaimId(const FString& Code) const;

    static FString BuildGiftUrl(const FString& Code, const FString& Query);

    UGF_GiftSave* LoadOrCreateGiftSave() const;
    void RecordClaim(const FString& Code);

    class UGF_CreatureManagerSubsystem* GetCreatureManager() const;
};
