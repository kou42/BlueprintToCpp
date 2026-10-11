"""Fail-closed code generation for one literal Branch and PrintString leaf arms."""
import re

from .schemas import Snapshot
from .ir_builder import build_ir
from .code_generator import _print_string, _is_class, GenerationError
from .node_registry import ConversionContext, UnsupportedNodeError
from .typed_ir import lower_single_branch


def generate_branch_cpp(snapshot: Snapshot, class_name: str) -> dict:
    if not re.fullmatch(r"A[A-Za-z_][A-Za-z_0-9]*", class_name):
        raise GenerationError("invalid A-prefixed class name")
    if snapshot.parent_class != "/Script/Engine.Actor" or snapshot.variables:
        raise UnsupportedNodeError("Branch MVP requires direct Actor parent and no variables")
    ir = build_ir(snapshot)
    if len(ir["graphs"]) != 1 or ir["graphs"][0]["kind"] != "event":
        raise UnsupportedNodeError("Branch MVP requires one EventGraph")
    graph = ir["graphs"][0]
    nodes = {node["id"]: node for node in graph["nodes"]}
    events = [n for n in nodes.values() if _is_class(n, "K2Node_Event")]
    if len(events) != 1 or events[0].get("function_name") != "ReceiveBeginPlay":
        raise UnsupportedNodeError("Branch MVP requires ReceiveBeginPlay")
    event = events[0]
    branch_ir = lower_single_branch(graph)
    incoming = [edge for edge in graph["exec_edges"]
                if edge["target_node"] == branch_ir.source_node]
    if len(incoming) != 1 or incoming[0]["source_node"] != event["id"]:
        raise UnsupportedNodeError("Branch must follow BeginPlay directly")
    if any(edge["target_node"] == event["id"] for edge in graph["exec_edges"]):
        raise UnsupportedNodeError("Event cannot have incoming exec edges")
    # No other edges may exist: prevents silent omission of disconnected flow.
    permitted = {(edge["source_node"], edge["target_node"])
                 for edge in incoming}
    for arm in (branch_ir.true_nodes, branch_ir.false_nodes):
        for nid in arm:
            permitted.add((branch_ir.source_node, nid))
    if any((e["source_node"], e["target_node"]) not in permitted
           for e in graph["exec_edges"]):
        raise UnsupportedNodeError("unexpected execution edge")
    expected = {event["id"], branch_ir.source_node,
                *branch_ir.true_nodes, *branch_ir.false_nodes}
    if set(nodes) != expected:
        raise UnsupportedNodeError("disconnected or unsupported nodes")
    if graph["data_edges"]:
        raise UnsupportedNodeError("data links not supported in Branch MVP")
    def statements(ids: tuple[str, ...]) -> str:
        return "\n".join(_print_string(ConversionContext(nodes[nid], graph)) for nid in ids)
    base = class_name[1:]
    header = (f'#pragma once\n#include "CoreMinimal.h"\n'
              f'#include "GameFramework/Actor.h"\n'
              f'#include "{base}.generated.h"\n\n'
              f'UCLASS()\nclass {class_name} : public AActor\n{{\n'
              f'    GENERATED_BODY()\npublic:\n'
              f'    virtual void BeginPlay() override;\n}};\n')
    body = (f'#include "{base}.h"\n'
            f'#include "Kismet/KismetSystemLibrary.h"\n\n'
            f'void {class_name}::BeginPlay()\n{{\n'
            f'    Super::BeginPlay();\n'
            f'    if ({"true" if branch_ir.condition.value else "false"})\n'
            f'    {{\n{statements(branch_ir.true_nodes)}\n    }}\n'
            f'    else\n    {{\n{statements(branch_ir.false_nodes)}\n    }}\n'
            f'}}\n')
    return {"status": "generated", "class_name": class_name,
            "files": [{"path": base + ".h", "content": header},
                      {"path": base + ".cpp", "content": body}],
            "warnings": ["Literal Branch only; generated C++ requires UBT validation."]}
