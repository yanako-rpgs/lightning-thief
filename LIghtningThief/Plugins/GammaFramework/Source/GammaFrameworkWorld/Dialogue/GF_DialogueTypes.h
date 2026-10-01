#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GF_DialogueTypes.generated.h"

// Forward declarations
class UWorld;
class USoundBase;
class USoundMix;

// ============================================================
// ENUMS
// ============================================================

UENUM(BlueprintType)
enum class EGF_DialogueNodeType : uint8
{
    Message     UMETA(DisplayName = "Message"),       // Standard text box
    Choice      UMETA(DisplayName = "Choice"),        // Show choice buttons
    Event       UMETA(DisplayName = "Event"),         // Fire event, no text
    Hide        UMETA(DisplayName = "Hide"),          // Hide box (cutscene pause)
};

UENUM(BlueprintType)
enum class EGF_DialogueEventType : uint8
{
    None            UMETA(DisplayName = "None"),
    StartBattle     UMETA(DisplayName = "Start Battle"),
    GiveItem        UMETA(DisplayName = "Give Item"),
    SetQuestFlag    UMETA(DisplayName = "Set Quest Flag"),
    PlayAnimation   UMETA(DisplayName = "Play Animation"),
    OpenShop        UMETA(DisplayName = "Open Shop"),
    WarpPlayer      UMETA(DisplayName = "Warp Player"),
    GiveCreature     UMETA(DisplayName = "Give Creature"),    // gives a Creature to the player
    ContinueFlow    UMETA(DisplayName = "Continue Flow"),  // fires named event on caller
    CustomEvent     UMETA(DisplayName = "Custom Event"),   // fires named event on subsystem
    BranchGender    UMETA(DisplayName = "Branch Gender"),  // auto-jumps based on PlayerGender (Boy/Girl)
};

UENUM(BlueprintType)
enum class EGF_DialogueWidgetType : uint8
{
    Overworld   UMETA(DisplayName = "Overworld"),    // White box, grey border
    Cinematic   UMETA(DisplayName = "Cinematic"),    // Black gradient (intro/cutscenes)
    Battle      UMETA(DisplayName = "Battle"),       // Battle text box
};

// ============================================================
// EVENT STRUCT
// ============================================================

USTRUCT(BlueprintType)
struct FGF_DialogueEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event")
    EGF_DialogueEventType Type = EGF_DialogueEventType::None;

    // StartBattle
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::StartBattle", EditConditionHides))
    TSubclassOf<AActor> TamerClass;   // an AGF_TamerMaster subclass, kept loose
    // so GammaFrameworkWorld does not depend on GammaFrameworkCreatures.

    // GiveItem
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::GiveItem", EditConditionHides))
    FName ItemID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::GiveItem", EditConditionHides))
    int32 ItemQuantity = 1;

    // SetQuestFlag
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::SetQuestFlag", EditConditionHides))
    FName QuestFlagName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::SetQuestFlag", EditConditionHides))
    bool bQuestFlagValue = true;

    // PlayAnimation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::PlayAnimation", EditConditionHides))
    FName AnimationName;

    // OpenShop
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::OpenShop", EditConditionHides))
    FName ShopID;

    // WarpPlayer
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::WarpPlayer", EditConditionHides))
    TSoftObjectPtr<UWorld> WarpMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::WarpPlayer", EditConditionHides))
    FVector WarpLocation = FVector::ZeroVector;

    // GiveCreature
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::GiveCreature", EditConditionHides))
    FName CreatureSpeciesName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::GiveCreature", EditConditionHides))
    int32 GiftCreatureLevel = 5;

    // If >= 0: jump to this node index when the party is full (creature goes to PC).
    // If -1: no branching — dialogue continues to NextNodeIndex regardless.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::GiveCreature", EditConditionHides))
    int32 PartyFullNodeIndex = -1;

    // ContinueFlow / CustomEvent — fires this named event
    // BranchGender (Boy) — label to jump to when player is Boy
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::ContinueFlow || Type == EGF_DialogueEventType::CustomEvent || Type == EGF_DialogueEventType::BranchGender", EditConditionHides))
    FName EventName;

    // BranchGender (Girl) — label to jump to when player is Girl
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event",
        meta = (EditCondition = "Type == EGF_DialogueEventType::BranchGender", EditConditionHides))
    FName BranchElseLabel;

    // How long to pause after this event fires before auto-advancing (seconds).
    //  0  = no pause, advance immediately after the event
    // >0  = wait this many seconds then auto-continue (use for item/creature receives, stingers, etc.)
    // -1  = wait forever — only advance when ResumeDialogue() is called manually
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Event", meta = (ClampMin = "-1.0"))
    float ResumeDelay = 0.f;
};

// ============================================================
// CHOICE STRUCT
// ============================================================

USTRUCT(BlueprintType)
struct FGF_DialogueChoice
{
    GENERATED_BODY()

    // Text shown on the button
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Choice")
    FText ChoiceText;

    // Which node to jump to when this choice is selected (-1 = end dialogue)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Choice")
    int32 NextNodeIndex = -1;

    // Optional event fired when this choice is selected
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Choice")
    FGF_DialogueEvent OnSelected;
};

// ============================================================
// NODE STRUCT
// ============================================================

USTRUCT(BlueprintType)
struct FGF_DialogueNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node")
    EGF_DialogueNodeType Type = EGF_DialogueNodeType::Message;

    // Speaker name shown above the text box (leave empty for no name tag)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node")
    FText SpeakerName;

    // The dialogue text (FText = automatically localizable)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Message", EditConditionHides, MultiLine = true))
    FText DialogueText;

    // Choices (only used when Type == Choice)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Choice", EditConditionHides))
    TArray<FGF_DialogueChoice> Choices;

    // Event to fire (only used when Type == Event)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Event", EditConditionHides))
    FGF_DialogueEvent Event;

    // For Hide nodes: auto-resume after this many seconds (0 = wait for ResumeDialogue() call)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Hide", EditConditionHides))
    float ResumeAfterSeconds = 0.f;

    // Sound to play when this node is first displayed (Message nodes only).
    // Leave null for no sound. Useful for item jingles, fanfares, etc.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Message", EditConditionHides))
    TObjectPtr<USoundBase> LineSound = nullptr;

    // Sound Mix to PUSH when this node displays — ducks/muffles the OST while active.
    // Use [duck /Game/Path/To/SoundMix] in .gfdlg. Popped by [unduck] or automatically when dialogue ends.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Message", EditConditionHides))
    TObjectPtr<USoundMix> PushSoundMix = nullptr;

    // If true, POP every sound mix this dialogue pushed when this node displays (restores the OST).
    // Use [unduck] in .gfdlg.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Message", EditConditionHides))
    bool bPopSoundMixes = false;

    // If true, the widget should switch to its "loud" visual variant for this line.
    // Use [loud] in .gfdlg on the line before or after the message.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Message", EditConditionHides))
    bool bLoud = false;

    // Minimum seconds this message must be visible before the player can advance.
    // Use [hold N] in .gfdlg. 0 = no lock (default).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Message", EditConditionHides, ClampMin = "0.0"))
    float MinDisplaySeconds = 0.f;

    // If > 0, dialogue ends automatically after this many seconds (no player input needed).
    // Use [autoend N] in .gfdlg. Combines with [hold N] — hold fires first, then autoend.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Message", EditConditionHides, ClampMin = "0.0"))
    float AutoEndSeconds = 0.f;

    // If > 0, auto-advances to the NEXT node after this many seconds (no player input needed).
    // Use [pause N] in .gfdlg. Unlike [autoend], this does NOT close the dialogue.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node",
        meta = (EditCondition = "Type == EGF_DialogueNodeType::Message", EditConditionHides, ClampMin = "0.0"))
    float AutoAdvanceSeconds = 0.f;

    // Which node comes next (-1 = end dialogue). Ignored for Choice nodes (they route themselves)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Node")
    int32 NextNodeIndex = -1;
};
