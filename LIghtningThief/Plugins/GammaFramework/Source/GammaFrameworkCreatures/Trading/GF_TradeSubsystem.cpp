// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_TradeSubsystem.h"

#include "GF_CreatureCodec.h"
#include "Dialogue/GF_DialogueSubsystem.h"
#include "GF_VaultSystem.h"
#include "GF_CreatureManagerSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrade, Log, All);

const FString UGF_TradeSave::SlotName  = TEXT("TradeSlot");
const int32   UGF_TradeSave::UserIndex = 0;

namespace
{
    // Server-side state strings. These are the contract with the Worker; changing
    // one means changing Tools/TradeRelay/worker.js in the same commit.
    const TCHAR* StateWaiting   = TEXT("waiting");
    const TCHAR* StateConnected = TEXT("connected");
    const TCHAR* StatePaired    = TEXT("paired");
    const TCHAR* StateCommitted = TEXT("committed");
    const TCHAR* StateCancelled = TEXT("cancelled");

    FString MakeJsonBody(const TMap<FString, FString>& Fields)
    {
        FString Out;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
        Writer->WriteObjectStart();
        for (const TPair<FString, FString>& Field : Fields)
        {
            Writer->WriteValue(Field.Key, Field.Value);
        }
        Writer->WriteObjectEnd();
        Writer->Close();
        return Out;
    }
}

#if !UE_BUILD_SHIPPING
namespace
{
    UGF_TradeSubsystem* FindTradeSubsystem(UWorld* World)
    {
        const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
        return GameInstance ? GameInstance->GetSubsystem<UGF_TradeSubsystem>() : nullptr;
    }

    const TCHAR* DescribeTradeState(EGF_TradeState State)
    {
        switch (State)
        {
        case EGF_TradeState::Idle:                   return TEXT("Idle");
        case EGF_TradeState::Joining:                return TEXT("Joining");
        case EGF_TradeState::WaitingForPartner:      return TEXT("WaitingForPartner");
        case EGF_TradeState::Connected:              return TEXT("Connected");
        case EGF_TradeState::OfferSent:              return TEXT("OfferSent");
        case EGF_TradeState::OfferReceived:          return TEXT("OfferReceived");
        case EGF_TradeState::AwaitingPartnerConfirm: return TEXT("AwaitingPartnerConfirm");
        case EGF_TradeState::Committing:             return TEXT("Committing");
        case EGF_TradeState::Completed:              return TEXT("Completed");
        case EGF_TradeState::Cancelled:              return TEXT("Cancelled");
        case EGF_TradeState::Failed:                 return TEXT("Failed");
        default:                                     return TEXT("Unknown");
        }
    }
}

// Drives a trade with no UI, so the transport can be tested against a relay
// before any widget exists. Diagnostics only -- kept out of shipping builds.
static FAutoConsoleCommandWithWorldAndArgs GTradeConnectCommand(
    TEXT("GE.Trade.Connect"),
    TEXT("GE.Trade.Connect <linkCode> - claim a link code and wait for a partner."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World)
        {
            UGF_TradeSubsystem* Trade = FindTradeSubsystem(World);
            if (Trade == nullptr)
            {
                UE_LOG(LogTrade, Error, TEXT("Trade subsystem unavailable."));
                return;
            }

            if (Args.Num() < 1)
            {
                UE_LOG(LogTrade, Error, TEXT("Usage: GE.Trade.Connect <linkCode>"));
                return;
            }

            if (Trade->ConnectToPartner(Args[0]))
            {
                UE_LOG(LogTrade, Display, TEXT("Connecting on code %s."), *Args[0]);
            }
            else
            {
                UE_LOG(LogTrade, Error, TEXT("Could not connect: %s"), *Trade->GetLastError());
            }
        }));

static FAutoConsoleCommandWithWorldAndArgs GTradeOfferCommand(
    TEXT("GE.Trade.Offer"),
    TEXT("GE.Trade.Offer <partySlot> | GE.Trade.Offer box <boxIndex> <slot> - put a Creature on the table."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World)
        {
            UGF_TradeSubsystem* Trade = FindTradeSubsystem(World);
            if (Trade == nullptr)
            {
                UE_LOG(LogTrade, Error, TEXT("Trade subsystem unavailable."));
                return;
            }

            bool bOk = false;

            if (Args.Num() >= 3 && Args[0].Equals(TEXT("box"), ESearchCase::IgnoreCase))
            {
                bOk = Trade->OfferVaultCreature(FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2]));
            }
            else if (Args.Num() >= 1)
            {
                bOk = Trade->OfferPartyCreature(FCString::Atoi(*Args[0]));
            }
            else
            {
                UE_LOG(LogTrade, Error, TEXT("Usage: GE.Trade.Offer <partySlot> | GE.Trade.Offer box <boxIndex> <slot>"));
                return;
            }

            if (!bOk)
            {
                UE_LOG(LogTrade, Error, TEXT("Could not offer: %s"), *Trade->GetLastError());
            }
        }));

static FAutoConsoleCommandWithWorld GTradeConfirmCommand(
    TEXT("GE.Trade.Confirm"),
    TEXT("Accept the offer currently on the table."),
    FConsoleCommandWithWorldDelegate::CreateLambda(
        [](UWorld* World)
        {
            UGF_TradeSubsystem* Trade = FindTradeSubsystem(World);
            if (Trade && !Trade->ConfirmTrade())
            {
                UE_LOG(LogTrade, Error, TEXT("Could not confirm: %s"), *Trade->GetLastError());
            }
        }));

static FAutoConsoleCommandWithWorld GTradeCancelCommand(
    TEXT("GE.Trade.Cancel"),
    TEXT("Back out of the trade in progress."),
    FConsoleCommandWithWorldDelegate::CreateLambda(
        [](UWorld* World)
        {
            if (UGF_TradeSubsystem* Trade = FindTradeSubsystem(World))
            {
                Trade->CancelTrade();
            }
        }));

static FAutoConsoleCommandWithWorld GTradeStatusCommand(
    TEXT("GE.Trade.Status"),
    TEXT("Print the current trade state, code, and last error."),
    FConsoleCommandWithWorldDelegate::CreateLambda(
        [](UWorld* World)
        {
            UGF_TradeSubsystem* Trade = FindTradeSubsystem(World);
            if (Trade == nullptr)
            {
                UE_LOG(LogTrade, Error, TEXT("Trade subsystem unavailable."));
                return;
            }

            UE_LOG(LogTrade, Display, TEXT("State: %s | Code: %s | Partner: %s | Relay: %s"),
                DescribeTradeState(Trade->GetTradeState()),
                Trade->GetActiveLinkCode().IsEmpty() ? TEXT("(none)") : *Trade->GetActiveLinkCode(),
                Trade->GetPartnerTamerName().IsEmpty() ? TEXT("(none)") : *Trade->GetPartnerTamerName(),
                *GetDefault<UGF_TradeSettings>()->RelayBaseUrl);

            if (Trade->GetTradeState() == EGF_TradeState::OfferReceived
                || Trade->GetTradeState() == EGF_TradeState::AwaitingPartnerConfirm)
            {
                UE_LOG(LogTrade, Display, TEXT("On offer: %s (Lv %d)"),
                    *Trade->GetPartnerOffer().GetDisplayName().ToString(),
                    Trade->GetPartnerOffer().Level);
            }

            if (!Trade->GetLastError().IsEmpty())
            {
                UE_LOG(LogTrade, Display, TEXT("Last error: %s"), *Trade->GetLastError());
            }
        }));
#endif

//--------------------
// LIFECYCLE
//--------------------

void UGF_TradeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    // Without this the manager comes back null here: a subsystem fetched during
    // another subsystem's Initialize is not guaranteed to exist yet, and it fails
    // silently rather than loudly.
    Collection.InitializeDependency(UGF_CreatureManagerSubsystem::StaticClass());

    Super::Initialize(Collection);

    // Best-effort. Init runs before the save is read, so this usually defers to
    // the explicit call from the load flow.
    RecoverPendingTradeIfAny();
}

void UGF_TradeSubsystem::Deinitialize()
{
    StopPolling();
    StopRecoveryRetries();
    Super::Deinitialize();
}

void UGF_TradeSubsystem::ResetToBootState()
{
    // Deliberately does NOT touch the pending-trade record on disk. That record
    // describes a trade the player may still be owed; a New Game or soft reset
    // must not silently discard it, or an interrupted trade eats a Creature
    // permanently. Only a resolved outcome clears it -- a completed swap, or the
    // relay confirming the session ended without one.
    StopPolling();

    State = EGF_TradeState::Idle;
    PartnerOffer = FGF_CreatureInstanceData();
    PeerId.Reset();
    LinkCode.Reset();
    OutgoingPayload.Reset();
    IncomingPayload.Reset();
    LastError.Reset();
    PartnerTamerName.Reset();
    OutgoingCreature = FGF_CreatureInstanceData();
    bOutgoingFromVault = false;
    OutgoingVaultPageIndex = INDEX_NONE;
    OutgoingSlotIndex = INDEX_NONE;
    OutgoingUniqueID.Invalidate();
    WaitDeadlineSeconds = 0.0;
    bPartnerOfferHandled = false;
    bRequestInFlight = false;
}

//--------------------
// PUBLIC FLOW
//--------------------

bool UGF_TradeSubsystem::IsTradeInProgress() const
{
    return State != EGF_TradeState::Idle
        && State != EGF_TradeState::Completed
        && State != EGF_TradeState::Cancelled
        && State != EGF_TradeState::Failed;
}

float UGF_TradeSubsystem::GetSecondsUntilTimeout() const
{
    if (State != EGF_TradeState::WaitingForPartner)
    {
        return 0.0f;
    }

    return FMath::Max(0.0f, static_cast<float>(WaitDeadlineSeconds - FPlatformTime::Seconds()));
}

void UGF_TradeSubsystem::PublishTradeDialogueTokens() const
{
    const UGameInstance* GameInstance = GetGameInstance();
    UGF_DialogueSubsystem* Dialogue = GameInstance
        ? GameInstance->GetSubsystem<UGF_DialogueSubsystem>()
        : nullptr;

    if (Dialogue == nullptr)
    {
        return;
    }

    Dialogue->SetDialogueVariable(FName("TradePartner"), PartnerTamerName);

    // Empty until the partner chooses, which is a legitimate state: the prompt
    // asking what to offer appears before they have picked anything.
    Dialogue->SetDialogueVariable(FName("TradeOffer"),
        bPartnerOfferHandled ? PartnerOffer.GetDisplayName().ToString() : FString());

    // The code the player is waiting on, for the "tell your friend this" screen.
    Dialogue->SetDialogueVariable(FName("TradeCode"), LinkCode);

    // Why the trade ended badly. Already phrased for players, and far more use
    // than a generic "trade failed" -- it distinguishes a wrong code from a
    // version mismatch from an expired session.
    Dialogue->SetDialogueVariable(FName("TradeError"), LastError);
}

FText UGF_TradeSubsystem::GetTradeStatusText() const
{
    switch (State)
    {
    case EGF_TradeState::Idle:
        return NSLOCTEXT("Trading", "TradeIdle", "Ready to trade.");

    case EGF_TradeState::Joining:
        return NSLOCTEXT("Trading", "TradeJoining", "Connecting...");

    case EGF_TradeState::WaitingForPartner:
        return FText::Format(
            NSLOCTEXT("Trading", "TradeWaiting", "Waiting for a partner on code {0}..."),
            FText::FromString(LinkCode));

    case EGF_TradeState::Connected:
        return FText::Format(
            NSLOCTEXT("Trading", "TradeConnected", "Connected to {0}. Choose a Creature to trade."),
            FText::FromString(PartnerTamerName));

    case EGF_TradeState::OfferSent:
        return FText::Format(
            NSLOCTEXT("Trading", "TradeOfferSent", "Waiting for {0} to choose..."),
            FText::FromString(PartnerTamerName));

    case EGF_TradeState::OfferReceived:
        return FText::Format(
            NSLOCTEXT("Trading", "TradeOffered", "{0} is offering {1}."),
            FText::FromString(PartnerTamerName),
            FText::FromName(PartnerOffer.GetDisplayName()));

    case EGF_TradeState::AwaitingPartnerConfirm:
        return NSLOCTEXT("Trading", "TradeAwaiting", "Waiting for the other player to accept...");

    case EGF_TradeState::Committing:
        return NSLOCTEXT("Trading", "TradeCommitting", "Trading...");

    case EGF_TradeState::Completed:
        return FText::Format(
            NSLOCTEXT("Trading", "TradeDone", "You received {0}!"),
            FText::FromName(PartnerOffer.GetDisplayName()));

    case EGF_TradeState::Cancelled:
        // LastError carries who cancelled and why, which is more useful than a
        // bare "Cancelled" -- but it is empty when the local player cancelled.
        return LastError.IsEmpty()
            ? NSLOCTEXT("Trading", "TradeCancelled", "Trade cancelled.")
            : FText::FromString(LastError);

    case EGF_TradeState::Failed:
        return LastError.IsEmpty()
            ? NSLOCTEXT("Trading", "TradeFailed", "The trade could not be completed.")
            : FText::FromString(LastError);

    default:
        return FText::GetEmpty();
    }
}

FString UGF_TradeSubsystem::GenerateLinkCode()
{
    FString Code;
    Code.Reserve(8);
    for (int32 Index = 0; Index < 8; ++Index)
    {
        Code.AppendChar(static_cast<TCHAR>(TEXT('0') + FMath::RandRange(0, 9)));
    }
    return Code;
}

bool UGF_TradeSubsystem::IsValidLinkCode(const FString& Code)
{
    const FString Trimmed = Code.TrimStartAndEnd();
    if (Trimmed.Len() < 4 || Trimmed.Len() > 16)
    {
        return false;
    }

    for (const TCHAR Char : Trimmed)
    {
        const bool bIsDigit = (Char >= TEXT('0') && Char <= TEXT('9'));
        const bool bIsLetter = (Char >= TEXT('A') && Char <= TEXT('Z'))
                            || (Char >= TEXT('a') && Char <= TEXT('z'));
        if (!bIsDigit && !bIsLetter)
        {
            return false;
        }
    }

    return true;
}

bool UGF_TradeSubsystem::ConnectToPartner(const FString& InLinkCode)
{
    if (IsTradeInProgress())
    {
        LastError = TEXT("A trade is already in progress.");
        return false;
    }

    if (!IsValidLinkCode(InLinkCode))
    {
        LastError = TEXT("That link code is not valid. Use 4-16 letters or numbers.");
        return false;
    }

    // Catch a mangled relay URL here rather than as a baffling DNS failure ten
    // seconds later. The usual cause is an unquoted URL in DefaultGame.ini: UE
    // parses ini with SwallowDoubleSlashComments, so an unquoted // starts a
    // comment and "http://host" silently becomes "http:".
    const FString ConfiguredUrl = GetDefault<UGF_TradeSettings>()->RelayBaseUrl;
    if (!ConfiguredUrl.Contains(TEXT("://")))
    {
        LastError = FString::Printf(
            TEXT("The trade server address is misconfigured ('%s'). In DefaultGame.ini, RelayBaseUrl must be wrapped in quotes."),
            *ConfiguredUrl);
        UE_LOG(LogTrade, Error, TEXT("%s"), *LastError);
        return false;
    }

    const UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr)
    {
        LastError = TEXT("Creature data is unavailable.");
        return false;
    }

    LinkCode = InLinkCode.TrimStartAndEnd().ToUpper();
    PeerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

    OutgoingPayload.Reset();
    IncomingPayload.Reset();
    PartnerTamerName.Reset();
    PartnerOffer = FGF_CreatureInstanceData();
    OutgoingCreature = FGF_CreatureInstanceData();
    bOutgoingFromVault = false;
    OutgoingVaultPageIndex = INDEX_NONE;
    OutgoingSlotIndex = INDEX_NONE;
    OutgoingUniqueID.Invalidate();
    bPartnerOfferHandled = false;
    LastError.Reset();

    const UGF_TradeSettings* Settings = GetDefault<UGF_TradeSettings>();
    WaitDeadlineSeconds = FPlatformTime::Seconds() + Settings->PartnerWaitTimeoutSeconds;

    SetState(EGF_TradeState::Joining);
    SendJoin();
    return true;
}

bool UGF_TradeSubsystem::OfferPartyCreature(int32 PartyIndex)
{
    const UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr)
    {
        LastError = TEXT("Creature data is unavailable.");
        return false;
    }

    FGF_CreatureInstanceData Outgoing;
    if (!Manager->GetPartyCreatureData(PartyIndex, Outgoing))
    {
        LastError = TEXT("That party slot is empty.");
        UE_LOG(LogTrade, Error, TEXT("Offer refused: party slot %d is empty."), PartyIndex);
        return false;
    }

    // Refuse to trade away the last able Creature: the player would be left
    // walking around with nothing, which softlocks the next wild encounter.
    // Only applies to the party -- emptying a box slot harms nobody.
    //
    // Counts NON-EGG party members, not raw party size. A party of
    // [Sprigling, Egg] passes a size check but leaves the player holding only an
    // egg once the Sprigling goes, which cannot battle and cannot be healed out of
    // -- the exact softlock this guard exists to prevent. A downed Creature is
    // fine by comparison: a Creature Center fixes that.
    int32 UsableRemaining = 0;
    for (int32 OtherIndex = 0; OtherIndex < Manager->GetPartySize(); ++OtherIndex)
    {
        if (OtherIndex == PartyIndex)
        {
            continue;
        }

        FGF_CreatureInstanceData Other;
        if (Manager->GetPartyCreatureData(OtherIndex, Other) && !Other.IsEgg())
        {
            ++UsableRemaining;
        }
    }

    if (UsableRemaining == 0)
    {
        LastError = TEXT("You cannot trade away your last Creature.");
        UE_LOG(LogTrade, Error,
            TEXT("Offer refused: trading party slot %d would leave no non-egg Creature."), PartyIndex);
        return false;
    }

    return OfferCreature(Outgoing, /*bFromVault=*/false, INDEX_NONE, PartyIndex);
}

bool UGF_TradeSubsystem::OfferVaultCreature(int32 VaultPageIndex, int32 SlotIndex)
{
    const UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr)
    {
        LastError = TEXT("Creature data is unavailable.");
        return false;
    }

    FGF_CreatureInstanceData Outgoing;
    if (!Manager->VaultSystem->GetVaultCreature(VaultPageIndex, SlotIndex, Outgoing))
    {
        LastError = TEXT("That box slot is empty.");
        UE_LOG(LogTrade, Error, TEXT("Offer refused: box %d slot %d is empty."), VaultPageIndex, SlotIndex);
        return false;
    }

    return OfferCreature(Outgoing, /*bFromVault=*/true, VaultPageIndex, SlotIndex);
}

bool UGF_TradeSubsystem::OfferCreature(const FGF_CreatureInstanceData& Creature,
    bool bFromVault, int32 VaultPageIndex, int32 SlotIndex)
{
    // Connected is the earliest point a Creature can be offered: the relay
    // refuses an offer with nobody to receive it, and OfferSent means we have
    // already chosen (re-offering to change your mind is allowed).
    if (State != EGF_TradeState::Connected
        && State != EGF_TradeState::OfferSent
        && State != EGF_TradeState::OfferReceived)
    {
        LastError = TEXT("You are not connected to a trade partner yet.");
        return false;
    }

    FString EncodeError;
    FString Encoded;
    if (!UGF_CreatureCodec::EncodeCreature(Creature, Encoded, EncodeError))
    {
        LastError = FString::Printf(TEXT("That Creature cannot be traded: %s"), *EncodeError);

        // Name the BOX as well as the slot. Without the box index this line
        // cannot distinguish "wrong slot" from "wrong box", which is precisely
        // the ambiguity that matters when a picker misreports which Creature it
        // handed over.
        UE_LOG(LogTrade, Error, TEXT("Offer refused (%s, %s): %s"),
            bFromVault
                ? *FString::Printf(TEXT("box %d slot %d"), VaultPageIndex, SlotIndex)
                : *FString::Printf(TEXT("party slot %d"), SlotIndex),
            *Creature.GetDisplayName().ToString(),
            *LastError);
        return false;
    }

    UE_LOG(LogTrade, Log, TEXT("Offering %s from %s."),
        *Creature.GetDisplayName().ToString(),
        bFromVault
            ? *FString::Printf(TEXT("box %d slot %d"), VaultPageIndex, SlotIndex)
            : *FString::Printf(TEXT("party slot %d"), SlotIndex));

    OutgoingPayload = Encoded;

    // Copy ASSIGNMENT (compiler-generated, complete). The animation needs this
    // after the commit has already overwritten the slot it came from.
    OutgoingCreature = Creature;

    bOutgoingFromVault = bFromVault;
    OutgoingVaultPageIndex = VaultPageIndex;
    OutgoingSlotIndex = SlotIndex;
    OutgoingUniqueID = Creature.UniqueID;
    LastError.Reset();

    SendOffer();
    return true;
}

bool UGF_TradeSubsystem::ConfirmTrade()
{
    if (State != EGF_TradeState::OfferReceived)
    {
        LastError = TEXT("There is no offer to accept right now.");
        return false;
    }

    // Record BEFORE telling the relay yes. The instant that request lands, the
    // partner may see "committed" and hand over their Creature -- so from here on
    // a trade can be owed to us even if this process dies one line later. With
    // no record written first, the next launch would have no idea.
    WritePendingRecord(EGF_PendingTradePhase::AwaitingCommit);

    SetState(EGF_TradeState::AwaitingPartnerConfirm);
    SendConfirm();
    return true;
}

void UGF_TradeSubsystem::CancelTrade()
{
    if (!IsTradeInProgress())
    {
        return;
    }

    // Past this point the party has already been touched, or is about to be
    // inside a single call. Cancelling would leave the two saves disagreeing.
    if (State == EGF_TradeState::Committing)
    {
        UE_LOG(LogTrade, Warning, TEXT("Ignoring cancel: the trade is already committing."));
        return;
    }

    StopPolling();

    if (LinkCode.IsEmpty() || PeerId.IsEmpty())
    {
        SetState(EGF_TradeState::Cancelled);
        return;
    }

    // If we already accepted, cancelling is a race we might lose: the partner
    // may have accepted first, in which case the relay refuses the cancel and
    // reports "committed". Honouring that instead of forcing a local cancel is
    // what keeps the two saves agreeing.
    const bool bAlreadyConfirmed = (State == EGF_TradeState::AwaitingPartnerConfirm);

    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        MakeRequest(TEXT("POST"), BuildSessionUrl(LinkCode, TEXT("/cancel")));
    Request->SetContentAsString(MakeJsonBody({ { TEXT("peerId"), PeerId } }));

    TWeakObjectPtr<UGF_TradeSubsystem> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, bAlreadyConfirmed](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
        {
            UGF_TradeSubsystem* Self = WeakThis.Get();
            if (Self == nullptr || !bAlreadyConfirmed)
            {
                return;
            }

            // Could not reach the relay after confirming: we genuinely do not
            // know how this ended. Leave the pending record in place so the next
            // launch resolves it rather than guessing now.
            if (!bConnectedOk || !Response.IsValid())
            {
                UE_LOG(LogTrade, Warning,
                    TEXT("Cancel after confirm could not reach the relay. Leaving the trade to be resolved on next launch."));
                return;
            }

            Self->HandleSessionResponse(Response->GetContentAsString());
        });

    Request->ProcessRequest();

    // A cancel before confirming never wrote a record, and nothing was owed.
    if (!bAlreadyConfirmed)
    {
        SetState(EGF_TradeState::Cancelled);
    }
}

//--------------------
// POLLING
//--------------------

void UGF_TradeSubsystem::StartPolling()
{
    if (PollHandle.IsValid())
    {
        return;
    }

    const UGF_TradeSettings* Settings = GetDefault<UGF_TradeSettings>();
    PollHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UGF_TradeSubsystem::Poll),
        Settings->PollIntervalSeconds);
}

void UGF_TradeSubsystem::StopPolling()
{
    if (PollHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
        PollHandle.Reset();
    }
}

bool UGF_TradeSubsystem::Poll(float DeltaTime)
{
    if (!IsTradeInProgress())
    {
        PollHandle.Reset();
        return false;
    }

    if (State == EGF_TradeState::WaitingForPartner && FPlatformTime::Seconds() > WaitDeadlineSeconds)
    {
        FailTrade(TEXT("Nobody joined that code in time. Check you both typed the same one."));
        return false;
    }

    // One request at a time. A slow relay must not queue up a burst.
    if (!bRequestInFlight)
    {
        SendPoll();
    }

    return true;
}

//--------------------
// HTTP
//--------------------

FString UGF_TradeSubsystem::BuildSessionUrl(const FString& Code, const FString& Suffix)
{
    const UGF_TradeSettings* Settings = GetDefault<UGF_TradeSettings>();
    FString Base = Settings->RelayBaseUrl;
    while (Base.EndsWith(TEXT("/")))
    {
        Base.LeftChopInline(1);
    }

    return FString::Printf(TEXT("%s/v1/session/%s%s"), *Base, *Code, *Suffix);
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UGF_TradeSubsystem::MakeRequest(const FString& Verb, const FString& Url)
{
    const UGF_TradeSettings* Settings = GetDefault<UGF_TradeSettings>();

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(Verb);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetTimeout(Settings->RequestTimeoutSeconds);
    return Request;
}

void UGF_TradeSubsystem::SendJoin()
{
    bRequestInFlight = true;

    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        MakeRequest(TEXT("POST"), BuildSessionUrl(LinkCode, TEXT("/join")));

    // The tamer name is what lets the partner's box screen say "Trade this
    // Creature with <name>?" before either player has chosen anything.
    const UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    const FString TamerName = (Manager && Manager->VaultSystem)
        ? Manager->VaultSystem->PlayerName
        : FString();

    Request->SetContentAsString(MakeJsonBody({
        { TEXT("peerId"),         PeerId },
        { TEXT("contentVersion"), UGF_CreatureCodec::GetTradeContentVersion() },
        { TEXT("tamerName"),    TamerName }
    }));

    TWeakObjectPtr<UGF_TradeSubsystem> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
        {
            UGF_TradeSubsystem* Self = WeakThis.Get();
            if (Self == nullptr)
            {
                return;
            }

            Self->bRequestInFlight = false;

            if (!bConnectedOk || !Response.IsValid())
            {
                Self->FailTrade(TEXT("Could not reach the trade server. Check your internet connection."));
                return;
            }

            const int32 Code = Response->GetResponseCode();
            if (Code == 409)
            {
                // The relay returns 409 for two very different problems, and
                // telling them apart is the difference between "pick another
                // code" and "one of you needs to update".
                TSharedPtr<FJsonObject> Conflict;
                const TSharedRef<TJsonReader<>> ConflictReader =
                    TJsonReaderFactory<>::Create(Response->GetContentAsString());

                FString ConflictKind;
                if (FJsonSerializer::Deserialize(ConflictReader, Conflict) && Conflict.IsValid())
                {
                    Conflict->TryGetStringField(TEXT("error"), ConflictKind);
                }

                if (ConflictKind == TEXT("version_mismatch"))
                {
                    Self->FailTrade(TEXT("You and the other player are on different game versions. Both of you need the same build to trade."));
                }
                else
                {
                    Self->FailTrade(TEXT("That link code is already in use by two other players. Try a different one."));
                }
                return;
            }

            if (Code < 200 || Code > 299)
            {
                Self->FailTrade(FString::Printf(TEXT("The trade server refused the request (HTTP %d)."), Code));
                return;
            }

            Self->HandleSessionResponse(Response->GetContentAsString());
            Self->StartPolling();
        });

    Request->ProcessRequest();
}

void UGF_TradeSubsystem::SendOffer()
{
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        MakeRequest(TEXT("POST"), BuildSessionUrl(LinkCode, TEXT("/offer")));

    Request->SetContentAsString(MakeJsonBody({
        { TEXT("peerId"),  PeerId },
        { TEXT("payload"), OutgoingPayload }
    }));

    TWeakObjectPtr<UGF_TradeSubsystem> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
        {
            UGF_TradeSubsystem* Self = WeakThis.Get();
            if (Self == nullptr)
            {
                return;
            }

            if (!bConnectedOk || !Response.IsValid())
            {
                Self->FailTrade(TEXT("Could not send your Creature to the trade server."));
                return;
            }

            if (Response->GetResponseCode() < 200 || Response->GetResponseCode() > 299)
            {
                Self->FailTrade(TEXT("The trade server would not accept your Creature."));
                return;
            }

            Self->HandleSessionResponse(Response->GetContentAsString());
        });

    Request->ProcessRequest();
}

void UGF_TradeSubsystem::SendPoll()
{
    bRequestInFlight = true;

    const FString Url = BuildSessionUrl(LinkCode, FString::Printf(TEXT("?peerId=%s"), *PeerId));
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(TEXT("GET"), Url);

    TWeakObjectPtr<UGF_TradeSubsystem> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
        {
            UGF_TradeSubsystem* Self = WeakThis.Get();
            if (Self == nullptr)
            {
                return;
            }

            Self->bRequestInFlight = false;

            // A single failed poll is not fatal -- the next tick retries, and the
            // wait deadline is what eventually gives up. Only a hard 404 means
            // the session is genuinely gone.
            if (!bConnectedOk || !Response.IsValid())
            {
                return;
            }

            if (Response->GetResponseCode() == 404)
            {
                Self->FailTrade(TEXT("The trade session expired."));
                return;
            }

            if (Response->GetResponseCode() < 200 || Response->GetResponseCode() > 299)
            {
                return;
            }

            Self->HandleSessionResponse(Response->GetContentAsString());
        });

    Request->ProcessRequest();
}

void UGF_TradeSubsystem::SendConfirm()
{
    bRequestInFlight = true;

    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        MakeRequest(TEXT("POST"), BuildSessionUrl(LinkCode, TEXT("/confirm")));

    Request->SetContentAsString(MakeJsonBody({ { TEXT("peerId"), PeerId } }));

    TWeakObjectPtr<UGF_TradeSubsystem> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
        {
            UGF_TradeSubsystem* Self = WeakThis.Get();
            if (Self == nullptr)
            {
                return;
            }

            Self->bRequestInFlight = false;

            if (!bConnectedOk || !Response.IsValid())
            {
                Self->FailTrade(TEXT("Lost contact with the trade server while confirming."));
                return;
            }

            if (Response->GetResponseCode() < 200 || Response->GetResponseCode() > 299)
            {
                Self->FailTrade(TEXT("The trade server rejected the confirmation."));
                return;
            }

            Self->HandleSessionResponse(Response->GetContentAsString());
        });

    Request->ProcessRequest();
}

void UGF_TradeSubsystem::HandleSessionResponse(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        FailTrade(TEXT("The trade server sent a response we could not read."));
        return;
    }

    const FString ServerState = Json->GetStringField(TEXT("state"));

    if (ServerState == StateCancelled)
    {
        StopPolling();

        // The relay is authoritative, and it says this trade is definitively
        // over. Nothing is owed to anyone, so any pending record is safe to drop
        // -- this is one of only two places that is true.
        ClearPendingCommit();

        LastError = TEXT("The other player cancelled the trade.");

        // Logged because this path is otherwise completely silent: it clears
        // state and returns, so a log dump shows the trade starting and then
        // nothing, which reads like a hang rather than a clean cancel.
        UE_LOG(LogTrade, Log, TEXT("Partner cancelled the trade on code %s."), *LinkCode);

        SetState(EGF_TradeState::Cancelled);
        return;
    }

    // Learned as soon as both players are in, well before either picks.
    FString IncomingTamerName;
    if (Json->TryGetStringField(TEXT("partnerTamerName"), IncomingTamerName)
        && PartnerTamerName.IsEmpty()
        && !IncomingTamerName.IsEmpty())
    {
        // Chosen on another player's machine and drawn through the dialogue
        // parser, so it gets the same treatment as a traded Creature's nickname.
        // A name that fails the check is replaced rather than rejected -- a
        // rude name is not worth killing an otherwise valid trade over.
        FString Rejected;
        PartnerTamerName = UGF_CreatureCodec::IsSafeTradeName(FName(*IncomingTamerName), TEXT("Tamer name"), Rejected)
            ? IncomingTamerName
            : TEXT("???");

        if (PartnerTamerName == TEXT("???"))
        {
            UE_LOG(LogTrade, Warning, TEXT("Partner tamer name rejected (%s); showing a placeholder."), *Rejected);
        }
    }

    // The partner's Creature arrives as soon as they choose one, so it can be
    // shown before either player commits to anything.
    FString PartnerPayload;
    if (Json->TryGetStringField(TEXT("partnerPayload"), PartnerPayload) && !PartnerPayload.IsEmpty())
    {
        AdoptPartnerOffer(PartnerPayload);
    }

    if (ServerState == StateCommitted)
    {
        // Both sides accepted. This is the only signal that authorises the swap.
        if (State != EGF_TradeState::Committing && State != EGF_TradeState::Completed)
        {
            StopPolling();
            CommitTrade();
        }
        return;
    }

    if (ServerState == StatePaired)
    {
        // Both Creature are on the table. Stay put if we already accepted.
        if (State != EGF_TradeState::AwaitingPartnerConfirm)
        {
            SetState(EGF_TradeState::OfferReceived);
        }
        return;
    }

    if (ServerState == StateConnected)
    {
        // Partner is present. Whether we are waiting on them or they on us
        // depends only on which of us has chosen.
        SetState(HasOfferedCreature() ? EGF_TradeState::OfferSent : EGF_TradeState::Connected);
        return;
    }

    if (ServerState == StateWaiting && State == EGF_TradeState::Joining)
    {
        SetState(EGF_TradeState::WaitingForPartner);
    }
}

void UGF_TradeSubsystem::AdoptPartnerOffer(const FString& Payload)
{
    if (bPartnerOfferHandled)
    {
        return;
    }

    FGF_CreatureInstanceData Offer;
    FString Error;
    const EGF_TradeCodecResult Result = UGF_CreatureCodec::DecodeCreature(Payload, Offer, Error);

    if (Result != EGF_TradeCodecResult::Success)
    {
        // The relay forwards blobs without inspecting them, so this is the first
        // and only point anything checks what the other player actually sent.
        StopPolling();
        FailTrade(FString::Printf(TEXT("The other player's Creature was rejected: %s"), *Error));
        return;
    }

    bPartnerOfferHandled = true;
    IncomingPayload = Payload;
    PartnerOffer = Offer;

    OnPartnerOfferReceived.Broadcast(PartnerOffer);
}

//--------------------
// COMMIT
//--------------------

bool UGF_TradeSubsystem::ReadCreatureFromSource(FGF_CreatureInstanceData& OutCreature,
    bool bFromVault, int32 VaultPageIndex, int32 SlotIndex) const
{
    const UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr)
    {
        return false;
    }

    return bFromVault
        ? Manager->VaultSystem->GetVaultCreature(VaultPageIndex, SlotIndex, OutCreature)
        : Manager->GetPartyCreatureData(SlotIndex, OutCreature);
}

bool UGF_TradeSubsystem::WriteCreatureToSource(const FGF_CreatureInstanceData& Creature,
    bool bFromVault, int32 VaultPageIndex, int32 SlotIndex)
{
    UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr)
    {
        return false;
    }

    // The received Creature goes back exactly where the offered one came from.
    // Defaulting to the party would overwrite an unrelated Creature whenever the
    // trade was started from a box.
    if (bFromVault)
    {
        // UpdateVaultCreature is a plain slot write, so it needs no special care.
        return Manager->VaultSystem->UpdateVaultCreature(VaultPageIndex, SlotIndex, Creature);
    }

    // Party writes go DIRECTLY to the array, deliberately NOT through
    // UpdatePartyCreature/UpdatePartyCreatureData.
    //
    // That entry point guards against a caller passing the wrong index by
    // comparing CreatureID, and on a mismatch it silently REDIRECTS the write to
    // whichever slot already holds that ID -- or rejects it outright. That guard
    // is right for read-modify-write callers (heal, teach move, rename), but it
    // is exactly wrong for a trade, where the incoming Creature is SUPPOSED to be
    // a different one replacing the slot. Going through it means the traded-away
    // Creature is never removed and the received one lands somewhere else, while
    // the call still returns true.
    //
    // UGF_VaultSystem::UpdatePartyCreature's own comment says as much: "Trades,
    // deposits and swaps write Party[] directly and never reach this."
    if (!Manager->VaultSystem->Party.IsValidIndex(SlotIndex))
    {
        UE_LOG(LogTrade, Error, TEXT("Party slot %d is out of range."), SlotIndex);
        return false;
    }

    // Copy ASSIGNMENT, which is the compiler-generated memberwise one and copies
    // every field. Constructing a copy would run the hand-written copy
    // constructor, which omits fields.
    Manager->VaultSystem->Party[SlotIndex] = Creature;
    Manager->MarkDirty();
    return true;
}

void UGF_TradeSubsystem::CommitTrade()
{
    SetState(EGF_TradeState::Committing);

    UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr)
    {
        FailTrade(TEXT("Creature data became unavailable mid-trade."));
        return;
    }

    FGF_CreatureInstanceData Incoming;
    FString Error;
    if (UGF_CreatureCodec::DecodeCreature(IncomingPayload, Incoming, Error) != EGF_TradeCodecResult::Success)
    {
        FailTrade(FString::Printf(TEXT("The received Creature was rejected: %s"), *Error));
        return;
    }

    // The receiver becomes the current tamer. OriginalTamer is deliberately
    // untouched -- a traded Creature remembering where it came from is the whole
    // point of the field, and is what makes it an outsider.
    Incoming.CurrentTamerName = FName(*Manager->VaultSystem->PlayerName);
    Incoming.CurrentTamerID = Manager->VaultSystem->PlayerID;

    // It arrived over the wire, so it is definitely not walking behind anyone.
    Incoming.bIsFollowerOut = false;

    // Register it in the Compendium. A traded Creature counts as owned in the real
    // games, and without this a species you can ONLY get by trading would never
    // appear in the dex at all. MarkSpeciesAsCaught adds to SeenCreature too, so
    // one call covers both halves.
    //
    // LoadSynchronous is acceptable here: the species asset is about to be
    // resolved anyway to display the Creature, and this runs once per trade at a
    // point where the player is already watching an animation.
    if (UGF_CreatureSpeciesData* IncomingSpecies = Incoming.SpeciesData.LoadSynchronous())
    {
        Manager->VaultSystem->MarkSpeciesAsCaughtFromData(IncomingSpecies);
    }
    else
    {
        UE_LOG(LogTrade, Warning,
            TEXT("Could not resolve the received Creature's species; Compendium not updated."));
    }

    // Skill the record to Committing before touching anything. A crash from here
    // on is recoverable from local data alone -- no need to ask the relay again.
    WritePendingRecord(EGF_PendingTradePhase::Committing);

    if (!WriteCreatureToSource(Incoming, bOutgoingFromVault, OutgoingVaultPageIndex, OutgoingSlotIndex))
    {
        FailTrade(TEXT("Could not write the received Creature into your storage."));
        return;
    }

    // Flush to disk before clearing the recovery record, not after. The record
    // is the only thing that can rebuild this swap, so it must outlive the write
    // it protects.
    if (!Manager->VaultSystem->SaveToDisk())
    {
        UE_LOG(LogTrade, Error,
            TEXT("Storage was updated but could not be saved. Keeping the recovery record so the next launch can finish."));
        SetState(EGF_TradeState::Completed);
        OnTradeCompleted.Broadcast(Incoming);
        return;
    }

    ClearPendingCommit();

    // Says WHERE it landed, not just that it landed. A trade started from a box
    // that silently writes into the party would look identical in the log
    // otherwise, while having overwritten a completely different Creature.
    UE_LOG(LogTrade, Log, TEXT("Trade complete on code %s: received %s into %s."),
        *LinkCode,
        *Incoming.GetDisplayName().ToString(),
        bOutgoingFromVault
            ? *FString::Printf(TEXT("box %d slot %d"), OutgoingVaultPageIndex, OutgoingSlotIndex)
            : *FString::Printf(TEXT("party slot %d"), OutgoingSlotIndex));

    // Trade evolutions are checked but not played here -- the evolution UI is
    // the caller's business, same as it is for NPC trades.
    //
    // As of 2026-08-04 no species carries a Trade evolution any more, so this
    // never fires. The EGF_EvolutionTrigger::Trade value still exists, so the check
    // is left in place and will start working on its own if trade evolutions are
    // ever put back. It is not dead code by mistake.
    if (Manager->CheckForEvolution(Incoming, EGF_EvolutionTrigger::Trade))
    {
        UE_LOG(LogTrade, Log, TEXT("%s can evolve from being traded."),
            *Incoming.GetDisplayName().ToString());
    }

    SetState(EGF_TradeState::Completed);
    OnTradeCompleted.Broadcast(Incoming);
}

void UGF_TradeSubsystem::WritePendingRecord(EGF_PendingTradePhase Phase)
{
    UGF_TradeSave* Save = LoadOrCreateTradeSave();
    if (Save == nullptr)
    {
        UE_LOG(LogTrade, Error, TEXT("Could not write the trade recovery record."));
        return;
    }

    Save->Phase = Phase;
    Save->LinkCode = LinkCode;
    Save->PeerId = PeerId;
    Save->bFromVault = bOutgoingFromVault;
    Save->VaultPageIndex = OutgoingVaultPageIndex;
    Save->PartyIndex = OutgoingSlotIndex;
    Save->OutgoingUniqueID = OutgoingUniqueID;
    Save->IncomingPayload = IncomingPayload;

    UGameplayStatics::SaveGameToSlot(Save, UGF_TradeSave::SlotName, UGF_TradeSave::UserIndex);
}

bool UGF_TradeSubsystem::ApplyRecordedSwap(const UGF_TradeSave& Save)
{
    UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr)
    {
        return false;
    }

    FGF_CreatureInstanceData InSlot;
    const bool bSlotReadable = ReadCreatureFromSource(
        InSlot, Save.bFromVault, Save.VaultPageIndex, Save.PartyIndex);

    // Idempotence hinges on UniqueID: if the Creature we were giving away is
    // still sitting in the slot, the swap never landed and we redo it. If it is
    // gone, the swap already happened and the record is merely stale.
    if (!bSlotReadable || InSlot.UniqueID != Save.OutgoingUniqueID)
    {
        UE_LOG(LogTrade, Log, TEXT("Recorded trade was already applied. Clearing the record."));
        return true;
    }

    FGF_CreatureInstanceData Incoming;
    FString Error;
    if (UGF_CreatureCodec::DecodeCreature(Save.IncomingPayload, Incoming, Error) != EGF_TradeCodecResult::Success)
    {
        // Nothing was taken from the player, so keeping their Creature and
        // dropping the record beats leaving it stuck forever.
        UE_LOG(LogTrade, Error,
            TEXT("Interrupted trade could not be recovered (%s). Your Creature was kept."), *Error);
        return true;
    }

    Incoming.CurrentTamerName = FName(*Manager->VaultSystem->PlayerName);
    Incoming.CurrentTamerID = Manager->VaultSystem->PlayerID;
    Incoming.bIsFollowerOut = false;

    // Same Compendium registration as a normal commit -- a trade recovered after a
    // crash must not leave the player owning a Creature the dex has never heard of.
    if (UGF_CreatureSpeciesData* IncomingSpecies = Incoming.SpeciesData.LoadSynchronous())
    {
        Manager->VaultSystem->MarkSpeciesAsCaughtFromData(IncomingSpecies);
    }

    if (!WriteCreatureToSource(Incoming, Save.bFromVault, Save.VaultPageIndex, Save.PartyIndex))
    {
        return false;
    }

    if (!Manager->VaultSystem->SaveToDisk())
    {
        // Do not let the caller clear the record: the swap is only in memory,
        // and losing the record here would lose the Creature for good.
        UE_LOG(LogTrade, Error, TEXT("Recovered trade could not be saved. Keeping the record for the next launch."));
        return false;
    }

    UE_LOG(LogTrade, Log, TEXT("Recovered an interrupted trade: %s is now in %s slot %d."),
        *Incoming.GetDisplayName().ToString(),
        Save.bFromVault ? TEXT("box") : TEXT("party"),
        Save.PartyIndex);

    OnTradeCompleted.Broadcast(Incoming);
    return true;
}

void UGF_TradeSubsystem::ResolveInterruptedTrade(const FString& Code, const FString& RecoveryPeerId)
{
    const FString Url = BuildSessionUrl(Code, FString::Printf(TEXT("?peerId=%s"), *RecoveryPeerId));
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(TEXT("GET"), Url);

    TWeakObjectPtr<UGF_TradeSubsystem> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
        {
            UGF_TradeSubsystem* Self = WeakThis.Get();
            if (Self == nullptr)
            {
                return;
            }

            if (!bConnectedOk || !Response.IsValid())
            {
                // Offline, or the relay is down. The record survives untouched
                // and the next launch tries again -- guessing here is what would
                // actually lose a Creature.
                UE_LOG(LogTrade, Warning,
                    TEXT("Could not reach the relay to resolve an interrupted trade. Will retry next launch."));
                return;
            }

            UGF_TradeSave* Save = Cast<UGF_TradeSave>(
                UGameplayStatics::LoadGameFromSlot(UGF_TradeSave::SlotName, UGF_TradeSave::UserIndex));

            if (Save == nullptr || Save->Phase == EGF_PendingTradePhase::None)
            {
                return;
            }

            const int32 ResponseCode = Response->GetResponseCode();

            // 404 means the session is gone. Either it expired without ever
            // committing, or it committed and then outlived its hour. Those are
            // indistinguishable from here, so take the branch that cannot hurt
            // this player: keep the Creature they still hold and drop the record.
            if (ResponseCode == 404)
            {
                UE_LOG(LogTrade, Warning,
                    TEXT("An interrupted trade could not be resolved -- the session had expired. Your Creature was kept."));
                Self->ClearPendingCommit();
                return;
            }

            if (ResponseCode < 200 || ResponseCode > 299)
            {
                return;
            }

            TSharedPtr<FJsonObject> Json;
            const TSharedRef<TJsonReader<>> Reader =
                TJsonReaderFactory<>::Create(Response->GetContentAsString());

            if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
            {
                return;
            }

            const FString ServerState = Json->GetStringField(TEXT("state"));

            if (ServerState == StateCommitted)
            {
                // The partner went through with it, so we owe them ours.
                if (Self->ApplyRecordedSwap(*Save))
                {
                    Self->ClearPendingCommit();
                }
                return;
            }

            if (ServerState == StateCancelled)
            {
                UE_LOG(LogTrade, Log, TEXT("Interrupted trade had been cancelled. Nothing to do."));
                Self->ClearPendingCommit();
                return;
            }

            // Still waiting or paired: the partner never confirmed, and the
            // session will expire on its own. Nothing changed hands.
            UE_LOG(LogTrade, Log,
                TEXT("Interrupted trade never completed (relay says '%s'). Your Creature was kept."), *ServerState);
            Self->ClearPendingCommit();
        });

    Request->ProcessRequest();
}

void UGF_TradeSubsystem::RecoverPendingTradeIfAny()
{
    if (!UGameplayStatics::DoesSaveGameExist(UGF_TradeSave::SlotName, UGF_TradeSave::UserIndex))
    {
        return;
    }

    UGF_TradeSave* Save = Cast<UGF_TradeSave>(
        UGameplayStatics::LoadGameFromSlot(UGF_TradeSave::SlotName, UGF_TradeSave::UserIndex));

    if (Save == nullptr || Save->Phase == EGF_PendingTradePhase::None)
    {
        return;
    }

    UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr || Manager->GetPartySize() <= 0)
    {
        // The save has not been read yet. Keep asking rather than waiting to be
        // asked -- see RecoveryRetryHandle for why this must not depend on a
        // caller remembering.
        if (!RecoveryRetryHandle.IsValid())
        {
            // ~2 minutes at 2s intervals, which comfortably covers booting into
            // a save on a slow disk without spinning forever if the player just
            // sits on the title screen.
            RecoveryAttemptsRemaining = 60;
            RecoveryRetryHandle = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateUObject(this, &UGF_TradeSubsystem::RetryRecovery), 2.0f);

            UE_LOG(LogTrade, Log,
                TEXT("Pending trade found, but the party is not loaded yet. Will keep retrying."));
        }
        return;
    }

    StopRecoveryRetries();

    if (Save->Phase == EGF_PendingTradePhase::Committing)
    {
        // Both sides had already confirmed, so local data is enough.
        if (ApplyRecordedSwap(*Save))
        {
            ClearPendingCommit();
        }
        return;
    }

    // AwaitingCommit: we said yes but never learned whether the partner did.
    // Only the relay knows, so ask it rather than assuming either way.
    if (Save->LinkCode.IsEmpty() || Save->PeerId.IsEmpty())
    {
        UE_LOG(LogTrade, Warning, TEXT("Interrupted trade has no session details. Clearing it."));
        ClearPendingCommit();
        return;
    }

    UE_LOG(LogTrade, Log, TEXT("Resolving an interrupted trade on code %s."), *Save->LinkCode);
    ResolveInterruptedTrade(Save->LinkCode, Save->PeerId);
}

bool UGF_TradeSubsystem::RetryRecovery(float DeltaTime)
{
    if (--RecoveryAttemptsRemaining <= 0)
    {
        // Give up ticking, but deliberately leave the record on disk. The next
        // launch tries again; discarding it here would be the one action that
        // actually loses the Creature.
        UE_LOG(LogTrade, Warning,
            TEXT("Gave up waiting for the party to load. The pending trade record is kept for next launch."));
        RecoveryRetryHandle.Reset();
        return false;
    }

    // The party check lives here rather than in RecoverPendingTradeIfAny so this
    // only re-enters that function when it will actually get somewhere.
    const UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr || Manager->GetPartySize() <= 0)
    {
        return true;
    }

    // Clear the handle BEFORE re-entering. Recovery calls StopRecoveryRetries,
    // and removing a ticker from inside its own callback is a trap worth not
    // stepping in; with the handle already cleared that call is a no-op and this
    // tick ends by returning false instead.
    RecoveryRetryHandle.Reset();
    RecoverPendingTradeIfAny();
    return false;
}

void UGF_TradeSubsystem::StopRecoveryRetries()
{
    if (RecoveryRetryHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(RecoveryRetryHandle);
        RecoveryRetryHandle.Reset();
    }
}

UGF_TradeSave* UGF_TradeSubsystem::LoadOrCreateTradeSave() const
{
    if (UGameplayStatics::DoesSaveGameExist(UGF_TradeSave::SlotName, UGF_TradeSave::UserIndex))
    {
        if (UGF_TradeSave* Existing = Cast<UGF_TradeSave>(
                UGameplayStatics::LoadGameFromSlot(UGF_TradeSave::SlotName, UGF_TradeSave::UserIndex)))
        {
            return Existing;
        }
    }

    return Cast<UGF_TradeSave>(UGameplayStatics::CreateSaveGameObject(UGF_TradeSave::StaticClass()));
}

void UGF_TradeSubsystem::ClearPendingCommit()
{
    if (UGF_TradeSave* Save = LoadOrCreateTradeSave())
    {
        Save->Phase = EGF_PendingTradePhase::None;
        Save->LinkCode.Reset();
        Save->PeerId.Reset();
        Save->bFromVault = false;
        Save->VaultPageIndex = INDEX_NONE;
        Save->PartyIndex = INDEX_NONE;
        Save->OutgoingUniqueID.Invalidate();
        Save->IncomingPayload.Reset();
        UGameplayStatics::SaveGameToSlot(Save, UGF_TradeSave::SlotName, UGF_TradeSave::UserIndex);
    }
}

//--------------------
// INTERNALS
//--------------------

void UGF_TradeSubsystem::SetState(EGF_TradeState NewState)
{
    if (State == NewState)
    {
        return;
    }

    State = NewState;
    OnTradeStateChanged.Broadcast(State);
}

void UGF_TradeSubsystem::FailTrade(const FString& Reason)
{
    StopPolling();
    LastError = Reason;

    UE_LOG(LogTrade, Warning, TEXT("Trade failed: %s"), *Reason);

    SetState(EGF_TradeState::Failed);
    OnTradeFailed.Broadcast(Reason);
}

UGF_CreatureManagerSubsystem* UGF_TradeSubsystem::GetCreatureManager() const
{
    const UGameInstance* GameInstance = GetGameInstance();
    return GameInstance ? GameInstance->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
}
