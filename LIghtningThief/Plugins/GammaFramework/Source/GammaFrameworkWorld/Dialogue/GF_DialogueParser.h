#pragma once

#include "CoreMinimal.h"
#include "GF_DialogueTypes.h"

class UGF_DialogueAsset;

/**
 * GF_DialogueParser
 *
 * Converts a .gfdlg plain-text file into a populated UGF_DialogueAsset.
 *
 * ──────────────────────────────────────────────────────────────────
 * FILE FORMAT
 * ──────────────────────────────────────────────────────────────────
 *
 * Header (optional, must appear before any dialogue lines):
 *   @id   RheaPreBattle     ← sets DialogueID (FName)
 *   @widget Overworld          ← Overworld | Cinematic | Battle
 *
 * Message lines:
 *   Rhea: Hello, tamer!   ← Speaker: text
 *   : No speaker name          ← leading colon = no speaker
 *   Just plain text            ← treated as nameless message
 *
 * Event commands (square brackets):
 *   [creature Voltkit level=5 full->pc_branch]   ← GiveCreature
 *   [item POTION count=3]                        ← GiveItem
 *   [flag GOT_STARTER]                           ← SetQuestFlag true
 *   [flag GOT_STARTER false]                     ← SetQuestFlag false
 *   [hide 1.5]                                   ← Hide node, auto-resume after 1.5s
 *   [hide]                                       ← Hide node, manual resume
 *   [battle]                                     ← StartBattle (CustomEvent broadcast)
 *   [shop SHOP_ID]                               ← OpenShop
 *   [end]                                        ← end dialogue
 *   [SomeCustomName]                             ← CustomEvent "SomeCustomName"
 *   [SomeCustomName delay=2.0]                   ← CustomEvent with ResumeDelay
 *
 * Labels and jumps:
 *   @label pc_branch           ← defines a label (target for -> jumps)
 *   [jump label_name]          ← unconditional jump to label
 *
 * Choice nodes:
 *   > Option text -> label     ← choice that jumps to @label
 *   > End it -> [end]          ← choice that ends dialogue
 *
 * Comments:
 *   # This is a comment
 *
 * ──────────────────────────────────────────────────────────────────
 */
// One section parsed from a multi-dialogue file
struct FGF_DialogueSection
{
    FString DialogueID;   // from @dialogue directive
    FString SectionText;  // raw text of this section (with @id already set)
};

class GAMMAFRAMEWORKWORLD_API FGF_DialogueParser
{
public:
    /**
     * Parse a single-section file text into the asset's Nodes array.
     * Also handles a section extracted by SplitIntoSections.
     */
    static bool ParseIntoAsset(const FString& FileText, UGF_DialogueAsset* Asset);

    /**
     * Split a multi-dialogue file (containing @dialogue blocks) into sections.
     * Each section's text is ready to pass directly to ParseIntoAsset.
     * Returns an empty array if the file has no @dialogue directives (single-section file).
     */
    static TArray<FGF_DialogueSection> SplitIntoSections(const FString& FileText);

private:

    // ────────────────────────────────────────────────────────
    // Intermediate representation used between passes
    // ────────────────────────────────────────────────────────

    // A jump target that needs to be resolved in pass 2
    struct FGF_PendingJump
    {
        int32 NodeIndex;     // which node holds the unresolved index
        FString LabelName;   // the label we need to resolve
        bool bIsChoiceJump;  // if true, ChoiceIndex is valid
        int32 ChoiceIndex;
    };

    // Internal node built in pass 1 before NextNodeIndex is wired up
    struct FGF_IntermediateNode
    {
        FGF_DialogueNode Node;

        // Raw label string for NextNodeIndex (empty = use sequential next, "end" = -1)
        FString NextLabelName;

        // Per-choice label names (parallel to Node.Choices)
        TArray<FString> ChoiceLabelNames;
    };

    // ────────────────────────────────────────────────────────
    // Parsing helpers
    // ────────────────────────────────────────────────────────

    static bool ParseLine(const FString& Line, int32 LineNumber,
                          TArray<FGF_IntermediateNode>& OutNodes,
                          TMap<FString, int32>& LabelMap,
                          FString& CurrentSpeaker,
                          bool& bInChoiceBlock,
                          UGF_DialogueAsset* Asset);

    static FGF_DialogueEvent ParseEventCommand(const FString& CommandBody, int32 LineNumber);

    // Resolve label names → node indices in pass 2
    static void ResolveLabels(TArray<FGF_IntermediateNode>& Nodes,
                              const TMap<FString, int32>& LabelMap);

    // Trim whitespace from both ends
    static FString Trim(const FString& S);

    // Parse key=value pairs from a command token array starting at Offset
    static bool GetKVParam(const TArray<FString>& Tokens, int32 Offset,
                           const FString& Key, FString& OutValue);
};
