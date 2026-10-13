from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact
import json
from pathlib import Path

client = TestClient(app)


def generated(value):
    return {"status": "generated", "files": [{"path": "Example.cpp", "content": value}], "diagnostics": []}


def request():
    direct = generated("int Value = 1;\n")
    ai = generated("int Value = 2;\n")
    return {
        "original": {"direct": direct, "ai": ai},
        "revision": {
            "expected_direct_fingerprint": normalize_artifact("direct", direct).fingerprint,
            "expected_ai_fingerprint": normalize_artifact("ai", ai).fingerprint,
            "target": "ai",
            "revised": generated("int Value = 1;\n"),
            "note": "レビューによりAI側の初期値を修正",
        },
    }


def test_recompare_returns_before_and_after_without_approval():
    response = client.post("/review/recompare", json=request())
    assert response.status_code == 200
    body = response.json()
    assert body["before"]["status"] == "different"
    assert body["after"]["status"] == "identical"
    assert body["after"]["equivalent"] is None
    assert body["review"]["approved"] is False
    assert body["review"]["original_fingerprint"] != body["review"]["revised_fingerprint"]


def test_recompare_preserves_semantic_trace_and_fingerprint():
    payload = request()
    golden = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"
    payload["original"]["semantic"] = json.loads(golden.read_text(encoding="utf-8"))
    response = client.post("/review/recompare", json=payload)
    assert response.status_code == 200
    body = response.json()
    fingerprint = body["review"]["semantic_fingerprint"]
    assert len(fingerprint) == 64
    assert body["before"]["semantic_fingerprint"] == fingerprint
    assert body["after"]["semantic_fingerprint"] == fingerprint
    assert body["after"]["semantic_trace"]["source_asset_path"] == "/Game/BP_Roundtrip.BP_Roundtrip"


def test_rejects_stale_fingerprint():
    payload = request()
    payload["revision"]["expected_ai_fingerprint"] = "0" * 64
    response = client.post("/review/recompare", json=payload)
    assert response.status_code == 409


def test_rejects_unsafe_revised_path():
    payload = request()
    payload["revision"]["revised"]["files"][0]["path"] = "../Example.cpp"
    assert client.post("/review/recompare", json=payload).status_code == 422


def test_rejects_mock_revision():
    payload = request()
    payload["revision"]["revised"] = {"status": "mocked", "files": [], "diagnostics": []}
    assert client.post("/review/recompare", json=payload).status_code == 422


def test_direct_side_revision():
    payload = request()
    payload["revision"]["target"] = "direct"
    payload["revision"]["revised"] = generated("int Value = 2;\n")
    result = client.post("/review/recompare", json=payload)
    assert result.status_code == 200
    assert result.json()["after"]["status"] == "identical"


def test_rejects_empty_note_and_unknown_fields():
    payload = request()
    payload["revision"]["note"] = ""
    assert client.post("/review/recompare", json=payload).status_code == 422
    payload = request()
    payload["revision"]["unexpected"] = True
    assert client.post("/review/recompare", json=payload).status_code == 422
