#pragma once

#include "CoreMinimal.h"

// Blueprintの親クラスパスを、生成C++で利用できる名前と公開ヘッダーへ解決する。
// ネイティブ祖先への暗黙フォールバックは継承したBlueprintの意味を失うため行わない。
struct FBPResolvedParentClass
{
    FString ClassPath;
    FString CppClassName;
    FString HeaderPath;
    FString GeneratedClassPrefix;
    bool bIsActor = false;
    bool bSupportsBeginPlay = false;
};

class BLUEPRINTAICONVERTER_API FBPParentClassResolver
{
public:
    // 任意のネイティブUObject派生クラスを解決する。個別APIの利用可否は
    // bIsActor / bSupportsBeginPlayなどの能力フラグで呼び出し側が判定する。
    static bool ResolveParent(const FString& ParentClassPath,
        FBPResolvedParentClass& OutParent, TArray<FString>& OutDiagnostics);

    // Actor固有APIを扱う既存経路向けの厳格なラッパー。
    static bool ResolveActorParent(const FString& ParentClassPath,
        FBPResolvedParentClass& OutParent, TArray<FString>& OutDiagnostics);
};
