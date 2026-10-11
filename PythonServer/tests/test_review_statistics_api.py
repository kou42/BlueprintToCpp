from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact

client = TestClient(app)


def create_review(reviewer="tester"):
    direct = {"status": "generated", "files": [{"path": "A.cpp", "content": "int X = 1;"}]}
    ai = {"status": "generated", "files": [{"path": "A.cpp", "content": "int X = 2;"}]}
    response = client.post("/review/records", json={
        "reviewer": reviewer,
        "request": {"original": {"direct": direct, "ai": ai}, "revision": {
            "target": "ai", "revised": direct, "note": "修正済み",
            "expected_direct_fingerprint": normalize_artifact("direct", direct).fingerprint,
            "expected_ai_fingerprint": normalize_artifact("ai", ai).fingerprint,
        }},
    })
    assert response.status_code == 201
    return response.json()


def test_empty_statistics(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    response = client.get("/review/statistics")
    assert response.status_code == 200
    data = response.json()
    assert data["total_reviews"] == 0
    assert data["evidence_complete"] == 0
    assert data["decisions"] == {"pending": 0, "approved": 0, "rejected": 0}
    assert data["verified_by_server"] is False


def test_statistics_count_latest_state_per_review(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    first = create_review()
    create_review()
    record_id = first["id"]
    fingerprint = first["result"]["review"]["revised_fingerprint"]

    for decision in ("rejected", "approved"):
        assert client.post(f"/review/records/{record_id}/decision", json={
            "reviewer": "tester", "decision": decision, "reason": "確認",
            "expected_revised_fingerprint": fingerprint,
        }).status_code == 201
    for tool in ("uht", "ubt"):
        assert client.post(f"/review/records/{record_id}/verification", json={
            "reviewer": "tester", "tool": tool, "outcome": "passed",
            "evidence_reference": "log", "summary": "外部検証",
            "expected_revised_fingerprint": fingerprint,
        }).status_code == 201
    assert client.post(f"/review/records/{record_id}/behavior", json={
        "reviewer": "tester", "environment": "UE", "evidence_reference": "test",
        "expected_revised_fingerprint": fingerprint,
        "cases": [{"case_id": "one", "description": "比較", "outcome": "matched",
                   "blueprint_observation": "ok", "cpp_observation": "ok"}],
    }).status_code == 201

    data = client.get("/review/statistics").json()
    assert data["total_reviews"] == 2
    assert data["decisions"] == {"pending": 1, "approved": 1, "rejected": 0}
    assert data["targets"]["ai"] == 2
    assert data["build"]["uht"]["passed"] == 1
    assert data["build"]["uht"]["not_recorded"] == 1
    assert data["behavior"]["matched"] == 1
    assert data["evidence_complete"] == 1
    assert data["evidence_incomplete"] == 1
    assert all(count == 1 for count in data["blockers"].values())

    # 最新の失敗イベントが統計にも反映されることを確認する。
    assert client.post(f"/review/records/{record_id}/verification", json={
        "reviewer": "tester", "tool": "ubt", "outcome": "failed",
        "evidence_reference": "new-log", "summary": "再検証失敗",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201
    updated = client.get("/review/statistics").json()
    assert updated["evidence_complete"] == 0
    assert updated["build"]["ubt"]["failed"] == 1
    assert updated["blockers"]["ubt_passed"] == 2
