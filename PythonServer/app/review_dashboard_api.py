"""V2: ダッシュボード用のレビュー処理状況を一括取得するAPI。"""
from __future__ import annotations

from fastapi import APIRouter, HTTPException

from .review_history_api import _summarize
from .review_statistics_api import get_review_statistics
from .review_store import list_records
from .verification_report_api import summarize_verification

router = APIRouter()


@router.get("/review/dashboard")
def get_review_dashboard(limit: int = 10) -> dict:
    """全体統計と優先確認対象をまとめる。

    一覧の順序はレビュー作成日の降順とし、承認未完了や証跡不足を優先する。
    ダッシュボード表示は外部申告の証跡に基づく。
    """
    if limit < 1 or limit > 100:
        raise HTTPException(status_code=422, detail="limit must be 1..100")

    records = list_records()
    summaries = _summarize(records)
    reviews = {item["id"]: item for item in records if item.get("type") == "recomparison"}
    attention: list[dict] = []
    pending = 0
    blocked = 0
    complete = 0
    for summary in summaries:
        report = summarize_verification(reviews[summary["id"]], records)
        if summary["latest_decision"] == "pending":
            pending += 1
        if report["evidence_complete"]:
            complete += 1
        else:
            blocked += 1
            attention.append({
                "id": summary["id"],
                "created_at": summary["created_at"],
                "reviewer": summary["reviewer"],
                "target": summary["target"],
                "decision": report["review"]["decision"],
                "uht": report["build"]["uht"]["status"],
                "ubt": report["build"]["ubt"]["status"],
                "behavior": report["behavior"]["status"],
                "blockers": report["blockers"],
            })

    # 処理待ち件数と最新の要確認レビューをコード本文なしで提供する。
    return {
        "total_reviews": len(summaries),
        "pending_decisions": pending,
        "evidence_complete": complete,
        "needs_attention": blocked,
        "attention_items": attention[:limit],
        "attention_total": len(attention),
        "verified_by_server": False,
        "diagnostics": [
            "Dashboard states reflect externally reported evidence, not server-executed checks.",
        ],
    }
