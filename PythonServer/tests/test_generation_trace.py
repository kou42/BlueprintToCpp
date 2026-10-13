"""Semantic出典がAI生成応答まで保持されることを検証する。"""
import copy
import json
from pathlib import Path

import pytest

from app.generation_trace import fingerprint_semantic, summarize_generation_trace
from app.semantic_json import SemanticJsonError

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"


def golden():
    return json.loads(GOLDEN.read_text(encoding="utf-8"))


def test_trace_preserves_graph_and_default_metadata():
    data = golden()
    trace = summarize_generation_trace(data)
    assert trace["source_asset_path"] == "/Game/BP_Roundtrip.BP_Roundtrip"
    assert trace["variable_count"] == 2
    assert trace["graph_count"] == 1
    assert trace["node_count"] == 2
    assert trace["pin_count"] == 2
    assert trace["exec_edge_count"] == 1
    assert trace["data_edge_count"] == 0
    assert trace["unresolved_types"] == []
    assert data["variables"][1]["default_value"] == ""
    assert len(trace["semantic_fingerprint"]) == 64


def test_semantic_fingerprint_is_canonical_and_content_sensitive():
    data = golden()
    reordered = {key: data[key] for key in reversed(data)}
    assert fingerprint_semantic(reordered) == fingerprint_semantic(data)

    changed = copy.deepcopy(data)
    changed["source"]["asset_path"] = "/Game/BP_Other.BP_Other"
    assert fingerprint_semantic(changed) != fingerprint_semantic(data)


def test_unresolved_type_is_reported_not_inferred():
    data = golden()
    data["variables"][0]["type"]["kind"] = "unresolved"
    data["functions"][0]["return_type"]["kind"] = "unresolved"
    data["functions"][0]["parameters"] = [{
        "name": "Value", "type": {"kind": "unresolved", "type_path": "",
            "is_reference": False, "is_const": False},
        "is_out": False, "is_return": False,
    }]
    trace = summarize_generation_trace(data)
    assert trace["unresolved_types"] == [
        {"scope": "variable", "name": "Health"},
        {"scope": "return", "name": "BeginPlay"},
        {"scope": "parameter", "name": "BeginPlay.Value"},
    ]
    assert data["variables"][0]["type"]["kind"] == "unresolved"


def test_trace_rejects_invalid_semantic():
    data = golden()
    data["graph_traces"][0]["exec_edges"][0]["target_node_id"] = None
    with pytest.raises(SemanticJsonError):
        summarize_generation_trace(data)


def test_ai_api_includes_trace_without_claiming_generation():
    from app.ai_generate_api import AIGenerateRequest, ai_generate
    response = ai_generate(AIGenerateRequest(semantic=golden(), provider="mock"))
    assert response["status"] == "mocked"
    assert response["files"] == []
    assert response["semantic_trace"]["graph_count"] == 1
    assert response["semantic_trace"]["exec_edge_count"] == 1
