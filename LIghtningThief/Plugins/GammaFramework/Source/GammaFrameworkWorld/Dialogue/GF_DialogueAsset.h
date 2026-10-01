#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GF_DialogueTypes.h"
#include "GF_DialogueAsset.generated.h"

/**
 * GF_DialogueAsset
 *
 * One asset per conversation. Create these in the Content Browser:
 *   Right-click → Miscellaneous → Data Asset → GF_DialogueAsset
 *
 * Naming convention:
 *   DA_Dialogue_Rhea_PreBattle
 *   DA_Dialogue_Rhea_FinalMessage
 *   DA_Dialogue_Rhea_Defeat
 *   DA_Dialogue_Generic_001_PreBattle
 *   DA_Dialogue_Sign_SlatehavenCity
 */
UCLASS(BlueprintType)
class GAMMAFRAMEWORKWORLD_API UGF_DialogueAsset : public UDataAsset
{
    GENERATED_BODY()

public:

    // Unique ID used by delegates to identify which conversation finished.
    // Must match your naming convention e.g. "Rhea_PreBattle", "Rhea_Defeat"
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
    FName DialogueID;

    // Which widget to use for this conversation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
    EGF_DialogueWidgetType WidgetType = EGF_DialogueWidgetType::Overworld;

    // The sequence of nodes in this conversation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
    TArray<FGF_DialogueNode> Nodes;

    // Label name → node index map, populated at import time.
    // Used by JumpToLabel() for runtime branching (e.g. [gender] command).
    UPROPERTY()
    TMap<FName, int32> LabelMap;

    // Optional sound to play when this dialogue starts (e.g. fanfare for trial leader)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
    USoundBase* StartSound = nullptr;

    // Path to the source .gfdlg file — set automatically on import, used for reimport.
    UPROPERTY(VisibleAnywhere, Category = "Dialogue|Import")
    FString SourceFilePath;
};
