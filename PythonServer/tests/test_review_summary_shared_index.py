"""要約と検証レポートが同じ証跡索引を共有できることを確認する。"""
from app.review_history_api import _summarize
from app.verification_report_api import index_verification_events, summarize_verification


def test_shared_index_matches_default_summary():
    review = {
        "id": "a" * 32,
        "type": "recomparison",
        "created_at": "2026-01-01T00:00:00+00:00",
        "reviewer": "tester",
        "result": {"review": {"target": "ai", "revised_fingerprint": "b" * 64}},
    }
    decision = {
        "id": "c" * 32,
        "type": "decision",
        "source_record_id": review["id"],
        "revised_fingerprint": "b" * 64,
        "created_at": "2026-01-02T00:00:00+00:00",
        "decision": "approved",
    }
    records = [review, decision]
    index = index_verification_events(records)
    assert _summarize(records, index) == _summarize(records)
    assert summarize_verification(review, records, index) == summarize_verification(review, records)
    assert _summarize(records, index)[0]["latest_decision"] == "approved"
