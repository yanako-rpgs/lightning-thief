#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GF_DialogueTypes.h"
#include "GF_DialogueAsset.h"
#include "GF_ResettableState.h"
#include "GF_DialogueSubsystem.generated.h"

class UGF_DialogueAsset;
class UUserWidget;
class UGF_DialogueWidgetBase;
class UGF_GridMovementComponent;
class USoundMix;

// ============================================================
// DELEGATES
// ============================================================

// Fires when any dialogue finishes — bind to this on your NPC/tamer Blueprint
// Caller is the actor that started the dialogue (same as GetCurrentCaller() during StartDialogue).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnDialogueFinished, FName, DialogueID, AActor*, Caller);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnDialogueStarted,   FName, DialogueID);

// Fires every time a new node is displayed — use for mid-dialogue camera pans etc
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnDialogueNodeReached, FName, DialogueID, int32, NodeIndex);

// Fires when a Choice node is reached — widget should display these buttons
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGF_OnDialogueChoicesReady, const TArray<FGF_DialogueChoice>&, Choices);

// Fires when the highlighted choice changes (keyboard navigation or mouse hover) —
// bind on an actor to preview the choice in the world before it is committed.
// ChoiceIndex is -1 when nothing is hovered any more (Choice is then a default struct).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnDialogueChoiceHovered, int32, ChoiceIndex, const FGF_DialogueChoice&, Choice);

// Fires when an event node is hit — subsystem handles built-in types automatically,
// this lets Blueprint react to CustomEvent / ContinueFlow types
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGF_OnDialogueEvent, FName, DialogueID, FGF_DialogueEvent, Event);

// Fires when a [hold N] timer expires and the player can now advance
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnDialogueInputUnlocked);

// Fires when dialogue is ready to close — play your closing animation then call EndDialogue()
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGF_OnDialogueClosing);

// ============================================================
// WIDGET TYPE CONFIG
// Stores which widget class corresponds to each EGF_DialogueWidgetType
// Set these up once in your GameInstance or via BP
// ============================================================

USTRUCT(BlueprintType)
struct FGF_DialogueWidgetConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<UUserWidget> OverworldWidgetClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<UUserWidget> CinematicWidgetClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<UUserWidget> BattleWidgetClass;

    // Style tag for dialogue message text. e.g. "Default" → "<Default>hello</>".
    // Leave empty to pass text through unchanged.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DefaultTextStyleTag = TEXT("Default");

    // Style tag for choice button text. Defaults to DefaultTextStyleTag if empty.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ChoiceTextStyleTag = TEXT("ChoiceDefault");
};

/**
 * A dialogue that arrived while another was already playing, held until its turn.
 *
 * Dialogues genuinely collide in this game: a Creature levels up and learns a move at the same
 * instant a tamer is defeated, and both want to speak. Neither line is disposable -- "Sprigling
 * learned First Strike!" is information the player cannot get anywhere else.
 */
USTRUCT()
struct FGF_QueuedDialogue
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UGF_DialogueAsset> Asset = nullptr;

    UPROPERTY()
    TObjectPtr<AActor> Caller = nullptr;

    // Callers set their variables BEFORE calling StartDialogue, so they belong to this queued
    // dialogue and must ride along with it -- the live dialogue will clear the shared map when
    // it ends, long before this one renders a line.
    TMap<FName, FString> Variables;
};

// ============================================================
// SUBSYSTEM
// ============================================================

UCLASS()
class GAMMAFRAMEWORKWORLD_API UGF_DialogueSubsystem : public UGameInstanceSubsystem, public IGF_ResettableState
{
    GENERATED_BODY()

public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --------------------------------------------------------
    // CONFIGURATION
    // Call this once from your GameInstance Blueprint on init
    // --------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Dialogue|Setup")
    void SetWidgetConfig(const FGF_DialogueWidgetConfig& Config);

    // --------------------------------------------------------
    // PRIMARY API — this is the ONE function you call everywhere
    // --------------------------------------------------------

    /**
     * Start a dialogue conversation.
     *
     * @param Asset      The dialogue data asset to play
     * @param Caller     The actor that triggered this dialogue (NPC, sign, etc.)
     *                   ContinueFlow events will fire named events on this actor.
     */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void StartDialogue(UGF_DialogueAsset* Asset, AActor* Caller = nullptr);

    // Advance to the next node (call this when player presses confirm)
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void AdvanceDialogue();

    // Select a choice by index (call from choice button widget)
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void SelectChoice(int32 ChoiceIndex);

    /** Tell the subsystem which choice is currently highlighted — call from the choice
     *  button's hover / UINav navigate event, NOT from its click event.
     *  Broadcasts OnDialogueChoiceHovered only when the index actually changes, so it is
     *  safe to call every frame or from both mouse-hover and keyboard-navigation paths. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void NotifyChoiceHovered(int32 ChoiceIndex);

    /** Broadcasts OnDialogueChoiceHovered with index -1 so listeners can clear their preview
     *  (e.g. mouse left the buttons). Does nothing if nothing is hovered. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void ClearChoiceHover();

    /** Currently highlighted choice index, or -1 if none. */
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    int32 GetHoveredChoiceIndex() const { return HoveredChoiceIndex; }

    // Hide the dialogue box (for cutscene pauses) — call ResumeDialogue() when ready
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void HideDialogue();

    // Resume after a Hide node (call this when your camera pan / animation finishes)
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void ResumeDialogue();

    // Force-end dialogue early (use sparingly — prefer letting it complete naturally)
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void EndDialogue();

    /** Call this during a soft reset / level reload BEFORE OpenLevel.
     *  Clears all active dialogue state without touching widgets (which are
     *  already being destroyed by the level teardown). Does NOT broadcast
     *  OnDialogueFinished — use this only for reset flows, not normal endings. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void ResetDialogueState();

    // IGF_ResettableState - ResetDialogueState already clears bIsActive and, critically,
    // releases the movement lock. Skipping it softlocks the next run.
    virtual void ResetToBootState() override { ResetDialogueState(); }

    /** Jump to a specific node index mid-dialogue.
     *  Use this from Blueprint after a CustomEvent pause to implement conditional branching.
     *  e.g. check PlayerGender then call JumpToNode(BoyNodeIndex) or JumpToNode(GirlNodeIndex). */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void JumpToNode(int32 NodeIndex);

    /** Jump to a named label in the current dialogue asset.
     *  Labels are defined with @label in the .gfdlg file. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void JumpToLabel(FName LabelName);

    // Signals the widget to play its closing animation.
    // Call EndDialogue() from Blueprint when the animation finishes.
    // If you don't, a watchdog forces it after CloseWatchdogSeconds and logs a warning —
    // a missed EndDialogue() used to lock the player out of the game permanently.
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void BeginCloseDialogue();

    // Returns choice text wrapped in ChoiceTextStyleTag (or DefaultTextStyleTag as fallback).
    // Call this when building choice buttons instead of using ChoiceText directly.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    FString GetChoiceDisplayText(const FGF_DialogueChoice& Choice) const;

    // Returns the display name of the last Creature given via a GiveCreature event.
    // Use {Creature} in dialogue text to have it substituted automatically.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    FString GetLastGivenCreatureName() const { return LastGivenCreatureName; }

    // Override the text of a specific node at runtime — use for dynamic messages
    // like "added to party" vs "sent to PC". Call this BEFORE ResumeDialogue().
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void SetNodeText(int32 NodeIndex, FText NewText);

    // Set a named variable for text substitution. Use {VarName} in .gfdlg text.
    // Call this before StartDialogue. e.g. SetDialogueVariable("Reward", "3000")
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void SetDialogueVariable(FName Key, const FString& Value);

    // Clear all variables (called automatically when dialogue ends)
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void ClearDialogueVariables();

    // Apply {Key} substitution to any string — used internally for both body and speaker name
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    FString SubstituteVariables(const FString& Input) const;

    // --------------------------------------------------------
    // STATE QUERIES
    // --------------------------------------------------------

    // Returns the currently active dialogue widget cast to the base class.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    UGF_DialogueWidgetBase* GetActiveDialogueWidget() const;

    // Queue an item icon to display when the first dialogue node fires.
    // Call this right after StartDialogue() — safe to call on the same frame.
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void SetPendingItemIcon(UTexture2D* Icon);

    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool IsDialogueActive() const { return bIsActive; }

    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool IsInputLocked() const { return bInputLocked; }

    UFUNCTION(BlueprintPure, Category = "Dialogue")
    AActor* GetCurrentCaller() const { return CurrentCaller; }

    /** Swap the active caller mid-dialogue.
     *  Use this before a ContinueFlow node that should fire on a different actor
     *  (e.g. switching speaker from an NPC to a TV). */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void SetCurrentCaller(AActor* NewCaller) { CurrentCaller = NewCaller; }

    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool IsWaitingForChoice() const { return bWaitingForChoice; }

    /** Returns true if the current node is the last one — advancing will end the dialogue.
     *  Use this in the widget's OnTypingComplete to hide the "more" arrow on the final line. */
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool IsLastNode() const;

    /** Returns true if pressing confirm should actually advance dialogue right now.
     *  Use this before playing a confirm sound or animation — if false, swallow the input silently. */
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool CanAdvance() const { return bIsActive && !bWaitingForChoice && !bIsHidden && !bInputLocked; }

    UFUNCTION(BlueprintPure, Category = "Dialogue")
    FName GetCurrentDialogueID() const;

    UFUNCTION(BlueprintPure, Category = "Dialogue")
    int32 GetCurrentNodeIndex() const { return CurrentNodeIndex; }

    // Returns the current node so the widget can read text, speaker name, type etc.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool GetCurrentNode(FGF_DialogueNode& OutNode) const
    {
        if (CurrentAsset && CurrentAsset->Nodes.IsValidIndex(CurrentNodeIndex))
        {
            OutNode = CurrentAsset->Nodes[CurrentNodeIndex];
            return true;
        }
        return false;
    }

    // Returns the node's dialogue text ready for a URichTextBlock.
    // If the text contains no markup (no '<'), it is automatically wrapped
    // in the DefaultTextStyleTag from widget config (e.g. "<Default>text</>").
    // Call this ONLY when typing is complete — not for partial typewriter reveals.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    FString GetDisplayText(const FGF_DialogueNode& Node) const;

    // Returns the number of visible (plain text) characters in the node.
    // Use this as the typewriter's target character count — NOT the length
    // of GetDisplayText, which includes markup characters.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    int32 GetPlainTextLength(const FGF_DialogueNode& Node) const;

    // Returns a partially-revealed version of the node's text, safe for URichTextBlock.
    // Wraps only the revealed plain-text characters in the default style tag.
    // Use this each TypewriterTick frame instead of substringing GetDisplayText.
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    FString GetPartialDisplayText(const FGF_DialogueNode& Node, int32 NumChars) const;

    // --------------------------------------------------------
    // DELEGATES — bind to these on your NPC/tamer Blueprints
    // --------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Dialogue|Events")
    FGF_OnDialogueFinished OnDialogueFinished;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue|Events")
    FGF_OnDialogueStarted OnDialogueStarted;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue|Events")
    FGF_OnDialogueNodeReached OnDialogueNodeReached;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue|Events")
    FGF_OnDialogueChoicesReady OnDialogueChoicesReady;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue|Events")
    FGF_OnDialogueChoiceHovered OnDialogueChoiceHovered;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue|Events")
    FGF_OnDialogueEvent OnDialogueEvent;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue|Events")
    FGF_OnDialogueInputUnlocked OnInputUnlocked;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue|Events")
    FGF_OnDialogueClosing OnDialogueClosing;

private:

    // --------------------------------------------------------
    // INTERNAL STATE
    // --------------------------------------------------------

    bool bIsActive          = false;
    bool bWaitingForChoice  = false;
    int32 HoveredChoiceIndex = -1;   // highlighted choice on the current Choice node, -1 = none
    bool bIsHidden          = false;
    bool bInputLocked       = false;  // true while a [hold N] timer is running

    UPROPERTY()
    UGF_DialogueAsset* CurrentAsset = nullptr;

    UPROPERTY()
    AActor* CurrentCaller = nullptr;

    UPROPERTY()
    UUserWidget* ActiveWidget = nullptr;

    int32 CurrentNodeIndex = -1;

    // Widget config set via SetWidgetConfig
    FGF_DialogueWidgetConfig WidgetConfig;

    // Auto-resume timer handle for Hide nodes with ResumeAfterSeconds > 0
    FTimerHandle AutoResumeTimerHandle;

    // Input-lock timer handle for Message nodes with MinDisplaySeconds > 0
    FTimerHandle HoldTimerHandle;

    // Auto-end timer handle for Message nodes with AutoEndSeconds > 0
    FTimerHandle AutoEndTimerHandle;

    // Auto-advance timer handle for Message nodes with AutoAdvanceSeconds > 0
    FTimerHandle AutoAdvanceTimerHandle;

    // One-frame delay timer so the widget can construct and bind before first node fires
    FTimerHandle FirstNodeTimerHandle;

    // Dialogues that arrived mid-conversation, played in order as each one ends.
    // Drained by EndDialogue(); cleared by ResetDialogueState().
    UPROPERTY()
    TArray<FGF_QueuedDialogue> QueuedDialogues;

    // Guards against a queued dialogue being started from inside EndDialogue()'s own broadcast.
    bool bIsDrainingQueue = false;

    void StartNextQueuedDialogue();

public:
    /**
     * Throw away everything waiting to speak, without stranding whoever queued it.
     *
     * Queued dialogue is normally the right answer -- two lines that collide should both be heard.
     * But some lines are only TRUE at the moment they are raised. "Sprigling learned First Strike!"
     * is queued behind the forget-a-move prompt, and if the player then declines, that line is a
     * lie by the time the queue reaches it. It shipped saying the move was learned no matter what
     * the player chose.
     *
     * Dropping a queued dialogue is only safe because this broadcasts OnDialogueFinished for each
     * one as it goes: callers lock player movement before starting a dialogue and wait on that
     * event to unlock, so silently discarding without it is how the original softlock happened.
     *
     * Call this when a decision invalidates whatever is still queued -- not as a general cleanup.
     */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    int32 ClearQueuedDialogues();

private:

    // Rescues a dialogue whose bound widget never called EndDialogue(). See BeginCloseDialogue().
    FTimerHandle CloseWatchdogHandle;
    void ForceCloseIfStillActive();

    // Which dialogue the pending watchdog belongs to.
    //
    // Bumped by every StartDialogue. The watchdog captures the value when armed and refuses to
    // fire if it no longer matches, because a timer can outlive the dialogue that set it: a
    // watchdog armed at 09:03:50 once force-closed a DIFFERENT dialogue that had opened 0.24s
    // earlier, killing the Fernhollow Woods post-battle cutscene 3s after an unrelated line began
    // closing. A rescue that can cancel an innocent dialogue is worse than the hang it prevents.
    uint32 DialogueSerial        = 0;
    uint32 CloseWatchdogSerial   = 0;

    // Generous next to a real outro animation (~0.3s). This is a safety net, not a
    // schedule — if it ever fires, the widget is broken and the log says so.
    static constexpr float CloseWatchdogSeconds = 3.0f;

    // Name of the last Creature given via GiveCreature — substituted for {Creature} in text
    FString LastGivenCreatureName;

    // Named variables substituted into dialogue text as {Key}
    TMap<FName, FString> DialogueVariables;

    // When GiveCreature branches (party full), ProcessEventNode uses this to override
    // the node's own NextNodeIndex before advancing. -1 = no override.
    int32 PendingNextNodeOverride = -1;

    // Item icon queued via SetPendingItemIcon() — applied on the first node tick
    UPROPERTY()
    UTexture2D* PendingItemIcon = nullptr;

    // Sound mixes pushed by [duck] tags this conversation. Popped by [unduck] or
    // automatically on EndDialogue/ResetDialogueState so the OST is never left muffled.
    UPROPERTY()
    TArray<TObjectPtr<USoundMix>> ActiveSoundMixes;

    // --------------------------------------------------------
    // INTERNAL HELPERS
    // --------------------------------------------------------

    void ProcessFirstNode();
    void ProcessNode(int32 NodeIndex);
    void UnlockInput();
    void AutoAdvanceDialogue();  // Clears lock then advances - used by [pause N] timer
    void ProcessMessageNode(const FGF_DialogueNode& Node);
    void ProcessChoiceNode(const FGF_DialogueNode& Node);
    void ProcessEventNode(const FGF_DialogueNode& Node);
    void ProcessHideNode(const FGF_DialogueNode& Node);

    void ExecuteEvent(const FGF_DialogueEvent& Event);

    void ShowWidget(EGF_DialogueWidgetType WidgetType);
    void HideWidget();

    void LockPlayerMovement();
    void UnlockPlayerMovement();

    // Pops every sound mix pushed by [duck] tags this conversation and clears the list.
    void PopActiveSoundMixes();

    // Calls a named Blueprint event on the given actor (used for ContinueFlow)
    void FireNamedEventOnActor(AActor* Target, FName EventName);
};
