"""The C++ serializer emits Branch statements in CFG blocks and nested trees."""
import copy
import json
import subprocess
import sys
from pathlib import Path

import pytest

from app.semantic_json import SemanticJsonError, parse_semantic_json
from app.semantic_roundtrip import validate_file
from app import semantic_editor_e2e
from app.semantic_editor_e2e import SemanticEditorE2EError, run_e2e

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"


def branch_document(shape="tree"):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    function = data["functions"][0]
    function.update(name="RunBranch", visibility="public", is_event_graph=False)
    data["source"]["parent_class_path"] = "/Script/CoreUObject.Object"
    block = function["blocks"][0]
    call = copy.deepcopy(block["statements"][0])
    call["expression"].update(function_name="SetSubtitlesEnabled",
                              function_owner_path="/Script/Engine.GameplayStatics")
    argument = call["expression"]["arguments"][0]
    argument["type"]["kind"] = "bool"
    argument["literal_value"] = "true"
    condition = copy.deepcopy(argument)
    condition.update(literal_value="false", kind="literal", arguments=[])
    branch = {"kind": "branch", "source_node_id": call["source_node_id"],
              "assignment_target": None, "expression": condition,
              "then_statements": [copy.deepcopy(call)], "else_statements": [copy.deepcopy(call)]}
    branch["else_statements"][0]["expression"]["arguments"][0]["literal_value"] = "false"
    block["statements"] = [branch]
    if shape == "nested":
        branch["then_statements"] = [copy.deepcopy(branch)]
    if shape == "cfg":
        successors = ["00000000-0000-0000-0000-000000000032",
                      "00000000-0000-0000-0000-000000000033"]
        block.update(block_id="00000000-0000-0000-0000-000000000031",
                     terminator="branch", successor_block_ids=successors)
        for block_id, field in zip(successors, ("then_statements", "else_statements")):
            function["blocks"].append({"block_id": block_id,
                "source_entry_node_id": block["source_entry_node_id"], "terminator": "end",
                "successor_block_ids": [], "statements": branch[field]})
            branch[field] = []
    return data


@pytest.mark.parametrize("shape", ["tree", "nested", "cfg"])
def test_branch_calls_survive_json_and_mock_provider(tmp_path, shape):
    data = branch_document(shape)
    assert parse_semantic_json(json.loads(json.dumps(data))) == data
    target = tmp_path / "branch.semantic.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    result = validate_file(target, check_provider=True)
    assert result["functions"] == 1
    assert result["provider_status"] == "mocked"


@pytest.mark.parametrize("mutate", [
    lambda branch: branch.pop("then_statements"),
    lambda branch: branch.pop("else_statements"),
    lambda branch: branch.update(then_statements=None),
    lambda branch: branch["expression"]["type"].update(kind="string"),
    lambda branch: branch["then_statements"][0].update(source_node_id="invalid"),
    lambda branch: branch["else_statements"][0].update(kind="unsupported"),
    lambda branch: branch["then_statements"][0]["expression"].update(arguments=[{}]),
])
def test_invalid_branch_children_and_condition_are_rejected(mutate):
    data = branch_document()
    branch = data["functions"][0]["blocks"][0]["statements"][0]
    mutate(branch)
    with pytest.raises(SemanticJsonError, match=r"functions\[0\].blocks\[0\].statements\[0\]"):
        parse_semantic_json(data)


def test_branch_nesting_has_a_bounded_validation_depth():
    data = branch_document()
    root = data["functions"][0]["blocks"][0]["statements"][0]
    template = copy.deepcopy(root)
    current = root
    for _ in range(65):
        child = copy.deepcopy(template)
        current["then_statements"] = [child]
        current = child
    with pytest.raises(SemanticJsonError, match="statement nesting exceeds 64"):
        parse_semantic_json(data)


def test_non_branch_statement_cannot_hide_unvalidated_children():
    data = branch_document()
    data["functions"][0]["blocks"][0]["statements"][0]["kind"] = "expression"
    with pytest.raises(SemanticJsonError, match="only branch statements"):
        parse_semantic_json(data)


@pytest.mark.parametrize("event_graph", [False, True])
def test_uobject_nested_branch_entry_and_common_continuation_roundtrip(tmp_path, event_graph):
    data = branch_document("nested")
    function = data["functions"][0]
    function.update(name="OnNested" if event_graph else "RunNested", is_event_graph=event_graph)
    statements = function["blocks"][0]["statements"]
    join = copy.deepcopy(statements[0]["else_statements"][0])
    statements.append(join)
    parsed = parse_semantic_json(json.loads(json.dumps(data)))
    assert parsed["source"]["parent_class_path"] == "/Script/CoreUObject.Object"
    assert parsed["functions"][0] == function
    assert len(parsed["functions"][0]["blocks"][0]["statements"]) == 2
    target = tmp_path / "nested-uobject.semantic.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    assert validate_file(target, check_provider=True)["provider_status"] == "mocked"


@pytest.mark.parametrize("shape,test_name", [
    ("cfg", "FunctionBranchNativeCalls"),
    ("nested", "NestedBranchNativeCalls"),
])
@pytest.mark.parametrize("invalid", [False, True])
def test_selected_branch_export_runs_real_python_cli(tmp_path, monkeypatch, shape, test_name, invalid):
    # Only the Editor process is simulated; schema validation and Mock Provider run in a subprocess.
    editor = tmp_path / "UnrealEditor-Cmd"
    project = tmp_path / "Branch.uproject"
    editor.touch()
    project.touch()
    data = branch_document(shape)
    if shape == "nested":
        data["source"]["parent_class_path"] = "/Script/Engine.Actor"
        data["functions"][0].update(name="BeginPlay", is_event_graph=True)
    if invalid:
        data["functions"][0]["blocks"][0]["statements"][0]["source_node_id"] = "invalid"
    artifact = tmp_path / "branch.semantic.json"
    artifact.write_text(json.dumps(data), encoding="utf-8")
    selected = "BlueprintAIConverter.Conversion.RealBlueprint." + test_name
    real_run = subprocess.run
    commands = []

    def fake_editor(command, **kwargs):
        commands.append(command)
        if command[0] == str(editor.resolve()):
            output = (f"Test Completed. Result={{Success}} Path={{{selected}}}\n"
                      f"SEMANTIC_ROUNDTRIP_ARTIFACT={artifact}\n"
                      "**** TEST COMPLETE. EXIT CODE: 0 ****")
            return subprocess.CompletedProcess(command, 0, output, "")
        return real_run(command, **kwargs)

    monkeypatch.setattr(semantic_editor_e2e.subprocess, "run", fake_editor)
    arguments = dict(python_executable=Path(sys.executable),
                     python_server=Path(__file__).resolve().parents[1], automation_test=selected)
    if invalid:
        with pytest.raises(SemanticEditorE2EError, match="Semantic JSON CLI validation failed"):
            run_e2e(editor, project, **arguments)
    else:
        report = run_e2e(editor, project, **arguments)
        assert report["automation_test"] == selected
        assert report["artifact"] == str(artifact.resolve())
        assert report["functions"] == 1
        assert report["provider_status"] == "mocked"
        assert report["graph_traces"] == len(data["graph_traces"])
    assert len(commands) == 2
    assert f"-ExecCmds=Automation RunTests {selected}; Quit" in commands[0]
