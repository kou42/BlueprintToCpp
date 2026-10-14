#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPCopilotGraphJsonExporter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPCopilotMultiLinkOrderTest,
    "BlueprintAIConverter.CopilotJson.MultiLinkOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPCopilotMultiLinkOrderTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.AssetPath = TEXT("/Game/BP_MultiLinkOrder.BP_MultiLinkOrder");
    FBPIRGraph Graph;
    Graph.Path = TEXT("EventGraph");
    FBPIRNode Source;
    Source.Id = FGuid(1, 0, 0, 0);
    FBPIRNode Target;
    Target.Id = FGuid(2, 0, 0, 0);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FBPIRPin Out;
        Out.Id = FGuid(3 + Index, 0, 0, 0);
        Out.Direction = EGPD_Output;
        Source.Pins.Add(Out);
        FBPIRPin In;
        In.Id = FGuid(5 + Index, 0, 0, 0);
        Target.Pins.Add(In);
        FBPIRLink Link;
        Link.SourceNode = Source.Id;
        Link.SourcePin = Out.Id;
        Link.TargetNode = Target.Id;
        Link.TargetPin = In.Id;
        Graph.ExecEdges.Add(Link);
        Graph.DataEdges.Add(Link);
    }
    Graph.Nodes.Add(Source);
    Graph.Nodes.Add(Target);
    IR.Graphs.Add(Graph);

    FString Path;
    FString Error;
    if (FBPCopilotGraphJsonExporter::Export(IR, TEXT("5.7.4"), Path, Error) == false)
    {
        AddError(Error);
        return false;
    }
    FString FirstJson;
    const bool bFirstRead = FFileHelper::LoadFileToString(FirstJson, *Path);
    if (bFirstRead == false)
    {
        IFileManager::Get().Delete(*Path);
        AddError(TEXT("First JSON could not be read"));
        return false;
    }

    // ノード・Pin・Exec/Data接続の列挙順を変えても、同一JSONになることを検証する。
    IR.Graphs[0].Nodes.Swap(0, 1);
    IR.Graphs[0].Nodes[0].Pins.Swap(0, 1);
    IR.Graphs[0].Nodes[1].Pins.Swap(0, 1);
    IR.Graphs[0].ExecEdges.Swap(0, 1);
    IR.Graphs[0].DataEdges.Swap(0, 1);
    if (FBPCopilotGraphJsonExporter::Export(IR, TEXT("5.7.4"), Path, Error) == false)
    {
        IFileManager::Get().Delete(*Path);
        AddError(Error);
        return false;
    }
    FString SecondJson;
    const bool bSecondRead = FFileHelper::LoadFileToString(SecondJson, *Path);
    IFileManager::Get().Delete(*Path);
    TestTrue(TEXT("Second JSON can be read"), bSecondRead);
    if (bSecondRead)
    {
        TestEqual(TEXT("Multi-link JSON output is deterministic"), FirstJson, SecondJson);
    }
    return true;
}
#endif
