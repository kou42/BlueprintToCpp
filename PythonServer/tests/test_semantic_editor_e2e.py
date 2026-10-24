import json
import subprocess
from pathlib import Path

import pytest

from app import semantic_editor_e2e
from app.semantic_editor_e2e import SemanticEditorE2EError, run_e2e


def _paths(tmp_path: Path):
    editor = tmp_path / "UnrealEditor-Cmd.exe"
    project = tmp_path / "Test.uproject"
    python = tmp_path / "python.exe"
    server = tmp_path / "PythonServer"
    artifact = tmp_path / "Saved" / "BlueprintSemanticJson" / "test.semantic.json"
    for path in (editor, project, python, artifact):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    server.mkdir()
    return editor, project, python, server, artifact


def _editor_output(artifact: Path) -> str:
    return "\n".join((
        "Test Completed. Result={Success} Name={EditorRoundtrip} "
        "Path={BlueprintAIConverter.SemanticJson.EditorRoundtrip}",
        f"SEMANTIC_ROUNDTRIP_ARTIFACT={artifact}",
        "**** TEST COMPLETE. EXIT CODE: 0 ****",
    ))


def test_real_export_artifact_is_passed_to_semantic_cli(tmp_path, monkeypatch):
    editor, project, python, server, artifact = _paths(tmp_path)
    calls = []

    def fake_run(command, **kwargs):
        calls.append((command, kwargs))
        if len(calls) == 1:
            return subprocess.CompletedProcess(command, 0, _editor_output(artifact), "")
        report = {
            "file": str(artifact), "status": "ok", "asset_path": "/Engine/Transient.Test",
            "semantic_status": "partial", "variables": 3, "functions": 1,
            "graph_traces": 1, "provider_status": "mocked",
        }
        return subprocess.CompletedProcess(command, 0, json.dumps(report), "")

    monkeypatch.setattr(semantic_editor_e2e.subprocess, "run", fake_run)
    report = run_e2e(
        editor, project, python_executable=python, python_server=server, timeout=30)
    assert report["status"] == "ok"
    assert report["variables"] == 3
    assert calls[1][0] == [
        str(python.resolve()), "-m", "app.semantic_roundtrip", "--provider", "mock",
        str(artifact.resolve()),
    ]
    assert calls[1][1]["cwd"] == server.resolve()


@pytest.mark.parametrize("output", [
    "**** TEST COMPLETE. EXIT CODE: 0 ****",
    "Test Completed. Result={Fail} Path={BlueprintAIConverter.SemanticJson.EditorRoundtrip}",
])
def test_missing_success_or_artifact_is_rejected(tmp_path, monkeypatch, output):
    editor, project, python, server, _ = _paths(tmp_path)
    monkeypatch.setattr(
        semantic_editor_e2e.subprocess,
        "run",
        lambda command, **kwargs: subprocess.CompletedProcess(command, 0, output, ""),
    )
    with pytest.raises(SemanticEditorE2EError, match="successful Automation result"):
        run_e2e(editor, project, python_executable=python, python_server=server)


def test_cli_failure_is_rejected(tmp_path, monkeypatch):
    editor, project, python, server, artifact = _paths(tmp_path)
    results = iter((
        subprocess.CompletedProcess([], 0, _editor_output(artifact), ""),
        subprocess.CompletedProcess([], 1, "", "schema mismatch"),
    ))
    monkeypatch.setattr(semantic_editor_e2e.subprocess, "run", lambda command, **kwargs: next(results))
    with pytest.raises(SemanticEditorE2EError, match="schema mismatch"):
        run_e2e(editor, project, python_executable=python, python_server=server)
