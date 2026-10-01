#include "GF_DialogueParser.h"
#include "GF_DialogueAsset.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundMix.h"

// ============================================================
// MULTI-SECTION SPLIT
// ============================================================

TArray<FGF_DialogueSection> FGF_DialogueParser::SplitIntoSections(const FString& FileText)
{
    TArray<FGF_DialogueSection> Sections;

    TArray<FString> Lines;
    FileText.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

    FGF_DialogueSection Current;
    bool bInSection = false;

    for (const FString& RawLine : Lines)
    {
        FString Line = Trim(RawLine);

        // @dialogue NAME starts a new section
        if (Line.StartsWith(TEXT("@dialogue"), ESearchCase::IgnoreCase))
        {
            // Save previous section if any
            if (bInSection && !Current.DialogueID.IsEmpty())
            {
                Sections.Add(Current);
            }

            // Start new section
            Current = FGF_DialogueSection();
            FString Rest = Trim(Line.Mid(9)); // skip "@dialogue"
            Current.DialogueID = Rest;

            // Inject @id so the single-section parser sets DialogueID correctly
            Current.SectionText = FString::Printf(TEXT("@id %s\n"), *Rest);
            bInSection = true;
            continue;
        }

        if (bInSection)
        {
            Current.SectionText += RawLine;
            Current.SectionText += TEXT("\n");
        }
    }

    // Save the last section
    if (bInSection && !Current.DialogueID.IsEmpty())
    {
        Sections.Add(Current);
    }

    return Sections;
}

// ============================================================
// PUBLIC ENTRY POINT
// ============================================================

bool FGF_DialogueParser::ParseIntoAsset(const FString& FileText, UGF_DialogueAsset* Asset)
{
    if (!Asset)
    {
        UE_LOG(LogTemp, Error, TEXT("GF_DialogueParser: Asset is null"));
        return false;
    }

    Asset->Nodes.Empty();

    TArray<FString> Lines;
    FileText.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

    TArray<FGF_IntermediateNode> IntermNodes;
    TMap<FString, int32> LabelMap;   // label name -> node index

    FString CurrentSpeaker;
    bool bInChoiceBlock = false;

    for (int32 i = 0; i < Lines.Num(); ++i)
    {
        FString Line = Trim(Lines[i]);

        // Skip blank lines and comments
        if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
        {
            continue;
        }

        if (!ParseLine(Line, i + 1, IntermNodes, LabelMap, CurrentSpeaker, bInChoiceBlock, Asset))
        {
            // Non-fatal: keep parsing to surface more errors
        }
    }

    // ──────────────────────────────────────
    // Pass 2: wire up NextNodeIndex values
    // ──────────────────────────────────────
    ResolveLabels(IntermNodes, LabelMap);

    // Store the label map in the asset for runtime use (e.g. JumpToLabel, BranchGender)
    Asset->LabelMap.Empty();
    for (const auto& Pair : LabelMap)
    {
        Asset->LabelMap.Add(FName(*Pair.Key), Pair.Value);
    }

    // Skill finished nodes into the asset
    Asset->Nodes.Reserve(IntermNodes.Num());
    for (FGF_IntermediateNode& INode : IntermNodes)
    {
        Asset->Nodes.Add(INode.Node);
    }

    UE_LOG(LogTemp, Log, TEXT("GF_DialogueParser: Parsed %d nodes into '%s'"),
        Asset->Nodes.Num(), *Asset->DialogueID.ToString());

    return true;
}

// ============================================================
// LINE PARSER
// ============================================================

// True when some @label points at the node index that will be created next —
// i.e. a bare @label line is still waiting for its node. [jump]/[end] must then
// create a passthrough node instead of modifying the previous one, or the label
// would dangle past the end of the node array.
static bool LabelAwaitsNode(const TMap<FString, int32>& LabelMap, int32 UpcomingIndex)
{
    for (const TPair<FString, int32>& Pair : LabelMap)
    {
        if (Pair.Value == UpcomingIndex)
        {
            return true;
        }
    }
    return false;
}

bool FGF_DialogueParser::ParseLine(const FString& Line, int32 LineNumber,
                                    TArray<FGF_IntermediateNode>& OutNodes,
                                    TMap<FString, int32>& LabelMap,
                                    FString& CurrentSpeaker,
                                    bool& bInChoiceBlock,
                                    UGF_DialogueAsset* Asset)
{
    // ── Header directives ──────────────────────────────────────
    if (Line.StartsWith(TEXT("@")))
    {
        FString Rest = Trim(Line.Mid(1));
        FString Keyword, Value;
        if (!Rest.Split(TEXT(" "), &Keyword, &Value, ESearchCase::IgnoreCase))
        {
            // Keyword-only directives like @label need special handling below
            Keyword = Rest;
            Value.Empty();
        }
        Value = Trim(Value);

        if (Keyword.Equals(TEXT("id"), ESearchCase::IgnoreCase))
        {
            Asset->DialogueID = FName(*Value);
        }
        else if (Keyword.Equals(TEXT("widget"), ESearchCase::IgnoreCase))
        {
            if (Value.Equals(TEXT("Cinematic"), ESearchCase::IgnoreCase))
                Asset->WidgetType = EGF_DialogueWidgetType::Cinematic;
            else if (Value.Equals(TEXT("Battle"), ESearchCase::IgnoreCase))
                Asset->WidgetType = EGF_DialogueWidgetType::Battle;
            else
                Asset->WidgetType = EGF_DialogueWidgetType::Overworld;
        }
        else if (Keyword.Equals(TEXT("label"), ESearchCase::IgnoreCase))
        {
            // Record label → next node index (the node hasn't been created yet)
            LabelMap.Add(Value, OutNodes.Num());
            bInChoiceBlock = false;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("GF_DialogueParser line %d: Unknown directive '@%s'"), LineNumber, *Keyword);
        }
        return true;
    }

    // ── Event commands ─────────────────────────────────────────
    if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
    {
        FString Body = Trim(Line.Mid(1, Line.Len() - 2));

        // [end] terminates the current sequence: add an implicit end marker
        if (Body.Equals(TEXT("end"), ESearchCase::IgnoreCase))
        {
            if (LabelAwaitsNode(LabelMap, OutNodes.Num()))
            {
                // A bare @label points at this [end] — give it a real node to land on.
                FGF_IntermediateNode EndNode;
                EndNode.Node.Type = EGF_DialogueNodeType::Event;   // Event/None = silent no-op
                EndNode.Node.NextNodeIndex = -1;
                EndNode.NextLabelName = TEXT("end");
                OutNodes.Add(EndNode);
            }
            // If no preceding node, nothing to do — just mark end of flow
            else if (OutNodes.Num() > 0)
            {
                // The previous node's NextLabelName is already "end" (empty = sequential).
                // Explicitly set the last node to -1.
                OutNodes.Last().Node.NextNodeIndex = -1;
                OutNodes.Last().NextLabelName = TEXT("end");
            }
            bInChoiceBlock = false;
            return true;
        }

        // [jump label] — unconditional jump
        FString JumpCmd, JumpTarget;
        if (Body.Split(TEXT(" "), &JumpCmd, &JumpTarget) && JumpCmd.Equals(TEXT("jump"), ESearchCase::IgnoreCase))
        {
            JumpTarget = Trim(JumpTarget);
            if (LabelAwaitsNode(LabelMap, OutNodes.Num()))
            {
                // A bare @label points at this [jump] — create a passthrough node so the
                // label resolves to something real, then route it to the jump target.
                FGF_IntermediateNode JumpNode;
                JumpNode.Node.Type = EGF_DialogueNodeType::Event;  // Event/None = silent no-op
                JumpNode.NextLabelName = JumpTarget;
                OutNodes.Add(JumpNode);
            }
            else if (OutNodes.Num() > 0)
            {
                OutNodes.Last().NextLabelName = JumpTarget;
            }
            bInChoiceBlock = false;
            return true;
        }

        // [loud] — mark the preceding message node as loud (widget swaps to loud variant)
        if (Body.Equals(TEXT("loud"), ESearchCase::IgnoreCase))
        {
            if (OutNodes.Num() > 0 && OutNodes.Last().Node.Type == EGF_DialogueNodeType::Message)
                OutNodes.Last().Node.bLoud = true;
            return true;
        }

        // [sound /Game/Path/To/Asset] — assign LineSound to the preceding message node (no new node)
        if (Body.StartsWith(TEXT("sound"), ESearchCase::IgnoreCase))
        {
            FString Cmd2, SoundPath;
            if (Body.Split(TEXT(" "), &Cmd2, &SoundPath))
            {
                SoundPath = Trim(SoundPath);
                if (OutNodes.Num() > 0 && OutNodes.Last().Node.Type == EGF_DialogueNodeType::Message)
                {
                    USoundBase* Sound = LoadObject<USoundBase>(nullptr, *SoundPath);
                    if (Sound)
                        OutNodes.Last().Node.LineSound = Sound;
                    else
                        UE_LOG(LogTemp, Warning, TEXT("GF_DialogueParser line %d: Sound asset not found: '%s'"), LineNumber, *SoundPath);
                }
            }
            return true;
        }

        // [duck /Game/Path/To/SoundMix] — push a Sound Mix on the preceding message (ducks/muffles the OST)
        if (Body.StartsWith(TEXT("duck"), ESearchCase::IgnoreCase))
        {
            FString Cmd2, MixPath;
            if (Body.Split(TEXT(" "), &Cmd2, &MixPath))
            {
                MixPath = Trim(MixPath);
                if (OutNodes.Num() > 0 && OutNodes.Last().Node.Type == EGF_DialogueNodeType::Message)
                {
                    USoundMix* Mix = LoadObject<USoundMix>(nullptr, *MixPath);
                    if (Mix)
                        OutNodes.Last().Node.PushSoundMix = Mix;
                    else
                        UE_LOG(LogTemp, Warning, TEXT("GF_DialogueParser line %d: Sound Mix not found: '%s'"), LineNumber, *MixPath);
                }
            }
            return true;
        }

        // [unduck] — pop every sound mix this dialogue pushed when the preceding message displays (restores OST)
        if (Body.Equals(TEXT("unduck"), ESearchCase::IgnoreCase))
        {
            if (OutNodes.Num() > 0 && OutNodes.Last().Node.Type == EGF_DialogueNodeType::Message)
                OutNodes.Last().Node.bPopSoundMixes = true;
            return true;
        }

        // [hold N] — lock input on the preceding message node for N seconds (no new node)
        if (Body.StartsWith(TEXT("hold"), ESearchCase::IgnoreCase))
        {
            FString Cmd2, Rest2;
            float Seconds = 1.f;
            if (Body.Split(TEXT(" "), &Cmd2, &Rest2))
                Seconds = FCString::Atof(*Trim(Rest2));
            if (OutNodes.Num() > 0 && OutNodes.Last().Node.Type == EGF_DialogueNodeType::Message)
                OutNodes.Last().Node.MinDisplaySeconds = Seconds;
            return true;
        }

        // [autoend N] — show current text for N seconds then end dialogue automatically
        if (Body.StartsWith(TEXT("autoend"), ESearchCase::IgnoreCase))
        {
            FString Cmd2, Rest2;
            float Seconds = 2.f;
            if (Body.Split(TEXT(" "), &Cmd2, &Rest2))
                Seconds = FCString::Atof(*Trim(Rest2));
            if (OutNodes.Num() > 0 && OutNodes.Last().Node.Type == EGF_DialogueNodeType::Message)
                OutNodes.Last().Node.AutoEndSeconds = Seconds;
            return true;
        }

        // [pause N] — auto-advance to the next node after N seconds (no player input needed)
        if (Body.StartsWith(TEXT("pause"), ESearchCase::IgnoreCase))
        {
            FString Cmd2, Rest2;
            float Seconds = 1.f;
            if (Body.Split(TEXT(" "), &Cmd2, &Rest2))
                Seconds = FCString::Atof(*Trim(Rest2));
            if (OutNodes.Num() > 0 && OutNodes.Last().Node.Type == EGF_DialogueNodeType::Message)
                OutNodes.Last().Node.AutoAdvanceSeconds = Seconds;
            return true;
        }

        // [hide] or [hide 1.5]
        if (Body.StartsWith(TEXT("hide"), ESearchCase::IgnoreCase))
        {
            FGF_IntermediateNode HideNode;
            HideNode.Node.Type = EGF_DialogueNodeType::Hide;
            FString HideRest = Trim(Body.Mid(4));
            HideNode.Node.ResumeAfterSeconds = HideRest.IsEmpty() ? 0.f : FCString::Atof(*HideRest);
            OutNodes.Add(HideNode);
            bInChoiceBlock = false;
            return true;
        }

        // All other [commands] → Event node
        FGF_DialogueEvent Ev = ParseEventCommand(Body, LineNumber);
        FGF_IntermediateNode EvNode;
        EvNode.Node.Type = EGF_DialogueNodeType::Event;
        EvNode.Node.Event = Ev;
        OutNodes.Add(EvNode);
        bInChoiceBlock = false;
        return true;
    }

    // ── Choice lines ───────────────────────────────────────────
    if (Line.StartsWith(TEXT(">")))
    {
        FString ChoiceLine = Trim(Line.Mid(1));

        // Split on " -> " to get text and target
        FString ChoiceText, Target;
        if (!ChoiceLine.Split(TEXT("->"), &ChoiceText, &Target))
        {
            UE_LOG(LogTemp, Warning, TEXT("GF_DialogueParser line %d: Choice line missing '->'"), LineNumber);
            ChoiceText = ChoiceLine;
            Target.Empty();
        }
        ChoiceText = Trim(ChoiceText);
        Target = Trim(Target);

        if (!bInChoiceBlock || OutNodes.Num() == 0 || OutNodes.Last().Node.Type != EGF_DialogueNodeType::Choice)
        {
            // Create a new Choice node
            FGF_IntermediateNode ChoiceNode;
            ChoiceNode.Node.Type = EGF_DialogueNodeType::Choice;
            OutNodes.Add(ChoiceNode);
            bInChoiceBlock = true;
        }

        FGF_DialogueChoice Choice;
        Choice.ChoiceText = FText::FromString(ChoiceText);
        Choice.NextNodeIndex = -1; // resolved in pass 2

        OutNodes.Last().Node.Choices.Add(Choice);
        OutNodes.Last().ChoiceLabelNames.Add(Target.Equals(TEXT("[end]"), ESearchCase::IgnoreCase) ? TEXT("end") : Target);
        return true;
    }

    // ── Message lines ──────────────────────────────────────────
    bInChoiceBlock = false;

    FString Speaker;
    FString Text;

    if (Line.StartsWith(TEXT(":")))
    {
        // : text — nameless message
        Speaker.Empty();
        Text = Trim(Line.Mid(1));
    }
    else
    {
        // Try "Speaker: text"
        int32 ColonIdx = Line.Find(TEXT(": "));
        if (ColonIdx != INDEX_NONE)
        {
            Speaker = Trim(Line.Left(ColonIdx));
            Text = Trim(Line.Mid(ColonIdx + 2));
        }
        else
        {
            // Plain text line — use the last known speaker (or none)
            Speaker = CurrentSpeaker;
            Text = Line;
        }
    }

    // Update persistent speaker
    if (!Speaker.IsEmpty())
    {
        CurrentSpeaker = Speaker;
    }

    FGF_IntermediateNode MsgNode;
    MsgNode.Node.Type = EGF_DialogueNodeType::Message;
    MsgNode.Node.SpeakerName = Speaker.IsEmpty() ? FText::GetEmpty() : FText::FromString(Speaker);
    MsgNode.Node.DialogueText = FText::FromString(Text);
    OutNodes.Add(MsgNode);
    return true;
}

// ============================================================
// EVENT COMMAND PARSER
// ============================================================

FGF_DialogueEvent FGF_DialogueParser::ParseEventCommand(const FString& Body, int32 LineNumber)
{
    FGF_DialogueEvent Event;

    // Tokenize on whitespace
    TArray<FString> Tokens;
    Body.ParseIntoArrayWS(Tokens);

    if (Tokens.Num() == 0)
    {
        return Event;
    }

    FString Cmd = Tokens[0].ToLower();

    // ── [creature Species level=N full->label] ──────────────────
    if (Cmd == TEXT("creature"))
    {
        Event.Type = EGF_DialogueEventType::GiveCreature;
        if (Tokens.IsValidIndex(1))
        {
            Event.CreatureSpeciesName = FName(*Tokens[1]);
        }

        FString LevelStr;
        if (GetKVParam(Tokens, 2, TEXT("level"), LevelStr))
        {
            Event.GiftCreatureLevel = FCString::Atoi(*LevelStr);
        }

        // full->label_name
        for (int32 i = 2; i < Tokens.Num(); ++i)
        {
            FString FullBranch;
            if (Tokens[i].StartsWith(TEXT("full->"), ESearchCase::IgnoreCase))
            {
                // We can't resolve label to index here — store in EventName temporarily
                // and resolve later via the ResolveLabels pass.
                // We re-use EventName as scratch storage; the factory strips this out.
                // Actually, PartyFullNodeIndex needs to be the resolved index.
                // We store the label in a special sentinel: we'll handle it in ResolveLabels
                // by putting it in EventName and clearing it after.
                FString LabelPart = Tokens[i].Mid(6); // after "full->"
                Event.EventName = FName(*LabelPart);  // temp: label name
                Event.PartyFullNodeIndex = -2;         // -2 = "needs label resolution"
                break;
            }
        }
        return Event;
    }

    // ── [item ITEM_ID count=N] ─────────────────────────────────
    if (Cmd == TEXT("item"))
    {
        Event.Type = EGF_DialogueEventType::GiveItem;
        if (Tokens.IsValidIndex(1))
        {
            Event.ItemID = FName(*Tokens[1]);
        }
        FString CountStr;
        if (GetKVParam(Tokens, 2, TEXT("count"), CountStr))
        {
            Event.ItemQuantity = FCString::Atoi(*CountStr);
        }
        return Event;
    }

    // ── [flag FLAG_NAME] or [flag FLAG_NAME false] ─────────────
    if (Cmd == TEXT("flag"))
    {
        Event.Type = EGF_DialogueEventType::SetQuestFlag;
        if (Tokens.IsValidIndex(1))
        {
            Event.QuestFlagName = FName(*Tokens[1]);
        }
        Event.bQuestFlagValue = true;
        if (Tokens.IsValidIndex(2) && Tokens[2].Equals(TEXT("false"), ESearchCase::IgnoreCase))
        {
            Event.bQuestFlagValue = false;
        }
        return Event;
    }

    // ── [shop SHOP_ID] ────────────────────────────────────────
    if (Cmd == TEXT("shop"))
    {
        Event.Type = EGF_DialogueEventType::OpenShop;
        if (Tokens.IsValidIndex(1))
        {
            Event.ShopID = FName(*Tokens[1]);
        }
        return Event;
    }

    // ── [battle] ─────────────────────────────────────────────
    if (Cmd == TEXT("battle"))
    {
        Event.Type = EGF_DialogueEventType::StartBattle;
        return Event;
    }

    // ── [gender BoyLabel GirlLabel] ───────────────────────────
    if (Cmd == TEXT("gender"))
    {
        Event.Type = EGF_DialogueEventType::BranchGender;
        if (Tokens.IsValidIndex(1)) Event.EventName      = FName(*Tokens[1]); // Boy label
        if (Tokens.IsValidIndex(2)) Event.BranchElseLabel = FName(*Tokens[2]); // Girl label
        return Event;
    }

    // ── [flow EventName] / [flow EventName delay=N] ────────────
    // Fires a Blueprint custom event named EventName directly ON THE CALLER
    // actor (ContinueFlow) — the actor passed into StartDialogue. Unlike
    // [CustomEventName], this uses no OnDialogueEvent delegate: exactly one
    // actor receives it, exactly once, no matter how many NPCs are bound.
    if (Cmd == TEXT("flow"))
    {
        Event.Type = EGF_DialogueEventType::ContinueFlow;
        if (Tokens.IsValidIndex(1))
        {
            Event.EventName = FName(*Tokens[1]);
        }

        FString FlowDelayStr;
        if (GetKVParam(Tokens, 2, TEXT("delay"), FlowDelayStr))
        {
            Event.ResumeDelay = FCString::Atof(*FlowDelayStr);
        }
        return Event;
    }

    // ── [CustomEventName] or [CustomEventName delay=N] ────────
    {
        Event.Type = EGF_DialogueEventType::CustomEvent;
        Event.EventName = FName(*Tokens[0]); // preserve original case

        FString DelayStr;
        if (GetKVParam(Tokens, 1, TEXT("delay"), DelayStr))
        {
            Event.ResumeDelay = FCString::Atof(*DelayStr);
        }
        return Event;
    }
}

// ============================================================
// LABEL RESOLUTION (PASS 2)
// ============================================================

void FGF_DialogueParser::ResolveLabels(TArray<FGF_IntermediateNode>& Nodes,
                                        const TMap<FString, int32>& LabelMap)
{
    auto ResolveLabel = [&](const FString& LabelName) -> int32
    {
        if (LabelName.IsEmpty() || LabelName.Equals(TEXT("end"), ESearchCase::IgnoreCase))
        {
            return -1;
        }
        const int32* Found = LabelMap.Find(LabelName);
        if (!Found || !Nodes.IsValidIndex(*Found))
        {
            UE_LOG(LogTemp, Warning, TEXT("GF_DialogueParser: Unresolved or dangling label '%s'"), *LabelName);
            return -1;
        }
        return *Found;
    };

    for (int32 i = 0; i < Nodes.Num(); ++i)
    {
        FGF_IntermediateNode& INode = Nodes[i];

        // ── NextNodeIndex ──────────────────────────────────────
        if (!INode.NextLabelName.IsEmpty())
        {
            INode.Node.NextNodeIndex = ResolveLabel(INode.NextLabelName);
        }
        else if (INode.Node.Type != EGF_DialogueNodeType::Choice)
        {
            // Default: fall through to next node sequentially
            const int32 Next = i + 1;
            INode.Node.NextNodeIndex = Nodes.IsValidIndex(Next) ? Next : -1;
        }

        // ── Choice labels ──────────────────────────────────────
        for (int32 c = 0; c < INode.Node.Choices.Num(); ++c)
        {
            if (INode.ChoiceLabelNames.IsValidIndex(c))
            {
                INode.Node.Choices[c].NextNodeIndex = ResolveLabel(INode.ChoiceLabelNames[c]);
            }
        }

        // ── GiveCreature full-> label ───────────────────────────
        if (INode.Node.Type == EGF_DialogueNodeType::Event &&
            INode.Node.Event.Type == EGF_DialogueEventType::GiveCreature &&
            INode.Node.Event.PartyFullNodeIndex == -2)
        {
            // EventName was used as scratch to store the label name
            FString LabelName = INode.Node.Event.EventName.ToString();
            INode.Node.Event.PartyFullNodeIndex = ResolveLabel(LabelName);
            INode.Node.Event.EventName = NAME_None; // clear scratch
        }
    }
}

// ============================================================
// HELPERS
// ============================================================

FString FGF_DialogueParser::Trim(const FString& S)
{
    return S.TrimStartAndEnd();
}

bool FGF_DialogueParser::GetKVParam(const TArray<FString>& Tokens, int32 Offset,
                                     const FString& Key, FString& OutValue)
{
    FString Prefix = Key + TEXT("=");
    for (int32 i = Offset; i < Tokens.Num(); ++i)
    {
        if (Tokens[i].StartsWith(Prefix, ESearchCase::IgnoreCase))
        {
            OutValue = Tokens[i].Mid(Prefix.Len());
            return true;
        }
    }
    return false;
}
