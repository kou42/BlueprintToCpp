"""Semantic JSON v2の厳密な構造検証。Snapshot v1とは独立して扱う。"""
from __future__ import annotations

from typing import Any
import re
import math
from uuid import UUID


class SemanticJsonError(ValueError):
    """Semantic JSONの契約違反。"""


TYPE_KINDS = frozenset(("unresolved", "void", "bool", "int32", "int64",
                        "float", "double", "string", "name", "text", "object", "struct",
                        "enum", "byte", "delegate", "multicast_delegate"))
EXPRESSION_KINDS = frozenset(("literal", "variable_reference", "function_call"))
LOOP_KINDS = frozenset(("for_loop", "for_each_loop"))
STATEMENT_KINDS = frozenset(("expression", "assignment", "return", "branch", "break")) | LOOP_KINDS
BLOCK_TERMINATORS = frozenset(("unspecified", "end", "fallthrough",
                               "return", "branch", "loop"))
STATUSES = frozenset(("complete", "partial", "unsupported"))
SEVERITIES = frozenset(("info", "warning", "error"))


def _object(value: Any, path: str) -> dict:
    if not isinstance(value, dict):
        raise SemanticJsonError(f"{path}: expected object")
    return value


def _array(value: Any, path: str) -> list:
    if not isinstance(value, list):
        raise SemanticJsonError(f"{path}: expected array")
    return value


def _string(value: Any, path: str) -> str:
    if not isinstance(value, str):
        raise SemanticJsonError(f"{path}: expected string")
    return value


def _bool(value: Any, path: str) -> bool:
    if type(value) is not bool:
        raise SemanticJsonError(f"{path}: expected boolean")
    return value


def _enum(value: Any, options: frozenset[str], path: str) -> str:
    result = _string(value, path)
    if result not in options:
        raise SemanticJsonError(f"{path}: unknown value {result!r}")
    return result


def _nullable_string(value: Any, path: str) -> None:
    if value is not None:
        _string(value, path)


def _guid(value: Any, path: str) -> None:
    if value is None:
        return
    raw = _string(value, path)
    try:
        parsed = UUID(raw)
    except ValueError as exc:
        raise SemanticJsonError(f"{path}: invalid GUID") from exc
    if str(parsed) != raw.lower() or parsed.int == 0:
        raise SemanticJsonError(f"{path}: expected non-zero canonical GUID")


def _required(obj: dict, names: tuple[str, ...], path: str) -> None:
    for name in names:
        if name not in obj:
            raise SemanticJsonError(f"{path}.{name}: missing field")


def _type(raw: Any, path: str, depth: int = 0) -> None:
    if depth > 64:
        raise SemanticJsonError(f"{path}: type nesting exceeds 64")
    obj = _object(raw, path)
    _required(obj, ("kind", "type_path", "is_reference", "is_const"), path)
    _enum(obj["kind"], TYPE_KINDS, path + ".kind")
    _string(obj["type_path"], path + ".type_path")
    _bool(obj["is_reference"], path + ".is_reference")
    _bool(obj["is_const"], path + ".is_const")
    # Container metadata is optional in older v2 files; validate it when present.
    if "container_kind" in obj:
        _enum(obj["container_kind"], frozenset(("", "none", "array", "set", "map")),
              path + ".container_kind")
    for field in ("inner_type", "key_type", "value_type"):
        if field in obj:
            _string(obj[field], path + "." + field)
    for field in ("element_type_info", "key_type_info", "value_type_info"):
        if field in obj and obj[field] is not None:
            _type(obj[field], path + "." + field, depth + 1)


def _expression(raw: Any, path: str, depth: int = 0) -> None:
    # 悪意のある入力による過剰な再帰を防ぐ。
    if depth > 64:
        raise SemanticJsonError(f"{path}: expression nesting exceeds 64")
    obj = _object(raw, path)
    names = ("kind", "type", "source_node_id", "source_pin_id", "literal_value",
             "variable_name", "function_name", "function_owner_path", "arguments")
    _required(obj, names, path)
    _enum(obj["kind"], EXPRESSION_KINDS, path + ".kind")
    _type(obj["type"], path + ".type")
    _guid(obj["source_node_id"], path + ".source_node_id")
    _guid(obj["source_pin_id"], path + ".source_pin_id")
    _string(obj["literal_value"], path + ".literal_value")
    for key in ("variable_name", "function_name", "function_owner_path"):
        _nullable_string(obj[key], path + "." + key)
    for i, arg in enumerate(_array(obj["arguments"], path + ".arguments")):
        _expression(arg, f"{path}.arguments[{i}]", depth + 1)


def _statement(raw: Any, path: str, depth: int = 0) -> None:
    if depth > 64:
        raise SemanticJsonError(f"{path}: statement nesting exceeds 64")
    obj = _object(raw, path)
    _required(obj, ("kind", "source_node_id", "assignment_target", "expression"), path)
    kind = _enum(obj["kind"], STATEMENT_KINDS, path + ".kind")
    _guid(obj["source_node_id"], path + ".source_node_id")
    _nullable_string(obj["assignment_target"], path + ".assignment_target")
    _expression(obj["expression"], path + ".expression")
    if kind == "branch":
        _required(obj, ("then_statements", "else_statements"), path)
        if obj["expression"]["type"]["kind"] != "bool":
            raise SemanticJsonError(f"{path}.expression.type: branch requires a bool condition")
    if kind in LOOP_KINDS:
        _required(obj, ("loop_index_variable", "loop_body_statements", "completed_statements"), path)
    if kind == "for_loop":
        _required(obj, ("loop_first_index", "loop_last_index"), path)
    if kind == "for_each_loop":
        _required(obj, ("loop_array", "loop_element_variable", "loop_element_type"), path)
    for field in ("loop_index_variable", "loop_element_variable"):
        if field in obj:
            _nullable_string(obj[field], path + "." + field)
    for field in ("loop_first_index", "loop_last_index", "loop_array"):
        if field in obj:
            _expression(obj[field], path + "." + field)
    if "loop_element_type" in obj:
        _type(obj["loop_element_type"], path + ".loop_element_type")
    for field in ("then_statements", "else_statements", "loop_body_statements", "completed_statements"):
        if field not in obj:
            continue
        children = _array(obj[field], path + "." + field)
        if field in ("then_statements", "else_statements"):
            if kind != "branch" and children:
                raise SemanticJsonError(f"{path}.{field}: only branch statements may contain branches")
        elif kind not in LOOP_KINDS and children:
            raise SemanticJsonError(f"{path}.{field}: only loop statements may contain loop bodies or Completed")
        for index, child in enumerate(children):
            _statement(child, f"{path}.{field}[{index}]", depth + 1)


WIDGET_TEXT_FIELDS = ("text", "text_culture_invariant", "text_localization_identity_available",
                      "text_namespace", "text_key", "text_source")


def _widget_text(obj: dict, path: str) -> None:
    _required(obj, WIDGET_TEXT_FIELDS, path)
    for field in ("text", "text_namespace", "text_key", "text_source"):
        _string(obj[field], path + "." + field)
    invariant = _bool(obj["text_culture_invariant"], path + ".text_culture_invariant")
    localized = _bool(obj["text_localization_identity_available"],
                      path + ".text_localization_identity_available")
    if invariant and localized:
        raise SemanticJsonError(f"{path}: conflicting text identity")
    if localized and not obj["text_key"]:
        raise SemanticJsonError(f"{path}.text_key: localized text requires a key")
    if not localized and any(obj[field] for field in ("text_namespace", "text_key", "text_source")):
        raise SemanticJsonError(f"{path}: undeclared text localization identity")
    if obj["text"] and not (invariant or localized):
        raise SemanticJsonError(f"{path}: nonempty text requires invariant text or localization identity")


def _widget_identifier(value: Any, path: str) -> str:
    name = _string(value, path)
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name) or name.casefold() == "none":
        raise SemanticJsonError(f"{path}: expected non-None C++ identifier")
    return name


def _widget_float(value: Any, path: str) -> None:
    if type(value) not in (int, float) or abs(value) > 3.4028234663852886e38 or not math.isfinite(value):
        raise SemanticJsonError(f"{path}: expected finite float-range number")


def _widget_canvas_slot(raw: Any, path: str) -> None:
    slot = _object(raw, path)
    _required(slot, ("offsets", "anchors_minimum", "anchors_maximum", "alignment", "auto_size", "z_order"), path)
    for field, count in (("offsets", 4), ("anchors_minimum", 2), ("anchors_maximum", 2), ("alignment", 2)):
        values = _array(slot[field], path + "." + field)
        if len(values) != count:
            raise SemanticJsonError(f"{path}.{field}: expected {count} values")
        for value in values:
            if field == "offsets":
                _widget_float(value, path + "." + field)
            elif type(value) not in (int, float) or abs(value) > 1.7976931348623157e308 or not math.isfinite(value):
                raise SemanticJsonError(f"{path}.{field}: expected finite double-range number")
    _bool(slot["auto_size"], path + ".auto_size")
    if type(slot["z_order"]) is not int or not -(2**31) <= slot["z_order"] < 2**31:
        raise SemanticJsonError(f"{path}.z_order: expected int32")


def _widget_tree(raw: Any, parent_path: str) -> None:
    obj = _object(raw, "widget_tree")
    _required(obj, ("kind", "root_name") + WIDGET_TEXT_FIELDS, "widget_tree")
    kind = _enum(obj["kind"], frozenset(("empty", "text_block", "panel")), "widget_tree.kind")
    if parent_path != "/Script/UMG.UserWidget":
        raise SemanticJsonError("widget_tree: requires direct UserWidget parent")
    _widget_text(obj, "widget_tree")
    nodes = _array(obj.get("nodes", []), "widget_tree.nodes")
    if kind != "panel" and nodes:
        raise SemanticJsonError("widget_tree: legacy payload cannot hide panel nodes")
    if kind == "empty":
        if obj["root_name"] is not None or any(obj[field] for field in WIDGET_TEXT_FIELDS):
            raise SemanticJsonError("widget_tree: empty tree contains TextBlock data")
        return
    _widget_identifier(obj["root_name"], "widget_tree.root_name")
    if kind == "text_block":
        return
    if any(obj[field] for field in WIDGET_TEXT_FIELDS) or not 1 <= len(nodes) <= 256:
        raise SemanticJsonError("widget_tree: invalid panel node count or hidden root text")
    names = set()
    depths = []
    for index, raw_node in enumerate(nodes):
        path = f"widget_tree.nodes[{index}]"
        node = _object(raw_node, path)
        _required(node, ("kind", "name", "parent_index", "padding", "horizontal_alignment",
                         "vertical_alignment", "size_rule", "size_value") + WIDGET_TEXT_FIELDS, path)
        node_kind = _enum(node["kind"], frozenset(("text_block", "vertical_box", "horizontal_box", "overlay", "canvas_panel")), path + ".kind")
        name = _widget_identifier(node["name"], path + ".name").casefold()
        if name in names:
            raise SemanticJsonError(f"{path}: duplicate widget name")
        names.add(name)
        parent = node["parent_index"]
        if type(parent) is not int or (index == 0 and parent != -1) or (index > 0 and not 0 <= parent < index):
            raise SemanticJsonError(f"{path}: parent must precede child, root parent must be -1")
        if index == 0 and (node_kind == "text_block" or node["name"].casefold() != obj["root_name"].casefold()):
            raise SemanticJsonError(f"{path}: root must be a matching panel")
        if index > 0 and nodes[parent]["kind"] == "text_block":
            raise SemanticJsonError(f"{path}: TextBlock cannot have children")
        depth = 0 if index == 0 else depths[parent] + 1
        if depth > 64:
            raise SemanticJsonError(f"{path}: widget nesting exceeds 64")
        depths.append(depth)
        _widget_text(node, path)
        if node_kind != "text_block" and any(node[field] for field in WIDGET_TEXT_FIELDS):
            raise SemanticJsonError(f"{path}: panel cannot contain TextBlock data")
        padding = _array(node["padding"], path + ".padding")
        if len(padding) != 4:
            raise SemanticJsonError(f"{path}.padding: expected left/top/right/bottom")
        for component in padding:
            _widget_float(component, path + ".padding")
        _enum(node["horizontal_alignment"], frozenset(("fill", "left", "center", "right")), path + ".horizontal_alignment")
        _enum(node["vertical_alignment"], frozenset(("fill", "top", "center", "bottom")), path + ".vertical_alignment")
        _enum(node["size_rule"], frozenset(("auto", "fill")), path + ".size_rule")
        _widget_float(node["size_value"], path + ".size_value")
        if node["size_value"] < 0:
            raise SemanticJsonError(f"{path}.size_value: expected nonnegative value")
        if index == 0 and (any(padding) or node["horizontal_alignment"] != "fill" or
                           node["vertical_alignment"] != "fill" or node["size_rule"] != "auto" or node["size_value"] != 1):
            raise SemanticJsonError(f"{path}: root cannot have slot data")
        parent_kind = None if index == 0 else nodes[parent]["kind"]
        if parent_kind == "canvas_panel":
            _required(node, ("canvas_slot",), path)
            _widget_canvas_slot(node["canvas_slot"], path + ".canvas_slot")
            if any(padding) or node["horizontal_alignment"] != "fill" or node["vertical_alignment"] != "fill":
                raise SemanticJsonError(f"{path}: Canvas slot cannot contain box/overlay layout")
        elif "canvas_slot" in node:
            raise SemanticJsonError(f"{path}: Canvas slot requires CanvasPanel parent")
        if parent_kind in ("canvas_panel", "overlay") and (node["size_rule"] != "auto" or node["size_value"] != 1):
            raise SemanticJsonError(f"{path}: slot size is only supported for box parents")


def parse_semantic_json(raw: Any) -> dict:
    """v2の形状を検証して返す。意味的一致やC++生成可能性は保証しない。"""
    root = _object(raw, "semantic")
    _required(root, ("schema_version", "format", "producer", "source",
                     "semantic_status", "functions", "diagnostics"), "semantic")
    if type(root["schema_version"]) is not int or root["schema_version"] != 2:
        raise SemanticJsonError("semantic.schema_version: expected 2")
    if root["format"] != "blueprint_semantic_ir":
        raise SemanticJsonError("semantic.format: unsupported format")
    producer = _object(root["producer"], "producer")
    _required(producer, ("name", "engine_version"), "producer")
    _string(producer["name"], "producer.name")
    _string(producer["engine_version"], "producer.engine_version")
    source = _object(root["source"], "source")
    _required(source, ("asset_path", "parent_class_path", "snapshot_schema_version"), "source")
    _string(source["asset_path"], "source.asset_path")
    _string(source["parent_class_path"], "source.parent_class_path")
    if "widget_tree" in root:
        _widget_tree(root["widget_tree"], source["parent_class_path"])
    if type(source["snapshot_schema_version"]) is not int or source["snapshot_schema_version"] != 1:
        raise SemanticJsonError("source.snapshot_schema_version: expected 1")
    # 旧v2 JSONにはvariablesが存在しないため、省略を許容する。
    variable_names: set[str] = set()
    for i, raw_variable in enumerate(_array(root.get("variables", []), "variables")):
        path = f"variables[{i}]"
        variable = _object(raw_variable, path)
        _required(variable, ("name", "guid", "type", "pin_category", "type_path",
                             "property_flags", "source", "default_value_available",
                             "default_value"), path)
        name = _string(variable["name"], path + ".name")
        if not name or name in variable_names:
            raise SemanticJsonError(f"{path}: empty or duplicate variable name")
        variable_names.add(name)
        _guid(variable["guid"], path + ".guid")
        _type(variable["type"], path + ".type")
        for key in ("pin_category", "type_path", "property_flags", "source"):
            _string(variable[key], path + "." + key)
        if variable["source"] != "blueprint_new_variables":
            raise SemanticJsonError(f"{path}.source: unsupported source")
        # J2-1のJSONには出典フィールドが存在しないため、旧形式は受け付ける。
        # J2-2以降の明示的な出典がある場合のみ、取得可否との整合性を強制する。
        available = _bool(variable["default_value_available"], path + ".default_value_available")
        if "default_value_source" in variable:
            default_source = _string(variable["default_value_source"], path + ".default_value_source")
            if default_source not in ("unavailable", "cdo"):
                raise SemanticJsonError(f"{path}.default_value_source: invalid source")
            if ("cdo" if available else "unavailable") != default_source:
                raise SemanticJsonError(f"{path}: inconsistent default value provenance")
        if available:
            _string(variable["default_value"], path + ".default_value")
        elif variable["default_value"] is not None:
            raise SemanticJsonError(f"{path}.default_value: expected null when unavailable")
    # Graph IR追跡領域は旧v2にはないため任意。リンク端点は実在GUIDと照合する。
    for gi, raw_graph in enumerate(_array(root.get("graph_traces", []), "graph_traces")):
        gp = f"graph_traces[{gi}]"
        graph = _object(raw_graph, gp)
        _required(graph, ("name", "path", "kind", "node_ids", "pin_ids",
                          "exec_edges", "data_edges"), gp)
        for key in ("name", "path", "kind"):
            _string(graph[key], gp + "." + key)
        node_ids = _array(graph["node_ids"], gp + ".node_ids")
        pin_ids = _array(graph["pin_ids"], gp + ".pin_ids")
        for key, ids in (("node_ids", node_ids), ("pin_ids", pin_ids)):
            if len(ids) != len(set(ids)):
                raise SemanticJsonError(f"{gp}.{key}: duplicate GUID")
            for index, guid in enumerate(ids):
                _guid(guid, f"{gp}.{key}[{index}]")
                if guid is None:
                    raise SemanticJsonError(f"{gp}.{key}: null GUID")
        for edge_kind in ("exec_edges", "data_edges"):
            for ei, raw_edge in enumerate(_array(graph[edge_kind], gp + "." + edge_kind)):
                ep = f"{gp}.{edge_kind}[{ei}]"
                edge = _object(raw_edge, ep)
                fields = ("source_node_id", "source_pin_id", "target_node_id", "target_pin_id")
                _required(edge, fields, ep)
                for field in fields:
                    _guid(edge[field], ep + "." + field)
                    if edge[field] is None:
                        raise SemanticJsonError(f"{ep}.{field}: null endpoint")
                if edge["source_node_id"] not in node_ids or edge["target_node_id"] not in node_ids:
                    raise SemanticJsonError(f"{ep}: unknown node endpoint")
                if edge["source_pin_id"] not in pin_ids or edge["target_pin_id"] not in pin_ids:
                    raise SemanticJsonError(f"{ep}: unknown pin endpoint")
    status = _enum(root["semantic_status"], STATUSES, "semantic_status")
    function_names: set[str] = set()
    for fi, function in enumerate(_array(root["functions"], "functions")):
        path = f"functions[{fi}]"
        f = _object(function, path)
        _required(f, ("name", "return_type", "blocks"), path)
        name = _string(f["name"], path + ".name")
        if name in function_names:
            raise SemanticJsonError(f"{path}: duplicate function {name}")
        function_names.add(name)
        _type(f["return_type"], path + ".return_type")
        # Additive v2 field: omission keeps legacy access defaults in older exports.
        if "visibility" in f:
            _enum(f["visibility"], ("unspecified", "public", "protected", "private"),
                  path + ".visibility")
        # 旧v2にはシグネチャがない。新形式のフィールドがある場合は一式を検証。
        if any(key in f for key in ("signature_source", "owner_class_path", "parameters")):
            _required(f, ("signature_source", "owner_class_path", "parameters"), path)
            if _string(f["signature_source"], path + ".signature_source") != "generated_class_reflection":
                raise SemanticJsonError(f"{path}: invalid signature source")
            _string(f["owner_class_path"], path + ".owner_class_path")
            names: set[str] = set()
            return_count = 0
            for pi, parameter in enumerate(_array(f["parameters"], path + ".parameters")):
                pp = f"{path}.parameters[{pi}]"
                p = _object(parameter, pp)
                _required(p, ("name", "type", "is_out", "is_return"), pp)
                pname = _string(p["name"], pp + ".name")
                if not pname or pname in names:
                    raise SemanticJsonError(f"{pp}: duplicate or empty parameter name")
                names.add(pname)
                _type(p["type"], pp + ".type")
                _bool(p["is_out"], pp + ".is_out")
                if _bool(p["is_return"], pp + ".is_return"):
                    return_count += 1
            if return_count > 1:
                raise SemanticJsonError(f"{path}: multiple return parameters")

        blocks = _array(f["blocks"], path + ".blocks")
        control_fields = ("block_id", "terminator", "successor_block_ids")
        uses_block_control = any(
            any(name in _object(block, f"{path}.blocks[{bi}]")
                for name in control_fields)
            for bi, block in enumerate(blocks)
        )
        block_ids: set[str] = set()
        successor_references: list[tuple[str, str]] = []
        for bi, block in enumerate(blocks):
            bp = f"{path}.blocks[{bi}]"
            b = _object(block, bp)
            _required(b, ("source_entry_node_id", "statements"), bp)
            _guid(b["source_entry_node_id"], bp + ".source_entry_node_id")
            if uses_block_control:
                _required(b, control_fields, bp)
                _guid(b["block_id"], bp + ".block_id")
                if len(blocks) > 1 and b["block_id"] is None:
                    raise SemanticJsonError(f"{bp}.block_id: required for multiple blocks")
                if b["block_id"] is not None:
                    block_id = b["block_id"].lower()
                    if block_id in block_ids:
                        raise SemanticJsonError(f"{bp}.block_id: duplicate block ID")
                    block_ids.add(block_id)
                terminator = _enum(
                    b["terminator"], BLOCK_TERMINATORS, bp + ".terminator")
                if len(blocks) > 1 and terminator == "unspecified":
                    raise SemanticJsonError(
                        f"{bp}.terminator: multiple blocks require an explicit terminator")
                for si, successor in enumerate(_array(
                        b["successor_block_ids"], bp + ".successor_block_ids")):
                    successor_path = f"{bp}.successor_block_ids[{si}]"
                    _guid(successor, successor_path)
                    if successor is None:
                        raise SemanticJsonError(f"{successor_path}: successor cannot be null")
                    successor_references.append((successor.lower(), successor_path))
            for si, statement in enumerate(_array(b["statements"], bp + ".statements")):
                _statement(statement, f"{bp}.statements[{si}]")
        for successor, successor_path in successor_references:
            if successor not in block_ids:
                raise SemanticJsonError(
                    f"{successor_path}: references an unknown block ID")
    has_error = False
    for i, diagnostic in enumerate(_array(root["diagnostics"], "diagnostics")):
        path = f"diagnostics[{i}]"
        d = _object(diagnostic, path)
        _required(d, ("severity", "code", "graph_name", "node_id", "pin_id", "message"), path)
        severity = _enum(d["severity"], SEVERITIES, path + ".severity")
        has_error = has_error or severity == "error"
        for key in ("code", "graph_name", "message"):
            _string(d[key], path + "." + key)
        _guid(d["node_id"], path + ".node_id")
        _guid(d["pin_id"], path + ".pin_id")
    if has_error and status == "complete":
        raise SemanticJsonError("semantic_status: complete cannot contain error diagnostics")
    return root
