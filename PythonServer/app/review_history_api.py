"""D4: 再比較結果の保存と人間による明示的な判定の管理。"""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Literal

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, ConfigDict, Field

from .review_api import ReviewRequest, review_recompare
from .review_store import load_record, save_record

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
