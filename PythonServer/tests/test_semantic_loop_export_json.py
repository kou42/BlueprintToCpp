"""Loop fields follow BPSemanticJsonSerializer::StatementToJson (JSON v2)."""
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


def loop_document(kind="for_loop", shape="tree"):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    function = data["functions"][0]
    block = function["blocks"][0]
    call = copy.deepcopy(block["statements"][0])
    value = copy.deepcopy(call["expression"]["arguments"][0])
    value.update(kind="literal", literal_value="0", arguments=[])
    value["type"].update(kind="int32")

    def statement(statement_kind):
        return dict(kind=statement_kind, source_node_id=None, assignment_target=None,
                    expression=copy.deepcopy(value), then_statements=[], else_statements=[],
                    loop_index_variable=None, loop_first_index=copy.deepcopy(value),
                    loop_last_index=copy.deepcopy(value), loop_body_statements=[],
                    completed_statements=[])

    loop = statement(kind)
    loop.update(loop_index_variable="Index", loop_body_statements=[call, statement("break")],
                completed_statements=[copy.deepcopy(call)])
    if kind == "for_each_loop":
        array = copy.deepcopy(value)
        array.update(kind="variable_reference", variable_name="Items", literal_value="")
        array["type"].update(container_kind="array", inner_type="int32")
        loop.update(loop_array=array, loop_element_variable="Element",
                    loop_element_type=copy.deepcopy(value["type"]))
    block["statements"] = [loop]
    if shape == "nested":
        nested = copy.deepcopy(loop)
        branch = statement("branch")
        branch["expression"]["type"]["kind"] = "bool"
        branch["expression"]["literal_value"] = "true"
        branch["then_statements"] = [nested]
        loop["loop_body_statements"] = [branch]
    if shape == "nested_loop":
        loop["loop_body_statements"] = loop["loop_body_statements"][:1]
        inner = copy.deepcopy(loop)
        inner["loop_index_variable"] = "InnerIndex"
        inner["loop_first_index"]["literal_value"] = "2"
        inner["loop_last_index"]["literal_value"] = "4"
        inner_completed = inner["completed_statements"]
        inner["completed_statements"] = []
        loop["loop_body_statements"] = [inner, *inner_completed]
    if shape == "cfg":
        header, body, completed = [f"00000000-0000-0000-0000-{i:012d}" for i in (31, 32, 33)]
        block.update(block_id=header, terminator="loop", successor_block_ids=[body, completed])
        for block_id, field, terminator, successors in (
            (body, "loop_body_statements", "fallthrough", [header]),
            (completed, "completed_statements", "end", []),
        ):
            children = loop[field]
            if field == "loop_body_statements":
                children = children[:1]  # Straight body returns to the loop header.
            function["blocks"].append(dict(block_id=block_id, source_entry_node_id=None,
                terminator=terminator, successor_block_ids=successors, statements=children))
            loop[field] = []
    return data


@pytest.mark.parametrize("kind,shape,selection", [
    ("for_loop", "cfg", "Conversion.RealBlueprint.FunctionForLoopCall"),
    ("for_each_loop", "tree", "GeneratedAssets.ForEachLoop"),
    ("for_each_loop", "cfg", "GeneratedAssets.ForEachLoopWithBreak"),
    ("for_loop", "nested_loop", "GeneratedAssets.NestedForLoop"),
])
@pytest.mark.parametrize("invalid", [False, True])
def test_selected_loop_export_runs_real_python_cli(tmp_path, monkeypatch, kind, shape, selection, invalid):
    # The Editor is simulated; the public schema/roundtrip CLI runs in a real process.
    data = loop_document(kind, shape)
    function = data["functions"][0]
    header = function["blocks"][0]
    loop = header["statements"][0]
    if kind == "for_loop" and shape == "cfg":
        data["source"]["parent_class_path"] = "/Script/CoreUObject.Object"
        function.update(name="RunLoopCall", is_event_graph=False)
        call = function["blocks"][1]["statements"][0]["expression"]
        call.update(function_name="SetSubtitlesEnabled",
                    function_owner_path="/Script/Engine.GameplayStatics")
        call["arguments"] = [copy.deepcopy(loop["loop_first_index"])]
        call["arguments"][0]["type"]["kind"] = "bool"
        call["arguments"][0]["literal_value"] = "true"
        function["blocks"][2]["statements"] = []
        if invalid:
            header["successor_block_ids"][0] = "00000000-0000-0000-0000-000000000099"
    elif kind == "for_loop":
        function.update(name="BeginPlay", is_event_graph=True)
        loop["loop_first_index"]["literal_value"] = "1"
        loop["loop_last_index"]["literal_value"] = "3"
        if invalid:
            # A malformed Completed child must fail even when nested inside another loop.
            loop["loop_body_statements"][1]["source_node_id"] = "invalid-inner-completed"
    else:
        function.update(name="BeginPlay", is_event_graph=True)
        with_break = selection.endswith("WithBreak")
        if with_break:
            body = function["blocks"][1]
            break_statement = copy.deepcopy(loop)
            break_statement.update(kind="break", loop_body_statements=[], completed_statements=[])
            # Break is represented as a body statement; the CFG still records the loop back-edge.
            for field in ("loop_array", "loop_element_variable", "loop_element_type"):
                break_statement.pop(field, None)
            body["statements"].append(break_statement)
        else:
            loop["loop_body_statements"] = loop["loop_body_statements"][:1]
        loop["loop_array"]["variable_name"] = "Numbers"
        loop["loop_array"]["type"]["element_type_info"] = copy.deepcopy(loop["loop_element_type"])
        variable = data["variables"][0]
        variable.update(name="Numbers", type=copy.deepcopy(loop["loop_array"]["type"]),
                        default_value_available=False, default_value=None,
                        default_value_source="unavailable")
        data["variables"] = [variable]
        if invalid:
            if with_break:
                body["statements"][-1]["source_node_id"] = "invalid-break-source"
            else:
                loop["loop_array"]["type"]["element_type_info"] = []
    editor = tmp_path / "UnrealEditor-Cmd"
    project = tmp_path / "Loop.uproject"
    editor.touch()
    project.touch()
    artifact = tmp_path / "loop.semantic.json"
    artifact.write_text(json.dumps(data), encoding="utf-8")
    selected = "BlueprintAIConverter." + selection
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
