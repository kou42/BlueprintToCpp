"""Optional generation API, mounted independently of the Phase 2 analyzer."""
from typing import Any
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from .schemas import SnapshotError, parse_snapshot
from .code_generator import GenerationError
from .generator_dispatch import generate_supported_cpp
from .node_registry import UnsupportedNodeError
from .generated_output_validator import GeneratedOutputError, validate_generation_result

router = APIRouter()


class GenerateRequest(BaseModel):
    snapshot: dict[str, Any]
    class_name: str = "AGeneratedBlueprintActor"


@router.post("/generate")
def generate(request: GenerateRequest) -> dict:
    try:
        result = generate_supported_cpp(parse_snapshot(request.snapshot), request.class_name)
        validate_generation_result(result.get("status"), result.get("files"))
        return result
    except (SnapshotError, GenerationError, UnsupportedNodeError, GeneratedOutputError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
