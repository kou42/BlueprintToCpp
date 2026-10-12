"""Semantic宣言の欠落候補と生成不能時の安全な判定を検証する。"""
import json
from pathlib import Path

from app.output_comparison import normalize_artifact
from app.semantic_declaration_audit import audit_semantic_declarations

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"


def semantic():
    return json.loads(GOLDEN.read_text(encoding="utf-8"))


def artifact(origin, header, status="generated"):
    return normalize_artifact(origin, {
        "status": status,
        "files": [{"path": "Example.h", "content": header}] if status == "generated" else [],
        "diagnostics": [],
    })


def test_detects_missing_declarations_as_review_candidates():
    result = audit_semantic_declarations(
        semantic(), artifact("direct", "class AExample {\n int32 Health;\n void BeginPlay();\n};"))
    assert result["status"] == "review_required"
    assert result["missing"] == [{"scope": "variable", "name": "Title"}]


def test_detected_declarations_do_not_claim_semantic_equivalence():
    result = audit_semantic_declarations(
        semantic(), artifact("ai", "class AExample {\n int32 Health;\n FString Title;\n void BeginPlay();\n};"))
    assert result["status"] == "incomplete"
    assert result["missing"] == []
    assert result["diagnostics"]


def test_mock_skips_audit():
    result = audit_semantic_declarations(
        semantic(), artifact("ai", "", status="mocked"))
    assert result["status"] == "unavailable"


def test_compare_api_audits_both_sides():
    from app.compare_api import CompareRequest, compare_generated
    request = CompareRequest(
        semantic=semantic(),
        direct={"status": "generated", "files": [{"path": "Example.h",
                "content": "class AExample {\n int32 Health;\n};"}]},
        ai={"status": "mocked", "files": []},
    )
    result = compare_generated(request)
    assert result["semantic_declaration_audit"]["direct"]["status"] == "review_required"
    assert result["semantic_declaration_audit"]["ai"]["status"] == "unavailable"
