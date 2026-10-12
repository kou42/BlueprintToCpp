"""Semantic JSON v2の厳密な構造検証。Snapshot v1とは独立して扱う。"""
from __future__ import annotations

from typing import Any
from uuid import UUID


class SemanticJsonError(ValueError):
    """Semantic JSONの契約違反。"""


TYPE_KINDS = frozenset(("unresolved", "void", "bool", "int32", "int64",
                        "float", "double", "string", "name", "text", "object", "struct"))
EXPRESSION_KINDS = frozenset(("literal", "variable_reference", "function_call"))
STATEMENT_KINDS = frozenset(("expression", "assignment", "return"))
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


def _type(raw: Any, path: str) -> None:
    obj = _object(raw, path)
    _required(obj, ("kind", "type_path", "is_reference", "is_const"), path)
    _enum(obj["kind"], TYPE_KINDS, path + ".kind")
    _string(obj["type_path"], path + ".type_path")
    _bool(obj["is_reference"], path + ".is_reference")
    _bool(obj["is_const"], path + ".is_const")


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
        for bi, block in enumerate(_array(f["blocks"], path + ".blocks")):
            bp = f"{path}.blocks[{bi}]"
            b = _object(block, bp)
            _required(b, ("source_entry_node_id", "statements"), bp)
            _guid(b["source_entry_node_id"], bp + ".source_entry_node_id")
            for si, statement in enumerate(_array(b["statements"], bp + ".statements")):
                sp = f"{bp}.statements[{si}]"
                s = _object(statement, sp)
                _required(s, ("kind", "source_node_id", "assignment_target", "expression"), sp)
                _enum(s["kind"], STATEMENT_KINDS, sp + ".kind")
                _guid(s["source_node_id"], sp + ".source_node_id")
                _nullable_string(s["assignment_target"], sp + ".assignment_target")
                _expression(s["expression"], sp + ".expression")
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
