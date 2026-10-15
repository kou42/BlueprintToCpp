#include "BPGraphIR.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Message.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"

namespace
{
FBPIRPin BuildPin(const UEdGraphPin* Pin)
{
    FBPIRPin Result;
    Result.Id = Pin->PinId;
    Result.Name = Pin->PinName;
    Result.Direction = Pin->Direction;
    Result.Category = Pin->PinType.PinCategory;
    Result.SubCategory = Pin->PinType.PinSubCategory;
    Result.SubCategoryObjectPath = GetPathNameSafe(Pin->PinType.PinSubCategoryObject.Get());
    Result.DefaultValue = Pin->DefaultValue;
    Result.DefaultObjectPath = GetPathNameSafe(Pin->DefaultObject);
    Result.DefaultText = Pin->DefaultTextValue.ToString();
    return Result;
}

void AppendGraph(const UEdGraph* Graph, const FString& Kind, FBPGraphIR& IR)
{
    if (Graph == nullptr)
    {
        return;
    }

    FBPIRGraph Result;
    Result.Name = Graph->GetName();
    Result.Path = Graph->GetPathName();
    Result.Kind = Kind;

    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }

        FBPIRNode IRNode;
        IRNode.Id = Node->NodeGuid;
        IRNode.ClassPath = Node->GetClass()->GetPathName();
        IRNode.Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

        if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
        {
            IRNode.FunctionName = Event->EventReference.GetMemberName();
        }
        if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            IRNode.FunctionName = Call->GetFunctionName();
            // 表示名ではなく解決済みUFunctionを優先し、Copilotが呼び出し先を推測しないようにする。
            const UFunction* TargetFunction = Call->GetTargetFunction();
            if (TargetFunction != nullptr)
            {
                IRNode.bFunctionResolved = true;
                IRNode.FunctionName = TargetFunction->GetFName();
                IRNode.FunctionOwnerPath = GetPathNameSafe(TargetFunction->GetOwnerClass());
                IRNode.FunctionPath = TargetFunction->GetPathName();
                IRNode.bFunctionNative = TargetFunction->HasAnyFunctionFlags(FUNC_Native);
                IRNode.bFunctionStatic = TargetFunction->HasAnyFunctionFlags(FUNC_Static);
                IRNode.bFunctionPure = TargetFunction->HasAnyFunctionFlags(FUNC_BlueprintPure);
                IRNode.bFunctionConst = TargetFunction->HasAnyFunctionFlags(FUNC_Const);
                IRNode.bFunctionLatent = TargetFunction->HasMetaData(TEXT("Latent"));
                IRNode.FunctionDispatchKind = TEXT("regular");
                if (Call->IsA(UK2Node_Message::StaticClass()))
                {
                    IRNode.FunctionDispatchKind = TEXT("interface_message");
                }
            }
            else
            {
                IRNode.FunctionTargetKind = TEXT("unresolved");
                IRNode.FunctionDispatchKind = TEXT("unresolved");
                IR.Diagnostics.Add(FString::Printf(TEXT("%s: unresolved function reference %s"),
                    *Node->GetPathName(), *IRNode.FunctionName.ToString()));
            }
            if (TargetFunction != nullptr)
            {
                if (IRNode.bFunctionStatic)
                {
                    IRNode.FunctionTargetKind = TEXT("static");
                }
                else
                {
                    // Self Pinの接続を一次情報として保持する。接続されていないSelfは
                    // Blueprintの暗黙Selfである場合があるが、非Selfの無接続は推測しない。
                    const UEdGraphPin* SelfPin = Call->FindPin(UEdGraphSchema_K2::PN_Self);
                    if (SelfPin == nullptr)
                    {
                        IRNode.FunctionTargetKind = TEXT("unresolved");
                        IR.Diagnostics.Add(FString::Printf(
                            TEXT("%s: instance function has no Self pin"), *Node->GetPathName()));
                    }
                    else
                    {
                        IRNode.FunctionTargetPinId = SelfPin->PinId;
                        for (const UEdGraphPin* LinkedPin : SelfPin->LinkedTo)
                        {
                            if (LinkedPin != nullptr)
                            {
                                IRNode.FunctionTargetLinkedPinIds.Add(LinkedPin->PinId);
                            }
                        }
                        IRNode.FunctionTargetLinkedPinIds.Sort(
                            [](const FGuid& A, const FGuid& B)
                            {
                                return A.ToString() < B.ToString();
                            });
                        if (IRNode.FunctionTargetLinkedPinIds.IsEmpty() == false)
                        {
                            IRNode.FunctionTargetKind = TEXT("explicit_target");
                        }
                        else if (SelfPin->DefaultObject != nullptr)
                        {
                            IRNode.FunctionTargetKind = TEXT("explicit_target");
                        }
                        else if (SelfPin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
                        {
                            IRNode.FunctionTargetKind = TEXT("self");
                        }
                        else
                        {
                            IRNode.FunctionTargetKind = TEXT("unconnected_target");
                            IR.Diagnostics.Add(FString::Printf(
                                TEXT("%s: instance function target is unconnected"), *Node->GetPathName()));
                        }
                    }
                }
            }
        }
        if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
        {
            IRNode.VariableName = Variable->VariableReference.GetMemberName();
            IRNode.bVariableSelfContext = Variable->VariableReference.IsSelfContext();
        }

        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr)
            {
                continue;
            }

            IRNode.Pins.Add(BuildPin(Pin));

            // 接続は出力ピンから一度だけ収集し、重複した双方向リンクを作らない。
            if (Pin->Direction != EGPD_Output)
            {
                continue;
            }

            for (const UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked == nullptr || Linked->GetOwningNode() == nullptr)
                {
                    IR.Diagnostics.Add(FString::Printf(TEXT("%s: invalid linked pin"), *Node->GetPathName()));
                    continue;
                }
                if (Linked->Direction != EGPD_Input)
                {
                    IR.Diagnostics.Add(FString::Printf(TEXT("%s: non-input link target"), *Node->GetPathName()));
                    continue;
                }

                FBPIRLink Link;
                Link.SourceNode = Node->NodeGuid;
                Link.SourcePin = Pin->PinId;
                Link.TargetNode = Linked->GetOwningNode()->NodeGuid;
                Link.TargetPin = Linked->PinId;

                // Exec接続とデータ依存を分離し、後続の制御フロー解析で利用する。
                if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    Result.ExecEdges.Add(Link);
                }
                else
                {
                    Result.DataEdges.Add(Link);
                }
            }
        }
        Result.Nodes.Add(MoveTemp(IRNode));
    }
    IR.Graphs.Add(MoveTemp(Result));
}
}

bool FBPGraphIRBuilder::Build(const UBlueprint* Blueprint, FBPGraphIR& OutIR)
{
    OutIR = FBPGraphIR();
    if (Blueprint == nullptr)
    {
        OutIR.Diagnostics.Add(TEXT("Blueprint is null"));
        return false;
    }

    OutIR.AssetPath = Blueprint->GetPathName();
    OutIR.ParentClassPath = GetPathNameSafe(Blueprint->ParentClass);
    OutIR.BlueprintVariableCount = Blueprint->NewVariables.Num();

    for (const UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        AppendGraph(Graph, TEXT("event"), OutIR);
    }
    for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        AppendGraph(Graph, TEXT("function"), OutIR);
    }
    for (const UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        AppendGraph(Graph, TEXT("macro"), OutIR);
    }

    return true;
}
