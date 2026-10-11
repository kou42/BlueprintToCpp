"""Dispatch to strict generators; do not fall back on generation errors."""
from .schemas import Snapshot
from .code_generator import generate_cpp
from .branch_codegen import generate_branch_cpp
from .variable_codegen import generate_variable_cpp


def generate_supported_cpp(snapshot: Snapshot, class_name: str = "AGeneratedBlueprintActor") -> dict:
    has_branch = any(
        node.node_class == "/Script/BlueprintGraph.K2Node_IfThenElse"
        for graph in snapshot.graphs for node in graph.nodes
    )
    has_variable_node = any(
        node.node_class in (
            '/Script/BlueprintGraph.K2Node_VariableGet',
            '/Script/BlueprintGraph.K2Node_VariableSet')
        for graph in snapshot.graphs for node in graph.nodes
    )
    if has_variable_node:
        return generate_variable_cpp(snapshot, class_name)
    if has_branch:
        return generate_branch_cpp(snapshot, class_name)
    return generate_cpp(snapshot, class_name)
