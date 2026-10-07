#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"

// ── GF.RemoveBrokenVariables ──────────────────────────────────────────────────
//
// A blueprint variable whose type points at a struct/class/enum that no longer
// exists never compiles into a property. The My Blueprint panel builds its list
// from the compiled properties, so the variable is not shown there and cannot be
// deleted by hand -- but it still fails the compile every time. This removes
// those variables straight from the blueprint.
//
//   GF.RemoveBrokenVariables /Game/BPS/Battle/BattleArena_LT_BP

namespace
{
    bool IsVariableTypeMissing(const FBPVariableDescription& Variable)
    {
        // The pin categories that are meaningless without a type object.
        static const FName ObjectBackedCategories[] =
        {
            TEXT("struct"), TEXT("enum"), TEXT("object"), TEXT("class"),
            TEXT("softobject"), TEXT("softclass"), TEXT("interface"),
        };

        for (const FName& Category : ObjectBackedCategories)
        {
            if (Variable.VarType.PinCategory == Category)
            {
                return !Variable.VarType.PinSubCategoryObject.IsValid();
            }
        }
        return false;
    }

    void RemoveBrokenVariables(const TArray<FString>& Args)
    {
        if (Args.Num() != 1)
        {
            UE_LOG(LogTemp, Warning, TEXT("Usage: GF.RemoveBrokenVariables /Game/Path/To/Blueprint"));
            return;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Args[0]);
        if (!Blueprint)
        {
            UE_LOG(LogTemp, Error, TEXT("GF.RemoveBrokenVariables: no blueprint at '%s'"), *Args[0]);
            return;
        }

        TArray<FName> RemovedNames;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            if (IsVariableTypeMissing(Variable))
            {
                RemovedNames.Add(Variable.VarName);
            }
        }

        if (RemovedNames.Num() == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("GF.RemoveBrokenVariables: nothing to remove in '%s'"), *Blueprint->GetName());
            return;
        }

        Blueprint->Modify();
        Blueprint->NewVariables.RemoveAll(&IsVariableTypeMissing);

        for (const FName& Name : RemovedNames)
        {
            UE_LOG(LogTemp, Log, TEXT("GF.RemoveBrokenVariables: removed '%s'"), *Name.ToString());

            // Broken variables can share a name with a working one. Leave the
            // get/set nodes alone when a variable of that name is still there.
            if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name) == INDEX_NONE)
            {
                FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, Name);
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

        UE_LOG(LogTemp, Log, TEXT("GF.RemoveBrokenVariables: removed %d variable(s) from '%s'. Compile and save it."),
            RemovedNames.Num(), *Blueprint->GetName());
    }

    FAutoConsoleCommand GRemoveBrokenVariablesCommand(
        TEXT("GF.RemoveBrokenVariables"),
        TEXT("Removes blueprint variables whose struct/class/enum type no longer exists. Usage: GF.RemoveBrokenVariables /Game/Path/To/Blueprint"),
        FConsoleCommandWithArgsDelegate::CreateStatic(&RemoveBrokenVariables));
}
