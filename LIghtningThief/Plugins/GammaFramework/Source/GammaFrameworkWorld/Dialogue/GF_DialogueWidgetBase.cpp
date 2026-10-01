#include "GF_DialogueWidgetBase.h"
#include "GF_DialogueSubsystem.h"
#include "Settings/GF_SettingsSubsystem.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// ============================================================
// LIFECYCLE
// ============================================================

void UGF_DialogueWidgetBase::NativeConstruct()
{
    // UINavWidget's NativeConstruct requires a valid player controller.
    // In the designer preview there is none — skip entirely to avoid the crash.
    if (IsDesignTime()) return;

    Super::NativeConstruct();

    ParentWidget = nullptr;

    UGameInstance* GI = GetGameInstance();
    if (!GI) return;

    // Player options — bind before the dialogue subsystem so the very first line
    // types at the right speed and the box already wears the right frame.
    CachedSettings = GI->GetSubsystem<UGF_SettingsSubsystem>();
    if (CachedSettings)
    {
        CachedSettings->OnSettingsChanged.AddDynamic(this, &UGF_DialogueWidgetBase::HandleSettingsChanged);
    }
    ApplyTextSpeedFromSettings();
    ApplyDialogueFrameFromSettings();

    CachedSubsystem = GI->GetSubsystem<UGF_DialogueSubsystem>();
    if (!CachedSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_DialogueWidgetBase: Could not find GF_DialogueSubsystem. Is the Game Instance class set in Project Settings?"));
        return;
    }

    // Bind directly in C++ — no Blueprint event binding needed
    CachedSubsystem->OnDialogueNodeReached.AddDynamic(this, &UGF_DialogueWidgetBase::HandleNodeReached);
    CachedSubsystem->OnDialogueChoicesReady.AddDynamic(this, &UGF_DialogueWidgetBase::HandleChoicesReady);
    CachedSubsystem->OnDialogueFinished.AddDynamic(this, &UGF_DialogueWidgetBase::HandleDialogueFinished);
    CachedSubsystem->OnInputUnlocked.AddDynamic(this, &UGF_DialogueWidgetBase::HandleInputUnlocked);
    CachedSubsystem->OnDialogueClosing.AddDynamic(this, &UGF_DialogueWidgetBase::HandleDialogueClosing);
}

void UGF_DialogueWidgetBase::NativeDestruct()
{
    if (IsDesignTime())
    {
        Super::NativeDestruct();
        return;
    }

    if (CachedSubsystem)
    {
        CachedSubsystem->OnDialogueNodeReached.RemoveDynamic(this, &UGF_DialogueWidgetBase::HandleNodeReached);
        CachedSubsystem->OnDialogueChoicesReady.RemoveDynamic(this, &UGF_DialogueWidgetBase::HandleChoicesReady);
        CachedSubsystem->OnDialogueFinished.RemoveDynamic(this, &UGF_DialogueWidgetBase::HandleDialogueFinished);
        CachedSubsystem->OnInputUnlocked.RemoveDynamic(this, &UGF_DialogueWidgetBase::HandleInputUnlocked);
        CachedSubsystem->OnDialogueClosing.RemoveDynamic(this, &UGF_DialogueWidgetBase::HandleDialogueClosing);
        CachedSubsystem = nullptr;
    }

    // Unbound separately — the settings subsystem outlives every dialogue widget,
    // so a stale binding here would keep this widget alive after it is removed.
    if (CachedSettings)
    {
        CachedSettings->OnSettingsChanged.RemoveDynamic(this, &UGF_DialogueWidgetBase::HandleSettingsChanged);
        CachedSettings = nullptr;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TypewriterTimerHandle);
    }

    ParentWidget = nullptr;

    Super::NativeDestruct();
}

void UGF_DialogueWidgetBase::RemoveFromParent()
{
    // UINavWidget::RemoveFromParent() intercepts this call and reads ParentWidget
    // BEFORE NativeDestruct fires. If ParentWidget is set, it calls ReturnToParent()
    // which re-adds the previous menu (e.g. the Bag) back to the viewport.
    // Clearing it here — before Super — ensures UINav treats this as a root widget
    // removal and just cleans up gracefully instead of restoring anything.
    ParentWidget = nullptr;
    Super::RemoveFromParent();
}

// ============================================================
// PLAYER OPTIONS
// ============================================================

void UGF_DialogueWidgetBase::ApplyTextSpeedFromSettings()
{
    if (!bUseSettingsTextSpeed || !CachedSettings) return;

    // 0 means Instant. StartTypewriter reads this as "reveal everything now".
    TypewriterSpeed = CachedSettings->GetTypewriterCharsPerSecond();
}

void UGF_DialogueWidgetBase::ApplyDialogueFrameFromSettings()
{
    if (!bUseSettingsDialogueFrame || !CachedSettings) return;

    FGF_DialogueFrame Frame;
    if (!CachedSettings->GetCurrentDialogueFrame(Frame))
    {
        // No frames configured yet — leave whatever the widget was authored with.
        return;
    }

    if (FrameImage)
    {
        FrameImage->SetBrush(CachedSettings->GetCurrentDialogueFrameBrush());
    }

    OnDialogueFrameChanged(Frame);
}

void UGF_DialogueWidgetBase::HandleSettingsChanged()
{
    ApplyTextSpeedFromSettings();
    ApplyDialogueFrameFromSettings();

    // A speed change mid-line only takes effect on the next line unless we
    // restart the running timer at the new interval.
    if (bIsTyping)
    {
        if (TypewriterSpeed <= 0.f)
        {
            // Switched to Instant — dump the rest of the line immediately.
            SkipToEnd();
        }
        else if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(TypewriterTimerHandle);
            World->GetTimerManager().SetTimer(
                TypewriterTimerHandle,
                this,
                &UGF_DialogueWidgetBase::TypewriterTick,
                1.f / TypewriterSpeed,
                true);
        }
    }
}

// ============================================================
// SUBSYSTEM DELEGATE HANDLERS
// ============================================================

void UGF_DialogueWidgetBase::HandleNodeReached(FName DialogueID, int32 NodeIndex)
{
    if (!CachedSubsystem) return;

    FGF_DialogueNode Node;
    if (!CachedSubsystem->GetCurrentNode(Node)) return;

    if (Node.Type == EGF_DialogueNodeType::Message)
    {
        // Speaker name — apply same {Key} variable substitution as body text
        if (SpeakerNameText)
        {
            FString SpeakerStr = Node.SpeakerName.ToString();
            SpeakerStr = CachedSubsystem->SubstituteVariables(SpeakerStr);
            SpeakerNameText->SetText(FText::FromString(SpeakerStr));
            SpeakerNameText->SetVisibility(
                SpeakerStr.IsEmpty()
                    ? ESlateVisibility::Collapsed
                    : ESlateVisibility::Visible
            );
        }

        // Start typewriter with the fully-formatted rich text
        StartTypewriter(CachedSubsystem->GetDisplayText(Node));

        // Notify Blueprint to swap box variant if needed
        OnNodeStyle(Node.bLoud);
    }

    // Notify Blueprint for any extra per-node logic
    OnNodeReached(DialogueID, NodeIndex);
}

void UGF_DialogueWidgetBase::HandleChoicesReady(const TArray<FGF_DialogueChoice>& Choices)
{
    OnChoicesReady(Choices);
}

void UGF_DialogueWidgetBase::HandleDialogueFinished(FName DialogueID, AActor* Caller)
{
    ClearItemIcon();
    OnDialogueEnded(DialogueID);
}

// ============================================================
// ITEM ICON
// ============================================================

void UGF_DialogueWidgetBase::SetItemIcon(UTexture2D* Icon)
{
    if (Icon)
    {
        // Apply brush/visibility if the widget is bound — Blueprint can also do this
        if (ItemIconImage)
        {
            ItemIconImage->SetBrushFromTexture(Icon, /*bMatchSize=*/true);
            ItemIconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
        }

        // Always fire — Blueprint owns the animation regardless of binding
        OnShowItemAnimation(Icon);
    }
    else
    {
        ClearItemIcon();
    }
}

void UGF_DialogueWidgetBase::ClearItemIcon()
{
    if (ItemIconImage)
    {
        ItemIconImage->SetVisibility(ESlateVisibility::Collapsed);
    }
}

// ============================================================
// TYPEWRITER — PUBLIC API
// ============================================================

void UGF_DialogueWidgetBase::StartTypewriter(const FString& RichText)
{
    // Cancel any in-progress typewriter first
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TypewriterTimerHandle);
    }

    FullRichText  = RichText;
    PlainChars    = StripRichTextTags(RichText);
    RevealedCount = 0;

    if (!DialogueText)
    {
        FinishTypewriter();
        return;
    }

    if (PlainChars.IsEmpty())
    {
        // Event / Hide node — no text to type
        DialogueText->SetText(FText::GetEmpty());
        bIsTyping = false;
        return;
    }

    bIsTyping = true;
    OnTypingStarted();

    // Show first character immediately to avoid a blank flash
    ApplyRevealedText(1);
    RevealedCount = 1;

    if (RevealedCount >= PlainChars.Len())
    {
        FinishTypewriter();
        return;
    }

    // Instant text speed (or any non-positive speed) means no animation at all.
    // A looping timer with a zero rate never fires, so this has to short-circuit
    // rather than fall through to SetTimer.
    if (TypewriterSpeed <= 0.f)
    {
        FinishTypewriter();
        return;
    }

    GetWorld()->GetTimerManager().SetTimer(
        TypewriterTimerHandle,
        this,
        &UGF_DialogueWidgetBase::TypewriterTick,
        1.f / TypewriterSpeed,
        true
    );
}

void UGF_DialogueWidgetBase::SkipToEnd()
{
    if (!bIsTyping) return;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TypewriterTimerHandle);
    }

    FinishTypewriter();
}

// ============================================================
// TYPEWRITER — INTERNALS
// ============================================================

void UGF_DialogueWidgetBase::TypewriterTick()
{
    RevealedCount++;

    if (RevealedCount >= PlainChars.Len())
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(TypewriterTimerHandle);
        }
        FinishTypewriter();
    }
    else
    {
        ApplyRevealedText(RevealedCount);
    }
}

void UGF_DialogueWidgetBase::ApplyRevealedText(int32 NumChars)
{
    if (!DialogueText) return;
    DialogueText->SetText(FText::FromString(PartialRichText(FullRichText, NumChars)));
}

void UGF_DialogueWidgetBase::FinishTypewriter()
{
    bIsTyping = false;

    if (DialogueText)
    {
        // Apply full rich text — activates any inline effects (Wave, Shake, etc.)
        DialogueText->SetText(FText::FromString(FullRichText));
    }

    // If a [hold N] timer is still running, defer the indicator until input unlocks
    if (CachedSubsystem && CachedSubsystem->IsInputLocked())
    {
        bPendingTypingComplete = true;
    }
    else
    {
        bPendingTypingComplete = false;
        OnTypingComplete();
    }
}

void UGF_DialogueWidgetBase::HandleInputUnlocked()
{
    if (bPendingTypingComplete)
    {
        bPendingTypingComplete = false;
        OnTypingComplete();
    }
}

void UGF_DialogueWidgetBase::HandleDialogueClosing()
{
    OnDialogueClosing();
}

void UGF_DialogueWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (IsDesignTime()) return;

    // Keep keyboard focus on the dialogue box while it's open so the OnKeyDown
    // advance input survives alt-tab / minimize / clicking away. On window restore
    // the game viewport steals focus for a frame or two; re-asserting here every
    // frame self-corrects until it sticks.
    //
    // Guard: only reclaim when NOTHING in our widget tree holds focus. If a choice
    // button (a descendant) is focused we leave it alone, so menu navigation is
    // never disturbed.
    APlayerController* PC = GetOwningPlayer();
    if (PC && !HasUserFocus(PC) && !HasUserFocusedDescendants(PC))
    {
        SetKeyboardFocus();
    }
}

// ============================================================
// HELPERS
// ============================================================

FString UGF_DialogueWidgetBase::PartialRichText(const FString& RichText, int32 NumChars) const
{
    // ── Phase 1: build the visible portion ──────────────────────────────────
    // Walk the rich text, copying tags verbatim and plain characters until we
    // have emitted NumChars visible characters.  Track open tags so we can
    // close them cleanly before appending the hidden portion.

    FString VisiblePart;
    TArray<FString> OpenTags;
    FString CurrentTag;
    int32 VisibleCount = 0;
    bool bInTag = false;
    int32 HiddenStartPos = RichText.Len();   // position where unrevealed text begins

    for (int32 i = 0; i < RichText.Len(); ++i)
    {
        TCHAR Ch = RichText[i];

        if (Ch == TEXT('<'))
        {
            bInTag = true;
            CurrentTag.Empty();
            VisiblePart.AppendChar(Ch);
            continue;
        }

        if (bInTag)
        {
            if (Ch == TEXT('>'))
            {
                bInTag = false;
                VisiblePart.AppendChar(Ch);
                if (CurrentTag.StartsWith(TEXT("/")))
                {
                    if (OpenTags.Num() > 0) OpenTags.Pop();
                }
                else
                {
                    OpenTags.Add(CurrentTag);
                }
                CurrentTag.Empty();
            }
            else
            {
                CurrentTag.AppendChar(Ch);
                VisiblePart.AppendChar(Ch);
            }
            continue;
        }

        // Plain (visible) character
        if (VisibleCount >= NumChars)
        {
            HiddenStartPos = i;   // rest of RichText goes into the invisible portion
            break;
        }
        VisiblePart.AppendChar(Ch);
        ++VisibleCount;
    }

    // Close any markup tags that are still open inside the visible portion
    for (int32 i = OpenTags.Num() - 1; i >= 0; --i)
    {
        VisiblePart += TEXT("</>");
    }

    // ── Phase 2: append the invisible placeholder ────────────────────────────
    // If InvisibleTextStyleTag is set and there is unrevealed text, wrap the
    // remaining PLAIN characters in the invisible style.  Rich-text decorators
    // (Wave, Shake…) are stripped from this portion because the hidden text only
    // needs to occupy the correct character widths — it is never seen.
    // Keeping the full string in the layout at all times means Slate never
    // re-wraps the text, so words cannot jump to the next line mid-type.

    if (!InvisibleTextStyleTag.IsEmpty() && HiddenStartPos < RichText.Len())
    {
        FString HiddenPlain = StripRichTextTags(RichText.Mid(HiddenStartPos));
        if (!HiddenPlain.IsEmpty())
        {
            VisiblePart += FString::Printf(TEXT("<%s>%s</>"),
                *InvisibleTextStyleTag, *HiddenPlain);
        }
    }

    return VisiblePart;
}

FString UGF_DialogueWidgetBase::StripRichTextTags(const FString& Input)
{
    FString Result;
    Result.Reserve(Input.Len());

    bool bInTag = false;
    for (TCHAR Ch : Input)
    {
        if      (Ch == TEXT('<')) { bInTag = true;  }
        else if (Ch == TEXT('>')) { bInTag = false; }
        else if (!bInTag)         { Result.AppendChar(Ch); }
    }

    return Result;
}
