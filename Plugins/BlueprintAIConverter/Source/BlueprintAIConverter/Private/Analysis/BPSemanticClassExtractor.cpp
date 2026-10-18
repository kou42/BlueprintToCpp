#include "BPSemanticClassExtractor.h"

#include "Engine/Blueprint.h"
#include "UObject/Class.h"
#include "UObject/Interface.h"
#include "UObject/UnrealType.h"

bool FBPSemanticClassExtractor::Extract(const UClass* Class, FBPSemanticClassInfo& OutInfo)
{
    OutInfo = FBPSemanticClassInfo();
    if (Class == nullptr)
    {
        return false;
    }

    // ネイティブクラスはClassGeneratedByを持たず、Blueprint生成クラスとは別に扱う。
    // 不明な出自をネイティブと推測しないため、CLASS_Nativeを明示的に検査する。
    if (Cast<UBlueprint>(Class->ClassGeneratedBy) != nullptr)
    {
        OutInfo.Origin = EBPSemanticClassOrigin::Blueprint;
    }
    else if (Class->HasAnyClassFlags(CLASS_Native))
    {
        OutInfo.Origin = EBPSemanticClassOrigin::Native;
    }

    OutInfo.ClassPath = Class->GetPathName();
    OutInfo.ClassName = Class->GetName();

    // 直接の親がBlueprintの場合も、ネイティブ親を辿って依存情報を記録する。
    const UClass* Parent = Class->GetSuperClass();
    while (Parent != nullptr)
    {
        if (Parent->HasAnyClassFlags(CLASS_Native))
        {
            OutInfo.NativeParentClassPath = Parent->GetPathName();
            break;
        }
        Parent = Parent->GetSuperClass();
    }

    for (const FImplementedInterface& Interface : Class->Interfaces)
    {
        if (Interface.Class != nullptr)
        {
            OutInfo.InterfacePaths.AddUnique(Interface.Class->GetPathName());
        }
    }
    OutInfo.InterfacePaths.Sort();

    // IncludeSuperを指定せず、このクラスで宣言されたリフレクションメンバーのみを収集する。
    // Blueprintのグラフ本体とネイティブ関数のC++本体はここから復元できない。
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property == nullptr)
        {
            continue;
        }
        FBPSemanticClassMember Member;
        Member.Name = Property->GetName();
        Member.CppType = Property->GetCPPType();
        Member.ReflectionPath = Property->GetPathName();
        Member.Flags = FString::Printf(TEXT("0x%llx"),
            static_cast<unsigned long long>(Property->GetPropertyFlags()));
        OutInfo.DeclaredProperties.Add(MoveTemp(Member));
    }
    for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        const UFunction* Function = *It;
        if (Function == nullptr)
        {
            continue;
        }
        FBPSemanticClassMember Member;
        Member.Name = Function->GetName();
        Member.ReflectionPath = Function->GetPathName();
        Member.Flags = FString::Printf(TEXT("0x%x"),
            static_cast<uint32>(Function->FunctionFlags));
        // 戻り値や引数を持つ関数の署名を関数名だけから推測しない。
        OutInfo.DeclaredFunctions.Add(MoveTemp(Member));

        FBPSemanticReflectedFunction Signature;
        Signature.Name = Function->GetName();
        Signature.ReflectionPath = Function->GetPathName();
        Signature.Flags = FString::Printf(TEXT("0x%x"), static_cast<uint32>(Function->FunctionFlags));
        Signature.bIsNative = Function->HasAnyFunctionFlags(FUNC_Native);
        Signature.bIsStatic = Function->HasAnyFunctionFlags(FUNC_Static);
        Signature.bIsConst = Function->HasAnyFunctionFlags(FUNC_Const);
        Signature.bIsBlueprintEvent = Function->HasAnyFunctionFlags(FUNC_BlueprintEvent);
        Signature.bIsNet = Function->HasAnyFunctionFlags(FUNC_Net);
        Signature.bIsFinal = Function->HasAnyFunctionFlags(FUNC_Final);
        // CPF_Parmを持つFPropertyだけが引数・戻り値。ローカルプロパティを混入させない。
        for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
        {
            const FProperty* Param = *ParamIt;
            if (Param == nullptr || Param->HasAnyPropertyFlags(CPF_Parm) == false)
            {
                continue;
            }
            if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                Signature.ReturnCppType = Param->GetCPPType();
                continue;
            }
            FBPSemanticReflectedParameter Parameter;
            Parameter.Name = Param->GetName();
            Parameter.CppType = Param->GetCPPType();
            Parameter.bIsOut = Param->HasAnyPropertyFlags(CPF_OutParm);
            Parameter.bIsReference = Param->HasAnyPropertyFlags(CPF_ReferenceParm);
            Parameter.bIsConst = Param->HasAnyPropertyFlags(CPF_ConstParm);
            Signature.Parameters.Add(MoveTemp(Parameter));
        }
        OutInfo.DeclaredFunctionSignatures.Add(MoveTemp(Signature));
    }
    OutInfo.DeclaredProperties.Sort([](const FBPSemanticClassMember& A, const FBPSemanticClassMember& B)
    {
        return A.Name < B.Name;
    });
    OutInfo.DeclaredFunctions.Sort([](const FBPSemanticClassMember& A, const FBPSemanticClassMember& B)
    {
        return A.Name < B.Name;
    });
    OutInfo.DeclaredFunctionSignatures.Sort([](const FBPSemanticReflectedFunction& A,
        const FBPSemanticReflectedFunction& B)
    {
        return A.Name < B.Name;
    });
    return true;
}

bool FBPSemanticClassExtractor::Append(const UBlueprint* Blueprint, FBPSemanticIR& IR)
{
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
    {
        // 未コンパイルのBlueprintでは不完全なクラス情報を作成しない。
        return false;
    }
    return Extract(Blueprint->GeneratedClass, IR.ClassInfo);
}
