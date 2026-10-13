from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact

client = TestClient(app)


def generated(text):
    return {"status": "generated", "files": [{"path": "Example.cpp", "content": text}], "diagnostics": []}


def review_payload():
    direct = generated("int X = 1;\n")
    ai = generated("int X = 2;\n")
    return {
        "request": {
            "original": {"direct": direct, "ai": ai},
            "revision": {
                "expected_direct_fingerprint": normalize_artifact("direct", direct).fingerprint,
                "expected_ai_fingerprint": normalize_artifact("ai", ai).fingerprint,
                "target": "ai",
                "revised": generated("int X = 1;\n"),
                "note": "差分を確認して修正",
            },
        },
        "reviewer": "tester",
    }


def test_save_and_load_and_explicit_decision(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    saved = client.post("/review/records", json=review_payload())
    assert saved.status_code == 201
    record = saved.json()
    assert record["decision"] == "pending"
    assert len(record["id"]) == 32
    loaded = client.get(f"/review/records/{record['id']}")
    assert loaded.status_code == 200
    assert loaded.json() == record
    assert record["result"]["after"]["status"] == "identical"

    fingerprint = record["result"]["review"]["revised_fingerprint"]
    decision = client.post(f"/review/records/{record['id']}/decision", json={
        "reviewer": "tester", "decision": "approved", "reason": "人間がレビュー済み",
        "expected_revised_fingerprint": fingerprint,
    })
    assert decision.status_code == 201
    assert decision.json()["decision"] == "approved"
    assert client.get(f"/review/records/{record['id']}").json()["decision"] == "pending"
    summaries = client.get("/review/records").json()["items"]
    assert summaries[0]["semantic_fingerprint"] is None


def test_stale_decision_rejected(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = client.post("/review/records", json=review_payload()).json()
    response = client.post(f"/review/records/{record['id']}/decision", json={
        "reviewer": "tester", "decision": "approved", "reason": "確認",
        "expected_revised_fingerprint": "0" * 64,
    })
    assert response.status_code == 409


def test_invalid_record_id_and_missing_record(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    assert client.get("/review/records/../../etc/passwd").status_code in (404, 422)
    assert client.get("/review/records/" + "0" * 32).status_code == 404


def test_invalid_revision_does_not_persist(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    payload = review_payload()
    payload["request"]["revision"]["expected_ai_fingerprint"] = "0" * 64
    assert client.post("/review/records", json=payload).status_code == 409
    assert list(tmp_path.iterdir()) == []
