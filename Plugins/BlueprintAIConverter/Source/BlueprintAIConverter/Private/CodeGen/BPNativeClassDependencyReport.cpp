#include "BPNativeClassDependencyReport.h"

namespace
{
bool IsModuleName(const FString& Name)
{
    if (Name.IsEmpty())
    {
        return false;
    }
    for (int32 Index = 0; Index < Name.Len(); ++Index)
    {
        const TCHAR Character = Name[Index];
        if ((Index == 0 && FChar::IsAlpha(Character) == false && Character != TEXT('_')) ||
            (FChar::IsAlnum(Character) == false && Character != TEXT('_')))
        {
            return false;
        }
    }
    return true;
}
}

bool FBPNativeClassDependencyReport::Build(const FBPSemanticClassInfo& Info,
    const FString& TargetModule, FString& OutReport, FString& OutError)
{
    OutReport.Reset();
    OutError.Reset();
    if (Info.Origin != EBPSemanticClassOrigin::Native ||
        Info.ClassPath.StartsWith(TEXT("/Script/")) == false ||
        IsModuleName(TargetModule) == false)
    {
        OutError = TEXT("Native UCLASS path and a valid target module are required");
        return false;
    }

    // /Script/Module.Class の Module 部分だけを抽出する。
    // 親クラスを直接継承するため、その定義元モジュールへの依存が必要になる。
    const FString PackageAndClass = Info.ClassPath.RightChop(8);
    FString SourceModule;
    FString SourceClass;
    if (PackageAndClass.Split(TEXT("."), &SourceModule, &SourceClass) == false ||
        IsModuleName(SourceModule) == false || SourceClass.IsEmpty())
    {
        OutError = TEXT("Invalid /Script/Module.Class path");
        return false;
    }

    OutReport = FString::Printf(
        TEXT("Source UCLASS: %s\nSource module: %s\nTarget module: %s\n"),
        *Info.ClassPath, *SourceModule, *TargetModule);
    if (SourceModule == TargetModule)
    {
        OutReport += TEXT("No cross-module dependency is required for the parent class.\n");
    }
    else
    {
        OutReport += FString::Printf(
            TEXT("Verify %s.Build.cs contains a dependency on \"%s\".\n"),
            *TargetModule, *SourceModule);
        OutReport += TEXT("Use PublicDependencyModuleNames when a public generated header exposes this parent type.\n");
    }
    // IncludePath/ModuleRelativePathは別途Resolverで検証する。
    // クラスが参照する型の所属モジュールやプラグイン有効化状態は自動推定しない。
    OutReport += TEXT("Check plugin enablement, public header visibility and transitive type dependencies manually.\n");
    return true;
}
