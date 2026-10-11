import pytest
from app.schemas import parse_snapshot
from app.generator_dispatch import generate_supported_cpp
from app.node_registry import UnsupportedNodeError
from test_analyzer import pin


def fixture(condition="true"):
    nodes = [
        {"id": "begin", "class": "/Script/BlueprintGraph.K2Node_Event",
         "title": "BeginPlay", "function_name": "ReceiveBeginPlay",
         "pins": [pin("then", "output", "exec", [("branch", "execute")])]},
        {"id": "branch", "class": "/Script/BlueprintGraph.K2Node_IfThenElse",
         "title": "Branch",
         "pins": [pin("execute", "input", "exec", [("begin", "then")]),
                  pin("Condition", "input", "bool"),
                  pin("then", "output", "exec", [("yes", "execute")]),
                  pin("else", "output", "exec", [("no", "execute")])]},
    ]
    nodes[1]["pins"][1]["default_value"] = condition
    for name, message, arm in [("yes", "YES", "then"), ("no", "NO", "else")]:
        text = pin("InString", "input", "string")
        text["default_value"] = message
        nodes.append({"id": name, "class": "/Script/BlueprintGraph.K2Node_CallFunction",
                      "title": "Print String", "function_name": "PrintString",
                      "pins": [pin("execute", "input", "exec", [("branch", arm)]),
                               pin("then", "output", "exec"), text]})
    return {"schema_version": 1, "engine_version": "5.7.0",
            "asset_path": "/Game/BP.BP", "parent_class": "/Script/Engine.Actor",
            "variables": [], "graphs": [{
                "name": "EventGraph", "path": "/Game/BP.BP:EventGraph",
                "kind": "event", "nodes": nodes}]}


def test_literal_branch_generates_both_arms():
    result = generate_supported_cpp(parse_snapshot(fixture()), "ATestActor")
    cpp = result["files"][1]["content"]
    assert "if (true)" in cpp
    assert 'TEXT("YES")' in cpp
    assert 'TEXT("NO")' in cpp


def test_false_literal():
    cpp = generate_supported_cpp(parse_snapshot(fixture("false")))["files"][1]["content"]
    assert "if (false)" in cpp


def test_invalid_bool_literal_rejected():
    with pytest.raises(UnsupportedNodeError, match="unresolved bool"):
        generate_supported_cpp(parse_snapshot(fixture("maybe")))


def test_linked_condition_rejected():
    raw = fixture()
    raw["graphs"][0]["nodes"][1]["pins"][1]["links"] = [
        {"node_id": "yes", "pin_id": "then"}]
    with pytest.raises(Exception):
        generate_supported_cpp(parse_snapshot(raw))


def test_disconnected_node_rejected():
    raw = fixture()
    raw["graphs"][0]["nodes"].append({
        "id": "orphan", "class": "/Script/BlueprintGraph.K2Node_CallFunction",
        "title": "Orphan", "function_name": "PrintString", "pins": []})
    with pytest.raises(UnsupportedNodeError, match="disconnected"):
        generate_supported_cpp(parse_snapshot(raw))


def test_branch_continuation_rejected():
    raw = fixture()
    raw["graphs"][0]["nodes"][2]["pins"][1]["links"] = [
        {"node_id": "no", "pin_id": "execute"}]
    raw["graphs"][0]["nodes"][3]["pins"][0]["links"].append(
        {"node_id": "yes", "pin_id": "then"})
    with pytest.raises(UnsupportedNodeError):
        generate_supported_cpp(parse_snapshot(raw))
