"""Copilot Graph JSON v1 の構造・参照整合性を検証する。

UE実機での意味的な正しさは判定しない。欠落を推測して補完しない。
"""
import json
import re
import sys
from pathlib import Path

GUID = re.compile(r"^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$")


def validate(document):
    errors = []
    if not isinstance(document, dict):
        return ["root must be an object"]
    if document.get("format") != "blueprint_copilot_graph":
        errors.append("unexpected format")
    if type(document.get("schema_version")) is not int or document["schema_version"] != 1:
        errors.append("unsupported schema_version")
    if document.get("semantic_status") not in ("partial", "unsupported"):
        errors.append("invalid semantic_status")
    if not isinstance(document.get("asset_path"), str) or not document["asset_path"]:
        errors.append("missing asset_path")
    graphs = document.get("graphs")
    if not isinstance(graphs, list):
        return errors + ["graphs must be an array"]
    paths = []
    for gi, graph in enumerate(graphs):
        prefix = f"graphs[{gi}]"
        if not isinstance(graph, dict):
            errors.append(f"{prefix}: not an object")
            continue
        path = graph.get("path")
        if not isinstance(path, str):
            errors.append(f"{prefix}: invalid path")
        else:
            paths.append(path)
        nodes = graph.get("nodes")
        if not isinstance(nodes, list):
            errors.append(f"{prefix}: nodes must be an array")
            continue
        node_pins = {}
        pin_owners = {}
        pin_ids = set()
        target_references = []
        for ni, node in enumerate(nodes):
            loc = f"{prefix}.nodes[{ni}]"
            if not isinstance(node, dict):
                errors.append(f"{loc}: not an object")
                continue
            nid = node.get("id")
            if not isinstance(nid, str) or GUID.fullmatch(nid) is None:
                errors.append(f"{loc}: invalid node GUID")
                continue
            if nid in node_pins:
                errors.append(f"{loc}: duplicate node GUID")
            pins = node.get("pins")
            if not isinstance(pins, list):
                errors.append(f"{loc}: pins must be an array")
                continue
            current = set()
            for pi, pin in enumerate(pins):
                ploc = f"{loc}.pins[{pi}]"
                if not isinstance(pin, dict):
                    errors.append(f"{ploc}: not an object")
                    continue
                pid = pin.get("id")
                if not isinstance(pid, str) or GUID.fullmatch(pid) is None:
                    errors.append(f"{ploc}: invalid pin GUID")
                    continue
                if pid in pin_ids:
                    errors.append(f"{ploc}: duplicate pin GUID")
                pin_ids.add(pid)
                pin_owners[pid] = nid
                current.add(pid)
                if pin.get("direction") not in ("input", "output"):
                    errors.append(f"{ploc}: invalid direction")
            if "function_dispatch_kind" in node and node["function_dispatch_kind"] not in ("regular", "interface_message", "interface_call", "unresolved", "not_applicable"):
                errors.append(f"{loc}: invalid function_dispatch_kind")
            if node.get("function_dispatch_kind") in ("interface_message", "interface_call"):
                if node.get("function_resolved") is not True:
                    errors.append(f"{loc}: interface dispatch requires resolved function")
                if not node.get("function_owner_path") or not node.get("function_path"):
                    errors.append(f"{loc}: interface dispatch missing function identity")
            if node.get("function_dispatch_kind") == "unresolved" and node.get("function_resolved") is True:
                errors.append(f"{loc}: unresolved dispatch contradicts resolved function")
            if "function_latent" in node and type(node["function_latent"]) is not bool:
                errors.append(f"{loc}: invalid function_latent")
            node_pins[nid] = current
            # 追加フィールドは旧JSONとの互換性のため任意。
            # 存在する場合はSelf Pinが実際にそのNodeに属することを検証する。
            if "function_target_kind" in node:
                kind = node["function_target_kind"]
                allowed = ("self", "explicit_target", "unconnected_target",
                           "static", "unresolved", "not_applicable")
                if kind not in allowed:
                    errors.append(f"{loc}: invalid function_target_kind")
                target_pin = node.get("function_target_pin_id")
                links = node.get("function_target_linked_pin_ids")
                if target_pin is not None and target_pin not in current:
                    errors.append(f"{loc}: invalid function_target_pin_id")
                if not isinstance(links, list):
                    errors.append(f"{loc}: target links must be an array")
                elif any(not isinstance(link, str) or GUID.fullmatch(link) is None
                         for link in links):
                    errors.append(f"{loc}: invalid target linked pin GUID")
                if kind in ("self", "explicit_target", "unconnected_target") and target_pin is None:
                    errors.append(f"{loc}: target kind requires Self pin")
                if kind in ("static", "unresolved", "not_applicable") and target_pin is not None:
                    errors.append(f"{loc}: target kind must not have Self pin")
                if isinstance(links, list):
                    if all(isinstance(link, str) for link in links) and len(links) != len(set(links)):
                        errors.append(f"{loc}: duplicate target linked pin GUID")
                    if all(isinstance(link, str) for link in links) and links != sorted(links):
                        errors.append(f"{loc}: target links are not sorted")
                    if target_pin is None and links:
                        errors.append(f"{loc}: target links without Self pin")
                    if kind in ("self", "unconnected_target") and links:
                        errors.append(f"{loc}: unconnected target has linked pins")
                    if all(isinstance(link, str) for link in links):
                        target_references.append((loc, nid, target_pin, links))
            if node.get("function_resolved") is True and (not node.get("function_path") or not node.get("function_owner_path")):
                errors.append(f"{loc}: resolved function missing identity")
        for kind in ("exec_edges", "data_edges"):
            edges = graph.get(kind)
            if not isinstance(edges, list):
                errors.append(f"{prefix}: {kind} must be an array")
                continue
            for ei, edge in enumerate(edges):
                loc = f"{prefix}.{kind}[{ei}]"
                if not isinstance(edge, dict):
                    errors.append(f"{loc}: not an object")
                    continue
                source = edge.get("source_node_id")
                target = edge.get("target_node_id")
                if source not in node_pins or edge.get("source_pin_id") not in node_pins.get(source, set()):
                    errors.append(f"{loc}: invalid source endpoint")
                if target not in node_pins or edge.get("target_pin_id") not in node_pins.get(target, set()):
                    errors.append(f"{loc}: invalid target endpoint")
        # ノード収集後に全接続を検査する。出力ノードの登場順には依存しない。
        data_links = {
            (edge.get("source_node_id"), edge.get("source_pin_id"),
             edge.get("target_node_id"), edge.get("target_pin_id"))
            for edge in graph.get("data_edges", [])
            if isinstance(edge, dict)
        } if isinstance(graph.get("data_edges"), list) else set()
        for loc, nid, target_pin, links in target_references:
            if target_pin is None:
                continue
            for linked_pin in links:
                if (pin_owners.get(linked_pin), linked_pin, nid, target_pin) not in data_links:
                    errors.append(f"{loc}: target linked pin missing data edge")
    diagnostics = document.get("diagnostics")
    if not isinstance(diagnostics, list) or any(not isinstance(d, str) for d in diagnostics):
        errors.append("diagnostics must be an array of strings")
    elif diagnostics != sorted(diagnostics):
        errors.append("diagnostics are not sorted")
    if paths != sorted(paths):
        errors.append("graphs are not sorted by path")
    return errors


def main():
    if len(sys.argv) != 2:
        print("Usage: python validate_copilot_graph_json.py FILE", file=sys.stderr)
        return 2
    try:
        document = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(exc, file=sys.stderr)
        return 2
    errors = validate(document)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print("Copilot Graph JSON v1: valid (structural checks only)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
