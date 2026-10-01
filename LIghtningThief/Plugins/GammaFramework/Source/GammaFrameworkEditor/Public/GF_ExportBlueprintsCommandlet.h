#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GF_ExportBlueprintsCommandlet.generated.h"

/**
 * UGF_ExportBlueprintsCommandlet
 *
 * Batch blueprint-to-JSON export with no editor window open, so it can run from
 * a script, a git hook, or a build step.
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=GF_ExportBlueprints [switches]
 *
 * Switches (all optional):
 *
 *   -Path=/Game/UI,/Game/Menus   Only these content folders, searched recursively.
 *                                Default: the whole project.
 *   -Asset=/Game/UI/MyMenu       One or more specific assets, by package name.
 *   -Out=<dir or file>           Where to write. Default <Project>/Saved/BlueprintJSON.
 *                                With -Combine this is the file to write instead.
 *   -WidgetsOnly                 Widget blueprints only, skipping everything else.
 *   -Combine                     One JSON array in one file, rather than a file each.
 *   -Compact                     No indentation. Smaller file, same content.
 *   -NoGraphs                    Skip node graphs.
 *   -NoProperties                Skip the per-object property dumps.
 *   -NoWidgetTree                Skip the UMG hierarchy.
 *   -HiddenPins                  Include hidden pins that are neither wired nor set.
 *
 * Exits non-zero when nothing matched or a file could not be written, so a
 * caller can tell an empty export from a failed one.
 */
UCLASS()
class UGF_ExportBlueprintsCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UGF_ExportBlueprintsCommandlet();

    virtual int32 Main(const FString& Params) override;
};
