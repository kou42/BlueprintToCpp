"""D4: 人間による修正結果を検証して再比較する、保存しないレビューAPI。"""
from __future__ import annotations

from typing import Literal

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, ConfigDict, Field

from .compare_api import CompareRequest, GenerationInput, compare_generated
from .generated_output_validator import GeneratedOutputError
from .output_comparison import normalize_artifact

router = APIRouter()


class ReviewRevision(BaseModel):
    model_config = ConfigDict(extra="forbid")

    # 比較した元の生成物を明示し、古いレビュー結果の誤適用を防ぐ。
    expected_direct_fingerprint: str = Field(pattern=r"^[0-9a-f]{64}$")
    expected_ai_fingerprint: str = Field(pattern=r"^[0-9a-f]{64}$")
    target: Literal["direct", "ai"]
    revised: GenerationInput
    note: str = Field(min_length=1, max_length=2000)


class ReviewRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    original: CompareRequest
    revision: ReviewRevision


@router.post("/review/recompare")
def review_recompare(request: ReviewRequest) -> dict:
    """修正した片側だけを置換して再比較する。ファイル保存や自動承認はしない。"""
    try:
        original_direct = normalize_artifact("direct", request.original.direct.model_dump())
        original_ai = normalize_artifact("ai", request.original.ai.model_dump())
        if original_direct.fingerprint != request.revision.expected_direct_fingerprint:
            raise HTTPException(status_code=409, detail="Direct fingerprint mismatch: refresh review")
        if original_ai.fingerprint != request.revision.expected_ai_fingerprint:
            raise HTTPException(status_code=409, detail="AI fingerprint mismatch: refresh review")
        revised = normalize_artifact(request.revision.target, request.revision.revised.model_dump())
    except GeneratedOutputError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc

    # 生成が成功していない修正候補は、レビュー完了済みとして扱わない。
    if revised.status != "generated":
        raise HTTPException(status_code=422, detail="Revised artifact must be generated")

    updated = CompareRequest(
        direct=request.revision.revised if request.revision.target == "direct" else request.original.direct,
        ai=request.revision.revised if request.revision.target == "ai" else request.original.ai,
        semantic=request.original.semantic,
    )
    before = compare_generated(request.original)
    after = compare_generated(updated)
    return {
        "status": "recompared",
        "review": {
            "target": request.revision.target,
            "note": request.revision.note,
            "original_fingerprint": original_direct.fingerprint if request.revision.target == "direct" else original_ai.fingerprint,
            "revised_fingerprint": revised.fingerprint,
            "semantic_fingerprint": after["semantic_fingerprint"],
            "approved": False,
        },
        "before": before,
        "after": after,
        "diagnostics": [
            "Recomparison does not establish semantic equivalence, UHT/UBT success, or reviewer approval.",
            "Review notes and revisions are returned but not persisted by the service.",
        ],
    }
