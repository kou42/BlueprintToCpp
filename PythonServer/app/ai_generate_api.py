from typing import Any
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from .ai_provider import AIProviderError, get_ai_provider
from .semantic_json import SemanticJsonError

router = APIRouter()

class AIGenerateRequest(BaseModel):
    semantic: dict[str, Any]
    provider: str = "mock"

@router.post("/ai/generate")
def ai_generate(request: AIGenerateRequest) -> dict:
    try:
        result = get_ai_provider(request.provider).generate(request.semantic)
    except (SemanticJsonError, AIProviderError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return {"status": result.status, "provider": result.provider,
            "files": [{"path": f.path, "content": f.content} for f in result.files],
            "diagnostics": list(result.diagnostics)}
