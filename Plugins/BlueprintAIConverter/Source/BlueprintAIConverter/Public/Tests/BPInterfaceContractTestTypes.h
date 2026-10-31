#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "BPInterfaceContractTestTypes.generated.h"

// P2の新規Interface継承・署名・戻り値の回帰fixture。
UINTERFACE(BlueprintType, Blueprintable)
class BLUEPRINTAICONVERTER_API UBPNotifyContract : public UInterface
{
    GENERATED_BODY()
};

class BLUEPRINTAICONVERTER_API IBPNotifyContract
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    void NotifyValue(int32 Value);
};

UINTERFACE(BlueprintType, Blueprintable)
class BLUEPRINTAICONVERTER_API UBPValueContract : public UInterface
{
    GENERATED_BODY()
};

class BLUEPRINTAICONVERTER_API IBPValueContract
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    int32 ReadValue(int32 Value) const;
};

UCLASS()
class BLUEPRINTAICONVERTER_API UBPInheritedNotifyObject : public UObject, public IBPNotifyContract
{
    GENERATED_BODY()
public:
    virtual void NotifyValue_Implementation(int32 Value) override {}
};

UINTERFACE(BlueprintType, Blueprintable)
class BLUEPRINTAICONVERTER_API UBPArrayContract : public UInterface
{
    GENERATED_BODY()
};

class BLUEPRINTAICONVERTER_API IBPArrayContract
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    int32 CountValues(const TArray<int32>& Values) const;
};
