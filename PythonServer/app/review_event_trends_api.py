"""V2: 承認・ビルド・動作比較の証跡イベント発生推移を集計する。"""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Literal

from fastapi import APIRouter, HTTPException

from .review_store import list_records
from .review_trends_api import _bucket, _parse_date

router = APIRouter()


@router.get("/review/statistics/event-trends")
def get_review_event_trends(
    interval: Literal["day", "week", "month"] = "day",
    start_date: str | None = None,
    end_date: str | None = None,
) -> dict:
    """イベントの記録時刻（UTC）で集計し、後続イベントを上書きしない。

    対応する元レビューと指紋が一致する証跡だけを数える。
    """
    start = _parse_date(start_date, "start_date")
    end = _parse_date(end_date, "end_date")
    if start is not None and end is not None and start > end:
        raise HTTPException(status_code=422, detail="start_date must not exceed end_date")

    records = list_records()
    reviews = {
        item["id"]: item for item in records if item.get("type") == "recomparison"
    }
    buckets: dict[str, dict] = {}
    included = 0

    for event in records:
        event_type = event.get("type")
        if event_type not in ("decision", "verification", "behavior"):
            continue
        review = reviews.get(event.get("source_record_id"))
        if review is None:
            continue
        if event.get("revised_fingerprint") != review["result"]["review"]["revised_fingerprint"]:
            continue

        created_at = event.get("created_at")
        if isinstance(created_at, str) is False:
            continue
        try:
            created = datetime.fromisoformat(created_at.replace("Z", "+00:00"))
        except ValueError:
            continue
        if created.tzinfo is None:
            created = created.replace(tzinfo=timezone.utc)
        day = created.astimezone(timezone.utc).date()
        if start is not None and day < start:
            continue
        if end is not None and day > end:
            continue

        # 作成日時と異なり、履歴イベントを一件ずつ数えるため再提出も含む。
        key = _bucket(day, interval)
        if key not in buckets:
            buckets[key] = {
                "period_start": key,
                "total_events": 0,
                "decisions": {"approved": 0, "rejected": 0},
                "build": {
                    "uht": {"passed": 0, "failed": 0, "not_run": 0},
                    "ubt": {"passed": 0, "failed": 0, "not_run": 0},
                },
                "behavior": {"matched": 0, "mismatched": 0, "not_run": 0},
            }
        row = buckets[key]
        if event_type == "decision":
            value = event.get("decision")
            if value not in row["decisions"]:
                continue
            row["decisions"][value] += 1
        elif event_type == "verification":
            tool = event.get("tool")
            outcome = event.get("outcome")
            if tool not in row["build"] or outcome not in row["build"][tool]:
                continue
            row["build"][tool][outcome] += 1
        else:
            # 動作比較は1回の提出を1イベントとして集計する。
            # 複数ケースを含む場合、いずれか不一致ならmismatched、
            # 不一致がなく未実施があればnot_runとして扱う。
            cases = event.get("cases")
            if isinstance(cases, list) is False or len(cases) == 0:
                continue
            outcomes = {case.get("outcome") for case in cases if isinstance(case, dict)}
            if "mismatched" in outcomes:
                outcome = "mismatched"
            elif "not_run" in outcomes:
                outcome = "not_run"
            elif outcomes == {"matched"}:
                outcome = "matched"
            else:
                continue
            row["behavior"][outcome] += 1

        row["total_events"] += 1
        included += 1

    return {
        "interval": interval,
        "start_date": start.isoformat() if start is not None else None,
        "end_date": end.isoformat() if end is not None else None,
        "total_events": included,
        "periods": [buckets[key] for key in sorted(buckets)],
        "verified_by_server": False,
        "diagnostics": [
            "Periods reflect event submission timestamps in UTC, not actual external execution times.",
            "All evidence outcomes are externally reported; repeated submissions count separately.",
        ],
    }
