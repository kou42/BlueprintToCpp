#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPConnectedFunctionCallEmitter.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPConnectedFunctionCallEmitterTest,
    "BlueprintAIConverter.SemanticIR.Emitter.ConnectedFunctionCall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPConnectedFunctionCallEmitterTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FString Statement = TEXT("stale");
    TArray<FBPSemanticDiagnostic> Diagnostics;
    const FGuid CallId = FGuid::NewGuid();

    TestFalse(TEXT("Null function rejected"),
        FBPConnectedFunctionCallEmitter::Emit(IR, CallId, nullptr, Statement, Diagnostics));
    TestTrue(TEXT("Failure clears statement"), Statement.IsEmpty());
    TestTrue(TEXT("Failure reports diagnostic"), Diagnostics.IsEmpty() == false);

    Diagnostics.Reset();
    const UFunction* Function = AActor::StaticClass()->FindFunctionByName(TEXT("K2_SetActorLocation"));
    if (TestNotNull(TEXT("SetActorLocation reflected function"), Function) == false)
    {
        return false;
    }
    TestFalse(TEXT("Missing call node rejected"),
        FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
    TestTrue(TEXT("Missing call node reports diagnostic"), Diagnostics.IsEmpty() == false);

    // ノードが存在しても出力パラメータを無視した文は生成しない。
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    FBPIRNode Call;
    Call.Id = CallId;
    Call.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Call.FunctionName = TEXT("K2_SetActorLocation");
    Graph.Nodes.Add(Call);
    IR.Graphs.Add(Graph);
    Diagnostics.Reset();
    Statement = TEXT("stale");
    TestFalse(TEXT("Unsupported call signature rejected"),
        FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
    TestTrue(TEXT("Unsupported call does not leave stale output"), Statement.IsEmpty());
    TestTrue(TEXT("Unsupported call has diagnostics"), Diagnostics.IsEmpty() == false);
    if (Diagnostics.IsEmpty() == false)
    {
        TestTrue(TEXT("Output parameter is identified"),
            Diagnostics[0].Message.Contains(TEXT("SweepHitResult")));
    }
    // 同じGUIDのノードが複数存在する場合も安全に拒否する。
    IR.Graphs[0].Nodes.Add(Call);
    Diagnostics.Reset();
    Statement = TEXT("stale");
    TestFalse(TEXT("Duplicate call GUID rejected"),
        FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
    TestTrue(TEXT("Duplicate call clears statement"), Statement.IsEmpty());
    if (Diagnostics.IsEmpty() == false)
    {
        TestTrue(TEXT("Duplicate call diagnostic"),
            Diagnostics[0].Message.Contains(TEXT("ambiguous")));
    }
    else
    {
        AddError(TEXT("Duplicate call must produce a diagnostic"));
    }
    return true;
}
#endif
