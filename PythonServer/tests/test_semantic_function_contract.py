"""P2 class/interface metadata and C++ declaration contract regressions."""
import copy
import json
from pathlib import Path
import subprocess
import sys

import pytest

from app import semantic_editor_e2e
from app.output_comparison import normalize_artifact
from app.semantic_declaration_audit import audit_semantic_declarations
from app.semantic_editor_e2e import run_e2e
from app.semantic_json import SemanticJsonError, parse_semantic_json
from app.semantic_roundtrip import validate_file
from app.semantic_signature_audit import audit_semantic_signatures

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"
INTERFACE = "/Script/BlueprintAIConverter.BPNotifyContract"


def interface_document():
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    data["variables"] = []
    data["source"]["parent_class_path"] = "/Script/CoreUObject.Object"
    data["class_info"] = dict(origin="blueprint", class_path="/Game/BP_Contract.BP_Contract_C",
        class_name="BP_Contract_C", native_parent_class_path="/Script/CoreUObject.Object",
        interface_paths=[INTERFACE], declared_properties=[], declared_functions=[],
        declared_function_signatures=[])
    function = data["functions"][0]
    function.update(name="NotifyValue", visibility="public", is_native=False, is_static=False,
                    is_const=False, is_blueprint_event=True, is_event_graph=True,
                    is_override=False, overridden_function_path=None, is_interface_implementation=True,
                    interface_class_path=INTERFACE, interface_function_path=INTERFACE + ":NotifyValue",
                    function_attributes_source="generated_class_reflection")
    function["parameters"] = [dict(name="Value", declaration_index=0,
        type=dict(kind="int32", type_path="", is_reference=False, is_const=False),
        is_out=False, is_return=False, has_default_value=True, default_value="0",
        default_value_status="specified", default_value_source="function_entry_pin",
        default_value_source_node_id=None, default_value_source_pin_id=None)]
    ordinary = copy.deepcopy(function)
    ordinary.update(name="RunOrdinary", visibility="private", is_blueprint_event=False,
                    is_event_graph=False, is_interface_implementation=False,
                    interface_class_path=None, interface_function_path=None, parameters=[])
    data["functions"].append(ordinary)
    data["class_info"]["declared_function_signatures"] = [dict(name="NotifyValue",
        reflection_path=function["owner_class_path"] + ":NotifyValue", flags="0", return_cpp_type="void",
        is_native=False, is_static=False, is_const=False, is_blueprint_event=True, is_net=False,
        is_final=False, parameters=[dict(name="Value", cpp_type="int32", is_out=False,
                                       is_reference=False, is_const=False)])]
    return data


def artifact(header):
    return normalize_artifact("direct", dict(status="generated", files=[dict(path="Contract.h", content=header)]))


def test_interface_class_and_function_metadata_survive_roundtrip(tmp_path):
    data = interface_document()
    assert parse_semantic_json(json.loads(json.dumps(data))) == data
    target = tmp_path / "interface.semantic.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    assert validate_file(target, check_provider=True)["functions"] == 2


@pytest.mark.parametrize("field", ["is_native", "is_static", "is_const", "is_blueprint_event",
                                  "is_event_graph", "is_override", "is_interface_implementation"])
def test_function_attributes_require_booleans(field):
    data = interface_document()
    data["functions"][0][field] = "false"
    with pytest.raises(SemanticJsonError, match=field):
        parse_semantic_json(data)


@pytest.mark.parametrize("field", ["interface_class_path", "interface_function_path", "overridden_function_path"])
def test_function_target_paths_require_nullable_strings(field):
    data = interface_document()
    data["functions"][0][field] = []
    with pytest.raises(SemanticJsonError, match=field):
        parse_semantic_json(data)


@pytest.mark.parametrize("field,value", [
    ("interface_paths", [INTERFACE, INTERFACE]), ("interface_paths", [None]),
    ("interface_paths", [""]), ("interface_paths", {}), ("origin", "interface"),
    ("class_path", 1), ("declared_properties", [{}]), ("declared_functions", [None]),
    ("declared_function_signatures", [{}]),
])
def test_invalid_class_metadata_is_rejected(field, value):
    data = interface_document()
    data["class_info"][field] = value
    with pytest.raises(SemanticJsonError, match="class_info"):
        parse_semantic_json(data)


@pytest.mark.parametrize("field,value", [
    ("declaration_index", True), ("declaration_index", "0"), ("has_default_value", 1),
    ("default_value_status", None), ("default_value_source", []),
    ("default_value_source_node_id", "bad"), ("default_value_source_pin_id", "bad"),
    ("default_value", None),
])
def test_invalid_parameter_metadata_is_rejected(field, value):
    data = interface_document()
    data["functions"][0]["parameters"][0][field] = value
    with pytest.raises(SemanticJsonError, match=field):
        parse_semantic_json(data)


def test_default_availability_preserves_empty_value_and_null():
    data = interface_document()
    parameter = data["functions"][0]["parameters"][0]
    parameter["default_value"] = ""
    assert parse_semantic_json(data) is data
    parameter.update(has_default_value=False, default_value=None, default_value_status="unavailable")
    assert parse_semantic_json(data) is data
    parameter["default_value"] = ""
    with pytest.raises(SemanticJsonError, match="expected null"):
        parse_semantic_json(data)


@pytest.mark.parametrize("policy", [None, "automatic", "omit"])
def test_super_call_policy_roundtrip_and_legacy_omission(tmp_path, policy):
    data = interface_document()
    function = data["functions"][0]
    function.update(name="VerifiedReturn", is_interface_implementation=False,
                    interface_class_path=None, interface_function_path=None,
                    is_event_graph=False, is_override=True, is_const=True,
                    overridden_function_path="/Script/BlueprintAIConverter.BPEmitterTestActor:VerifiedReturn")
    function["return_type"]["kind"] = "int32"
    if policy is not None:
        function["super_call_policy"] = policy
    assert parse_semantic_json(json.loads(json.dumps(data))) == data
    path = tmp_path / "super.semantic.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    assert validate_file(path, check_provider=True)["functions"] == 2
    header = "class AContract {\n virtual int32 VerifiedReturn_Implementation(int32 Value) const override;\n void RunOrdinary();\n};"
    assert audit_semantic_signatures(data, artifact(header))["differences"] == []


@pytest.mark.parametrize("policy", ["always", "", None, False, {}])
def test_unknown_or_malformed_super_policy_is_rejected(policy):
    data = interface_document()
    data["functions"][0]["super_call_policy"] = policy
    with pytest.raises(SemanticJsonError, match="super_call_policy"):
        parse_semantic_json(data)


def test_implementation_declaration_is_audited_by_generated_name():
    data = interface_document()
    header = "class UContract {\n virtual void NotifyValue_Implementation(int32 Value) override;\n void RunOrdinary();\n};"
    result = audit_semantic_signatures(data, artifact(header))
    assert result["differences"] == []
    assert not any("unambiguous" in message for message in result["diagnostics"])
    assert audit_semantic_declarations(data, artifact(header))["missing"] == []
    bad = header.replace("int32 Value", "bool Value").replace(" override;", ";")
    fields = {item["field"] for item in audit_semantic_signatures(data, artifact(bad))["differences"]}
    assert fields == {"parameter[0].type", "override"}


def test_audit_preserves_interface_function_graph_and_const_contract():
    data = interface_document()
    function = data["functions"][0]
    function.update(is_event_graph=False, is_const=True)
    correct = "class UContract {\n virtual void NotifyValue_Implementation(int32 Value) const override;\n void RunOrdinary();\n};"
    assert audit_semantic_signatures(data, artifact(correct))["differences"] == []
    bad = correct.replace(" const override", " override")
    assert {item["field"] for item in audit_semantic_signatures(data, artifact(bad))["differences"]} == {"const"}


def test_audit_uses_declaration_indices_and_reports_ambiguous_indices():
    data = interface_document()
    function = data["functions"][0]
    second = copy.deepcopy(function["parameters"][0])
    second.update(name="Other", declaration_index=1)
    second["type"]["kind"] = "bool"
    function["parameters"] = [second, function["parameters"][0]]
    header = "class UContract {\n virtual void NotifyValue_Implementation(int32 Value, bool Other) override;\n void RunOrdinary();\n};"
    assert audit_semantic_signatures(data, artifact(header))["differences"] == []
    second["declaration_index"] = 0
    result = audit_semantic_signatures(data, artifact(header))
    assert any("ambiguous parameter declaration indices" in message for message in result["diagnostics"])


def test_real_interface_export_selection_uses_public_cli(tmp_path, monkeypatch):
    editor = tmp_path / "UnrealEditor-Cmd"
    project = tmp_path / "Contract.uproject"
    editor.touch()
    project.touch()
    target = tmp_path / "contract.semantic.json"
    target.write_text(json.dumps(interface_document()), encoding="utf-8")
    selected = "BlueprintAIConverter.Conversion.RealBlueprint.InterfaceContract"
    real_run = subprocess.run

    def simulate_editor(command, **kwargs):
        if command[0] == str(editor.resolve()):
            return subprocess.CompletedProcess(command, 0,
                f"Test Completed. Result={{Success}} Path={{{selected}}}\n"
                f"SEMANTIC_ROUNDTRIP_ARTIFACT={target}\n**** TEST COMPLETE. EXIT CODE: 0 ****", "")
        return real_run(command, **kwargs)

    monkeypatch.setattr(semantic_editor_e2e.subprocess, "run", simulate_editor)
    report = run_e2e(editor, project, python_executable=Path(sys.executable),
                     python_server=Path(__file__).resolve().parents[1], automation_test=selected)
    assert report["functions"] == 2
    assert report["provider_status"] == "mocked"
