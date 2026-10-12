import copy

import pytest

from app.semantic_json import SemanticJsonError, parse_semantic_json


def sample():
    return {
        "schema_version": 2,
        "format": "blueprint_semantic_ir",
        "producer": {"name": "BlueprintAIConverter", "engine_version": "5.7.4"},
        "source": {"asset_path": "/Game/Test.Test", "parent_class_path": "/Script/Engine.Actor",
                   "snapshot_schema_version": 1},
        "semantic_status": "partial",
        "functions": [{
            "name": "BeginPlay",
            "return_type": {"kind": "void", "type_path": "", "is_reference": False, "is_const": False},
            "blocks": [{
                "source_entry_node_id": "00000000-0000-0000-0000-000000000001",
                "statements": [{
                    "kind": "expression",
                    "source_node_id": "00000000-0000-0000-0000-000000000002",
                    "assignment_target": None,
                    "expression": {
                        "kind": "function_call",
                        "type": {"kind": "void", "type_path": "", "is_reference": False, "is_const": False},
                        "source_node_id": "00000000-0000-0000-0000-000000000002",
                        "source_pin_id": None,
                        "literal_value": "",
                        "variable_name": None,
                        "function_name": "PrintString",
                        "function_owner_path": "/Script/Engine.KismetSystemLibrary",
                        "arguments": [{
                            "kind": "literal",
                            "type": {"kind": "string", "type_path": "", "is_reference": False, "is_const": False},
                            "source_node_id": "00000000-0000-0000-0000-000000000002",
                            "source_pin_id": "00000000-0000-0000-0000-000000000003",
                            "literal_value": "",
                            "variable_name": None,
                            "function_name": None,
                            "function_owner_path": None,
                            "arguments": [],
                        }],
                    },
                }],
            }],
        }],
        "diagnostics": [],
    }


def test_variable_metadata_and_legacy_compatibility():
    old = sample()
    parse_semantic_json(old)
    data = sample()
    data["variables"] = [{
        "name": "Health", "guid": None,
        "type": {"kind": "int32", "type_path": "", "is_reference": False, "is_const": False},
        "pin_category": "int", "type_path": "", "property_flags": "0",
        "source": "blueprint_new_variables",
        "default_value_available": False, "default_value": None,
    }]
    assert parse_semantic_json(data) is data
    for invalid in ("", 0):
        bad = copy.deepcopy(data)
        bad["variables"][0]["default_value"] = invalid
        with pytest.raises(SemanticJsonError):
            parse_semantic_json(bad)


def test_valid_nested_expression():
    data = sample()
    assert parse_semantic_json(data) is data


@pytest.mark.parametrize("change", [
    lambda d: d.update(schema_version=1),
    lambda d: d.update(format="other"),
    lambda d: d.pop("functions"),
    lambda d: d["functions"][0]["return_type"].update(kind="unknown"),
    lambda d: d["functions"][0]["blocks"][0].update(source_entry_node_id="bad"),
    lambda d: d["functions"][0]["blocks"][0]["statements"][0]["expression"].pop("arguments"),
    lambda d: d["functions"].append(copy.deepcopy(d["functions"][0])),
])
def test_invalid_input(change):
    data = sample()
    change(data)
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


def test_error_diagnostic_not_complete():
    data = sample()
    data["semantic_status"] = "complete"
    data["diagnostics"].append({
        "severity": "error", "code": "UNSUPPORTED_NODE", "graph_name": "EventGraph",
        "node_id": None, "pin_id": None, "message": "unsupported",
    })
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


def test_diagnostics_allow_unsupported():
    data = sample()
    data["semantic_status"] = "unsupported"
    data["diagnostics"].append({
        "severity": "error", "code": "SEMANTIC_LOWERING_ERROR", "graph_name": "",
        "node_id": None, "pin_id": None, "message": "unsupported",
    })
    parse_semantic_json(data)


@pytest.mark.parametrize("kind,category", [
    ("bool", "bool"), ("int32", "int"), ("int64", "int64"),
    ("float", "real"), ("double", "real"),
    ("string", "string"), ("name", "name"), ("text", "text"),
])
def test_variable_supported_scalar_kinds(kind, category):
    data = sample()
    data["variables"] = [{
        "name": "Value", "guid": None,
        "type": {"kind": kind, "type_path": "", "is_reference": False, "is_const": False},
        "pin_category": category, "type_path": "", "property_flags": "0",
        "source": "blueprint_new_variables",
        "default_value_available": False, "default_value": None,
    }]
    assert parse_semantic_json(data) is data


def test_variable_duplicate_and_invalid_type():
    data = sample()
    data["variables"] = [{
        "name": "Value", "guid": None,
        "type": {"kind": "unresolved", "type_path": "", "is_reference": False, "is_const": False},
        "pin_category": "unknown", "type_path": "", "property_flags": "0",
        "source": "blueprint_new_variables",
        "default_value_available": False, "default_value": None,
    }]
    assert parse_semantic_json(data) is data
    duplicate = copy.deepcopy(data)
    duplicate["variables"].append(copy.deepcopy(duplicate["variables"][0]))
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(duplicate)
    invalid = copy.deepcopy(data)
    invalid["variables"][0]["type"]["kind"] = "not_a_kind"
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(invalid)


def test_variable_explicit_empty_default():
    data = sample()
    data["variables"] = [{
        "name": "Title", "guid": None,
        "type": {"kind": "string", "type_path": "", "is_reference": False, "is_const": False},
        "pin_category": "string", "type_path": "", "property_flags": "0",
        "source": "blueprint_new_variables",
        "default_value_available": True, "default_value": "",
    }]
    assert parse_semantic_json(data) is data

