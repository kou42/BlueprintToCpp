#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPConnectedFunctionCallEmitter.h"
#include "GameFramework/Actor.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UnrealType.h"

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

    // ノードが存在しても必要な引数ピンが欠けていれば生成しない。
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
        TestTrue(TEXT("Missing parameter pin is identified"),
            Diagnostics[0].Message.Contains(TEXT("parameter pin")));
    }
    // 関数名が一致してもCallFunction以外のノードは受理しない。
    IR.Graphs[0].Nodes[0].ClassPath = TEXT("/Script/BlueprintGraph.K2Node_VariableGet");
    Diagnostics.Reset();
    Statement = TEXT("stale");
    TestFalse(TEXT("Wrong node class rejected"),
        FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
    TestTrue(TEXT("Wrong node class clears statement"), Statement.IsEmpty());
    if (Diagnostics.IsEmpty() == false)
    {
        TestTrue(TEXT("Wrong node class diagnostic"),
            Diagnostics[0].Message.Contains(TEXT("K2Node_CallFunction")));
    }
    IR.Graphs[0].Nodes[0].ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");

    // 正しい出力ピンが存在する場合、未接続と接続済みを区別して診断する。
    // 入力引数も揃えたノードを作成し、出力引数の検証経路まで到達させる。
    IR.Graphs[0].Nodes[0].Pins.Reset();
    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(CPF_Parm) == false)
        {
            continue;
        }
        if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        FBPIRPin Pin;
        Pin.Id = FGuid::NewGuid();
        Pin.Name = Property->GetFName();
        const bool bOutput = Property->HasAnyPropertyFlags(CPF_OutParm) &&
            Property->HasAnyPropertyFlags(CPF_ConstParm) == false;
        Pin.Direction = bOutput ? EGPD_Output : EGPD_Input;
        if (CastField<FBoolProperty>(Property) != nullptr)
        {
            Pin.Category = UEdGraphSchema_K2::PC_Boolean;
        }
        else if (CastField<FStructProperty>(Property) != nullptr)
        {
            Pin.Category = UEdGraphSchema_K2::PC_Struct;
            Pin.SubCategoryObjectPath = CastField<FStructProperty>(Property)->Struct->GetPathName();
        }
        IR.Graphs[0].Nodes[0].Pins.Add(Pin);
    }
    const FBPIRPin* OutputPin = IR.Graphs[0].Nodes[0].Pins.FindByPredicate(
        [](const FBPIRPin& Pin) { return Pin.Name == TEXT("SweepHitResult"); });
    if (TestNotNull(TEXT("SweepHitResult output pin"), OutputPin))
    {
        const FGuid OutputPinId = OutputPin->Id;
        Diagnostics.Reset();
        TestFalse(TEXT("Unconnected output rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        if (Diagnostics.IsEmpty() == false)
        {
            TestTrue(TEXT("Unconnected output diagnostic"),
                Diagnostics.Last().Message.Contains(TEXT("Unconnected output parameter")));
        }

        FBPIRLink Link;
        Link.SourceNode = CallId;
        Link.SourcePin = OutputPinId;
        Link.TargetNode = FGuid::NewGuid();
        Link.TargetPin = FGuid::NewGuid();
        IR.Graphs[0].DataEdges.Add(Link);
        Diagnostics.Reset();
        TestFalse(TEXT("Connected output rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        if (Diagnostics.IsEmpty() == false)
        {
            TestTrue(TEXT("Connected output diagnostic"),
                Diagnostics.Last().Message.Contains(TEXT("Connected output parameter")));
        }
        IR.Graphs[0].DataEdges.Reset();
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
