#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

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
    // 解決できたUFunctionの所有クラスと完全パス。未解決時は空文字を保持する。
    FString FunctionOwnerPath;
    FString FunctionPath;
    bool bFunctionResolved = false;
    bool bFunctionNative = false;
    bool bFunctionStatic = false;
    bool bFunctionPure = false;
    bool bFunctionConst = false;
    // CallFunctionのSelf/Targetは関数所有クラスとは別情報。
    // self / explicit_target / unconnected_target / static / unresolved / not_applicable
    FString FunctionTargetKind = TEXT("not_applicable");
    FGuid FunctionTargetPinId;
    TArray<FGuid> FunctionTargetLinkedPinIds;
    FName VariableName;
    bool bVariableSelfContext = false;
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
    // 未対応のBlueprintメンバー変数を生成時に黙って失わないために保持する。
    int32 BlueprintVariableCount = 0;
    TArray<FBPIRGraph> Graphs;
    TArray<FString> Diagnostics;
};

class BLUEPRINTAICONVERTER_API FBPGraphIRBuilder
{
public:
    static bool Build(const UBlueprint* Blueprint, FBPGraphIR& OutIR);
};
