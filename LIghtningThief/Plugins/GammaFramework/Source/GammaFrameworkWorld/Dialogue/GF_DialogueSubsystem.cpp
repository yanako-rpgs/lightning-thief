#include "GF_DialogueSubsystem.h"
#include "GF_DialogueAsset.h"
#include "GF_DialogueWidgetBase.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundMix.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GF_GridMovementComponent.h"
#include "GF_CreatureBridge.h"
// Player profile is read through FGF_CreatureBridge, not the vault directly.

void UGF_DialogueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UGF_DialogueSubsystem::Deinitialize()
{
    if (bIsActive)
    {
        EndDialogue();
    }
    Super::Deinitialize();
}

// ============================================================
// CONFIGURATION
// ============================================================

void UGF_DialogueSubsystem::SetWidgetConfig(const FGF_DialogueWidgetConfig& Config)
{
    WidgetConfig = Config;
}

UGF_DialogueWidgetBase* UGF_DialogueSubsystem::GetActiveDialogueWidget() const
{
    return Cast<UGF_DialogueWidgetBase>(ActiveWidget);
}

void UGF_DialogueSubsystem::SetPendingItemIcon(UTexture2D* Icon)
{
    PendingItemIcon = Icon;
}

// ============================================================
// PRIMARY API
// ============================================================

void UGF_DialogueSubsystem::StartDialogue(UGF_DialogueAsset* Asset, AActor* Caller)
{
    if (!Asset)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_DialogueSubsystem: StartDialogue called with null asset!"));
        return;
    }

    if (Asset->Nodes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem: Dialogue asset '%s' has no nodes."), *Asset->DialogueID.ToString());
        return;
    }

    // A dialogue landing on top of a live one WAITS ITS TURN.
    //
    // Collisions here are legitimate and common: a Creature levelling up and a tamer being
    // defeated both want to speak in the same frame. Two earlier attempts at this were both
    // wrong, and both shipped:
    //
    //   Dropping the newcomer strands its caller. Callers lock player movement BEFORE calling
    //   StartDialogue and wait on OnDialogueFinished to unlock, so a swallowed dialogue freezes
    //   the player for good. One tester's log hit that nine times in a session, and he soft reset
    //   forty-two times to escape -- losing every unsaved level-up, because a battle end
    //   deliberately does not write to disk.
    //
    //   Replacing the live one destroys information the player needed. "Sprigling learned Quick
    //   Attack!" arrived alongside the tamer-defeat line, and replacing meant the move was
    //   learned in silence.
    //
    // Queueing has neither failure: every line plays, in order, and every caller receives its own
    // OnDialogueFinished. Sequential messages are what this genre does anyway.
    if (bIsActive)
    {
        // Same asset AND same caller is a DUPLICATE, not a collision -- ignore it.
        //
        // Ignoring is what caused the original softlock, so it is only safe under exactly this
        // condition: the dialogue already running was started by this same caller with this same
        // asset, so when it ends it fires OnDialogueFinished with that caller. Whoever locked
        // player movement still gets its event and still unlocks. Nothing is stranded.
        //
        // Without this, a call site that fires twice plays the line twice. BP_CreatureMaster's
        // WinBattleSecondHalf is reached from W_GF_BattleHUD and was landing twice on a tamer
        // win, so the victory line appeared as "Orion defeated Youngster..." immediately followed
        // by a second copy. Before dialogues replaced each other the duplicate was swallowed by
        // this guard and nobody noticed; making collisions visible made the double-fire visible
        // with it. This restores the old behaviour for the one case where it was correct.
        if (CurrentAsset == Asset && CurrentCaller == Caller)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("GF_DialogueSubsystem: duplicate StartDialogue for '%s' from the same caller - ignoring. ")
                TEXT("The dialogue already running will fire OnDialogueFinished for it. Something is calling ")
                TEXT("this twice; the running copy is the one the player sees."),
                *Asset->DialogueID.ToString());
            return;
        }

        // QUEUE it, do not replace it and do not drop it.
        //
        // Both of the obvious answers are wrong. Dropping the new dialogue strands its caller:
        // callers lock player movement BEFORE calling StartDialogue and wait on OnDialogueFinished
        // to unlock, so a swallowed dialogue freezes the player permanently -- one tester hit that
        // nine times in a session and soft reset forty-two times to escape. Replacing the live one
        // destroys a line the player needed: a Creature levelling up mid-victory produced
        // "Sprigling learned First Strike!" and the tamer-defeat line in the same instant, and
        // replacing meant the move was learned silently.
        //
        // Queueing loses nothing and strands nobody. Each dialogue plays in turn and each caller
        // gets its OnDialogueFinished when its own dialogue ends. Sequential messages are also
        // what this genre does anyway.
        FGF_QueuedDialogue& Queued = QueuedDialogues.AddDefaulted_GetRef();
        Queued.Asset  = Asset;
        Queued.Caller = Caller;

        // Callers set variables BEFORE StartDialogue, so DialogueVariables currently belongs to
        // THIS dialogue, not the one playing. Take a copy now: the live dialogue clears the shared
        // map when it ends, which would otherwise leave every {Token} to render raw. That shipped
        // once as "{PlayerName} defeated Lady..." instead of the player's name.
        Queued.Variables = DialogueVariables;

        UE_LOG(LogTemp, Log,
            TEXT("GF_DialogueSubsystem: '%s' arrived while '%s' was playing - queued (%d waiting)."),
            *Asset->DialogueID.ToString(),
            CurrentAsset ? *CurrentAsset->DialogueID.ToString() : TEXT("<unknown>"),
            QueuedDialogues.Num());

        return;
    }

    // Instrumentation: every successful start, with who asked for it.
    //
    // Added while chasing a prompt that was reported as "still playing" 1.7s after EndDialogue()
    // had closed it -- which can only mean something started it a second time. Guessing at the
    // caller from Blueprint exports failed repeatedly; this names it.
    UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem: START '%s' (caller: %s)"),
        *Asset->DialogueID.ToString(), *GetNameSafe(Caller));

    CurrentAsset    = Asset;
    CurrentCaller   = Caller;
    bIsActive       = true;
    bWaitingForChoice = false;
    bIsHidden       = false;

    // Reset with the rest of the state, not just in EndDialogue.
    //
    // bInputLocked gates AdvanceDialogue -- while it is true the player cannot advance
    // a line. It is cleared in EndDialogue, which is fine as long as every dialogue ends
    // cleanly. It does not: when one dialogue is started over another (a battle victory
    // line landing on top of a "learned a move!" line, say) the first never reaches
    // EndDialogue, and the flag stays true for the rest of the session. Every later
    // dialogue then shows its widget and refuses to advance -- and because callers lock
    // player movement while a dialogue runs, that reads as a total softlock with no
    // error anywhere.
    //
    // Starting a dialogue means this one is now in control, so its input state must be
    // clean regardless of how the previous one ended.
    bInputLocked    = false;

    // Every dialogue gets its own serial so a close watchdog armed by an earlier one cannot
    // mistake this for the dialogue it was meant to rescue. See ForceCloseIfStillActive().
    ++DialogueSerial;

    CurrentNodeIndex = 0;
    LastGivenCreatureName.Empty();
    PendingNextNodeOverride = -1;

    // Lock player movement
    LockPlayerMovement();

    // Show the correct widget
    ShowWidget(Asset->WidgetType);

    // Play start sound if set
    if (Asset->StartSound)
    {
        UGameplayStatics::PlaySound2D(GetWorld(), Asset->StartSound);
    }

    OnDialogueStarted.Broadcast(Asset->DialogueID);

    // Defer first node by one frame so the widget has time to construct
    // and bind to OnDialogueNodeReached before we broadcast it
    GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UGF_DialogueSubsystem::ProcessFirstNode);
}

void UGF_DialogueSubsystem::AdvanceDialogue()
{
    if (!bIsActive || bWaitingForChoice || bIsHidden || bInputLocked)
    {
        return;
    }

    if (!CurrentAsset || !CurrentAsset->Nodes.IsValidIndex(CurrentNodeIndex))
    {
        EndDialogue();
        return;
    }

    const FGF_DialogueNode& Node = CurrentAsset->Nodes[CurrentNodeIndex];

    // PendingNextNodeOverride is set by GiveCreature when party was full and a branch exists.
    // It takes priority over the node's own NextNodeIndex.
    int32 NextIndex = (PendingNextNodeOverride >= 0) ? PendingNextNodeOverride : Node.NextNodeIndex;
    PendingNextNodeOverride = -1;

    if (NextIndex < 0 || !CurrentAsset->Nodes.IsValidIndex(NextIndex))
    {
        // No valid next node — conversation over
        EndDialogue();
        return;
    }

    ProcessNode(NextIndex);
}

void UGF_DialogueSubsystem::SelectChoice(int32 ChoiceIndex)
{
    if (!bIsActive || !bWaitingForChoice)
    {
        return;
    }

    if (!CurrentAsset || !CurrentAsset->Nodes.IsValidIndex(CurrentNodeIndex))
    {
        EndDialogue();
        return;
    }

    const FGF_DialogueNode& Node = CurrentAsset->Nodes[CurrentNodeIndex];

    if (!Node.Choices.IsValidIndex(ChoiceIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem: Invalid choice index %d"), ChoiceIndex);
        return;
    }

    const FGF_DialogueChoice& Choice = Node.Choices[ChoiceIndex];

    UE_LOG(LogTemp, Log, TEXT("GE Dialogue: SelectChoice(%d) -> '%s' NextNodeIndex=%d"),
        ChoiceIndex, *Choice.ChoiceText.ToString(), Choice.NextNodeIndex);

    bWaitingForChoice = false;

    // Reset the hover silently — listeners keep whatever preview they were showing so the
    // selected option stays on screen; the choice's own event handles anything further.
    HoveredChoiceIndex = -1;

    // Fire choice event if set
    if (Choice.OnSelected.Type != EGF_DialogueEventType::None)
    {
        ExecuteEvent(Choice.OnSelected);
    }

    // Jump to choice destination
    int32 NextIndex = Choice.NextNodeIndex;

    if (NextIndex < 0 || !CurrentAsset->Nodes.IsValidIndex(NextIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("GE Dialogue: SelectChoice - NextNodeIndex %d is invalid, ending dialogue"), NextIndex);
        EndDialogue();
        return;
    }

    ProcessNode(NextIndex);
}

void UGF_DialogueSubsystem::HideDialogue()
{
    if (!bIsActive)
    {
        return;
    }

    bIsHidden = true;
    HideWidget();
}

void UGF_DialogueSubsystem::JumpToNode(int32 NodeIndex)
{
    if (!bIsActive || !CurrentAsset || !CurrentAsset->Nodes.IsValidIndex(NodeIndex))
        return;

    UE_LOG(LogTemp, Log, TEXT("GE Dialogue: JumpToNode(%d) - was at node %d ('%s')"),
        NodeIndex, CurrentNodeIndex,
        CurrentAsset->Nodes.IsValidIndex(CurrentNodeIndex)
            ? *StaticEnum<EGF_DialogueNodeType>()->GetNameStringByValue((int64)CurrentAsset->Nodes[CurrentNodeIndex].Type)
            : TEXT("?"));

    // Clear any pending timers so the previous node doesn't interfere
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(HoldTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(AutoAdvanceTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(AutoEndTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(AutoResumeTimerHandle);
    }

    bInputLocked      = false;
    bWaitingForChoice = false;
    bIsHidden         = false;

    ProcessNode(NodeIndex);
}

void UGF_DialogueSubsystem::JumpToLabel(FName LabelName)
{
    if (!bIsActive || !CurrentAsset) return;

    const int32* NodeIndex = CurrentAsset->LabelMap.Find(LabelName);
    if (!NodeIndex)
    {
        UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem: JumpToLabel - label '%s' not found in '%s'"),
            *LabelName.ToString(), *CurrentAsset->DialogueID.ToString());
        return;
    }
    JumpToNode(*NodeIndex);
}

void UGF_DialogueSubsystem::ResumeDialogue()
{
    if (!bIsActive || !bIsHidden)
    {
        return;
    }

    bIsHidden = false;

    if (CurrentAsset)
    {
        ShowWidget(CurrentAsset->WidgetType);
    }

    // Advance past the Hide node
    AdvanceDialogue();
}

void UGF_DialogueSubsystem::EndDialogue()
{
    if (!bIsActive)
    {
        return;
    }

    // Cancel any pending timers
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(AutoResumeTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(HoldTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(AutoEndTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(AutoAdvanceTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(CloseWatchdogHandle);
    }
    bInputLocked = false;

    // Only broadcasts if a choice was still highlighted — i.e. the dialogue was aborted while
    // the buttons were up. A normal SelectChoice() clears the index silently first.
    ClearChoiceHover();

    // Restore the OST in case dialogue ended (or was skipped) before an [unduck]
    PopActiveSoundMixes();

    FName FinishedID    = CurrentAsset  ? CurrentAsset->DialogueID : NAME_None;
    AActor* FinishedBy  = CurrentCaller;   // capture before clearing

    HideWidget();
    UnlockPlayerMovement();

    // Drop the widget reference so the NEXT StartDialogue builds a FRESH widget
    // instead of reusing this one. Reuse is unsafe here: a dialogue that closed via
    // its CloseAnim leaves the widget parked on the animation's end keyframe
    // (collapsed / faded out). Reusing that instance for the next conversation makes
    // the new box invisible and unresponsive. HideDialogue()/[hide] do NOT call
    // EndDialogue(), so ResumeDialogue() still reuses the widget correctly.
    ActiveWidget = nullptr;

    bIsActive         = false;
    bWaitingForChoice = false;
    bIsHidden         = false;
    CurrentNodeIndex  = -1;
    CurrentAsset      = nullptr;
    CurrentCaller     = nullptr;
    PendingItemIcon   = nullptr;
    DialogueVariables.Empty();

    UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem: END   '%s'"), *FinishedID.ToString());

    // Broadcast AFTER cleanup so listeners can safely start new dialogue.
    // FinishedBy lets each NPC check (Caller == Self) without racing against CurrentCaller being cleared.
    OnDialogueFinished.Broadcast(FinishedID, FinishedBy);

    // Then hand the floor to whatever was waiting. After the broadcast on purpose: a listener may
    // legitimately start a dialogue from its OnDialogueFinished handler, and that one should take
    // its place in line rather than being overtaken by the queue.
    StartNextQueuedDialogue();
}

int32 UGF_DialogueSubsystem::ClearQueuedDialogues()
{
    if (QueuedDialogues.IsEmpty())
    {
        return 0;
    }

    // Take a copy and empty first: a listener may legitimately start a new dialogue from its
    // OnDialogueFinished handler, and that one must not be swept away by this same call.
    TArray<FGF_QueuedDialogue> Dropped = MoveTemp(QueuedDialogues);
    QueuedDialogues.Reset();

    UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem: discarding %d queued dialogue(s) - superseded."),
        Dropped.Num());

    for (const FGF_QueuedDialogue& Entry : Dropped)
    {
        const FName DroppedID = IsValid(Entry.Asset) ? Entry.Asset->DialogueID : NAME_None;

        UE_LOG(LogTemp, Warning, TEXT("   dropped '%s'"), *DroppedID.ToString());

        // Tell its caller it is over anyway. Whoever queued this locked player movement and is
        // waiting on OnDialogueFinished to unlock; dropping it silently is exactly the softlock
        // the queue was added to prevent.
        OnDialogueFinished.Broadcast(DroppedID, Entry.Caller);
    }

    return Dropped.Num();
}

void UGF_DialogueSubsystem::StartNextQueuedDialogue()
{
    if (bIsDrainingQueue)
    {
        return;
    }

    // Loop rather than recurse, and hold the guard for the whole drain.
    //
    // StartDialogue() below can come straight back here through EndDialogue() when a queued
    // dialogue finishes on the spot, and it can also bail before setting bIsActive (a null or
    // empty asset). The guard turns that re-entry into a no-op and the loop condition picks the
    // work back up, so neither case can strand the rest of the queue or recurse per entry.
    TGuardValue<bool> DrainGuard(bIsDrainingQueue, true);

    while (!bIsActive && !QueuedDialogues.IsEmpty())
    {
        FGF_QueuedDialogue Next = QueuedDialogues[0];
        QueuedDialogues.RemoveAt(0);

        if (!IsValid(Next.Asset))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("GF_DialogueSubsystem: a queued dialogue's asset went away before its turn - skipping it."));
            continue;
        }

        // Restore the variables this dialogue was queued with. They were captured when it was
        // queued because the dialogue ahead of it clears the shared map on the way out.
        DialogueVariables = Next.Variables;

        StartDialogue(Next.Asset, Next.Caller);
    }
}

void UGF_DialogueSubsystem::ResetDialogueState()
{
    // Clear all timers safely — world may be mid-teardown so guard the call
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AutoResumeTimerHandle);
        World->GetTimerManager().ClearTimer(HoldTimerHandle);
        World->GetTimerManager().ClearTimer(AutoEndTimerHandle);
        World->GetTimerManager().ClearTimer(AutoAdvanceTimerHandle);
        World->GetTimerManager().ClearTimer(CloseWatchdogHandle);
    }

    // Anything still waiting belongs to the run being torn down. Carrying it into the next one
    // would open the new game with a line from the old one.
    //
    // Discard it the SAME way ClearQueuedDialogues() does -- announcing each drop -- rather than
    // emptying the array. Whoever queued a dialogue locked player movement and is waiting on
    // OnDialogueFinished to unlock; dropping it in silence leaves them locked forever. That
    // shipped: a Creature learned a move at the end of a battle, the post-battle cutscene line was
    // queued behind it, the battle tore down, the queue was emptied without a word, and the player
    // arrived in the overworld unable to move.
    ClearQueuedDialogues();
    bIsDrainingQueue = false;

    // If dialogue was active when the reset was triggered (e.g. level teardown mid-conversation),
    // make sure movement is unlocked before we clear bIsActive — otherwise EndDialogue()'s
    // early-out guard fires first and the player is left permanently locked.
    if (bIsActive)
    {
        UnlockPlayerMovement();
    }

    // Restore the OST so a mid-conversation teardown never leaves it muffled
    PopActiveSoundMixes();

    // Wipe all state — do NOT touch ActiveWidget (already destroyed by level teardown)
    // and do NOT broadcast OnDialogueFinished (this is a reset, not a natural end)
    bIsActive         = false;
    bIsHidden         = false;
    bWaitingForChoice = false;
    bInputLocked      = false;
    CurrentNodeIndex  = -1;
    CurrentAsset      = nullptr;
    CurrentCaller     = nullptr;
    ActiveWidget      = nullptr;
    PendingItemIcon   = nullptr;
    PendingNextNodeOverride = -1;
    DialogueVariables.Empty();
}

// ============================================================
// STATE QUERIES
// ============================================================

void UGF_DialogueSubsystem::SetDialogueVariable(FName Key, const FString& Value)
{
    DialogueVariables.Add(Key, Value);
}

void UGF_DialogueSubsystem::ClearDialogueVariables()
{
    DialogueVariables.Empty();
}

void UGF_DialogueSubsystem::SetNodeText(int32 NodeIndex, FText NewText)
{
    if (CurrentAsset && CurrentAsset->Nodes.IsValidIndex(NodeIndex))
    {
        CurrentAsset->Nodes[NodeIndex].DialogueText = NewText;
    }
}

bool UGF_DialogueSubsystem::IsLastNode() const
{
    if (!CurrentAsset || !CurrentAsset->Nodes.IsValidIndex(CurrentNodeIndex))
        return true;

    const FGF_DialogueNode& Node = CurrentAsset->Nodes[CurrentNodeIndex];
    const int32 NextIndex = (PendingNextNodeOverride >= 0) ? PendingNextNodeOverride : Node.NextNodeIndex;
    return NextIndex < 0 || !CurrentAsset->Nodes.IsValidIndex(NextIndex);
}

FName UGF_DialogueSubsystem::GetCurrentDialogueID() const
{
    return CurrentAsset ? CurrentAsset->DialogueID : NAME_None;
}

FString UGF_DialogueSubsystem::SubstituteVariables(const FString& Input) const
{
    FString Result = Input;

    if (!LastGivenCreatureName.IsEmpty())
    {
        Result.ReplaceInline(TEXT("{Creature}"), *LastGivenCreatureName);
    }

    for (const TPair<FName, FString>& Var : DialogueVariables)
    {
        FString Token = FString::Printf(TEXT("{%s}"), *Var.Key.ToString());
        Result.ReplaceInline(*Token, *Var.Value);
    }

    return Result;
}

FString UGF_DialogueSubsystem::GetDisplayText(const FGF_DialogueNode& Node) const
{
    FString RawText = SubstituteVariables(Node.DialogueText.ToString());

    // Already has rich text markup — pass through as-is
    if (RawText.Contains(TEXT("<")))
    {
        return RawText;
    }

    // Ensnare with default style tag if one is configured
    if (!WidgetConfig.DefaultTextStyleTag.IsEmpty())
    {
        return FString::Printf(TEXT("<%s>%s</>"), *WidgetConfig.DefaultTextStyleTag, *RawText);
    }

    return RawText;
}

FString UGF_DialogueSubsystem::GetChoiceDisplayText(const FGF_DialogueChoice& Choice) const
{
    // Choice labels support the same {Token} substitution as message text
    FString Raw = SubstituteVariables(Choice.ChoiceText.ToString());
    const FString& Tag = WidgetConfig.ChoiceTextStyleTag.IsEmpty()
        ? WidgetConfig.DefaultTextStyleTag
        : WidgetConfig.ChoiceTextStyleTag;

    if (Tag.IsEmpty()) return Raw;
    return FString::Printf(TEXT("<%s>%s</>"), *Tag, *Raw);
}

int32 UGF_DialogueSubsystem::GetPlainTextLength(const FGF_DialogueNode& Node) const
{
    // Strip markup tags so the typewriter counts visible characters only.
    // This handles both plain text and pre-marked-up text authored with <Wave> etc.
    FString Raw = Node.DialogueText.ToString();
    FString Result;
    Result.Reserve(Raw.Len());

    bool bInTag = false;
    for (TCHAR Ch : Raw)
    {
        if (Ch == TEXT('<'))      { bInTag = true;  continue; }
        if (Ch == TEXT('>'))      { bInTag = false; continue; }
        if (!bInTag)              { Result.AppendChar(Ch); }
    }
    return Result.Len();
}

FString UGF_DialogueSubsystem::GetPartialDisplayText(const FGF_DialogueNode& Node, int32 NumChars) const
{
    // Build a safe partial reveal: strip any inline markup, take N plain chars,
    // then re-wrap in the default style tag. The animated inline effects (Wave, Shake)
    // will appear once typing completes and GetDisplayText() is applied.
    FString Raw = Node.DialogueText.ToString();

    // Substitute {Creature} before stripping tags so the revealed count is correct
    if (!LastGivenCreatureName.IsEmpty())
    {
        Raw.ReplaceInline(TEXT("{Creature}"), *LastGivenCreatureName);
    }

    // Strip tags to get plain text
    FString PlainText;
    PlainText.Reserve(Raw.Len());
    bool bInTag = false;
    for (TCHAR Ch : Raw)
    {
        if (Ch == TEXT('<'))      { bInTag = true;  continue; }
        if (Ch == TEXT('>'))      { bInTag = false; continue; }
        if (!bInTag)              { PlainText.AppendChar(Ch); }
    }

    FString Partial = PlainText.Left(FMath::Clamp(NumChars, 0, PlainText.Len()));

    if (!WidgetConfig.DefaultTextStyleTag.IsEmpty())
    {
        return FString::Printf(TEXT("<%s>%s</>"), *WidgetConfig.DefaultTextStyleTag, *Partial);
    }
    return Partial;
}

// ============================================================
// NODE PROCESSING
// ============================================================

void UGF_DialogueSubsystem::ProcessFirstNode()
{
    // Apply any queued item icon now — the widget is fully constructed and visible
    if (PendingItemIcon)
    {
        if (UGF_DialogueWidgetBase* Widget = GetActiveDialogueWidget())
        {
            Widget->SetItemIcon(PendingItemIcon);
        }
        PendingItemIcon = nullptr;
    }

    ProcessNode(0);
}

void UGF_DialogueSubsystem::ProcessNode(int32 NodeIndex)
{
    if (!CurrentAsset || !CurrentAsset->Nodes.IsValidIndex(NodeIndex))
    {
        EndDialogue();
        return;
    }

    CurrentNodeIndex = NodeIndex;
    const FGF_DialogueNode& Node = CurrentAsset->Nodes[NodeIndex];

    OnDialogueNodeReached.Broadcast(CurrentAsset->DialogueID, NodeIndex);

    switch (Node.Type)
    {
        case EGF_DialogueNodeType::Message:
            ProcessMessageNode(Node);
            break;

        case EGF_DialogueNodeType::Choice:
            ProcessChoiceNode(Node);
            break;

        case EGF_DialogueNodeType::Event:
            ProcessEventNode(Node);
            break;

        case EGF_DialogueNodeType::Hide:
            ProcessHideNode(Node);
            break;
    }
}

void UGF_DialogueSubsystem::ProcessMessageNode(const FGF_DialogueNode& Node)
{
    // The widget listens to OnDialogueNodeReached and reads the current node's text.
    // Player presses confirm → AdvanceDialogue() is called by the widget.

    if (Node.LineSound)
    {
        UGameplayStatics::PlaySound2D(GetWorld(), Node.LineSound);
    }

    // [duck] — push a sound mix to muffle/duck the OST while this line is up
    if (Node.PushSoundMix)
    {
        UGameplayStatics::PushSoundMixModifier(GetWorld(), Node.PushSoundMix);
        ActiveSoundMixes.AddUnique(Node.PushSoundMix);
    }

    // [unduck] — restore the OST by popping everything we pushed
    if (Node.bPopSoundMixes)
    {
        PopActiveSoundMixes();
    }

    if (Node.MinDisplaySeconds > 0.f && GetWorld())
    {
        bInputLocked = true;
        GetWorld()->GetTimerManager().SetTimer(
            HoldTimerHandle,
            this,
            &UGF_DialogueSubsystem::UnlockInput,
            Node.MinDisplaySeconds,
            false
        );
    }

    if (Node.AutoEndSeconds > 0.f && GetWorld())
    {
        // Lock input for the full duration so player can't advance manually
        bInputLocked = true;
        GetWorld()->GetTimerManager().SetTimer(
            AutoEndTimerHandle,
            this,
            &UGF_DialogueSubsystem::BeginCloseDialogue,
            Node.AutoEndSeconds,
            false
        );
    }

    if (Node.AutoAdvanceSeconds > 0.f && GetWorld())
    {
        // Lock input so the player can't skip — auto-advance fires after the delay
        bInputLocked = true;
        GetWorld()->GetTimerManager().SetTimer(
            AutoAdvanceTimerHandle,
            this,
            &UGF_DialogueSubsystem::AutoAdvanceDialogue,
            Node.AutoAdvanceSeconds,
            false
        );
    }
}

void UGF_DialogueSubsystem::BeginCloseDialogue()
{
    if (!bIsActive) return;

    bInputLocked = true;

    // Broadcast so widgets can play a close animation if needed.
    // The widget must call EndDialogue() itself if it needs time to animate out.
    // If nothing is bound, end immediately.
    if (OnDialogueClosing.IsBound())
    {
        OnDialogueClosing.Broadcast();

        // A widget that binds OnDialogueClosing owns calling EndDialogue() once its outro
        // animation finishes (DIALOGUE_SYNTAX.md). If it never does -- its graph dead-ends,
        // its animation is interrupted, or the binding outlived the widget that made it --
        // then nothing else will, because the branch above deliberately skipped our own
        // EndDialogue(). bInputLocked stays true so the player cannot advance, bIsActive
        // stays true so no new dialogue can start, and OnDialogueFinished never broadcasts,
        // so every caller that locked player movement for this dialogue never unlocks it.
        // That is a silent, total softlock with nothing in the log -- it shipped once as
        // "talk to your follower and lose all control", because W_GF_OW_DIalogue's closing
        // event played its animation and stopped. Only AutoEndSeconds nodes reach here, so
        // it hid behind auto-closing barks while every player-advanced line worked fine.
        //
        // The widget is never the only way out again.
        if (UWorld* World = GetWorld())
        {
            CloseWatchdogSerial = DialogueSerial;

            World->GetTimerManager().SetTimer(
                CloseWatchdogHandle,
                this,
                &UGF_DialogueSubsystem::ForceCloseIfStillActive,
                CloseWatchdogSeconds,
                false
            );
        }
    }
    else
    {
        EndDialogue();
    }
}

void UGF_DialogueSubsystem::ForceCloseIfStillActive()
{
    if (!bIsActive)
    {
        return;   // the widget did its job - nothing to rescue
    }

    // Only rescue the dialogue this watchdog was armed for.
    //
    // A pending timer outlives the dialogue that set it. Without this check the watchdog closes
    // whatever happens to be on screen when it fires, and that shipped: a watchdog armed while
    // one line was closing force-ended the Fernhollow Woods post-battle dialogue 0.24 seconds
    // after it opened, and the cutscene chain that depended on it never ran. The player was left
    // standing in the woods with nothing to do but reset.
    if (DialogueSerial != CloseWatchdogSerial)
    {
        return;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("GF_DialogueSubsystem: '%s' never closed in the %.1fs after OnDialogueClosing - forcing EndDialogue(). ")
        TEXT("The bound dialogue widget did not call EndDialogue(); see DIALOGUE_SYNTAX.md. ")
        TEXT("Player control is being restored, but fix the widget: this cost the player a closing animation."),
        CurrentAsset ? *CurrentAsset->DialogueID.ToString() : TEXT("<none>"),
        CloseWatchdogSeconds);

    EndDialogue();
}

void UGF_DialogueSubsystem::UnlockInput()
{
    bInputLocked = false;
    OnInputUnlocked.Broadcast();
}

void UGF_DialogueSubsystem::AutoAdvanceDialogue()
{
    bInputLocked = false;
    OnInputUnlocked.Broadcast();
    AdvanceDialogue();
}

void UGF_DialogueSubsystem::ProcessChoiceNode(const FGF_DialogueNode& Node)
{
    bWaitingForChoice = true;
    HoveredChoiceIndex = -1;
    OnDialogueChoicesReady.Broadcast(Node.Choices);
}

void UGF_DialogueSubsystem::NotifyChoiceHovered(int32 ChoiceIndex)
{
    if (!bIsActive || !bWaitingForChoice)
    {
        return;
    }

    if (!CurrentAsset || !CurrentAsset->Nodes.IsValidIndex(CurrentNodeIndex))
    {
        return;
    }

    const FGF_DialogueNode& Node = CurrentAsset->Nodes[CurrentNodeIndex];

    if (!Node.Choices.IsValidIndex(ChoiceIndex))
    {
        // Treat an out-of-range index as "nothing hovered" rather than warning — mouse-leave
        // handlers commonly pass -1.
        ClearChoiceHover();
        return;
    }

    // Hover events fire repeatedly (mouse move, navigation re-assert) — only react to changes.
    if (ChoiceIndex == HoveredChoiceIndex)
    {
        return;
    }

    HoveredChoiceIndex = ChoiceIndex;
    OnDialogueChoiceHovered.Broadcast(ChoiceIndex, Node.Choices[ChoiceIndex]);
}

void UGF_DialogueSubsystem::ClearChoiceHover()
{
    if (HoveredChoiceIndex == -1)
    {
        return;
    }

    HoveredChoiceIndex = -1;
    OnDialogueChoiceHovered.Broadcast(-1, FGF_DialogueChoice());
}

void UGF_DialogueSubsystem::ProcessEventNode(const FGF_DialogueNode& Node)
{
    // BranchGender: silently jump to the correct label based on saved PlayerGender.
    // Handled entirely in C++ — no Blueprint wiring needed per dialogue.
    if (Node.Event.Type == EGF_DialogueEventType::BranchGender)
    {
        EGF_PlayerGender Gender = EGF_PlayerGender::Boy;
        if (UGameInstance* GI = GetGameInstance())
        {
            {
                {
                    Gender = static_cast<EGF_PlayerGender>(FGF_CreatureBridge::PlayerGender(this));
                }
            }
        }

        FName TargetLabel = (Gender == EGF_PlayerGender::Boy)
            ? Node.Event.EventName
            : Node.Event.BranchElseLabel;

        JumpToLabel(TargetLabel);
        return;
    }

    // Snapshot our identity and the values we'll need BEFORE firing the event.
    // ExecuteEvent broadcasts OnDialogueEvent synchronously, and a listener can
    // start a *different* dialogue from inside that broadcast (e.g. a battle
    // "final message" cutscene that calls EndDialogue + StartDialogue). When that
    // happens CurrentAsset/CurrentNodeIndex change and the `Node` reference dangles,
    // so we must read what we need now and detect the swap afterward instead of
    // driving this (now-stale) node's continuation into the brand-new dialogue.
    UGF_DialogueAsset* const AssetBefore     = CurrentAsset;
    const int32              NodeIndexBefore = CurrentNodeIndex;
    const float              Delay           = Node.Event.ResumeDelay;
    const int32              NodeNextIndex   = Node.NextNodeIndex;

    ExecuteEvent(Node.Event);

    if (!bIsActive) return;

    // Re-entrancy guard: the event broadcast started/replaced the dialogue. The
    // new dialogue now owns the flow — do NOT apply this old node's continuation
    // to it, or we'll end or misroute the dialogue that was just started.
    if (CurrentAsset != AssetBefore || CurrentNodeIndex != NodeIndexBefore)
    {
        return;
    }

    if (Delay < 0.f)
    {
        // Manual resume — caller must call ResumeDialogue() explicitly
        bIsHidden = true;
        return;
    }

    if (Delay > 0.f && GetWorld())
    {
        // Auto-resume after the specified delay — no Blueprint wiring needed
        bIsHidden = true;
        GetWorld()->GetTimerManager().SetTimer(
            AutoResumeTimerHandle,
            this,
            &UGF_DialogueSubsystem::ResumeDialogue,
            Delay,
            false
        );
        return;
    }

    // Delay == 0: advance immediately.
    // PendingNextNodeOverride is set by GiveCreature when party is full and a branch exists.
    int32 NextIndex = (PendingNextNodeOverride >= 0) ? PendingNextNodeOverride : NodeNextIndex;
    PendingNextNodeOverride = -1;

    if (NextIndex >= 0 && CurrentAsset->Nodes.IsValidIndex(NextIndex))
    {
        ProcessNode(NextIndex);
    }
    else
    {
        EndDialogue();
    }
}

void UGF_DialogueSubsystem::ProcessHideNode(const FGF_DialogueNode& Node)
{
    HideDialogue();

    if (Node.ResumeAfterSeconds > 0.f && GetWorld())
    {
        // Auto-resume after the specified delay
        GetWorld()->GetTimerManager().SetTimer(
            AutoResumeTimerHandle,
            this,
            &UGF_DialogueSubsystem::ResumeDialogue,
            Node.ResumeAfterSeconds,
            false
        );
    }
    // If ResumeAfterSeconds == 0, waits for manual ResumeDialogue() call
}

// ============================================================
// EVENT EXECUTION
// ============================================================

void UGF_DialogueSubsystem::ExecuteEvent(const FGF_DialogueEvent& Event)
{
    // None-type events are silent passthrough nodes (e.g. parser-generated
    // anchors for bare @label + [jump]/[end]) — never surface them to Blueprint.
    if (Event.Type == EGF_DialogueEventType::None)
    {
        return;
    }

    // Always broadcast so Blueprint can handle anything
    if (CurrentAsset)
    {
        OnDialogueEvent.Broadcast(CurrentAsset->DialogueID, Event);
    }

    // Handle built-in types automatically
    switch (Event.Type)
    {
        case EGF_DialogueEventType::None:
            break;

        case EGF_DialogueEventType::GiveItem:
        {
            // Hook into your item subsystem here when ready
            UE_LOG(LogTemp, Log, TEXT("Dialogue: Give item '%s' x%d"), *Event.ItemID.ToString(), Event.ItemQuantity);
            break;
        }

        case EGF_DialogueEventType::SetQuestFlag:
        {
            // Hook into your quest subsystem here when ready
            UE_LOG(LogTemp, Log, TEXT("Dialogue: Set quest flag '%s' = %s"),
                *Event.QuestFlagName.ToString(), Event.bQuestFlagValue ? TEXT("true") : TEXT("false"));
            break;
        }

        case EGF_DialogueEventType::ContinueFlow:
        {
            // Fire the named event on the caller actor
            if (CurrentCaller && !Event.EventName.IsNone())
            {
                FireNamedEventOnActor(CurrentCaller, Event.EventName);
            }
            break;
        }

        case EGF_DialogueEventType::GiveCreature:
        {
                        if (!FGF_CreatureBridge::GiveCreature)
            {
                UE_LOG(LogTemp, Error, TEXT("Dialogue GiveCreature: creature layer not bound"));
                break;
            }

            // Store name for {Creature} substitution in subsequent message nodes
            LastGivenCreatureName = Event.CreatureSpeciesName.ToString();

            // OutVaultPageIndex: -1 = added to party, >=0 = sent to that box.
            // Returns false only when party AND all boxes are full.
            int32 VaultPageIndex = -1;
            const bool bGiven = FGF_CreatureBridge::GiveCreature(
                this, Event.CreatureSpeciesName, Event.GiftCreatureLevel, VaultPageIndex);
            const bool bAddedToParty = bGiven && VaultPageIndex < 0;

            // Sent to a box (or not given at all): take the full-> branch if one exists
            if (!bAddedToParty && Event.PartyFullNodeIndex >= 0)
            {
                PendingNextNodeOverride = Event.PartyFullNodeIndex;
            }

            // Expose the destination box's name as {Vault} for subsequent message nodes
            if (VaultPageIndex >= 0 && FGF_CreatureBridge::GetVaultPageName)
            {
                DialogueVariables.Add(FName("Vault"),
                    FGF_CreatureBridge::GetVaultPageName(this, VaultPageIndex));
            }

            UE_LOG(LogTemp, Log, TEXT("Dialogue: Gave %s Lv.%d - %s"),
                *Event.CreatureSpeciesName.ToString(),
                Event.GiftCreatureLevel,
                bAddedToParty ? TEXT("added to party")
                              : (bGiven ? TEXT("sent to box") : TEXT("FAILED - party and boxes full")));
            break;
        }

        case EGF_DialogueEventType::CustomEvent:
        {
            // Broadcast via OnDialogueEvent — Blueprint handles this
            UE_LOG(LogTemp, Log, TEXT("Dialogue: Custom event '%s'"), *Event.EventName.ToString());
            break;
        }

        case EGF_DialogueEventType::StartBattle:
        case EGF_DialogueEventType::OpenShop:
        case EGF_DialogueEventType::WarpPlayer:
        case EGF_DialogueEventType::PlayAnimation:
        {
            // These are all handled by the OnDialogueEvent broadcast above
            // Blueprint binds to that delegate and executes the appropriate logic
            break;
        }
    }
}

// ============================================================
// WIDGET MANAGEMENT
// ============================================================

void UGF_DialogueSubsystem::ShowWidget(EGF_DialogueWidgetType WidgetType)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_DialogueSubsystem::ShowWidget - No World!"));
        return;
    }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_DialogueSubsystem::ShowWidget - No PlayerController!"));
        return;
    }

    // Get the right class for this widget type
    TSubclassOf<UUserWidget> WidgetClass = nullptr;
    switch (WidgetType)
    {
        case EGF_DialogueWidgetType::Overworld:   WidgetClass = WidgetConfig.OverworldWidgetClass;  break;
        case EGF_DialogueWidgetType::Cinematic:   WidgetClass = WidgetConfig.CinematicWidgetClass;  break;
        case EGF_DialogueWidgetType::Battle:      WidgetClass = WidgetConfig.BattleWidgetClass;     break;
    }

    if (!WidgetClass)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_DialogueSubsystem::ShowWidget - WidgetClass is NULL for type %d. Did you call SetWidgetConfig?"), (int32)WidgetType);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem::ShowWidget - Creating widget: %s"), *WidgetClass->GetName());

    // Reuse existing widget if it's the same type, otherwise create a new one
    if (!ActiveWidget || ActiveWidget->GetClass() != WidgetClass)
    {
        if (ActiveWidget)
        {
            ActiveWidget->RemoveFromParent();
        }
        ActiveWidget = CreateWidget<UUserWidget>(PC, WidgetClass);

        if (!ActiveWidget)
        {
            UE_LOG(LogTemp, Error, TEXT("GF_DialogueSubsystem::ShowWidget - CreateWidget returned null!"));
            return;
        }
    }

    if (!ActiveWidget->IsInViewport())
    {
        ActiveWidget->AddToViewport(999999);
        UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem::ShowWidget - Widget added to viewport."));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem::ShowWidget - Widget already in viewport."));
    }
}

void UGF_DialogueSubsystem::HideWidget()
{
    if (ActiveWidget && ActiveWidget->IsInViewport())
    {
        ActiveWidget->RemoveFromParent();
    }
}

// ============================================================
// MOVEMENT LOCK
// ============================================================

void UGF_DialogueSubsystem::LockPlayerMovement()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC)
    {
        return;
    }

    APawn* Pawn = PC->GetPawn();
    if (!Pawn)
    {
        return;
    }

    UGF_GridMovementComponent* Grid = Pawn->FindComponentByClass<UGF_GridMovementComponent>();
    if (Grid)
    {
        Grid->LockMovement();
    }
}

void UGF_DialogueSubsystem::UnlockPlayerMovement()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC)
    {
        return;
    }

    APawn* Pawn = PC->GetPawn();
    if (!Pawn)
    {
        return;
    }

    UGF_GridMovementComponent* Grid = Pawn->FindComponentByClass<UGF_GridMovementComponent>();
    if (Grid)
    {
        // UnlockMovement() silently bails if bIsFollowingNPC is true (to protect follow
        // cutscenes from being interrupted by dialogue). But if we're here, dialogue is
        // ending — if movement is still Locked after the call it means bIsFollowingNPC is
        // stale (follow cutscene ended without calling StopFollowingNPC). Force idle so the
        // player is never permanently stuck.
        Grid->UnlockMovement();

        if (Grid->MovementState == EGF_GridMovementState::Locked)
        {
            // If the player is intentionally locked for an active follow cutscene,
            // leave it alone — the cutscene will unlock when it finishes.
            if (Grid->IsFollowingNPC())
                return;

            // Otherwise bIsFollowingNPC is stale (follow ended without StopFollowingNPC).
            // Force idle so the player is never permanently stuck.
            UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem: UnlockMovement was blocked (bIsFollowingNPC stale?). Force-unlocking."));
            Grid->MovementState = EGF_GridMovementState::Idle;
        }
    }
}

void UGF_DialogueSubsystem::PopActiveSoundMixes()
{
    if (ActiveSoundMixes.Num() == 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    for (USoundMix* Mix : ActiveSoundMixes)
    {
        if (Mix && World)
        {
            UGameplayStatics::PopSoundMixModifier(World, Mix);
        }
    }
    ActiveSoundMixes.Empty();
}

// ============================================================
// NAMED EVENT HELPER
// ============================================================

void UGF_DialogueSubsystem::FireNamedEventOnActor(AActor* Target, FName EventName)
{
    if (!Target || EventName.IsNone())
    {
        return;
    }

    UFunction* Func = Target->FindFunction(EventName);
    if (Func)
    {
        Target->ProcessEvent(Func, nullptr);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GF_DialogueSubsystem: Could not find function '%s' on actor '%s'"),
            *EventName.ToString(), *Target->GetName());
    }
}
