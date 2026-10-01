// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_GiftSubsystem.h"

#include "GF_CreatureCodec.h"
#include "GF_TradeSubsystem.h"
#include "GF_SkillDefinition.h"
#include "Dialogue/GF_DialogueSubsystem.h"
#include "GF_GameVersion.h"
#include "GF_CreatureTraits.h"
#include "GF_VaultSystem.h"
#include "GF_CreatureManagerSubsystem.h"
#include "GF_CreatureSpeciesData.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/ConfigCacheIni.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogGift, Log, All);

const FString UGF_GiftSave::SlotName  = TEXT("GiftSlot");
const int32   UGF_GiftSave::UserIndex = 0;

namespace
{
    // Server-side error strings. These are the contract with the Worker;
    // changing one means changing Tools/TradeRelay/worker.js in the same commit.
    const TCHAR* ErrorGiftNotFound  = TEXT("gift_not_found");
    const TCHAR* ErrorGiftExpired   = TEXT("gift_expired");
    const TCHAR* ErrorGiftInactive  = TEXT("gift_inactive");
    const TCHAR* ErrorAlreadyClaim  = TEXT("already_claimed");
    const TCHAR* ErrorVersionMismat = TEXT("version_mismatch");
    const TCHAR* ErrorClientTooOld  = TEXT("client_too_old");

    /**
     * Turn a machine error into something worth showing a player.
     *
     * Every branch says what to DO, because a gift code is usually typed off a
     * stream or a post and "gift_not_found" tells a player nothing about whether
     * they mistyped it or turned up a week late.
     */
    FString DescribeGiftError(const FString& ServerError)
    {
        if (ServerError == ErrorGiftNotFound)
        {
            return TEXT("No gift is using that code. Check the spelling and try again.");
        }
        if (ServerError == ErrorGiftExpired)
        {
            return TEXT("That gift has ended.");
        }
        if (ServerError == ErrorGiftInactive)
        {
            return TEXT("That gift is not available right now.");
        }
        if (ServerError == ErrorAlreadyClaim)
        {
            return TEXT("You have already received this gift.");
        }
        if (ServerError == ErrorVersionMismat || ServerError == ErrorClientTooOld)
        {
            // Same sentence for both on purpose. They are different faults --
            // one is "your assets moved", the other "your build lacks the code
            // this gift needs" -- but the player's action is identical, and the
            // important half is the reassurance that the claim is still theirs.
            return TEXT("This gift needs a newer version of the game. Update, then claim it -- nothing has been used up.");
        }

        return TEXT("The gift server could not complete that request. Try again in a moment.");
    }
}

#if !UE_BUILD_SHIPPING
namespace
{
    UGF_GiftSubsystem* FindGiftSubsystem(UWorld* World)
    {
        const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
        return GameInstance ? GameInstance->GetSubsystem<UGF_GiftSubsystem>() : nullptr;
    }

    UGF_CreatureManagerSubsystem* FindManager(UWorld* World)
    {
        const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
        return GameInstance ? GameInstance->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
    }

    /**
     * Puts the blob in a file as well as the log.
     *
     * A gift blob is ~1200 characters. Console log lines wrap, get timestamped
     * and are frequently truncated, so recovering one by hand is miserable and
     * error-prone -- and a blob that lost three characters fails at the far end
     * with a corrupt-payload error that looks like a codec bug.
     */
    void WriteBlobToFile(const FString& Label, const FString& Blob)
    {
        const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GiftBlobs"));
        IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);

        const FString Path = FPaths::Combine(Dir, Label + TEXT(".txt"));

        if (FFileHelper::SaveStringToFile(Blob, *Path))
        {
            UE_LOG(LogGift, Display, TEXT("Gift blob (%d chars) written to %s"), Blob.Len(), *Path);
        }
        else
        {
            UE_LOG(LogGift, Error, TEXT("Could not write %s"), *Path);
        }
    }

    bool ParseKeyValue(const FString& Arg, FString& OutKey, FString& OutValue)
    {
        if (!Arg.Split(TEXT("="), &OutKey, &OutValue))
        {
            return false;
        }

        OutKey = OutKey.TrimStartAndEnd().ToLower();
        OutValue = OutValue.TrimStartAndEnd();
        return !OutKey.IsEmpty() && !OutValue.IsEmpty();
    }

    /** Six comma-separated numbers, in HP, Atk, Def, SpA, SpD, Spe order. */
    bool ParseSixStats(const FString& Value, int32* OutStats)
    {
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), /*CullEmpty=*/true);
        if (Parts.Num() != 6)
        {
            return false;
        }

        for (int32 Index = 0; Index < 6; ++Index)
        {
            OutStats[Index] = FCString::Atoi(*Parts[Index].TrimStartAndEnd());
        }
        return true;
    }

    /**
     * Console arguments are split on whitespace, so a value that needs a space
     * is typed with underscores: ball=Cherish_Core, metloc=Tidecliff_City.
     */
    FString Unescape(const FString& Value)
    {
        return Value.Replace(TEXT("_"), TEXT(" "));
    }

    /**
     * Finds a move Blueprint by its bare name ("Thunderbolt") anywhere under the
     * moves folder, so authoring does not need asset paths.
     *
     * Goes through the asset registry rather than a hardcoded path because the
     * moves are filed by type (ELECTRIC/, NORMAL/, ...) and the caller has no
     * reason to know which folder a move lives in. Matching tolerates the
     * trailing _C because a cooked asset registry lists the generated class,
     * while an editor one lists the Blueprint.
     */
    bool ResolveSkillClass(const FString& SkillName, TSoftClassPtr<AGF_SkillDefinition>& OutSkill)
    {
        const FAssetRegistryModule& Module =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

        // Folders and asset naming this project uses for moves. Gamma Framework
        // files them as /Game/BPS/ABILITIES/Skills/BP_Move_Ember; Godsmarch as
        // /Game/Godsmarch/Blueprints/Attacks/Ember/Ember_Move_BP. A plugin that
        // assumes either one resolves nothing in the other project.
        //
        // Config/DefaultGame.ini:
        //   [GammaFramework.Skills]
        //   +SkillDataPaths=/Game/Godsmarch/Blueprints/Attacks
        //   SkillAssetNameFormat={Name}_Move_BP
        TArray<FString> SkillScanPaths;
        FString NameFormat;
        if (GConfig)
        {
            GConfig->GetArray(TEXT("GammaFramework.Skills"), TEXT("SkillDataPaths"),
                              SkillScanPaths, GGameIni);
            GConfig->GetString(TEXT("GammaFramework.Skills"), TEXT("SkillAssetNameFormat"),
                               NameFormat, GGameIni);
        }

        // Legacy defaults, kept so Gamma Framework resolves without an ini entry.
        if (SkillScanPaths.Num() == 0)
        {
            SkillScanPaths.Add(TEXT("/Game/BPS/ABILITIES/Skills"));
        }
        if (NameFormat.IsEmpty())
        {
            NameFormat = TEXT("BP_Move_{Name}");
        }

        TArray<FAssetData> Assets;
        for (const FString& ScanPath : SkillScanPaths)
        {
            Module.Get().GetAssetsByPath(FName(*ScanPath), Assets, /*bRecursive=*/true);
        }

        const FString Wanted = NameFormat.Replace(TEXT("{Name}"), *SkillName);

        for (const FAssetData& Asset : Assets)
        {
            FString AssetName = Asset.AssetName.ToString();
            AssetName.RemoveFromEnd(TEXT("_C"));

            if (!AssetName.Equals(Wanted, ESearchCase::IgnoreCase))
            {
                continue;
            }

            OutSkill = TSoftClassPtr<AGF_SkillDefinition>(FSoftObjectPath(
                FString::Printf(TEXT("%s.%s_C"), *Asset.PackageName.ToString(), *AssetName)));
            return true;
        }

        return false;
    }

    /**
     * Replace the whole moveset, keeping the Uses arrays in lockstep.
     *
     * Lockstep is not cosmetic: UGF_CreatureCodec::ValidateCreature rejects a
     * Creature whose Uses arrays do not match its Skills array, so a half-applied
     * moveset would fail at encode time rather than silently ship.
     */
    bool ApplySkills(FGF_CreatureInstanceData& Creature, const FString& CsvNames)
    {
        TArray<FString> Names;
        CsvNames.ParseIntoArray(Names, TEXT(","), /*CullEmpty=*/true);

        if (Names.Num() == 0 || Names.Num() > UGF_CreatureCodec::MaxSkills)
        {
            UE_LOG(LogGift, Error, TEXT("moves= needs 1 to %d comma-separated move names."),
                UGF_CreatureCodec::MaxSkills);
            return false;
        }

        TArray<TSoftClassPtr<AGF_SkillDefinition>> Resolved;
        TArray<int32> Uses;

        for (const FString& Raw : Names)
        {
            const FString Name = Raw.TrimStartAndEnd();

            TSoftClassPtr<AGF_SkillDefinition> Skill;
            if (!ResolveSkillClass(Name, Skill))
            {
                UE_LOG(LogGift, Error,
                    TEXT("No move asset found for '%s'. Check [GammaFramework.Skills] SkillDataPaths "
                         "and SkillAssetNameFormat in Config/DefaultGame.ini."), *Name);
                return false;
            }

            int32 SkillUses = 10;
            if (TSubclassOf<AGF_SkillDefinition> Loaded = Skill.LoadSynchronous())
            {
                if (const AGF_SkillDefinition* CDO = Loaded->GetDefaultObject<AGF_SkillDefinition>())
                {
                    SkillUses = CDO->MaxUses;
                }
            }

            Resolved.Add(Skill);
            Uses.Add(SkillUses);
        }

        // Built fully before anything is written, so a bad name in the middle of
        // the list leaves the Creature's existing moveset untouched.
        Creature.Skills = Resolved;
        Creature.CurrentUses = Uses;
        Creature.MaxUses = Uses;
        return true;
    }

    /**
     * Encode, then report. Shared by Make and Export because the failure they
     * care about is the same one: EncodeCreature validates before encoding, so a
     * refusal here is the authoring mistake surfacing at the earliest possible
     * point rather than as "their game rejected my gift" a week later.
     */
    void EncodeAndReport(const FGF_CreatureInstanceData& Creature, const FString& Label)
    {
        FString Blob;
        FString Error;
        if (!UGF_CreatureCodec::EncodeCreature(Creature, Blob, Error))
        {
            UE_LOG(LogGift, Error, TEXT("This Creature cannot be gifted: %s"), *Error);
            return;
        }

        UE_LOG(LogGift, Display, TEXT("=== GIFT BLOB for %s (contentVersion %s) ==="),
            *Creature.GetDisplayName().ToString(), *UGF_CreatureCodec::GetTradeContentVersion());
        UE_LOG(LogGift, Display, TEXT("%s"), *Blob);

        WriteBlobToFile(Label, Blob);
    }
}

// Authoring and claim commands. Diagnostics only, but note that Ship_Patch.bat
// packages with -clientconfig=Development, so these survive into tester builds
// on purpose: authoring a gift needs the real content loaded, and that is far
// easier in a packaged build than in the editor.
static FAutoConsoleCommandWithWorldAndArgs GGiftClaimCommand(
    TEXT("GE.Gift.Claim"),
    TEXT("GE.Gift.Claim <giftCode> - collect a Mystery Gift."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World)
        {
            UGF_GiftSubsystem* Gift = FindGiftSubsystem(World);
            if (Gift == nullptr)
            {
                UE_LOG(LogGift, Error, TEXT("Gift subsystem unavailable."));
                return;
            }

            if (Args.Num() < 1)
            {
                UE_LOG(LogGift, Error, TEXT("Usage: GE.Gift.Claim <giftCode>"));
                return;
            }

            if (Gift->ClaimGift(Args[0]))
            {
                UE_LOG(LogGift, Display, TEXT("Collecting gift %s..."), *Args[0]);
            }
            else
            {
                UE_LOG(LogGift, Error, TEXT("Could not collect: %s"), *Gift->GetLastError());
            }
        }));

static FAutoConsoleCommandWithWorldAndArgs GGiftStatusCommand(
    TEXT("GE.Gift.Status"),
    TEXT("GE.Gift.Status - show the current gift state and this installation's claims."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>&, UWorld* World)
        {
            UGF_GiftSubsystem* Gift = FindGiftSubsystem(World);
            if (Gift == nullptr)
            {
                UE_LOG(LogGift, Error, TEXT("Gift subsystem unavailable."));
                return;
            }

            const UEnum* StateEnum = StaticEnum<EGF_GiftState>();
            UE_LOG(LogGift, Display, TEXT("State: %s"),
                StateEnum ? *StateEnum->GetNameStringByValue(static_cast<int64>(Gift->GetGiftState())) : TEXT("?"));

            if (!Gift->GetLastError().IsEmpty())
            {
                UE_LOG(LogGift, Display, TEXT("Last error: %s"), *Gift->GetLastError());
            }

            if (Gift->GetGiftState() == EGF_GiftState::Received)
            {
                UE_LOG(LogGift, Display, TEXT("Received %s into %s."),
                    *Gift->GetReceivedCreature().GetDisplayName().ToString(),
                    Gift->DidGiftGoToParty() ? TEXT("the party") : *FString::Printf(TEXT("box %d"), Gift->GetGiftVaultPageIndex()));
            }
        }));

static FAutoConsoleCommandWithWorldAndArgs GGiftForgetCommand(
    TEXT("GE.Gift.Forget"),
    TEXT("GE.Gift.Forget <giftCode> - drop the LOCAL claim record so the client path can be retested. The server still refuses."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World)
        {
            UGF_GiftSubsystem* Gift = FindGiftSubsystem(World);
            if (Gift == nullptr || Args.Num() < 1)
            {
                UE_LOG(LogGift, Error, TEXT("Usage: GE.Gift.Forget <giftCode>"));
                return;
            }

            Gift->DebugForgetClaim(Args[0]);
        }));

static FAutoConsoleCommandWithWorldAndArgs GGiftExportCommand(
    TEXT("GE.Gift.Export"),
    TEXT("GE.Gift.Export <partySlot> - encode an existing party Creature as a gift blob. Use this when the Creature needs specific moves you taught it in-game."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World)
        {
            UGF_CreatureManagerSubsystem* Manager = FindManager(World);
            if (Manager == nullptr || Manager->VaultSystem == nullptr)
            {
                UE_LOG(LogGift, Error, TEXT("Creature data is unavailable."));
                return;
            }

            if (Args.Num() < 1)
            {
                UE_LOG(LogGift, Error, TEXT("Usage: GE.Gift.Export <partySlot>"));
                return;
            }

            const int32 Slot = FCString::Atoi(*Args[0]);
            if (!Manager->VaultSystem->Party.IsValidIndex(Slot))
            {
                UE_LOG(LogGift, Error, TEXT("Party slot %d is empty or out of range."), Slot);
                return;
            }

            EncodeAndReport(Manager->VaultSystem->Party[Slot],
                FString::Printf(TEXT("Export_Slot%d"), Slot));
        }));

static FAutoConsoleCommandWithWorldAndArgs GGiftMakeCommand(
    TEXT("GE.Gift.Make"),
    TEXT("GE.Gift.Make <Species> [Level] [ivs=31,31,31,31,31,31] [evs=0,0,0,0,0,0] [nature=Timid] [trait=Lodestone] "
         "[moves=Thunderbolt,Spark] [unique=1] [gender=Female] [ot=NAME] [otid=12345] [nick=NAME] [ball=Cherish_Core] "
         "[metloc=Tidecliff_City] [mettype=Gift] [metlevel=5] [metdate=2004-07-17] [note=Some_flavour_text.] "
         "[bond=255] [noevolve=1] [egg=1] "
         "- build an event Creature and print its gift blob. Underscores in a value become spaces."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World)
        {
            UGF_CreatureManagerSubsystem* Manager = FindManager(World);
            if (Manager == nullptr)
            {
                UE_LOG(LogGift, Error, TEXT("Creature data is unavailable."));
                return;
            }

            if (Args.Num() < 1)
            {
                UE_LOG(LogGift, Error, TEXT("Usage: GE.Gift.Make <Species> [Level] [key=value ...]"));
                return;
            }

            UGF_CreatureSpeciesData* Species = Manager->GetCreatureSpeciesData(FName(*Args[0]));
            if (Species == nullptr)
            {
                UE_LOG(LogGift, Error, TEXT("No species named '%s'. Try gf.Species.Find."), *Args[0]);
                return;
            }

            int32 Level = 50;
            if (Args.Num() >= 2 && !Args[1].Contains(TEXT("=")))
            {
                Level = FMath::Clamp(FCString::Atoi(*Args[1]), 1, UGF_CreatureCodec::MaxLevel);
            }

            // Built through CreateCreature so trait, moves and base stats are
            // set up exactly as a normally-obtained Creature would be. Only the
            // fields explicitly named below are then overridden.
            //
            // Copy ASSIGNMENT, not construction: FGF_CreatureInstanceData's
            // hand-written copy constructor drops fields, and CreateCreature
            // returns by value.
            FGF_CreatureInstanceData Creature;
            Creature = Manager->CreateCreature(Species, Level);

            for (int32 Index = 1; Index < Args.Num(); ++Index)
            {
                FString Key;
                FString Value;
                if (!ParseKeyValue(Args[Index], Key, Value))
                {
                    continue;
                }

                if (Key == TEXT("ivs"))
                {
                    int32 Stats[6] = { 0 };
                    if (!ParseSixStats(Value, Stats))
                    {
                        UE_LOG(LogGift, Error, TEXT("ivs needs six comma-separated numbers (HP,Atk,Def,SpA,SpD,Spe)."));
                        return;
                    }

                    Creature.HP_Potential             = FMath::Clamp(Stats[0], 0, UGF_CreatureCodec::MaxPotential);
                    Creature.Attack_Potential         = FMath::Clamp(Stats[1], 0, UGF_CreatureCodec::MaxPotential);
                    Creature.Defense_Potential        = FMath::Clamp(Stats[2], 0, UGF_CreatureCodec::MaxPotential);
                    Creature.Magic_Potential  = FMath::Clamp(Stats[3], 0, UGF_CreatureCodec::MaxPotential);
                    Creature.Poise_Potential = FMath::Clamp(Stats[4], 0, UGF_CreatureCodec::MaxPotential);
                    Creature.Speed_Potential          = FMath::Clamp(Stats[5], 0, UGF_CreatureCodec::MaxPotential);
                }
                else if (Key == TEXT("evs"))
                {
                    int32 Stats[6] = { 0 };
                    if (!ParseSixStats(Value, Stats))
                    {
                        UE_LOG(LogGift, Error, TEXT("evs needs six comma-separated numbers (HP,Atk,Def,SpA,SpD,Spe)."));
                        return;
                    }

                    // Left unclamped against the 510 total on purpose --
                    // EncodeCreature validates and will name the exact problem,
                    // which is more useful than silently reshaping the spread.
                    Creature.HP_Training             = Stats[0];
                    Creature.Attack_Training         = Stats[1];
                    Creature.Defense_Training        = Stats[2];
                    Creature.Magic_Training  = Stats[3];
                    Creature.Poise_Training = Stats[4];
                    Creature.Speed_Training          = Stats[5];
                }
                else if (Key == TEXT("nature"))
                {
                    // GetValueByNameString matches the ENUMERATOR name, not the
                    // UMETA DisplayName -- which is what we want, because
                    // DisplayName is stripped from cooked builds entirely.
                    const int64 Found = StaticEnum<EGF_Temperament>()->GetValueByNameString(Value);
                    if (Found == INDEX_NONE)
                    {
                        UE_LOG(LogGift, Error, TEXT("Unknown nature '%s'."), *Value);
                        return;
                    }
                    Creature.Temperament = static_cast<EGF_Temperament>(Found);
                }
                else if (Key == TEXT("gender"))
                {
                    const int64 Found = StaticEnum<EGF_CreatureGender>()->GetValueByNameString(Value);
                    if (Found == INDEX_NONE)
                    {
                        UE_LOG(LogGift, Error, TEXT("Unknown gender '%s' (Male, Female, Genderless)."), *Value);
                        return;
                    }
                    Creature.Gender = static_cast<EGF_CreatureGender>(Found);
                }
                else if (Key == TEXT("unique"))
                {
                    Creature.bIsUnique = Value.ToBool() || Value == TEXT("1");
                }
                else if (Key == TEXT("egg"))
                {
                    Creature.bIsEgg = Value.ToBool() || Value == TEXT("1");
                }
                else if (Key == TEXT("trait"))
                {
                    const int64 Found = StaticEnum<EGF_CreatureTrait>()->GetValueByNameString(Value);
                    if (Found == INDEX_NONE)
                    {
                        UE_LOG(LogGift, Error, TEXT("Unknown trait '%s'."), *Value);
                        return;
                    }

                    const EGF_CreatureTrait Wanted = static_cast<EGF_CreatureTrait>(Found);

                    // Trait and TraitSlot MUST agree.
                    //
                    // UGF_VaultSystem re-syncs Trait *from* TraitSlot on
                    // every load, so setting the enum alone is silently reverted
                    // the first time the receiving player's save is read back --
                    // and the gift would quietly have the wrong trait for
                    // everyone, with nothing in the logs to say so.
                    int32 Slot = INDEX_NONE;
                    for (int32 Candidate = 0; Candidate <= 1; ++Candidate)
                    {
                        if (UGF_CreatureTraitLibrary::GetSpeciesTraitBySlot(Species, Candidate) == Wanted)
                        {
                            Slot = Candidate;
                            break;
                        }
                    }

                    if (Slot == INDEX_NONE)
                    {
                        const UEnum* TraitEnum = StaticEnum<EGF_CreatureTrait>();
                        UE_LOG(LogGift, Error,
                            TEXT("%s cannot have '%s'. Its traits are '%s' (slot 0) and '%s' (slot 1)."),
                            *Args[0], *Value,
                            *TraitEnum->GetNameStringByValue(static_cast<int64>(Species->Trait1)),
                            *TraitEnum->GetNameStringByValue(static_cast<int64>(Species->Trait2)));
                        return;
                    }

                    Creature.Trait = Wanted;
                    Creature.TraitSlot = Slot;
                }
                else if (Key == TEXT("moves"))
                {
                    if (!ApplySkills(Creature, Value))
                    {
                        return;
                    }
                }
                else if (Key == TEXT("noevolve"))
                {
                    Creature.bCannotEvolve = Value.ToBool() || Value == TEXT("1");
                }
                else if (Key == TEXT("ot"))
                {
                    Creature.OriginalTamerName = FName(*Unescape(Value));
                }
                else if (Key == TEXT("otid"))
                {
                    Creature.OriginalTamerID = FCString::Atoi(*Value);
                }
                else if (Key == TEXT("nick"))
                {
                    Creature.Nickname = FName(*Unescape(Value));
                }
                else if (Key == TEXT("ball"))
                {
                    Creature.CaughtCoreName = FName(*Unescape(Value));
                }
                else if (Key == TEXT("metloc"))
                {
                    // The override, not MetRoutePath: this Creature was never
                    // standing anywhere the player was. The field's own comment
                    // names event gifts as exactly this case.
                    Creature.MetLocationOverride = Unescape(Value);
                }
                else if (Key == TEXT("mettype"))
                {
                    const int64 Found = StaticEnum<EGF_CreatureMetType>()->GetValueByNameString(Value);
                    if (Found == INDEX_NONE)
                    {
                        UE_LOG(LogGift, Error,
                            TEXT("Unknown mettype '%s' (Unknown, Caught, Gift, Hatched, EggReceived)."), *Value);
                        return;
                    }
                    Creature.MetType = static_cast<EGF_CreatureMetType>(Found);
                }
                else if (Key == TEXT("metlevel"))
                {
                    Creature.MetLevel = FMath::Clamp(FCString::Atoi(*Value), 0, UGF_CreatureCodec::MaxLevel);
                }
                else if (Key == TEXT("metdate"))
                {
                    // YYYY-MM-DD. Setting this deliberately opts OUT of the
                    // claim-time stamp: ReceiveGiftPayload only fills MetDate in
                    // when it is the zero sentinel, so an event that wants a
                    // fixed date in every copy just says so here.
                    TArray<FString> Parts;
                    Value.ParseIntoArray(Parts, TEXT("-"), /*CullEmpty=*/true);

                    int32 Year = 0, Month = 0, Day = 0;
                    if (Parts.Num() == 3)
                    {
                        Year  = FCString::Atoi(*Parts[0]);
                        Month = FCString::Atoi(*Parts[1]);
                        Day   = FCString::Atoi(*Parts[2]);
                    }

                    if (!FDateTime::Validate(Year, Month, Day, 0, 0, 0, 0))
                    {
                        UE_LOG(LogGift, Error,
                            TEXT("metdate must be a real date as YYYY-MM-DD, e.g. metdate=2004-07-17 (got '%s')."),
                            *Value);
                        return;
                    }

                    Creature.MetDate = FDateTime(Year, Month, Day);
                }
                else if (Key == TEXT("note"))
                {
                    // Bounded like any other player-visible string, because this
                    // one arrives on thousands of machines and lands in the same
                    // text boxes a nickname does.
                    const FString Note = Unescape(Value);
                    if (Note.Len() > UGF_CreatureCodec::MaxLocationChars)
                    {
                        UE_LOG(LogGift, Error, TEXT("note is %d characters; the limit is %d."),
                            Note.Len(), UGF_CreatureCodec::MaxLocationChars);
                        return;
                    }
                    Creature.MemoNote = Note;
                }
                else if (Key == TEXT("bond"))
                {
                    Creature.Bond = FMath::Clamp(FCString::Atoi(*Value), 0, UGF_CreatureCodec::MaxBond);
                }
                else
                {
                    UE_LOG(LogGift, Warning, TEXT("Ignoring unknown key '%s'."), *Key);
                }
            }

            // Potentials, nature and level all feed the stat formula, so anything set
            // above has invalidated whatever CreateCreature computed.
            Manager->RecalculateStats(Creature);
            Creature.CurrentHP = Creature.MaxHP;

            EncodeAndReport(Creature, FString::Printf(TEXT("Make_%s_Lv%d"), *Args[0], Level));
        }));
#endif // !UE_BUILD_SHIPPING

//--------------------
// LIFECYCLE
//--------------------

void UGF_GiftSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // The Creature manager is fetched lazily rather than declared here. A
    // subsystem fetched inside Initialize needs InitializeDependency or it comes
    // back silently null, and nothing in this class needs it until a claim runs.
    State = EGF_GiftState::Idle;
}

void UGF_GiftSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

void UGF_GiftSubsystem::ResetToBootState()
{
    // In-flight claim only. ClaimedGiftCodes lives on disk and is deliberately
    // untouched -- see the class comment.
    State = EGF_GiftState::Idle;
    ActiveGiftCode.Reset();
    LastError.Reset();
    LastGiftName.Reset();
    ReceivedCreature = FGF_CreatureInstanceData();
    bWentToParty = false;
    ReceivedVaultPageIndex = INDEX_NONE;
    ReceivedSlotIndex = INDEX_NONE;
    bRequestInFlight = false;
}

//--------------------
// CODE CLASSIFICATION
//--------------------

EGF_CodeKind UGF_GiftSubsystem::ClassifyCode(const FString& Code)
{
    const FString Trimmed = Code.TrimStartAndEnd();
    if (Trimmed.Len() < 4 || Trimmed.Len() > 16)
    {
        return EGF_CodeKind::Invalid;
    }

    bool bHasLetter = false;
    for (const TCHAR Char : Trimmed)
    {
        const bool bIsDigit = (Char >= TEXT('0') && Char <= TEXT('9'));
        const bool bIsLetter = (Char >= TEXT('A') && Char <= TEXT('Z'))
                            || (Char >= TEXT('a') && Char <= TEXT('z'));

        if (!bIsDigit && !bIsLetter)
        {
            return EGF_CodeKind::Invalid;
        }

        bHasLetter |= bIsLetter;
    }

    // GenerateLinkCode only ever produces digits, so a letter anywhere is an
    // unambiguous signal that this is a gift code. That keeps one text box
    // serving both features without asking the player which kind they hold.
    return bHasLetter ? EGF_CodeKind::GiftCode : EGF_CodeKind::LinkCode;
}

bool UGF_GiftSubsystem::IsGiftCode(const FString& Code)
{
    return ClassifyCode(Code) == EGF_CodeKind::GiftCode;
}

//--------------------
// FLOW
//--------------------

bool UGF_GiftSubsystem::SubmitCode(const FString& Code)
{
    switch (ClassifyCode(Code))
    {
    case EGF_CodeKind::GiftCode:
        return ClaimGift(Code);

    case EGF_CodeKind::LinkCode:
    {
        UGameInstance* GameInstance = GetGameInstance();
        UGF_TradeSubsystem* Trade = GameInstance ? GameInstance->GetSubsystem<UGF_TradeSubsystem>() : nullptr;
        if (Trade == nullptr)
        {
            LastError = TEXT("Trading is unavailable.");
            return false;
        }

        if (Trade->ConnectToPartner(Code))
        {
            return true;
        }

        // Surface the trade subsystem's reason, not a generic one -- it knows
        // things this class does not, like a misconfigured relay URL.
        LastError = Trade->GetLastError();
        return false;
    }

    default:
        LastError = TEXT("That code is not valid. Codes are 4-16 letters or numbers.");
        return false;
    }
}

bool UGF_GiftSubsystem::ClaimGift(const FString& GiftCode)
{
    // The ONE rejection that must not touch the state machine: a claim is
    // genuinely running, and moving it to Failed would abandon a request that is
    // about to deliver a Creature.
    if (IsClaimInProgress())
    {
        LastError = TEXT("A gift is already being collected.");
        return false;
    }

    // Any usable code, NOT IsGiftCode.
    //
    // The "must contain a letter" rule belongs to SubmitCode, which has to guess
    // where an unlabelled code is going. A caller that reached here has already
    // said this is a gift -- a dedicated Mystery Gift button, or a console
    // command -- so imposing the router's heuristic on it would ban all-numeric
    // gift codes for no reason, including on a numeric keypad that cannot type
    // a letter in the first place.
    //
    // Every rejection from here down goes through FailClaim rather than just
    // setting LastError and returning.
    //
    // Without that, a synchronous refusal leaves the state untouched, so
    // GetGiftStatusText() keeps reporting whatever happened last -- a player
    // re-entering a code they already used would see "Enter a gift code." and no
    // explanation at all, which is precisely the case this text exists to cover.
    // Routing everything through FailClaim also means a screen can bind
    // OnGiftFailed once and handle every failure, instead of handling the
    // asynchronous ones through the delegate and the synchronous ones through
    // the return value.
    if (ClassifyCode(GiftCode) == EGF_CodeKind::Invalid)
    {
        FailClaim(TEXT("That gift code is not valid. Codes are 4-16 letters or numbers."));
        return false;
    }

    const FString Normalized = GiftCode.TrimStartAndEnd().ToUpper();

    if (HasClaimedGift(Normalized))
    {
        FailClaim(TEXT("You have already received this gift."));
        return false;
    }

    // Catch a mangled relay URL here rather than as a baffling DNS failure ten
    // seconds later. UE parses ini with SwallowDoubleSlashComments, so an
    // unquoted // starts a comment and "https://host" silently becomes "https:".
    const FString ConfiguredUrl = GetDefault<UGF_TradeSettings>()->RelayBaseUrl;
    if (!ConfiguredUrl.Contains(TEXT("://")))
    {
        const FString Message = FString::Printf(
            TEXT("The gift server address is misconfigured ('%s'). In DefaultGame.ini, RelayBaseUrl must be wrapped in quotes."),
            *ConfiguredUrl);
        UE_LOG(LogGift, Error, TEXT("%s"), *Message);
        FailClaim(Message);
        return false;
    }

    const UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr)
    {
        FailClaim(TEXT("Creature data is unavailable."));
        return false;
    }

    ActiveGiftCode = Normalized;
    LastError.Reset();
    LastGiftName.Reset();
    ReceivedCreature = FGF_CreatureInstanceData();
    bWentToParty = false;
    ReceivedVaultPageIndex = INDEX_NONE;
    ReceivedSlotIndex = INDEX_NONE;

    SetState(EGF_GiftState::Claiming);
    SendClaim(Normalized);
    return true;
}

bool UGF_GiftSubsystem::HasClaimedGift(const FString& GiftCode) const
{
    const UGF_GiftSave* Save = LoadOrCreateGiftSave();
    if (Save == nullptr)
    {
        return false;
    }

    return Save->ClaimedGiftCodes.Contains(GiftCode.TrimStartAndEnd().ToUpper());
}

//--------------------
// HTTP
//--------------------

FString UGF_GiftSubsystem::BuildGiftUrl(const FString& Code, const FString& Query)
{
    const UGF_TradeSettings* Settings = GetDefault<UGF_TradeSettings>();
    FString Base = Settings->RelayBaseUrl;
    while (Base.EndsWith(TEXT("/")))
    {
        Base.LeftChopInline(1);
    }

    return FString::Printf(TEXT("%s/v1/gift/%s%s"), *Base, *Code, *Query);
}

FString UGF_GiftSubsystem::BuildClaimId(const FString& Code) const
{
    UGF_GiftSave* Save = LoadOrCreateGiftSave();
    if (Save == nullptr)
    {
        // Without a stable identity the server cannot enforce anything, so a
        // random one would quietly turn the safeguard off. Better to fail the
        // claim than to succeed with no protection.
        return FString();
    }

    if (!Save->InstallId.IsValid())
    {
        Save->InstallId = FGuid::NewGuid();
        UGameplayStatics::SaveGameToSlot(Save, UGF_GiftSave::SlotName, UGF_GiftSave::UserIndex);
    }

    const FString Material = FString::Printf(TEXT("%s:%s"),
        *Code, *Save->InstallId.ToString(EGuidFormats::DigitsWithHyphens));

    return FMD5::HashAnsiString(*Material);
}

void UGF_GiftSubsystem::SendClaim(const FString& Code)
{
    const FString ClaimId = BuildClaimId(Code);
    if (ClaimId.IsEmpty())
    {
        FailClaim(TEXT("This installation could not be identified, so the gift cannot be collected."));
        return;
    }

    // buildVersion is DIAGNOSTIC ONLY -- nothing gates on it, and nothing should.
    // It is here so `wrangler tail` shows which patch each claim came from during
    // an event, turning "someone says the gift is broken" into something you can
    // actually correlate. Gating stays on clientVersion, which changes only when
    // a capability changes.
    const FString Query = FString::Printf(TEXT("?claimId=%s&contentVersion=%s&clientVersion=%d&buildVersion=%s"),
        *FGenericPlatformHttp::UrlEncode(ClaimId),
        *FGenericPlatformHttp::UrlEncode(UGF_CreatureCodec::GetTradeContentVersion()),
        GiftClientVersion,
        *FGenericPlatformHttp::UrlEncode(UGF_GameVersionLibrary::GetBuildVersion()));

    const UGF_TradeSettings* Settings = GetDefault<UGF_TradeSettings>();

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildGiftUrl(Code, Query));
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetTimeout(Settings->RequestTimeoutSeconds);

    bRequestInFlight = true;

    TWeakObjectPtr<UGF_GiftSubsystem> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            UGF_GiftSubsystem* Self = WeakThis.Get();
            if (Self == nullptr)
            {
                return;
            }

            Self->bRequestInFlight = false;

            if (!bSucceeded || !Response.IsValid())
            {
                Self->FailClaim(TEXT("Could not reach the gift server. Check your internet connection and try again."));
                return;
            }

            Self->HandleClaimResponse(Response->GetResponseCode(), Response->GetContentAsString());
        });

    Request->ProcessRequest();
}

void UGF_GiftSubsystem::HandleClaimResponse(int32 ResponseCode, const FString& Body)
{
    if (State != EGF_GiftState::Claiming)
    {
        // Cancelled or reset while the request was in flight.
        return;
    }

    TSharedPtr<FJsonObject> Json;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        FailClaim(TEXT("The gift server sent something unreadable. Try again in a moment."));
        return;
    }

    if (ResponseCode < 200 || ResponseCode >= 300)
    {
        FString ServerError;
        Json->TryGetStringField(TEXT("error"), ServerError);

        UE_LOG(LogGift, Warning, TEXT("Gift %s refused (HTTP %d, %s)."),
            *ActiveGiftCode, ResponseCode, ServerError.IsEmpty() ? TEXT("no error field") : *ServerError);

        // The server's own record says this installation already has it, but the
        // local record clearly did not -- a deleted GiftSlot.sav, or the same
        // gift claimed on another install of the same save. Write the local
        // record so the UI stops offering it.
        if (ServerError == ErrorAlreadyClaim)
        {
            RecordClaim(ActiveGiftCode);
        }

        FailClaim(DescribeGiftError(ServerError));
        return;
    }

    FString Payload;
    if (!Json->TryGetStringField(TEXT("payload"), Payload) || Payload.IsEmpty())
    {
        FailClaim(TEXT("The gift server sent an empty gift. Try again in a moment."));
        return;
    }

    Json->TryGetStringField(TEXT("giftName"), LastGiftName);

    // The name comes from outside this machine and lands in dialogue, which
    // reads {Token} and [flow] markup -- so it gets the same treatment a
    // partner's tamer name gets rather than being trusted.
    FString NameError;
    if (!LastGiftName.IsEmpty() && !UGF_CreatureCodec::IsSafeTradeName(FName(*LastGiftName), TEXT("Gift name"), NameError))
    {
        UE_LOG(LogGift, Warning, TEXT("Gift name rejected (%s); showing it unnamed."), *NameError);
        LastGiftName.Reset();
    }

    FString ReceiveError;
    if (!ReceiveGiftPayload(Payload, ReceiveError))
    {
        FailClaim(ReceiveError);
        return;
    }

    SetState(EGF_GiftState::Received);
    OnGiftReceived.Broadcast(ReceivedCreature);
}

bool UGF_GiftSubsystem::ReceiveGiftPayload(const FString& Payload, FString& OutError)
{
    UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
    if (Manager == nullptr || Manager->VaultSystem == nullptr)
    {
        OutError = TEXT("Creature data became unavailable.");
        return false;
    }

    // Same gate as a traded Creature, for the same reason: the relay is a mailbox
    // that never inspects payloads, so this is the only thing between a crafted
    // blob and the player's save. DecodeCreature also assigns a fresh UniqueID,
    // which is what stops every recipient of a mass gift sharing one identity.
    FGF_CreatureInstanceData Incoming;
    FString CodecError;
    const EGF_TradeCodecResult Result = UGF_CreatureCodec::DecodeCreature(Payload, Incoming, CodecError);
    if (Result != EGF_TradeCodecResult::Success)
    {
        UE_LOG(LogGift, Error, TEXT("Gift %s failed to decode: %s"), *ActiveGiftCode, *CodecError);

        OutError = (Result == EGF_TradeCodecResult::ContentMismatch)
            ? TEXT("This gift was made for a different version of the game.")
            : TEXT("This gift could not be read and was not accepted.");
        return false;
    }

    // The player becomes the current tamer, but OriginalTamer is left as the
    // event authored it -- that is what makes it recognisably an event Creature
    // and an outsider, exactly as a traded one is.
    Incoming.CurrentTamerName = FName(*Manager->VaultSystem->PlayerName);
    Incoming.CurrentTamerID = Manager->VaultSystem->PlayerID;
    Incoming.bIsFollowerOut = false;

    // Stamp the day THIS player received it, not the day the gift was authored.
    // Only when unset, so an event that deliberately bakes in a fixed date keeps
    // it -- a zero FDateTime is the struct's documented "never stamped" sentinel.
    if (Incoming.MetDate == FDateTime(0))
    {
        Incoming.MetDate = FDateTime::Now();
    }

    // Eggs deliberately do not register in the Compendium here; that happens at
    // hatch, which is also what GiveCreatureInstanceTracked assumes.
    if (!Incoming.bIsEgg)
    {
        if (UGF_CreatureSpeciesData* Species = Incoming.SpeciesData.LoadSynchronous())
        {
            Manager->VaultSystem->MarkSpeciesAsCaughtFromData(Species);
        }
        else
        {
            UE_LOG(LogGift, Warning, TEXT("Could not resolve the gift's species; Compendium not updated."));
        }
    }

    if (!Manager->GiveCreatureInstanceTracked(Incoming, bWentToParty, ReceivedVaultPageIndex, ReceivedSlotIndex))
    {
        // Nothing was placed, so the claim must NOT be recorded -- the server's
        // grace window is what lets them come back after making room.
        OutError = TEXT("Your party and all your boxes are full. Make room and try again.");
        return false;
    }

    // Flush before recording the claim, never after. If the write fails the gift
    // has to stay claimable, and the ordering is the only thing guaranteeing it.
    if (!Manager->VaultSystem->SaveToDisk())
    {
        UE_LOG(LogGift, Error,
            TEXT("Gift %s was placed but could not be saved; leaving the claim unrecorded so it can be collected again."),
            *ActiveGiftCode);

        OutError = TEXT("The gift could not be saved. Try again in a moment.");
        return false;
    }

    RecordClaim(ActiveGiftCode);

    // Copy ASSIGNMENT. Constructing a copy would run FGF_CreatureInstanceData's
    // hand-written copy constructor, which omits fields.
    ReceivedCreature = Incoming;

    UE_LOG(LogGift, Log, TEXT("Gift %s received: %s into %s."),
        *ActiveGiftCode,
        *ReceivedCreature.GetDisplayName().ToString(),
        bWentToParty
            ? *FString::Printf(TEXT("party slot %d"), ReceivedSlotIndex)
            : *FString::Printf(TEXT("box %d slot %d"), ReceivedVaultPageIndex, ReceivedSlotIndex));

    return true;
}

//--------------------
// PRESENTATION
//--------------------

void UGF_GiftSubsystem::PublishGiftDialogueTokens() const
{
    UGameInstance* GameInstance = GetGameInstance();
    UGF_DialogueSubsystem* Dialogue = GameInstance ? GameInstance->GetSubsystem<UGF_DialogueSubsystem>() : nullptr;
    if (Dialogue == nullptr)
    {
        return;
    }

    Dialogue->SetDialogueVariable(TEXT("GiftName"), LastGiftName);
    Dialogue->SetDialogueVariable(TEXT("GiftCreature"), ReceivedCreature.GetDisplayName().ToString());

    FString Destination = TEXT("your party");
    if (!bWentToParty)
    {
        const UGF_CreatureManagerSubsystem* Manager = GetCreatureManager();
        const UGF_VaultSystem* VaultPages = Manager ? Manager->VaultSystem : nullptr;

        Destination = (VaultPages && VaultPages->VaultPageNames.IsValidIndex(ReceivedVaultPageIndex))
            ? VaultPages->VaultPageNames[ReceivedVaultPageIndex]
            : FString::Printf(TEXT("Vault %d"), ReceivedVaultPageIndex + 1);
    }

    Dialogue->SetDialogueVariable(TEXT("GiftDestination"), Destination);
}

//--------------------
// SAVE
//--------------------

UGF_GiftSave* UGF_GiftSubsystem::LoadOrCreateGiftSave() const
{
    if (UGameplayStatics::DoesSaveGameExist(UGF_GiftSave::SlotName, UGF_GiftSave::UserIndex))
    {
        if (UGF_GiftSave* Existing = Cast<UGF_GiftSave>(
                UGameplayStatics::LoadGameFromSlot(UGF_GiftSave::SlotName, UGF_GiftSave::UserIndex)))
        {
            return Existing;
        }

        UE_LOG(LogGift, Warning, TEXT("GiftSlot exists but could not be loaded; starting a fresh record."));
    }

    return Cast<UGF_GiftSave>(UGameplayStatics::CreateSaveGameObject(UGF_GiftSave::StaticClass()));
}

void UGF_GiftSubsystem::RecordClaim(const FString& Code)
{
    UGF_GiftSave* Save = LoadOrCreateGiftSave();
    if (Save == nullptr)
    {
        UE_LOG(LogGift, Error, TEXT("Could not record the claim for %s."), *Code);
        return;
    }

    // Preserve the identity across the rewrite. A fresh save object here would
    // mint a new InstallId and hand this installation a second entitlement to
    // every gift it has already had.
    if (!Save->InstallId.IsValid())
    {
        Save->InstallId = FGuid::NewGuid();
    }

    Save->ClaimedGiftCodes.AddUnique(Code.ToUpper());

    if (!UGameplayStatics::SaveGameToSlot(Save, UGF_GiftSave::SlotName, UGF_GiftSave::UserIndex))
    {
        UE_LOG(LogGift, Error, TEXT("Could not write GiftSlot after claiming %s."), *Code);
    }
}

void UGF_GiftSubsystem::DebugForgetClaim(const FString& GiftCode)
{
    UGF_GiftSave* Save = LoadOrCreateGiftSave();
    if (Save == nullptr)
    {
        return;
    }

    Save->ClaimedGiftCodes.Remove(GiftCode.TrimStartAndEnd().ToUpper());
    UGameplayStatics::SaveGameToSlot(Save, UGF_GiftSave::SlotName, UGF_GiftSave::UserIndex);

    UE_LOG(LogGift, Warning,
        TEXT("Local claim for %s forgotten. The SERVER still refuses it -- this only re-tests the client path."),
        *GiftCode);
}

//--------------------
// INTERNALS
//--------------------

FText UGF_GiftSubsystem::GetGiftStatusText() const
{
    switch (State)
    {
    case EGF_GiftState::Idle:
        return NSLOCTEXT("Gift", "GiftIdle", "Enter a gift code.");

    case EGF_GiftState::Claiming:
        return NSLOCTEXT("Gift", "GiftClaiming", "Checking that code...");

    case EGF_GiftState::Received:
        return FText::Format(
            NSLOCTEXT("Gift", "GiftReceived", "{0} arrived!"),
            FText::FromName(ReceivedCreature.GetDisplayName()));

    case EGF_GiftState::Failed:
        // The specific reason, not a generic failure. A player entering a code
        // that worked yesterday needs to be told they already have it, or the
        // feature looks broken and becomes a support message.
        return LastError.IsEmpty()
            ? NSLOCTEXT("Gift", "GiftFailedGeneric", "That gift could not be collected.")
            : FText::FromString(LastError);

    default:
        return FText::GetEmpty();
    }
}

void UGF_GiftSubsystem::SetState(EGF_GiftState NewState)
{
    if (State == NewState)
    {
        return;
    }

    State = NewState;
    OnGiftStateChanged.Broadcast(State);
}

void UGF_GiftSubsystem::FailClaim(const FString& Reason)
{
    LastError = Reason;
    bRequestInFlight = false;

    SetState(EGF_GiftState::Failed);
    OnGiftFailed.Broadcast(Reason);
}

UGF_CreatureManagerSubsystem* UGF_GiftSubsystem::GetCreatureManager() const
{
    UGameInstance* GameInstance = GetGameInstance();
    return GameInstance ? GameInstance->GetSubsystem<UGF_CreatureManagerSubsystem>() : nullptr;
}
