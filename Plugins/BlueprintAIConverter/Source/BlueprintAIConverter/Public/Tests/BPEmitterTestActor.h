#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/HitResult.h"
#include "UObject/Interface.h"
#include "BPEmitterTestActor.generated.h"

UINTERFACE()
class BLUEPRINTAICONVERTER_API UBPFunctionSignatureTestInterface : public UInterface
{
    GENERATED_BODY()
};

class BLUEPRINTAICONVERTER_API IBPFunctionSignatureTestInterface
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintImplementableEvent)
    int32 ResolveValue(const FString& Input, int32& Output) const;
};

// Automation Test用の反射済み代入先。実際のBlueprint生成クラスの代用として
// FHitResultメンバーを持つActor派生型を用意する。
UCLASS()
class BLUEPRINTAICONVERTER_API ABPEmitterTestActor : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY()
    FHitResult TestHitResult;

    // 共通Call Emitterの公開ネイティブbool参照テスト用。
    UPROPERTY()
    bool bTestEnabled = true;

    UFUNCTION(BlueprintImplementableEvent)
    int32 MatchingSignature(const FString& Input, int32& Output) const;

    UFUNCTION(BlueprintImplementableEvent)
    int32 MismatchedType(int32 Input, int32& Output) const;

    UFUNCTION(BlueprintImplementableEvent)
    int32 MismatchedDirection(const FString& Input, int32 Output) const;

    UFUNCTION(BlueprintImplementableEvent)
    int32 MismatchedConst(const FString& Input, int32& Output);
};
