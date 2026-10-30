import copy
import json
import subprocess
import sys
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


MULTIPLE_FUNCTION_TEST = "BlueprintAIConverter.SemanticIR.Lowering.MultipleFunctionGraphs"


def test_selected_multiple_function_export_runs_real_python_cli(tmp_path, monkeypatch):
    editor, project, _, server, artifact = _paths(tmp_path)
    server = Path(__file__).resolve().parents[1]
    data = json.loads((Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json")
                      .read_text(encoding="utf-8"))
    data["source"]["parent_class_path"] = "/Script/CoreUObject.Object"
    template = data["functions"][0]
    data["functions"] = []
    for name in ("RunFirst", "RunSecond", "OnConverted"):
        function = copy.deepcopy(template)
        function.update(name=name, visibility="public", is_event_graph=name == "OnConverted")
        data["functions"].append(function)
    artifact.write_text(json.dumps(data), encoding="utf-8")
    real_run = subprocess.run
    calls = []

    def fake_editor(command, **kwargs):
        calls.append(command)
        if command[0] == str(editor.resolve()):
            output = _editor_output(artifact).replace(
                semantic_editor_e2e.DEFAULT_AUTOMATION_TEST, MULTIPLE_FUNCTION_TEST)
            return subprocess.CompletedProcess(command, 0, output, "")
        return real_run(command, **kwargs)

    monkeypatch.setattr(semantic_editor_e2e.subprocess, "run", fake_editor)
    result = run_e2e(editor, project, python_executable=Path(sys.executable),
                     python_server=server, automation_test=MULTIPLE_FUNCTION_TEST)
    assert result["functions"] == 3
    assert result["graph_traces"] == 1
    assert result["automation_test"] == MULTIPLE_FUNCTION_TEST
    assert result["provider_status"] == "mocked"
    assert f"-ExecCmds=Automation RunTests {MULTIPLE_FUNCTION_TEST}; Quit" in calls[0]


def test_success_for_a_different_automation_test_is_rejected(tmp_path, monkeypatch):
    editor, project, python, server, artifact = _paths(tmp_path)
    monkeypatch.setattr(semantic_editor_e2e.subprocess, "run", lambda command, **kwargs:
                        subprocess.CompletedProcess(command, 0, _editor_output(artifact), ""))
    with pytest.raises(SemanticEditorE2EError, match="successful Automation result"):
        run_e2e(editor, project, python_executable=python, python_server=server,
                automation_test=MULTIPLE_FUNCTION_TEST)


@pytest.mark.parametrize("name", ["", "Other.Test", "BlueprintAIConverter.Test; Quit",
                                  "BlueprintAIConverter.Test,Other", "BlueprintAIConverter..Test"])
def test_invalid_automation_selection_does_not_start_process(tmp_path, monkeypatch, name):
    editor, project, python, server, _ = _paths(tmp_path)

    def unexpected_process(*args, **kwargs):
        pytest.fail("invalid selection must not launch Editor")

    monkeypatch.setattr(semantic_editor_e2e.subprocess, "run", unexpected_process)
    with pytest.raises(SemanticEditorE2EError, match="invalid BlueprintAIConverter"):
        run_e2e(editor, project, python_executable=python, python_server=server,
                automation_test=name)


def test_cli_passes_selected_automation_test(tmp_path, monkeypatch, capsys):
    editor, project, _, _, _ = _paths(tmp_path)
    selected = []

    def fake_e2e(editor_path, project_path, **kwargs):
        selected.append(kwargs["automation_test"])
        return {"status": "ok"}

    monkeypatch.setattr(semantic_editor_e2e, "run_e2e", fake_e2e)
    assert semantic_editor_e2e.main([
        "--editor", str(editor), "--project", str(project),
        "--automation-test", MULTIPLE_FUNCTION_TEST]) == 0
    assert selected == [MULTIPLE_FUNCTION_TEST]
    assert json.loads(capsys.readouterr().out)["status"] == "ok"
