#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPConnectedFunctionArguments.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPConnectedFunctionArgumentsTest,
    "BlueprintAIConverter.SemanticIR.Emitter.ConnectedFunctionArguments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPConnectedFunctionArgumentsTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    FBPIRNode Call;
    Call.Id = FGuid::NewGuid();
    Call.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Call.FunctionName = TEXT("Example");
    Graph.Nodes.Add(Call);
    IR.Graphs.Add(Graph);

    FString Output = TEXT("stale");
    TArray<FBPSemanticDiagnostic> Diagnostics;
    TestFalse(TEXT("Null UFunction is rejected"),
        FBPConnectedFunctionArguments::Emit(IR, Call.Id, nullptr, Output, Diagnostics));
    TestTrue(TEXT("Failure clears generated arguments"), Output.IsEmpty());
    TestTrue(TEXT("Function mismatch has diagnostics"), Diagnostics.IsEmpty() == false);

    Diagnostics.Reset();
    IR.Diagnostics.Add(TEXT("Unresolved graph pin"));
    TestFalse(TEXT("Graph diagnostics reject emission"),
        FBPConnectedFunctionArguments::Emit(IR, Call.Id, nullptr, Output, Diagnostics));
    TestTrue(TEXT("Graph failure has diagnostics"), Diagnostics.IsEmpty() == false);

    IR.Diagnostics.Reset();
    Diagnostics.Reset();
    const FGuid Unknown = FGuid::NewGuid();
    TestFalse(TEXT("Unknown call node rejected"),
        FBPConnectedFunctionArguments::Emit(IR, Unknown, nullptr, Output, Diagnostics));
    TestTrue(TEXT("Missing node has diagnostics"), Diagnostics.IsEmpty() == false);

    Diagnostics.Reset();
    IR.ParentClassPath = TEXT("/Script/Engine.Pawn");
    TestFalse(TEXT("Non-Actor parent rejected"),
        FBPConnectedFunctionArguments::Emit(IR, Call.Id, nullptr, Output, Diagnostics));
    TestTrue(TEXT("Unsupported parent has diagnostics"), Diagnostics.IsEmpty() == false);
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");

    Diagnostics.Reset();
    IR.Graphs[0].Nodes.Add(Call);
    TestFalse(TEXT("Duplicate call node GUID rejected"),
        FBPConnectedFunctionArguments::Emit(IR, Call.Id, nullptr, Output, Diagnostics));
    TestTrue(TEXT("Ambiguous node has diagnostics"), Diagnostics.IsEmpty() == false);
    return true;
}
#endif
