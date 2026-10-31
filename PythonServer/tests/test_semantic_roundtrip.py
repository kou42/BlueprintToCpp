"""Golden JSONと実際のEditor出力に共通するファイル検証経路を固定する。"""
import copy
import json
from pathlib import Path

import pytest

from app.semantic_json import SemanticJsonError, parse_semantic_json
from app.semantic_roundtrip import main, validate_file

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"


@pytest.mark.parametrize("parent_path, component", [
    ("/Script/Engine.ActorComponent", True),
    ("/Script/Engine.SceneComponent", True),
    ("/Script/Engine.DataAsset", False),
    ("/Script/Engine.SaveGame", False),
    ("/Script/MyPlugin.NativeObject", False),
])
def test_native_parent_functions_survive_without_offline_allowlist(tmp_path, parent_path, component):
    """JSON transport preserves native parents; it does not generate or validate UCLASSes."""
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    data["source"]["parent_class_path"] = parent_path
    template = data["functions"][0]
    functions = []
    for name in (["BeginPlay"] if component else []) + ["RunFirst", "RunSecond"]:
        function = copy.deepcopy(template)
        function.update(name=name, is_event_graph=name == "BeginPlay",
                        is_override=name == "BeginPlay", visibility="public" if name != "BeginPlay" else "protected")
        functions.append(function)
    data["functions"] = functions
    parsed = parse_semantic_json(json.loads(json.dumps(data)))
    assert parsed["source"]["parent_class_path"] == parent_path
    assert parsed["functions"] == functions
    target = tmp_path / "native_parent.semantic.json"
    target.write_text(json.dumps(parsed), encoding="utf-8")
    report = validate_file(target, check_provider=True)
    assert report["functions"] == (3 if component else 2)
    assert report["provider_status"] == "mocked"


def test_golden_json_passes_validator_and_mock_provider():
    result = validate_file(GOLDEN, check_provider=True)
    assert result["semantic_status"] == "partial"
    assert result["variables"] == 2
    assert result["functions"] == 1
    assert result["graph_traces"] == 1
    assert result["provider_status"] == "mocked"


def test_golden_empty_and_zero_defaults_preserved():
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    assert data["variables"][0]["default_value"] == "0"
    assert data["variables"][1]["default_value"] == ""


@pytest.mark.parametrize("mutate", [
    lambda data: data.update(schema_version=1),
    lambda data: data["variables"][0].update(default_value_source="unavailable"),
    lambda data: data["functions"][0]["parameters"].append({"name": "broken"}),
    lambda data: data["graph_traces"][0]["exec_edges"][0].update(target_pin_id=None),
])
def test_golden_invalid_mutations_rejected(tmp_path, mutate):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    mutate(data)
    target = tmp_path / "invalid.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(SemanticJsonError):
        validate_file(target, check_provider=True)


def test_cli_exit_codes_and_multiple_files(tmp_path, capsys):
    bad = tmp_path / "bad.json"
    bad.write_text("{invalid", encoding="utf-8")
    assert main(["--provider", "mock", str(GOLDEN)]) == 0
    assert main([str(GOLDEN), str(bad)]) == 1
    output = capsys.readouterr().out
    assert '"status": "ok"' in output
    assert '"status": "error"' in output


def test_utf8_bom_and_missing_file(tmp_path):
    target = tmp_path / "export.json"
    target.write_bytes(bytes((0xEF, 0xBB, 0xBF)) + GOLDEN.read_bytes())
    assert validate_file(target)["variables"] == 2
    with pytest.raises(SemanticJsonError):
        validate_file(tmp_path / "missing.json")


@pytest.mark.parametrize("is_const_reference", [False, True])
def test_override_signature_metadata_survives_json_and_provider(tmp_path, is_const_reference):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    function = data["functions"][0]
    function.update(
        name="VerifiedOverride",
        signature_source="generated_class_reflection",
        is_native=True,
        is_blueprint_event=True,
        is_event_graph=True,
        is_override=True,
        overridden_function_path="/Script/BlueprintAIConverter.BPEmitterTestActor:VerifiedOverride",
    )
    function["parameters"] = [{
        "name": "Value", "declaration_index": 0,
        "type": {"kind": "int32", "type_path": "",
                 "is_reference": is_const_reference, "is_const": is_const_reference},
        "is_out": is_const_reference, "is_return": False,
    }]
    # Preserve Reflection facts; compatibility with native targets is checked in UE.
    parsed = parse_semantic_json(json.loads(json.dumps(data)))
    assert parsed["functions"][0] == function
    target = tmp_path / "override.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    result = validate_file(target, check_provider=True)
    assert result["functions"] == 1
    assert result["provider_status"] == "mocked"


@pytest.mark.parametrize("visibility", [None, "unspecified", "public", "protected", "private"])
def test_function_visibility_survives_schema_and_provider(tmp_path, visibility):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    function = data["functions"][0]
    if visibility is not None:
        function["visibility"] = visibility
    parsed = parse_semantic_json(json.loads(json.dumps(data)))
    assert parsed["functions"][0] == function
    target = tmp_path / "visibility.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    result = validate_file(target, check_provider=True)
    assert result["functions"] == 1
    assert result["provider_status"] == "mocked"


@pytest.mark.parametrize("entry_kind", ["function", "custom_event", "begin_play"])
@pytest.mark.parametrize("second_call", ["static", "self", "print"])
def test_linear_common_calls_preserve_order_types_and_targets(tmp_path, second_call, entry_kind):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    parent = "/Script/Engine.Actor" if second_call == "self" or entry_kind == "begin_play" else "/Script/CoreUObject.Object"
    data["source"]["parent_class_path"] = parent
    function = data["functions"][0]
    function.update(
        name={"function": "RunCalls", "custom_event": "OnCalls", "begin_play": "BeginPlay"}[entry_kind],
        visibility="protected" if entry_kind == "begin_play" else "public",
        is_event_graph=entry_kind != "function")
    original = function["blocks"][0]["statements"][0]
    first = copy.deepcopy(original)
    first["expression"].update(
        function_name="SetSubtitlesEnabled", function_owner_path="/Script/Engine.GameplayStatics")
    boolean = copy.deepcopy(first["expression"]["arguments"][0])
    boolean["type"]["kind"] = "bool"
    boolean["literal_value"] = "true"
    first["expression"]["arguments"] = [boolean]
    second = copy.deepcopy(original if second_call == "print" else first)
    if second_call == "self":
        second["expression"].update(function_name="SetActorTickEnabled",
                                     function_owner_path=parent)
        target = copy.deepcopy(boolean)
        target.update(kind="variable_reference", literal_value="", variable_name="this")
        target["type"].update(kind="object", type_path=parent)
        second["expression"]["arguments"].insert(0, target)
    if second_call != "print":
        second["expression"]["arguments"][-1]["literal_value"] = "false"
    function["blocks"][0]["statements"] = [first, second]
    parsed = parse_semantic_json(json.loads(json.dumps(data)))
    assert parsed["functions"][0] == function
    target_path = tmp_path / "sequence.json"
    target_path.write_text(json.dumps(data), encoding="utf-8")
    result = validate_file(target_path, check_provider=True)
    assert result["provider_status"] == "mocked"
    assert result["functions"] == 1
