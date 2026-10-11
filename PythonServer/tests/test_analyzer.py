import copy
import pytest

from app.schemas import SnapshotError, parse_snapshot
from app.graph_analyzer import analyze_snapshot
from app.ir_builder import build_ir


def pin(pid, direction, category, links=()):
    return {
        "id": pid, "name": pid, "direction": direction,
        "type": {"category": category, "subcategory": ""},
        "default_value": "", "links": [
            {"node_id": n, "pin_id": p} for n, p in links
        ],
    }


def fixture():
    return {
        "schema_version": 1,
        "engine_version": "5.7.0",
        "asset_path": "/Game/BP_Test.BP_Test",
        "parent_class": "/Script/Engine.Actor",
        "variables": [{"name": "Speed", "type": {"category": "real"}}],
        "graphs": [{
            "name": "EventGraph",
            "path": "/Game/BP_Test.BP_Test:EventGraph",
            "kind": "event",
            "nodes": [
                {"id": "tick", "class": "K2Node_Event", "title": "Event Tick",
                 "pins": [pin("out", "output", "exec", [("branch", "in")])]},
                {"id": "branch", "class": "K2Node_IfThenElse", "title": "Branch",
                 "pins": [pin("in", "input", "exec", [("tick", "out")]),
                          pin("cond", "input", "bool", [("var", "value")])]},
                {"id": "var", "class": "K2Node_VariableGet", "title": "Get",
                 "pins": [pin("value", "output", "bool", [("branch", "cond")])]},
            ],
        }],
    }


def test_parse_and_analyze():
    snapshot = parse_snapshot(fixture())
    graph, = analyze_snapshot(snapshot)
    assert graph.node_count == 3
    assert len(graph.exec_edges) == 1
    assert len(graph.data_edges) == 1
    assert graph.entry_nodes == ("tick",)
    ir = build_ir(snapshot)
    assert ir["semantic_status"] == "graph_only"
    assert ir["graphs"][0]["exec_edges"][0]["source_node"] == "tick"


def test_missing_target_pin():
    raw = fixture()
    raw["graphs"][0]["nodes"][0]["pins"][0]["links"][0]["pin_id"] = "missing"
    with pytest.raises(SnapshotError, match="dangling link"):
        analyze_snapshot(parse_snapshot(raw))


def test_nonreciprocal_link():
    raw = fixture()
    raw["graphs"][0]["nodes"][1]["pins"][0]["links"] = []
    with pytest.raises(SnapshotError, match="non-reciprocal"):
        analyze_snapshot(parse_snapshot(raw))


def test_type_mismatch():
    raw = fixture()
    raw["graphs"][0]["nodes"][2]["pins"][0]["type"]["category"] = "int"
    with pytest.raises(SnapshotError, match="incompatible"):
        analyze_snapshot(parse_snapshot(raw))


def test_duplicate_node_id():
    raw = fixture()
    raw["graphs"][0]["nodes"][2]["id"] = "tick"
    with pytest.raises(SnapshotError, match="duplicate node"):
        parse_snapshot(raw)


def test_unsupported_schema():
    raw = fixture()
    raw["schema_version"] = 2
    with pytest.raises(SnapshotError, match="schema_version"):
        parse_snapshot(raw)


def test_graph_cycle_is_not_rejected():
    raw = fixture()
    graph = raw["graphs"][0]
    graph["nodes"][1]["pins"].append(pin("next", "output", "exec", [("tick", "back")]))
    graph["nodes"][0]["pins"].append(pin("back", "input", "exec", [("branch", "next")]))
    result, = analyze_snapshot(parse_snapshot(raw))
    assert len(result.exec_edges) == 2
    assert result.entry_nodes == ()


def test_separate_graphs_can_reuse_node_ids():
    raw = fixture()
    other = copy.deepcopy(raw["graphs"][0])
    other["path"] = "/Game/BP_Test.BP_Test:AnotherGraph"
    raw["graphs"].append(other)
    assert len(analyze_snapshot(parse_snapshot(raw))) == 2
