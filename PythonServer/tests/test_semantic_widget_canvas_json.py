"""Canvas/Overlay slots preserve positioning, precision and parent-specific contracts."""
import copy
import json
from pathlib import Path

import pytest

from app.semantic_json import SemanticJsonError, parse_semantic_json
from app.semantic_roundtrip import main, validate_file
from test_semantic_json import sample
from test_semantic_widget_panel_json import panel_sample

GOLDEN = Path(__file__).parent / "golden" / "semantic_widget_canvas.json"


def canvas_sample():
    return json.loads(GOLDEN.read_text(encoding="utf-8"))


def test_canvas_golden_cli(capsys):
    assert main(["--provider", "mock", str(GOLDEN)]) == 0
    assert json.loads(capsys.readouterr().out)["provider_status"] == "mocked"


@pytest.mark.parametrize("stretch", [False, True])
@pytest.mark.parametrize("functions", [0, 2])
def test_canvas_roundtrip_layout_and_functions(tmp_path, stretch, functions):
    data = canvas_sample()
    slot = data["widget_tree"]["nodes"][1]["canvas_slot"]
    if not stretch:
        slot.update(anchors_minimum=[0.5, 0.5], anchors_maximum=[0.5, 0.5], auto_size=True)
    for index in range(functions):
        function = copy.deepcopy(sample()["functions"][0])
        function.update(name=f"Run{index}", visibility="public", is_event_graph=False)
        data["functions"].append(function)
    parsed = parse_semantic_json(json.loads(json.dumps(data, ensure_ascii=False)))
    assert parsed == data
    assert parsed["widget_tree"]["nodes"][1]["canvas_slot"]["alignment"][0] == 0.1234567890123456
    assert parsed["widget_tree"]["nodes"][1]["canvas_slot"]["z_order"] == -7
    artifact = tmp_path / "canvas.semantic.json"
    artifact.write_text(json.dumps(parsed, ensure_ascii=False), encoding="utf-8")
    report = validate_file(artifact, check_provider=True)
    assert report["functions"] == functions and report["provider_status"] == "mocked"


@pytest.mark.parametrize("root_kind", ["vertical_box", "horizontal_box", "overlay", "canvas_panel"])
@pytest.mark.parametrize("nested_kind", ["vertical_box", "horizontal_box", "overlay", "canvas_panel"])
def test_mixed_panel_parent_slot_combinations(root_kind, nested_kind):
    data = panel_sample()
    template = data["widget_tree"]["nodes"][0]
    root = copy.deepcopy(template)
    root.update(kind=root_kind)
    child = copy.deepcopy(template)
    child.update(kind=nested_kind, name="Nested", parent_index=0)
    text = copy.deepcopy(template)
    text.update(kind="text_block", name="Message", parent_index=1, text="Nested label", text_culture_invariant=True)
    canvas_slot = canvas_sample()["widget_tree"]["nodes"][1]["canvas_slot"]
    if root_kind == "canvas_panel":
        child["canvas_slot"] = copy.deepcopy(canvas_slot)
    if nested_kind == "canvas_panel":
        text["canvas_slot"] = copy.deepcopy(canvas_slot)
    data["widget_tree"]["nodes"] = [root, child, text]
    assert parse_semantic_json(json.loads(json.dumps(data))) == data


@pytest.mark.parametrize("field", ["offsets", "anchors_minimum", "anchors_maximum", "alignment", "auto_size", "z_order"])
def test_canvas_slot_requires_complete_layout(field):
    data = canvas_sample()
    del data["widget_tree"]["nodes"][1]["canvas_slot"][field]
    with pytest.raises(SemanticJsonError, match="missing field"):
        parse_semantic_json(data)


@pytest.mark.parametrize("mutation", [
    {"offsets": [0, 0, 0]}, {"offsets": None}, {"offsets": [0, 0, True, 0]},
    {"offsets": [0, 0, float("nan"), 0]}, {"offsets": [0, 0, 0, float("inf")]},
    {"offsets": [0, 0, 1e39, 0]}, {"offsets": [0, 0, 10**400, 0]},
    {"anchors_minimum": [0]}, {"anchors_minimum": "0,0"}, {"anchors_minimum": [False, 0]},
    {"anchors_minimum": [float("nan"), 0]}, {"anchors_minimum": [10**400, 0]},
    {"anchors_maximum": [0, 0, 0]}, {"anchors_maximum": [0, float("inf")]},
    {"alignment": [0]}, {"alignment": ["0.5", 0]}, {"alignment": [0, float("nan")]},
    {"auto_size": 1}, {"auto_size": "false"}, {"auto_size": None},
    {"z_order": True}, {"z_order": 1.0}, {"z_order": "2"},
    {"z_order": -(2**31)-1}, {"z_order": 2**31},
])
def test_invalid_canvas_layout_rejected(mutation):
    data = canvas_sample()
    data["widget_tree"]["nodes"][1]["canvas_slot"].update(mutation)
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


@pytest.mark.parametrize("payload", [None, [], "canvas", {}])
def test_invalid_canvas_slot_objects(payload):
    data = canvas_sample()
    data["widget_tree"]["nodes"][1]["canvas_slot"] = payload
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


def test_canvas_parent_requires_slot_payload():
    data = canvas_sample()
    del data["widget_tree"]["nodes"][1]["canvas_slot"]
    with pytest.raises(SemanticJsonError, match="missing field"):
        parse_semantic_json(data)


@pytest.mark.parametrize("mutation", [
    {"padding": [1, 0, 0, 0]}, {"horizontal_alignment": "left"}, {"vertical_alignment": "top"},
    {"size_rule": "fill"}, {"size_value": 2},
])
def test_canvas_cannot_hide_box_or_overlay_slot_data(mutation):
    data = canvas_sample()
    data["widget_tree"]["nodes"][1].update(mutation)
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


@pytest.mark.parametrize("parent_kind", ["vertical_box", "horizontal_box", "overlay"])
@pytest.mark.parametrize("payload", [None, {}])
def test_noncanvas_parent_cannot_have_canvas_slot(parent_kind, payload):
    data = panel_sample()
    data["widget_tree"]["nodes"][0]["kind"] = parent_kind
    node = data["widget_tree"]["nodes"][1]
    node.update(size_rule="auto", size_value=1, canvas_slot=payload)
    with pytest.raises(SemanticJsonError, match="CanvasPanel parent"):
        parse_semantic_json(data)


def test_root_cannot_have_canvas_slot():
    data = canvas_sample()
    data["widget_tree"]["nodes"][0]["canvas_slot"] = copy.deepcopy(data["widget_tree"]["nodes"][1]["canvas_slot"])
    with pytest.raises(SemanticJsonError, match="CanvasPanel parent"):
        parse_semantic_json(data)


@pytest.mark.parametrize("mutation", [{"size_rule": "fill"}, {"size_value": 2}])
def test_overlay_cannot_have_box_size(mutation):
    data = panel_sample()
    data["widget_tree"]["nodes"][0]["kind"] = "overlay"
    data["widget_tree"]["nodes"][1].update(size_rule="auto", size_value=1)
    data["widget_tree"]["nodes"][1].update(mutation)
    with pytest.raises(SemanticJsonError, match="only supported for box"):
        parse_semantic_json(data)


@pytest.mark.parametrize("z_order", [-(2**31), 2**31-1])
def test_canvas_z_order_int32_boundaries(z_order):
    data = canvas_sample()
    data["widget_tree"]["nodes"][1]["canvas_slot"]["z_order"] = z_order
    assert parse_semantic_json(data) == data


def test_canvas_preserves_double_precision_and_finite_extended_coordinates():
    data = canvas_sample()
    slot = data["widget_tree"]["nodes"][1]["canvas_slot"]
    slot.update(anchors_minimum=[-1e100, 0.1234567890123456], anchors_maximum=[1e100, 1], alignment=[0.1234567890123456, -0.5])
    assert parse_semantic_json(json.loads(json.dumps(data))) == data
