"""Validate pin links and construct typed execution/data edges.

Graphs are scoped by their asset graph path; cross-graph links are not silently
resolved. Multigraph edges retain source and target pin identities and order.
"""
from dataclasses import dataclass
from .schemas import Graph, Snapshot, SnapshotError


@dataclass(frozen=True)
class Edge:
    source_node: str
    source_pin: str
    target_node: str
    target_pin: str
    kind: str


@dataclass(frozen=True)
class AnalyzedGraph:
    path: str
    name: str
    kind: str
    node_count: int
    exec_edges: tuple[Edge, ...]
    data_edges: tuple[Edge, ...]
    entry_nodes: tuple[str, ...]


def analyze_graph(graph: Graph) -> AnalyzedGraph:
    nodes = {node.id: node for node in graph.nodes}
    pins = {(node.id, pin.id): pin for node in graph.nodes for pin in node.pins}
    edges = []
    for node in graph.nodes:
        for pin in node.pins:
            for link in pin.links:
                other = pins.get((link.node_id, link.pin_id))
                if other is None:
                    raise SnapshotError(
                        f"{graph.path}: dangling link {node.id}.{pin.id} -> "
                        f"{link.node_id}.{link.pin_id}")
                if pin.direction == other.direction:
                    raise SnapshotError(f"{graph.path}: invalid pin directions on {node.id}.{pin.id}")
                is_exec = pin.category == "exec"
                if is_exec != (other.category == "exec"):
                    raise SnapshotError(f"{graph.path}: exec/data mismatch on {node.id}.{pin.id}")
                if not is_exec and pin.category != other.category:
                    # Wildcard types require resolution in a later phase.
                    if "wildcard" not in (pin.category, other.category):
                        raise SnapshotError(
                            f"{graph.path}: incompatible pin categories {pin.category}/{other.category}")
                if not any(back.node_id == node.id and back.pin_id == pin.id
                           for back in other.links):
                    raise SnapshotError(f"{graph.path}: non-reciprocal link at {node.id}.{pin.id}")
                if pin.direction == "output":
                    edges.append(Edge(node.id, pin.id, link.node_id, link.pin_id,
                                      "exec" if is_exec else "data"))
    exec_edges = tuple(edge for edge in edges if edge.kind == "exec")
    data_edges = tuple(edge for edge in edges if edge.kind == "data")
    incoming = {edge.target_node for edge in exec_edges}
    exec_sources = {edge.source_node for edge in exec_edges}
    entries = tuple(node.id for node in graph.nodes
                    if node.id in exec_sources and node.id not in incoming)
    return AnalyzedGraph(graph.path, graph.name, graph.kind, len(nodes),
                         exec_edges, data_edges, entries)


def analyze_snapshot(snapshot: Snapshot) -> tuple[AnalyzedGraph, ...]:
    return tuple(analyze_graph(graph) for graph in snapshot.graphs)
