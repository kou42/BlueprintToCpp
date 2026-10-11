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


def test_filter_by_reviewer_decision_and_verification(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    first = create_review("Alice")
    second = create_review("Bob")
    fingerprint = first["result"]["review"]["revised_fingerprint"]
    assert client.post(f"/review/records/{first['id']}/decision", json={
        "reviewer": "Alice", "decision": "approved", "reason": "確認",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201
    assert client.post(f"/review/records/{first['id']}/verification", json={
        "reviewer": "Alice", "tool": "uht", "outcome": "passed",
        "evidence_reference": "log", "summary": "成功",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201

    response = client.get("/review/records?reviewer=ali&decision=approved&uht=passed")
    assert response.status_code == 200
    assert response.json()["total"] == 1
    assert response.json()["items"][0]["id"] == first["id"]
    assert client.get("/review/records?decision=pending").json()["items"][0]["id"] == second["id"]
    assert client.get("/review/records?ubt=not_recorded").json()["total"] == 2
    assert client.get("/review/records?evidence_complete=true").json()["total"] == 0
    assert client.get("/review/records?target=ai").json()["total"] == 2


def test_filter_before_pagination(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    create_review("Alice")
    create_review("Bob")
    create_review("Alice")
    response = client.get("/review/records?reviewer=Alice&limit=1&offset=1")
    assert response.status_code == 200
    assert response.json()["total"] == 2
    assert len(response.json()["items"]) == 1


def test_filter_validation(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    assert client.get("/review/records?decision=unknown").status_code == 422
    assert client.get("/review/records?uht=matched").status_code == 422
    assert client.get("/review/records?evidence_complete=maybe").status_code == 422
    assert client.get("/review/records?reviewer=").status_code == 422
    assert client.get("/review/records?limit=0").status_code == 422
