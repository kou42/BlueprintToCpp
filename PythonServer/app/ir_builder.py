"""Conservative graph IR. This is *not* a semantic C++-ready AST.

Phase 2 preserves graph connectivity and reports unsupported semantics rather
than pretending to reconstruct structured loops, pure evaluation or latent flow.
"""
from dataclasses import asdict
from .schemas import Snapshot
from .graph_analyzer import analyze_snapshot


def build_ir(snapshot: Snapshot) -> dict:
    analyzed = analyze_snapshot(snapshot)
    graphs = []
    for original, graph in zip(snapshot.graphs, analyzed):
        graphs.append({
            "path": graph.path,
            "name": graph.name,
            "kind": graph.kind,
            "entry_nodes": list(graph.entry_nodes),
            "nodes": [{
                "id": node.id,
                "class": node.node_class,
                "title": node.title,
                "function_name": node.function_name,
                "variable_name": node.variable_name,
                "pins": [{
                    "id": pin.id,
                    "name": pin.name,
                    "direction": pin.direction,
                    "type": pin.type_info,
                    "default_value": pin.default_value,
                } for pin in node.pins],
            } for node in original.nodes],
            "exec_edges": [asdict(edge) for edge in graph.exec_edges],
            "data_edges": [asdict(edge) for edge in graph.data_edges],
        })
    return {
        "ir_version": 1,
        "semantic_status": "graph_only",
        "asset_path": snapshot.asset_path,
        "parent_class": snapshot.parent_class,
        "graphs": graphs,
        "variables": list(snapshot.variables),
        "warnings": [
            "Control-flow structuring, pure-node evaluation, function semantics, "
            "and latent behavior are not implemented in Phase 2."
        ],
    }
