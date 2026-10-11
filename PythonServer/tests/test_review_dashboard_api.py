from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact

client = TestClient(app)


def create_review(reviewer):
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


def test_dashboard_empty_and_validation(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    data = client.get("/review/dashboard").json()
    assert data["total_reviews"] == 0
    assert data["attention_items"] == []
    assert data["needs_attention"] == 0
    assert data["decision_counts"] == {"pending": 0, "approved": 0, "rejected": 0}
    assert all(value == 0 for value in data["blocker_counts"].values())
    assert data["verified_by_server"] is False
    assert client.get("/review/dashboard?limit=0").status_code == 422
    assert client.get("/review/dashboard?limit=101").status_code == 422


def test_dashboard_attention_and_latest_evidence(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    first = create_review("Alice")
    create_review("Bob")
    fingerprint = first["result"]["review"]["revised_fingerprint"]
    record_id = first["id"]
    for tool in ("uht", "ubt"):
        assert client.post(f"/review/records/{record_id}/verification", json={
            "reviewer": "Alice", "tool": tool, "outcome": "passed",
            "evidence_reference": "log", "summary": "外部ビルド",
            "expected_revised_fingerprint": fingerprint,
        }).status_code == 201
    assert client.post(f"/review/records/{record_id}/behavior", json={
        "reviewer": "Alice", "environment": "UE", "evidence_reference": "test",
        "expected_revised_fingerprint": fingerprint,
        "cases": [{"case_id": "one", "description": "比較", "outcome": "matched",
                   "blueprint_observation": "ok", "cpp_observation": "ok"}],
    }).status_code == 201
    assert client.post(f"/review/records/{record_id}/decision", json={
        "reviewer": "Alice", "decision": "approved", "reason": "確認",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201

    data = client.get("/review/dashboard?limit=1").json()
    assert data["total_reviews"] == 2
    assert data["pending_decisions"] == 1
    assert data["evidence_complete"] == 1
    assert data["needs_attention"] == 1
    assert data["decision_counts"] == {"pending": 1, "approved": 1, "rejected": 0}
    assert all(value == 1 for value in data["blocker_counts"].values())
    assert data["attention_total"] == 1
    assert len(data["attention_items"]) == 1
    assert data["attention_items"][0]["reviewer"] == "Bob"
    assert "input" not in data["attention_items"][0]

    # 最新の失敗が記録された場合は要確認対象へ戻る。
    assert client.post(f"/review/records/{record_id}/verification", json={
        "reviewer": "Alice", "tool": "ubt", "outcome": "failed",
        "evidence_reference": "new-log", "summary": "再ビルド失敗",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201
    updated = client.get("/review/dashboard?limit=1").json()
    assert updated["evidence_complete"] == 0
    assert updated["needs_attention"] == 2
    assert updated["blocker_counts"]["ubt_passed"] == 2
    assert updated["attention_total"] == 2
    assert len(updated["attention_items"]) == 1
