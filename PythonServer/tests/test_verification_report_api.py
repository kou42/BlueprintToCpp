import json
from pathlib import Path

from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact
from app.verification_report_api import _latest

client = TestClient(app)


def test_latest_prefers_server_save_order_when_wall_clock_matches():
    created_at = "2026-10-13T00:00:00+00:00"
    events = [
        {"id": "f" * 32, "created_at": created_at, "recorded_at_ns": 100,
         "decision": "rejected"},
        {"id": "0" * 32, "created_at": created_at, "recorded_at_ns": 101,
         "decision": "approved"},
    ]
    assert _latest(events)["decision"] == "approved"


def create_review(semantic=None):
    direct = {"status": "generated", "files": [{"path": "Example.cpp", "content": "int X = 1;"}]}
    ai = {"status": "generated", "files": [{"path": "Example.cpp", "content": "int X = 2;"}]}
    original = {"direct": direct, "ai": ai}
    if semantic is not None:
        original["semantic"] = semantic
    response = client.post("/review/records", json={
        "reviewer": "tester",
        "request": {
            "original": original,
            "revision": {
                "target": "ai", "revised": direct, "note": "修正済み",
                "expected_direct_fingerprint": normalize_artifact("direct", direct).fingerprint,
                "expected_ai_fingerprint": normalize_artifact("ai", ai).fingerprint,
            },
        },
    })
    assert response.status_code == 201
    return response.json()


def test_report_missing_evidence_is_not_complete(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    response = client.get(f"/review/records/{record['id']}/report")
    assert response.status_code == 200
    data = response.json()
    assert data["review"]["decision"] == "pending"
    assert data["build"]["uht"]["status"] == "not_recorded"
    assert data["behavior"]["status"] == "not_recorded"
    assert data["evidence_complete"] is False
    assert len(data["blockers"]) == 4
    assert data["verified_by_server"] is False


def test_report_aggregates_all_checks(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    record_id = record["id"]
    fingerprint = record["result"]["review"]["revised_fingerprint"]
    for tool in ("uht", "ubt"):
        response = client.post(f"/review/records/{record_id}/verification", json={
            "reviewer": "tester", "tool": tool, "outcome": "passed",
            "evidence_reference": "log", "summary": "外部ビルド",
            "expected_revised_fingerprint": fingerprint,
        })
        assert response.status_code == 201
    response = client.post(f"/review/records/{record_id}/behavior", json={
        "reviewer": "tester", "environment": "UE local",
        "evidence_reference": "behavior-log",
        "expected_revised_fingerprint": fingerprint,
        "cases": [{"case_id": "one", "description": "動作確認", "outcome": "matched",
                   "blueprint_observation": "ok", "cpp_observation": "ok"}],
    })
    assert response.status_code == 201
    response = client.post(f"/review/records/{record_id}/decision", json={
        "reviewer": "tester", "decision": "approved", "reason": "確認",
        "expected_revised_fingerprint": fingerprint,
    })
    assert response.status_code == 201

    data = client.get(f"/review/records/{record_id}/report").json()
    assert data["evidence_complete"] is True
    assert data["blockers"] == []
    assert data["behavior"]["counts"]["matched"] == 1
    assert data["verified_by_server"] is False

    # 後から失敗が申告された場合は最新結果を優先し、完了判定を取り消す。
    assert client.post(f"/review/records/{record_id}/verification", json={
        "reviewer": "tester", "tool": "ubt", "outcome": "failed",
        "evidence_reference": "new-log", "summary": "再ビルド失敗",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201
    updated = client.get(f"/review/records/{record_id}/report").json()
    assert updated["evidence_complete"] is False
    assert "ubt_passed" in updated["blockers"]


def test_report_missing_review(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    assert client.get("/review/records/" + "0" * 32 + "/report").status_code == 404


def test_semantic_fingerprint_is_linked_to_all_evidence(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    golden = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"
    record = create_review(json.loads(golden.read_text(encoding="utf-8")))
    record_id = record["id"]
    artifact_fingerprint = record["result"]["review"]["revised_fingerprint"]
    semantic_fingerprint = record["result"]["review"]["semantic_fingerprint"]

    verification = client.post(f"/review/records/{record_id}/verification", json={
        "reviewer": "tester", "tool": "uht", "outcome": "passed",
        "evidence_reference": "log", "summary": "external",
        "expected_revised_fingerprint": artifact_fingerprint,
    }).json()
    behavior = client.post(f"/review/records/{record_id}/behavior", json={
        "reviewer": "tester", "environment": "UE", "evidence_reference": "log",
        "expected_revised_fingerprint": artifact_fingerprint,
        "cases": [{"case_id": "one", "description": "compare", "outcome": "matched",
                   "blueprint_observation": "ok", "cpp_observation": "ok"}],
    }).json()
    decision = client.post(f"/review/records/{record_id}/decision", json={
        "reviewer": "tester", "decision": "approved", "reason": "reviewed",
        "expected_revised_fingerprint": artifact_fingerprint,
    }).json()

    assert {verification["semantic_fingerprint"], behavior["semantic_fingerprint"],
            decision["semantic_fingerprint"]} == {semantic_fingerprint}
    report = client.get(f"/review/records/{record_id}/report").json()
    assert report["semantic_fingerprint"] == semantic_fingerprint
