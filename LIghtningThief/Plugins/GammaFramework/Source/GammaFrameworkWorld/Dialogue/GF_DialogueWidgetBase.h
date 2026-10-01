#pragma once

#include "CoreMinimal.h"
#include "UINavWidget.h"
#include "GF_DialogueTypes.h"
#include "Settings/GF_SettingsTypes.h"
#include "GF_DialogueWidgetBase.generated.h"

class URichTextBlock;
class UTextBlock;
class UImage;
class UGF_DialogueSubsystem;
class UGF_SettingsSubsystem;

/**
 * Base class for all Gamma Framework dialogue widgets.
 *
 * This class binds to GF_DialogueSubsystem delegates directly in C++,
 * so Blueprint never needs to bind events manually. Just reparent your
 * widget Blueprint to this class and implement the BlueprintImplementableEvents.
 *
 * Required widget names (BindWidgetOptional — missing ones are silently skipped):
 *   "DialogueText"   — URichTextBlock for the dialogue body
 *   "SpeakerNameText" — UTextBlock for the speaker name
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class GAMMAFRAMEWORKWORLD_API UGF_DialogueWidgetBase : public UUINavWidget
{
    GENERATED_BODY()

public:

    // --------------------------------------------------------
    // TYPEWRITER API — call from Blueprint confirm input
    // --------------------------------------------------------

    /** Reveal dialogue text one character at a time.
     *  Called automatically by the base class — you do NOT need to call this
     *  manually unless you are doing something custom. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue|Typewriter")
    void StartTypewriter(const FString& RichText);

    /** If bIsTyping, reveals all text instantly and fires OnTypingComplete.
     *  Call this when the player presses confirm while text is still typing. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue|Typewriter")
    void SkipToEnd();

    /** True while the typewriter is animating.
     *  Confirm input: if bIsTyping → SkipToEnd, else → AdvanceDialogue. */
    UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Typewriter")
    bool bIsTyping = false;

    /** Visible characters revealed per second.
     *  Overwritten from the player's Text Speed option while bUseSettingsTextSpeed
     *  is true — set that to false to keep the value authored here.
     *  0 or below means "no animation": the full line appears at once (Instant). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Typewriter")
    float TypewriterSpeed = 30.f;

    /** When true this widget follows the player's Text Speed option, updating live
     *  if it is changed while the box is open. Turn off for cutscene boxes that must
     *  type at a fixed pace. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Typewriter")
    bool bUseSettingsTextSpeed = true;

    /** Pulls TypewriterSpeed from the settings subsystem. Called automatically on
     *  construct and whenever the player changes a setting. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue|Typewriter")
    void ApplyTextSpeedFromSettings();

    // --------------------------------------------------------
    // DIALOGUE FRAME (player-selectable box skin)
    // --------------------------------------------------------

    /** When true this widget applies the player's chosen dialogue frame.
     *  Turn off for boxes with a fixed look (chat bubbles, battle prompts). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Frame")
    bool bUseSettingsDialogueFrame = true;

    /** Applies the player's chosen frame to FrameImage and fires OnDialogueFrameChanged.
     *  Called automatically on construct and on any settings change. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue|Frame")
    void ApplyDialogueFrameFromSettings();

    /** Style tag wrapped around partially-revealed text during animation.
     *  Must match a row in your RichTextBlock's style DataTable.
     *  Should match DefaultTextStyleTag in GF_DialogueSubsystem widget config. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Typewriter")
    FString RevealStyleTag = TEXT("Default");

    /** Style tag used for the unrevealed (invisible) portion of text during typewriting.
     *  Must match a row in your RichTextBlock's style DataTable that has the same font/size
     *  as your default text style but with TextColor.A = 0 (fully transparent).
     *  This keeps the full text in the layout at all times so words never jump to the next
     *  line mid-type.  Leave empty to disable (falls back to the old cut-text behaviour). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Typewriter")
    FString InvisibleTextStyleTag = TEXT("DialogueInvisible");

    // Show an item icon in the dialogue box. Pass nullptr to hide it.
    // Fires OnShowItemAnimation so Blueprint can play a slide-in animation.
    UFUNCTION(BlueprintCallable, Category = "Dialogue|Item")
    void SetItemIcon(UTexture2D* Icon);

    // Hide the item icon (called automatically when dialogue ends).
    UFUNCTION(BlueprintCallable, Category = "Dialogue|Item")
    void ClearItemIcon();

protected:

    // --------------------------------------------------------
    // BLUEPRINT IMPLEMENTABLE EVENTS
    // Override these in your widget Blueprint for custom behaviour.
    // The base class handles text display and speaker name automatically.
    // --------------------------------------------------------

    /** Called when a Message node is reached and the typewriter has started.
     *  Use for any extra per-node logic (camera, sound, etc.) */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
    void OnNodeReached(FName DialogueID, int32 NodeIndex);

    /** Called when a Message node is reached. bLoud=true means swap to the loud box variant. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
    void OnNodeStyle(bool bLoud);

    /** Called when a Choice node is reached. Spawn/show your choice buttons here. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
    void OnChoicesReady(const TArray<FGF_DialogueChoice>& Choices);

    /** Called when dialogue ends. Hide the widget here if needed. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
    void OnDialogueEnded(FName DialogueID);

    /** Called when dialogue is ready to close. Play your closing animation here,
     *  then call EndDialogue() on the subsystem when it finishes. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
    void OnDialogueClosing();

    /** Called when all characters are revealed. Show your advance indicator. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue|Typewriter")
    void OnTypingComplete();

    /**
     * Called when an item icon is ready to display.
     * The texture has already been applied to ItemIconImage — override this in
     * Blueprint to play your slide-in (or pop-in) animation.
     * @param Icon  The item's texture (never nullptr when this fires).
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue|Item")
    void OnShowItemAnimation(UTexture2D* Icon);

    /** Called when a new typewriter sequence starts. Hide your advance indicator. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue|Typewriter")
    void OnTypingStarted();

    /**
     * Called when the player's dialogue frame is applied — on construct and again
     * whenever they change it in the options screen.
     * The brush has already been set on FrameImage (if that widget exists); override
     * this to restyle anything else the frame owns, e.g. swapping the advance-arrow
     * art. Text colour and font are NOT part of a frame — they belong to the
     * RichTextBlock's style DataTable and are never driven by player settings.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue|Frame")
    void OnDialogueFrameChanged(const FGF_DialogueFrame& Frame);

    // --------------------------------------------------------
    // BOUND WIDGETS — name these exactly in your UMG hierarchy
    // --------------------------------------------------------

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<URichTextBlock> DialogueText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> SpeakerNameText;

    // Optional item icon image — name this widget "ItemIconImage" in your UMG hierarchy.
    // Automatically hidden when dialogue ends.
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UImage> ItemIconImage;

    // Optional dialogue box frame — name this widget "FrameImage" in your UMG hierarchy
    // and the player's chosen frame texture is applied to it automatically.
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UImage> FrameImage;

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    // Overridden to prevent UINavigation from restoring a previously-open menu
    // (e.g. the Bag) when the dialogue widget is removed. UINavWidget::RemoveFromParent
    // intercepts the call and reads ParentWidget before NativeDestruct ever fires,
    // so we must clear it here — before the Super call — to stop the re-add.
    virtual void RemoveFromParent() override;

private:

    // --------------------------------------------------------
    // SUBSYSTEM DELEGATE HANDLERS (bound in NativeConstruct)
    // --------------------------------------------------------

    UFUNCTION()
    void HandleNodeReached(FName DialogueID, int32 NodeIndex);

    UFUNCTION()
    void HandleChoicesReady(const TArray<FGF_DialogueChoice>& Choices);

    UFUNCTION()
    void HandleDialogueFinished(FName DialogueID, AActor* Caller);

    // --------------------------------------------------------
    // TYPEWRITER INTERNALS
    // --------------------------------------------------------

    FString      FullRichText;
    FString      PlainChars;
    int32        RevealedCount = 0;
    FTimerHandle TypewriterTimerHandle;

    void TypewriterTick();
    void ApplyRevealedText(int32 NumChars);
    void FinishTypewriter();

    static FString StripRichTextTags(const FString& Input);

    // Returns the first NumChars visible characters of RichText with all markup tags preserved
    // and closed, followed by the remaining text wrapped in InvisibleTextStyleTag (if set) so
    // the full string always occupies the same layout space — preventing mid-word line jumps.
    FString PartialRichText(const FString& RichText, int32 NumChars) const;

    // Set when typing finishes while input is still locked — OnTypingComplete fires when unlock arrives
    bool bPendingTypingComplete = false;

    UFUNCTION()
    void HandleInputUnlocked();

    UFUNCTION()
    void HandleDialogueClosing();

    // Re-applies text speed and frame when the player changes an option while the
    // box is open (e.g. options screen opened over a paused dialogue).
    UFUNCTION()
    void HandleSettingsChanged();

    // --------------------------------------------------------
    // WINDOW-FOCUS RECOVERY
    // --------------------------------------------------------
    // The dialogue advance input is driven by this widget's OnKeyDown, which
    // requires the widget to hold keyboard focus. Alt-tab / minimize / clicking
    // another app drops that focus, and clicking back gives focus to the game
    // viewport instead of the widget — so input goes dead. NativeTick re-asserts
    // focus every frame the box is open whenever nothing in our widget tree holds
    // it, which self-corrects after the viewport steals focus on window restore.
    // The "nothing in our tree" guard means we never yank focus off a focused
    // choice button.

    // Cached subsystem pointer — set in NativeConstruct, cleared in NativeDestruct
    UPROPERTY()
    TObjectPtr<UGF_DialogueSubsystem> CachedSubsystem;

    // Cached options subsystem — same lifetime as CachedSubsystem
    UPROPERTY()
    TObjectPtr<UGF_SettingsSubsystem> CachedSettings;
};
