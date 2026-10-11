"""V2: レビュー作成期間ごとの推移と現在の検証状況を集計する。"""
from __future__ import annotations

from datetime import date, datetime, timedelta, timezone
from typing import Literal

from fastapi import APIRouter, HTTPException

from .review_history_api import _summarize
from .review_store import list_records
from .verification_report_api import index_verification_events, summarize_verification

router = APIRouter()


def _parse_date(value: str | None, name: str) -> date | None:
    if value is None:
        return None
    try:
        # ISO日付のみ許容し、タイムゾーン曖昧性を避ける。
        parsed = date.fromisoformat(value)
        if parsed.isoformat() != value:
            raise ValueError("Non-canonical date")
        return parsed
    except ValueError as exc:
        raise HTTPException(status_code=422, detail=f"{name} must be YYYY-MM-DD") from exc


def _bucket(day: date, interval: str) -> str:
    if interval == "day":
        return day.isoformat()
    if interval == "week":
        return (day - timedelta(days=day.weekday())).isoformat()
    return day.replace(day=1).isoformat()


@router.get("/review/statistics/trends")
def get_review_trends(
    interval: Literal["day", "week", "month"] = "day",
    start_date: str | None = None,
    end_date: str | None = None,
) -> dict:
    """レビュー作成日（UTC）で分類し、最新の証跡状態を同じ区間に集計する。

    承認や検証イベントの発生日を集計する時系列ではない点に注意する。
    """
    start = _parse_date(start_date, "start_date")
    end = _parse_date(end_date, "end_date")
    if start is not None and end is not None and start > end:
        raise HTTPException(status_code=422, detail="start_date must not exceed end_date")

    records = list_records()
    summaries = _summarize(records)
    reviews = {item["id"]: item for item in records if item.get("type") == "recomparison"}
    event_index = index_verification_events(records)
    buckets: dict[str, dict] = {}
    included = 0

    for summary in summaries:
        created = datetime.fromisoformat(summary["created_at"].replace("Z", "+00:00"))
        # 過去のnaive時刻はUTCとして解釈し、日付境界を統一する。
        if created.tzinfo is None:
            created = created.replace(tzinfo=timezone.utc)
        day = created.astimezone(timezone.utc).date()
        if start is not None and day < start:
            continue
        if end is not None and day > end:
            continue

        key = _bucket(day, interval)
        if key not in buckets:
            buckets[key] = {
                "period_start": key,
                "created": 0,
                "decisions": {"pending": 0, "approved": 0, "rejected": 0},
                "evidence_complete": 0,
                "evidence_incomplete": 0,
                "build_failed": {"uht": 0, "ubt": 0},
                "behavior_mismatched": 0,
            }
        row = buckets[key]
        report = summarize_verification(reviews[summary["id"]], records, event_index)
        row["created"] += 1
        row["decisions"][report["review"]["decision"]] += 1
        if report["evidence_complete"]:
            row["evidence_complete"] += 1
        else:
            row["evidence_incomplete"] += 1
        for tool in ("uht", "ubt"):
            if report["build"][tool]["status"] == "failed":
                row["build_failed"][tool] += 1
        if report["behavior"]["status"] == "mismatched":
            row["behavior_mismatched"] += 1
        included += 1

    return {
        "interval": interval,
        "start_date": start.isoformat() if start is not None else None,
        "end_date": end.isoformat() if end is not None else None,
        "total_reviews": included,
        "periods": [buckets[key] for key in sorted(buckets)],
        "state_as_of": "request_time",
        "verified_by_server": False,
        "diagnostics": [
            "Periods are based on review creation dates in UTC; statuses reflect latest reported evidence.",
            "This is not a historical timeline of decisions or build executions.",
        ],
    }
