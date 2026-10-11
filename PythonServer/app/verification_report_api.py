"""V2: レビュー・ビルド・動作証跡をまとめる読み取り専用レポート。

外部から申告された結果を集約するのみで、意味的同値性は判定しない。
"""
from __future__ import annotations

from fastapi import APIRouter, HTTPException

from .review_store import list_records, load_record

router = APIRouter()


def _latest(events: list[dict]) -> dict | None:
    """イベントの時刻とIDで安定した順序を決定する。"""
    if len(events) == 0:
        return None
    return max(events, key=lambda item: (item.get("created_at", ""), item["id"]))


@router.get("/review/records/{record_id}/report")
def get_verification_report(record_id: str) -> dict:
    """同じレビューID・修正後指紋の証跡だけを集計する。"""
    review = load_record(record_id)
    if review is None or review.get("type") != "recomparison":
        raise HTTPException(status_code=404, detail="Review record not found")
    return summarize_verification(review, list_records())


def index_verification_events(records: list[dict]) -> dict[tuple[str, str], list[dict]]:
    """証跡をレビューID・指紋で一度だけ分類する。

    レビュー数ごとに全証跡を再走査する二乗の処理を避ける。
    """
    index: dict[tuple[str, str], list[dict]] = {}
    for item in records:
        if item.get("type") not in ("decision", "verification", "behavior"):
            continue
        source_id = item.get("source_record_id")
        fingerprint = item.get("revised_fingerprint")
        if isinstance(source_id, str) is False or isinstance(fingerprint, str) is False:
            continue
        index.setdefault((source_id, fingerprint), []).append(item)
    return index


def summarize_verification(
    review: dict,
    records: list[dict],
    event_index: dict[tuple[str, str], list[dict]] | None = None,
) -> dict:
    """読み込み済みレコードから総合状態を構築する。

    一覧APIからも同じ判定を再利用し、レコード数分のディスク再走査を避ける。
    """
    record_id = review["id"]
    fingerprint = review["result"]["review"]["revised_fingerprint"]
    if event_index is None:
        event_index = index_verification_events(records)
    related = event_index.get((record_id, fingerprint), [])
    decision = _latest([item for item in related if item["type"] == "decision"])
    build: dict[str, dict | None] = {}
    for tool in ("uht", "ubt"):
        build[tool] = _latest([
            item for item in related if item["type"] == "verification" and item.get("tool") == tool
        ])
    behavior = _latest([item for item in related if item["type"] == "behavior"])

    counts = {"matched": 0, "mismatched": 0, "not_run": 0}
    if behavior is not None:
        for case in behavior["cases"]:
            counts[case["outcome"]] += 1
    behavior_status = "not_recorded"
    if behavior is not None:
        if counts["mismatched"] > 0:
            behavior_status = "mismatched"
        elif counts["not_run"] > 0:
            behavior_status = "not_run"
        else:
            behavior_status = "matched"

    build_status = {
        tool: build[tool]["outcome"] if build[tool] is not None else "not_recorded"
        for tool in ("uht", "ubt")
    }
    decision_status = decision["decision"] if decision is not None else "pending"
    # 人間の承認があっても外部テストに不備があればreadyにはしない。
    checks = {
        "human_approved": decision_status == "approved",
        "uht_passed": build_status["uht"] == "passed",
        "ubt_passed": build_status["ubt"] == "passed",
        "behavior_matched": behavior_status == "matched",
    }
    blockers = [key for key, passed in checks.items() if passed is False]
    return {
        "record_id": record_id,
        "revised_fingerprint": fingerprint,
        "review": {
            "decision": decision_status,
            "latest_event_id": decision["id"] if decision is not None else None,
        },
        "build": {
            tool: {"status": build_status[tool], "latest_event_id": build[tool]["id"] if build[tool] is not None else None}
            for tool in ("uht", "ubt")
        },
        "behavior": {
            "status": behavior_status,
            "counts": counts,
            "latest_event_id": behavior["id"] if behavior is not None else None,
        },
        "checks": checks,
        "blockers": blockers,
        "evidence_complete": len(blockers) == 0,
        "verified_by_server": False,
        "diagnostics": [
            "All build and behavior results are externally supplied and not authenticated.",
            "Evidence completeness is not proof of C++ correctness or Blueprint semantic equivalence.",
        ],
    }
