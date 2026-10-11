"""Optional FastAPI service: install with pip install -e '.[api]'."""
from typing import Any
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from .schemas import SnapshotError, parse_snapshot
from .graph_analyzer import analyze_snapshot
from .ir_builder import build_ir

from .generate_api import router as generate_router
from .ai_generate_api import router as ai_generate_router
from .compare_api import router as compare_router
from .review_api import router as review_router

app = FastAPI(title="BlueprintToCpp Analyzer", version="0.2.0")


app.include_router(generate_router)
app.include_router(ai_generate_router)
app.include_router(compare_router)
app.include_router(review_router)


class AnalyzeRequest(BaseModel):
    snapshot: dict[str, Any]


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.post("/analyze")
def analyze(request: AnalyzeRequest) -> dict:
    try:
        snapshot = parse_snapshot(request.snapshot)
        graphs = analyze_snapshot(snapshot)
        return {
            "status": "ok",
            "asset_path": snapshot.asset_path,
            "graphs": [{
                "path": graph.path,
                "nodes": graph.node_count,
                "exec_edges": len(graph.exec_edges),
                "data_edges": len(graph.data_edges),
                "entry_nodes": list(graph.entry_nodes),
            } for graph in graphs],
            "ir": build_ir(snapshot),
        }
    except SnapshotError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
