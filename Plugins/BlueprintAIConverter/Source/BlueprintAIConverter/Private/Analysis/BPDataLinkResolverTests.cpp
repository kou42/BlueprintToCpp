#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPDataLinkResolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPDataLinkResolverTest,
    "BlueprintAIConverter.SemanticIR.DataLink.Resolve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPDataLinkResolverTest::RunTest(const FString& Parameters)
{
    FBPIRGraph Graph;
    FBPIRNode Source;
    Source.Id = FGuid::NewGuid();
    FBPIRPin Output;
    Output.Id = FGuid::NewGuid();
    Output.Direction = EGPD_Output;
    Output.Category = TEXT("struct");
    Output.SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
    Source.Pins.Add(Output);

    FBPIRNode Destination;
    Destination.Id = FGuid::NewGuid();
    FBPIRPin Input;
    Input.Id = FGuid::NewGuid();
    Input.Direction = EGPD_Input;
    Input.Category = TEXT("struct");
    Input.SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
    Destination.Pins.Add(Input);

    FBPIRLink Link;
    Link.SourceNode = Source.Id;
    Link.SourcePin = Output.Id;
    Link.TargetNode = Destination.Id;
    Link.TargetPin = Input.Id;
    Graph.Nodes.Add(Source);
    Graph.Nodes.Add(Destination);
    Graph.DataEdges.Add(Link);

    const FBPIRNode* ResolvedNode = nullptr;
    const FBPIRPin* ResolvedPin = nullptr;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Matching vector link resolved"),
        FBPDataLinkResolver::ResolveInput(Graph, Destination.Id, Input.Id,
            ResolvedNode, ResolvedPin, Diagnostics));
    TestTrue(TEXT("Correct source node"), ResolvedNode == &Graph.Nodes[0]);
    TestTrue(TEXT("Correct source pin"), ResolvedPin == &Graph.Nodes[0].Pins[0]);

    Graph.Nodes[1].Pins[0].SubCategoryObjectPath = TEXT("/Script/CoreUObject.Rotator");
    Diagnostics.Reset();
    TestFalse(TEXT("Mismatched struct link rejected"),
        FBPDataLinkResolver::ResolveInput(Graph, Destination.Id, Input.Id,
            ResolvedNode, ResolvedPin, Diagnostics));
    TestTrue(TEXT("Failure does not expose source"), ResolvedNode == nullptr);

    Graph.Nodes[1].Pins[0].SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
    Graph.DataEdges.Add(Link);
    Diagnostics.Reset();
    TestFalse(TEXT("Multiple sources rejected"),
        FBPDataLinkResolver::ResolveInput(Graph, Destination.Id, Input.Id,
            ResolvedNode, ResolvedPin, Diagnostics));
    return true;
}
#endif
