import pytest

from app.schemas import parse_snapshot
from app.code_generator import generate_cpp, GenerationError
from app.node_registry import UnsupportedNodeError, NodeConverterRegistry
from test_analyzer import fixture, pin


def begin_play_snapshot():
    raw = fixture()
    raw["variables"] = []
    raw["graphs"][0]["nodes"] = [{
        "id": "begin", "class": "/Script/BlueprintGraph.K2Node_Event",
        "title": "Event BeginPlay", "function_name": "ReceiveBeginPlay",
        "pins": [pin("then", "output", "exec")],
    }]
    return raw


def test_empty_begin_play_generates_cpp():
    result = generate_cpp(parse_snapshot(begin_play_snapshot()), "AMyActor")
    assert result["files"][0]["path"] == "MyActor.h"
    assert '#include "MyActor.generated.h"' in result["files"][0]["content"]
    assert "Super::BeginPlay();" in result["files"][1]["content"]


def test_literal_print_string_chain():
    raw = begin_play_snapshot()
    event = raw["graphs"][0]["nodes"][0]
    event["pins"][0]["links"] = [{"node_id": "print", "pin_id": "execute"}]
    raw["graphs"][0]["nodes"].append({
        "id": "print", "class": "/Script/BlueprintGraph.K2Node_CallFunction",
        "title": "Print String", "function_name": "PrintString",
        "pins": [
            pin("execute", "input", "exec", [("begin", "then")]),
            pin("then", "output", "exec"),
            pin("InString", "input", "string"),
        ],
    })
    raw["graphs"][0]["nodes"][1]["pins"][2]["default_value"] = 'Hello "UE"'
    cpp = generate_cpp(parse_snapshot(raw))["files"][1]["content"]
    assert 'TEXT("Hello \\"UE\\"")' in cpp


def test_unknown_event_rejected():
    raw = begin_play_snapshot()
    raw["graphs"][0]["nodes"][0]["function_name"] = "ReceiveTick"
    with pytest.raises(UnsupportedNodeError):
        generate_cpp(parse_snapshot(raw))


def test_unknown_node_rejected():
    raw = begin_play_snapshot()
    raw["graphs"][0]["nodes"].append({
        "id": "branch", "class": "/Script/BlueprintGraph.K2Node_IfThenElse",
        "title": "Branch", "pins": [],
    })
    with pytest.raises(UnsupportedNodeError):
        generate_cpp(parse_snapshot(raw))


def test_invalid_class_name_rejected():
    with pytest.raises(GenerationError):
        generate_cpp(parse_snapshot(begin_play_snapshot()), "A../../Danger")


def test_registry_fails_closed():
    registry = NodeConverterRegistry()
    with pytest.raises(UnsupportedNodeError):
        registry.convert(type("Context", (), {"node": {"class": "Unknown"}})())


def test_disconnected_tick_does_not_block_begin_play():
    raw = begin_play_snapshot()
    raw["graphs"][0]["nodes"].append({
        "id": "tick", "class": "/Script/BlueprintGraph.K2Node_Event",
        "title": "Event Tick", "function_name": "ReceiveTick",
        "pins": [pin("then", "output", "exec")],
    })
    cpp = generate_cpp(parse_snapshot(raw))["files"][1]["content"]
    assert "Super::BeginPlay();" in cpp


def test_additional_empty_function_graph_is_analyzed():
    raw = begin_play_snapshot()
    raw["graphs"].append({
        "name": "UnusedFunction", "path": "/Game/BP_Test.BP_Test:UnusedFunction",
        "kind": "function", "nodes": [],
    })
    result = generate_cpp(parse_snapshot(raw))
    assert result["status"] == "generated"
