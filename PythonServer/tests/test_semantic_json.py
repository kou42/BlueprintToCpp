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
