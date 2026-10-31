"""TypeToJson's additive v2 fields, including unresolved container roots."""
import copy
import json
from pathlib import Path

import pytest

from app.semantic_json import SemanticJsonError, parse_semantic_json
from app.semantic_roundtrip import validate_file

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"


def semantic_type(kind="unresolved"):
    return dict(kind=kind, type_path="", is_reference=False, is_const=False,
                container_kind="none", inner_type="", key_type="", value_type="",
                element_type_info=None, key_type_info=None, value_type_info=None)


def nested_type():
    # Mirrors SerializeV2's Map<FName, Set<int32>> fixture. Container Kind is unresolved.
    result = semantic_type()
    result.update(container_kind="map", key_type="FName", value_type="TSet<int32>",
                  key_type_info=semantic_type("name"), value_type_info=semantic_type())
    result["value_type_info"].update(container_kind="set", inner_type="int32",
                                   element_type_info=semantic_type("int32"))
    return result


def document_with_type(location, value):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    function = data["functions"][0]
    statement = function["blocks"][0]["statements"][0]
    if location == "variable":
        data["variables"][0]["type"] = value
    elif location == "return":
        function["return_type"] = value
    elif location == "parameter":
        function["parameters"] = [dict(name="Lookup", type=value, is_out=False, is_return=False)]
    elif location == "expression":
        statement["expression"]["type"] = value
    elif location == "argument":
        statement["expression"]["arguments"][0]["type"] = value
    elif location == "loop_element":
        expression = copy.deepcopy(statement["expression"])
        statement.update(kind="for_each_loop", loop_index_variable="Index",
                         loop_body_statements=[], completed_statements=[],
                         loop_array=expression, loop_element_variable="Element",
                         loop_element_type=value)
    return data


LOCATIONS = ("variable", "return", "parameter", "expression", "argument", "loop_element")


@pytest.mark.parametrize("location", LOCATIONS)
def test_nested_types_survive_json_and_provider_boundary(tmp_path, location):
    data = document_with_type(location, nested_type())
    assert parse_semantic_json(json.loads(json.dumps(data))) == data
    target = tmp_path / "types.semantic.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    assert validate_file(target, check_provider=True)["provider_status"] == "mocked"


@pytest.mark.parametrize("kind", ["enum", "byte", "delegate", "multicast_delegate"])
def test_additional_cpp_type_kinds_roundtrip(tmp_path, kind):
    value = semantic_type(kind)
    if kind != "byte":
        value["type_path"] = "/Script/Test.ExportedType"
    data = document_with_type("parameter", value)
    target = tmp_path / "kinds.semantic.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    assert parse_semantic_json(json.loads(target.read_text(encoding="utf-8"))) == data
    assert validate_file(target, check_provider=True)["provider_status"] == "mocked"


@pytest.mark.parametrize("container", ["none", "array", "set", "map"])
def test_unresolved_types_and_null_children_are_preserved(container):
    value = semantic_type()
    value["container_kind"] = container
    data = document_with_type("variable", value)
    assert parse_semantic_json(data) is data


@pytest.mark.parametrize("location", LOCATIONS)
def test_nested_invalid_types_report_the_child_path(location):
    value = nested_type()
    value["value_type_info"]["element_type_info"]["is_reference"] = "false"
    data = document_with_type(location, value)
    with pytest.raises(SemanticJsonError, match=r"value_type_info.element_type_info.is_reference"):
        parse_semantic_json(data)


@pytest.mark.parametrize("field", ["element_type_info", "key_type_info", "value_type_info"])
@pytest.mark.parametrize("invalid", [[], 0, "int32", {}, {"kind": "int32"}])
def test_malformed_type_children_are_rejected(field, invalid):
    value = semantic_type()
    value[field] = invalid
    with pytest.raises(SemanticJsonError, match=field):
        parse_semantic_json(document_with_type("parameter", value))


@pytest.mark.parametrize("field,invalid", [
    ("container_kind", "list"), ("container_kind", None), ("container_kind", 1),
    ("inner_type", []), ("key_type", None), ("value_type", 1),
    ("kind", "future_type"),
])
def test_invalid_type_metadata_is_rejected(field, invalid):
    value = semantic_type()
    value[field] = invalid
    with pytest.raises(SemanticJsonError, match=field):
        parse_semantic_json(document_with_type("parameter", value))


def test_legacy_type_strings_without_recursive_fields_remain_valid():
    value = dict(kind="unresolved", type_path="", is_reference=False, is_const=False,
                 container_kind="map", key_type="FName", value_type="TSet<int32>")
    data = document_with_type("parameter", value)
    assert parse_semantic_json(data) is data


@pytest.mark.parametrize("child_field", ["element_type_info", "key_type_info", "value_type_info"])
@pytest.mark.parametrize("depth", [64, 65])
def test_type_nesting_limit(child_field, depth):
    root = semantic_type()
    current = root
    for _ in range(depth):
        current[child_field] = semantic_type()
        current = current[child_field]
    data = document_with_type("parameter", root)
    if depth == 64:
        assert parse_semantic_json(data) is data
    else:
        with pytest.raises(SemanticJsonError, match="type nesting exceeds 64"):
            parse_semantic_json(data)
