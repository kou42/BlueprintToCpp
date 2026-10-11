"""証跡索引のスケーリングを再現可能な入力で計測する。

実行時間は環境依存のため合否には用いず、集計結果の一致を検証する。
"""
from __future__ import annotations

from time import perf_counter

from app.verification_report_api import index_verification_events, summarize_verification


def _records(count: int) -> tuple[list[dict], list[dict]]:
    reviews = []
    records = []
    for index in range(count):
        record_id = f"{index:032x}"
        fingerprint = f"{index:064x}"
        review = {
            "id": record_id,
            "type": "recomparison",
            "result": {"review": {"revised_fingerprint": fingerprint}},
        }
        reviews.append(review)
        records.append(review)
        records.append({
            "id": f"{index + count:032x}",
            "type": "verification",
            "source_record_id": record_id,
            "revised_fingerprint": fingerprint,
            "tool": "ubt",
            "outcome": "passed",
            "created_at": "2026-01-01T00:00:00+00:00",
        })
    return reviews, records


def _measure(count: int) -> tuple[float, float]:
    reviews, records = _records(count)
    started = perf_counter()
    baseline = [summarize_verification(review, records) for review in reviews]
    baseline_seconds = perf_counter() - started

    started = perf_counter()
    index = index_verification_events(records)
    indexed = [summarize_verification(review, records, index) for review in reviews]
    indexed_seconds = perf_counter() - started
    assert baseline == indexed
    assert len(index) == count
    return baseline_seconds, indexed_seconds


def test_indexed_reports_match_baseline_for_multiple_sizes():
    for count in (1, 10, 100):
        baseline_seconds, indexed_seconds = _measure(count)
        print(f"reviews={count} baseline={baseline_seconds:.6f}s indexed={indexed_seconds:.6f}s")
