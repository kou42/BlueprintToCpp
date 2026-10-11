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


def test_event_trends_empty_and_validation(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    data = client.get("/review/statistics/event-trends").json()
    assert data["total_events"] == 0
    assert data["periods"] == []
    assert client.get("/review/statistics/event-trends?interval=year").status_code == 422
    assert client.get("/review/statistics/event-trends?start_date=invalid").status_code == 422
    assert client.get("/review/statistics/event-trends?start_date=2026-01-02&end_date=2026-01-01").status_code == 422


def test_event_trends_counts_each_submission(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    review = create_review()
    record_id = review["id"]
    fingerprint = review["result"]["review"]["revised_fingerprint"]
    day = datetime.now(timezone.utc).date().isoformat()

    for decision in ("rejected", "approved"):
        assert client.post(f"/review/records/{record_id}/decision", json={
            "reviewer": "tester", "decision": decision, "reason": "確認",
            "expected_revised_fingerprint": fingerprint,
        }).status_code == 201
    for outcome in ("failed", "passed"):
        assert client.post(f"/review/records/{record_id}/verification", json={
            "reviewer": "tester", "tool": "ubt", "outcome": outcome,
            "evidence_reference": "log", "summary": "外部検証",
            "expected_revised_fingerprint": fingerprint,
        }).status_code == 201
    assert client.post(f"/review/records/{record_id}/behavior", json={
        "reviewer": "tester", "environment": "UE", "evidence_reference": "test",
        "expected_revised_fingerprint": fingerprint,
        "cases": [
            {"case_id": "one", "description": "比較", "outcome": "matched",
             "blueprint_observation": "ok", "cpp_observation": "ok"},
            {"case_id": "two", "description": "未実施", "outcome": "not_run"},
        ],
    }).status_code == 201

    for interval in ("day", "week", "month"):
        response = client.get(f"/review/statistics/event-trends?interval={interval}&start_date={day}&end_date={day}")
        assert response.status_code == 200
        data = response.json()
        assert data["total_events"] == 5
        assert len(data["periods"]) == 1
        row = data["periods"][0]
        assert row["total_events"] == 5
        assert row["decisions"] == {"approved": 1, "rejected": 1}
        assert row["build"]["ubt"]["failed"] == 1
        assert row["build"]["ubt"]["passed"] == 1
        assert row["behavior"]["not_run"] == 1

    assert client.get("/review/statistics/event-trends?start_date=2000-01-01&end_date=2000-01-02").json()["total_events"] == 0
    # 作成期間ベースの統計は最新状態のみを示し、イベント件数とは異なる。
    assert client.get("/review/statistics").json()["decisions"]["approved"] == 1
