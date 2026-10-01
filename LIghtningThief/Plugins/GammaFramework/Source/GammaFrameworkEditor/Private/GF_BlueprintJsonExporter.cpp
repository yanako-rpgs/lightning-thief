#include "GF_BlueprintJsonExporter.h"

#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"

#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogGFBlueprintExport, Log, All);

// -----------------------------------------------------------------------------
// Local helpers
// -----------------------------------------------------------------------------
namespace
{
    /**
     * Object-valued properties come out as their path, not as a nested dump of
     * everything they point at. Without this the exporter follows the first
     * hard reference it finds and never comes back: a widget references its
     * font, which references its typeface, which references a font face asset.
     */
    const FJsonObjectConverter::CustomExportCallback& ObjectsAsPaths()
    {
        static FJsonObjectConverter::CustomExportCallback Callback = []()
        {
            FJsonObjectConverter::CustomExportCallback Cb;
            Cb.BindLambda([](FProperty* Property, const void* Value) -> TSharedPtr<FJsonValue>
            {
                if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
                {
                    const UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
                    return MakeShared<FJsonValueString>(Object ? Object->GetPathName() : FString(TEXT("None")));
                }
                // Anything else: let the converter do its usual job.
                return TSharedPtr<FJsonValue>();
            });
            return Cb;
        }();
        return Callback;
    }

    /** Runtime state and dead properties are noise in a file meant to be read. */
    constexpr uint64 SkippedPropertyFlags = CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated;

    FString FlattenTitle(const FText& Title)
    {
        FString Flat = Title.ToString();
        Flat.ReplaceInline(TEXT("\r"), TEXT(""));
        Flat.ReplaceInline(TEXT("\n"), TEXT(" | "));
        return Flat;
    }

    template <typename PolicyType>
    bool SerializeWith(const TSharedRef<FJsonObject>& Object, FString& OutString)
    {
        TSharedRef<TJsonWriter<TCHAR, PolicyType>> Writer =
            TJsonWriterFactory<TCHAR, PolicyType>::Create(&OutString);
        const bool bOk = FJsonSerializer::Serialize(Object, Writer);
        Writer->Close();
        return bOk;
    }

    template <typename PolicyType>
    bool SerializeArrayWith(const TArray<TSharedPtr<FJsonValue>>& Values, FString& OutString)
    {
        TSharedRef<TJsonWriter<TCHAR, PolicyType>> Writer =
            TJsonWriterFactory<TCHAR, PolicyType>::Create(&OutString);
        const bool bOk = FJsonSerializer::Serialize(Values, Writer);
        Writer->Close();
        return bOk;
    }

    bool SerializeObject(const TSharedRef<FJsonObject>& Object, bool bPretty, FString& OutString)
    {
        return bPretty
            ? SerializeWith<TPrettyJsonPrintPolicy<TCHAR>>(Object, OutString)
            : SerializeWith<TCondensedJsonPrintPolicy<TCHAR>>(Object, OutString);
    }
}

// -----------------------------------------------------------------------------
// Small shared helpers
// -----------------------------------------------------------------------------

FString FGF_BlueprintJsonExporter::ObjectPath(const UObject* Object)
{
    return Object ? Object->GetPathName() : FString();
}

void FGF_BlueprintJsonExporter::WriteChangedProperties(UObject* Object, const TSharedRef<FJsonObject>& Out)
{
    if (!Object)
    {
        return;
    }

    // The archetype, not the class default object: a widget inside a widget
    // tree inherits from its template, and it is the difference from *that*
    // that somebody typed into the designer.
    UObject* Archetype = Object->GetArchetype();

    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(SkippedPropertyFlags))
        {
            continue;
        }
        if (Archetype && Property->Identical_InContainer(Object, Archetype))
        {
            continue;
        }

        const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
        const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
            Property, ValuePtr, /*CheckFlags*/ 0, static_cast<int64>(SkippedPropertyFlags), &ObjectsAsPaths());

        if (Value.IsValid())
        {
            Out->SetField(Property->GetName(), Value);
        }
    }
}

FString FGF_BlueprintJsonExporter::PinTypeToString(const FEdGraphPinType& PinType)
{
    FString Result = PinType.PinCategory.ToString();

    if (!PinType.PinSubCategory.IsNone())
    {
        Result += TEXT(":") + PinType.PinSubCategory.ToString();
    }
    if (const UObject* SubObject = PinType.PinSubCategoryObject.Get())
    {
        Result += TEXT(":") + SubObject->GetPathName();
    }

    switch (PinType.ContainerType)
    {
    case EPinContainerType::Array:
        Result = FString::Printf(TEXT("array<%s>"), *Result);
        break;
    case EPinContainerType::Set:
        Result = FString::Printf(TEXT("set<%s>"), *Result);
        break;
    case EPinContainerType::Map:
        Result = FString::Printf(TEXT("map<%s,%s>"), *Result, *PinType.PinValueType.TerminalCategory.ToString());
        break;
    default:
        break;
    }

    if (PinType.bIsReference)
    {
        Result += TEXT("&");
    }
    if (PinType.bIsConst)
    {
        Result = TEXT("const ") + Result;
    }

    return Result;
}

// -----------------------------------------------------------------------------
// Pins, nodes, graphs
// -----------------------------------------------------------------------------

TSharedPtr<FJsonObject> FGF_BlueprintJsonExporter::ExportPin(UEdGraphPin* Pin)
{
    TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

    Out->SetStringField(TEXT("id"),   Pin->PinId.ToString(EGuidFormats::Digits));
    Out->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Out->SetStringField(TEXT("dir"),  Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"));
    Out->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));

    if (!Pin->PinFriendlyName.IsEmpty())
    {
        Out->SetStringField(TEXT("label"), Pin->PinFriendlyName.ToString());
    }

    // The value typed into the pin, but only when it is not the value the node
    // shipped with, otherwise every pin carries its own default twice.
    if (!Pin->DefaultValue.IsEmpty() && Pin->DefaultValue != Pin->AutogeneratedDefaultValue)
    {
        Out->SetStringField(TEXT("default"), Pin->DefaultValue);
    }
    if (Pin->DefaultObject)
    {
        Out->SetStringField(TEXT("defaultObject"), ObjectPath(Pin->DefaultObject));
    }
    if (!Pin->DefaultTextValue.IsEmpty())
    {
        Out->SetStringField(TEXT("defaultText"), Pin->DefaultTextValue.ToString());
    }

    if (Pin->bHidden)
    {
        Out->SetBoolField(TEXT("hidden"), true);
    }
    if (Pin->bOrphanedPin)
    {
        Out->SetBoolField(TEXT("orphaned"), true);
    }
    if (Pin->ParentPin)
    {
        Out->SetStringField(TEXT("parentPin"), Pin->ParentPin->PinName.ToString());
    }

    // The wires. Named by the node GUID on the far end, which is how a reader
    // walks the graph without a picture of it in front of them.
    if (Pin->LinkedTo.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> Links;
        for (UEdGraphPin* Linked : Pin->LinkedTo)
        {
            if (!Linked)
            {
                continue;
            }
            TSharedRef<FJsonObject> Link = MakeShared<FJsonObject>();
            if (UEdGraphNode* Far = Linked->GetOwningNodeUnchecked())
            {
                Link->SetStringField(TEXT("node"), Far->NodeGuid.ToString(EGuidFormats::Digits));
                Link->SetStringField(TEXT("nodeTitle"), FlattenTitle(Far->GetNodeTitle(ENodeTitleType::ListView)));
            }
            Link->SetStringField(TEXT("pin"), Linked->PinName.ToString());
            Links.Add(MakeShared<FJsonValueObject>(Link));
        }
        Out->SetArrayField(TEXT("links"), Links);
    }

    return Out;
}

TSharedPtr<FJsonObject> FGF_BlueprintJsonExporter::ExportNode(UEdGraphNode* Node, const FGF_BlueprintExportOptions& Options)
{
    TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

    Out->SetStringField(TEXT("id"),    Node->NodeGuid.ToString(EGuidFormats::Digits));
    Out->SetStringField(TEXT("class"), Node->GetClass()->GetName());
    Out->SetStringField(TEXT("title"), FlattenTitle(Node->GetNodeTitle(ENodeTitleType::FullTitle)));

    if (!Node->NodeComment.IsEmpty())
    {
        Out->SetStringField(TEXT("comment"), Node->NodeComment);
    }
    if (Node->GetDesiredEnabledState() != ENodeEnabledState::Enabled)
    {
        Out->SetStringField(TEXT("enabledState"), LexToString(Node->GetDesiredEnabledState()));
    }

    TArray<TSharedPtr<FJsonValue>> Position;
    Position.Add(MakeShared<FJsonValueNumber>(Node->NodePosX));
    Position.Add(MakeShared<FJsonValueNumber>(Node->NodePosY));
    Out->SetArrayField(TEXT("pos"), Position);

    // No per-node-class special cases. A call node's function reference, a
    // variable node's variable reference, a cast node's target type: they are
    // all just properties on the node, so the same diff-the-archetype pass that
    // handles widgets picks up whatever the node class happens to store.
    if (Options.bIncludeProperties)
    {
        TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
        WriteChangedProperties(Node, Properties);
        if (Properties->Values.Num() > 0)
        {
            Out->SetObjectField(TEXT("properties"), Properties);
        }
    }

    TArray<TSharedPtr<FJsonValue>> Pins;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin)
        {
            continue;
        }
        // A hidden pin nobody wired and nobody overrode says nothing.
        const bool bIsSilent =
            Pin->bHidden &&
            Pin->LinkedTo.Num() == 0 &&
            !Pin->DefaultObject &&
            Pin->DefaultValue == Pin->AutogeneratedDefaultValue;

        if (bIsSilent && !Options.bIncludeHiddenPins)
        {
            continue;
        }
        Pins.Add(MakeShared<FJsonValueObject>(ExportPin(Pin).ToSharedRef()));
    }
    Out->SetArrayField(TEXT("pins"), Pins);

    return Out;
}

TSharedPtr<FJsonObject> FGF_BlueprintJsonExporter::ExportGraph(UEdGraph* Graph, const FString& GraphKind, const FGF_BlueprintExportOptions& Options)
{
    if (!Graph)
    {
        return nullptr;
    }

    TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("name"), Graph->GetName());
    Out->SetStringField(TEXT("kind"), GraphKind);

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node)
        {
            Nodes.Add(MakeShared<FJsonValueObject>(ExportNode(Node, Options).ToSharedRef()));
        }
    }
    Out->SetArrayField(TEXT("nodes"), Nodes);

    // A collapsed node keeps its contents in a graph of its own.
    if (Graph->SubGraphs.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> SubGraphs;
        for (UEdGraph* SubGraph : Graph->SubGraphs)
        {
            if (const TSharedPtr<FJsonObject> Exported = ExportGraph(SubGraph, TEXT("Collapsed"), Options))
            {
                SubGraphs.Add(MakeShared<FJsonValueObject>(Exported.ToSharedRef()));
            }
        }
        Out->SetArrayField(TEXT("subGraphs"), SubGraphs);
    }

    return Out;
}

// -----------------------------------------------------------------------------
// Widget tree
// -----------------------------------------------------------------------------

TSharedPtr<FJsonObject> FGF_BlueprintJsonExporter::ExportWidget(UWidget* Widget, const FGF_BlueprintExportOptions& Options)
{
    if (!Widget)
    {
        return nullptr;
    }

    TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("name"),  Widget->GetName());
    Out->SetStringField(TEXT("class"), Widget->GetClass()->GetPathName());
    if (Widget->bIsVariable)
    {
        Out->SetBoolField(TEXT("isVariable"), true);
    }

    if (Options.bIncludeProperties)
    {
        TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
        WriteChangedProperties(Widget, Properties);
        if (Properties->Values.Num() > 0)
        {
            Out->SetObjectField(TEXT("properties"), Properties);
        }

        // The slot is where the layout lives: padding, anchors, fill. It is a
        // separate object owned by the parent panel, so it needs its own dump.
        if (UPanelSlot* Slot = Widget->Slot)
        {
            TSharedRef<FJsonObject> SlotJson = MakeShared<FJsonObject>();
            SlotJson->SetStringField(TEXT("class"), Slot->GetClass()->GetPathName());
            TSharedRef<FJsonObject> SlotProperties = MakeShared<FJsonObject>();
            WriteChangedProperties(Slot, SlotProperties);
            SlotJson->SetObjectField(TEXT("properties"), SlotProperties);
            Out->SetObjectField(TEXT("slot"), SlotJson);
        }
    }

    if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
    {
        TArray<TSharedPtr<FJsonValue>> Children;
        for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
        {
            if (const TSharedPtr<FJsonObject> Child = ExportWidget(Panel->GetChildAt(Index), Options))
            {
                Children.Add(MakeShared<FJsonValueObject>(Child.ToSharedRef()));
            }
        }
        if (Children.Num() > 0)
        {
            Out->SetArrayField(TEXT("children"), Children);
        }
    }

    return Out;
}

// -----------------------------------------------------------------------------
// The blueprint itself
// -----------------------------------------------------------------------------

TSharedPtr<FJsonObject> FGF_BlueprintJsonExporter::ExportBlueprint(UBlueprint* Blueprint, const FGF_BlueprintExportOptions& Options)
{
    if (!Blueprint)
    {
        return nullptr;
    }

    TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("asset"),       Blueprint->GetPathName());
    Out->SetStringField(TEXT("name"),        Blueprint->GetName());
    Out->SetStringField(TEXT("class"),       Blueprint->GetClass()->GetName());
    Out->SetStringField(TEXT("parentClass"), ObjectPath(Blueprint->ParentClass));

    if (Blueprint->ImplementedInterfaces.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> Interfaces;
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            Interfaces.Add(MakeShared<FJsonValueString>(ObjectPath(Interface.Interface)));
        }
        Out->SetArrayField(TEXT("interfaces"), Interfaces);
    }

    // ----- variables ---------------------------------------------------------
    {
        TArray<TSharedPtr<FJsonValue>> Variables;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            TSharedRef<FJsonObject> VariableJson = MakeShared<FJsonObject>();
            VariableJson->SetStringField(TEXT("name"), Variable.VarName.ToString());
            VariableJson->SetStringField(TEXT("type"), PinTypeToString(Variable.VarType));
            if (!Variable.DefaultValue.IsEmpty())
            {
                VariableJson->SetStringField(TEXT("default"), Variable.DefaultValue);
            }
            if (!Variable.Category.IsEmpty())
            {
                VariableJson->SetStringField(TEXT("category"), Variable.Category.ToString());
            }
            VariableJson->SetBoolField(TEXT("instanceEditable"),
                (Variable.PropertyFlags & CPF_Edit) != 0 && (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0);

            if (Variable.MetaDataArray.Num() > 0)
            {
                TSharedRef<FJsonObject> MetaData = MakeShared<FJsonObject>();
                for (const FBPVariableMetaDataEntry& Entry : Variable.MetaDataArray)
                {
                    MetaData->SetStringField(Entry.DataKey.ToString(), Entry.DataValue);
                }
                VariableJson->SetObjectField(TEXT("metadata"), MetaData);
            }
            Variables.Add(MakeShared<FJsonValueObject>(VariableJson));
        }
        Out->SetArrayField(TEXT("variables"), Variables);
    }

    // ----- widget tree, animations, bindings ---------------------------------
    if (Options.bIncludeWidgetTree)
    {
        if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint))
        {
            if (WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->RootWidget)
            {
                if (const TSharedPtr<FJsonObject> Root = ExportWidget(WidgetBlueprint->WidgetTree->RootWidget, Options))
                {
                    Out->SetObjectField(TEXT("widgetTree"), Root.ToSharedRef());
                }
            }

            if (WidgetBlueprint->Animations.Num() > 0)
            {
                TArray<TSharedPtr<FJsonValue>> Animations;
                for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
                {
                    if (!Animation)
                    {
                        continue;
                    }
                    TSharedRef<FJsonObject> AnimationJson = MakeShared<FJsonObject>();
                    AnimationJson->SetStringField(TEXT("name"),  Animation->GetName());
                    AnimationJson->SetStringField(TEXT("label"), Animation->GetDisplayLabel());
                    AnimationJson->SetNumberField(TEXT("start"), Animation->GetStartTime());
                    AnimationJson->SetNumberField(TEXT("end"),   Animation->GetEndTime());

                    TArray<TSharedPtr<FJsonValue>> Targets;
                    for (const FWidgetAnimationBinding& Binding : Animation->GetBindings())
                    {
                        TSharedRef<FJsonObject> BindingJson = MakeShared<FJsonObject>();
                        BindingJson->SetStringField(TEXT("widget"), Binding.WidgetName.ToString());
                        if (!Binding.SlotWidgetName.IsNone())
                        {
                            BindingJson->SetStringField(TEXT("slotOf"), Binding.SlotWidgetName.ToString());
                        }
                        Targets.Add(MakeShared<FJsonValueObject>(BindingJson));
                    }
                    AnimationJson->SetArrayField(TEXT("targets"), Targets);
                    Animations.Add(MakeShared<FJsonValueObject>(AnimationJson));
                }
                Out->SetArrayField(TEXT("animations"), Animations);
            }

            if (WidgetBlueprint->Bindings.Num() > 0)
            {
                TArray<TSharedPtr<FJsonValue>> Bindings;
                for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
                {
                    TSharedRef<FJsonObject> BindingJson = MakeShared<FJsonObject>();
                    BindingJson->SetStringField(TEXT("widget"),   Binding.ObjectName);
                    BindingJson->SetStringField(TEXT("property"), Binding.PropertyName.ToString());
                    BindingJson->SetStringField(TEXT("function"), Binding.FunctionName.ToString());
                    if (!Binding.SourceProperty.IsNone())
                    {
                        BindingJson->SetStringField(TEXT("sourceProperty"), Binding.SourceProperty.ToString());
                    }
                    Bindings.Add(MakeShared<FJsonValueObject>(BindingJson));
                }
                Out->SetArrayField(TEXT("bindings"), Bindings);
            }
        }
    }

    // ----- components (actor blueprints) -------------------------------------
    if (Options.bIncludeComponents && Blueprint->SimpleConstructionScript)
    {
        TFunction<TSharedPtr<FJsonObject>(USCS_Node*)> ExportComponentNode =
            [&ExportComponentNode, &Options](USCS_Node* ScsNode) -> TSharedPtr<FJsonObject>
        {
            if (!ScsNode)
            {
                return nullptr;
            }
            TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
            NodeJson->SetStringField(TEXT("name"),  ScsNode->GetVariableName().ToString());
            NodeJson->SetStringField(TEXT("class"), ScsNode->ComponentClass ? ScsNode->ComponentClass->GetPathName() : FString());

            if (Options.bIncludeProperties && ScsNode->ComponentTemplate)
            {
                TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
                WriteChangedProperties(ScsNode->ComponentTemplate, Properties);
                if (Properties->Values.Num() > 0)
                {
                    NodeJson->SetObjectField(TEXT("properties"), Properties);
                }
            }

            TArray<TSharedPtr<FJsonValue>> Children;
            for (USCS_Node* Child : ScsNode->GetChildNodes())
            {
                if (const TSharedPtr<FJsonObject> ChildJson = ExportComponentNode(Child))
                {
                    Children.Add(MakeShared<FJsonValueObject>(ChildJson.ToSharedRef()));
                }
            }
            if (Children.Num() > 0)
            {
                NodeJson->SetArrayField(TEXT("children"), Children);
            }
            return NodeJson;
        };

        TArray<TSharedPtr<FJsonValue>> Components;
        for (USCS_Node* RootNode : Blueprint->SimpleConstructionScript->GetRootNodes())
        {
            if (const TSharedPtr<FJsonObject> NodeJson = ExportComponentNode(RootNode))
            {
                Components.Add(MakeShared<FJsonValueObject>(NodeJson.ToSharedRef()));
            }
        }
        if (Components.Num() > 0)
        {
            Out->SetArrayField(TEXT("components"), Components);
        }
    }

    // ----- graphs ------------------------------------------------------------
    if (Options.bIncludeGraphs)
    {
        TArray<TSharedPtr<FJsonValue>> Graphs;

        auto AddGraphs = [&Graphs, &Options](const TArray<TObjectPtr<UEdGraph>>& Source, const TCHAR* Kind)
        {
            for (UEdGraph* Graph : Source)
            {
                if (const TSharedPtr<FJsonObject> Exported = ExportGraph(Graph, Kind, Options))
                {
                    Graphs.Add(MakeShared<FJsonValueObject>(Exported.ToSharedRef()));
                }
            }
        };

        AddGraphs(Blueprint->UbergraphPages,          TEXT("EventGraph"));
        AddGraphs(Blueprint->FunctionGraphs,          TEXT("Function"));
        AddGraphs(Blueprint->MacroGraphs,             TEXT("Macro"));
        AddGraphs(Blueprint->DelegateSignatureGraphs, TEXT("DelegateSignature"));

        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            AddGraphs(Interface.Graphs, TEXT("InterfaceFunction"));
        }

        Out->SetArrayField(TEXT("graphs"), Graphs);
    }

    return Out;
}

FString FGF_BlueprintJsonExporter::ExportBlueprintToString(UBlueprint* Blueprint, const FGF_BlueprintExportOptions& Options)
{
    const TSharedPtr<FJsonObject> Json = ExportBlueprint(Blueprint, Options);
    if (!Json.IsValid())
    {
        return FString();
    }

    FString Result;
    SerializeObject(Json.ToSharedRef(), Options.bPretty, Result);
    return Result;
}

bool FGF_BlueprintJsonExporter::ExportBlueprintToFile(UBlueprint* Blueprint, const FString& OutFilePath, const FGF_BlueprintExportOptions& Options)
{
    const FString Json = ExportBlueprintToString(Blueprint, Options);
    if (Json.IsEmpty())
    {
        return false;
    }
    return FFileHelper::SaveStringToFile(Json, *OutFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// -----------------------------------------------------------------------------
// Batches
// -----------------------------------------------------------------------------

FString FGF_BlueprintJsonExporter::DefaultOutputDir()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BlueprintJSON"));
}

void FGF_BlueprintJsonExporter::FindBlueprints(const TArray<FString>& PackagePaths, bool bWidgetsOnly, TArray<FAssetData>& OutAssets)
{
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // A commandlet starts with an empty registry; in the editor this returns
    // straight away, because the scan already happened.
    AssetRegistry.SearchAllAssets(/*bSynchronousSearch*/ true);

    FARFilter Filter;
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths   = true;
    Filter.ClassPaths.Add(bWidgetsOnly
        ? UWidgetBlueprint::StaticClass()->GetClassPathName()
        : UBlueprint::StaticClass()->GetClassPathName());

    for (const FString& Path : PackagePaths)
    {
        Filter.PackagePaths.Add(FName(*Path));
    }

    AssetRegistry.GetAssets(Filter, OutAssets);
}

int32 FGF_BlueprintJsonExporter::ExportAssets(
    const TArray<FAssetData>&         Assets,
    const FString&                    OutDir,
    const FGF_BlueprintExportOptions& Options,
    TArray<FString>&                  OutWrittenFiles)
{
    int32 Written = 0;

    for (const FAssetData& Asset : Assets)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
        if (!Blueprint)
        {
            UE_LOG(LogGFBlueprintExport, Warning, TEXT("Skipped %s: not a blueprint, or it failed to load"),
                *Asset.GetObjectPathString());
            continue;
        }

        // Mirror the content folder structure, so two assets that share a name
        // in different folders do not overwrite each other.
        FString Relative = Asset.PackageName.ToString();
        Relative.RemoveFromStart(TEXT("/"));
        const FString FilePath = FPaths::Combine(OutDir, Relative + TEXT(".json"));

        if (ExportBlueprintToFile(Blueprint, FilePath, Options))
        {
            OutWrittenFiles.Add(FilePath);
            ++Written;
        }
        else
        {
            UE_LOG(LogGFBlueprintExport, Error, TEXT("Could not write %s"), *FilePath);
        }
    }

    return Written;
}

bool FGF_BlueprintJsonExporter::ExportAssetsCombined(
    const TArray<FAssetData>&         Assets,
    const FString&                    OutFilePath,
    const FGF_BlueprintExportOptions& Options)
{
    TArray<TSharedPtr<FJsonValue>> Values;

    for (const FAssetData& Asset : Assets)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
        if (!Blueprint)
        {
            continue;
        }
        if (const TSharedPtr<FJsonObject> Json = ExportBlueprint(Blueprint, Options))
        {
            Values.Add(MakeShared<FJsonValueObject>(Json.ToSharedRef()));
        }
    }

    FString Result;
    const bool bSerialized = Options.bPretty
        ? SerializeArrayWith<TPrettyJsonPrintPolicy<TCHAR>>(Values, Result)
        : SerializeArrayWith<TCondensedJsonPrintPolicy<TCHAR>>(Values, Result);

    if (!bSerialized)
    {
        return false;
    }
    return FFileHelper::SaveStringToFile(Result, *OutFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
