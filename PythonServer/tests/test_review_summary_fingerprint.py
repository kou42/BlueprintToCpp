"""レビュー要約で修正後指紋の異なる判定を無視する回帰テスト。"""
from app.review_history_api import _summarize


def test_summary_ignores_decisions_for_other_fingerprint():
    review_id = "a" * 32
    review = {
        "id": review_id,
        "type": "recomparison",
        "created_at": "2026-01-01T00:00:00+00:00",
        "reviewer": "tester",
        "result": {"review": {"target": "ai", "revised_fingerprint": "b" * 64}},
    }
    wrong = {
        "id": "c" * 32,
        "type": "decision",
        "created_at": "2026-01-02T00:00:00+00:00",
        "source_record_id": review_id,
        "revised_fingerprint": "d" * 64,
        "decision": "approved",
    }
    summary = _summarize([review, wrong])[0]
    assert summary["latest_decision"] == "pending"
    assert summary["decision_count"] == 0
    assert summary["latest_decision_id"] is None

    valid = {**wrong, "id": "e" * 32, "revised_fingerprint": "b" * 64}
    summary = _summarize([review, wrong, valid])[0]
    assert summary["latest_decision"] == "approved"
    assert summary["decision_count"] == 1
    assert summary["latest_decision_id"] == valid["id"]
