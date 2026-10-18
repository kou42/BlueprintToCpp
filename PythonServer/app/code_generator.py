"""Conservative Unreal C++ generator for an intentionally tiny BP subset.

Phase 3 MVP supports a single EventGraph with a single K2Node_Event
(ReceiveBeginPlay) and an optional straight-line chain of K2Node_CallFunction
(PrintString), with literal InString only. No implicit flow reconstruction,
unsupported semantics, pure nodes or unverified API signatures.
"""
from dataclasses import dataclass
import json
import re

from .schemas import Snapshot, SnapshotError
from .ir_builder import build_ir
from .node_registry import ConversionContext, NodeConverterRegistry, UnsupportedNodeError
from .parent_class import resolve_parent


class GenerationError(ValueError):
    pass


@dataclass(frozen=True)
class GeneratedFile:
    path: str
    content: str


def _cpp_string(value: str) -> str:
    # JSON string escaping is compatible with the C++ string escape sequences
    # used for ordinary printable text, after handling non-ASCII via TCHAR.
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    escaped = escaped.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    if "\x00" in escaped:
        raise GenerationError("NUL in string literal is unsupported")
    return 'TEXT("' + escaped + '")'


def _pin(node: dict, name: str, direction: str) -> dict:
    found = [p for p in node["pins"] if p["name"] == name and p["direction"] == direction]
    if len(found) != 1:
        raise GenerationError(f"{node['id']}: expected exactly one {direction} pin {name}")
    return found[0]


def _print_string(ctx: ConversionContext) -> str:
    node = ctx.node
    if node.get("function_name") != "PrintString":
        raise UnsupportedNodeError(f"{node['id']}: unsupported function {node.get('function_name')}")
    pins = node["pins"]
    allowed = {"execute", "then", "InString", "bPrintToScreen", "bPrintToLog",
               "TextColor", "Duration", "Key", "self", "WorldContextObject"}
    unsupported_pins = [
        p["name"]
        for p in pins
        if p["name"] not in allowed
    ]

    if unsupported_pins:
        raise UnsupportedNodeError(
            f"{node['id']}: unsupported PrintString pins: "
            f"{unsupported_pins}"
        )

    # UEのWorldContextObjectは、Actorのthisを渡すことで解決できる。
    # 明示的な指定は意味が変わり得るため、未接続・未設定の場合だけ許可する。
    context_pins = [p for p in pins if p["name"] == "WorldContextObject"]
    for context_pin in context_pins:
        if context_pin["direction"] != "input" or context_pin.get("links") or context_pin.get("default_value") not in ("", None):
            raise UnsupportedNodeError(
                f"{node['id']}: explicit WorldContextObject is unsupported")
    text = _pin(node, "InString", "input")
    if text["type"]["category"] != "string":
        raise GenerationError(f"{node['id']}: InString is not string")
    for edge in ctx.graph["data_edges"]:
        if edge["target_node"] == node["id"]:
            raise UnsupportedNodeError(f"{node['id']}: linked PrintString data inputs unsupported")
    # BlueprintのboolリテラルをC++へ変換し、画面／ログ出力設定を保持する。
    # 空値はUEのPrintStringの標準デフォルト（両方true）として扱う。
    bool_values = {}
    for name in ("bPrintToScreen", "bPrintToLog"):
        matching = [p for p in pins if p["name"] == name and p["direction"] == "input"]
        if len(matching) > 1:
            raise UnsupportedNodeError(f"{node['id']}: duplicate {name} pin")
        value = matching[0].get("default_value", "") if matching else ""
        if value == "":
            bool_values[name] = "true"
        elif value.lower() in ("true", "false"):
            bool_values[name] = value.lower()
        else:
            raise UnsupportedNodeError(f"{node['id']}: invalid {name} bool default {value!r}")
    # 色・表示時間・キーは意味を検証できるまで明示値を拒否する。
    for p in pins:
        if p["direction"] == "input" and p["name"] in ("TextColor", "Duration", "Key"):
            if p.get("default_value") not in ("", None):
                raise UnsupportedNodeError(
                    f"{node['id']}: explicit {p['name']} default requires semantic handling")
    return (f"    UKismetSystemLibrary::PrintString(this, {_cpp_string(text['default_value'])}, "
            f"{bool_values['bPrintToScreen']}, {bool_values['bPrintToLog']});")


def _is_class(node: dict, name: str) -> bool:
    return node["class"].rsplit(".", 1)[-1] == name


def generate_cpp(snapshot: Snapshot, class_name: str = "AGeneratedBlueprintActor") -> dict:
    parent = resolve_parent(snapshot.parent_class, class_name)
    if snapshot.variables:
        raise UnsupportedNodeError("MVP does not yet generate Blueprint member variables")
    ir = build_ir(snapshot)
    # 全グラフの接続整合性は build_ir で検証済み。
    # 実行線を持たないイベントは実行されないため、変換対象から除外する。
    event_graphs = [g for g in ir["graphs"] if g["kind"] == "event"]
    if len(event_graphs) != 1:
        raise UnsupportedNodeError("MVP requires exactly one EventGraph")
    graph = event_graphs[0]
    nodes = {n["id"]: n for n in graph["nodes"]}
    active = {e["source_node"] for e in graph["exec_edges"]} | {e["target_node"] for e in graph["exec_edges"]}
    for node in nodes.values():
        if _is_class(node, "K2Node_Event") and node["id"] not in active:
            # 未接続イベントはC++に副作用を生まない。
            continue
    nodes = {nid: n for nid, n in nodes.items()
             if nid in active or not _is_class(n, "K2Node_Event")
             or n.get("function_name") == "ReceiveBeginPlay"}
    if not nodes:
        raise UnsupportedNodeError("empty event graph")
    if graph["data_edges"]:
        raise UnsupportedNodeError("MVP does not support data links")
    events = [n for n in nodes.values() if _is_class(n, "K2Node_Event")]
    if len(events) != 1:
        raise UnsupportedNodeError("MVP requires exactly one event")
    event = events[0]
    if event.get("function_name") not in (None, "ReceiveBeginPlay"):
        raise UnsupportedNodeError("unsupported event function")
    # K2Node_Event snapshots from Phase 1 do not include event signature yet.
    # Require explicit event marker from an enhanced snapshot or refuse.
    if event.get("function_name") != "ReceiveBeginPlay":
        raise UnsupportedNodeError("event signature unresolved: export ReceiveBeginPlay function_name")
    registry = NodeConverterRegistry()
    registry.register("/Script/BlueprintGraph.K2Node_CallFunction", _print_string)
    edges = graph["exec_edges"]
    outgoing = {}
    incoming = {}
    for edge in edges:
        outgoing.setdefault(edge["source_node"], []).append(edge)
        incoming.setdefault(edge["target_node"], []).append(edge)
    if incoming.get(event["id"]):
        raise UnsupportedNodeError("event has incoming execution edges")
    current = event["id"]
    visited = {current}
    statements = []
    while True:
        successors = outgoing.get(current, [])
        if not successors:
            break
        if len(successors) != 1:
            raise UnsupportedNodeError("branches/parallel execution unsupported")
        edge = successors[0]
        next_id = edge["target_node"]
        if next_id in visited:
            raise UnsupportedNodeError("cycles unsupported in MVP code generation")
        if len(incoming.get(next_id, [])) != 1:
            raise UnsupportedNodeError("multiple incoming execution edges unsupported")
        next_node = nodes[next_id]
        if not _is_class(next_node, "K2Node_CallFunction"):
            raise UnsupportedNodeError(f"unsupported execution node {next_node['class']}")
        # Ensure the function node is exactly the API registered below.
        if next_node["class"] != "/Script/BlueprintGraph.K2Node_CallFunction":
            raise UnsupportedNodeError(f"unsupported class path: {next_node['class']}")
        statements.append(registry.convert(ConversionContext(next_node, graph)))
        visited.add(next_id)
        current = next_id
    if set(nodes) != visited:
        raise UnsupportedNodeError("unvisited nodes: disconnected or pure nodes unsupported")
    base = class_name[len(parent.prefix):]
    header = (f'#pragma once\n\n#include "CoreMinimal.h"\n'
              f'#include "{parent.header}"\n'
              f'#include "{base}.generated.h"\n\n'
              f'UCLASS()\nclass {class_name} : public {parent.cpp_name}\n{{\n'
              f'    GENERATED_BODY()\npublic:\n'
              f'    virtual void BeginPlay() override;\n}};\n')
    body = (f'#include "{base}.h"\n'
            f'#include "Kismet/KismetSystemLibrary.h"\n\n'
            f'void {class_name}::BeginPlay()\n{{\n'
            f'    Super::BeginPlay();\n'
            + ("\n".join(statements) + "\n" if statements else "")
            + "}\n")
    return {
        "status": "generated",
        "class_name": class_name,
        "files": [
            {"path": f"{base}.h", "content": header},
            {"path": f"{base}.cpp", "content": body},
        ],
        "warnings": [
            "Generated code is uncompiled and requires review.",
            "This MVP supports only ReceiveBeginPlay -> literal PrintString chains."
        ],
    }
