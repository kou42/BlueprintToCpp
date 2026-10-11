"""DirectとAIの生成結果を比較する読み取り専用API。"""
from typing import Any

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, ConfigDict, Field

from .generated_output_validator import GeneratedOutputError
from .output_comparison import compare_artifacts, normalize_artifact
from .cpp_structure_diff import compare_declarations
from .ue_reflection_diff import compare_reflection
from .cpp_signature_diff import compare_signatures

router = APIRouter()


class GenerationInput(BaseModel):
    # 不明なフィールドを受け付けず、レビュー対象の入力を明確にする。
    model_config = ConfigDict(extra="forbid")

    status: str
    files: list[dict[str, Any]] = Field(default_factory=list)
    diagnostics: list[str] = Field(default_factory=list)


class CompareRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    direct: GenerationInput
    ai: GenerationInput


@router.post("/compare")
def compare_generated(request: CompareRequest) -> dict:
    """両経路の既存生成結果を比較する。AI呼び出しやファイル保存は行わない。"""
    try:
        direct = normalize_artifact("direct", request.direct.model_dump())
        ai = normalize_artifact("ai", request.ai.model_dump())
        result = compare_artifacts(direct, ai)
    except GeneratedOutputError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc

    return {
        **result,
        "direct_fingerprint": direct.fingerprint,
        "ai_fingerprint": ai.fingerprint,
        "structure": compare_declarations(direct, ai),
        "reflection": compare_reflection(direct, ai),
        "signatures": compare_signatures(direct, ai),
    }
