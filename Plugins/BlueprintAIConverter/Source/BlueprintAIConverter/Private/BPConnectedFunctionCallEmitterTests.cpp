#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPConnectedFunctionCallEmitter.h"
#include "BPOutputPinBindingPlan.h"
#include "BPOutputConsumerAssignmentEmitter.h"
#include "BPEmitterTestActor.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
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
    Diagnostics.Reset();
    Statement = TEXT("stale");
    TestFalse(TEXT("Null generated owner class rejected"),
        FBPConnectedFunctionCallEmitter::Emit(
            IR, CallId, Function, nullptr, Statement, Diagnostics));
    TestTrue(TEXT("Null owner clears statement"), Statement.IsEmpty());
    TestTrue(TEXT("Null owner reports diagnostic"), Diagnostics.IsEmpty() == false);
    Diagnostics.Reset();
    Statement = TEXT("stale");
    TestFalse(TEXT("Non Actor generated owner class rejected"),
        FBPConnectedFunctionCallEmitter::Emit(
            IR, CallId, Function, UActorComponent::StaticClass(),
            Statement, Diagnostics));
    TestTrue(TEXT("Invalid owner clears statement"), Statement.IsEmpty());
    if (Diagnostics.IsEmpty() == false)
    {
        TestTrue(TEXT("Invalid owner diagnostic"),
            Diagnostics.Last().Message.Contains(TEXT("Invalid generated Actor owner class")));
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
    // K2_SetActorLocationはbool戻り値を持つため、Validator用のピンを明示する。
    FBPIRPin ReturnPin;
    ReturnPin.Id = FGuid::NewGuid();
    ReturnPin.Name = TEXT("ReturnValue");
    ReturnPin.Direction = EGPD_Output;
    ReturnPin.Category = UEdGraphSchema_K2::PC_Boolean;
    IR.Graphs[0].Nodes[0].Pins.Add(ReturnPin);
    const FBPIRPin* OutputPin = IR.Graphs[0].Nodes[0].Pins.FindByPredicate(
        [](const FBPIRPin& Pin) { return Pin.Name == TEXT("SweepHitResult"); });
    if (TestNotNull(TEXT("SweepHitResult output pin"), OutputPin))
    {
        const FGuid OutputPinId = OutputPin->Id;
        const FString OutputTypePath = OutputPin->SubCategoryObjectPath;
        // 正常系: GetActorLocationの戻り値をNewLocationへ接続する。
        // 出力HitResultと戻り値は未接続のままにし、生成文を厳密に検証する。
        FBPIRNode LocationSource;
        LocationSource.Id = FGuid::NewGuid();
        LocationSource.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
        LocationSource.FunctionName = TEXT("K2_GetActorLocation");
        FBPIRPin LocationReturn;
        LocationReturn.Id = FGuid::NewGuid();
        LocationReturn.Name = TEXT("ReturnValue");
        LocationReturn.Direction = EGPD_Output;
        LocationReturn.Category = UEdGraphSchema_K2::PC_Struct;
        LocationReturn.SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
        LocationSource.Pins.Add(LocationReturn);
        const FBPIRPin* LocationInput = IR.Graphs[0].Nodes[0].Pins.FindByPredicate(
            [](const FBPIRPin& Pin) { return Pin.Name == TEXT("NewLocation"); });
        if (TestNotNull(TEXT("NewLocation input pin"), LocationInput) == false)
        {
            return false;
        }
        FBPIRLink LocationLink;
        LocationLink.SourceNode = LocationSource.Id;
        LocationLink.SourcePin = LocationReturn.Id;
        LocationLink.TargetNode = CallId;
        LocationLink.TargetPin = LocationInput->Id;
        IR.Graphs[0].Nodes.Add(LocationSource);
        IR.Graphs[0].DataEdges.Add(LocationLink);
        for (FBPIRPin& Pin : IR.Graphs[0].Nodes[0].Pins)
        {
            if (Pin.Name == TEXT("bSweep"))
            {
                Pin.DefaultValue = TEXT("true");
            }
            if (Pin.Name == TEXT("bTeleport"))
            {
                Pin.DefaultValue = TEXT("false");
            }
        }
        Diagnostics.Reset();
        Statement = TEXT("stale");
        TestTrue(TEXT("Unconnected HitResult call emitted"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        TestTrue(TEXT("Storage declaration generated"),
            Statement.Contains(TEXT("FHitResult BP_Hit_")));
        TestTrue(TEXT("Call contains correctly ordered arguments"),
            Statement.Contains(TEXT("K2_SetActorLocation(GetActorLocation(), true, BP_Hit_")));
        TestTrue(TEXT("Teleport argument preserved"),
            Statement.Contains(TEXT(", false);")));
        TestTrue(TEXT("Successful emission has no diagnostics"), Diagnostics.IsEmpty());
        // 正常系: HitResult出力を同じActorの反射済みメンバーへ代入する。
        // 宣言、関数呼び出し、後続の代入がこの順に実際の改行で生成されることを確認する。
        FBPIRNode PositiveConsumer;
        PositiveConsumer.Id = FGuid::NewGuid();
        PositiveConsumer.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_VariableSet");
        PositiveConsumer.VariableName = TEXT("TestHitResult");
        FBPIRPin PositiveInput;
        PositiveInput.Id = FGuid::NewGuid();
        PositiveInput.Name = TEXT("TestHitResult");
        PositiveInput.Direction = EGPD_Input;
        PositiveInput.Category = UEdGraphSchema_K2::PC_Struct;
        PositiveInput.SubCategoryObjectPath = OutputTypePath;
        PositiveConsumer.Pins.Add(PositiveInput);
        IR.Graphs[0].Nodes.Add(PositiveConsumer);
        FBPIRLink PositiveDataEdge;
        PositiveDataEdge.SourceNode = CallId;
        PositiveDataEdge.SourcePin = OutputPinId;
        PositiveDataEdge.TargetNode = PositiveConsumer.Id;
        PositiveDataEdge.TargetPin = PositiveInput.Id;
        IR.Graphs[0].DataEdges.Add(PositiveDataEdge);
        FBPIRLink PositiveExecEdge;
        PositiveExecEdge.SourceNode = CallId;
        PositiveExecEdge.SourcePin = FGuid::NewGuid();
        PositiveExecEdge.TargetNode = PositiveConsumer.Id;
        PositiveExecEdge.TargetPin = FGuid::NewGuid();
        IR.Graphs[0].ExecEdges.Add(PositiveExecEdge);
        Diagnostics.Reset();
        Statement.Reset();
        TestTrue(TEXT("Reflected HitResult variable set emitted"),
            FBPConnectedFunctionCallEmitter::Emit(
                IR, CallId, Function, ABPEmitterTestActor::StaticClass(),
                Statement, Diagnostics));
        const int32 DeclarationPos = Statement.Find(TEXT("FHitResult BP_Hit_"));
        const int32 CallPos = Statement.Find(TEXT("\nK2_SetActorLocation("));
        const int32 AssignmentPos = Statement.Find(TEXT("\nTestHitResult = BP_Hit_"));
        TestTrue(TEXT("Declaration precedes call and assignment"),
            DeclarationPos != INDEX_NONE && CallPos > DeclarationPos &&
            AssignmentPos > CallPos);
        TestTrue(TEXT("Positive lowering has no diagnostics"), Diagnostics.IsEmpty());
        IR.Graphs[0].ExecEdges.Pop();
        IR.Graphs[0].DataEdges.Pop();
        IR.Graphs[0].Nodes.Pop();

        // 戻り値を利用するグラフは、結果の保持が未実装なので拒否する。
        FBPIRLink ReturnLink;
        ReturnLink.SourceNode = CallId;
        ReturnLink.SourcePin = ReturnPin.Id;
        ReturnLink.TargetNode = FGuid::NewGuid();
        ReturnLink.TargetPin = FGuid::NewGuid();
        IR.Graphs[0].DataEdges.Add(ReturnLink);
        Diagnostics.Reset();
        TestFalse(TEXT("Connected return value rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        TestTrue(TEXT("Connected return clears output"), Statement.IsEmpty());
        IR.Graphs[0].DataEdges.Pop();
        // bool接続を既定値に置き換えない。
        const FBPIRPin* SweepPin = IR.Graphs[0].Nodes[0].Pins.FindByPredicate(
            [](const FBPIRPin& Pin) { return Pin.Name == TEXT("bSweep"); });
        if (TestNotNull(TEXT("Sweep input pin"), SweepPin) == false)
        {
            return false;
        }
        FBPIRLink SweepLink;
        SweepLink.SourceNode = LocationSource.Id;
        SweepLink.SourcePin = LocationReturn.Id;
        SweepLink.TargetNode = CallId;
        SweepLink.TargetPin = SweepPin->Id;
        IR.Graphs[0].DataEdges.Add(SweepLink);
        Diagnostics.Reset();
        TestFalse(TEXT("Connected bool input rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        TestTrue(TEXT("Connected bool clears output"), Statement.IsEmpty());
        IR.Graphs[0].DataEdges.Pop();
        Diagnostics.Reset();
        TestTrue(TEXT("Unconnected output accepted"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        TestTrue(TEXT("Unconnected output emits statement"), Statement.IsEmpty() == false);

        FBPIRLink Link;
        Link.SourceNode = CallId;
        Link.SourcePin = OutputPinId;
        Link.TargetNode = FGuid::NewGuid();
        Link.TargetPin = FGuid::NewGuid();
        IR.Graphs[0].DataEdges.Add(Link);
        Diagnostics.Reset();
        TestFalse(TEXT("Dangling output link rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        if (Diagnostics.IsEmpty() == false)
        {
            TestTrue(TEXT("Dangling link diagnostic"),
                Diagnostics.Last().Message.Contains(TEXT("Invalid output parameter data edge target")));
        }

        // 実在する入力ピンを接続先に指定した場合だけ、接続済みとして扱う。
        FBPIRNode Consumer;
        Consumer.Id = Link.TargetNode;
        FBPIRPin ConsumerInput;
        ConsumerInput.Id = Link.TargetPin;
        ConsumerInput.Name = TEXT("Input");
        ConsumerInput.Direction = EGPD_Input;
        ConsumerInput.Category = UEdGraphSchema_K2::PC_Struct;
        ConsumerInput.SubCategoryObjectPath = OutputTypePath;
        Consumer.Pins.Add(ConsumerInput);
        IR.Graphs[0].Nodes.Add(Consumer);
        Diagnostics.Reset();
        TestFalse(TEXT("Connected output without Exec order rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        TestTrue(TEXT("Missing Exec order clears output"), Statement.IsEmpty());
        if (Diagnostics.IsEmpty() == false)
        {
            TestTrue(TEXT("Missing Exec order diagnostic"),
                Diagnostics.Last().Message.Contains(TEXT("Invalid output Exec order")));
        }
        // 接続済み出力のデータフローは、Exec順序が正しくてもまだ生成しない。
        FBPIRLink OrderedExecLink;
        OrderedExecLink.SourceNode = CallId;
        OrderedExecLink.TargetNode = Consumer.Id;
        OrderedExecLink.SourcePin = FGuid::NewGuid();
        OrderedExecLink.TargetPin = FGuid::NewGuid();
        IR.Graphs[0].ExecEdges.Add(OrderedExecLink);
        Diagnostics.Reset();
        TestFalse(TEXT("Connected output rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        if (Diagnostics.IsEmpty() == false)
        {
            TestTrue(TEXT("Connected output diagnostic"),
                Diagnostics.Last().Message.Contains(TEXT("Unsupported output consumer lowering")));
        }
        // Exec順序が正しくても、接続先ピンの所属・型が不正なら
        // Emitterは参照解決に進む前に安全に拒否する。
        IR.Graphs[0].Nodes.Last().Pins[0].Direction = EGPD_Output;
        Diagnostics.Reset();
        TestFalse(TEXT("Emitter rejects reversed consumer pin"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        TestTrue(TEXT("Emitter clears statement on invalid consumer"), Statement.IsEmpty());
        IR.Graphs[0].Nodes.Last().Pins[0].Direction = EGPD_Input;
        // 以下は独立した順序検証テストのため、EmitterテストのExecリンクを除去する。
        IR.Graphs[0].ExecEdges.Reset();
        // 接続先の実行順序が保証されるか、生成前に独立して検証する。
        FBPOutputPinBinding Binding;
        FString BindingError;
        FBPSemanticType HitType;
        HitType.Kind = EBPSemanticTypeKind::Struct;
        HitType.TypePath = OutputTypePath;
        TestTrue(TEXT("Connected HitResult binding built"),
            FBPOutputPinBindingPlan::Build(IR.Graphs[0], CallId, OutputPinId,
                TEXT("BP_Hit_Test"), HitType, Binding, BindingError));
        // ソースピンが変化した場合はBindingの再構築を拒否する。
        FBPIRPin* MutableOutputPin = IR.Graphs[0].Nodes[0].Pins.FindByPredicate(
            [OutputPinId](const FBPIRPin& Pin) { return Pin.Id == OutputPinId; });
        if (TestNotNull(TEXT("Mutable output pin"), MutableOutputPin) == false)
        {
            return false;
        }
        MutableOutputPin->SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
        FBPOutputPinBinding InvalidSourceBinding;
        TestFalse(TEXT("Mismatched source pin type rejected"),
            FBPOutputPinBindingPlan::Build(IR.Graphs[0], CallId, OutputPinId,
                TEXT("BP_Hit_Test"), HitType, InvalidSourceBinding, BindingError));
        MutableOutputPin->SubCategoryObjectPath = OutputTypePath;
        TestFalse(TEXT("Consumer without Exec order rejected"),
            FBPOutputPinBindingPlan::ValidateExecOrder(IR.Graphs[0], Binding, BindingError));
        FBPIRLink ExecLink;
        ExecLink.SourceNode = CallId;
        ExecLink.TargetNode = Consumer.Id;
        ExecLink.SourcePin = FGuid::NewGuid();
        ExecLink.TargetPin = FGuid::NewGuid();
        IR.Graphs[0].ExecEdges.Add(ExecLink);
        TestTrue(TEXT("Unique downstream Exec chain accepted"),
            FBPOutputPinBindingPlan::ValidateExecOrder(IR.Graphs[0], Binding, BindingError));
        TArray<FGuid> DerivedOrder;
        TestTrue(TEXT("Statement order derived from unique Exec chain"),
            FBPOutputPinBindingPlan::BuildStatementOrder(
                IR.Graphs[0], Binding, DerivedOrder, BindingError));
        TestEqual(TEXT("Derived statement count"), DerivedOrder.Num(), 2);
        if (DerivedOrder.Num() == 2)
        {
            TestEqual(TEXT("Source statement precedes consumer"), DerivedOrder[0], CallId);
            TestEqual(TEXT("Consumer statement follows source"), DerivedOrder[1], Consumer.Id);
        }
        TArray<FGuid> ValidStatementOrder;
        ValidStatementOrder.Add(CallId);
        ValidStatementOrder.Add(Consumer.Id);
        FString ResolvedVariable = TEXT("stale");
        TestTrue(TEXT("Consumer variable resolved with valid Exec and scope"),
            FBPOutputPinBindingPlan::ResolveConsumerVariable(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                ValidStatementOrder, ResolvedVariable, BindingError));
        TestEqual(TEXT("Resolved variable matches binding"), ResolvedVariable, Binding.VariableName);
        ResolvedVariable = TEXT("stale");
        TestFalse(TEXT("Unknown consumer pin rejected"),
            FBPOutputPinBindingPlan::ResolveConsumerVariable(
                IR.Graphs[0], Binding, Consumer.Id, FGuid::NewGuid(),
                ValidStatementOrder, ResolvedVariable, BindingError));
        TestTrue(TEXT("Failed variable resolution clears output"), ResolvedVariable.IsEmpty());
        // Binding作成後にDataEdgeが変更された場合、古い変数参照を許可しない。
        IR.Graphs[0].DataEdges.Pop();
        ResolvedVariable = TEXT("stale");
        TestFalse(TEXT("Stale consumer edge rejected"),
            FBPOutputPinBindingPlan::ResolveConsumerVariable(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                ValidStatementOrder, ResolvedVariable, BindingError));
        TestTrue(TEXT("Stale edge clears variable"), ResolvedVariable.IsEmpty());
        IR.Graphs[0].DataEdges.Add(Link);
        IR.Graphs[0].DataEdges.Add(Link);
        TestFalse(TEXT("Duplicate consumer edge rejected at resolution"),
            FBPOutputPinBindingPlan::ResolveConsumerVariable(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                ValidStatementOrder, ResolvedVariable, BindingError));
        IR.Graphs[0].DataEdges.Pop();
        // DataEdgeが残っていても入力ピンが失われた場合や型変更は拒否する。
        IR.Graphs[0].Nodes.Last().Pins[0].Id = FGuid::NewGuid();
        TestFalse(TEXT("Removed consumer pin rejected at resolution"),
            FBPOutputPinBindingPlan::ResolveConsumerVariable(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                ValidStatementOrder, ResolvedVariable, BindingError));
        IR.Graphs[0].Nodes.Last().Pins[0].Id = ConsumerInput.Id;
        IR.Graphs[0].Nodes.Last().Pins[0].SubCategoryObjectPath =
            TEXT("/Script/CoreUObject.Vector");
        TestFalse(TEXT("Changed consumer type rejected at resolution"),
            FBPOutputPinBindingPlan::ResolveConsumerVariable(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                ValidStatementOrder, ResolvedVariable, BindingError));
        IR.Graphs[0].Nodes.Last().Pins[0].SubCategoryObjectPath = OutputTypePath;
        IR.Graphs[0].Nodes.Last().Pins[0].Direction = EGPD_Output;
        TestFalse(TEXT("Reversed consumer pin rejected at resolution"),
            FBPOutputPinBindingPlan::ResolveConsumerVariable(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                ValidStatementOrder, ResolvedVariable, BindingError));
        IR.Graphs[0].Nodes.Last().Pins[0].Direction = EGPD_Input;
        FString ExecResolvedVariable;
        TestTrue(TEXT("Consumer variable resolved from actual Exec chain"),
            FBPOutputPinBindingPlan::ResolveConsumerVariableFromExec(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                ExecResolvedVariable, BindingError));
        TestEqual(TEXT("Exec-derived variable matches binding"),
            ExecResolvedVariable, Binding.VariableName);
        IR.Graphs[0].ExecEdges.Pop();
        ExecResolvedVariable = TEXT("stale");
        TestFalse(TEXT("Exec-derived resolution rejects missing chain"),
            FBPOutputPinBindingPlan::ResolveConsumerVariableFromExec(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                ExecResolvedVariable, BindingError));
        TestTrue(TEXT("Failed Exec-derived resolution clears variable"),
            ExecResolvedVariable.IsEmpty());
        IR.Graphs[0].ExecEdges.Add(ExecLink);
        // VariableSet Loweringは実在するメンバープロパティに限定する。
        // Actorに存在しないFHitResultプロパティ名をIRに書いても代入しない。
        IR.Graphs[0].Nodes.Last().ClassPath =
            TEXT("/Script/BlueprintGraph.K2Node_VariableSet");
        IR.Graphs[0].Nodes.Last().VariableName = TEXT("NonexistentHitResult");
        IR.Graphs[0].Nodes.Last().Pins[0].Name = TEXT("NonexistentHitResult");
        FString ConsumerStatement = TEXT("stale");
        TestFalse(TEXT("Unreflected variable set is rejected"),
            FBPOutputConsumerAssignmentEmitter::Emit(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                AActor::StaticClass(), ConsumerStatement, BindingError));
        TestTrue(TEXT("Failed assignment clears statement"), ConsumerStatement.IsEmpty());
        IR.Graphs[0].Nodes.Last().ClassPath.Reset();
        IR.Graphs[0].Nodes.Last().VariableName = NAME_None;
        IR.Graphs[0].Nodes.Last().Pins[0].Name = TEXT("Input");
        TArray<FGuid> InvalidStatementOrder;
        InvalidStatementOrder.Add(Consumer.Id);
        InvalidStatementOrder.Add(CallId);
        TestFalse(TEXT("Consumer variable rejected before declaration"),
            FBPOutputPinBindingPlan::ResolveConsumerVariable(
                IR.Graphs[0], Binding, Consumer.Id, ConsumerInput.Id,
                InvalidStatementOrder, ResolvedVariable, BindingError));
        TestTrue(TEXT("Scope failure clears resolved variable"), ResolvedVariable.IsEmpty());
        FBPIRLink AlternateExec = ExecLink;
        AlternateExec.SourceNode = LocationSource.Id;
        IR.Graphs[0].ExecEdges.Add(AlternateExec);
        DerivedOrder.Reset();
        TestFalse(TEXT("Merged Exec statement order rejected"),
            FBPOutputPinBindingPlan::BuildStatementOrder(
                IR.Graphs[0], Binding, DerivedOrder, BindingError));
        TestTrue(TEXT("Failed statement order clears output"), DerivedOrder.IsEmpty());
        TestFalse(TEXT("Merged Exec path rejected"),
            FBPOutputPinBindingPlan::ValidateExecOrder(IR.Graphs[0], Binding, BindingError));
        IR.Graphs[0].ExecEdges.Pop();
        IR.Graphs[0].ExecEdges.Pop();
        // 生成C++の同一文ブロックに宣言元と利用先が正しい順序であるか確認する。
        TArray<FGuid> StatementOrder;
        StatementOrder.Add(CallId);
        StatementOrder.Add(Consumer.Id);
        TestTrue(TEXT("Output variable visible to later statement"),
            FBPOutputPinBindingPlan::ValidateVariableScope(Binding, StatementOrder, BindingError));
        StatementOrder.Swap(0, 1);
        TestFalse(TEXT("Output variable use before declaration rejected"),
            FBPOutputPinBindingPlan::ValidateVariableScope(Binding, StatementOrder, BindingError));
        StatementOrder.Reset();
        StatementOrder.Add(CallId);
        TestFalse(TEXT("Output variable use outside block rejected"),
            FBPOutputPinBindingPlan::ValidateVariableScope(Binding, StatementOrder, BindingError));
        StatementOrder.Add(Consumer.Id);
        StatementOrder.Add(CallId);
        TestFalse(TEXT("Repeated output source rejected"),
            FBPOutputPinBindingPlan::ValidateVariableScope(Binding, StatementOrder, BindingError));
        // 同じ出力ピンから同一入力ピンへの重複リンクはBindingPlanで拒否する。
        IR.Graphs[0].DataEdges.Add(Link);
        Diagnostics.Reset();
        Statement = TEXT("stale");
        TestFalse(TEXT("Duplicate output consumer rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        TestTrue(TEXT("Duplicate output consumer clears statement"), Statement.IsEmpty());
        if (Diagnostics.IsEmpty() == false)
        {
            TestTrue(TEXT("Duplicate output consumer diagnostic"),
                Diagnostics.Last().Message.Contains(TEXT("Invalid output binding plan")));
        }
        IR.Graphs[0].DataEdges.Pop();
        // ピンのカテゴリが同じでも構造体型が異なる接続は拒否する。
        IR.Graphs[0].Nodes.Last().Pins[0].SubCategoryObjectPath = TEXT("/Script/CoreUObject.Vector");
        Diagnostics.Reset();
        TestFalse(TEXT("Mismatched struct destination rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        if (Diagnostics.IsEmpty() == false)
        {
            TestTrue(TEXT("Mismatched struct destination diagnostic"),
                Diagnostics.Last().Message.Contains(TEXT("Invalid output parameter data edge target or type")));
        }
        IR.Graphs[0].Nodes.Last().Pins[0].SubCategoryObjectPath = OutputTypePath;
        // Execピンをデータ接続先として使う不正なIRも拒否する。
        IR.Graphs[0].Nodes.Last().Pins[0].Category = UEdGraphSchema_K2::PC_Exec;
        Diagnostics.Reset();
        TestFalse(TEXT("Exec destination rejected"),
            FBPConnectedFunctionCallEmitter::Emit(IR, CallId, Function, Statement, Diagnostics));
        if (Diagnostics.IsEmpty() == false)
        {
            TestTrue(TEXT("Exec destination diagnostic"),
                Diagnostics.Last().Message.Contains(TEXT("Invalid output parameter data edge target")));
        }
        IR.Graphs[0].ExecEdges.Reset();
        IR.Graphs[0].Nodes.Pop();
        IR.Graphs[0].DataEdges.Reset();
        IR.Graphs[0].Nodes.Pop();
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
