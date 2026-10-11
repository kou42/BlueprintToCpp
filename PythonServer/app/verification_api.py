"""V2: 外部で実施したUHT/UBT検証の証跡をレビューに紐づける。

本APIはコマンド実行を行わず、提出された結果を記録するだけである。
"""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Literal

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, ConfigDict, Field

from .review_store import list_records, load_record, save_record

router = APIRouter()


class VerificationEvidence(BaseModel):
    model_config = ConfigDict(extra="forbid")

    reviewer: str = Field(min_length=1, max_length=120)
    tool: Literal["uht", "ubt"]
    outcome: Literal["passed", "failed", "not_run"]
    # 外部ログの保管先は任意の参照文字列。コマンドやファイルは実行・読み込みしない。
    evidence_reference: str = Field(min_length=1, max_length=1000)
    summary: str = Field(min_length=1, max_length=2000)
    expected_revised_fingerprint: str = Field(pattern=r"^[0-9a-f]{64}$")


def _verification_events(record_id: str) -> list[dict]:
    """レビューIDと修正後の指紋の両方が一致する証跡のみ集約する。"""
    record = load_record(record_id)
    expected = record["result"]["review"]["revised_fingerprint"]
    events = [
        item for item in list_records()
        if item.get("type") == "verification"
        and item.get("source_record_id") == record_id
        and item.get("revised_fingerprint") == expected
    ]
    return sorted(events, key=lambda item: (item.get("created_at", ""), item["id"]))


@router.post("/review/records/{record_id}/verification", status_code=201)
def create_verification(record_id: str, request: VerificationEvidence) -> dict:
    """人が提出した検証結果を不変イベントとして保存する。"""
    record = load_record(record_id)
    if record is None or record.get("type") != "recomparison":
        raise HTTPException(status_code=404, detail="Review record not found")
    expected = record["result"]["review"]["revised_fingerprint"]
    if expected != request.expected_revised_fingerprint:
        raise HTTPException(status_code=409, detail="Revised fingerprint mismatch")
    return save_record({
        "type": "verification",
        "source_record_id": record_id,
        "revised_fingerprint": expected,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "reviewer": request.reviewer,
        "tool": request.tool,
        "outcome": request.outcome,
        "evidence_reference": request.evidence_reference,
        "summary": request.summary,
        "diagnostics": [
            "Externally supplied verification evidence; the server did not run UHT or UBT.",
        ],
    })


@router.get("/review/records/{record_id}/verification")
def get_verification(record_id: str) -> dict:
    """各ツールの最新提出結果と全イベントを返す。未提出はnot_recordedとする。"""
    record = load_record(record_id)
    if record is None or record.get("type") != "recomparison":
        raise HTTPException(status_code=404, detail="Review record not found")
    events = _verification_events(record_id)
    latest = {"uht": "not_recorded", "ubt": "not_recorded"}
    for event in events:
        latest[event["tool"]] = event["outcome"]
    return {
        "record_id": record_id,
        "revised_fingerprint": record["result"]["review"]["revised_fingerprint"],
        "latest": latest,
        "events": events,
        "verified_by_server": False,
    }
