#include "BPNativeSubclassGenerator.h"
#include "BPGeneratedFunctionShape.h"

#include "BPParentClassResolver.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
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

namespace
{
// 反射だけではC++のvirtual宣言が分からないため、既知のAActor署名のみを検証済み入力とする。
const FBPVerifiedOverrideFunction* FindActorOverride(const FString& Name)
{
    static const TArray<FBPVerifiedOverrideFunction> Known = {
        {TEXT("BeginPlay"), TEXT("virtual void BeginPlay() override;"), TEXT("void %s::BeginPlay()"), TEXT("Super::BeginPlay();"), TEXT("protected")},
        {TEXT("Tick"), TEXT("virtual void Tick(float DeltaTime) override;"), TEXT("void %s::Tick(float DeltaTime)"), TEXT("Super::Tick(DeltaTime);"), TEXT("public")},
        {TEXT("EndPlay"), TEXT("virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;"), TEXT("void %s::EndPlay(const EEndPlayReason::Type EndPlayReason)"), TEXT("Super::EndPlay(EndPlayReason);"), TEXT("protected")}
    };
    return Known.FindByPredicate([&Name](const FBPVerifiedOverrideFunction& Item)
    {
        return Item.Name == Name;
    });
}
}

bool FBPNativeSubclassGenerator::ParseVerifiedActorOverrides(const FString& Selection,
    TArray<FBPVerifiedOverrideFunction>& OutFunctions, TArray<FString>& OutDiagnostics)
{
    OutFunctions.Reset();
    OutDiagnostics.Reset();
    if (Selection.IsEmpty())
    {
        return true;
    }
    // ParseIntoArrayの空要素の扱いに依存せず、先頭・末尾・連続カンマを明示的に拒否する。
    if (Selection.StartsWith(TEXT(",")) || Selection.EndsWith(TEXT(",")) ||
        Selection.Contains(TEXT(",,")))
    {
        OutDiagnostics.Add(TEXT("Empty function selection item"));
        return false;
    }
    TArray<FString> Names;
    Selection.ParseIntoArray(Names, TEXT(","), false);
    TSet<FString> Seen;
    for (const FString& RawName : Names)
    {
        const FString Name = RawName.TrimStartAndEnd();
        const FBPVerifiedOverrideFunction* Function = FindActorOverride(Name);
        if (Function == nullptr || Seen.Contains(Name))
        {
            OutDiagnostics.Add(FString::Printf(TEXT("Unknown or duplicate verified override: %s"), *Name));
            OutFunctions.Reset();
            return false;
        }
        Seen.Add(Name);
        OutFunctions.Add(*Function);
    }
    if (Names.IsEmpty())
    {
        OutDiagnostics.Add(TEXT("Empty function selection"));
        return false;
    }
    return true;
}

bool FBPNativeSubclassGenerator::GenerateWithVerifiedOverrides(
    const FBPSemanticClassInfo& Info, const FString& NewClassName,
    const FString& ModuleApiMacro, const TArray<FBPVerifiedOverrideFunction>& Functions,
    FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics)
{
    if (Generate(Info, NewClassName, ModuleApiMacro, OutFiles, OutDiagnostics) == false)
    {
        return false;
    }
    if (Functions.IsEmpty())
    {
        return true;
    }
    UClass* ParentClass = FindObject<UClass>(nullptr, *Info.ClassPath);
    if (ParentClass == nullptr)
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("Native parent class is unavailable"));
        return false;
    }
    const bool bActor = ParentClass->IsChildOf(AActor::StaticClass());
    const bool bActorComponent = ParentClass->IsChildOf(UActorComponent::StaticClass());
    // ActorComponentのBeginPlayはActorと同じ検証済み署名を持つ。
    // Tick/EndPlayはComponent側で署名が異なるため、Actor用定義を流用しない。
    if (bActor == false && bActorComponent == false)
    {
        OutFiles = FBPGeneratedActorFiles();
        OutDiagnostics.Add(TEXT("Verified lifecycle overrides require an Actor or ActorComponent parent"));
        return false;
    }
    FString Declarations = TEXT("    GENERATED_BODY()\n");
    FString CurrentAccess;
    TSet<FString> Seen;
    for (const FBPVerifiedOverrideFunction& Function : Functions)
    {
        // 外部入力のC++断片をそのまま挿入せず、検証済み定義と完全一致するものだけを許可する。
        const FBPVerifiedOverrideFunction* Verified = FindActorOverride(Function.Name);
        if (Verified == nullptr || Seen.Contains(Function.Name) ||
            Verified->Declaration != Function.Declaration ||
            Verified->DefinitionSignature != Function.DefinitionSignature ||
            Verified->SuperCall != Function.SuperCall ||
            Verified->AccessSpecifier != Function.AccessSpecifier)
        {
            OutFiles = FBPGeneratedActorFiles();
            OutDiagnostics.Add(FString::Printf(TEXT("Unverified or duplicate override: %s"), *Function.Name));
            return false;
        }
        if (bActorComponent && Function.Name != TEXT("BeginPlay"))
        {
            OutFiles = FBPGeneratedActorFiles();
            OutDiagnostics.Add(FString::Printf(
                TEXT("ActorComponent override %s has a different native signature; only BeginPlay is verified"),
                *Function.Name));
            return false;
        }
        Seen.Add(Function.Name);
        if (CurrentAccess != Function.AccessSpecifier)
        {
            CurrentAccess = Function.AccessSpecifier;
            Declarations += FString::Printf(TEXT("\n%s:\n"), *CurrentAccess);
        }
        // Semantic関数と同じFunction Shapeへ正規化する。NativeのPrologueは検証済みSuper呼び出しのみ。
        FBPGeneratedFunctionShape Shape;
        Shape.Declaration = FString::Printf(TEXT("    %s\n"), *Function.Declaration);
        Shape.Definition = Function.DefinitionSignature.Replace(TEXT("%s"), *NewClassName);
        Shape.Prologue = FString::Printf(TEXT("    %s\n"), *Function.SuperCall);
        Declarations += Shape.Declaration;
        OutFiles.SourceContent += FBPFunctionDefinitionEmitter::RenderDefinition(Shape, FString());
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

bool FBPNativeSubclassGenerator::GenerateWithBeginPlayOverride(
    const FBPSemanticClassInfo& Info, const FString& NewClassName,
    const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
    TArray<FString>& OutDiagnostics)
{
    return GenerateWithVerifiedOverrides(Info, NewClassName, ModuleApiMacro,
        {*FindActorOverride(TEXT("BeginPlay"))}, OutFiles, OutDiagnostics);
}

bool FBPNativeSubclassGenerator::GenerateWithTickOverride(
    const FBPSemanticClassInfo& Info, const FString& NewClassName,
    const FString& ModuleApiMacro, FBPGeneratedActorFiles& OutFiles,
    TArray<FString>& OutDiagnostics)
{
    return GenerateWithVerifiedOverrides(Info, NewClassName, ModuleApiMacro,
        {*FindActorOverride(TEXT("Tick"))}, OutFiles, OutDiagnostics);
}

bool FBPNativeSubclassGenerator::GenerateWithLifecycleOverrides(
    const FBPSemanticClassInfo& Info, const FString& NewClassName,
    const FString& ModuleApiMacro, bool bBeginPlay, bool bTick,
    FBPGeneratedActorFiles& OutFiles, TArray<FString>& OutDiagnostics)
{
    TArray<FBPVerifiedOverrideFunction> Functions;
    if (bBeginPlay)
    {
        Functions.Add(*FindActorOverride(TEXT("BeginPlay")));
    }
    if (bTick)
    {
        Functions.Add(*FindActorOverride(TEXT("Tick")));
    }
    return GenerateWithVerifiedOverrides(Info, NewClassName, ModuleApiMacro,
        Functions, OutFiles, OutDiagnostics);
}
