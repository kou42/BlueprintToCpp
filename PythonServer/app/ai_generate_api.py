from typing import Any
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from .ai_provider import AIProviderError, get_ai_provider
from .semantic_json import SemanticJsonError
from .generated_output_validator import GeneratedOutputError, validate_generation_result
from .generation_trace import summarize_generation_trace

router = APIRouter()

class AIGenerateRequest(BaseModel):
    semantic: dict[str, Any]
    provider: str = "mock"

@router.post("/ai/generate")
def ai_generate(request: AIGenerateRequest) -> dict:
    try:
        result = get_ai_provider(request.provider).generate(request.semantic)
        files = validate_generation_result(result.status, result.files)
        trace = summarize_generation_trace(request.semantic)
    except (SemanticJsonError, AIProviderError, GeneratedOutputError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return {"status": result.status, "provider": result.provider,
            "files": list(files),
            "diagnostics": list(result.diagnostics),
            "semantic_trace": trace}
