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


def test_list_shows_verification_blockers(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    record = create_review()
    record_id = record["id"]
    item = client.get("/review/records").json()["items"][0]
    assert item["verification"]["uht"] == "not_recorded"
    assert item["verification"]["behavior"] == "not_recorded"
    assert item["verification"]["evidence_complete"] is False
    assert len(item["verification"]["blockers"]) == 4

    fingerprint = record["result"]["review"]["revised_fingerprint"]
    for tool in ("uht", "ubt"):
        assert client.post(f"/review/records/{record_id}/verification", json={
            "reviewer": "tester", "tool": tool, "outcome": "passed",
            "evidence_reference": "log", "summary": "外部ビルド",
            "expected_revised_fingerprint": fingerprint,
        }).status_code == 201
    assert client.post(f"/review/records/{record_id}/behavior", json={
        "reviewer": "tester", "environment": "UE", "evidence_reference": "test",
        "expected_revised_fingerprint": fingerprint,
        "cases": [{"case_id": "one", "description": "比較", "outcome": "matched",
                   "blueprint_observation": "ok", "cpp_observation": "ok"}],
    }).status_code == 201
    assert client.post(f"/review/records/{record_id}/decision", json={
        "reviewer": "tester", "decision": "approved", "reason": "確認済み",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201

    item = client.get("/review/records").json()["items"][0]
    report = client.get(f"/review/records/{record_id}/report").json()
    assert item["verification"]["evidence_complete"] is True
    assert item["verification"]["blockers"] == []
    assert item["verification"]["uht"] == report["build"]["uht"]["status"]
    assert item["verification"]["behavior"] == report["behavior"]["status"]

    # 最新の失敗イベントを追加すると一覧の表示も失敗に更新される。
    assert client.post(f"/review/records/{record_id}/verification", json={
        "reviewer": "tester", "tool": "ubt", "outcome": "failed",
        "evidence_reference": "new-log", "summary": "再ビルド失敗",
        "expected_revised_fingerprint": fingerprint,
    }).status_code == 201
    item = client.get("/review/records").json()["items"][0]
    assert item["verification"]["ubt"] == "failed"
    assert "ubt_passed" in item["verification"]["blockers"]


def test_list_pagination_keeps_summary(tmp_path, monkeypatch):
    monkeypatch.setenv("BLUEPRINT_TO_CPP_REVIEW_STORE", str(tmp_path))
    create_review()
    create_review()
    result = client.get("/review/records?limit=1&offset=1").json()
    assert result["total"] == 2
    assert len(result["items"]) == 1
    assert "verification" in result["items"][0]
    assert "input" not in result["items"][0]
