#include "BPReflectedClassPreview.h"

namespace
{
bool IsSafeName(const FString& Name)
{
    if (Name.IsEmpty())
    {
        return false;
    }
    for (int32 Index = 0; Index < Name.Len(); ++Index)
    {
        const TCHAR Ch = Name[Index];
        if (Index == 0 && FChar::IsAlpha(Ch) == false && Ch != TEXT('_'))
        {
            return false;
        }
        if (FChar::IsAlnum(Ch) == false && Ch != TEXT('_'))
        {
            return false;
        }
    }
    return true;
}
}

bool FBPReflectedClassPreview::Generate(
    const FBPSemanticClassInfo& Info, FString& OutText, FString& OutError)
{
    OutText.Reset();
    OutError.Reset();
    if (Info.Origin == EBPSemanticClassOrigin::Unknown || IsSafeName(Info.ClassName) == false)
    {
        OutError = TEXT("Unresolved class origin or invalid class name");
        return false;
    }

    // リフレクションだけではアクセス指定子、ヘッダー依存関係、関数本体を復元できない。
    // 既存UCLASSとの重複定義を防ぐため、意図的にコンパイル可能なUCLASS宣言は出力しない。
    OutText = TEXT("// REFLECTION REFERENCE ONLY - NOT COMPILABLE C++\n");
    OutText += TEXT("// Existing UCLASS must not be redefined. Function bodies are unavailable.\n");
    OutText += FString::Printf(TEXT("// Class: %s\n// Path: %s\n"),
        *Info.ClassName, *Info.ClassPath);
    OutText += FString::Printf(TEXT("// Native parent: %s\n\n"), *Info.NativeParentClassPath);
    for (const FString& InterfacePath : Info.InterfacePaths)
    {
        OutText += FString::Printf(TEXT("// Interface: %s\n"), *InterfacePath);
    }
    OutText += TEXT("\n// Declared properties\n");
    for (const FBPSemanticClassMember& Property : Info.DeclaredProperties)
    {
        // 型文字列をそのままC++宣言へ挿入すると未解決のincludeや不完全な型で誤認を招く。
        OutText += FString::Printf(TEXT("// %s %s;  [%s]\n"),
            *Property.CppType, *Property.Name, *Property.ReflectionPath);
    }
    OutText += TEXT("\n// Declared function signatures\n");
    for (const FBPSemanticReflectedFunction& Function : Info.DeclaredFunctionSignatures)
    {
        TArray<FString> Parameters;
        for (const FBPSemanticReflectedParameter& Parameter : Function.Parameters)
        {
            FString Qualifiers;
            if (Parameter.bIsConst)
            {
                Qualifiers += TEXT("const ");
            }
            Qualifiers += Parameter.CppType;
            if (Parameter.bIsReference)
            {
                Qualifiers += TEXT("&");
            }
            Parameters.Add(Qualifiers + TEXT(" ") + Parameter.Name);
        }
        OutText += FString::Printf(TEXT("// %s %s(%s);  [%s]\n"),
            *Function.ReturnCppType, *Function.Name,
            *FString::Join(Parameters, TEXT(", ")), *Function.ReflectionPath);
    }
    return true;
}
