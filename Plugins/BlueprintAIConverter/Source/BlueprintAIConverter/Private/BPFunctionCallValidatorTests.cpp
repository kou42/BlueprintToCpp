#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPFunctionCallValidator.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPFunctionCallValidatorTest,
    "BlueprintAIConverter.SemanticIR.FunctionCall.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPFunctionCallValidatorTest::RunTest(const FString& Parameters)
{
    const UFunction* Function = AActor::StaticClass()->FindFunctionByName(TEXT("K2_GetActorLocation"));
    if (TestNotNull(TEXT("GetActorLocation function exists"), Function) == false)
    {
        return false;
    }

    FBPIRNode Node;
    Node.ClassPath = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    Node.FunctionName = Function->GetFName();
    FBPIRPin ReturnPin;
    ReturnPin.Name = TEXT("ReturnValue");
    ReturnPin.Direction = EGPD_Output;
    ReturnPin.Category = TEXT("struct");
    Node.Pins.Add(ReturnPin);

    FBPResolvedFunctionSignature Signature;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Matching function call accepted"),
        FBPFunctionCallValidator::Validate(Node, Function, Signature, Diagnostics));
    TestEqual(TEXT("Resolved owner"), Signature.OwnerClassPath, FString(TEXT("/Script/Engine.Actor")));

    Diagnostics.Reset();
    Node.Pins[0].Direction = EGPD_Input;
    TestFalse(TEXT("Wrong return pin direction rejected"),
        FBPFunctionCallValidator::Validate(Node, Function, Signature, Diagnostics));
    TestTrue(TEXT("Invalid signature is not exposed"), Signature.FunctionName.IsNone());

    Diagnostics.Reset();
    Node.Pins[0].Direction = EGPD_Output;
    FBPIRPin Unknown;
    Unknown.Name = TEXT("UnexpectedArgument");
    Unknown.Direction = EGPD_Input;
    Unknown.Category = TEXT("int");
    Node.Pins.Add(Unknown);
    TestFalse(TEXT("Unknown data pin rejected"),
        FBPFunctionCallValidator::Validate(Node, Function, Signature, Diagnostics));

    Diagnostics.Reset();
    Node.Pins.Pop();
    Node.FunctionName = TEXT("OtherFunction");
    TestFalse(TEXT("Mismatched function reference rejected"),
        FBPFunctionCallValidator::Validate(Node, Function, Signature, Diagnostics));
    return true;
}
#endif
