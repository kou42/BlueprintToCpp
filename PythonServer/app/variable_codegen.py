"""Restricted UE Actor variable declarations and literal Set/Get lowering.

Only unlinked literal Set inputs, and Get->Branch.Condition for bool are
supported. All other data edges and execution nodes are rejected.
"""
import re
from .schemas import Snapshot
from .ir_builder import build_ir
from .node_registry import UnsupportedNodeError
from .code_generator import GenerationError, _cpp_string

TYPES = {"bool": "bool", "int": "int32", "int64": "int64", "float": "float"}
GET = "/Script/BlueprintGraph.K2Node_VariableGet"
SET = "/Script/BlueprintGraph.K2Node_VariableSet"
EVENT = "/Script/BlueprintGraph.K2Node_Event"
BRANCH = "/Script/BlueprintGraph.K2Node_IfThenElse"


def _variable_name(name: str) -> str:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
        raise UnsupportedNodeError(f"invalid variable name {name!r}")
    if name in {"class", "default", "new", "delete", "this", "true", "false",
                "if", "else", "return", "virtual", "public", "private", "protected"}:
        raise UnsupportedNodeError(f"reserved variable name {name!r}")
    return name


def _type(var: dict) -> str:
    typ = var.get("type", {})
    category = typ.get("category")
    if category not in TYPES or typ.get("container", 0) != 0 or typ.get("is_reference", False):
        raise UnsupportedNodeError(f"unsupported variable type: {category}")
    # float pin category can encode double/float in UE5; require explicit subcategory.
    if category == "float" and typ.get("subcategory") not in ("float",):
        raise UnsupportedNodeError("ambiguous UE5 float precision")
    return TYPES[category]


def _literal(value: str, category: str) -> str:
    if category == "bool":
        if value.lower() not in ("true", "false"):
            raise UnsupportedNodeError(f"invalid bool literal {value!r}")
        return value.lower()
    if category in ("int", "int64"):
        if not re.fullmatch(r"[+-]?[0-9]+", value):
            raise UnsupportedNodeError(f"invalid integer literal {value!r}")
        num = int(value)
        bounds = (-2**31, 2**31 - 1) if category == "int" else (-2**63, 2**63 - 1)
        if not bounds[0] <= num <= bounds[1]:
            raise UnsupportedNodeError("integer literal out of range")
        return str(num) + ("LL" if category == "int64" else "")
    if category == "float":
        if not re.fullmatch(r"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?", value):
            raise UnsupportedNodeError(f"invalid float literal {value!r}")
        return value + ("f" if "." in value or "e" in value.lower() else ".0f")
    raise UnsupportedNodeError(f"unsupported literal type {category}")


def _pin(node: dict, name: str, direction: str) -> dict:
    matches = [p for p in node["pins"] if p["name"] == name and p["direction"] == direction]
    if len(matches) != 1:
        raise UnsupportedNodeError(f"{node['id']}: missing {direction} pin {name}")
    return matches[0]


def generate_variable_cpp(snapshot: Snapshot, class_name: str) -> dict:
    if not re.fullmatch(r"A[A-Za-z_][A-Za-z_0-9]*", class_name):
        raise GenerationError("invalid Actor class name")
    if snapshot.parent_class != "/Script/Engine.Actor":
        raise UnsupportedNodeError("direct Actor parent required")
    ir = build_ir(snapshot)
    if len(ir["graphs"]) != 1 or ir["graphs"][0]["kind"] != "event":
        raise UnsupportedNodeError("one EventGraph required")
    graph = ir["graphs"][0]
    nodes = {n["id"]: n for n in graph["nodes"]}
    events = [n for n in nodes.values() if n["class"] == EVENT and n.get("function_name") == "ReceiveBeginPlay"]
    if len(events) != 1:
        raise UnsupportedNodeError("one ReceiveBeginPlay event required")
    variables = {}
    for var in snapshot.variables:
        name = _variable_name(var.get("name", ""))
        if name in variables:
            raise UnsupportedNodeError(f"duplicate variable {name}")
        variables[name] = (var, _type(var))
    if not variables:
        raise UnsupportedNodeError("no variables to convert")
    # This incremental converter supports only a linear chain of variable sets.
    if any(n["class"] not in (EVENT, SET) for n in nodes.values()):
        raise UnsupportedNodeError("only BeginPlay and VariableSet nodes supported")
    if graph["data_edges"]:
        raise UnsupportedNodeError("connected Set values are not yet supported")
    outgoing = {}
    incoming = {}
    for edge in graph["exec_edges"]:
        outgoing.setdefault(edge["source_node"], []).append(edge)
        incoming.setdefault(edge["target_node"], []).append(edge)
    current = events[0]["id"]
    visited = {current}
    statements = []
    while outgoing.get(current):
        edges = outgoing[current]
        if len(edges) != 1:
            raise UnsupportedNodeError("nonlinear execution unsupported")
        target = edges[0]["target_node"]
        if target in visited or len(incoming.get(target, [])) != 1:
            raise UnsupportedNodeError("cycle or merge unsupported")
        node = nodes[target]
        if node["class"] != SET:
            raise UnsupportedNodeError("unsupported execution node")
        name = node.get("variable_name")
        if name not in variables:
            raise UnsupportedNodeError(f"{target}: unknown variable {name!r}")
        var, cpp_type = variables[name]
        pin = _pin(node, name, "input")
        if pin["type"]["category"] != var["type"]["category"]:
            raise UnsupportedNodeError(f"{target}: variable pin type mismatch")
        # Verify all non-exec pins are the expected value pin only.
        if any(p["type"]["category"] != "exec" and p is not pin for p in node["pins"]):
            raise UnsupportedNodeError(f"{target}: unsupported data pins")
        statements.append(f"    {name} = {_literal(pin['default_value'], var['type']['category'])};")
        visited.add(target)
        current = target
    if set(nodes) != visited:
        raise UnsupportedNodeError("disconnected nodes unsupported")
    if incoming.get(events[0]["id"]):
        raise UnsupportedNodeError("event has incoming execution edges")
    declarations = []
    for name, (var, cpp_type) in variables.items():
        default = var.get("default_value", "")
        if not default:
            default = "false" if cpp_type == "bool" else "0"
        declarations.append(f"    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=\"Converted\")\n"
                            f"    {cpp_type} {name} = {_literal(default, var['type']['category'])};")
    base = class_name[1:]
    header = (f'#pragma once\n#include "CoreMinimal.h"\n'
              f'#include "GameFramework/Actor.h"\n'
              f'#include "{base}.generated.h"\n\n'
              f'UCLASS()\nclass {class_name} : public AActor\n{{\n'
              f'    GENERATED_BODY()\npublic:\n'
              f'    virtual void BeginPlay() override;\n'
              + "\n".join(declarations) + "\n};\n")
    body = (f'#include "{base}.h"\n\n'
            f'void {class_name}::BeginPlay()\n{{\n'
            f'    Super::BeginPlay();\n'
            + ("\n".join(statements) + "\n" if statements else "")
            + "}\n")
    return {"status": "generated", "class_name": class_name,
            "files": [{"path": base + ".h", "content": header},
                      {"path": base + ".cpp", "content": body}],
            "warnings": ["VariableSet literals only; no Get/data links; UBT not verified."]}
