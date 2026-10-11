#pragma once

#include "CoreMinimal.h"
#include "BPOutputPinBindingPlan.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UnrealType.h"

// 出力参照を受け取る単純なメンバー変数SetをC++代入文にLoweringする。
// 呼び出し側は実際の生成クラスと同じOwnerClassを渡す必要がある。
struct FBPOutputConsumerAssignmentEmitter
{
    static bool Emit(const FBPIRGraph& Graph, const FBPOutputPinBinding& Binding,
        const FGuid& ConsumerNodeId, const FGuid& ConsumerPinId,
        const UClass* OwnerClass, FString& OutStatement, FString& OutError)
    {
        OutStatement.Reset();
        OutError.Reset();
        if (OwnerClass == nullptr || ConsumerNodeId.IsValid() == false)
        {
            OutError = TEXT("Missing owner class or consumer node");
            return false;
        }
        const FBPIRNode* Consumer = nullptr;
        int32 Matches = 0;
        for (const FBPIRNode& Node : Graph.Nodes)
        {
            if (Node.Id == ConsumerNodeId)
            {
                ++Matches;
                Consumer = &Node;
            }
        }
        if (Matches != 1 || Consumer == nullptr ||
            Consumer->ClassPath != TEXT("/Script/BlueprintGraph.K2Node_VariableSet") ||
            Consumer->VariableName.IsNone())
        {
            OutError = TEXT("Unsupported output consumer variable set");
            return false;
        }
        // 任意のIR文字列を識別子として出力しない。反射済みプロパティのみ許可する。
        const FStructProperty* Property = FindFProperty<FStructProperty>(
            OwnerClass, Consumer->VariableName);
        if (Property == nullptr || Property->Struct == nullptr ||
            Property->Struct->GetPathName() != Binding.TypePath ||
            Binding.TypeKind != EBPSemanticTypeKind::Struct)
        {
            OutError = TEXT("Output consumer property type mismatch");
            return false;
        }
        // 生成されるC++からアクセスできないプロパティや静的でない通常の
        // インスタンス変数以外は、この単純代入Loweringの対象にしない。
        if (Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_EditorOnly |
            CPF_Deprecated | CPF_DisableEditOnInstance))
        {
            OutError = TEXT("Output consumer property is not writable in generated code");
            return false;
        }
        int32 InputMatches = 0;
        int32 ValueInputs = 0;
        for (const FBPIRPin& Pin : Consumer->Pins)
        {
            if (Pin.Direction == EGPD_Input &&
                Pin.Category != UEdGraphSchema_K2::PC_Exec)
            {
                // selfや別の値ピンが未接続でも存在する場合は、
                // 代入先オブジェクトなどの意味を落とさないよう拒否する。
                ++ValueInputs;
            }
            if (Pin.Id == ConsumerPinId && Pin.Name == Consumer->VariableName &&
                Pin.Direction == EGPD_Input &&
                Pin.Category == UEdGraphSchema_K2::PC_Struct &&
                Pin.SubCategoryObjectPath == Binding.TypePath)
            {
                ++InputMatches;
            }
        }
        if (InputMatches != 1 || ValueInputs != 1)
        {
            OutError = TEXT("Output consumer property input missing");
            return false;
        }
        // ほかのデータ入力やself接続がある場合は、意味を省略しないため拒否する。
        for (const FBPIRLink& Edge : Graph.DataEdges)
        {
            if (Edge.TargetNode == ConsumerNodeId && Edge.TargetPin != ConsumerPinId)
            {
                OutError = TEXT("Additional variable set input unsupported");
                return false;
            }
        }
        FString Variable;
        if (FBPOutputPinBindingPlan::ResolveConsumerVariableFromExec(
            Graph, Binding, ConsumerNodeId, ConsumerPinId, Variable, OutError) == false)
        {
            return false;
        }
        OutStatement = FString::Printf(TEXT("%s = %s;"),
            *Property->GetName(), *Variable);
        return true;
    }
};
