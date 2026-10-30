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
                "block_id": "00000000-0000-0000-0000-000000000004",
                "source_entry_node_id": "00000000-0000-0000-0000-000000000001",
                "terminator": "end",
                "successor_block_ids": [],
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


def test_block_control_flow_references():
    data = sample()
    first = data["functions"][0]["blocks"][0]
    second = copy.deepcopy(first)
    second["block_id"] = "00000000-0000-0000-0000-000000000005"
    second["source_entry_node_id"] = "00000000-0000-0000-0000-000000000006"
    second["terminator"] = "end"
    first["terminator"] = "fallthrough"
    first["successor_block_ids"] = [second["block_id"]]
    data["functions"][0]["blocks"].append(second)
    assert parse_semantic_json(data) is data

    unknown = copy.deepcopy(data)
    unknown["functions"][0]["blocks"][0]["successor_block_ids"] = [
        "00000000-0000-0000-0000-000000000007"]
    with pytest.raises(SemanticJsonError, match="unknown block ID"):
        parse_semantic_json(unknown)

    duplicate = copy.deepcopy(data)
    duplicate["functions"][0]["blocks"][1]["block_id"] = first["block_id"]
    with pytest.raises(SemanticJsonError, match="duplicate block ID"):
        parse_semantic_json(duplicate)


@pytest.mark.parametrize("change", [
    lambda d: d.update(schema_version=1),
    lambda d: d.update(format="other"),
    lambda d: d.pop("functions"),
    lambda d: d["functions"][0]["return_type"].update(kind="unknown"),
    lambda d: d["functions"][0]["blocks"][0].update(source_entry_node_id="bad"),
    lambda d: d["functions"][0]["blocks"][0].update(terminator="unknown"),
    lambda d: d["functions"][0]["blocks"][0].update(successor_block_ids=["bad"]),
    lambda d: d["functions"][0]["blocks"][0].pop("block_id"),
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



def test_cdo_default_provenance_consistency():
    data = sample()
    data["variables"] = [{
        "name": "Health", "guid": None,
        "type": {"kind": "int32", "type_path": "", "is_reference": False, "is_const": False},
        "pin_category": "int", "type_path": "", "property_flags": "0",
        "source": "blueprint_new_variables",
        "default_value_source": "cdo",
        "default_value_available": True, "default_value": "0",
    }]
    assert parse_semantic_json(data) is data
    for source, available in [("unavailable", True), ("cdo", False), ("invalid", True)]:
        bad = copy.deepcopy(data)
        bad["variables"][0]["default_value_source"] = source
        bad["variables"][0]["default_value_available"] = available
        if not available:
            bad["variables"][0]["default_value"] = None
        with pytest.raises(SemanticJsonError):
            parse_semantic_json(bad)


def test_reflected_function_signature_and_legacy():
    data = sample()
    # 旧形式はシグネチャの追加なしでも引き続き受理する。
    assert parse_semantic_json(data) is data
    signature = {
        "signature_source": "generated_class_reflection",
        "owner_class_path": "/Game/BP_Test.BP_Test_C",
        "parameters": [
            {"name": "Amount", "type": {"kind": "int32", "type_path": "",
             "is_reference": False, "is_const": False}, "is_out": False, "is_return": False},
            {"name": "ReturnValue", "type": {"kind": "bool", "type_path": "",
             "is_reference": False, "is_const": False}, "is_out": True, "is_return": True},
        ],
    }
    data["functions"][0].update(signature)
    assert parse_semantic_json(data) is data
    bad = copy.deepcopy(data)
    bad["functions"][0]["parameters"].append(copy.deepcopy(signature["parameters"][0]))
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(bad)
    bad = copy.deepcopy(data)
    bad["functions"][0]["parameters"][0]["type"]["kind"] = "invalid"
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(bad)
    bad = copy.deepcopy(data)
    del bad["functions"][0]["owner_class_path"]
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(bad)


def test_graph_trace_exec_data_edges_and_legacy():
    data = sample()
    assert parse_semantic_json(data) is data
    n1 = "00000000-0000-0000-0000-000000000011"
    n2 = "00000000-0000-0000-0000-000000000012"
    p1 = "00000000-0000-0000-0000-000000000021"
    p2 = "00000000-0000-0000-0000-000000000022"
    edge = {"source_node_id": n1, "source_pin_id": p1,
            "target_node_id": n2, "target_pin_id": p2}
    data["graph_traces"] = [{
        "name": "EventGraph", "path": "/Game/Test.EventGraph", "kind": "ubergraph",
        "node_ids": [n1, n2], "pin_ids": [p1, p2],
        "exec_edges": [edge], "data_edges": [copy.deepcopy(edge)],
    }]
    assert parse_semantic_json(data) is data
    bad = copy.deepcopy(data)
    bad["graph_traces"][0]["data_edges"][0]["target_pin_id"] = (
        "00000000-0000-0000-0000-000000000099"
    )
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(bad)
    bad = copy.deepcopy(data)
    bad["graph_traces"][0]["node_ids"].append(n1)
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(bad)


@pytest.mark.parametrize("visibility", ["", "PUBLIC", "internal", None, True, 1, [], {}])
def test_invalid_function_visibility_is_rejected(visibility):
    data = sample()
    data["functions"][0]["visibility"] = visibility
    with pytest.raises(SemanticJsonError, match=r"functions\[0\].visibility"):
        parse_semantic_json(data)
