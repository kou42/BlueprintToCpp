from fastapi.testclient import TestClient

from app.main import app

client = TestClient(app)


def payload(direct_content="int x = 1;\n", ai_content="int x = 2;\n"):
    return {
        "direct": {"status": "generated", "files": [{"path": "Example.cpp", "content": direct_content}]},
        "ai": {"status": "generated", "files": [{"path": "Example.cpp", "content": ai_content}]},
    }


def test_compare_api_reports_diff():
    response = client.post("/compare", json=payload())
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "different"
    assert data["equivalent"] is None
    assert data["files"][0]["kind"] == "modified"
    assert len(data["direct_fingerprint"]) == 64


def test_compare_api_identical_not_approved():
    response = client.post("/compare", json=payload(ai_content="int x = 1;\n"))
    assert response.status_code == 200
    assert response.json()["status"] == "identical"
    assert response.json()["equivalent"] is None


def test_compare_api_mock_unavailable():
    request = payload()
    request["ai"] = {"status": "mocked", "files": [], "diagnostics": ["Mock only"]}
    response = client.post("/compare", json=request)
    assert response.status_code == 200
    assert response.json()["status"] == "unavailable"


def test_compare_api_rejects_unsafe_path():
    request = payload()
    request["ai"]["files"][0]["path"] = "../Example.cpp"
    response = client.post("/compare", json=request)
    assert response.status_code == 422


def test_compare_api_rejects_extra_input_fields():
    request = payload()
    request["ai"]["secret"] = "unexpected"
    response = client.post("/compare", json=request)
    assert response.status_code == 422


def test_compare_api_includes_header_structure_diff():
    request = {
        "direct": {"status": "generated", "files": [{"path": "Example.h", "content": "class AExample {\n void BeginPlay();\n};"}]},
        "ai": {"status": "generated", "files": [{"path": "Example.h", "content": "class AExample {\n void Tick(float Delta);\n};"}]},
    }
    response = client.post("/compare", json=request)
    assert response.status_code == 200
    assert response.json()["structure"]["status"] == "different"


def test_compare_with_semantic_trace_keeps_existing_diff():
    import json
    from pathlib import Path
    golden = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"
    request = payload()
    request["semantic"] = json.loads(golden.read_text(encoding="utf-8"))
    response = client.post("/compare", json=request)
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "different"
    assert data["equivalent"] is None
    assert data["semantic_trace"]["source_asset_path"] == "/Game/BP_Roundtrip.BP_Roundtrip"
    assert data["semantic_trace"]["exec_edge_count"] == 1
    assert data["semantic_trace"]["unresolved_types"] == []
    assert data["semantic_fingerprint"] == data["semantic_trace"]["semantic_fingerprint"]


def test_compare_legacy_request_has_no_semantic_trace():
    response = client.post("/compare", json=payload())
    assert response.status_code == 200
    assert response.json()["semantic_trace"] is None
    assert response.json()["semantic_fingerprint"] is None


def test_compare_rejects_invalid_semantic_trace():
    request = payload()
    request["semantic"] = {"schema_version": 1}
    response = client.post("/compare", json=request)
    assert response.status_code == 422


def test_compare_mock_preserves_trace_without_claiming_equivalence():
    import json
    from pathlib import Path
    golden = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"
    request = payload()
    request["ai"] = {"status": "mocked", "files": [], "diagnostics": ["Mock only"]}
    request["semantic"] = json.loads(golden.read_text(encoding="utf-8"))
    response = client.post("/compare", json=request)
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "unavailable"
    assert data["equivalent"] is None
    assert data["semantic_trace"]["graph_count"] == 1


def test_compare_semantic_trace_optional_and_consistent():
    import json
    from pathlib import Path

    golden = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"
    request = payload()
    # 旧リクエストでは追跡情報が存在しなくても比較可能。
    old_response = client.post("/compare", json=request)
    assert old_response.status_code == 200
    assert old_response.json()["semantic_trace"] is None

    request["semantic"] = json.loads(golden.read_text(encoding="utf-8"))
    response = client.post("/compare", json=request)
    assert response.status_code == 200
    trace = response.json()["semantic_trace"]
    assert trace["source_asset_path"] == "/Game/BP_Roundtrip.BP_Roundtrip"
    assert trace["variable_count"] == 2
    assert trace["exec_edge_count"] == 1
    # 比較結果の判定はSemantic情報を付加しても変わらない。
    assert response.json()["status"] == old_response.json()["status"]
    assert response.json()["equivalent"] is None


def test_compare_rejects_invalid_semantic_trace():
    request = payload()
    request["semantic"] = {"schema_version": 1}
    response = client.post("/compare", json=request)
    assert response.status_code == 422
