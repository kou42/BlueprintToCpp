#include "BPCopilotGraphJsonExporter.h"
#include "BPSemanticIR.h"
#include "BPSemanticJsonSerializer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
TSharedPtr<FJsonValue> GuidJson(const FGuid& Id)
{
    if (Id.IsValid() == false)
    {
        return MakeShared<FJsonValueNull>();
    }
    return MakeShared<FJsonValueString>(Id.ToString(EGuidFormats::DigitsWithHyphens));
}

TSharedRef<FJsonObject> LinkJson(const FBPIRLink& Link)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetField(TEXT("source_node_id"), GuidJson(Link.SourceNode));
    Result->SetField(TEXT("source_pin_id"), GuidJson(Link.SourcePin));
    Result->SetField(TEXT("target_node_id"), GuidJson(Link.TargetNode));
    Result->SetField(TEXT("target_pin_id"), GuidJson(Link.TargetPin));
    return Result;
}
// グラフ内の接続が実在するNode/Pinを指しているか検証する。
// 不正な参照は推測で修復せず、出力を失敗させて原因を明示する。
bool ValidateGraphLinks(const FBPIRGraph& Graph, FString& OutError)
{
    TMap<FGuid, TSet<FGuid>> NodePins;
    for (const FBPIRNode& Node : Graph.Nodes)
    {
        if (Node.Id.IsValid() == false || NodePins.Contains(Node.Id))
        {
            OutError = FString::Printf(TEXT("Invalid or duplicate node GUID: %s"), *Graph.Name);
            return false;
        }
        TSet<FGuid>& Pins = NodePins.Add(Node.Id);
        for (const FBPIRPin& Pin : Node.Pins)
        {
            if (Pin.Id.IsValid() == false || Pins.Contains(Pin.Id))
            {
                OutError = FString::Printf(TEXT("Invalid or duplicate pin GUID: %s"), *Graph.Name);
                return false;
            }
            Pins.Add(Pin.Id);
        }
    }
    const auto IsValidLink = [&NodePins](const FBPIRLink& Link)
    {
        const TSet<FGuid>* Source = NodePins.Find(Link.SourceNode);
        const TSet<FGuid>* Target = NodePins.Find(Link.TargetNode);
        return Source != nullptr && Target != nullptr
            && Source->Contains(Link.SourcePin) && Target->Contains(Link.TargetPin);
    };
    for (const FBPIRLink& Link : Graph.ExecEdges)
    {
        if (IsValidLink(Link) == false)
        {
            OutError = FString::Printf(TEXT("Invalid exec edge: %s"), *Graph.Name);
            return false;
        }
    }
    for (const FBPIRLink& Link : Graph.DataEdges)
    {
        if (IsValidLink(Link) == false)
        {
            OutError = FString::Printf(TEXT("Invalid data edge: %s"), *Graph.Name);
            return false;
        }
    }
    return true;
}

FString LinkKey(const FBPIRLink& Link)
{
    return Link.SourceNode.ToString() + TEXT("/") + Link.SourcePin.ToString()
        + TEXT("/") + Link.TargetNode.ToString() + TEXT("/") + Link.TargetPin.ToString();
}
}

bool FBPCopilotGraphJsonExporter::Export(const FBPGraphIR& IR, const FString& EngineVersion,
    FString& OutPath, FString& OutError, const FBPSemanticIR* SemanticIR)
{
    OutPath.Reset();
    OutError.Reset();
    if (IR.AssetPath.IsEmpty())
    {
        OutError = TEXT("Graph IR asset path is empty");
        return false;
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 1);
    Root->SetStringField(TEXT("format"), TEXT("blueprint_copilot_graph"));
    Root->SetStringField(TEXT("engine_version"), EngineVersion);
    Root->SetStringField(TEXT("asset_path"), IR.AssetPath);
    Root->SetStringField(TEXT("parent_class_path"), IR.ParentClassPath);
    // Graph IRは意味的な完全性を保証しない。Copilotに推測で補完させない。
    Root->SetStringField(TEXT("semantic_status"), TEXT("partial"));
    if (SemanticIR != nullptr)
    {
        // Semantic JSON v2のシリアライザーを再利用し、型情報の解釈差を避ける。
        FString SemanticJson;
        if (FBPSemanticJsonSerializer::Serialize(*SemanticIR, EngineVersion, SemanticJson) == false)
        {
            OutError = TEXT("Semantic JSON serialization failed");
            return false;
        }
        TSharedPtr<FJsonObject> SemanticRoot;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SemanticJson);
        if (FJsonSerializer::Deserialize(Reader, SemanticRoot) == false || SemanticRoot.IsValid() == false)
        {
            OutError = TEXT("Semantic JSON parsing failed");
            return false;
        }
        Root->SetObjectField(TEXT("semantic_declarations"), SemanticRoot);
    }

    TArray<TSharedPtr<FJsonValue>> Graphs;
    TArray<FBPIRGraph> SortedGraphs = IR.Graphs;
    SortedGraphs.Sort([](const FBPIRGraph& A, const FBPIRGraph& B)
    {
        return A.Path < B.Path;
    });
    for (const FBPIRGraph& Graph : SortedGraphs)
    {
        if (ValidateGraphLinks(Graph, OutError) == false)
        {
            return false;
        }
        TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
        GraphJson->SetStringField(TEXT("name"), Graph.Name);
        GraphJson->SetStringField(TEXT("path"), Graph.Path);
        GraphJson->SetStringField(TEXT("kind"), Graph.Kind);
        TArray<TSharedPtr<FJsonValue>> Nodes;
        TArray<FBPIRNode> SortedNodes = Graph.Nodes;
        SortedNodes.Sort([](const FBPIRNode& A, const FBPIRNode& B)
        {
            return A.Id.ToString() < B.Id.ToString();
        });
        for (const FBPIRNode& Node : SortedNodes)
        {
            TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
            NodeJson->SetField(TEXT("id"), GuidJson(Node.Id));
            NodeJson->SetStringField(TEXT("class_path"), Node.ClassPath);
            NodeJson->SetStringField(TEXT("title"), Node.Title);
            NodeJson->SetStringField(TEXT("function_name"), Node.FunctionName.ToString());
            NodeJson->SetStringField(TEXT("function_owner_path"), Node.FunctionOwnerPath);
            NodeJson->SetStringField(TEXT("function_path"), Node.FunctionPath);
            NodeJson->SetBoolField(TEXT("function_resolved"), Node.bFunctionResolved);
            NodeJson->SetBoolField(TEXT("function_native"), Node.bFunctionNative);
            NodeJson->SetBoolField(TEXT("function_static"), Node.bFunctionStatic);
            NodeJson->SetBoolField(TEXT("function_pure"), Node.bFunctionPure);
            NodeJson->SetBoolField(TEXT("function_const"), Node.bFunctionConst);
            NodeJson->SetStringField(TEXT("macro_graph_name"), Node.MacroGraphName);
            NodeJson->SetStringField(TEXT("macro_graph_path"), Node.MacroGraphPath);
            NodeJson->SetStringField(TEXT("variable_name"), Node.VariableName.ToString());
            TArray<TSharedPtr<FJsonValue>> Pins;
            TArray<FBPIRPin> SortedPins = Node.Pins;
            SortedPins.Sort([](const FBPIRPin& A, const FBPIRPin& B)
            {
                return A.Id.ToString() < B.Id.ToString();
            });
            for (const FBPIRPin& Pin : SortedPins)
            {
                TSharedRef<FJsonObject> PinJson = MakeShared<FJsonObject>();
                PinJson->SetField(TEXT("id"), GuidJson(Pin.Id));
                PinJson->SetStringField(TEXT("name"), Pin.Name.ToString());
                PinJson->SetStringField(TEXT("direction"), Pin.Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
                PinJson->SetStringField(TEXT("category"), Pin.Category.ToString());
                PinJson->SetStringField(TEXT("subcategory"), Pin.SubCategory.ToString());
                PinJson->SetStringField(TEXT("subcategory_object_path"), Pin.SubCategoryObjectPath);
                PinJson->SetStringField(TEXT("default_value"), Pin.DefaultValue);
                PinJson->SetStringField(TEXT("default_object_path"), Pin.DefaultObjectPath);
                PinJson->SetStringField(TEXT("default_text"), Pin.DefaultText);
                Pins.Add(MakeShared<FJsonValueObject>(PinJson));
            }
            NodeJson->SetArrayField(TEXT("pins"), Pins);
            Nodes.Add(MakeShared<FJsonValueObject>(NodeJson));
        }
        GraphJson->SetArrayField(TEXT("nodes"), Nodes);
        TArray<TSharedPtr<FJsonValue>> ExecEdges;
        TArray<FBPIRLink> SortedExecEdges = Graph.ExecEdges;
        SortedExecEdges.Sort([](const FBPIRLink& A, const FBPIRLink& B)
        {
            return LinkKey(A) < LinkKey(B);
        });
        for (const FBPIRLink& Link : SortedExecEdges)
        {
            ExecEdges.Add(MakeShared<FJsonValueObject>(LinkJson(Link)));
        }
        GraphJson->SetArrayField(TEXT("exec_edges"), ExecEdges);
        TArray<TSharedPtr<FJsonValue>> DataEdges;
        TArray<FBPIRLink> SortedDataEdges = Graph.DataEdges;
        SortedDataEdges.Sort([](const FBPIRLink& A, const FBPIRLink& B)
        {
            return LinkKey(A) < LinkKey(B);
        });
        for (const FBPIRLink& Link : SortedDataEdges)
        {
            DataEdges.Add(MakeShared<FJsonValueObject>(LinkJson(Link)));
        }
        GraphJson->SetArrayField(TEXT("data_edges"), DataEdges);
        Graphs.Add(MakeShared<FJsonValueObject>(GraphJson));
    }
    Root->SetArrayField(TEXT("graphs"), Graphs);
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    TArray<FString> SortedDiagnostics = IR.Diagnostics;
    SortedDiagnostics.Sort();
    for (const FString& Message : SortedDiagnostics)
    {
        Diagnostics.Add(MakeShared<FJsonValueString>(Message));
    }
    Root->SetArrayField(TEXT("diagnostics"), Diagnostics);

    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    if (FJsonSerializer::Serialize(Root, Writer) == false)
    {
        OutError = TEXT("Copilot graph JSON serialization failed");
        return false;
    }

    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintCopilotJson"));
    if (IFileManager::Get().MakeDirectory(*Directory, true) == false)
    {
        OutError = TEXT("Could not create Copilot JSON directory");
        return false;
    }
    const FString Candidate = FPaths::Combine(Directory,
        FPaths::MakeValidFileName(IR.AssetPath) + TEXT(".copilot.graph.json"));
    const FString FullDirectory = FPaths::ConvertRelativePathToFull(Directory);
    const FString FullCandidate = FPaths::ConvertRelativePathToFull(Candidate);
    if (FPaths::IsUnderDirectory(FullCandidate, FullDirectory) == false)
    {
        OutError = TEXT("Copilot JSON path is outside the output directory");
        return false;
    }
    if (FFileHelper::SaveStringToFile(Json, *FullCandidate,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) == false)
    {
        OutError = TEXT("Could not save Copilot graph JSON");
        return false;
    }
    OutPath = FullCandidate;
    return true;
}
