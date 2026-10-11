#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class FJsonObject;

class BLUEPRINTAICONVERTER_API FBPSnapshotExporter
{
public:
    static TSharedRef<FJsonObject> Extract(const UBlueprint* Blueprint);
    static bool Serialize(const UBlueprint* Blueprint, FString& OutJson);
    static bool Export(const UBlueprint* Blueprint, FString& OutPath, FString& OutError);
};
