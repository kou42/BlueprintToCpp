#include "BPSemanticClassExtractor.h"

#include "Engine/Blueprint.h"
#include "UObject/Class.h"
#include "UObject/Interface.h"

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
