import pytest
from app.schemas import parse_snapshot
from app.generator_dispatch import generate_supported_cpp
from app.node_registry import UnsupportedNodeError
from test_analyzer import pin


def snapshot(category="bool", initial="false", assigned="true"):
    value = pin("Enabled", "input", category)
    value["default_value"] = assigned
    return {
        "schema_version": 1, "engine_version": "5.7.0",
        "asset_path": "/Game/BP.BP", "parent_class": "/Script/Engine.Actor",
        "variables": [{"name": "Enabled", "type": {"category": category},
                       "default_value": initial}],
        "graphs": [{"name": "EventGraph", "path": "/Game/BP.BP:EventGraph",
                    "kind": "event", "nodes": [
            {"id": "begin", "class": "/Script/BlueprintGraph.K2Node_Event",
             "title": "BeginPlay", "function_name": "ReceiveBeginPlay",
             "pins": [pin("then", "output", "exec", [("set", "execute")])]},
            {"id": "set", "class": "/Script/BlueprintGraph.K2Node_VariableSet",
             "title": "Set Enabled", "variable_name": "Enabled",
             "pins": [pin("execute", "input", "exec", [("begin", "then")]),
                      pin("then", "output", "exec"), value]},
        ]}]}


def test_bool_variable_declaration_and_assignment():
    result = generate_supported_cpp(parse_snapshot(snapshot()), "ATestActor")
    assert "bool Enabled = false;" in result["files"][0]["content"]
    assert "Enabled = true;" in result["files"][1]["content"]


def test_int_variable():
    raw = snapshot("int", "4", "12")
    result = generate_supported_cpp(parse_snapshot(raw))
    assert "int32 Enabled = 4;" in result["files"][0]["content"]
    assert "Enabled = 12;" in result["files"][1]["content"]


def test_unknown_variable_rejected():
    raw = snapshot()
    raw["graphs"][0]["nodes"][1]["variable_name"] = "Missing"
    with pytest.raises(UnsupportedNodeError, match="unknown variable"):
        generate_supported_cpp(parse_snapshot(raw))


def test_invalid_literal_rejected():
    with pytest.raises(UnsupportedNodeError, match="invalid bool"):
        generate_supported_cpp(parse_snapshot(snapshot(assigned="maybe")))


def test_connected_input_rejected():
    raw = snapshot()
    raw["graphs"][0]["nodes"][1]["pins"][2]["links"] = [
        {"node_id": "begin", "pin_id": "then"}]
    with pytest.raises(Exception):
        generate_supported_cpp(parse_snapshot(raw))


def test_variable_get_is_not_silently_ignored():
    raw = snapshot()
    raw["graphs"][0]["nodes"].append({
        "id": "get", "class": "/Script/BlueprintGraph.K2Node_VariableGet",
        "title": "Get Enabled", "variable_name": "Enabled",
        "pins": [pin("Enabled", "output", "bool")]})
    with pytest.raises(UnsupportedNodeError):
        generate_supported_cpp(parse_snapshot(raw))


def test_ambiguous_float_precision_rejected():
    raw = snapshot("float", "0", "1")
    with pytest.raises(UnsupportedNodeError, match="precision"):
        generate_supported_cpp(parse_snapshot(raw))
