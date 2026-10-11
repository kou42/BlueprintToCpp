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
                "target": "ai",
                "revised": direct,
                "note": "修正済み",
                "expected_direct_fingerprint": normalize_artifact("direct", direct).fingerprint,
                "expected_ai_fingerprint": normalize_artifact("ai", ai).fingerprint,
            },
        },
    })
    assert response.status_code == 201
    return response.json()


def evidence(record, tool="uht", outcome="passed"):
    return {
        "reviewer": "tester",
        "tool": tool,
        "outcome": outcome,
        "evidence_reference": "local-build-log-001",
        "summary": "外部環境で実施した結果",
        "expected_revised_fingerprint": record["result"]["review"]["revised_fingerprint"],
    }


def test_record_and_query_external_evidence(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    url = f"/review/records/{record['id']}/verification"
    before = client.get(url)
    assert before.status_code == 200
    assert before.json()["latest"] == {"uht": "not_recorded", "ubt": "not_recorded"}
    assert before.json()["verified_by_server"] is False

    assert client.post(url, json=evidence(record)).status_code == 201
    assert client.post(url, json=evidence(record, "ubt", "failed")).status_code == 201
    assert client.post(url, json=evidence(record, "uht", "failed")).status_code == 201
    result = client.get(url).json()
    assert result["latest"] == {"uht": "failed", "ubt": "failed"}
    assert len(result["events"]) == 3
    assert client.get(f"/review/records/{record['id']}").json()["decision"] == "pending"


def test_reject_stale_fingerprint_and_invalid_outcome(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    url = f"/review/records/{record['id']}/verification"
    invalid = evidence(record)
    invalid["expected_revised_fingerprint"] = "0" * 64
    assert client.post(url, json=invalid).status_code == 409
    invalid = evidence(record)
    invalid["outcome"] = "success"
    assert client.post(url, json=invalid).status_code == 422
    assert client.get(url).json()["events"] == []


def test_missing_review_returns_404(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    url = "/review/records/" + "0" * 32 + "/verification"
    assert client.get(url).status_code == 404
    assert client.post(url, json={
        "reviewer": "tester", "tool": "ubt", "outcome": "not_run",
        "evidence_reference": "none", "summary": "未実施",
        "expected_revised_fingerprint": "0" * 64,
    }).status_code == 404
