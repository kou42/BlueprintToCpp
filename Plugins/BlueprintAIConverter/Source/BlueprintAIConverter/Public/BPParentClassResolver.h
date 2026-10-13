#pragma once

#include "CoreMinimal.h"

// Blueprintの親クラスパスを、生成C++で利用できる名前と公開ヘッダーへ解決する。
// ネイティブ祖先への暗黙フォールバックは継承したBlueprintの意味を失うため行わない。
struct FBPResolvedParentClass
{
    FString ClassPath;
    FString CppClassName;
    FString HeaderPath;
};

class BLUEPRINTAICONVERTER_API FBPParentClassResolver
{
public:
    static bool ResolveActorParent(const FString& ParentClassPath,
        FBPResolvedParentClass& OutParent, TArray<FString>& OutDiagnostics);
};
