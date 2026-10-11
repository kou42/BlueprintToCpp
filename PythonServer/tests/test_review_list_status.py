from fastapi.testclient import TestClient

from app.main import app
from app.output_comparison import normalize_artifact

client = TestClient(app)


def create_review():
    direct = {"status": "generated", "files": [{"path": "A.cpp", "content": "int X = 1;"}]}
    ai = {"status": "generated", "files": [{"path": "A.cpp", "content": "int X = 2;"}]}
    payload = {
        "reviewer": "tester",
        "request": {
            "original": {"direct": direct, "ai": ai},
            "revision": {
                "target": "ai",
                "revised": direct,
                "note": "修正を確認",
                "expected_direct_fingerprint": normalize_artifact("direct", direct).fingerprint,
                "expected_ai_fingerprint": normalize_artifact("ai", ai).fingerprint,
            },
        },
    }
    response = client.post("/review/records", json=payload)
    assert response.status_code == 201
    return response.json()


def test_review_list_and_latest_decision(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    first = create_review()
    second = create_review()
    listing = client.get("/review/records").json()
    assert listing["total"] == 2
    assert {item["id"] for item in listing["items"]} == {first["id"], second["id"]}
    assert all(item["latest_decision"] == "pending" for item in listing["items"])

    fingerprint = first["result"]["review"]["revised_fingerprint"]
    for decision in ("approved", "rejected"):
        response = client.post(f"/review/records/{first['id']}/decision", json={
            "reviewer": "reviewer", "decision": decision, "reason": decision,
            "expected_revised_fingerprint": fingerprint,
        })
        assert response.status_code == 201

    status = client.get(f"/review/records/{first['id']}/status")
    assert status.status_code == 200
    body = status.json()
    assert body["decision_count"] == 2
    assert len(body["events"]) == 2
    assert body["latest_decision"] == body["events"][-1]["decision"]
    assert client.get(f"/review/records/{second['id']}/status").json()["latest_decision"] == "pending"
    assert client.get(f"/review/records/{first['id']}").json()["decision"] == "pending"


def test_pagination_and_validation(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    for _ in range(3):
        create_review()
    assert len(client.get("/review/records?limit=2&offset=1").json()["items"]) == 2
    assert client.get("/review/records?limit=0").status_code == 422
    assert client.get("/review/records?offset=-1").status_code == 422
    assert client.get("/review/records/" + "0" * 32 + "/status").status_code == 404


def test_ignores_non_record_files(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    (tmp_path / "junk.json").write_text("{}", encoding="utf-8")
    (tmp_path / ("a" * 32 + ".json")).write_text("invalid", encoding="utf-8")
    assert client.get("/review/records").json()["total"] == 0
