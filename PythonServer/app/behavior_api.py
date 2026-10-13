"""V2: Blueprintと生成C++の外部動作テスト結果をレビューに記録する。

値の比較は提出側で行う。本APIは申告された判定と証跡を保存するだけである。
"""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Literal

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, ConfigDict, Field

from .review_store import list_records, load_record, record_sort_key, save_record

router = APIRouter()


class BehaviorCase(BaseModel):
    model_config = ConfigDict(extra="forbid")

    case_id: str = Field(min_length=1, max_length=120)
    description: str = Field(min_length=1, max_length=500)
    outcome: Literal["matched", "mismatched", "not_run"]
    # 外部テストの出力を文字列で受け取り、型や数値許容差は解釈しない。
    blueprint_observation: str | None = Field(default=None, max_length=2000)
    cpp_observation: str | None = Field(default=None, max_length=2000)


class BehaviorEvidence(BaseModel):
    model_config = ConfigDict(extra="forbid")

    reviewer: str = Field(min_length=1, max_length=120)
    expected_revised_fingerprint: str = Field(pattern=r"^[0-9a-f]{64}$")
    environment: str = Field(min_length=1, max_length=500)
    evidence_reference: str = Field(min_length=1, max_length=1000)
    cases: list[BehaviorCase] = Field(min_length=1, max_length=100)

    def validated_cases(self) -> list[dict]:
        """重複IDや観測値の欠落を、結果の保存前に拒否する。"""
        identifiers: set[str] = set()
        for case in self.cases:
            if case.case_id in identifiers:
                raise HTTPException(status_code=422, detail="Duplicate behavior case_id")
            identifiers.add(case.case_id)
            if case.outcome != "not_run" and (
                case.blueprint_observation is None or case.cpp_observation is None
            ):
                raise HTTPException(status_code=422, detail="Executed case requires both observations")
        return [case.model_dump() for case in self.cases]


def _load_review(record_id: str) -> dict:
    record = load_record(record_id)
    if record is None or record.get("type") != "recomparison":
        raise HTTPException(status_code=404, detail="Review record not found")
    return record


@router.post("/review/records/{record_id}/behavior", status_code=201)
def create_behavior_evidence(record_id: str, request: BehaviorEvidence) -> dict:
    """実行環境とケース単位の観測値を不変の履歴イベントとして保存する。"""
    record = _load_review(record_id)
    expected = record["result"]["review"]["revised_fingerprint"]
    if expected != request.expected_revised_fingerprint:
        raise HTTPException(status_code=409, detail="Revised fingerprint mismatch")
    cases = request.validated_cases()
    return save_record({
        "type": "behavior",
        "source_record_id": record_id,
        "revised_fingerprint": expected,
        "semantic_fingerprint": record["result"]["review"].get("semantic_fingerprint"),
        "created_at": datetime.now(timezone.utc).isoformat(),
        "reviewer": request.reviewer,
        "environment": request.environment,
        "evidence_reference": request.evidence_reference,
        "cases": cases,
        "diagnostics": [
            "Behavior outcomes are externally reported; no Blueprint or C++ execution was performed.",
        ],
    })


@router.get("/review/records/{record_id}/behavior")
def get_behavior_evidence(record_id: str) -> dict:
    """最新の提出結果とケース別集計を返す。未提出と未実施を区別する。"""
    record = _load_review(record_id)
    expected = record["result"]["review"]["revised_fingerprint"]
    events = sorted(
        (
            item for item in list_records()
            if item.get("type") == "behavior"
            and item.get("source_record_id") == record_id
            and item.get("revised_fingerprint") == expected
        ),
        key=record_sort_key,
    )
    latest = events[-1] if events else None
    counts = {"matched": 0, "mismatched": 0, "not_run": 0}
    if latest is not None:
        for case in latest["cases"]:
            counts[case["outcome"]] += 1
    return {
        "record_id": record_id,
        "revised_fingerprint": expected,
        "status": "not_recorded" if latest is None else (
            "mismatched" if counts["mismatched"] > 0 else (
                "not_run" if counts["not_run"] > 0 else "matched"
            )
        ),
        "counts": counts,
        "latest_event_id": latest["id"] if latest is not None else None,
        "events": events,
        "verified_by_server": False,
    }
