"""Conservative typed pin expressions and Branch CFG lowering.

Only literal bool Branch conditions are accepted in this increment.
Unsupported data dependencies are rejected rather than guessed.
"""
from dataclasses import dataclass
from .node_registry import UnsupportedNodeError


@dataclass(frozen=True)
class BoolLiteral:
    value: bool
    source_node: str
    source_pin: str


@dataclass(frozen=True)
class IfStatement:
    condition: BoolLiteral
    true_nodes: tuple[str, ...]
    false_nodes: tuple[str, ...]
    source_node: str


def _find_pin(node: dict, name: str, direction: str) -> dict:
    found = [p for p in node["pins"]
             if p["name"].lower() == name.lower() and p["direction"] == direction]
    if len(found) != 1:
        raise UnsupportedNodeError(f"{node['id']}: expected one {direction} {name} pin")
    return found[0]


def parse_bool_literal(node: dict, graph: dict) -> BoolLiteral:
    pin = _find_pin(node, "Condition", "input")
    if pin["type"]["category"] != "bool":
        raise UnsupportedNodeError(f"{node['id']}: Branch Condition must be bool")
    if any(edge["target_node"] == node["id"] and edge["target_pin"] == pin["id"]
           for edge in graph["data_edges"]):
        raise UnsupportedNodeError(f"{node['id']}: connected Branch Condition not yet supported")
    value = pin.get("default_value", "").lower()
    if value not in ("true", "false"):
        raise UnsupportedNodeError(f"{node['id']}: unresolved bool default {value!r}")
    return BoolLiteral(value == "true", node["id"], pin["id"])


def lower_single_branch(graph: dict) -> IfStatement:
    nodes = {node["id"]: node for node in graph["nodes"]}
    branches = [node for node in nodes.values()
                if node["class"] == "/Script/BlueprintGraph.K2Node_IfThenElse"]
    if len(branches) != 1:
        raise UnsupportedNodeError("requires exactly one K2Node_IfThenElse")
    branch = branches[0]
    condition = parse_bool_literal(branch, graph)
    outgoing: dict[str, list[dict]] = {}
    incoming: dict[str, list[dict]] = {}
    for edge in graph["exec_edges"]:
        outgoing.setdefault(edge["source_node"], []).append(edge)
        incoming.setdefault(edge["target_node"], []).append(edge)

    def arm(name: str) -> tuple[str, ...]:
        pin = _find_pin(branch, name, "output")
        matches = [edge for edge in outgoing.get(branch["id"], [])
                   if edge["source_pin"] == pin["id"]]
        if len(matches) > 1:
            raise UnsupportedNodeError(f"{branch['id']}: multiple {name} edges")
        if not matches:
            return ()
        edge = matches[0]
        if len(incoming.get(edge["target_node"], [])) != 1:
            raise UnsupportedNodeError("branch arm has multiple incoming edges")
        node = nodes[edge["target_node"]]
        if node["class"] != "/Script/BlueprintGraph.K2Node_CallFunction":
            raise UnsupportedNodeError("branch arm supports only PrintString")
        if node.get("function_name") != "PrintString":
            raise UnsupportedNodeError("branch arm supports only PrintString")
        if outgoing.get(node["id"]):
            raise UnsupportedNodeError("branch arm continuation unsupported")
        return (node["id"],)

    true_nodes = arm("then")
    false_nodes = arm("else")
    return IfStatement(condition, true_nodes, false_nodes, branch["id"])
