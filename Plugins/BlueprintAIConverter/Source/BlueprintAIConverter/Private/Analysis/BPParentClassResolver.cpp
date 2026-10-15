#include "BPParentClassResolver.h"

#include "GameFramework/Actor.h"
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

bool FBPParentClassResolver::ResolveActorParent(const FString& ParentClassPath,
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
    if (ParentClass->IsChildOf(AActor::StaticClass()) == false)
    {
        OutDiagnostics.Add(FString::Printf(TEXT("Parent class is not derived from AActor: %s"), *ParentClassPath));
        return false;
    }
    if (ParentClass->HasAnyClassFlags(CLASS_Native) == false)
    {
        OutDiagnostics.Add(FString::Printf(
            TEXT("Blueprint-generated parent classes require dependency conversion and are not yet supported: %s"),
            *ParentClassPath));
        return false;
    }

    const FString CppClassName = ParentClass->GetPrefixCPP() + ParentClass->GetName();
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
    return true;
}
