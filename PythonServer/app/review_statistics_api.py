"""V2: レビューと外部検証証跡の集計統計を返す読み取り専用API。

イベント数ではなくレビュー単位の最新状態を数える。
"""
from __future__ import annotations

from collections import Counter

from fastapi import APIRouter

from .review_history_api import _summarize
from .review_store import list_records
from .verification_report_api import summarize_verification

router = APIRouter()


@router.get("/review/statistics")
def get_review_statistics() -> dict:
    """全レビューの最新状態を一度の読み込みで集計する。

    個別レポートと同じ判定ロジックを使用し、イベントの重複計上を防ぐ。
    """
    records = list_records()
    summaries = _summarize(records)
    reviews = {
        item["id"]: item for item in records if item.get("type") == "recomparison"
    }

    decision_counts = Counter({"pending": 0, "approved": 0, "rejected": 0})
    target_counts = Counter({"direct": 0, "ai": 0})
    build_counts = {
        tool: Counter({"not_recorded": 0, "passed": 0, "failed": 0, "not_run": 0})
        for tool in ("uht", "ubt")
    }
    behavior_counts = Counter({
        "not_recorded": 0, "matched": 0, "mismatched": 0, "not_run": 0,
    })
    blocker_counts = Counter({
        "human_approved": 0, "uht_passed": 0,
        "ubt_passed": 0, "behavior_matched": 0,
    })
    complete_count = 0

    for summary in summaries:
        report = summarize_verification(reviews[summary["id"]], records)
        decision_counts[report["review"]["decision"]] += 1
        target_counts[summary["target"]] += 1
        for tool in ("uht", "ubt"):
            build_counts[tool][report["build"][tool]["status"]] += 1
        behavior_counts[report["behavior"]["status"]] += 1
        for blocker in report["blockers"]:
            blocker_counts[blocker] += 1
        if report["evidence_complete"]:
            complete_count += 1

    return {
        "total_reviews": len(summaries),
        "decisions": dict(decision_counts),
        "targets": dict(target_counts),
        "build": {tool: dict(build_counts[tool]) for tool in ("uht", "ubt")},
        "behavior": dict(behavior_counts),
        "evidence_complete": complete_count,
        "evidence_incomplete": len(summaries) - complete_count,
        "blockers": dict(blocker_counts),
        "verified_by_server": False,
        "diagnostics": [
            "Counts reflect externally reported evidence, not server-executed verification.",
        ],
    }
