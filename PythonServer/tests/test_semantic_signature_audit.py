"""反射シグネチャの型照合と未対応構文の安全な保留。"""
import json
from pathlib import Path

from app.output_comparison import normalize_artifact
from app.semantic_signature_audit import audit_semantic_signatures

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"


def semantic():
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    data["functions"][0]["name"] = "Compute"
    data["functions"][0]["return_type"]["kind"] = "bool"
    data["functions"][0]["parameters"] = [{
        "name": "Amount",
        "type": {"kind": "int32", "type_path": "", "is_reference": True, "is_const": True},
        "is_out": False, "is_return": False,
    }, {
        "name": "ReturnValue",
        "type": {"kind": "bool", "type_path": "", "is_reference": False, "is_const": False},
        "is_out": True, "is_return": True,
    }]
    return data


def artifact(header, status="generated"):
    return normalize_artifact("direct", {"status": status,
        "files": [{"path": "Example.h", "content": header}] if status == "generated" else []})


def test_reflected_signature_matches_without_claiming_equivalence():
    result = audit_semantic_signatures(semantic(), artifact(
        "class AExample {\n bool Compute(const int32& Amount);\n};"))
    assert result["status"] == "incomplete"
    assert result["differences"] == []


def test_return_and_parameter_type_differences():
    result = audit_semantic_signatures(semantic(), artifact(
        "class AExample {\n int32 Compute(float Amount);\n};"))
    assert result["status"] == "review_required"
    assert {d["field"] for d in result["differences"]} == {
        "return_type", "parameter[0].type"}


def test_unsupported_type_is_not_inferred():
    data = semantic()
    data["functions"][0]["parameters"][0]["type"]["kind"] = "unresolved"
    result = audit_semantic_signatures(data, artifact(
        "class AExample {\n bool Compute(int32 Amount);\n};"))
    assert result["status"] == "incomplete"
    assert any("unsupported argument" in d for d in result["diagnostics"])


def test_known_unreal_struct_and_object_paths_are_compared():
    data = semantic()
    data["functions"][0]["parameters"] = [{
        "name": "Location",
        "type": {"kind": "struct", "type_path": "/Script/CoreUObject.Vector",
                 "is_reference": True, "is_const": True},
        "is_out": False, "is_return": False,
    }, {
        "name": "Target",
        "type": {"kind": "object", "type_path": "/Script/Engine.Actor",
                 "is_reference": False, "is_const": False},
        "is_out": False, "is_return": False,
    }]
    result = audit_semantic_signatures(data, artifact(
        "class AExample {\n bool Compute(const FVector& Location, AActor* Target);\n};"))
    assert result["status"] == "incomplete"
    assert result["differences"] == []


def test_unknown_unreal_type_path_remains_unverified():
    data = semantic()
    data["functions"][0]["parameters"][0]["type"] = {
        "kind": "object", "type_path": "/Script/Example.CustomObject",
        "is_reference": False, "is_const": False,
    }
    result = audit_semantic_signatures(data, artifact(
        "class AExample {\n bool Compute(UCustomObject* Amount);\n};"))
    assert result["status"] == "incomplete"
    assert result["differences"] == []
    assert any("unsupported argument" in d for d in result["diagnostics"])


def test_mock_generation_is_unavailable():
    assert audit_semantic_signatures(semantic(), artifact("", "mocked"))["status"] == "unavailable"


def test_compare_api_returns_per_origin_signature_audit():
    from app.compare_api import CompareRequest, compare_generated
    request = CompareRequest(
        semantic=semantic(),
        direct={"status": "generated", "files": [{"path": "Example.h",
            "content": "class AExample {\n bool Compute(const int32& Amount);\n};"}]},
        ai={"status": "mocked", "files": []},
    )
    result = compare_generated(request)["semantic_signature_audit"]
    assert result["direct"]["status"] == "incomplete"
    assert result["ai"]["status"] == "unavailable"
