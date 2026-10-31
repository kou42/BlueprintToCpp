#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPActorCodeGenerator.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticJsonSerializer.h"
#include "BPSemanticLowering.h"
#include "BPReflectedCallResolver.h"
#include "Tests/BPEmitterTestActor.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Pawn.h"
#include "UObject/Class.h"

namespace
{
FBPGraphIR MakeGeneratorTestGraph()
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Name = TEXT("EventGraph");
    Graph.Kind = TEXT("event");
    FBPIRNode Event;
    Event.Id = FGuid::NewGuid();
    Event.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Event.FunctionName = TEXT("ReceiveBeginPlay");
    FBPIRNode Print;
    Print.Id = FGuid::NewGuid();
    Print.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Print.FunctionName = TEXT("PrintString");

    FBPIRPin Text;
    Text.Id = FGuid::NewGuid();
    Text.Name = TEXT("InString");
    Text.Category = TEXT("string");
    Text.Direction = EGPD_Input;
    Text.DefaultValue = TEXT("Hello \"UE\"");
    Print.Pins.Add(Text);

    FBPIRPin Color;
    Color.Id = FGuid::NewGuid();
    Color.Name = TEXT("TextColor");
    Color.Direction = EGPD_Input;
    Color.DefaultValue = TEXT("(R=1,G=0.25,B=0,A=1)");
    Print.Pins.Add(Color);

    FBPIRPin Duration;
    Duration.Id = FGuid::NewGuid();
    Duration.Name = TEXT("Duration");
    Duration.Direction = EGPD_Input;
    Duration.DefaultValue = TEXT("3.5");
    Print.Pins.Add(Duration);

    FBPIRLink Edge;
    Edge.SourceNode = Event.Id;
    Edge.TargetNode = Print.Id;
    Graph.ExecEdges.Add(Edge);
    Graph.Nodes.Add(MoveTemp(Event));
    Graph.Nodes.Add(MoveTemp(Print));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}

FBPSemanticExpression MakeActorLocationCall(const FString& ParentClassPath,
    const FName HitResultName)
{
    FBPSemanticExpression Target;
    Target.Kind = EBPSemanticExpressionKind::VariableReference;
    Target.Type.Kind = EBPSemanticTypeKind::Object;
    Target.Type.TypePath = ParentClassPath;
    Target.VariableName = TEXT("this");

    FBPSemanticExpression Location;
    Location.Kind = EBPSemanticExpressionKind::FunctionCall;
    Location.Type.Kind = EBPSemanticTypeKind::Struct;
    Location.Type.TypePath = TEXT("/Script/CoreUObject.Vector");
    Location.FunctionName = TEXT("K2_GetActorLocation");
    Location.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Location.Arguments.Add(Target);

    FBPSemanticExpression BoolLiteral;
    BoolLiteral.Kind = EBPSemanticExpressionKind::Literal;
    BoolLiteral.Type.Kind = EBPSemanticTypeKind::Bool;
    BoolLiteral.LiteralValue = TEXT("false");

    FBPSemanticExpression HitResult;
    HitResult.Kind = EBPSemanticExpressionKind::VariableReference;
    HitResult.Type.Kind = EBPSemanticTypeKind::Struct;
    HitResult.Type.TypePath = TEXT("/Script/Engine.HitResult");
    HitResult.VariableName = HitResultName;

    FBPSemanticExpression Call;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Type.Kind = EBPSemanticTypeKind::Bool;
    Call.FunctionName = TEXT("K2_SetActorLocation");
    Call.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Call.Arguments.Add(MoveTemp(Target));
    Call.Arguments.Add(MoveTemp(Location));
    Call.Arguments.Add(BoolLiteral);
    Call.Arguments.Add(MoveTemp(HitResult));
    Call.Arguments.Add(MoveTemp(BoolLiteral));
    return Call;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorParityTest,
    "BlueprintAIConverter.SemanticIR.Generator.LegacyParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorParityTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPGeneratedActorFiles LegacyFiles;
    TArray<FString> LegacyDiagnostics;
    if (TestTrue(TEXT("旧Generator成功"),
        FBPActorCodeGenerator::Generate(Graph, TEXT("AParityActor"), TEXT("TEST_API"), LegacyFiles, LegacyDiagnostics)) == false)
    {
        return false;
    }
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    TestEqual(TEXT("PrintString KeyはReflectionどおりFName"),
        Semantic.Functions[0].Blocks[0].Statements[0].Expression.Arguments[6].Type.Kind,
        EBPSemanticTypeKind::Name);
    FBPGeneratedActorFiles NewFiles;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("新Generator成功"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AParityActor"), TEXT("TEST_API"), NewFiles, Diagnostics)) == false)
    {
        return false;
    }
    TestEqual(TEXT("ヘッダー名一致"), NewFiles.HeaderFileName, LegacyFiles.HeaderFileName);
    TestEqual(TEXT("ソース名一致"), NewFiles.SourceFileName, LegacyFiles.SourceFileName);
    TestEqual(TEXT("ヘッダー一致"), NewFiles.HeaderContent, LegacyFiles.HeaderContent);
    TestEqual(TEXT("ソース一致"), NewFiles.SourceContent, LegacyFiles.SourceContent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorRejectTest,
    "BlueprintAIConverter.SemanticIR.Generator.RejectUnsupported",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorRejectTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    Semantic.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    Statement.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Statement.Expression.FunctionName = TEXT("UnsupportedFunction");
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    Semantic.Functions.Add(Function);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("未対応関数を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AParityActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("診断あり"), Diagnostics.IsEmpty() == false);
    TestTrue(TEXT("失敗時にソースを公開しない"), Files.SourceContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorGenericBranchStatementsTest,
    "BlueprintAIConverter.SemanticIR.Generator.GenericBranchStatements",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorGenericBranchStatementsTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("テスト用Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticStatement& Statement = Semantic.Functions[0].Blocks[0].Statements[0];
    Statement.Kind = EBPSemanticStatementKind::Branch;
    Statement.Expression = FBPSemanticExpression();
    Statement.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Statement.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Statement.Expression.LiteralValue = TEXT("true");

    FBPSemanticStatement ThenStatement;
    ThenStatement.Kind = EBPSemanticStatementKind::Expression;
    ThenStatement.Expression = MakeActorLocationCall(
        Semantic.ParentClassPath, TEXT("ThenHitResult"));
    Statement.ThenStatements.Add(MoveTemp(ThenStatement));

    FBPSemanticStatement ElseStatement;
    ElseStatement.Kind = EBPSemanticStatementKind::Expression;
    ElseStatement.Expression = MakeActorLocationCall(
        Semantic.ParentClassPath, TEXT("ElseHitResult"));
    Statement.ElseStatements.Add(MoveTemp(ElseStatement));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Branch末端を共通式Emitterで生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AGenericBranchActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("必要なHitResultヘッダーを収集"),
        Files.SourceContent.Contains(TEXT("#include \"Engine/HitResult.h\"")));
    TestTrue(TEXT("True側の汎用式文"), Files.SourceContent.Contains(
        TEXT("FHitResult ThenHitResult{};")));
    TestTrue(TEXT("False側の汎用式文"), Files.SourceContent.Contains(
        TEXT("FHitResult ElseHitResult{};")));
    TestFalse(TEXT("Branch生成がPrintStringに依存しない"),
        Files.SourceContent.Contains(TEXT("UKismetSystemLibrary::PrintString")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorBasicBlockBranchCfgTest,
    "BlueprintAIConverter.SemanticIR.Generator.BasicBlockBranchCfg",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorBasicBlockBranchCfgTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("CFGテスト用Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    auto MakeCall = [&Semantic](const FName HitResultName)
    {
        FBPSemanticStatement Statement;
        Statement.Kind = EBPSemanticStatementKind::Expression;
        Statement.Expression = MakeActorLocationCall(
            Semantic.ParentClassPath, HitResultName);
        return Statement;
    };

    FBPSemanticStatement Branch;
    Branch.Kind = EBPSemanticStatementKind::Branch;
    Branch.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Branch.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Branch.Expression.LiteralValue = TEXT("true");

    FBPSemanticBasicBlock EntryBlock;
    EntryBlock.BlockId = FGuid::NewGuid();
    EntryBlock.Terminator = EBPSemanticBlockTerminatorKind::Branch;
    EntryBlock.Statements.Add(Branch);
    FBPSemanticBasicBlock TrueBlock;
    TrueBlock.BlockId = FGuid::NewGuid();
    TrueBlock.Terminator = EBPSemanticBlockTerminatorKind::Fallthrough;
    TrueBlock.Statements.Add(MakeCall(TEXT("CfgTrue")));
    FBPSemanticBasicBlock FalseBlock;
    FalseBlock.BlockId = FGuid::NewGuid();
    FalseBlock.Terminator = EBPSemanticBlockTerminatorKind::Fallthrough;
    FalseBlock.Statements.Add(MakeCall(TEXT("CfgFalse")));
    FBPSemanticBasicBlock JoinBlock;
    JoinBlock.BlockId = FGuid::NewGuid();
    JoinBlock.Terminator = EBPSemanticBlockTerminatorKind::End;
    JoinBlock.Statements.Add(MakeCall(TEXT("CfgJoin")));
    EntryBlock.SuccessorBlockIds = { TrueBlock.BlockId, FalseBlock.BlockId };
    TrueBlock.SuccessorBlockIds.Add(JoinBlock.BlockId);
    FalseBlock.SuccessorBlockIds.Add(JoinBlock.BlockId);

    FBPSemanticFunction& Function = Semantic.Functions[0];
    Function.Blocks.Reset();
    Function.Blocks.Add(EntryBlock);
    Function.Blocks.Add(TrueBlock);
    Function.Blocks.Add(FalseBlock);
    Function.Blocks.Add(JoinBlock);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Branch CFGからC++を生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ABranchCfgActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const FString& Source = Files.SourceContent;
    const int32 TruePosition = Source.Find(TEXT("FHitResult CfgTrue{};"));
    const int32 FalsePosition = Source.Find(TEXT("FHitResult CfgFalse{};"));
    const int32 JoinPosition = Source.Find(TEXT("FHitResult CfgJoin{};"));
    TestTrue(TEXT("True/Falseをif/else内へ生成"),
        Source.Contains(TEXT("if (true)")) && Source.Contains(TEXT("else")) &&
        TruePosition != INDEX_NONE && FalsePosition != INDEX_NONE);
    TestTrue(TEXT("合流Blockを分岐後に一度だけ生成"),
        JoinPosition > TruePosition && JoinPosition > FalsePosition &&
        Source.Find(TEXT("FHitResult CfgJoin{};"), ESearchCase::CaseSensitive,
            ESearchDir::FromEnd) == JoinPosition);

    FString Json;
    TestTrue(TEXT("Branch CFGをSemantic JSONへ出力"),
        FBPSemanticJsonSerializer::Serialize(Semantic, TEXT("5.7"), Json));
    TestTrue(TEXT("JSONにBranch終端を保持"),
        Json.Contains(TEXT("\"terminator\": \"branch\"")));
    TestTrue(TEXT("JSONにTrue/False後続を保持"),
        Json.Contains(TrueBlock.BlockId.ToString(EGuidFormats::DigitsWithHyphensLower)) &&
        Json.Contains(FalseBlock.BlockId.ToString(EGuidFormats::DigitsWithHyphensLower)));

    FBPSemanticIR Invalid = Semantic;
    Invalid.Functions[0].Blocks[2].SuccessorBlockIds[0] = FGuid::NewGuid();
    Diagnostics.Reset();
    TestFalse(TEXT("異なる合流先を持つBranch CFGを拒否"),
        FBPSemanticCodeGenerator::Generate(Invalid, TEXT("ABranchCfgActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("不正合流の診断を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("must terminate or share one join block"));
        }));
    TestTrue(TEXT("拒否時に生成物を公開しない"),
        Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorBasicBlockLoopCfgTest,
    "BlueprintAIConverter.SemanticIR.Generator.BasicBlockLoopCfg",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorBasicBlockLoopCfgTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Loop CFGテスト用Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    const FBPSemanticStatement OriginalBody =
        Semantic.Functions[0].Blocks[0].Statements[0];
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForLoop;
    Loop.LoopIndexVariable = TEXT("CfgIndex");
    Loop.LoopFirstIndex.Kind = EBPSemanticExpressionKind::Literal;
    Loop.LoopFirstIndex.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopFirstIndex.LiteralValue = TEXT("1");
    Loop.LoopLastIndex.Kind = EBPSemanticExpressionKind::Literal;
    Loop.LoopLastIndex.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopLastIndex.LiteralValue = TEXT("3");

    FBPSemanticStatement Completed;
    Completed.Kind = EBPSemanticStatementKind::Expression;
    Completed.Expression = MakeActorLocationCall(
        Semantic.ParentClassPath, TEXT("LoopCfgCompleted"));

    FBPSemanticBasicBlock HeaderBlock;
    HeaderBlock.BlockId = FGuid::NewGuid();
    HeaderBlock.Terminator = EBPSemanticBlockTerminatorKind::Loop;
    HeaderBlock.Statements.Add(Loop);
    FBPSemanticBasicBlock BodyBlock;
    BodyBlock.BlockId = FGuid::NewGuid();
    BodyBlock.Terminator = EBPSemanticBlockTerminatorKind::Fallthrough;
    BodyBlock.Statements.Add(OriginalBody);
    FBPSemanticBasicBlock CompletedBlock;
    CompletedBlock.BlockId = FGuid::NewGuid();
    CompletedBlock.Terminator = EBPSemanticBlockTerminatorKind::End;
    CompletedBlock.Statements.Add(Completed);
    HeaderBlock.SuccessorBlockIds = { BodyBlock.BlockId, CompletedBlock.BlockId };
    BodyBlock.SuccessorBlockIds.Add(HeaderBlock.BlockId);

    FBPSemanticFunction& Function = Semantic.Functions[0];
    Function.Blocks.Reset();
    Function.Blocks.Add(HeaderBlock);
    Function.Blocks.Add(BodyBlock);
    Function.Blocks.Add(CompletedBlock);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Loop CFGからC++を生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ALoopCfgActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const FString& Source = Files.SourceContent;
    const int32 LoopPosition = Source.Find(
        TEXT("for (int32 CfgIndex = 1; CfgIndex <= 3; ++CfgIndex)"));
    const int32 BodyPosition = Source.Find(TEXT("UKismetSystemLibrary::PrintString"));
    const int32 CompletedPosition = Source.Find(TEXT("FHitResult LoopCfgCompleted{};"));
    TestTrue(TEXT("Loop Bodyをfor内へ生成"),
        LoopPosition != INDEX_NONE && BodyPosition > LoopPosition);
    TestTrue(TEXT("Completed BlockをLoop後へ生成"),
        CompletedPosition > BodyPosition);

    FString Json;
    TestTrue(TEXT("Loop CFGをSemantic JSONへ出力"),
        FBPSemanticJsonSerializer::Serialize(Semantic, TEXT("5.7"), Json));
    TestTrue(TEXT("JSONにLoop終端を保持"),
        Json.Contains(TEXT("\"terminator\": \"loop\"")));
    TestTrue(TEXT("JSONにBody back-edgeを保持"),
        Json.Contains(HeaderBlock.BlockId.ToString(
            EGuidFormats::DigitsWithHyphensLower)));

    FBPSemanticIR BranchSemantic = Semantic;
    FBPSemanticBasicBlock BranchHeader = HeaderBlock;
    FBPSemanticBasicBlock BodyBranchBlock;
    BodyBranchBlock.BlockId = FGuid::NewGuid();
    BodyBranchBlock.Terminator = EBPSemanticBlockTerminatorKind::Branch;
    FBPSemanticStatement BodyBranch;
    BodyBranch.Kind = EBPSemanticStatementKind::Branch;
    BodyBranch.Expression.Kind = EBPSemanticExpressionKind::Literal;
    BodyBranch.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    BodyBranch.Expression.LiteralValue = TEXT("true");
    BodyBranchBlock.Statements.Add(BodyBranch);
    FBPSemanticBasicBlock TrueBodyBlock;
    TrueBodyBlock.BlockId = FGuid::NewGuid();
    TrueBodyBlock.Terminator = EBPSemanticBlockTerminatorKind::Fallthrough;
    TrueBodyBlock.Statements.Add(OriginalBody);
    TrueBodyBlock.SuccessorBlockIds.Add(BranchHeader.BlockId);
    FBPSemanticBasicBlock FalseBodyBlock;
    FalseBodyBlock.BlockId = FGuid::NewGuid();
    FalseBodyBlock.Terminator = EBPSemanticBlockTerminatorKind::Fallthrough;
    FBPSemanticStatement FalseBody;
    FalseBody.Kind = EBPSemanticStatementKind::Expression;
    FalseBody.Expression = MakeActorLocationCall(
        BranchSemantic.ParentClassPath, TEXT("LoopBranchFalse"));
    FalseBodyBlock.Statements.Add(FalseBody);
    FalseBodyBlock.SuccessorBlockIds.Add(BranchHeader.BlockId);
    FBPSemanticBasicBlock BranchCompletedBlock = CompletedBlock;
    BranchHeader.SuccessorBlockIds = {
        BodyBranchBlock.BlockId, BranchCompletedBlock.BlockId };
    BodyBranchBlock.SuccessorBlockIds = {
        TrueBodyBlock.BlockId, FalseBodyBlock.BlockId };
    BranchSemantic.Functions[0].Blocks.Reset();
    BranchSemantic.Functions[0].Blocks.Add(BranchHeader);
    BranchSemantic.Functions[0].Blocks.Add(BodyBranchBlock);
    BranchSemantic.Functions[0].Blocks.Add(TrueBodyBlock);
    BranchSemantic.Functions[0].Blocks.Add(FalseBodyBlock);
    BranchSemantic.Functions[0].Blocks.Add(BranchCompletedBlock);

    Diagnostics.Reset();
    if (TestTrue(TEXT("Loop Body Branch CFGからC++を生成"),
        FBPSemanticCodeGenerator::Generate(
            BranchSemantic, TEXT("ALoopBranchCfgActor"), FString(),
            Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("Loop Body Branchをifへ生成"),
        Files.SourceContent.Contains(TEXT("if (true)")) &&
        Files.SourceContent.Contains(TEXT("LoopBranchFalse")));
    FString BranchJson;
    TestTrue(TEXT("Loop Body Branch CFGをSemantic JSONへ出力"),
        FBPSemanticJsonSerializer::Serialize(
            BranchSemantic, TEXT("5.7"), BranchJson));
    TestTrue(TEXT("JSONにLoop内Branch終端を保持"),
        BranchJson.Contains(TEXT("\"terminator\": \"branch\"")));

    FBPSemanticIR InvalidBranch = BranchSemantic;
    InvalidBranch.Functions[0].Blocks[3].SuccessorBlockIds[0] =
        InvalidBranch.Functions[0].Blocks[4].BlockId;
    Diagnostics.Reset();
    TestFalse(TEXT("片側back-edgeを欠くLoop Body Branchを拒否"),
        FBPSemanticCodeGenerator::Generate(
            InvalidBranch, TEXT("ALoopBranchCfgActor"), FString(),
            Files, Diagnostics));
    TestTrue(TEXT("Loop Body Branch拒否理由を保持"),
        Diagnostics.ContainsByPredicate(
            [](const FString& Message)
            {
                return Message.Contains(TEXT("requires True/False back-edges"));
            }));

    FBPSemanticIR Invalid = Semantic;
    Invalid.Functions[0].Blocks[1].SuccessorBlockIds[0] =
        Invalid.Functions[0].Blocks[2].BlockId;
    Diagnostics.Reset();
    TestFalse(TEXT("Body back-edgeを欠くLoop CFGを拒否"),
        FBPSemanticCodeGenerator::Generate(Invalid, TEXT("ALoopCfgActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("不正back-edgeの診断を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("requires a Body back-edge"));
        }));
    TestTrue(TEXT("拒否時に生成物を公開しない"),
        Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorNestedBranchStatementsTest,
    "BlueprintAIConverter.SemanticIR.Generator.NestedBranchStatements",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorNestedBranchStatementsTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("テスト用Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    Outer.Kind = EBPSemanticStatementKind::Branch;
    Outer.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Outer.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Outer.Expression.LiteralValue = TEXT("true");

    // 処理1、処理2、内側if、処理5、処理6の順序とスコープを確認する。
    auto MakeCall = [&Semantic](const FName HitResultName)
    {
        FBPSemanticStatement Statement;
        Statement.Kind = EBPSemanticStatementKind::Expression;
        Statement.Expression = MakeActorLocationCall(Semantic.ParentClassPath, HitResultName);
        return Statement;
    };
    Outer.ThenStatements.Add(MakeCall(TEXT("Step1")));
    Outer.ThenStatements.Add(MakeCall(TEXT("Step2")));
    FBPSemanticStatement Inner;
    Inner.Kind = EBPSemanticStatementKind::Branch;
    Inner.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Inner.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Inner.Expression.LiteralValue = TEXT("false");
    Inner.ThenStatements.Add(MakeCall(TEXT("Step3")));
    Inner.ThenStatements.Add(MakeCall(TEXT("Step4")));
    Outer.ThenStatements.Add(MoveTemp(Inner));
    Outer.ThenStatements.Add(MakeCall(TEXT("Step5")));
    Outer.ThenStatements.Add(MakeCall(TEXT("Step6")));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ネストしたBranchのC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ANestedBranchActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const FString& Source = Files.SourceContent;
    const int32 Step1 = Source.Find(TEXT("FHitResult Step1{};"));
    const int32 Step2 = Source.Find(TEXT("FHitResult Step2{};"));
    const int32 Step3 = Source.Find(TEXT("FHitResult Step3{};"));
    const int32 Step4 = Source.Find(TEXT("FHitResult Step4{};"));
    const int32 Step5 = Source.Find(TEXT("FHitResult Step5{};"));
    const int32 Step6 = Source.Find(TEXT("FHitResult Step6{};"));
    TestTrue(TEXT("処理1から処理6の生成順を維持"),
        Step1 != INDEX_NONE && Step1 < Step2 && Step2 < Step3 &&
        Step3 < Step4 && Step4 < Step5 && Step5 < Step6);
    TestTrue(TEXT("内側ifを出力"), Source.Contains(TEXT("if (false)")));
    TestTrue(TEXT("外側ifを出力"), Source.Contains(TEXT("if (true)")));

    // Branchの両枝が合流した後の文は、ifの外側へ一度だけ出力する。
    Semantic.Functions[0].Blocks[0].Statements.Add(MakeCall(TEXT("AfterJoin")));
    FBPGeneratedActorFiles JoinedFiles;
    TArray<FString> JoinedDiagnostics;
    if (TestTrue(TEXT("Branch後続文のC++生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("ANestedBranchActor"),
            FString(), JoinedFiles, JoinedDiagnostics)) == false)
    {
        for (const FString& Diagnostic : JoinedDiagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    const FString& JoinedSource = JoinedFiles.SourceContent;
    const int32 JoinPosition = JoinedSource.Find(TEXT("FHitResult AfterJoin{};"));
    TestTrue(TEXT("合流後の文は内側のifより後ろ"),
        JoinPosition > JoinedSource.Find(TEXT("FHitResult Step6{};")));
    TestTrue(TEXT("合流後の文を重複出力しない"),
        JoinedSource.Contains(TEXT("FHitResult AfterJoin{};")) &&
        JoinedSource.Find(TEXT("FHitResult AfterJoin{};"), ESearchCase::CaseSensitive,
            ESearchDir::FromEnd) == JoinPosition);
    return true;
}

// 関数名やクラス別の分岐ではなく、リフレクションと継承関係で解決する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPReflectedCallResolverInheritanceTest,
    "BlueprintAIConverter.SemanticIR.Resolver.Inheritance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPReflectedCallResolverInheritanceTest::RunTest(const FString& Parameters)
{
    FBPReflectedCall Call;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Pawn派生のselfからActorの関数を解決"),
        FBPReflectedCallResolver::Resolve(TEXT("/Script/Engine.Actor"),
            TEXT("SetActorTickEnabled"), TEXT("/Script/Engine.Pawn"),
            false, Call, Diagnostics));
    TestNotNull(TEXT("反射で解決された関数"), Call.Function);
    TestEqual(TEXT("所有クラス"), Call.Signature.OwnerClassPath,
        FString(TEXT("/Script/Engine.Actor")));

    // ActorはComponentの派生ではないためself呼び出しに変換してはいけない。
    Diagnostics.Reset();
    TestFalse(TEXT("継承関係のない所有クラスを拒否"),
        FBPReflectedCallResolver::Resolve(TEXT("/Script/Engine.ActorComponent"),
            TEXT("SetComponentTickEnabled"), TEXT("/Script/Engine.Actor"),
            false, Call, Diagnostics));
    TestNull(TEXT("失敗時に古い関数参照を残さない"), Call.Function);
    Diagnostics.Reset();
    TestTrue(TEXT("TargetがActor派生ならクラス名によらず関数を解決"),
        FBPReflectedCallResolver::ResolveForTarget(TEXT("/Script/Engine.Actor"),
            TEXT("SetActorTickEnabled"), TEXT("/Script/Engine.Pawn"),
            false, Call, Diagnostics));
    Diagnostics.Reset();
    TestFalse(TEXT("Component型TargetにActor関数を適用しない"),
        FBPReflectedCallResolver::ResolveForTarget(TEXT("/Script/Engine.Actor"),
            TEXT("SetActorTickEnabled"), TEXT("/Script/Engine.ActorComponent"),
            false, Call, Diagnostics));

    // WorldContextを持つ静的関数も、Reflectionの所有クラスと署名から解決する。
    // 専用PrintString Emitterの存在に依存しないResolverの回帰テスト。
    Diagnostics.Reset();
    TestTrue(TEXT("PrintStringの静的UFunctionをReflectionから解決"),
        FBPReflectedCallResolver::Resolve(TEXT("/Script/Engine.KismetSystemLibrary"),
            TEXT("PrintString"), TEXT("/Script/Engine.Actor"),
            true, Call, Diagnostics));
    if (Call.Function != nullptr)
    {
        TestTrue(TEXT("WorldContextメタデータを持つ"),
            Call.Function->HasMetaData(TEXT("WorldContext")));
        TestTrue(TEXT("WorldContext引数が署名に存在"),
            Call.Signature.ParameterNames.Contains(FName(TEXT("WorldContextObject"))));
        TestTrue(TEXT("WorldContextが先頭引数"),
            Call.Signature.ParameterNames.IsEmpty() == false &&
            Call.Signature.ParameterNames[0] == FName(TEXT("WorldContextObject")));
        TestTrue(TEXT("WorldContextがオブジェクト入力引数"),
            Call.Signature.ParameterTypes.IsEmpty() == false &&
            Call.Signature.ParameterTypes[0].Kind == EBPSemanticTypeKind::Object &&
            Call.Signature.OutputParameters[0] == false);
        if (Call.Signature.ParameterTypes.IsEmpty() == false)
        {
            // 参照引数として扱うとthisの値渡し規約が変わるため、
            // 共通Emitterの受理条件と同じ型制約を検証する。
            TestFalse(TEXT("WorldContextは参照引数ではない"),
                Call.Signature.ParameterTypes[0].bIsReference);
            TestEqual(TEXT("WorldContextの期待クラス"),
                Call.Signature.ParameterTypes[0].TypePath,
                FString(TEXT("/Script/CoreUObject.Object")));
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorActorNoArgBranchTest,
    "BlueprintAIConverter.SemanticIR.Generator.ActorNoArgBranch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorActorNoArgBranchTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("テスト用Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }
    FBPSemanticStatement& Outer = Semantic.Functions[0].Blocks[0].Statements[0];
    Outer.Kind = EBPSemanticStatementKind::Branch;
    Outer.Expression = FBPSemanticExpression();
    Outer.Expression.Kind = EBPSemanticExpressionKind::Literal;
    Outer.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    Outer.Expression.LiteralValue = TEXT("true");

    // PrintString以外のActor関数をBranch内部に配置できることを確認する。
    FBPSemanticStatement Call;
    Call.Kind = EBPSemanticStatementKind::Expression;
    Call.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Expression.Type.Kind = EBPSemanticTypeKind::Void;
    Call.Expression.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Call.Expression.FunctionName = TEXT("K2_DestroyActor");
    FBPSemanticExpression Self;
    Self.Kind = EBPSemanticExpressionKind::VariableReference;
    Self.Type.Kind = EBPSemanticTypeKind::Object;
    Self.Type.TypePath = Semantic.ParentClassPath;
    Self.VariableName = TEXT("this");
    Call.Expression.Arguments.Add(MoveTemp(Self));
    Outer.ThenStatements.Add(MoveTemp(Call));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("引数なしActor関数の生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AActorNoArgBranch"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("Actor関数をif内部に出力"),
        Files.SourceContent.Contains(TEXT("this->K2_DestroyActor();")));

    // リテラル引数を持つActor関数のC++呼び出しを検証する。
    FBPSemanticStatement& ActorCall = Outer.ThenStatements[0];
    ActorCall.Expression.FunctionName = TEXT("SetActorTickEnabled");
    FBPSemanticExpression Enabled;
    Enabled.Kind = EBPSemanticExpressionKind::Literal;
    Enabled.Type.Kind = EBPSemanticTypeKind::Bool;
    Enabled.LiteralValue = TEXT("false");
    ActorCall.Expression.Arguments.Add(MoveTemp(Enabled));
    FBPGeneratedActorFiles WithArgument;
    TArray<FString> ArgumentDiagnostics;
    if (TestTrue(TEXT("bool引数付きActor関数の生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AActorNoArgBranch"),
            FString(), WithArgument, ArgumentDiagnostics)) == false)
    {
        for (const FString& Diagnostic : ArgumentDiagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("bool引数を生成"),
        WithArgument.SourceContent.Contains(TEXT("this->SetActorTickEnabled(false);")));

    // Reflectionのbool引数へFString変数を誤って渡さないことを検証する。
    // 失敗時には部分的なC++ファイルを公開しない。
    FBPSemanticExpression& InvalidArgument = ActorCall.Expression.Arguments.Last();
    InvalidArgument.Kind = EBPSemanticExpressionKind::VariableReference;
    InvalidArgument.Type.Kind = EBPSemanticTypeKind::String;
    InvalidArgument.VariableName = TEXT("UnverifiedString");
    FBPGeneratedActorFiles InvalidFiles;
    TArray<FString> InvalidDiagnostics;
    TestFalse(TEXT("Reflectionと一致しないFString変数を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AActorNoArgBranch"),
            FString(), InvalidFiles, InvalidDiagnostics));
    TestTrue(TEXT("型不一致の診断を返す"), InvalidDiagnostics.IsEmpty() == false);
    TestTrue(TEXT("失敗時にソースを公開しない"), InvalidFiles.SourceContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorOptionsParityTest,
    "BlueprintAIConverter.SemanticIR.Generator.OptionsParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorOptionsParityTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPIRNode& Print = Graph.Graphs[0].Nodes[1];

    // エスケープ文字と既定値以外のオプションを旧経路と比較する。
    Print.Pins[0].DefaultValue = TEXT("Line 1\nLine 2\t\\ \"quote\"");
    FBPIRPin Screen;
    Screen.Name = TEXT("bPrintToScreen");
    Screen.Direction = EGPD_Input;
    Screen.DefaultValue = TEXT("false");
    Print.Pins.Add(Screen);
    FBPIRPin Log;
    Log.Name = TEXT("bPrintToLog");
    Log.Direction = EGPD_Input;
    Log.DefaultValue = TEXT("true");
    Print.Pins.Add(Log);
    FBPIRPin Key;
    Key.Name = TEXT("Key");
    Key.Direction = EGPD_Input;
    Key.DefaultValue = TEXT("debug-key");
    Print.Pins.Add(Key);

    FBPGeneratedActorFiles LegacyFiles;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("旧経路成功"), FBPActorCodeGenerator::Generate(
        Graph, TEXT("AOptionsActor"), FString(), LegacyFiles, Diagnostics)) == false)
    {
        return false;
    }
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles NewFiles;
    if (TestTrue(TEXT("新経路成功"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AOptionsActor"), FString(), NewFiles, Diagnostics)) == false)
    {
        return false;
    }
    TestEqual(TEXT("オプション付きヘッダー一致"), NewFiles.HeaderContent, LegacyFiles.HeaderContent);
    TestEqual(TEXT("オプション付きソース一致"), NewFiles.SourceContent, LegacyFiles.SourceContent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorReflectedDefaultsTest,
    "BlueprintAIConverter.SemanticIR.Generator.ReflectedDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorReflectedDefaultsTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("PrintString Semantic IRを生成"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticExpression& Print =
        Semantic.Functions[0].Blocks[0].Statements[0].Expression;
    Print.Arguments.RemoveAt(Print.Arguments.Num() - 1);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("省略した末尾KeyをReflection既定値で補完"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AReflectedDefaultsActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("FName既定値を明示出力"),
        Files.SourceContent.Contains(TEXT(", NAME_None);")));

    Print.Arguments.SetNum(1);
    FBPGeneratedActorFiles AllDefaultsFiles;
    TArray<FString> AllDefaultsDiagnostics;
    if (TestTrue(TEXT("対応型の末尾既定値をすべて補完"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AAllReflectedDefaultsActor"),
            FString(), AllDefaultsFiles, AllDefaultsDiagnostics)) == false)
    {
        for (const FString& Diagnostic : AllDefaultsDiagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("文字列既定値を明示出力"),
        AllDefaultsFiles.SourceContent.Contains(TEXT("TEXT(\"Hello\")")));
    TestTrue(TEXT("LinearColor既定値を明示出力"),
        AllDefaultsFiles.SourceContent.Contains(TEXT("FLinearColor(")));
    TestTrue(TEXT("float既定値を明示出力"),
        AllDefaultsFiles.SourceContent.Contains(TEXT(", 2.0f, NAME_None);")));

    // 既定値を持たない必須bool引数は省略させない。
    Print.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Print.FunctionName = TEXT("SetActorTickEnabled");
    Print.Arguments.SetNum(1);
    FBPGeneratedActorFiles InvalidFiles;
    TArray<FString> InvalidDiagnostics;
    TestFalse(TEXT("必須引数の省略を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AReflectedDefaultsActor"),
            FString(), InvalidFiles, InvalidDiagnostics));
    TestTrue(TEXT("必須引数省略の診断あり"), InvalidDiagnostics.IsEmpty() == false);
    TestTrue(TEXT("拒否時に生成物を公開しない"), InvalidFiles.SourceContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorInvalidLiteralTest,
    "BlueprintAIConverter.SemanticIR.Generator.RejectInvalidLiteral",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorInvalidLiteralTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeGeneratorTestGraph();
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Lowering成功"), FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }

    // 手動構築されたSemantic IRでも、不正な値をC++へ流さない。
    FBPSemanticExpression& Duration = Semantic.Functions[0].Blocks[0].Statements[0].Expression.Arguments[5];
    Duration.LiteralValue = TEXT("not-a-number");
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("不正Durationを拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AInvalidActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("診断あり"), Diagnostics.IsEmpty() == false);
    TestTrue(TEXT("失敗時はファイルを空にする"), Files.SourceContent.IsEmpty());

    // 未対応の式を黙ってリテラルへ変換しない。
    Duration.LiteralValue = TEXT("2.0");
    Duration.Kind = EBPSemanticExpressionKind::VariableReference;
    TestFalse(TEXT("未対応式を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AInvalidActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("失敗時はヘッダーも空"), Files.HeaderContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorParentClassTest,
    "BlueprintAIConverter.SemanticIR.Generator.ParentClass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorParentClassTest::RunTest(const FString& Parameters)
{
    FBPGraphIR Graph = MakeGeneratorTestGraph();
    Graph.ParentClassPath = APawn::StaticClass()->GetPathName();
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Pawn派生をLowering"),
        FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Pawn派生C++を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AGeneratedPawn"), FString(), Files, Diagnostics));
    TestTrue(TEXT("Pawnヘッダーを生成"),
        Files.HeaderContent.Contains(TEXT("#include \"GameFramework/Pawn.h\"")));
    TestTrue(TEXT("Pawn継承を生成"),
        Files.HeaderContent.Contains(TEXT("class AGeneratedPawn : public APawn")));

    Graph = MakeGeneratorTestGraph();
    Graph.ParentClassPath = UActorComponent::StaticClass()->GetPathName();
    if (TestTrue(TEXT("ActorComponent派生をLowering"),
        FBPSemanticLowering::LowerPrintString(Graph, Semantic)) == false)
    {
        return false;
    }
    Diagnostics.Reset();
    TestTrue(TEXT("ActorComponent派生C++を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UGeneratedComponent"), FString(), Files, Diagnostics));
    TestTrue(TEXT("ActorComponentヘッダーを生成"),
        Files.HeaderContent.Contains(TEXT("#include \"Components/ActorComponent.h\"")));
    TestTrue(TEXT("ActorComponent継承を生成"),
        Files.HeaderContent.Contains(TEXT("class UGeneratedComponent : public UActorComponent")));
    TestEqual(TEXT("U prefixを除いたファイル名"), Files.HeaderFileName,
        FString(TEXT("GeneratedComponent.h")));

    Diagnostics.Reset();
    TestFalse(TEXT("親と異なるprefixを拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AGeneratedComponent"), FString(), Files, Diagnostics));
    TestTrue(TEXT("prefix拒否理由を返す"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("U-prefixed"));
        }));

    Semantic.ParentClassPath = UObject::StaticClass()->GetPathName();
    Diagnostics.Reset();
    TestFalse(TEXT("BeginPlayを持たない親を安全に拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UGeneratedObject"), FString(), Files, Diagnostics));
    TestTrue(TEXT("lifecycle拒否理由を返す"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("BeginPlay lifecycle"));
        }));

    FBPSemanticFunction& ObjectFunction = Semantic.Functions[0];
    ObjectFunction.Name = TEXT("RunConversion");
    ObjectFunction.bIsOverride = false;
    ObjectFunction.bIsBlueprintEvent = false;
    ObjectFunction.SignatureSource = TEXT("generated_class_reflection");
    ObjectFunction.OverriddenFunctionPath.Reset();
    ObjectFunction.Parameters.Reset();
    FBPSemanticParameter CountParameter;
    CountParameter.Name = TEXT("Count");
    CountParameter.Type.Kind = EBPSemanticTypeKind::Int32;
    CountParameter.DeclarationIndex = 0;
    ObjectFunction.Parameters.Add(CountParameter);
    ObjectFunction.Blocks[0].Statements[0].Expression.Arguments[0].Type.TypePath =
        UObject::StaticClass()->GetPathName();
    Diagnostics.Reset();
    if (TestTrue(TEXT("UObjectの通常関数を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("UGeneratedObject"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("通常関数をUFUNCTIONとして宣言"), Files.HeaderContent.Contains(
        TEXT("UFUNCTION(BlueprintCallable)\n    void RunConversion(int32 Count);")));
    TestTrue(TEXT("通常関数定義を生成"), Files.SourceContent.Contains(
        TEXT("void UGeneratedObject::RunConversion(int32 Count)")));
    TestFalse(TEXT("通常関数ではSuperを呼ばない"), Files.SourceContent.Contains(
        TEXT("Super::RunConversion")));

    ObjectFunction.Name = TEXT("OnConverted");
    ObjectFunction.bIsBlueprintEvent = true;
    ObjectFunction.bIsEventGraph = true;
    Diagnostics.Reset();
    if (TestTrue(TEXT("UObjectのBlueprintイベントを生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("UGeneratedObject"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics)
        {
            AddError(Message);
        }
        return false;
    }
    TestTrue(TEXT("BlueprintNativeEvent宣言"), Files.HeaderContent.Contains(
        TEXT("UFUNCTION(BlueprintNativeEvent, BlueprintCallable)")));
    TestTrue(TEXT("イベントImplementation宣言"), Files.HeaderContent.Contains(
        TEXT("virtual void OnConverted_Implementation(int32 Count);")));
    TestTrue(TEXT("イベントImplementation本体"), Files.SourceContent.Contains(
        TEXT("void UGeneratedObject::OnConverted_Implementation(int32 Count)")));

    ObjectFunction.Name = TEXT("BeginDestroy");
    ObjectFunction.Parameters.Reset();
    ObjectFunction.bIsOverride = true;
    ObjectFunction.bIsBlueprintEvent = false;
    ObjectFunction.bIsEventGraph = false;
    ObjectFunction.OverriddenFunctionPath = TEXT("/Script/CoreUObject.Object.BeginDestroy");
    Diagnostics.Reset();
    TestFalse(TEXT("Reflection根拠のないC++ lifecycle overrideを拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("UGeneratedObject"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("未検証overrideの生成物を公開しない"),
        Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorFunctionSignatureValidationTest,
    "BlueprintAIConverter.SemanticIR.Generator.FunctionSignatureValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorFunctionSignatureValidationTest::RunTest(
    const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("関数署名検証用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(
            MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticFunction& Function = Semantic.Functions[0];
    Function.Name = TEXT("RunValidated");
    Function.bIsEventGraph = false;
    Function.bIsBlueprintEvent = false;
    Function.bIsOverride = false;
    Function.bIsInterfaceImplementation = false;
    Function.OverriddenFunctionPath.Reset();
    Function.InterfaceClassPath.Reset();
    Function.InterfaceFunctionPath.Reset();
    Function.Parameters.Reset();
    FBPSemanticParameter First;
    First.Name = TEXT("FirstValue");
    First.Type.Kind = EBPSemanticTypeKind::Int32;
    First.DeclarationIndex = 0;
    FBPSemanticParameter Second = First;
    Second.Name = TEXT("SecondValue");
    Second.DeclarationIndex = 1;
    Function.Parameters.Add(First);
    Function.Parameters.Add(Second);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("連続した一意な引数順を生成"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("引数宣言順を保持"), Files.HeaderContent.Contains(
        TEXT("void RunValidated(int32 FirstValue, int32 SecondValue);")));

    for (int32 CaseIndex = 0; CaseIndex < 3; ++CaseIndex)
    {
        Function.Parameters[0] = First;
        Function.Parameters[0].bIsOut = CaseIndex != 1;
        Function.Parameters[0].Type.bIsReference = CaseIndex != 0;
        Diagnostics.Reset();
        TestFalse(TEXT("out値・可変参照・out参照を拒否"),
            FBPSemanticCodeGenerator::Generate(Semantic,
                TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
        TestTrue(TEXT("出力方向の拒否理由に関数名と引数名を保持"),
            Diagnostics.ContainsByPredicate([](const FString& Message)
            {
                return Message.Contains(TEXT("RunValidated")) &&
                    Message.Contains(TEXT("FirstValue")) &&
                    Message.Contains(TEXT("unsupported output or mutable reference"));
            }));
        TestTrue(TEXT("出力方向拒否時に生成物を公開しない"),
            Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    }
    Function.Parameters[0] = First;
    Function.Parameters[0].Type.bIsReference = true;
    Function.Parameters[0].Type.bIsConst = true;
    Diagnostics.Reset();
    TestTrue(TEXT("const参照入力を生成"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("const参照入力の宣言を保持"), Files.HeaderContent.Contains(
        TEXT("const int32& FirstValue")));
    Function.Parameters[0].bIsOut = true;
    Diagnostics.Reset();
    TestTrue(TEXT("ReflectionのOutParm付きconst参照も入力として生成"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    Function.Parameters[0] = First;

    for (int32 CaseIndex = 0; CaseIndex < 3; ++CaseIndex)
    {
        Function.ReturnType.bIsReference = CaseIndex == 0;
        Function.ReturnType.bIsConst = CaseIndex == 1;
        Function.ReturnType.ContainerKind = CaseIndex == 2 ? TEXT("array") : TEXT("none");
        Diagnostics.Reset();
        TestFalse(TEXT("参照戻り値・const戻り値・void配列を拒否"),
            FBPSemanticCodeGenerator::Generate(Semantic,
                TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
        TestTrue(TEXT("戻り値Policyの診断を保持"),
            Diagnostics.ContainsByPredicate([](const FString& Message)
            {
                return Message.Contains(TEXT("unsupported return type qualifiers or void container"));
            }));
        TestTrue(TEXT("戻り値Policy拒否時に生成物を公開しない"),
            Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    }
    Function.ReturnType.bIsReference = false;
    Function.ReturnType.bIsConst = false;
    Function.ReturnType.ContainerKind = TEXT("none");

    Function.Parameters[1].Name = Function.Parameters[0].Name;
    Diagnostics.Reset();
    TestFalse(TEXT("重複引数名を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("重複引数名の理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("duplicate parameter name"));
        }));
    TestTrue(TEXT("重複引数名拒否時に生成物を公開しない"),
        Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());

    Function.Parameters[1] = Second;
    Function.Parameters[1].DeclarationIndex = 0;
    Diagnostics.Reset();
    TestFalse(TEXT("重複引数順を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("重複引数順の理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("duplicate parameter declaration index"));
        }));

    Function.Parameters[1].DeclarationIndex = 2;
    Diagnostics.Reset();
    TestFalse(TEXT("引数順の欠番を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("引数順欠番の理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("indices must be contiguous from zero"));
        }));

    Function.Parameters[1] = Second;
    FBPSemanticParameter ReturnParameter;
    ReturnParameter.Name = TEXT("ReturnValue");
    ReturnParameter.Type.Kind = EBPSemanticTypeKind::Bool;
    ReturnParameter.bIsReturn = true;
    Function.Parameters.Add(ReturnParameter);
    Diagnostics.Reset();
    TestFalse(TEXT("戻り値メタデータの型不一致を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("戻り値型不一致の理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("inconsistent return parameter metadata"));
        }));

    Function.Parameters.RemoveAt(2);
    Function.OverriddenFunctionPath = TEXT("/Script/Engine.Actor:BeginPlay");
    Diagnostics.Reset();
    TestFalse(TEXT("overrideフラグのない親関数メタデータを拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("overrideメタデータ不整合の理由を保持"),
        Diagnostics.ContainsByPredicate(
            [](const FString& Message)
            {
                return Message.Contains(
                    TEXT("inconsistent override or interface metadata"));
            }));

    Function.OverriddenFunctionPath.Reset();
    Function.bIsInterfaceImplementation = true;
    Function.InterfaceFunctionPath = TEXT("/Script/Test.Interface:RunValidated");
    Diagnostics.Reset();
    TestFalse(TEXT("不完全なInterfaceメタデータを拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("ASignatureActor"), TEXT("TEST_API"), Files, Diagnostics));
    TestTrue(TEXT("Interfaceメタデータ不整合の理由を保持"),
        Diagnostics.ContainsByPredicate(
            [](const FString& Message)
            {
                return Message.Contains(
                    TEXT("inconsistent override or interface metadata"));
            }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorOverrideSignatureTest,
    "BlueprintAIConverter.SemanticIR.Generator.OverrideSignature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorOverrideSignatureTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("override検証用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }
    const UClass* Parent = ABPEmitterTestActor::StaticClass();
    const UFunction* Target = Parent->FindFunctionByName(TEXT("VerifiedOverride"));
    if (TestNotNull(TEXT("ネイティブイベント署名が存在"), Target) == false)
    {
        return false;
    }
    Semantic.ParentClassPath = Parent->GetPathName();
    FBPSemanticFunction& Function = Semantic.Functions[0];
    Function.Name = TEXT("VerifiedOverride");
    Function.SignatureSource = TEXT("generated_class_reflection");
    Function.bIsNative = true;
    Function.bIsBlueprintEvent = true;
    Function.bIsEventGraph = true;
    Function.bIsOverride = true;
    Function.OverriddenFunctionPath = Target->GetPathName();
    Function.Parameters.Reset();
    FBPSemanticParameter Value;
    Value.Name = TEXT("Value");
    Value.Type.Kind = EBPSemanticTypeKind::Int32;
    Value.DeclarationIndex = 0;
    Function.Parameters.Add(Value);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("親Reflectionと一致するoverrideを生成"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AVerifiedActor"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics) { AddError(Message); }
        return false;
    }
    TestTrue(TEXT("Implementation override宣言"), Files.HeaderContent.Contains(
        TEXT("virtual void VerifiedOverride_Implementation(int32 Value) override;")));
    TestTrue(TEXT("正しいSuper呼び出し"), Files.SourceContent.Contains(
        TEXT("Super::VerifiedOverride_Implementation(Value);")));
    FString SemanticJson;
    TestTrue(TEXT("override署名のJSON出力成功"),
        FBPSemanticJsonSerializer::Serialize(Semantic, TEXT("5.7"), SemanticJson));
    TestTrue(TEXT("JSONにoverride元の正規パスを保持"),
        SemanticJson.Contains(Target->GetPathName()));
    Function.Visibility = EBPSemanticFunctionVisibility::Public;
    Diagnostics.Reset();
    TestTrue(TEXT("親と一致する明示的なpublic overrideを生成"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AVerifiedActor"), FString(), Files, Diagnostics));

    const FBPSemanticIR Valid = Semantic;
    Semantic.Functions[0].bIsNative = false;
    Semantic.Functions[0].bIsEventGraph = false;
    TestTrue(TEXT("生成クラスのFunction Graph overrideも親のnative署名で検証"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AVerifiedActor"), FString(), Files, Diagnostics));
    for (int32 CaseIndex = 0; CaseIndex < 13; ++CaseIndex)
    {
        Semantic = Valid;
        FBPSemanticFunction& Invalid = Semantic.Functions[0];
        switch (CaseIndex)
        {
        case 0: Invalid.OverriddenFunctionPath += TEXT("Missing"); break;
        case 1: Invalid.Parameters[0].Type.Kind = EBPSemanticTypeKind::Bool; break;
        case 2: Invalid.Parameters[0].Name = TEXT("OtherValue"); break;
        case 3: Invalid.Parameters.Reset(); break;
        case 4: Invalid.bIsConst = true; break;
        case 5: Invalid.ReturnType.Kind = EBPSemanticTypeKind::Bool; break;
        case 6: Invalid.SignatureSource = TEXT("unverified"); break;
        case 7: Semantic.ParentClassPath = TEXT("/Script/Engine.Actor"); break;
        case 8:
            Invalid.Parameters[0].Type.bIsReference = true;
            Invalid.Parameters[0].Type.bIsConst = true;
            break;
        case 9:
            Invalid.Name = TEXT("SetActorTickEnabled");
            Invalid.OverriddenFunctionPath = AActor::StaticClass()->FindFunctionByName(
                TEXT("SetActorTickEnabled"))->GetPathName();
            break;
        case 10:
            Invalid.Name = TEXT("MatchingSignature");
            Invalid.OverriddenFunctionPath = Parent->FindFunctionByName(Invalid.Name)->GetPathName();
            break;
        case 11: Invalid.Visibility = EBPSemanticFunctionVisibility::Protected; break;
        case 12: Invalid.Visibility = EBPSemanticFunctionVisibility::Private; break;
        }
        Diagnostics.Reset();
        TestFalse(TEXT("改ざんしたoverride署名・所有者・出力方式を拒否"),
            FBPSemanticCodeGenerator::Generate(Semantic,
                TEXT("AVerifiedActor"), FString(), Files, Diagnostics));
        TestTrue(TEXT("override拒否理由を保持"),
            Diagnostics.ContainsByPredicate([](const FString& Message)
            {
                return Message.Contains(TEXT("Override"));
            }));
        TestTrue(TEXT("override拒否時に生成物を公開しない"),
            Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    }
    Semantic = Valid;
    Semantic.Functions[0].bIsOverride = false;
    Semantic.Functions[0].OverriddenFunctionPath.Reset();
    Semantic.Functions[0].bIsInterfaceImplementation = true;
    Semantic.Functions[0].InterfaceClassPath =
        UBPFunctionSignatureTestInterface::StaticClass()->GetPathName();
    Semantic.Functions[0].InterfaceFunctionPath = TEXT("unused");
    Diagnostics.Reset();
    TestFalse(TEXT("親が未実装のInterfaceをoverrideとして生成しない"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AVerifiedActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("Interface継承不足の理由を保持"),
        Diagnostics.ContainsByPredicate([](const FString& Message)
        {
            return Message.Contains(TEXT("interface implemented by the native parent"));
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorFunctionVisibilityTest,
    "BlueprintAIConverter.SemanticIR.Generator.FunctionVisibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorFunctionVisibilityTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("可視性検証用Lowering成功"), FBPSemanticLowering::LowerPrintString(
            MakeGeneratorTestGraph(), Semantic)) == false) { return false; }
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    Semantic.Functions[0].Visibility = EBPSemanticFunctionVisibility::Public;
    TestFalse(TEXT("BeginPlayの公開範囲改変を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVisibilityActor"), FString(), Files, Diagnostics));
    Semantic.Functions[0].Visibility = EBPSemanticFunctionVisibility::Protected;
    Diagnostics.Reset();
    TestTrue(TEXT("protected BeginPlayを生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVisibilityActor"), FString(), Files, Diagnostics));
    FBPSemanticFunction Function = Semantic.Functions[0];
    Function.bIsOverride = false;
    Function.OverriddenFunctionPath.Reset();
    Function.bIsBlueprintEvent = false;
    Function.bIsEventGraph = false;
    Semantic.Functions.Reset();
    const EBPSemanticFunctionVisibility Access[] = {
        EBPSemanticFunctionVisibility::Public, EBPSemanticFunctionVisibility::Protected,
        EBPSemanticFunctionVisibility::Private, EBPSemanticFunctionVisibility::Unspecified };
    const TCHAR* Names[] = { TEXT("RunPublic"), TEXT("RunProtected"),
        TEXT("RunPrivate"), TEXT("RunLegacy") };
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Function.Name = FName(Names[Index]);
        Function.Visibility = Access[Index];
        Semantic.Functions.Add(Function);
    }
    Diagnostics.Reset();
    TestTrue(TEXT("異なる可視性の複数関数を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVisibilityActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("public宣言"), Files.HeaderContent.Contains(
        TEXT("public:\n    UFUNCTION(BlueprintCallable)\n    void RunPublic();")));
    TestTrue(TEXT("protected宣言とBlueprintProtected"), Files.HeaderContent.Contains(
        TEXT("protected:\n    UFUNCTION(BlueprintCallable, meta=(BlueprintProtected))\n    void RunProtected();")));
    TestTrue(TEXT("private宣言"), Files.HeaderContent.Contains(
        TEXT("private:\n    UFUNCTION(BlueprintCallable)\n    void RunPrivate();")));
    TestTrue(TEXT("旧IRのpublic既定を維持"), Files.HeaderContent.Contains(
        TEXT("public:\n    UFUNCTION(BlueprintCallable)\n    void RunLegacy();")));
    Semantic.Functions[1].bIsEventGraph = true;
    Diagnostics.Reset();
    TestTrue(TEXT("protected native eventを生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVisibilityActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("protected eventのメタデータを保持"), Files.HeaderContent.Contains(
        TEXT("protected:\n    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, meta=(BlueprintProtected))")));
    Semantic.Functions[1].bIsEventGraph = false;
    Semantic.Functions[2].bIsEventGraph = true;
    Diagnostics.Reset();
    TestFalse(TEXT("private native eventは拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVisibilityActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"),
        Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    Semantic.Functions[2].bIsEventGraph = false;
    Semantic.Functions[2].Visibility = static_cast<EBPSemanticFunctionVisibility>(255);
    Diagnostics.Reset();
    TestFalse(TEXT("未知の可視性は拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVisibilityActor"), FString(), Files, Diagnostics));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorMultipleFunctionsTest,
    "BlueprintAIConverter.SemanticIR.Generator.MultipleFunctions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorMultipleFunctionsTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("複数関数テスト用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(
            MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }
    if (TestEqual(TEXT("元Semantic関数数"), Semantic.Functions.Num(), 1) == false)
    {
        return false;
    }

    FBPSemanticFunction SecondFunction = Semantic.Functions[0];
    SecondFunction.Name = TEXT("RunSecond");
    SecondFunction.bIsEventGraph = false;
    SecondFunction.bIsBlueprintEvent = false;
    SecondFunction.bIsOverride = false;
    SecondFunction.OverriddenFunctionPath.Reset();
    FBPSemanticStatement LoopBody =
        SecondFunction.Blocks[0].Statements[0];
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForLoop;
    Loop.LoopIndexVariable = TEXT("FunctionLoopIndex");
    Loop.LoopFirstIndex.Kind = EBPSemanticExpressionKind::Literal;
    Loop.LoopFirstIndex.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopFirstIndex.LiteralValue = TEXT("0");
    Loop.LoopLastIndex.Kind = EBPSemanticExpressionKind::Literal;
    Loop.LoopLastIndex.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopLastIndex.LiteralValue = TEXT("2");
    Loop.LoopBodyStatements.Add(MoveTemp(LoopBody));
    SecondFunction.Blocks[0].Statements.Reset();
    SecondFunction.Blocks[0].Statements.Add(MoveTemp(Loop));
    Semantic.Functions.Add(MoveTemp(SecondFunction));

    FBPSemanticFunction ForEachFunction = Semantic.Functions[0];
    ForEachFunction.Name = TEXT("RunForEach");
    ForEachFunction.bIsEventGraph = false;
    ForEachFunction.bIsBlueprintEvent = false;
    ForEachFunction.bIsOverride = false;
    ForEachFunction.OverriddenFunctionPath.Reset();
    FBPSemanticStatement ForEachBody =
        ForEachFunction.Blocks[0].Statements[0];
    FBPSemanticStatement ForEach;
    ForEach.Kind = EBPSemanticStatementKind::ForEachLoop;
    ForEach.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    ForEach.LoopArray.VariableName = TEXT("Numbers");
    ForEach.LoopArray.Type.Kind = EBPSemanticTypeKind::Int32;
    ForEach.LoopArray.Type.ContainerKind = TEXT("array");
    ForEach.LoopIndexVariable = TEXT("ForEachIndex");
    ForEach.LoopElementVariable = TEXT("ForEachElement");
    ForEach.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    ForEach.LoopBodyStatements.Add(MoveTemp(ForEachBody));
    ForEachFunction.Blocks[0].Statements.Reset();
    ForEachFunction.Blocks[0].Statements.Add(MoveTemp(ForEach));
    Semantic.Functions.Add(MoveTemp(ForEachFunction));
    FBPSemanticVariable Numbers;
    Numbers.Name = TEXT("Numbers");
    Numbers.Type.Kind = EBPSemanticTypeKind::Int32;
    Numbers.Type.ContainerKind = TEXT("array");
    Numbers.ReflectedCppType = TEXT("TArray<int32>");
    Semantic.Variables.Add(MoveTemp(Numbers));

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("複数関数を一つのUCLASSとして生成"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AMultipleFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("BeginPlay宣言を生成"), Files.HeaderContent.Contains(
        TEXT("virtual void BeginPlay() override;")));
    TestTrue(TEXT("通常関数宣言を生成"), Files.HeaderContent.Contains(
        TEXT("UFUNCTION(BlueprintCallable)\n    void RunSecond();")));
    TestTrue(TEXT("BeginPlay定義を生成"), Files.SourceContent.Contains(
        TEXT("void AMultipleFunctionActor::BeginPlay()")));
    TestTrue(TEXT("通常関数定義を生成"), Files.SourceContent.Contains(
        TEXT("void AMultipleFunctionActor::RunSecond()")));
    TestTrue(TEXT("通常関数内のForLoopを生成"), Files.SourceContent.Contains(
        TEXT("for (int32 FunctionLoopIndex = 0; FunctionLoopIndex <= 2; ++FunctionLoopIndex)")));
    TestTrue(TEXT("ForEach関数宣言を生成"), Files.HeaderContent.Contains(
        TEXT("void RunForEach();")));
    TestTrue(TEXT("通常関数内のForEachLoopを生成"), Files.SourceContent.Contains(
        TEXT("for (int32 ForEachIndex = 0; ForEachIndex < Numbers.Num(); ++ForEachIndex)")));
    TestTrue(TEXT("ForEach関数定義を生成"), Files.SourceContent.Contains(
        TEXT("void AMultipleFunctionActor::RunForEach()")));
    const FString PrintCall(TEXT("UKismetSystemLibrary::PrintString"));
    TestTrue(TEXT("両関数のPrintStringを生成"),
        Files.SourceContent.Find(PrintCall) !=
            Files.SourceContent.Find(PrintCall, ESearchCase::CaseSensitive,
                ESearchDir::FromEnd));
    const FString SystemInclude(
        TEXT("#include \"Kismet/KismetSystemLibrary.h\""));
    TestTrue(TEXT("依存includeを重複生成しない"),
        Files.SourceContent.Find(SystemInclude) != INDEX_NONE &&
        Files.SourceContent.Find(SystemInclude) ==
            Files.SourceContent.Find(SystemInclude, ESearchCase::CaseSensitive,
                ESearchDir::FromEnd));

    FString SemanticJson;
    TestTrue(TEXT("複数関数Semantic JSONを生成"),
        FBPSemanticJsonSerializer::Serialize(
            Semantic, TEXT("5.7"), SemanticJson));
    TestTrue(TEXT("JSONにBeginPlayを保持"),
        SemanticJson.Contains(TEXT("\"name\": \"BeginPlay\"")));
    TestTrue(TEXT("JSONに通常関数を保持"),
        SemanticJson.Contains(TEXT("\"name\": \"RunSecond\"")));
    TestTrue(TEXT("JSONにForEach関数を保持"),
        SemanticJson.Contains(TEXT("\"name\": \"RunForEach\"")));

    Semantic.Functions[1].Name = Semantic.Functions[0].Name;
    Diagnostics.Reset();
    TestFalse(TEXT("同名関数を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AMultipleFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("同名拒否時に生成物を公開しない"),
        Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    TestTrue(TEXT("同名拒否理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("class-conflicting semantic function name"));
        }));

    Semantic.Functions[1].Name = TEXT("OnCollision");
    Semantic.Functions[1].bIsEventGraph = true;
    Semantic.Functions[1].bIsBlueprintEvent = true;
    Semantic.Functions[2].Name = TEXT("OnCollision_Implementation");
    Semantic.Functions[2].bIsEventGraph = false;
    Semantic.Functions[2].bIsBlueprintEvent = false;
    Diagnostics.Reset();
    TestFalse(TEXT("イベントImplementationと通常関数のC++名衝突を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AMultipleFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics,
            EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("生成C++名衝突時に生成物を公開しない"),
        Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    TestTrue(TEXT("生成C++名の衝突理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(
                TEXT("Generated C++ function definition name collision"));
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorReturnStatementsTest,
    "BlueprintAIConverter.SemanticIR.Generator.ReturnStatements",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorReturnStatementsTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("Returnテスト用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(
            MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticFunction EchoFunction = Semantic.Functions[0];
    EchoFunction.Name = TEXT("EchoValue");
    EchoFunction.ReturnType.Kind = EBPSemanticTypeKind::Int32;
    EchoFunction.bIsEventGraph = false;
    EchoFunction.bIsBlueprintEvent = false;
    EchoFunction.bIsOverride = false;
    EchoFunction.OverriddenFunctionPath.Reset();
    EchoFunction.Parameters.Reset();
    FBPSemanticParameter Input;
    Input.Name = TEXT("InputValue");
    Input.Type.Kind = EBPSemanticTypeKind::Int32;
    Input.DeclarationIndex = 0;
    EchoFunction.Parameters.Add(Input);
    const FBPSemanticStatement OriginalExpression =
        Semantic.Functions[0].Blocks[0].Statements[0];
    FBPSemanticStatement Return;
    Return.Kind = EBPSemanticStatementKind::Return;
    Return.Expression.Kind = EBPSemanticExpressionKind::VariableReference;
    Return.Expression.Type.Kind = EBPSemanticTypeKind::Int32;
    Return.Expression.VariableName = Input.Name;
    FBPSemanticBasicBlock ExpressionBlock;
    ExpressionBlock.BlockId = FGuid::NewGuid();
    ExpressionBlock.Terminator = EBPSemanticBlockTerminatorKind::Fallthrough;
    ExpressionBlock.Statements.Add(OriginalExpression);
    FBPSemanticBasicBlock ReturnBlock;
    ReturnBlock.BlockId = FGuid::NewGuid();
    ReturnBlock.Terminator = EBPSemanticBlockTerminatorKind::Return;
    ReturnBlock.Statements.Add(Return);
    ExpressionBlock.SuccessorBlockIds.Add(ReturnBlock.BlockId);
    EchoFunction.Blocks.Reset();
    EchoFunction.Blocks.Add(ExpressionBlock);
    EchoFunction.Blocks.Add(ReturnBlock);
    Semantic.Functions.Add(EchoFunction);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("複数関数内の非void Returnを生成"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AReturnFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("戻り値付き宣言を生成"), Files.HeaderContent.Contains(
        TEXT("int32 EchoValue(int32 InputValue);")));
    TestTrue(TEXT("戻り値付き定義を生成"), Files.SourceContent.Contains(
        TEXT("int32 AReturnFunctionActor::EchoValue(int32 InputValue)")));
    TestTrue(TEXT("引数を返すReturnを生成"), Files.SourceContent.Contains(
        TEXT("return InputValue;")));
    TestTrue(TEXT("直列BasicBlockの先行式を生成"),
        Files.SourceContent.Find(TEXT("UKismetSystemLibrary::PrintString")) <
        Files.SourceContent.Find(TEXT("return InputValue;")));

    FString SemanticJson;
    TestTrue(TEXT("Returnを含むSemantic JSONを生成"),
        FBPSemanticJsonSerializer::Serialize(
            Semantic, TEXT("5.7"), SemanticJson));
    TestTrue(TEXT("JSONにReturn文を保持"),
        SemanticJson.Contains(TEXT("\"kind\": \"return\"")));
    TestTrue(TEXT("JSONにBlock IDを保持"),
        SemanticJson.Contains(TEXT("\"block_id\":")));
    TestTrue(TEXT("JSONにFallthroughを保持"),
        SemanticJson.Contains(TEXT("\"terminator\": \"fallthrough\"")));
    TestTrue(TEXT("JSONにBlock後続を保持"),
        SemanticJson.Contains(TEXT("\"successor_block_ids\":")));

    FBPSemanticIR SingleFunctionSemantic = Semantic;
    SingleFunctionSemantic.Functions.RemoveAt(0);
    Diagnostics.Reset();
    TestTrue(TEXT("単一非void関数でもReturnを生成"),
        FBPSemanticCodeGenerator::Generate(SingleFunctionSemantic,
            TEXT("AReturnOnlyActor"), TEXT("TEST_API"),
            Files, Diagnostics));
    TestTrue(TEXT("単一関数のReturnを保持"), Files.SourceContent.Contains(
        TEXT("return InputValue;")));

    Semantic.Functions[1].Blocks.Last().Statements[0] = OriginalExpression;
    Diagnostics.Reset();
    TestFalse(TEXT("非void関数のReturn欠落を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AReturnFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics));
    TestTrue(TEXT("Return欠落理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("requires a terminal Return statement"));
        }));
    TestTrue(TEXT("Return欠落時に生成物を公開しない"),
        Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());

    FBPSemanticBasicBlock InvalidBlock;
    Return.Expression.Type.Kind = EBPSemanticTypeKind::Bool;
    InvalidBlock.Statements.Add(Return);
    Semantic.Functions[1].Blocks.Reset();
    Semantic.Functions[1].Blocks.Add(InvalidBlock);
    Diagnostics.Reset();
    TestFalse(TEXT("Return型不一致を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AReturnFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics));
    TestTrue(TEXT("Return型不一致理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("Return expression type does not match"));
        }));

    Return.Expression.Type.Kind = EBPSemanticTypeKind::Int32;
    Semantic.Functions[1].Blocks[0].Statements.Reset();
    Semantic.Functions[1].Blocks[0].Statements.Add(Return);
    Semantic.Functions[1].Blocks[0].Statements.Add(OriginalExpression);
    Diagnostics.Reset();
    TestFalse(TEXT("途中Returnを拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AReturnFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics));
    TestTrue(TEXT("途中Return理由を保持"), Diagnostics.ContainsByPredicate(
        [](const FString& Message)
        {
            return Message.Contains(TEXT("non-terminal Return statement"));
        }));

    Semantic.Functions[1] = EchoFunction;
    Semantic.Functions[1].Blocks[0].SuccessorBlockIds[0] = FGuid::NewGuid();
    Diagnostics.Reset();
    TestFalse(TEXT("不正なBasicBlock後続を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AReturnFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics));
    TestTrue(TEXT("BasicBlock後続の拒否理由を保持"),
        Diagnostics.ContainsByPredicate(
            [](const FString& Message)
            {
                return Message.Contains(TEXT("invalid BasicBlock successor"));
            }));

    Semantic.Functions[1] = EchoFunction;
    Semantic.Functions[1].ReturnType.Kind = EBPSemanticTypeKind::Void;
    Semantic.Functions[1].Blocks.Reset();
    InvalidBlock.Statements.Reset();
    InvalidBlock.Statements.Add(Return);
    Semantic.Functions[1].Blocks.Add(InvalidBlock);
    Diagnostics.Reset();
    TestFalse(TEXT("void関数が値を返すReturnを拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic,
            TEXT("AReturnFunctionActor"), TEXT("TEST_API"),
            Files, Diagnostics));
    TestTrue(TEXT("void Return値の拒否理由を保持"),
        Diagnostics.ContainsByPredicate(
            [](const FString& Message)
            {
                return Message.Contains(TEXT("Void semantic function cannot return a value"));
            }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorVariablesTest,
    "BlueprintAIConverter.SemanticIR.Generator.MemberVariables",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorVariablesTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("変数テスト用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticVariable Enabled;
    Enabled.Name = TEXT("Enabled");
    Enabled.Type.Kind = EBPSemanticTypeKind::Bool;
    Enabled.ReflectedCppType = TEXT("bool");
    Enabled.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    Enabled.bDefaultValueAvailable = true;
    Enabled.DefaultValueSource = TEXT("cdo");
    Enabled.DefaultValue = TEXT("true");
    Semantic.Variables.Add(Enabled);

    FBPSemanticVariable Health;
    Health.Name = TEXT("Health");
    Health.Type.Kind = EBPSemanticTypeKind::Int32;
    Health.ReflectedCppType = TEXT("int32");
    Health.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    Health.bDefaultValueAvailable = true;
    Health.DefaultValueSource = TEXT("cdo");
    Health.DefaultValue = TEXT("-17");
    Semantic.Variables.Add(Health);

    FBPSemanticVariable Title;
    Title.Name = TEXT("Title");
    Title.Type.Kind = EBPSemanticTypeKind::String;
    Title.ReflectedCppType = TEXT("FString");
    Title.InitializationStrategy = EBPVariableInitializationStrategy::StringLiteral;
    Title.bDefaultValueAvailable = true;
    Title.DefaultValueSource = TEXT("cdo");
    Title.DefaultValue = TEXT("Line 1\n\"Ready\"");
    Semantic.Variables.Add(Title);

    FBPSemanticVariable LargeCount;
    LargeCount.Name = TEXT("LargeCount");
    LargeCount.Type.Kind = EBPSemanticTypeKind::Int64;
    LargeCount.ReflectedCppType = TEXT("int64");
    LargeCount.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    LargeCount.bDefaultValueAvailable = true;
    LargeCount.DefaultValueSource = TEXT("cdo");
    LargeCount.DefaultValue = TEXT("9000000000");
    Semantic.Variables.Add(LargeCount);

    FBPSemanticVariable Speed;
    Speed.Name = TEXT("Speed");
    Speed.Type.Kind = EBPSemanticTypeKind::Float;
    Speed.ReflectedCppType = TEXT("float");
    Speed.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    Speed.bDefaultValueAvailable = true;
    Speed.DefaultValueSource = TEXT("cdo");
    Speed.DefaultValue = TEXT("1.25");
    Semantic.Variables.Add(Speed);

    FBPSemanticVariable Precision;
    Precision.Name = TEXT("Precision");
    Precision.Type.Kind = EBPSemanticTypeKind::Double;
    Precision.ReflectedCppType = TEXT("double");
    Precision.InitializationStrategy = EBPVariableInitializationStrategy::DirectLiteral;
    Precision.bDefaultValueAvailable = true;
    Precision.DefaultValueSource = TEXT("cdo");
    Precision.DefaultValue = TEXT("2.5");
    Semantic.Variables.Add(Precision);

    FBPSemanticVariable Tag;
    Tag.Name = TEXT("Tag");
    Tag.Type.Kind = EBPSemanticTypeKind::Name;
    Tag.ReflectedCppType = TEXT("FName");
    Tag.InitializationStrategy = EBPVariableInitializationStrategy::NameLiteral;
    Tag.bDefaultValueAvailable = true;
    Tag.DefaultValueSource = TEXT("cdo");
    Tag.DefaultValue = TEXT("Runner");
    Semantic.Variables.Add(Tag);

    FBPSemanticVariable Label;
    Label.Name = TEXT("Label");
    Label.Type.Kind = EBPSemanticTypeKind::Text;
    Label.ReflectedCppType = TEXT("FText");
    Label.InitializationStrategy = EBPVariableInitializationStrategy::TextLiteral;
    Label.bDefaultValueAvailable = true;
    Label.DefaultValueSource = TEXT("cdo");
    Label.DefaultValue = TEXT("Ready Text");
    Label.bTextCultureInvariant = true;
    Semantic.Variables.Add(Label);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("対応済みメンバー変数を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVariableActor"), FString(), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("bool初期値"), Files.HeaderContent.Contains(TEXT("bool Enabled = true;")));
    TestTrue(TEXT("int32初期値"), Files.HeaderContent.Contains(TEXT("int32 Health = -17;")));
    TestTrue(TEXT("FString初期値をエスケープ"), Files.HeaderContent.Contains(
        TEXT("FString Title = TEXT(\"Line 1\\n\\\"Ready\\\"\");")));
    TestTrue(TEXT("int64初期値"), Files.HeaderContent.Contains(
        TEXT("int64 LargeCount = 9000000000LL;")));
    TestTrue(TEXT("float初期値"), Files.HeaderContent.Contains(TEXT("float Speed = 1.25f;")));
    TestTrue(TEXT("double初期値"), Files.HeaderContent.Contains(TEXT("double Precision = 2.5")));
    TestTrue(TEXT("FName初期値"), Files.HeaderContent.Contains(
        TEXT("FName Tag = FName(TEXT(\"Runner\"));")));
    TestTrue(TEXT("FText初期値"), Files.HeaderContent.Contains(
        TEXT("FText Label = FText::AsCultureInvariant(TEXT(\"Ready Text\"));")));
    FString HeaderWithoutProperties = Files.HeaderContent;
    TestEqual(TEXT("UPROPERTYを8件生成"),
        HeaderWithoutProperties.ReplaceInline(TEXT("UPROPERTY()"), TEXT("")), 8);

    Semantic.Variables[0].bDefaultValueAvailable = false;
    TestFalse(TEXT("CDO値を取得できない変数を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVariableActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), Files.HeaderContent.IsEmpty());

    Semantic.Variables[0].bDefaultValueAvailable = true;
    Semantic.Variables[4].DefaultValue = TEXT("nan");
    TestFalse(TEXT("非有限floatを拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AVariableActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("数値拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorVariableFallbackTest,
    "BlueprintAIConverter.SemanticIR.Generator.VariableDeclarationFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorVariableFallbackTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("フォールバックテスト用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticVariable Variable;
    Variable.Name = TEXT("CustomValue");
    Variable.Guid = FGuid(1, 2, 3, 4);
    Variable.Type.Kind = EBPSemanticTypeKind::Unresolved;
    Variable.ReflectedCppType = TEXT("FCustomValue");
    Variable.ReflectedPropertyClass = TEXT("StructProperty");
    Variable.PropertyFlags = TEXT("0");
    Semantic.Variables.Add(Variable);

    FBPSemanticDiagnostic Warning;
    Warning.Severity = EBPSemanticDiagnosticSeverity::Warning;
    Warning.Message = TEXT("CustomValue requires type resolution");
    Semantic.Diagnostics.Add(Warning);
    TestFalse(TEXT("警告だけではSemantic IRをエラー扱いしない"), Semantic.HasErrors());

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("Strictでは未対応変数を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AFallbackActor"), FString(), Files, Diagnostics));

    if (TestTrue(TEXT("DeclarationOnlyでは宣言を生成"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AFallbackActor"), FString(), Files, Diagnostics,
        EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        return false;
    }
    TestTrue(TEXT("既定値欠落コメント"), Files.HeaderContent.Contains(
        TEXT("部分変換: Blueprintのデフォルト値とプロパティメタデータは保持されていません。")));
    TestTrue(TEXT("依存関係確認コメント"), Files.HeaderContent.Contains(
        TEXT("必要なヘッダーとモジュール依存関係を確認してください。")));
    TestTrue(TEXT("元変数GUIDコメント"), Files.HeaderContent.Contains(
        TEXT("00000001-0000-0002-0000-000300000004")));
    TestTrue(TEXT("反射型で値初期化宣言"), Files.HeaderContent.Contains(
        TEXT("FCustomValue CustomValue{};")));
    TestTrue(TEXT("partial診断を返す"), Diagnostics.ContainsByPredicate(
        [](const FString& Diagnostic)
        {
            return Diagnostic.Contains(TEXT("Partial variable conversion: CustomValue"));
        }));

    Semantic.Variables[0].ReflectedCppType = TEXT("FVector; #error unsafe");
    TestFalse(TEXT("危険な反射型文字列を拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AFallbackActor"), FString(), Files, Diagnostics,
        EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("危険な型の生成物を公開しない"), Files.HeaderContent.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorImportTextVariableTest,
    "BlueprintAIConverter.SemanticIR.Generator.VerifiedImportTextVariable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorImportTextVariableTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Semantic;
    if (TestTrue(TEXT("ImportTextテスト用Lowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), Semantic)) == false)
    {
        return false;
    }

    FBPSemanticVariable Variable;
    Variable.Name = TEXT("SpawnOffset");
    Variable.Type.Kind = EBPSemanticTypeKind::Struct;
    Variable.TypePath = TEXT("/Script/CoreUObject.Vector");
    Variable.ReflectedCppType = TEXT("FVector");
    Variable.ReflectedPropertyClass = TEXT("StructProperty");
    Variable.PropertyFlags = TEXT("0");
    Variable.InitializationStrategy = EBPVariableInitializationStrategy::ImportText;
    Variable.bDefaultValueAvailable = true;
    Variable.DefaultValueSource = TEXT("cdo_export_text");
    Variable.DefaultValue = TEXT("(X=1.0,Y=2.0,Z=3.0)");
    Variable.bExportedDefaultValueAvailable = true;
    Variable.ExportedDefaultValue = Variable.DefaultValue;
    Variable.bExportTextRoundTripVerified = true;
    Semantic.Variables.Add(Variable);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("往復検証済みImportText変数を生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AImportTextActor"),
            FString(), Files, Diagnostics)) == false)
    {
        return false;
    }
    TestTrue(TEXT("値初期化した反射型宣言"),
        Files.HeaderContent.Contains(TEXT("FVector SpawnOffset{};")));
    TestTrue(TEXT("コンストラクタ宣言"),
        Files.HeaderContent.Contains(TEXT("AImportTextActor();")));
    TestTrue(TEXT("UnrealTypeヘッダー"),
        Files.SourceContent.Contains(TEXT("#include \"UObject/UnrealType.h\"")));
    TestTrue(TEXT("日本語の復元コメント"), Files.SourceContent.Contains(
        TEXT("往復検証済み値からBlueprintのデフォルト値を復元します。")));
    TestTrue(TEXT("反射プロパティを名前で解決"), Files.SourceContent.Contains(
        TEXT("GET_MEMBER_NAME_CHECKED(AImportTextActor, SpawnOffset)")));
    TestTrue(TEXT("ExportText値をImportTextへ渡す"), Files.SourceContent.Contains(
        TEXT("TEXT(\"(X=1.0,Y=2.0,Z=3.0)\")")));

    Semantic.Variables[0].bExportTextRoundTripVerified = false;
    TestFalse(TEXT("往復未検証値は拒否"), FBPSemanticCodeGenerator::Generate(
        Semantic, TEXT("AImportTextActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), Files.HeaderContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorReflectedBoolMemberTest,
    "BlueprintAIConverter.SemanticIR.Generator.ReflectedBoolMember",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorReflectedBoolMemberTest::RunTest(const FString& Parameters)
{
    // テスト用Actorの公開ネイティブboolを、PrintString専用処理を経由せず共通Callへ渡す。
    FBPSemanticIR Semantic;
    Semantic.ParentClassPath = ABPEmitterTestActor::StaticClass()->GetPathName();
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    FBPSemanticExpression& Call = Statement.Expression;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Type.Kind = EBPSemanticTypeKind::Void;
    Call.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Call.FunctionName = TEXT("SetActorTickEnabled");
    FBPSemanticExpression Target;
    Target.Kind = EBPSemanticExpressionKind::VariableReference;
    Target.Type.Kind = EBPSemanticTypeKind::Object;
    Target.Type.TypePath = Semantic.ParentClassPath;
    Target.VariableName = TEXT("this");
    Call.Arguments.Add(Target);
    FBPSemanticExpression Argument;
    Argument.Kind = EBPSemanticExpressionKind::VariableReference;
    Argument.Type.Kind = EBPSemanticTypeKind::Bool;
    Argument.VariableName = TEXT("bTestEnabled");
    Call.Arguments.Add(Argument);
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    Semantic.Functions.Add(Function);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("公開ネイティブboolを共通Callへ渡せる"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AReflectedBoolActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("共通Callでboolメンバー参照を生成"),
        Files.SourceContent.Contains(TEXT("this->SetActorTickEnabled(this->bTestEnabled);")));

    // Reflection上の型とIR型が異なる変数を安全に拒否する。
    Semantic.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1].VariableName =
        TEXT("InitialLifeSpan");
    Diagnostics.Reset();
    TestFalse(TEXT("floatプロパティをboolとして参照しない"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AReflectedBoolActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("型不一致で生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorReflectedFloatLiteralTest,
    "BlueprintAIConverter.SemanticIR.Generator.ReflectedFloatLiteral",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorReflectedFloatLiteralTest::RunTest(const FString& Parameters)
{
    // PrintString専用経路ではなく、Actorのnative UFunctionで数値正規化を検証する。
    FBPSemanticIR Semantic;
    Semantic.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    FBPSemanticExpression& Call = Statement.Expression;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Type.Kind = EBPSemanticTypeKind::Void;
    Call.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Call.FunctionName = TEXT("SetActorTickInterval");
    FBPSemanticExpression Target;
    Target.Kind = EBPSemanticExpressionKind::VariableReference;
    Target.Type.Kind = EBPSemanticTypeKind::Object;
    Target.Type.TypePath = Semantic.ParentClassPath;
    Target.VariableName = TEXT("this");
    Call.Arguments.Add(Target);
    FBPSemanticExpression Interval;
    Interval.Kind = EBPSemanticExpressionKind::Literal;
    Interval.Type.Kind = EBPSemanticTypeKind::Float;
    Interval.LiteralValue = TEXT("1e-2");
    Call.Arguments.Add(Interval);
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    Semantic.Functions.Add(Function);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("指数表記floatを共通Callで生成"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AReflectedFloatActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("数値リテラルを正規化して出力"),
        Files.SourceContent.Contains(TEXT("this->SetActorTickInterval(0.01f);")));

    Semantic.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1].LiteralValue =
        TEXT("1e100");
    Diagnostics.Reset();
    TestFalse(TEXT("float範囲外を拒否"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AReflectedFloatActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorReflectedFloatMemberTest,
    "BlueprintAIConverter.SemanticIR.Generator.ReflectedFloatMember",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorReflectedFloatMemberTest::RunTest(const FString& Parameters)
{
    // 共通CallがActorの公開ネイティブfloatをReflectionで確認して参照する。
    FBPSemanticIR Semantic;
    Semantic.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Statement;
    Statement.Kind = EBPSemanticStatementKind::Expression;
    FBPSemanticExpression& Call = Statement.Expression;
    Call.Kind = EBPSemanticExpressionKind::FunctionCall;
    Call.Type.Kind = EBPSemanticTypeKind::Void;
    Call.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Call.FunctionName = TEXT("SetActorTickInterval");
    FBPSemanticExpression Target;
    Target.Kind = EBPSemanticExpressionKind::VariableReference;
    Target.Type.Kind = EBPSemanticTypeKind::Object;
    Target.Type.TypePath = Semantic.ParentClassPath;
    Target.VariableName = TEXT("this");
    Call.Arguments.Add(Target);
    FBPSemanticExpression Argument;
    Argument.Kind = EBPSemanticExpressionKind::VariableReference;
    Argument.Type.Kind = EBPSemanticTypeKind::Float;
    Argument.VariableName = TEXT("InitialLifeSpan");
    Call.Arguments.Add(Argument);
    Block.Statements.Add(Statement);
    Function.Blocks.Add(Block);
    Semantic.Functions.Add(Function);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("公開ネイティブfloatを共通Callへ渡せる"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AReflectedFloatMemberActor"),
            FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("floatメンバー参照を生成"),
        Files.SourceContent.Contains(TEXT("this->SetActorTickInterval(this->InitialLifeSpan);")));

    // 同じメンバー名をdoubleとして偽装してもReflection型照合で拒否する。
    Semantic.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1].Type.Kind =
        EBPSemanticTypeKind::Double;
    Diagnostics.Reset();
    TestFalse(TEXT("floatメンバーをdoubleとして出力しない"),
        FBPSemanticCodeGenerator::Generate(Semantic, TEXT("AReflectedFloatMemberActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("失敗時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorForEachLocalTypeTest,
    "BlueprintAIConverter.SemanticIR.Generator.ForEachLocalType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorForEachLocalTypeTest::RunTest(const FString& Parameters)
{
    // int32配列のForEachローカルElementをfloatとして偽装したIRは拒否する。
    FBPSemanticIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPSemanticVariable Array;
    Array.Name = TEXT("Numbers");
    Array.Type.Kind = EBPSemanticTypeKind::Int32;
    Array.Type.ContainerKind = TEXT("array");
    Array.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Array);
    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = TEXT("Numbers");
    Loop.LoopArray.Type.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopArray.Type.ContainerKind = TEXT("array");
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopIndexVariable = TEXT("LoopIndex");
    Loop.LoopElementVariable = TEXT("LoopElement");
    FBPSemanticStatement Body;
    Body.Kind = EBPSemanticStatementKind::Expression;
    Body.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Body.Expression.Type.Kind = EBPSemanticTypeKind::Void;
    Body.Expression.FunctionOwnerPath = TEXT("/Script/Engine.Actor");
    Body.Expression.FunctionName = TEXT("SetActorTickInterval");
    FBPSemanticExpression Target;
    Target.Kind = EBPSemanticExpressionKind::VariableReference;
    Target.Type.Kind = EBPSemanticTypeKind::Object;
    Target.Type.TypePath = IR.ParentClassPath;
    Target.VariableName = TEXT("this");
    Body.Expression.Arguments.Add(Target);
    FBPSemanticExpression Invalid;
    Invalid.Kind = EBPSemanticExpressionKind::VariableReference;
    Invalid.Type.Kind = EBPSemanticTypeKind::Float;
    Invalid.VariableName = Loop.LoopElementVariable;
    Body.Expression.Arguments.Add(Invalid);
    Loop.LoopBodyStatements.Add(Body);
    Block.Statements.Add(Loop);
    Function.Blocks.Add(Block);
    IR.Functions.Add(Function);
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("ForEachローカルElementの型偽装を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachLocalTypeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());

    // 正常系では同じForEach内の非PrintString共通Callを出力できる。
    // まずboolリテラルでループ本体の共通Emitter経路を検証する。
    FBPSemanticExpression& BodyCall =
        IR.Functions[0].Blocks[0].Statements[0].LoopBodyStatements[0].Expression;
    BodyCall.FunctionName = TEXT("SetActorTickEnabled");
    BodyCall.Arguments[1].Kind = EBPSemanticExpressionKind::Literal;
    BodyCall.Arguments[1].Type.Kind = EBPSemanticTypeKind::Bool;
    BodyCall.Arguments[1].VariableName = NAME_None;
    BodyCall.Arguments[1].LiteralValue = TEXT("true");
    Diagnostics.Reset();
    if (TestTrue(TEXT("ForEach内の非PrintString共通Callを生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachLocalTypeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("ForEachの本体に共通Callを出力"),
        Files.SourceContent.Contains(TEXT("this->SetActorTickEnabled(true);")));
    TestFalse(TEXT("Element未使用時に要素コピーを生成しない"),
        Files.SourceContent.Contains(TEXT("const int32 LoopElement =")));

    // CompletedもPrintString専用条件を通さず、通常の共通Callで検証する。
    FBPSemanticStatement Completed;
    Completed.Kind = EBPSemanticStatementKind::Expression;
    Completed.Expression = BodyCall;
    Completed.Expression.Arguments[1].LiteralValue = TEXT("false");
    IR.Functions[0].Blocks[0].Statements[0].CompletedStatements.Add(Completed);
    Diagnostics.Reset();
    if (TestTrue(TEXT("ForEach Completedの共通Callを生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachLocalTypeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly)))
    {
        const int32 LoopEnd = Files.SourceContent.Find(TEXT("    }\n"));
        const int32 CompletedCall = Files.SourceContent.Find(
            TEXT("this->SetActorTickEnabled(false);"));
        TestTrue(TEXT("Completedはループ本体の後に出力"),
            LoopEnd != INDEX_NONE && CompletedCall > LoopEnd);
    }
    else
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
    }

    // 同じループ内でIndex/Elementの型偽装が拒否されることを検証する。
    BodyCall.FunctionName = TEXT("SetActorTickInterval");
    BodyCall.Arguments[1].Kind = EBPSemanticExpressionKind::VariableReference;
    BodyCall.Arguments[1].VariableName = TEXT("LoopIndex");
    BodyCall.Arguments[1].Type.Kind = EBPSemanticTypeKind::Float;
    BodyCall.Arguments[1].LiteralValue.Reset();
    Diagnostics.Reset();
    TestFalse(TEXT("ForEachローカルIndexの型偽装を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachLocalTypeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("Index拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());

    // 親Actorの公開ネイティブプロパティと同じIndex名も拒否する。
    // Blueprint側のIR.Variablesだけでは継承メンバーを列挙できない。
    FBPSemanticStatement& CheckedLoop = IR.Functions[0].Blocks[0].Statements[0];
    CheckedLoop.LoopIndexVariable = TEXT("InitialLifeSpan");
    BodyCall.FunctionName = TEXT("SetActorTickEnabled");
    BodyCall.Arguments[1].Kind = EBPSemanticExpressionKind::Literal;
    BodyCall.Arguments[1].Type.Kind = EBPSemanticTypeKind::Bool;
    BodyCall.Arguments[1].VariableName = NAME_None;
    BodyCall.Arguments[1].LiteralValue = TEXT("true");
    Diagnostics.Reset();
    TestFalse(TEXT("継承したネイティブメンバーとIndex名の衝突を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachLocalTypeActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("名前衝突時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorForEachInt32ArgumentsTest,
    "BlueprintAIConverter.SemanticIR.Generator.ForEachInt32Arguments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorForEachInt32ArgumentsTest::RunTest(const FString& Parameters)
{
    // PlayerControllerのSetMouseLocation(int32, int32)を使用し、
    // 共通CallがForEachのIndexとElementを直接受け取る正常系を確認する。
    FBPSemanticIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.PlayerController");
    FBPSemanticVariable Array;
    Array.Name = TEXT("Numbers");
    Array.Type.Kind = EBPSemanticTypeKind::Int32;
    Array.Type.ContainerKind = TEXT("array");
    Array.ReflectedCppType = TEXT("TArray<int32>");
    IR.Variables.Add(Array);

    FBPSemanticFunction Function;
    Function.Name = TEXT("BeginPlay");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Void;
    FBPSemanticBasicBlock Block;
    FBPSemanticStatement Loop;
    Loop.Kind = EBPSemanticStatementKind::ForEachLoop;
    Loop.LoopArray.Kind = EBPSemanticExpressionKind::VariableReference;
    Loop.LoopArray.VariableName = Array.Name;
    Loop.LoopArray.Type = Array.Type;
    Loop.LoopElementType.Kind = EBPSemanticTypeKind::Int32;
    Loop.LoopIndexVariable = TEXT("LoopIndex");
    Loop.LoopElementVariable = TEXT("LoopElement");

    FBPSemanticStatement Body;
    Body.Kind = EBPSemanticStatementKind::Expression;
    Body.Expression.Kind = EBPSemanticExpressionKind::FunctionCall;
    Body.Expression.Type.Kind = EBPSemanticTypeKind::Void;
    Body.Expression.FunctionOwnerPath = IR.ParentClassPath;
    Body.Expression.FunctionName = TEXT("SetMouseLocation");
    FBPSemanticExpression Target;
    Target.Kind = EBPSemanticExpressionKind::VariableReference;
    Target.Type.Kind = EBPSemanticTypeKind::Object;
    Target.Type.TypePath = IR.ParentClassPath;
    Target.VariableName = TEXT("this");
    Body.Expression.Arguments.Add(Target);
    FBPSemanticExpression Index;
    Index.Kind = EBPSemanticExpressionKind::VariableReference;
    Index.Type.Kind = EBPSemanticTypeKind::Int32;
    Index.VariableName = Loop.LoopIndexVariable;
    Body.Expression.Arguments.Add(Index);
    FBPSemanticExpression Element = Index;
    Element.VariableName = Loop.LoopElementVariable;
    Body.Expression.Arguments.Add(Element);
    Loop.LoopBodyStatements.Add(Body);
    Block.Statements.Add(Loop);
    Function.Blocks.Add(Block);
    IR.Functions.Add(Function);

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("ForEachのIndex/Elementを共通Call引数として生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachInt32PlayerController"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("Elementのローカル宣言を生成"),
        Files.SourceContent.Contains(TEXT("const int32 LoopElement = Numbers[LoopIndex];")));
    TestTrue(TEXT("共通CallにIndex/Elementを渡す"),
        Files.SourceContent.Contains(TEXT("this->SetMouseLocation(LoopIndex, LoopElement);")));

    // SourcePinIdを持つ参照はループローカルではなくGetter由来と扱う。
    // 名前が一致するだけでローカルへ解決すると参照元の偽装を許してしまう。
    FBPSemanticExpression& SpoofedElement = IR.Functions[0].Blocks[0].Statements[0]
        .LoopBodyStatements[0].Expression.Arguments[2];
    SpoofedElement.SourcePinId = FGuid::NewGuid();
    Diagnostics.Reset();
    TestFalse(TEXT("SourcePinId付きElementのローカル偽装を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AForEachInt32PlayerController"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("偽装拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorPrintStringQualifierTest,
    "BlueprintAIConverter.SemanticIR.Generator.PrintStringQualifier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorPrintStringQualifierTest::RunTest(const FString& Parameters)
{
    // 正常なPrintString IRから文字列引数のみを変異させ、
    // コンテナや参照修飾をC++の単一FStringとして誤出力しないことを確認する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("PrintStringのLowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticExpression& TextArgument =
        IR.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1];
    TextArgument.Kind = EBPSemanticExpressionKind::VariableReference;
    TextArgument.VariableName = TEXT("TextValue");
    TextArgument.Type.Kind = EBPSemanticTypeKind::String;
    TextArgument.LiteralValue.Reset();

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TextArgument.Type.ContainerKind = TEXT("array");
    TestFalse(TEXT("PrintStringのFString配列参照を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AStringQualifierActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("配列拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());

    TextArgument.Type.ContainerKind.Reset();
    TextArgument.Type.bIsReference = true;
    Diagnostics.Reset();
    TestFalse(TEXT("PrintStringのFString参照修飾を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AStringQualifierActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("参照拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());

    TextArgument.Type.bIsReference = false;
    TextArgument.Type.bIsConst = true;
    Diagnostics.Reset();
    TestFalse(TEXT("PrintStringのconst修飾を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AStringQualifierActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("const拒否時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorPrintStringNativeMemberTest,
    "BlueprintAIConverter.SemanticIR.Generator.PrintStringNativeMember",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorPrintStringNativeMemberTest::RunTest(const FString& Parameters)
{
    // PrintStringの文字列引数がActorのfloatプロパティをFStringとして
    // 偽装しても、ネイティブReflectionの実型と一致しないため拒否する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("PrintStringのLowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticExpression& Argument =
        IR.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1];
    Argument.Kind = EBPSemanticExpressionKind::VariableReference;
    Argument.VariableName = TEXT("InitialLifeSpan");
    Argument.Type.Kind = EBPSemanticTypeKind::String;
    Argument.LiteralValue.Reset();
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("floatネイティブメンバーのFString偽装を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AStringNativeMemberActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("型偽装時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorPrintStringDeclaredVariableTest,
    "BlueprintAIConverter.SemanticIR.Generator.PrintStringDeclaredVariable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorPrintStringDeclaredVariableTest::RunTest(const FString& Parameters)
{
    // PrintStringに渡すBlueprint変数名が実在しても、
    // 宣言された型がFString以外なら型偽装として拒否する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("PrintStringのLowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticVariable Variable;
    Variable.Name = TEXT("Message");
    Variable.Type.Kind = EBPSemanticTypeKind::Int32;
    Variable.ReflectedCppType = TEXT("int32");
    IR.Variables.Add(Variable);
    FBPSemanticExpression& Argument =
        IR.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1];
    Argument.Kind = EBPSemanticExpressionKind::VariableReference;
    Argument.VariableName = Variable.Name;
    Argument.Type.Kind = EBPSemanticTypeKind::String;
    Argument.LiteralValue.Reset();

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("Blueprintのint32変数をFStringとして偽装すると拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AStringDeclaredVariableActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("型偽装時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorPrintStringUnknownMemberTest,
    "BlueprintAIConverter.SemanticIR.Generator.PrintStringUnknownMember",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorPrintStringUnknownMemberTest::RunTest(const FString& Parameters)
{
    // Blueprint宣言にも親ActorのReflectionにもない文字列メンバー名は、
    // 未定義識別子を含むC++を生成しないよう拒否する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("PrintStringのLowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), IR)) == false)
    {
        return false;
    }
    FBPSemanticExpression& Argument =
        IR.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1];
    Argument.Kind = EBPSemanticExpressionKind::VariableReference;
    Argument.VariableName = TEXT("UnknownPrintStringMemberForTest");
    Argument.Type.Kind = EBPSemanticTypeKind::String;
    Argument.LiteralValue.Reset();

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("未宣言のFStringメンバーを拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AUnknownStringMemberActor"),
            FString(), Files, Diagnostics));
    TestTrue(TEXT("未宣言参照の拒否時に生成物を公開しない"),
        Files.SourceContent.IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticGeneratorPrintStringIntegerExpressionTest,
    "BlueprintAIConverter.SemanticIR.Generator.PrintStringIntegerExpression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticGeneratorPrintStringIntegerExpressionTest::RunTest(const FString& Parameters)
{
    // Conv_IntToStringの内側でも、Getter由来のメンバーとループローカルを
    // SourcePinIdだけで信用せず、宣言型・Reflection・スコープと照合する。
    FBPSemanticIR IR;
    if (TestTrue(TEXT("PrintStringのLowering成功"),
        FBPSemanticLowering::LowerPrintString(MakeGeneratorTestGraph(), IR)) == false)
    {
        return false;
    }

    FBPSemanticVariable Counter;
    Counter.Name = TEXT("Counter");
    Counter.Type.Kind = EBPSemanticTypeKind::Int32;
    Counter.ReflectedCppType = TEXT("int32");
    IR.Variables.Add(Counter);

    FBPSemanticExpression Member;
    Member.Kind = EBPSemanticExpressionKind::VariableReference;
    Member.Type.Kind = EBPSemanticTypeKind::Int32;
    Member.VariableName = Counter.Name;
    Member.SourcePinId = FGuid::NewGuid();

    FBPSemanticExpression One;
    One.Kind = EBPSemanticExpressionKind::Literal;
    One.Type.Kind = EBPSemanticTypeKind::Int32;
    One.LiteralValue = TEXT("1");

    FBPSemanticExpression Addition;
    Addition.Kind = EBPSemanticExpressionKind::FunctionCall;
    Addition.Type.Kind = EBPSemanticTypeKind::Int32;
    Addition.FunctionOwnerPath = TEXT("/Script/Engine.KismetMathLibrary");
    Addition.FunctionName = TEXT("Add_IntInt");
    Addition.Arguments.Add(Member);
    Addition.Arguments.Add(One);

    FBPSemanticExpression Conversion;
    Conversion.Kind = EBPSemanticExpressionKind::FunctionCall;
    Conversion.Type.Kind = EBPSemanticTypeKind::String;
    Conversion.FunctionOwnerPath = TEXT("/Script/Engine.KismetStringLibrary");
    Conversion.FunctionName = TEXT("Conv_IntToString");
    Conversion.Arguments.Add(Addition);
    IR.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1] = Conversion;

    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("宣言済みint32メンバーのネスト式を生成"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AIntegerExpressionActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly)) == false)
    {
        for (const FString& Diagnostic : Diagnostics)
        {
            AddError(Diagnostic);
        }
        return false;
    }
    TestTrue(TEXT("メンバー参照をthisで修飾"), Files.SourceContent.Contains(
        TEXT("UKismetStringLibrary::Conv_IntToString((this->Counter + 1))")));

    // 同じ式でActorのfloatメンバーをint32として偽装しても、
    // 親クラスReflectionの実型と一致しないため拒否する。
    FBPSemanticExpression& CheckedMember =
        IR.Functions[0].Blocks[0].Statements[0].Expression.Arguments[1].Arguments[0].Arguments[0];
    CheckedMember.VariableName = TEXT("InitialLifeSpan");
    IR.Variables.Reset();
    Diagnostics.Reset();
    TestFalse(TEXT("ネスト式のネイティブメンバー型偽装を拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AIntegerExpressionActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("型偽装時に生成物を公開しない"), Files.SourceContent.IsEmpty());

    // SourcePinIdなしの名前は、実際のループスコープから渡された
    // LocalIndex/LocalElementでなければローカル変数として扱わない。
    CheckedMember.VariableName = TEXT("SpoofedLoopIndex");
    CheckedMember.SourcePinId.Invalidate();
    Diagnostics.Reset();
    TestFalse(TEXT("スコープ外のローカルIndexを拒否"),
        FBPSemanticCodeGenerator::Generate(IR, TEXT("AIntegerExpressionActor"),
            FString(), Files, Diagnostics, EBPVariableFallbackMode::DeclarationOnly));
    TestTrue(TEXT("ローカル偽装時に生成物を公開しない"), Files.SourceContent.IsEmpty());
    return true;
}

#endif
