from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact
from app.verification_report_api import index_verification_events, summarize_verification

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


def test_index_matches_unindexed_report_and_isolates_fingerprints(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    first = create_review()
    second = create_review()
    fingerprint = first["result"]["review"]["revised_fingerprint"]
    assert client.post(f"/review/records/{first['id']}/verification", json={
        "reviewer": "tester", "tool": "ubt", "outcome": "passed",
        "evidence_reference": "log", "summary": "外部成功",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201
    from app.review_store import list_records
    records = list_records()
    index = index_verification_events(records)
    for review in (first, second):
        assert summarize_verification(review, records, index) == summarize_verification(review, records)
    assert client.get("/review/dashboard").json()["total_reviews"] == 2
    assert client.get("/review/statistics").json()["build"]["ubt"]["passed"] == 1
    assert client.get("/review/records?ubt=passed").json()["total"] == 1
