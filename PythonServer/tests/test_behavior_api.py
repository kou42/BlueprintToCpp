from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact

client = TestClient(app)


def create_review():
    direct = {"status": "generated", "files": [{"path": "Example.cpp", "content": "int X = 1;"}]}
    ai = {"status": "generated", "files": [{"path": "Example.cpp", "content": "int X = 2;"}]}
    response = client.post("/review/records", json={
        "reviewer": "tester",
        "request": {
            "original": {"direct": direct, "ai": ai},
            "revision": {
                "target": "ai", "revised": direct, "note": "修正済み",
                "expected_direct_fingerprint": normalize_artifact("direct", direct).fingerprint,
                "expected_ai_fingerprint": normalize_artifact("ai", ai).fingerprint,
            },
        },
    })
    assert response.status_code == 201
    return response.json()


def evidence(record, outcome="matched"):
    return {
        "reviewer": "tester",
        "expected_revised_fingerprint": record["result"]["review"]["revised_fingerprint"],
        "environment": "Unreal Editor local test",
        "evidence_reference": "test-report-001",
        "cases": [{
            "case_id": "jump",
            "description": "ジャンプ操作",
            "outcome": outcome,
            "blueprint_observation": "Jumped",
            "cpp_observation": "Jumped",
        }],
    }


def test_behavior_events_and_latest_status(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    url = f"/review/records/{record['id']}/behavior"
    assert client.get(url).json()["status"] == "not_recorded"
    assert client.post(url, json=evidence(record)).status_code == 201
    assert client.get(url).json()["status"] == "matched"
    assert client.post(url, json=evidence(record, "mismatched")).status_code == 201
    result = client.get(url).json()
    assert result["status"] == "mismatched"
    assert result["counts"]["mismatched"] == 1
    assert len(result["events"]) == 2
    assert result["verified_by_server"] is False
    assert client.get(f"/review/records/{record['id']}").json()["decision"] == "pending"


def test_not_run_is_distinct(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    payload = evidence(record, "not_run")
    payload["cases"][0]["blueprint_observation"] = None
    payload["cases"][0]["cpp_observation"] = None
    url = f"/review/records/{record['id']}/behavior"
    assert client.post(url, json=payload).status_code == 201
    assert client.get(url).json()["status"] == "not_run"


def test_reject_stale_fingerprint_and_duplicate_case(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    url = f"/review/records/{record['id']}/behavior"
    payload = evidence(record)
    payload["expected_revised_fingerprint"] = "0" * 64
    assert client.post(url, json=payload).status_code == 409
    payload = evidence(record)
    payload["cases"].append(payload["cases"][0])
    assert client.post(url, json=payload).status_code == 422
    assert client.get(url).json()["events"] == []


def test_executed_case_requires_observations(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    payload = evidence(record)
    payload["cases"][0]["cpp_observation"] = None
    assert client.post(f"/review/records/{record['id']}/behavior", json=payload).status_code == 422


def test_missing_review(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    assert client.get("/review/records/" + "0" * 32 + "/behavior").status_code == 404
