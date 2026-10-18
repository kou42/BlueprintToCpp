#include "BPNativeSubclassGenerator.h"

#include "BPParentClassResolver.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

namespace
{
bool IsIdentifier(const FString& Name)
{
    if (Name.IsEmpty())
    {
        return false;
    }
    for (int32 Index = 0; Index < Name.Len(); ++Index)
    {
        const TCHAR Ch = Name[Index];
        if ((Index == 0 && FChar::IsAlpha(Ch) == false && Ch != TEXT('_')) ||
            (FChar::IsAlnum(Ch) == false && Ch != TEXT('_')))
        {
            return false;
        }
    }
    return true;
}
}

bool FBPNativeSubclassGenerator::Generate(const FBPSemanticClassInfo& Info,
    const FString& NewClassName, const FString& ModuleApiMacro,
    FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics)
{
    OutFiles = FBPGeneratedActorFiles();
    OutDiagnostics.Reset();
    if (Info.Origin != EBPSemanticClassOrigin::Native ||
        Info.ClassPath.StartsWith(TEXT("/Script/")) == false)
    {
        OutDiagnostics.Add(TEXT("Only reflected native UCLASS sources are supported"));
        return false;
    }
    if (ModuleApiMacro.IsEmpty() == false &&
        (IsIdentifier(ModuleApiMacro) == false || ModuleApiMacro.EndsWith(TEXT("_API")) == false))
    {
        OutDiagnostics.Add(TEXT("Invalid module API macro"));
        return false;
    }

    // 元クラスの直接の派生型として生成する。宣言メンバーをコピーすると、
    // 同名メンバーの隠蔽・重複定義や未検証のUFUNCTION署名が発生するため行わない。
    FBPResolvedParentClass Parent;
    if (FBPParentClassResolver::ResolveParent(Info.ClassPath, Parent, OutDiagnostics) == false)
    {
        return false;
    }
    if (IsIdentifier(NewClassName) == false ||
        NewClassName.StartsWith(Parent.GeneratedClassPrefix) == false ||
        NewClassName.Len() <= Parent.GeneratedClassPrefix.Len() ||
        NewClassName == Parent.CppClassName)
    {
        OutDiagnostics.Add(TEXT("New class name must be distinct and use the parent C++ prefix"));
        return false;
    }

    // finalクラスからは派生できないため、通常経路でも拒否する。
    UClass* NativeParent = FindObject<UClass>(nullptr, *Info.ClassPath);
    if (NativeParent == nullptr || NativeParent->HasAnyClassFlags(CLASS_Final))
    {
        OutDiagnostics.Add(TEXT("Native parent is unavailable or final"));
        return false;
    }

    // 既にロードされた同名クラスへの衝突を避ける。
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (It->GetPrefixCPP() + It->GetName() == NewClassName)
        {
            OutDiagnostics.Add(TEXT("A loaded UCLASS already uses the requested class name"));
            return false;
        }
    }

    const FString BaseName = NewClassName.Mid(Parent.GeneratedClassPrefix.Len());
    const FString ApiPrefix = ModuleApiMacro.IsEmpty() ? FString() : ModuleApiMacro + TEXT(" ");
    OutFiles.HeaderFileName = BaseName + TEXT(".h");
    OutFiles.SourceFileName = BaseName + TEXT(".cpp");
    OutFiles.HeaderContent = FString::Printf(
        TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"%s\"\n#include \"%s.generated.h\"\n\n")
        TEXT("// The original native class implementation is inherited, not reconstructed.\n")
        TEXT("UCLASS()\nclass %s%s : public %s\n{\n    GENERATED_BODY()\n};\n"),
        *Parent.HeaderPath, *BaseName, *ApiPrefix, *NewClassName, *Parent.CppClassName);
    OutFiles.SourceContent = FString::Printf(TEXT("#include \"%s.h\"\n"), *BaseName);
    return true;
}

bool FBPNativeSubclassGenerator::GenerateWithBeginPlayOverride(
    const FBPSemanticClassInfo& Info, const FString& NewClassName,
    const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
    TArray<FString>& OutDiagnostics)
{
    if (Generate(Info, NewClassName, ModuleApiMacro, OutFiles, OutDiagnostics) == false)
    {
        return false;
    }

    // AActor::BeginPlayは既知のvirtual void BeginPlay()であり、
    // UFunctionのフラグからvirtualを推測する汎用経路とは区別する。
    UClass* ParentClass = FindObject<UClass>(nullptr, *Info.ClassPath);
    if (ParentClass == nullptr || ParentClass->IsChildOf(AActor::StaticClass()) == false)
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("BeginPlay override requires a loaded AActor-derived native class"));
        return false;
    }

    // 直接の親クラスがfinalのときは派生クラスそのものを作成できない。
    if (ParentClass->HasAnyClassFlags(CLASS_Final))
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("Cannot subclass a final native class"));
        return false;
    }

    const FString Declaration = TEXT("    virtual void BeginPlay() override;\n");
    if (OutFiles.HeaderContent.ReplaceInline(TEXT("    GENERATED_BODY()\n};"),
        *(FString(TEXT("    GENERATED_BODY()\n\nprotected:\n")) + Declaration + TEXT("};"))) == 0)
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("Generated header structure was not recognized"));
        return false;
    }
    OutFiles.SourceContent += FString::Printf(
        TEXT("\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n}\n"), *NewClassName);
    return true;
}

bool FBPNativeSubclassGenerator::GenerateWithTickOverride(
    const FBPSemanticClassInfo& Info, const FString& NewClassName,
    const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
    TArray<FString>& OutDiagnostics)
{
    if (Generate(Info, NewClassName, ModuleApiMacro, OutFiles, OutDiagnostics) == false)
    {
        return false;
    }

    // AActor::Tick(float)の署名は既知だが、実行可否はbCanEverTickなどの設定に依存する。
    // 設定値を推測で変更せず、overrideとSuper呼び出しだけを生成する。
    UClass* ParentClass = FindObject<UClass>(nullptr, *Info.ClassPath);
    if (ParentClass == nullptr || ParentClass->IsChildOf(AActor::StaticClass()) == false)
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("Tick override requires a loaded AActor-derived native class"));
        return false;
    }
    const FString Declaration = TEXT("    virtual void Tick(float DeltaTime) override;\n");
    if (OutFiles.HeaderContent.ReplaceInline(TEXT("    GENERATED_BODY()\n};"),
        *(FString(TEXT("    GENERATED_BODY()\n\npublic:\n")) + Declaration + TEXT("};"))) == 0)
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("Generated header structure was not recognized"));
        return false;
    }
    OutFiles.SourceContent += FString::Printf(
        TEXT("\nvoid %s::Tick(float DeltaTime)\n{\n    Super::Tick(DeltaTime);\n}\n"), *NewClassName);
    return true;
}

bool FBPNativeSubclassGenerator::GenerateWithLifecycleOverrides(
    const FBPSemanticClassInfo& Info, const FString& NewClassName,
    const FString& ModuleApiMacro, bool bBeginPlay, bool bTick,
    FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics)
{
    if (Generate(Info, NewClassName, ModuleApiMacro, OutFiles, OutDiagnostics) == false)
    {
        return false;
    }
    if (bBeginPlay == false && bTick == false)
    {
        return true;
    }

    // 複数のoverrideを個別生成後に結合するとクラス定義が重複する。
    // 一つのUCLASSへアクセス指定子ごとに宣言をまとめて挿入する。
    UClass* ParentClass = FindObject<UClass>(nullptr, *Info.ClassPath);
    if (ParentClass == nullptr || ParentClass->IsChildOf(AActor::StaticClass()) == false)
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("Lifecycle overrides require an AActor-derived native class"));
        return false;
    }

    FString Declarations = TEXT("    GENERATED_BODY()\n");
    if (bBeginPlay)
    {
        Declarations += TEXT("\nprotected:\n    virtual void BeginPlay() override;\n");
        OutFiles.SourceContent += FString::Printf(
            TEXT("\nvoid %s::BeginPlay()\n{\n    Super::BeginPlay();\n}\n"), *NewClassName);
    }
    if (bTick)
    {
        Declarations += TEXT("\npublic:\n    virtual void Tick(float DeltaTime) override;\n");
        OutFiles.SourceContent += FString::Printf(
            TEXT("\nvoid %s::Tick(float DeltaTime)\n{\n    Super::Tick(DeltaTime);\n}\n"), *NewClassName);
    }
    if (OutFiles.HeaderContent.ReplaceInline(TEXT("    GENERATED_BODY()\n};"),
        *(Declarations + TEXT("};"))) == 0)
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("Generated header structure was not recognized"));
        return false;
    }
    return true;
}
