#include "BPSnapshotExporter.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"

namespace
{
TSharedRef<FJsonObject> PinTypeToJson(const FEdGraphPinType& Type)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("category"), Type.PinCategory.ToString());
    Json->SetStringField(TEXT("subcategory"), Type.PinSubCategory.ToString());
    Json->SetStringField(TEXT("subcategory_object"), GetPathNameSafe(Type.PinSubCategoryObject.Get()));
    Json->SetNumberField(TEXT("container"), static_cast<int32>(Type.ContainerType));
    Json->SetBoolField(TEXT("is_reference"), Type.bIsReference);
    Json->SetBoolField(TEXT("is_const"), Type.bIsConst);
    Json->SetBoolField(TEXT("is_weak_pointer"), Type.bIsWeakPointer);
    return Json;
}

TSharedRef<FJsonObject> PinToJson(const UEdGraphPin* Pin)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), Pin->PinId.ToString());
    Json->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Json->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
    Json->SetObjectField(TEXT("type"), PinTypeToJson(Pin->PinType));
    Json->SetStringField(TEXT("default_value"), Pin->DefaultValue);
    Json->SetStringField(TEXT("default_object"), GetPathNameSafe(Pin->DefaultObject));
    Json->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());
    Json->SetBoolField(TEXT("is_hidden"), Pin->bHidden);
    TArray<TSharedPtr<FJsonValue>> Links;
    for (const UEdGraphPin* Linked : Pin->LinkedTo)
    {
        if (!Linked || !Linked->GetOwningNode()) continue;
        auto Link = MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("node_id"), Linked->GetOwningNode()->NodeGuid.ToString());
        Link->SetStringField(TEXT("pin_id"), Linked->PinId.ToString());
        Links.Add(MakeShared<FJsonValueObject>(Link));
    }
    Json->SetArrayField(TEXT("links"), Links);
    return Json;
}

TSharedRef<FJsonObject> NodeToJson(const UEdGraphNode* Node)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
    Json->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
    Json->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    Json->SetNumberField(TEXT("x"), Node->NodePosX);
    Json->SetNumberField(TEXT("y"), Node->NodePosY);
    if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
    {
        Json->SetStringField(TEXT("function_name"), Call->GetFunctionName().ToString());
        Json->SetStringField(TEXT("function_path"), GetPathNameSafe(Call->GetTargetFunction()));
    }
    TArray<TSharedPtr<FJsonValue>> Pins;
    for (const UEdGraphPin* Pin : Node->Pins)
        if (Pin) Pins.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
    Json->SetArrayField(TEXT("pins"), Pins);
    return Json;
}

TSharedRef<FJsonObject> GraphToJson(const UEdGraph* Graph, const FString& Kind)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Graph->GetName());
    Json->SetStringField(TEXT("path"), Graph->GetPathName());
    Json->SetStringField(TEXT("kind"), Kind);
    Json->SetStringField(TEXT("guid"), Graph->GraphGuid.ToString());
    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (const UEdGraphNode* Node : Graph->Nodes)
        if (Node) Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(Node)));
    Json->SetArrayField(TEXT("nodes"), Nodes);
    return Json;
}
}

TSharedRef<FJsonObject> FBPSnapshotExporter::Extract(const UBlueprint* Blueprint)
{
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 1);
    Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Root->SetStringField(TEXT("asset_path"), GetPathNameSafe(Blueprint));
    Root->SetStringField(TEXT("parent_class"), Blueprint ? GetPathNameSafe(Blueprint->ParentClass) : TEXT("None"));
    TArray<TSharedPtr<FJsonValue>> Graphs, Variables;
    if (Blueprint)
    {
        auto Append = [&Graphs](const TArray<TObjectPtr<UEdGraph>>& Source, const TCHAR* Kind)
        {
            for (const UEdGraph* Graph : Source)
                if (Graph) Graphs.Add(MakeShared<FJsonValueObject>(GraphToJson(Graph, Kind)));
        };
        Append(Blueprint->UbergraphPages, TEXT("event"));
        Append(Blueprint->FunctionGraphs, TEXT("function"));
        Append(Blueprint->MacroGraphs, TEXT("macro"));
        for (const FBPVariableDescription& Var : Blueprint->NewVariables)
        {
            auto Json = MakeShared<FJsonObject>();
            Json->SetStringField(TEXT("name"), Var.VarName.ToString());
            Json->SetStringField(TEXT("guid"), Var.VarGuid.ToString());
            Json->SetObjectField(TEXT("type"), PinTypeToJson(Var.VarType));
            Json->SetStringField(TEXT("default_value"), Var.DefaultValue);
            Json->SetStringField(TEXT("property_flags"), FString::Printf(TEXT("%llu"), static_cast<uint64>(Var.PropertyFlags)));
            Variables.Add(MakeShared<FJsonValueObject>(Json));
        }
    }
    Root->SetArrayField(TEXT("graphs"), Graphs);
    Root->SetArrayField(TEXT("variables"), Variables);
    return Root;
}

bool FBPSnapshotExporter::Serialize(const UBlueprint* Blueprint, FString& OutJson)
{
    OutJson.Reset();
    if (!Blueprint) return false;
    auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJson);
    return FJsonSerializer::Serialize(Extract(Blueprint), Writer);
}

bool FBPSnapshotExporter::Export(const UBlueprint* Blueprint, FString& OutPath, FString& OutError)
{
    OutPath.Reset();
    OutError.Reset();
    if (!Blueprint) { OutError = TEXT("Blueprint is null"); return false; }
    FString Json;
    if (!Serialize(Blueprint, Json)) { OutError = TEXT("JSON serialization failed"); return false; }
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("BlueprintSnapshots");
    if (!IFileManager::Get().MakeDirectory(*Folder, true))
    {
        OutError = TEXT("Could not create output folder");
        return false;
    }
    OutPath = Folder / (FPaths::MakeValidFileName(Blueprint->GetPathName()) + TEXT(".json"));
    if (!FFileHelper::SaveStringToFile(Json, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = TEXT("Could not write JSON file");
        return false;
    }
    return true;
}
