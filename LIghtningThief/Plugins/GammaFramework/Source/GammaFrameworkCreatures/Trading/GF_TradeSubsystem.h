// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/SaveGame.h"
#include "Interfaces/IHttpRequest.h"
#include "GF_ResettableState.h"
#include "GF_CreatureInstanceData.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_TradeSubsystem.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Gamma Framework Link Trading"))
class GAMMAFRAMEWORKCREATURES_API UGF_TradeSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, Category = "Relay")
    FString RelayBaseUrl = TEXT("https://example.workers.dev");

    UPROPERTY(Config, EditAnywhere, Category = "Relay", meta = (ClampMin = "0.5", ClampMax = "10.0"))
    float PollIntervalSeconds = 1.0f;

    UPROPERTY(Config, EditAnywhere, Category = "Relay", meta = (ClampMin = "15.0", ClampMax = "600.0"))
    float PartnerWaitTimeoutSeconds = 180.0f;

    UPROPERTY(Config, EditAnywhere, Category = "Relay", meta = (ClampMin = "2.0", ClampMax = "60.0"))
    float RequestTimeoutSeconds = 10.0f;
};

/**
 * Where a trade currently is. Drives the UI directly.
 */
UENUM(BlueprintType)
enum class EGF_TradeState : uint8
{
    // No trade in progress.
    Idle                    UMETA(DisplayName = "Idle"),

    // Uploading our offer and claiming the code.
    Joining                 UMETA(DisplayName = "Joining"),

    // We hold the code; nobody else has entered it yet.
    WaitingForPartner       UMETA(DisplayName = "Waiting For Partner"),

    // Both players are in and we know who they are, but nobody has chosen a
    // Creature yet. This is where the box/party screen opens.
    Connected               UMETA(DisplayName = "Connected"),

    // We have chosen and sent ours; the partner has not chosen yet.
    OfferSent               UMETA(DisplayName = "Offer Sent"),

    // Both Creature are on the table. Waiting on the local player to accept or
    // back out.
    OfferReceived           UMETA(DisplayName = "Offer Received"),

    // We accepted. The partner has not yet.
    AwaitingPartnerConfirm  UMETA(DisplayName = "Awaiting Partner Confirm"),

    // Both accepted; the swap is being written to the save.
    Committing              UMETA(DisplayName = "Committing"),

    // Done. The received Creature is in the party.
    Completed               UMETA(DisplayName = "Completed"),

    // Cancelled by either side. Nothing changed hands.
    Cancelled               UMETA(DisplayName = "Cancelled"),

    // Something went wrong. Nothing changed hands. See GetLastError().
    Failed                  UMETA(DisplayName = "Failed")
};

/**
 * How far a trade had got when the game stopped.
 *
 * The distinction matters because the two phases need different recovery. A
 * trade interrupted mid-swap can be finished from local data alone. One
 * interrupted after confirming cannot -- only the relay knows whether the
 * partner also confirmed, so recovery has to go ask.
 */
UENUM()
enum class EGF_PendingTradePhase : uint8
{
    /** Nothing to recover. */
    None,

    /**
     * We accepted and told the relay, but had not yet seen the partner accept.
     * The partner may or may not have committed, so the relay is the only
     * authority on what actually happened.
     */
    AwaitingCommit,

    /** Both sides confirmed and the party was being rewritten. Replay locally. */
    Committing
};

/**
 * Crash-safety record covering the whole window in which a trade can go wrong.
 *
 * Written when the player ACCEPTS (not when the swap starts), because the
 * dangerous window opens the moment we tell the relay yes: from there on the
 * partner can commit, and if we die before finding that out we would otherwise
 * have no idea a trade was ever owed to us.
 *
 * Cleared only on a definitively finished trade -- success, or the relay saying
 * the session was cancelled. Never cleared by a failure or a reset, because a
 * failure is exactly when it is needed.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_TradeSave : public USaveGame
{
    GENERATED_BODY()

public:
    static const FString SlotName;
    static const int32   UserIndex;

    UPROPERTY()
    EGF_PendingTradePhase Phase = EGF_PendingTradePhase::None;

    /** Identifies the session to the relay when re-polling after a crash. */
    UPROPERTY()
    FString LinkCode;

    UPROPERTY()
    FString PeerId;

    /**
     * Where the outgoing Creature came from, and where the incoming one goes.
     *
     * True means a PC box, and VaultPageIndex is meaningful. False means the party,
     * and VaultPageIndex is ignored. Recorded rather than assumed because recovery
     * runs in a later process that has no idea which screen the trade started
     * from -- writing a box Creature into a party slot would overwrite a
     * different Creature entirely.
     */
    UPROPERTY()
    bool bFromVault = false;

    UPROPERTY()
    int32 VaultPageIndex = INDEX_NONE;

    /** Party slot, or slot within VaultPageIndex when bFromVault is true. */
    UPROPERTY()
    int32 PartyIndex = INDEX_NONE;

    /**
     * Identity of the Creature we were giving away.
     *
     * This is what makes recovery idempotent: on the next launch, if this
     * Creature is still sitting in the slot then the swap never happened and we
     * redo it; if it is gone, the swap already landed and we just clear the
     * record. Relies on UniqueID being SaveGame -- it was not, until the link
     * trade work fixed it.
     */
    UPROPERTY()
    FGuid OutgoingUniqueID;

    /** The partner's Creature, still in wire form. */
    UPROPERTY()
    FString IncomingPayload;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnTradeStateChanged, EGF_TradeState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnTradePartnerOffer, const FGF_CreatureInstanceData&, Offer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnTradeCompleted, const FGF_CreatureInstanceData&, Received);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnTradeFailed, const FString&, Reason);

/**
 * Player-to-player trading over a link code.
 *
 * WHY THIS IS NOT UE MULTIPLAYER
 * ------------------------------
 * A trade is a single atomic exchange of about a kilobyte. Modelling it as
 * networked gameplay would drag in a NetDriver, replication, a listen server and
 * NAT traversal to move data smaller than one texture -- which is exactly how
 * this feature dies in fangames. Instead both clients talk to a dumb HTTP relay
 * that pairs them by code and forwards two blobs. Nothing here is replicated,
 * no GameMode changes, and the game stays single-player in every other respect.
 *
 * THE PROTOCOL (must stay in lockstep with the Worker in Tools/TradeRelay/)
 * ------------------------------------------------------------------------
 * All bodies are JSON. Base is <RelayBaseUrl>/v1/session/<code>.
 *
 *   POST .../join     { peerId, contentVersion, payload }
 *                     -> 200 { state, partnerPayload?, partnerConfirmed, selfConfirmed }
 *                     -> 409 { error: "session_full" }
 *   GET  ...?peerId=  -> 200 { state, partnerPayload?, partnerConfirmed, selfConfirmed }
 *                     -> 404 { error: "not_found" }
 *   POST .../confirm  { peerId }
 *                     -> 200 { state, ... }   state becomes "committed" once both confirmed
 *   POST .../cancel   { peerId }
 *                     -> 200 { state: "cancelled" }
 *
 * Server states: waiting (one peer) -> paired (two peers) -> committed (both
 * confirmed, terminal) or cancelled (terminal).
 *
 * WHAT THE RELAY IS NOT
 * ---------------------
 * It never parses, validates or stores Creature. It sees two opaque strings and
 * forwards them. That keeps it free to run, trivial to audit and pointless to
 * attack -- but it also means the RECEIVING CLIENT is the only thing checking
 * legality, which is why UGF_CreatureCodec::DecodeCreature validates on every
 * decode and cannot be bypassed.
 *
 * KNOWN LIMITATION
 * ----------------
 * Duping cannot be prevented without a server that owns save state. Two players
 * can back up saves and re-trade. Accepted deliberately: this is a fangame with
 * no ladder, and the alternative is hosting authoritative saves, which costs
 * money and makes us responsible for everyone's progress.
 */
UCLASS()
class GAMMAFRAMEWORKCREATURES_API UGF_TradeSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** IGF_ResettableState - abandon any in-flight trade and go back to Idle. */
    virtual void ResetToBootState() override;

    //--------------------
    // EVENTS
    //--------------------

    UPROPERTY(BlueprintAssignable, Category = "Trading")
    FGF_OnTradeStateChanged OnTradeStateChanged;

    /** The partner's Creature arrived and is ready to show. */
    UPROPERTY(BlueprintAssignable, Category = "Trading")
    FGF_OnTradePartnerOffer OnPartnerOfferReceived;

    /** The swap is done and the received Creature is in the party. */
    UPROPERTY(BlueprintAssignable, Category = "Trading")
    FGF_OnTradeCompleted OnTradeCompleted;

    /** Nothing changed hands. Reason is safe to show the player. */
    UPROPERTY(BlueprintAssignable, Category = "Trading")
    FGF_OnTradeFailed OnTradeFailed;

    //--------------------
    // FLOW
    //--------------------

    /**
     * Claim a link code and wait for a partner. No Creature is chosen yet.
     *
     * Both players type the same code. Once both are in, the state becomes
     * Connected and GetPartnerTamerName() is populated -- which is what lets
     * the box screen ask "Trade this Creature with <name>?" before anything is
     * picked.
     *
     * @return false if a trade is already running or the code is unusable.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading")
    bool ConnectToPartner(const FString& LinkCode);

    /**
     * Put a party Creature on the table. Valid from Connected onwards.
     *
     * Nothing leaves the party here -- the Creature is only given up once both
     * sides confirm. Calling again before confirming replaces the offer, so a
     * player can change their mind.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading")
    bool OfferPartyCreature(int32 PartyIndex);

    /** Put a Creature from a PC box on the table. Same rules as the party version. */
    UFUNCTION(BlueprintCallable, Category = "Trading")
    bool OfferVaultCreature(int32 VaultPageIndex, int32 SlotIndex);

    /**
     * Accept the offer on the table. Safe to call only in OfferReceived.
     * The swap happens once the partner also confirms.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading")
    bool ConfirmTrade();

    /** Back out. Legal at any point before the commit begins. */
    UFUNCTION(BlueprintCallable, Category = "Trading")
    void CancelTrade();

    //--------------------
    // QUERIES
    //--------------------

    UFUNCTION(BlueprintPure, Category = "Trading")
    EGF_TradeState GetTradeState() const { return State; }

    UFUNCTION(BlueprintPure, Category = "Trading")
    bool IsTradeInProgress() const;

    /** Only meaningful from OfferReceived onwards. */
    UFUNCTION(BlueprintPure, Category = "Trading")
    const FGF_CreatureInstanceData& GetPartnerOffer() const { return PartnerOffer; }

    /**
     * The Creature the local player put on the table.
     *
     * Kept as a whole copy, not just its encoded blob, specifically so the trade
     * animation can still show what was GIVEN AWAY -- by the time the animation
     * plays, the commit has already overwritten that slot, so there is nowhere
     * else left to read it from.
     *
     * Valid from the moment an offer is made until the next trade starts.
     */
    UFUNCTION(BlueprintPure, Category = "Trading")
    const FGF_CreatureInstanceData& GetOfferedCreature() const { return OutgoingCreature; }

    /**
     * The partner's tamer name, known from Connected onwards. Empty before that.
     *
     * Already sanitised: it arrives from another player's machine and is drawn
     * through the dialogue parser, so it is filtered the same way a traded
     * Creature's nickname is. Safe to drop straight into a dialogue line.
     */
    UFUNCTION(BlueprintPure, Category = "Trading")
    FString GetPartnerTamerName() const { return PartnerTamerName; }

    /** True once the local player has put a Creature on the table. */
    UFUNCTION(BlueprintPure, Category = "Trading")
    bool HasOfferedCreature() const { return !OutgoingPayload.IsEmpty(); }

    /**
     * Publishes {TradePartner} and {TradeOffer} for dialogue substitution, so a
     * line can read "Trade this Creature with {TradePartner}?".
     *
     * CALL THIS IMMEDIATELY BEFORE StartDialogue, every time. Dialogue variables
     * are wiped automatically when a dialogue ends, so setting them once when
     * the partner connects would work for the first prompt and silently produce
     * an empty name for every prompt after it.
     *
     * {TradePartner} is the partner's tamer name, already sanitised.
     * {TradeOffer}   is the Creature they are offering, empty until they choose.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading")
    void PublishTradeDialogueTokens() const;

    /** Player-facing text for the most recent failure. */
    UFUNCTION(BlueprintPure, Category = "Trading")
    FString GetLastError() const { return LastError; }

    /**
     * One line describing what the trade is doing, ready to drop into a widget.
     *
     * Lives here rather than as a Blueprint switch so every screen that shows
     * trade status says the same thing, and so a new state cannot silently show
     * blank text on a screen someone forgot to update.
     */
    UFUNCTION(BlueprintPure, Category = "Trading")
    FText GetTradeStatusText() const;

    UFUNCTION(BlueprintPure, Category = "Trading")
    FString GetActiveLinkCode() const { return LinkCode; }

    /** Seconds left before we stop waiting for a partner. Zero when not waiting. */
    UFUNCTION(BlueprintPure, Category = "Trading")
    float GetSecondsUntilTimeout() const;

    //--------------------
    // HELPERS
    //--------------------

    /**
     * Replay a trade that was interrupted mid-commit, if one was recorded.
     *
     * Called automatically at subsystem init, but init runs before the save is
     * read -- so if the party is not loaded yet the record is left alone and
     * nothing happens. Call this again from the load flow, once the party is
     * actually populated, to make recovery reliable.
     *
     * Safe to call any number of times; it is a no-op with no pending record.
     */
    UFUNCTION(BlueprintCallable, Category = "Trading")
    void RecoverPendingTradeIfAny();

    /** A fresh random 8-digit code, for the "host" side of the UI. */
    UFUNCTION(BlueprintPure, Category = "Trading")
    static FString GenerateLinkCode();

    /**
     * Codes are 4-16 characters of A-Z and 0-9, case-insensitive.
     * Kept loose so a code can be read aloud over voice chat without ambiguity
     * about case, but tight enough that it is always URL-safe.
     */
    UFUNCTION(BlueprintPure, Category = "Trading")
    static bool IsValidLinkCode(const FString& Code);

private:
    //--------------------
    // STATE
    //--------------------

    UPROPERTY(Transient)
    EGF_TradeState State = EGF_TradeState::Idle;

    UPROPERTY(Transient)
    FGF_CreatureInstanceData PartnerOffer;

    /** What we offered, kept whole for the trade animation. See GetOfferedCreature. */
    UPROPERTY(Transient)
    FGF_CreatureInstanceData OutgoingCreature;

    /** Our own random per-trade identity, so the relay can tell the two sides apart. */
    FString PeerId;
    FString LinkCode;
    FString OutgoingPayload;
    FString IncomingPayload;
    FString LastError;
    FString PartnerTamerName;

    /** Where the offered Creature came from; mirrors the fields on UGF_TradeSave. */
    bool bOutgoingFromVault = false;
    int32 OutgoingVaultPageIndex = INDEX_NONE;
    int32 OutgoingSlotIndex = INDEX_NONE;
    FGuid OutgoingUniqueID;

    double WaitDeadlineSeconds = 0.0;
    bool bPartnerOfferHandled = false;

    /** Guards against a second request going out while one is in flight. */
    bool bRequestInFlight = false;

    FTSTicker::FDelegateHandle PollHandle;

    /**
     * Retries recovery until the party actually exists.
     *
     * Recovery runs at Initialize, which is before the save is read, so it
     * almost always defers. Making that retry the caller's job would mean a
     * Creature is only recovered if someone remembered to wire a Blueprint call
     * -- an unacceptable dependency for the one path that exists to stop a
     * Creature being lost. So the subsystem keeps asking on its own.
     */
    FTSTicker::FDelegateHandle RecoveryRetryHandle;
    int32 RecoveryAttemptsRemaining = 0;

    bool RetryRecovery(float DeltaTime);
    void StopRecoveryRetries();

    //--------------------
    // INTERNALS
    //--------------------

    void SetState(EGF_TradeState NewState);
    void FailTrade(const FString& Reason);

    bool Poll(float DeltaTime);
    void StartPolling();
    void StopPolling();

    /** Code is passed explicitly so recovery can build a URL for a session this run never joined. */
    static FString BuildSessionUrl(const FString& Code, const FString& Suffix);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FString& Verb, const FString& Url);

    void SendJoin();
    void SendOffer();
    void SendPoll();
    void SendConfirm();

    /** Shared by OfferPartyCreature and OfferVaultCreature once the source is resolved. */
    bool OfferCreature(const FGF_CreatureInstanceData& Creature, bool bFromVault, int32 VaultPageIndex, int32 SlotIndex);

    /** Write the received Creature back wherever the offered one came from. */
    bool WriteCreatureToSource(const FGF_CreatureInstanceData& Creature,
        bool bFromVault, int32 VaultPageIndex, int32 SlotIndex);

    /** Read whatever currently occupies a source slot, party or box. */
    bool ReadCreatureFromSource(FGF_CreatureInstanceData& OutCreature,
        bool bFromVault, int32 VaultPageIndex, int32 SlotIndex) const;

    /** Shared handling for join/poll/confirm, which all return the same body. */
    void HandleSessionResponse(const FString& Body);

    /** Decode + validate the partner's blob, then announce it once. */
    void AdoptPartnerOffer(const FString& Payload);

    /** Both sides confirmed. Swap the party entry and clear the recovery record. */
    void CommitTrade();

    /** Persist where we are, so a crash from here on is recoverable. */
    void WritePendingRecord(EGF_PendingTradePhase Phase);

    /**
     * Apply a recorded swap to the party, idempotently.
     * @return true if the record can be cleared afterwards.
     */
    bool ApplyRecordedSwap(const UGF_TradeSave& Save);

    /** Ask the relay how an interrupted, already-confirmed trade actually ended. */
    void ResolveInterruptedTrade(const FString& Code, const FString& RecoveryPeerId);

    UGF_TradeSave* LoadOrCreateTradeSave() const;
    void ClearPendingCommit();

    class UGF_CreatureManagerSubsystem* GetCreatureManager() const;
};
