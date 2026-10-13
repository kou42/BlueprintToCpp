"""Semantic JSON v2の出典・未解決情報を生成結果に添付する読み取り専用要約。"""
from __future__ import annotations

import hashlib
import json

from .semantic_json import SemanticJsonError, parse_semantic_json


def _fingerprint_parsed_semantic(parsed: dict) -> str:
    """JSONのキー順や空白に依存しない入力追跡用SHA-256を返す。"""
    try:
        canonical = json.dumps(
            parsed, ensure_ascii=False, sort_keys=True,
            separators=(",", ":"), allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise SemanticJsonError(f"semantic: cannot create canonical fingerprint: {exc}") from exc
    return hashlib.sha256(canonical).hexdigest()


def fingerprint_semantic(semantic: dict) -> str:
    """契約検証済みSemantic JSON全体を決定的に識別する。"""
    return _fingerprint_parsed_semantic(parse_semantic_json(semantic))


def summarize_generation_trace(semantic: dict) -> dict:
    """推測による型補完は行わず、入力に実在する情報だけを集計する。"""
    parsed = parse_semantic_json(semantic)
    unresolved: list[dict[str, str]] = []
    for variable in parsed.get("variables", []):
        if variable["type"]["kind"] == "unresolved":
            unresolved.append({"scope": "variable", "name": variable["name"]})
    for function in parsed["functions"]:
        if function["return_type"]["kind"] == "unresolved":
            unresolved.append({"scope": "return", "name": function["name"]})
        for parameter in function.get("parameters", []):
            if parameter["type"]["kind"] == "unresolved":
                unresolved.append({
                    "scope": "parameter",
                    "name": function["name"] + "." + parameter["name"],
                })
    traces = parsed.get("graph_traces", [])
    return {
        "semantic_fingerprint": _fingerprint_parsed_semantic(parsed),
        "source_asset_path": parsed["source"]["asset_path"],
        "semantic_status": parsed["semantic_status"],
        "variable_count": len(parsed.get("variables", [])),
        "function_count": len(parsed["functions"]),
        "graph_count": len(traces),
        "node_count": sum(len(g["node_ids"]) for g in traces),
        "pin_count": sum(len(g["pin_ids"]) for g in traces),
        "exec_edge_count": sum(len(g["exec_edges"]) for g in traces),
        "data_edge_count": sum(len(g["data_edges"]) for g in traces),
        "unresolved_types": unresolved,
        "diagnostics": [
            {"severity": d["severity"], "code": d["code"],
             "graph_name": d["graph_name"], "node_id": d["node_id"],
             "pin_id": d["pin_id"], "message": d["message"]}
            for d in parsed["diagnostics"]
        ],
    }
