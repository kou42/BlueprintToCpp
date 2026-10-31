#include "BPParentClassResolver.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

namespace
{
bool IsCppIdentifier(const FString& Value)
{
    if (Value.IsEmpty())
    {
        return false;
    }
    for (int32 Index = 0; Index < Value.Len(); ++Index)
    {
        const TCHAR Character = Value[Index];
        const bool bAlpha = FChar::IsAlpha(Character) != 0;
        const bool bDigit = FChar::IsDigit(Character) != 0;
        if ((Index == 0 && bAlpha == false && Character != TEXT('_')) ||
            (Index > 0 && bAlpha == false && bDigit == false && Character != TEXT('_')))
        {
            return false;
        }
    }
    return true;
}

FString NormalizePublicHeaderPath(const UClass* ParentClass)
{
    FString HeaderPath = ParentClass->GetMetaData(TEXT("IncludePath"));
    if (HeaderPath.IsEmpty())
    {
        HeaderPath = ParentClass->GetMetaData(TEXT("ModuleRelativePath"));
    }
    HeaderPath.ReplaceInline(TEXT("\\"), TEXT("/"));
    if (HeaderPath.StartsWith(TEXT("Public/")))
    {
        HeaderPath.RightChopInline(7);
    }
    else if (HeaderPath.StartsWith(TEXT("Classes/")))
    {
        HeaderPath.RightChopInline(8);
    }
    return HeaderPath;
}
}

bool FBPParentClassResolver::ResolveParent(const FString& ParentClassPath,
    FBPResolvedParentClass& OutParent, TArray<FString>& OutDiagnostics)
{
    OutParent = FBPResolvedParentClass();
    if (ParentClassPath.IsEmpty())
    {
        OutDiagnostics.Add(TEXT("Parent class path is empty"));
        return false;
    }

    UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
    if (ParentClass == nullptr)
    {
        ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
    }
    if (ParentClass == nullptr)
    {
        OutDiagnostics.Add(FString::Printf(TEXT("Parent class could not be resolved: %s"), *ParentClassPath));
        return false;
    }
    if (ParentClass->IsChildOf(UObject::StaticClass()) == false)
    {
        OutDiagnostics.Add(FString::Printf(TEXT("Parent class is not derived from UObject: %s"), *ParentClassPath));
        return false;
    }
    if (ParentClass->HasAnyClassFlags(CLASS_Native) == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Blueprint-generated parent classes require dependency conversion and are not yet supported: %s"),
            *ParentClassPath));
        return false;
    }

    const FString GeneratedClassPrefix = ParentClass->GetPrefixCPP();
    const FString CppClassName = GeneratedClassPrefix + ParentClass->GetName();
    if (GeneratedClassPrefix.IsEmpty() || IsCppIdentifier(GeneratedClassPrefix) == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Parent class does not expose a usable generated-class prefix: %s"), *ParentClassPath));
        return false;
    }
    if (IsCppIdentifier(CppClassName) == false)
    {
        OutDiagnostics.Add(FString::Printf(TEXT("Parent class has an unsafe C++ name: %s"), *CppClassName));
        return false;
    }
    FString HeaderPath = NormalizePublicHeaderPath(ParentClass);
    if (HeaderPath.IsEmpty() || HeaderPath.StartsWith(TEXT("Private/")) ||
        HeaderPath.Contains(TEXT("..")) || HeaderPath.Contains(TEXT(":")))
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Parent class does not expose a usable public header: %s"), *ParentClassPath));
        return false;
    }

    OutParent.ClassPath = ParentClass->GetPathName();
    OutParent.CppClassName = CppClassName;
    OutParent.HeaderPath = MoveTemp(HeaderPath);
    OutParent.GeneratedClassPrefix = GeneratedClassPrefix;
    OutParent.bIsActor = ParentClass->IsChildOf(AActor::StaticClass());
    OutParent.bSupportsBeginPlay = OutParent.bIsActor ||
        ParentClass->IsChildOf(UActorComponent::StaticClass());
    return true;
}

bool FBPParentClassResolver::ResolveSemanticParent(const FString& ParentClassPath,
    FBPResolvedParentClass& OutParent, TArray<FString>& OutDiagnostics,
    const bool bHasAuditedWidgetTree)
{
    if (ResolveParent(ParentClassPath, OutParent, OutDiagnostics) == false)
    {
        return false;
    }
    // Inspect the actual ancestry, including native subclasses. Path checks avoid
    // introducing UMG/AnimGraph editor dependencies solely to reject these assets.
    const UClass* Parent = FindObject<UClass>(nullptr, *OutParent.ClassPath);
    for (const UClass* Ancestor = Parent; Ancestor != nullptr;
        Ancestor = Ancestor->GetSuperClass())
    {
        const FString Path = Ancestor->GetPathName();
        FString Requirement;
        if (Path == TEXT("/Script/UMG.UserWidget"))
        {
            if (bHasAuditedWidgetTree && Parent == Ancestor)
            {
                return true;
            }
            Requirement = TEXT("Widget Blueprint requires widget-tree, binding and animation conversion");
        }
        else if (Path == TEXT("/Script/Engine.AnimInstance"))
        {
            Requirement = TEXT("Animation Blueprint requires anim graph, state-machine and generated-class conversion");
        }
        if (Requirement.IsEmpty() == false)
        {
            OutDiagnostics.Add(FString::Printf(TEXT("%s; semantic conversion is not yet supported for %s"),
                *Requirement, *ParentClassPath));
            OutParent = FBPResolvedParentClass();
            return false;
        }
    }
    return true;
}

bool FBPParentClassResolver::ResolveActorParent(const FString& ParentClassPath,
    FBPResolvedParentClass& OutParent, TArray<FString>& OutDiagnostics)
{
    if (ResolveParent(ParentClassPath, OutParent, OutDiagnostics) == false)
    {
        return false;
    }
    if (OutParent.bIsActor == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Parent class is not derived from AActor: %s"), *ParentClassPath));
        OutParent = FBPResolvedParentClass();
        return false;
    }
    return true;
}
