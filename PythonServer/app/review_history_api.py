"""D4: 再比較結果の保存と人間による明示的な判定の管理。"""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Literal

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, ConfigDict, Field

from .review_api import ReviewRequest, review_recompare
from .review_store import list_records, load_record, record_sort_key, save_record
from .verification_report_api import index_verification_events, summarize_verification

router = APIRouter()


class ReviewSaveRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    request: ReviewRequest
    reviewer: str = Field(min_length=1, max_length=120)


class ReviewDecisionRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    reviewer: str = Field(min_length=1, max_length=120)
    decision: Literal["approved", "rejected"]
    reason: str = Field(min_length=1, max_length=2000)
    # 保存時の結果を変えずに、新しいイベントを別レコードとして保存する。
    expected_revised_fingerprint: str = Field(pattern=r"^[0-9a-f]{64}$")


@router.post("/review/records", status_code=201)
def create_review_record(request: ReviewSaveRequest) -> dict:
    """再比較を実行した後、その時点の入力と結果を監査用に保存する。"""
    result = review_recompare(request.request)
    return save_record({
        "type": "recomparison",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "reviewer": request.reviewer,
        "input": request.request.model_dump(),
        "result": result,
        "decision": "pending",
    })


@router.get("/review/records/{record_id}")
def get_review_record(record_id: str) -> dict:
    record = load_record(record_id)
    if record is None:
        raise HTTPException(status_code=404, detail="Review record not found")
    return record


@router.post("/review/records/{record_id}/decision", status_code=201)
def create_review_decision(record_id: str, request: ReviewDecisionRequest) -> dict:
    """承認は必ず明示的な操作で作成し、差分一致だけで承認しない。"""
    original = load_record(record_id)
    if original is None or original.get("type") != "recomparison":
        raise HTTPException(status_code=404, detail="Review record not found")
    expected = original["result"]["review"]["revised_fingerprint"]
    if expected != request.expected_revised_fingerprint:
        raise HTTPException(status_code=409, detail="Revised fingerprint mismatch")
    return save_record({
        "type": "decision",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "reviewer": request.reviewer,
        "source_record_id": record_id,
        "revised_fingerprint": expected,
        "decision": request.decision,
        "reason": request.reason,
        "diagnostics": ["Human decision only; UHT/UBT and Blueprint behavior are not verified by this endpoint."],
    })



def _summarize(
    records: list[dict],
    event_index: dict[tuple[str, str], list[dict]] | None = None,
) -> list[dict]:
    """レビュー単位に最新の人間の判断を集約する。

    イベント作成時刻で並べ、同時刻ならIDを使って順序を安定させる。
    元レコードのpendingは変更しない。
    """
    reviews = {item["id"]: item for item in records if item.get("type") == "recomparison"}
    # 指紋の異なる判定を混ぜないよう、レビューごとの正しい証跡だけを参照する。
    if event_index is None:
        event_index = index_verification_events(records)

    summaries: list[dict] = []
    for record_id, review in reviews.items():
        fingerprint = review["result"]["review"]["revised_fingerprint"]
        events = sorted(
            (item for item in event_index.get((record_id, fingerprint), []) if item["type"] == "decision"),
            key=record_sort_key,
        )
        latest = events[-1] if events else None
        summaries.append({
            "id": record_id,
            "created_at": review.get("created_at"),
            "reviewer": review.get("reviewer"),
            "target": review["result"]["review"]["target"],
            "revised_fingerprint": review["result"]["review"]["revised_fingerprint"],
            "latest_decision": latest["decision"] if latest else "pending",
            "latest_decision_id": latest["id"] if latest else None,
            "decision_count": len(events),
        })
    return sorted(summaries, key=lambda item: (item["created_at"] or "", item["id"]), reverse=True)


@router.get("/review/records")
def get_review_records(
    limit: int = 50,
    offset: int = 0,
    reviewer: str | None = None,
    target: Literal["direct", "ai"] | None = None,
    decision: Literal["pending", "approved", "rejected"] | None = None,
    uht: Literal["not_recorded", "passed", "failed", "not_run"] | None = None,
    ubt: Literal["not_recorded", "passed", "failed", "not_run"] | None = None,
    behavior: Literal["not_recorded", "matched", "mismatched", "not_run"] | None = None,
    evidence_complete: bool | None = None,
) -> dict:
    """レビューのメタデータと最新証跡で絞り込んでからページングする。"""
    if limit < 1 or limit > 200 or offset < 0:
        raise HTTPException(status_code=422, detail="limit must be 1..200 and offset >= 0")
    if reviewer is not None and (len(reviewer) == 0 or len(reviewer) > 120):
        raise HTTPException(status_code=422, detail="reviewer must be 1..120 characters")

    records = list_records()
    event_index = index_verification_events(records)
    summaries = _summarize(records, event_index)
    reviews = {item["id"]: item for item in records if item.get("type") == "recomparison"}
    filtered: list[dict] = []

    for item in summaries:
        if reviewer is not None and reviewer.casefold() not in item["reviewer"].casefold():
            continue
        if target is not None and item["target"] != target:
            continue
        if decision is not None and item["latest_decision"] != decision:
            continue

        # 検証状態のフィルタは、一覧と個別レポートで同じ判定を使用する。
        report = summarize_verification(reviews[item["id"]], records, event_index)
        verification = {
            "uht": report["build"]["uht"]["status"],
            "ubt": report["build"]["ubt"]["status"],
            "behavior": report["behavior"]["status"],
            "evidence_complete": report["evidence_complete"],
            "blockers": report["blockers"],
        }
        if uht is not None and verification["uht"] != uht:
            continue
        if ubt is not None and verification["ubt"] != ubt:
            continue
        if behavior is not None and verification["behavior"] != behavior:
            continue
        if evidence_complete is not None and verification["evidence_complete"] != evidence_complete:
            continue
        item["verification"] = verification
        filtered.append(item)

    # totalは絞り込み後の件数とし、ページを跨いでも意味が変わらないようにする。
    return {"total": len(filtered), "items": filtered[offset:offset + limit]}


@router.get("/review/records/{record_id}/status")
def get_review_status(record_id: str) -> dict:
    """IDを指定して現在の判断と判定イベントを取得する。"""
    record = load_record(record_id)
    if record is None or record.get("type") != "recomparison":
        raise HTTPException(status_code=404, detail="Review record not found")
    records = list_records()
    summary = next(item for item in _summarize(records) if item["id"] == record_id)
    events = [
        {"id": item["id"], "created_at": item["created_at"], "reviewer": item["reviewer"],
         "decision": item["decision"], "reason": item["reason"]}
        for item in records
        if item.get("type") == "decision" and item.get("source_record_id") == record_id
    ]
    events.sort(key=lambda item: (item["created_at"], item["id"]))
    return {**summary, "events": events}
