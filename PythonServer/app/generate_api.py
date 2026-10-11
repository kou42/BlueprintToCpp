"""Optional generation API, mounted independently of the Phase 2 analyzer."""
from typing import Any
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from .schemas import SnapshotError, parse_snapshot
from .code_generator import GenerationError, generate_cpp
from .node_registry import UnsupportedNodeError

router = APIRouter()


class GenerateRequest(BaseModel):
    snapshot: dict[str, Any]
    class_name: str = "AGeneratedBlueprintActor"


@router.post("/generate")
def generate(request: GenerateRequest) -> dict:
    try:
        return generate_cpp(parse_snapshot(request.snapshot), request.class_name)
    except (SnapshotError, GenerationError, UnsupportedNodeError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
