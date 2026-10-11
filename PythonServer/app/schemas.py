"""Strict parsing of Phase 1 Blueprint snapshots without third-party dependencies."""
from dataclasses import dataclass
from typing import Any


class SnapshotError(ValueError):
    pass


@dataclass(frozen=True)
class Link:
    node_id: str
    pin_id: str


@dataclass(frozen=True)
class Pin:
    id: str
    name: str
    direction: str
    category: str
    type_info: dict[str, Any]
    links: tuple[Link, ...]
    default_value: str


@dataclass(frozen=True)
class Node:
    id: str
    node_class: str
    title: str
    pins: tuple[Pin, ...]
    function_name: str | None
    variable_name: str | None


@dataclass(frozen=True)
class Graph:
    path: str
    name: str
    kind: str
    nodes: tuple[Node, ...]


@dataclass(frozen=True)
class Snapshot:
    asset_path: str
    parent_class: str
    engine_version: str
    graphs: tuple[Graph, ...]
    variables: tuple[dict[str, Any], ...]


def _object(value: Any, path: str) -> dict:
    if not isinstance(value, dict):
        raise SnapshotError(f"{path}: expected object")
    return value


def _array(value: Any, path: str) -> list:
    if not isinstance(value, list):
        raise SnapshotError(f"{path}: expected array")
    return value


def _string(value: Any, path: str) -> str:
    if not isinstance(value, str):
        raise SnapshotError(f"{path}: expected string")
    return value


def parse_snapshot(raw: Any) -> Snapshot:
    root = _object(raw, "snapshot")
    if type(root.get("schema_version")) is not int or root["schema_version"] != 1:
        raise SnapshotError("snapshot.schema_version: expected 1")
    graphs = []
    paths = set()
    for gi, graph_raw in enumerate(_array(root.get("graphs"), "graphs")):
        g = _object(graph_raw, f"graphs[{gi}]")
        path = _string(g.get("path"), f"graphs[{gi}].path")
        if path in paths:
            raise SnapshotError(f"duplicate graph path: {path}")
        paths.add(path)
        nodes = []
        node_ids = set()
        for ni, node_raw in enumerate(_array(g.get("nodes"), f"{path}.nodes")):
            n = _object(node_raw, f"{path}.nodes[{ni}]")
            nid = _string(n.get("id"), "node.id")
            if nid in node_ids:
                raise SnapshotError(f"{path}: duplicate node id {nid}")
            node_ids.add(nid)
            pins = []
            pin_ids = set()
            for pi, pin_raw in enumerate(_array(n.get("pins"), f"{nid}.pins")):
                p = _object(pin_raw, f"{nid}.pins[{pi}]")
                pid = _string(p.get("id"), "pin.id")
                if pid in pin_ids:
                    raise SnapshotError(f"{nid}: duplicate pin id {pid}")
                pin_ids.add(pid)
                direction = _string(p.get("direction"), "pin.direction")
                if direction not in ("input", "output"):
                    raise SnapshotError(f"{nid}.{pid}: invalid direction {direction}")
                typ = _object(p.get("type"), "pin.type")
                category = _string(typ.get("category"), "pin.type.category")
                links = []
                for link_raw in _array(p.get("links"), f"{nid}.{pid}.links"):
                    link = _object(link_raw, "link")
                    links.append(Link(_string(link.get("node_id"), "link.node_id"),
                                      _string(link.get("pin_id"), "link.pin_id")))
                pins.append(Pin(pid, _string(p.get("name"), "pin.name"), direction,
                                category, typ, tuple(links),
                                _string(p.get("default_value", ""), "pin.default_value")))
            function_name = n.get("function_name")
            if function_name is not None:
                _string(function_name, "node.function_name")
            variable_name = n.get("variable_name")
            if variable_name is not None:
                _string(variable_name, "node.variable_name")
            nodes.append(Node(nid, _string(n.get("class"), "node.class"),
                              _string(n.get("title"), "node.title"),
                              tuple(pins), function_name, variable_name))
        graphs.append(Graph(path, _string(g.get("name"), "graph.name"),
                            _string(g.get("kind"), "graph.kind"), tuple(nodes)))
    variables = _array(root.get("variables"), "variables")
    for var in variables:
        _object(var, "variable")
    return Snapshot(_string(root.get("asset_path"), "asset_path"),
                    _string(root.get("parent_class"), "parent_class"),
                    _string(root.get("engine_version"), "engine_version"),
                    tuple(graphs), tuple(variables))
