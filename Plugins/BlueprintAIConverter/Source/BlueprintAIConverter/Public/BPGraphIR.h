#pragma once

#include "CoreMinimal.h"

class UBlueprint;

// Blueprintのグラフ構造をJSONを経由せず保持するための中間表現。
// この段階では制御フローを構造化せず、元の接続情報を失わないことを優先する。
struct FBPIRPin
{
    FGuid Id;
    FName Name;
    EEdGraphPinDirection Direction = EGPD_Input;
    FName Category;
    FName SubCategory;
    FString SubCategoryObjectPath;
    FString DefaultValue;
    FString DefaultObjectPath;
    FString DefaultText;
};

struct FBPIRNode
{
    FGuid Id;
    FString ClassPath;
    FString Title;
    FName FunctionName;
    FName VariableName;
    TArray<FBPIRPin> Pins;
};

struct FBPIRLink
{
    FGuid SourceNode;
    FGuid SourcePin;
    FGuid TargetNode;
    FGuid TargetPin;
};

struct FBPIRGraph
{
    FString Name;
    FString Path;
    FString Kind;
    TArray<FBPIRNode> Nodes;
    TArray<FBPIRLink> ExecEdges;
    TArray<FBPIRLink> DataEdges;
};

struct FBPGraphIR
{
    FString AssetPath;
    FString ParentClassPath;
    TArray<FBPIRGraph> Graphs;
    TArray<FString> Diagnostics;
};

class BLUEPRINTAICONVERTER_API FBPGraphIRBuilder
{
public:
    static bool Build(const UBlueprint* Blueprint, FBPGraphIR& OutIR);
};
