"""Widget Tree v2 contract: preserve layout payload alongside regular functions."""
import copy
import json
from pathlib import Path

import pytest

from app.semantic_json import SemanticJsonError, parse_semantic_json
from app.semantic_roundtrip import main, validate_file
from test_semantic_json import sample

GOLDEN = Path(__file__).parent / "golden" / "semantic_widget_tree.json"


def widget_sample(kind="text_block"):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    if kind == "empty":
        data["widget_tree"].update(kind="empty", root_name=None, text="", text_culture_invariant=False)
    return data


def test_widget_golden_through_public_cli(capsys):
    assert main(["--provider", "mock", str(GOLDEN)]) == 0
    report = json.loads(capsys.readouterr().out)
    assert report["status"] == "ok"
    assert report["functions"] == 0
    assert report["provider_status"] == "mocked"


@pytest.mark.parametrize("kind", ["empty", "text_block"])
@pytest.mark.parametrize("function_count", [0, 1, 2])
def test_widget_tree_roundtrip_and_provider(tmp_path, kind, function_count):
    data = widget_sample(kind)
    for i in range(function_count):
        function = copy.deepcopy(sample()["functions"][0])
        function.update(name=f"Run{i}", visibility="public", is_event_graph=False)
        data["functions"].append(function)
    parsed = parse_semantic_json(json.loads(json.dumps(data, ensure_ascii=False)))
    assert parsed == data
    artifact = tmp_path / "widget.semantic.json"
    artifact.write_text(json.dumps(parsed, ensure_ascii=False), encoding="utf-8")
    result = validate_file(artifact, check_provider=True)
    assert result["functions"] == function_count
    assert result["provider_status"] == "mocked"


@pytest.mark.parametrize("source", ["Localized greeting", ""])
def test_localized_widget_preserves_identity_even_with_empty_source(source):
    data = widget_sample()
    tree = data["widget_tree"]
    tree.update(text=source, text_culture_invariant=False,
                text_localization_identity_available=True, text_namespace="WidgetConversion",
                text_key="Greeting", text_source=source)
    assert parse_semantic_json(json.loads(json.dumps(data)))["widget_tree"] == tree


@pytest.mark.parametrize("field", ["kind", "root_name", "text", "text_culture_invariant",
                                  "text_localization_identity_available", "text_namespace", "text_key", "text_source"])
def test_widget_tree_requires_all_payload_fields(field):
    data = widget_sample()
    del data["widget_tree"][field]
    with pytest.raises(SemanticJsonError, match="missing field"):
        parse_semantic_json(data)


@pytest.mark.parametrize("mutation", [
    {"kind": "canvas"}, {"kind": None}, {"root_name": None}, {"root_name": "Bad\"Name"},
    {"root_name": ""}, {"text": 5}, {"text_culture_invariant": 1},
    {"text_localization_identity_available": "false"}, {"text_namespace": None},
    {"text_key": []}, {"text_source": False},
    {"text_culture_invariant": False},
    {"text_localization_identity_available": True, "text_key": "Greeting"},
    {"text_culture_invariant": False, "text_localization_identity_available": True},
    {"text_namespace": "Undeclared"}, {"text_key": "Undeclared"}, {"text_source": "Undeclared"},
])
def test_invalid_widget_payload_rejected(mutation):
    data = widget_sample()
    data["widget_tree"].update(mutation)
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


@pytest.mark.parametrize("field,value", [("root_name", "Hidden"), ("text", "Hidden"),
    ("text_culture_invariant", True), ("text_localization_identity_available", True),
    ("text_namespace", "Hidden"), ("text_key", "Hidden"), ("text_source", "Hidden")])
def test_empty_tree_cannot_hide_text_payload(field, value):
    data = widget_sample("empty")
    data["widget_tree"][field] = value
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


@pytest.mark.parametrize("parent", ["/Script/CoreUObject.Object", "/Script/Engine.Actor",
                                   "/Script/Engine.AnimInstance", "/Script/MyPlugin.CustomWidget"])
def test_widget_payload_requires_audited_direct_parent(parent):
    data = widget_sample()
    data["source"]["parent_class_path"] = parent
    with pytest.raises(SemanticJsonError, match="direct UserWidget"):
        parse_semantic_json(data)


def test_legacy_v2_does_not_require_widget_payload():
    data = sample()
    assert "widget_tree" not in parse_semantic_json(data)
