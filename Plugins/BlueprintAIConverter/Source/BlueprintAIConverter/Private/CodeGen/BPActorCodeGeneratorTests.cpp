#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPActorCodeGenerator.h"
#include "GameFramework/Pawn.h"

namespace
{
FBPGraphIR MakeLiteralPrintStringIR()
{
    FBPGraphIR IR;
    IR.ParentClassPath = TEXT("/Script/Engine.Actor");
    FBPIRGraph Graph;
    Graph.Kind = TEXT("event");

    FBPIRNode Event;
    Event.Id = FGuid::NewGuid();
    Event.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_Event");
    Event.FunctionName = TEXT("ReceiveBeginPlay");

    FBPIRNode Call;
    Call.Id = FGuid::NewGuid();
    Call.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Call.FunctionName = TEXT("PrintString");

    FBPIRPin Input;
    Input.Id = FGuid::NewGuid();
    Input.Name = TEXT("InString");
    Input.Category = TEXT("string");
    Input.Direction = EGPD_Input;
    Input.DefaultValue = TEXT("Hello \"UE\"\\n");
    Call.Pins.Add(Input);

    FBPIRLink Edge;
    Edge.SourceNode = Event.Id;
    Edge.TargetNode = Call.Id;
    Graph.ExecEdges.Add(Edge);
    Graph.Nodes.Add(MoveTemp(Event));
    Graph.Nodes.Add(MoveTemp(Call));
    IR.Graphs.Add(MoveTemp(Graph));
    return IR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPActorCodeGeneratorLiteralTest,
    "BlueprintAIConverter.CodeGenerator.LiteralPrintString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPActorCodeGeneratorLiteralTest::RunTest(const FString& Parameters)
{
    const FBPGraphIR IR = MakeLiteralPrintStringIR();
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Actor C++ generated"), FBPActorCodeGenerator::Generate(
        IR, TEXT("AGeneratedPrintActor"), TEXT("MYGAME_API"), Files, Diagnostics));
    TestEqual(TEXT("Header file name"), Files.HeaderFileName, FString(TEXT("GeneratedPrintActor.h")));
    TestEqual(TEXT("Source file name"), Files.SourceFileName, FString(TEXT("GeneratedPrintActor.cpp")));
    TestTrue(TEXT("UHT generated include"), Files.HeaderContent.Contains(TEXT("GeneratedPrintActor.generated.h")));
    TestTrue(TEXT("Module API macro"), Files.HeaderContent.Contains(TEXT("MYGAME_API AGeneratedPrintActor")));
    TestTrue(TEXT("BeginPlay override"), Files.SourceContent.Contains(TEXT("AGeneratedPrintActor::BeginPlay()")));
    TestTrue(TEXT("PrintString API"), Files.SourceContent.Contains(TEXT("UKismetSystemLibrary::PrintString")));
    TestTrue(TEXT("Escaped quotes"), Files.SourceContent.Contains(TEXT("\\\"UE\\\"")));
    TestTrue(TEXT("Duration is a float literal"), Files.SourceContent.Contains(TEXT("2.0f")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPActorCodeGeneratorRejectTest,
    "BlueprintAIConverter.CodeGenerator.RejectInvalidInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPActorCodeGeneratorRejectTest::RunTest(const FString& Parameters)
{
    FBPGraphIR IR = MakeLiteralPrintStringIR();
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;

    TestFalse(TEXT("Invalid class name rejected"), FBPActorCodeGenerator::Generate(
        IR, TEXT("GeneratedActor"), TEXT(""), Files, Diagnostics));
    TestTrue(TEXT("No output on failure"), Files.HeaderContent.IsEmpty());

    TestFalse(TEXT("Invalid API macro rejected"), FBPActorCodeGenerator::Generate(
        IR, TEXT("AGeneratedActor"), TEXT("invalid macro"), Files, Diagnostics));

    IR.ParentClassPath = APawn::StaticClass()->GetPathName();
    TestTrue(TEXT("Pawn parent generated"), FBPActorCodeGenerator::Generate(
        IR, TEXT("AGeneratedPawn"), TEXT(""), Files, Diagnostics));
    TestTrue(TEXT("Pawn header included"),
        Files.HeaderContent.Contains(TEXT("#include \"GameFramework/Pawn.h\"")));
    TestTrue(TEXT("Pawn inheritance generated"),
        Files.HeaderContent.Contains(TEXT("class AGeneratedPawn : public APawn")));

    IR.ParentClassPath = UObject::StaticClass()->GetPathName();
    TestFalse(TEXT("Non-Actor parent rejected"), FBPActorCodeGenerator::Generate(
        IR, TEXT("AGeneratedActor"), TEXT(""), Files, Diagnostics));
    return true;
}
#endif
