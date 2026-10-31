#include "Misc/AutomationTest.h"
#include "BPSemanticCodeGenerator.h"
#include "BPSemanticJsonSerializer.h"
#include "BPInterfaceContractTestTypes.h"
#include "BPEmitterTestActor.h"
#include "BPFunctionSignatureResolver.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FBPSemanticIR MakeInterfaceContract()
{
    FBPSemanticIR IR;
    IR.AssetPath = TEXT("/Game/BP_InterfaceContract");
    IR.ParentClassPath = TEXT("/Script/CoreUObject.Object");
    IR.ClassInfo.InterfacePaths.Add(UBPValueContract::StaticClass()->GetPathName());
    FBPSemanticFunction Function;
    Function.Name = TEXT("ReadValue");
    Function.ReturnType.Kind = EBPSemanticTypeKind::Int32;
    Function.SignatureSource = TEXT("generated_class_reflection");
    Function.bIsBlueprintEvent = true;
    Function.bIsInterfaceImplementation = true;
    Function.bIsConst = true;
    Function.InterfaceClassPath = IR.ClassInfo.InterfacePaths[0];
    Function.InterfaceFunctionPath = UBPValueContract::StaticClass()->FindFunctionByName(Function.Name)->GetPathName();
    FBPSemanticParameter Input;
    Input.Name = TEXT("Value");
    Input.Type.Kind = EBPSemanticTypeKind::Int32;
    Input.DeclarationIndex = 0;
    Function.Parameters.Add(Input);
    FBPSemanticStatement Return;
    Return.Kind = EBPSemanticStatementKind::Return;
    Return.Expression.Kind = EBPSemanticExpressionKind::VariableReference;
    Return.Expression.Type.Kind = EBPSemanticTypeKind::Int32;
    Return.Expression.VariableName = Input.Name;
    FBPSemanticBasicBlock Block;
    Block.Statements.Add(Return);
    Function.Blocks.Add(Block);
    IR.Functions.Add(Function);
    return IR;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticInterfaceContractTest,
    "BlueprintAIConverter.SemanticIR.Generator.InterfaceContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticInterfaceContractTest::RunTest(const FString& Parameters)
{
    const FBPSemanticIR Valid = MakeInterfaceContract();
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("新規Interfaceのconst戻り値実装を生成"), FBPSemanticCodeGenerator::Generate(
            Valid, TEXT("UInterfaceObject"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics) { AddError(Message); }
        return false;
    }
    TestTrue(TEXT("明示的なIInterface継承"), Files.HeaderContent.Contains(TEXT(": public UObject, public IBPValueContract")));
    const int32 Include = Files.HeaderContent.Find(TEXT("#include \"Tests/BPInterfaceContractTestTypes.h\""));
    TestTrue(TEXT("Interfaceヘッダーはgenerated.hより前"), Include != INDEX_NONE &&
        Include < Files.HeaderContent.Find(TEXT("#include \"InterfaceObject.generated.h\"")));
    TestTrue(TEXT("非Event Graph由来もImplementation overrideを生成"), Files.HeaderContent.Contains(
        TEXT("virtual int32 ReadValue_Implementation(int32 Value) const override;")));
    TestTrue(TEXT("入力引数を返す"), Files.SourceContent.Contains(TEXT("return Value;")));
    TestFalse(TEXT("新規Interfaceで存在しないSuperを呼ばない"), Files.SourceContent.Contains(TEXT("Super::")));

    FBPSemanticIR Multiple = Valid;
    FBPSemanticFunction Ordinary = Valid.Functions[0];
    Ordinary.Name = TEXT("ReadOrdinary");
    Ordinary.bIsBlueprintEvent = false;
    Ordinary.bIsInterfaceImplementation = false;
    Ordinary.InterfaceClassPath.Reset();
    Ordinary.InterfaceFunctionPath.Reset();
    Ordinary.Visibility = EBPSemanticFunctionVisibility::Private;
    Multiple.Functions.Add(Ordinary);
    TestTrue(TEXT("Interface実装とprivate通常関数を一つのクラスへ生成"), FBPSemanticCodeGenerator::Generate(
        Multiple, TEXT("UInterfaceObject"), FString(), Files, Diagnostics));
    TestTrue(TEXT("通常関数の可視性を保持"), Files.HeaderContent.Contains(TEXT("private:\n    UFUNCTION(BlueprintCallable)")));
    const TSharedRef<FJsonObject> Json = FBPSemanticJsonSerializer::ToJson(Multiple, TEXT("5.7"));
    TestEqual(TEXT("JSONに明示Interfaceを保持"), Json->GetObjectField(TEXT("class_info"))
        ->GetArrayField(TEXT("interface_paths")).Num(), 1);
    TestEqual(TEXT("JSONに両方の関数を保持"), Json->GetArrayField(TEXT("functions")).Num(), 2);

    FBPSemanticIR Inherited = Valid;
    Inherited.ParentClassPath = UBPInheritedNotifyObject::StaticClass()->GetPathName();
    Inherited.ClassInfo.InterfacePaths.Reset();
    FBPSemanticFunction& Notify = Inherited.Functions[0];
    Notify.Name = TEXT("NotifyValue");
    Notify.ReturnType.Kind = EBPSemanticTypeKind::Void;
    Notify.bIsConst = false;
    Notify.InterfaceClassPath = UBPNotifyContract::StaticClass()->GetPathName();
    Notify.InterfaceFunctionPath = UBPNotifyContract::StaticClass()->FindFunctionByName(Notify.Name)->GetPathName();
    if (const UFunction* ParentMethod = UBPInheritedNotifyObject::StaticClass()->FindFunctionByName(Notify.Name))
    {
        Notify.bIsOverride = true;
        Notify.OverriddenFunctionPath = ParentMethod->GetPathName();
    }
    Notify.Blocks[0].Statements[0].Expression = FBPSemanticExpression();
    Notify.Blocks[0].Statements[0].Expression.Type.Kind = EBPSemanticTypeKind::Void;
    TestTrue(TEXT("親から継承したInterface実装を生成"), FBPSemanticCodeGenerator::Generate(
        Inherited, TEXT("UInheritedObject"), FString(), Files, Diagnostics));
    TestFalse(TEXT("親のInterfaceを重複継承しない"), Files.HeaderContent.Contains(TEXT(", public IBPNotifyContract")));

    for (int32 CaseIndex = 0; CaseIndex < 11; ++CaseIndex)
    {
        FBPSemanticIR Invalid = Valid;
        FBPSemanticFunction& Function = Invalid.Functions[0];
        switch (CaseIndex)
        {
        case 0: Invalid.ClassInfo.InterfacePaths.Reset(); break;
        case 1: Invalid.ClassInfo.InterfacePaths.Add(Invalid.ClassInfo.InterfacePaths[0]); break;
        case 2: Invalid.ClassInfo.InterfacePaths[0] = TEXT("/Script/CoreUObject.Object"); break;
        case 3: Function.InterfaceFunctionPath += TEXT("Missing"); break;
        case 4: Function.Parameters[0].Type.Kind = EBPSemanticTypeKind::Bool; break;
        case 5: Function.bIsConst = false; break;
        case 6: Function.bIsInterfaceImplementation = false; Function.InterfaceClassPath.Reset(); Function.InterfaceFunctionPath.Reset(); break;
        case 7: Function.Name = TEXT("MissingImplementation"); break;
        case 8: Function.Parameters[0].bIsOut = true; break;
        case 9: Invalid.ClassInfo.InterfacePaths.Add(UBPNotifyContract::StaticClass()->GetPathName()); break;
        case 10: Invalid.ClassInfo.NativeParentClassPath = TEXT("/Script/Engine.Actor"); break;
        }
        Files.HeaderContent = TEXT("stale");
        Files.SourceContent = TEXT("stale");
        TestFalse(TEXT("不正なInterface宣言・署名・実装漏れを拒否"), FBPSemanticCodeGenerator::Generate(
            Invalid, TEXT("UInterfaceObject"), FString(), Files, Diagnostics));
        TestTrue(TEXT("拒否時に診断を保持し生成物を公開しない"), Diagnostics.IsEmpty() == false &&
            Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticClassDeclarationContractTest,
    "BlueprintAIConverter.SemanticIR.Generator.ClassDeclarationContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticClassDeclarationContractTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR Base = MakeInterfaceContract();
    Base.ClassInfo.InterfacePaths.Reset();
    Base.Functions[0].bIsInterfaceImplementation = false;
    Base.Functions[0].bIsBlueprintEvent = false;
    Base.Functions[0].InterfaceClassPath.Reset();
    Base.Functions[0].InterfaceFunctionPath.Reset();
    for (int32 CaseIndex = 0; CaseIndex < 9; ++CaseIndex)
    {
        FBPSemanticIR IR = Base;
        FString ClassName = TEXT("UContractObject");
        switch (CaseIndex)
        {
        case 0: IR.Functions[0].Name = TEXT("class"); break;
        case 1: IR.Functions[0].Parameters[0].Name = TEXT("return"); break;
        case 2: IR.Functions[0].Name = TEXT("UContractObject"); break;
        case 3: IR.Functions[0].Name = TEXT("StaticClass"); break;
        case 4:
        {
            FBPSemanticVariable Member;
            Member.Name = IR.Functions[0].Name;
            IR.Variables.Add(Member);
            break;
        }
        case 5: ClassName = TEXT("UObject"); break;
        case 6: IR.Functions.Add(IR.Functions[0]); break;
        case 7:
        case 8:
        {
            IR.Functions[0].bIsEventGraph = true;
            IR.Functions[0].bIsBlueprintEvent = true;
            FBPSemanticFunction Other = IR.Functions[0];
            Other.Name = TEXT("ReadValue_Implementation");
            IR.Functions.Add(Other);
            if (CaseIndex == 8) { IR.Functions.Swap(0, 1); }
            break;
        }
        }
        FBPGeneratedActorFiles Files;
        TArray<FString> Diagnostics;
        TestFalse(TEXT("単一・複数関数の宣言衝突と予約語を拒否"),
            FBPSemanticCodeGenerator::Generate(IR, ClassName, FString(), Files, Diagnostics));
        TestTrue(TEXT("拒否時に生成物なし・診断あり"), Files.HeaderContent.IsEmpty() &&
            Files.SourceContent.IsEmpty() && Diagnostics.IsEmpty() == false);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticNativeReturnOverrideTest,
    "BlueprintAIConverter.SemanticIR.Generator.NativeReturnOverride",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticNativeReturnOverrideTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR IR = MakeInterfaceContract();
    IR.ParentClassPath = ABPEmitterTestActor::StaticClass()->GetPathName();
    IR.ClassInfo.InterfacePaths.Reset();
    FBPSemanticFunction& Function = IR.Functions[0];
    Function.Name = TEXT("VerifiedReturn");
    Function.bIsInterfaceImplementation = false;
    Function.InterfaceClassPath.Reset();
    Function.InterfaceFunctionPath.Reset();
    Function.bIsOverride = true;
    Function.OverriddenFunctionPath = ABPEmitterTestActor::StaticClass()->FindFunctionByName(Function.Name)->GetPathName();
    Function.SuperCallPolicy = EBPSemanticSuperCallPolicy::Omit;
    FBPGeneratedActorFiles Files;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("Super省略が明示された非void native overrideを生成"),
            FBPSemanticCodeGenerator::Generate(IR, TEXT("AReturnActor"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics) { AddError(Message); }
        return false;
    }
    TestTrue(TEXT("const戻り値overrideの宣言"), Files.HeaderContent.Contains(
        TEXT("virtual int32 VerifiedReturn_Implementation(int32 Value) const override;")));
    TestTrue(TEXT("overrideの戻り値本体を保持"), Files.SourceContent.Contains(TEXT("return Value;")));
    TestFalse(TEXT("Superの戻り値を暗黙破棄しない"), Files.SourceContent.Contains(TEXT("Super::")));
    const TSharedRef<FJsonObject> Json = FBPSemanticJsonSerializer::ToJson(IR, TEXT("5.7"));
    TestEqual(TEXT("JSONにSuper省略方針を保持"), Json->GetArrayField(TEXT("functions"))[0]
        ->AsObject()->GetStringField(TEXT("super_call_policy")), FString(TEXT("omit")));
    Function.SuperCallPolicy = EBPSemanticSuperCallPolicy::Automatic;
    TestFalse(TEXT("方針未指定の非void Superを拒否"), FBPSemanticCodeGenerator::Generate(
        IR, TEXT("AReturnActor"), FString(), Files, Diagnostics));
    TestTrue(TEXT("拒否時に生成物を公開しない"), Files.HeaderContent.IsEmpty() && Files.SourceContent.IsEmpty());
    Function.SuperCallPolicy = static_cast<EBPSemanticSuperCallPolicy>(255);
    TestFalse(TEXT("未知のSuper方針を拒否"), FBPSemanticCodeGenerator::Generate(
        IR, TEXT("AReturnActor"), FString(), Files, Diagnostics));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSemanticInterfaceArraySignatureTest,
    "BlueprintAIConverter.SemanticIR.Generator.InterfaceArraySignature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSemanticInterfaceArraySignatureTest::RunTest(const FString& Parameters)
{
    FBPSemanticIR IR = MakeInterfaceContract();
    IR.ClassInfo.InterfacePaths = { UBPArrayContract::StaticClass()->GetPathName() };
    FBPSemanticFunction& Function = IR.Functions[0];
    Function.Name = TEXT("CountValues");
    Function.InterfaceClassPath = IR.ClassInfo.InterfacePaths[0];
    const UFunction* Target = UBPArrayContract::StaticClass()->FindFunctionByName(Function.Name);
    if (TestNotNull(TEXT("配列Interfaceの反射署名"), Target) == false) { return false; }
    Function.InterfaceFunctionPath = Target->GetPathName();
    FBPResolvedFunctionSignature Signature;
    TArray<FString> Diagnostics;
    if (TestTrue(TEXT("配列署名をReflectionから解決"),
            FBPFunctionSignatureResolver::Resolve(Target, Signature, Diagnostics)) == false ||
        TestEqual(TEXT("配列入力数"), Signature.ParameterTypes.Num(), 1) == false) { return false; }
    Function.Parameters[0].Name = TEXT("Values");
    Function.Parameters[0].Type = Signature.ParameterTypes[0];
    const FProperty* Property = FindFProperty<FProperty>(Target, TEXT("Values"));
    if (TestNotNull(TEXT("配列入力Property"), Property) == false) { return false; }
    Function.Parameters[0].bIsOut = Property->HasAnyPropertyFlags(CPF_OutParm);
    FBPSemanticExpression& Return = Function.Blocks[0].Statements[0].Expression;
    Return.Kind = EBPSemanticExpressionKind::Literal;
    Return.VariableName = NAME_None;
    Return.LiteralValue = TEXT("0");
    FBPGeneratedActorFiles Files;
    if (TestTrue(TEXT("Reflection由来const配列入力を生成"), FBPSemanticCodeGenerator::Generate(
            IR, TEXT("UArrayContractObject"), FString(), Files, Diagnostics)) == false)
    {
        for (const FString& Message : Diagnostics) { AddError(Message); }
        return false;
    }
    TestTrue(TEXT("配列根のKindに依存せず要素型を生成"), Files.HeaderContent.Contains(
        TEXT("CountValues_Implementation(const TArray<int32>& Values) const override;")));
    return true;
}
#endif
