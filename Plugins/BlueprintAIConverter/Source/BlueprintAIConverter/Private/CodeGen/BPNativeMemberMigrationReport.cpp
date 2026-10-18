#include "BPNativeMemberMigrationReport.h"

bool FBPNativeMemberMigrationReport::Build(
    const FBPSemanticClassInfo& Info, FString& OutReport, FString& OutError)
{
    OutReport.Reset();
    OutError.Reset();
    if (Info.Origin != EBPSemanticClassOrigin::Native ||
        Info.ClassPath.StartsWith(TEXT("/Script/")) == false)
    {
        OutError = TEXT("A native UCLASS is required");
        return false;
    }

    // 元クラスの派生クラスには宣言済みのプロパティと関数が継承される。
    // 同名のUPROPERTY/UFUNCTIONを再宣言すると衝突するため、自動移植は許可しない。
    OutReport = FString::Printf(TEXT("Native member migration assessment: %s\n"), *Info.ClassPath);
    OutReport += TEXT("Mode: inherited members only; no declarations will be copied.\n");
    for (const FBPSemanticClassMember& Property : Info.DeclaredProperties)
    {
        OutReport += FString::Printf(
            TEXT("[INHERITED / DO NOT REDECLARE] property %s : %s\n"),
            *Property.Name, *Property.CppType);
    }
    for (const FBPSemanticReflectedFunction& Function : Info.DeclaredFunctionSignatures)
    {
        // リフレクションは元の実装本体、アクセス指定子、全てのC++修飾子を保持しない。
        // virtual/override、BlueprintNativeEvent、RPC等の正確な再宣言は追加検証が必要。
        OutReport += FString::Printf(
            TEXT("[INHERITED / BODY UNAVAILABLE] function %s -> %s\n"),
            *Function.Name, *Function.ReturnCppType);
    }
    OutReport += TEXT("To customize behavior, implement an explicit override after checking the original C++ header.\n");
    OutReport += TEXT("Native source bodies and default member initializers cannot be reconstructed from reflection.\n");
    return true;
}
