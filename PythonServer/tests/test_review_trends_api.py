from datetime import datetime, timezone

from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact

client = TestClient(app)


def create_review():
    direct = {"status": "generated", "files": [{"path": "A.cpp", "content": "int X = 1;"}]}
    ai = {"status": "generated", "files": [{"path": "A.cpp", "content": "int X = 2;"}]}
    response = client.post("/review/records", json={
        "reviewer": "tester",
        "request": {"original": {"direct": direct, "ai": ai}, "revision": {
            "target": "ai", "revised": direct, "note": "修正済み",
            "expected_direct_fingerprint": normalize_artifact("direct", direct).fingerprint,
            "expected_ai_fingerprint": normalize_artifact("ai", ai).fingerprint,
        }},
    })
    assert response.status_code == 201
    return response.json()


def test_empty_and_invalid_ranges(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    data = client.get("/review/statistics/trends?interval=week").json()
    assert data["total_reviews"] == 0
    assert data["periods"] == []
    assert client.get("/review/statistics/trends?interval=year").status_code == 422
    assert client.get("/review/statistics/trends?start_date=2026-13-01").status_code == 422
    assert client.get("/review/statistics/trends?start_date=2026-01-02&end_date=2026-01-01").status_code == 422


def test_review_creation_period_and_latest_status(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    fingerprint = record["result"]["review"]["revised_fingerprint"]
    day = datetime.fromisoformat(record["created_at"]).astimezone(timezone.utc).date().isoformat()
    record_id = record["id"]

    assert client.post(f"/review/records/{record_id}/verification", json={
        "reviewer": "tester", "tool": "ubt", "outcome": "failed",
        "evidence_reference": "log", "summary": "外部失敗",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201

    for interval in ("day", "week", "month"):
        data = client.get(f"/review/statistics/trends?interval={interval}&start_date={day}&end_date={day}").json()
        assert data["total_reviews"] == 1
        assert len(data["periods"]) == 1
        row = data["periods"][0]
        assert row["created"] == 1
        assert row["decisions"]["pending"] == 1
        assert row["build_failed"]["ubt"] == 1
        assert row["evidence_incomplete"] == 1
        assert data["verified_by_server"] is False

    assert client.post(f"/review/records/{record_id}/verification", json={
        "reviewer": "tester", "tool": "ubt", "outcome": "passed",
        "evidence_reference": "new-log", "summary": "外部成功",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201
    data = client.get(f"/review/statistics/trends?start_date={day}&end_date={day}").json()
    assert data["periods"][0]["build_failed"]["ubt"] == 0
    assert client.get("/review/statistics/trends?start_date=2000-01-01&end_date=2000-01-02").json()["total_reviews"] == 0
