"""D4: 再比較結果の保存と人間による明示的な判定の管理。"""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Literal

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, ConfigDict, Field

from .review_api import ReviewRequest, review_recompare
from .review_store import list_records, load_record, save_record
from .verification_report_api import summarize_verification

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



def _summarize(records: list[dict]) -> list[dict]:
    """レビュー単位に最新の人間の判断を集約する。

    イベント作成時刻で並べ、同時刻ならIDを使って順序を安定させる。
    元レコードのpendingは変更しない。
    """
    reviews = {item["id"]: item for item in records if item.get("type") == "recomparison"}
    decisions: dict[str, list[dict]] = {}
    for item in records:
        if item.get("type") == "decision" and item.get("source_record_id") in reviews:
            decisions.setdefault(item["source_record_id"], []).append(item)

    summaries: list[dict] = []
    for record_id, review in reviews.items():
        events = sorted(decisions.get(record_id, []), key=lambda item: (item.get("created_at", ""), item["id"]))
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
def get_review_records(limit: int = 50, offset: int = 0) -> dict:
    """レビュー一覧と最新判断を返す。コード本体は一覧に含めない。"""
    if limit < 1 or limit > 200 or offset < 0:
        raise HTTPException(status_code=422, detail="limit must be 1..200 and offset >= 0")
    records = list_records()
    summaries = _summarize(records)
    page = summaries[offset:offset + limit]
    reviews = {item["id"]: item for item in records if item.get("type") == "recomparison"}
    # ページ内のレビューだけを集約する。全レコードは一度だけ読み込む。
    for item in page:
        report = summarize_verification(reviews[item["id"]], records)
        item["verification"] = {
            "uht": report["build"]["uht"]["status"],
            "ubt": report["build"]["ubt"]["status"],
            "behavior": report["behavior"]["status"],
            "evidence_complete": report["evidence_complete"],
            "blockers": report["blockers"],
        }
    return {"total": len(summaries), "items": page}


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
