"""Nested box layouts preserve parent/child ordering and complete box-slot data."""
import copy
import json
from pathlib import Path

import pytest

from app.semantic_json import SemanticJsonError, parse_semantic_json
from app.semantic_roundtrip import main, validate_file
from test_semantic_json import sample
from test_semantic_widget_json import widget_sample

GOLDEN = Path(__file__).parent / "golden" / "semantic_widget_panel.json"


def panel_sample():
    return json.loads(GOLDEN.read_text(encoding="utf-8"))


def test_panel_golden_through_cli(capsys):
    assert main(["--provider", "mock", str(GOLDEN)]) == 0
    assert json.loads(capsys.readouterr().out)["provider_status"] == "mocked"


@pytest.mark.parametrize("root_kind", ["vertical_box", "horizontal_box"])
@pytest.mark.parametrize("functions", [0, 1, 2])
def test_panel_layout_roundtrip_and_provider(tmp_path, root_kind, functions):
    data = panel_sample()
    data["widget_tree"]["nodes"][0]["kind"] = root_kind
    for index in range(functions):
        function = copy.deepcopy(sample()["functions"][0])
        function.update(name=f"Run{index}", visibility="public", is_event_graph=False)
        data["functions"].append(function)
    parsed = parse_semantic_json(json.loads(json.dumps(data, ensure_ascii=False)))
    assert parsed == data
    nodes = parsed["widget_tree"]["nodes"]
    assert [node["parent_index"] for node in nodes] == [-1, 0, 0, 2]
    assert nodes[1]["padding"] == [1, 2, 3, 4]
    assert nodes[1]["size_rule"] == "fill" and nodes[1]["size_value"] == 2.5
    assert nodes[3]["text_key"] == "Secondary"
    artifact = tmp_path / "layout.semantic.json"
    artifact.write_text(json.dumps(parsed), encoding="utf-8")
    report = validate_file(artifact, check_provider=True)
    assert report["functions"] == functions
    assert report["provider_status"] == "mocked"


@pytest.mark.parametrize("field", ["kind", "name", "parent_index", "padding", "horizontal_alignment",
    "vertical_alignment", "size_rule", "size_value", "text", "text_culture_invariant",
    "text_localization_identity_available", "text_namespace", "text_key", "text_source"])
def test_panel_requires_complete_nodes(field):
    data = panel_sample()
    del data["widget_tree"]["nodes"][1][field]
    with pytest.raises(SemanticJsonError, match="missing field"):
        parse_semantic_json(data)


@pytest.mark.parametrize("index,mutation", [
    (0, {"parent_index": 0}), (0, {"parent_index": None}), (0, {"name": "OtherRoot"}),
    (0, {"kind": "text_block"}), (0, {"padding": [1, 0, 0, 0]}),
    (0, {"size_rule": "fill"}), (0, {"size_value": 2}), (0, {"horizontal_alignment": "left"}),
    (0, {"vertical_alignment": "top"}),
    (1, {"name": "layout"}), (1, {"name": "None"}), (1, {"name": "Bad\"Name"}),
    (1, {"name": None}), (1, {"name": ""}), (1, {"kind": "button"}),
    (1, {"parent_index": 1}), (1, {"parent_index": 2}), (1, {"parent_index": -1}),
    (1, {"parent_index": True}), (1, {"parent_index": 0.0}), (3, {"parent_index": 1}),
    (1, {"padding": [0, 1, 2]}), (1, {"padding": "0,0,0,0"}), (1, {"padding": [0, 0, True, 0]}),
    (1, {"padding": [0, float("nan"), 0, 0]}), (1, {"padding": [0, 0, 0, float("inf")]}),
    (1, {"padding": [1e39, 0, 0, 0]}), (1, {"padding": [10**400, 0, 0, 0]}),
    (1, {"horizontal_alignment": "top"}), (1, {"vertical_alignment": "left"}),
    (1, {"size_rule": "stretch"}), (1, {"size_value": -1}), (1, {"size_value": True}),
    (1, {"size_value": float("inf")}), (1, {"size_value": "2.5"}),
    (2, {"text": "Hidden text", "text_culture_invariant": True}),
    (3, {"text_key": ""}), (3, {"text_culture_invariant": True}),
])
def test_invalid_node_and_layout_rejected(index, mutation):
    data = panel_sample()
    data["widget_tree"]["nodes"][index].update(mutation)
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


@pytest.mark.parametrize("mutate", [
    lambda tree: tree.pop("nodes"), lambda tree: tree.update(nodes=[]),
    lambda tree: tree.update(nodes=None), lambda tree: tree["nodes"].append(None),
    lambda tree: tree.update(root_name="Different"),
    lambda tree: tree.update(text="Hidden", text_culture_invariant=True),
])
def test_invalid_panel_tree_rejected(mutate):
    data = panel_sample()
    mutate(data["widget_tree"])
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


@pytest.mark.parametrize("kind", ["empty", "text_block"])
def test_legacy_tree_cannot_hide_panel_nodes(kind):
    data = widget_sample(kind)
    data["widget_tree"]["nodes"] = panel_sample()["widget_tree"]["nodes"]
    with pytest.raises(SemanticJsonError, match="hide panel nodes"):
        parse_semantic_json(data)


@pytest.mark.parametrize("depth,valid", [(64, True), (65, False)])
def test_panel_nesting_limit(depth, valid):
    data = panel_sample()
    root = data["widget_tree"]["nodes"][0]
    data["widget_tree"]["nodes"] = [root]
    for index in range(1, depth + 1):
        node = copy.deepcopy(root)
        node.update(name=f"Panel{index}", parent_index=index - 1)
        data["widget_tree"]["nodes"].append(node)
    if valid:
        assert parse_semantic_json(data) == data
    else:
        with pytest.raises(SemanticJsonError, match="nesting exceeds"):
            parse_semantic_json(data)


@pytest.mark.parametrize("count,valid", [(256, True), (257, False)])
def test_panel_node_limit(count, valid):
    data = panel_sample()
    root = data["widget_tree"]["nodes"][0]
    data["widget_tree"]["nodes"] = [root]
    for index in range(1, count):
        node = copy.deepcopy(root)
        node.update(name=f"Panel{index}", parent_index=0)
        data["widget_tree"]["nodes"].append(node)
    if valid:
        assert parse_semantic_json(data) == data
    else:
        with pytest.raises(SemanticJsonError, match="node count"):
            parse_semantic_json(data)
