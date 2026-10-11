#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/HitResult.h"
#include "BPEmitterTestActor.generated.h"

// Automation Test用の反射済み代入先。実際のBlueprint生成クラスの代用として
// FHitResultメンバーを持つActor派生型を用意する。
UCLASS()
class BLUEPRINTAICONVERTER_API ABPEmitterTestActor : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY()
    FHitResult TestHitResult;
};
