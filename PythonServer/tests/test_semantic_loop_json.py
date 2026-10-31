"""Validate the existing v2 loop wire format, without claiming C++ executability."""
import copy
import json
from pathlib import Path

import pytest

from app.semantic_json import SemanticJsonError, parse_semantic_json
from app.semantic_roundtrip import validate_file

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"


def loop_document(kind="for_loop", shape="tree"):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    function = data["functions"][0]
    function.update(name="RunLoops", visibility="public", is_event_graph=False)
    block = function["blocks"][0]
    call = copy.deepcopy(block["statements"][0])
    call["expression"].update(function_name="SetSubtitlesEnabled",
                              function_owner_path="/Script/Engine.GameplayStatics")

    def expression(type_kind, value=""):
        result = copy.deepcopy(call["expression"]["arguments"][0])
        result["type"].update(kind=type_kind)
        result["literal_value"] = value
        return result

    call["expression"]["arguments"] = [expression("bool", "true")]
    completed = copy.deepcopy(call)
    completed["expression"]["arguments"][0]["literal_value"] = "false"
    blank = expression("unresolved")
    brk = {"kind": "break", "source_node_id": call["source_node_id"],
           "assignment_target": None, "expression": copy.deepcopy(blank),
           "then_statements": [], "else_statements": [],
           "loop_index_variable": None, "loop_first_index": copy.deepcopy(blank),
           "loop_last_index": copy.deepcopy(blank), "loop_body_statements": [],
           "completed_statements": []}
    branch = copy.deepcopy(brk)
    branch.update(kind="branch", expression=expression("bool", "false"), then_statements=[brk])
    loop = copy.deepcopy(brk)
    loop.update(kind=kind, loop_index_variable="Index", loop_first_index=expression("int32", "0"),
                loop_last_index=expression("int32", "2"), loop_body_statements=[call, branch],
                completed_statements=[completed])
    if kind == "for_each_loop":
        array = expression("int32")
        element_type = copy.deepcopy(array["type"])
        array.update(kind="variable_reference", variable_name="Numbers")
        array["type"].update(container_kind="array", inner_type="int32",
                             element_type_info=copy.deepcopy(element_type))
        loop.update(loop_array=array, loop_element_variable="Element", loop_element_type=element_type,
                    loop_first_index=copy.deepcopy(blank), loop_last_index=copy.deepcopy(blank))
        variable = copy.deepcopy(data["variables"][0])
        variable.update(name="Numbers", type=copy.deepcopy(array["type"]),
                        default_value_source="unavailable", default_value_available=False, default_value=None)
        data["variables"] = [variable]
    if shape == "nested":
        inner = copy.deepcopy(loop)
        inner["loop_index_variable"] = "InnerIndex"
        loop["loop_body_statements"] = [inner]
    block["statements"] = [loop]
    if shape == "cfg":
        ids = [f"00000000-0000-0000-0000-{number:012d}" for number in (41, 42, 43)]
        block.update(block_id=ids[0], terminator="loop", successor_block_ids=ids[1:])
        function["blocks"].extend([
            {"block_id": ids[1], "source_entry_node_id": block["source_entry_node_id"],
             "terminator": "fallthrough", "successor_block_ids": [ids[0]],
             "statements": loop["loop_body_statements"]},
            {"block_id": ids[2], "source_entry_node_id": block["source_entry_node_id"],
             "terminator": "end", "successor_block_ids": [],
             "statements": loop["completed_statements"]},
        ])
        loop.update(loop_body_statements=[], completed_statements=[])
    return data


def first_loop(data):
    return data["functions"][0]["blocks"][0]["statements"][0]


@pytest.mark.parametrize("kind", ["for_loop", "for_each_loop"])
@pytest.mark.parametrize("shape", ["tree", "nested", "cfg"])
def test_loop_body_completed_break_and_cfg_survive_roundtrip(tmp_path, kind, shape):
    data = loop_document(kind, shape)
    assert parse_semantic_json(json.loads(json.dumps(data))) == data
    target = tmp_path / "loop.semantic.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    report = validate_file(target, check_provider=True)
    assert report["functions"] == 1
    assert report["provider_status"] == "mocked"


@pytest.mark.parametrize("kind, field", [
    ("for_loop", "loop_index_variable"), ("for_loop", "loop_first_index"),
    ("for_loop", "loop_last_index"), ("for_loop", "loop_body_statements"),
    ("for_loop", "completed_statements"), ("for_each_loop", "loop_array"),
    ("for_each_loop", "loop_element_variable"), ("for_each_loop", "loop_element_type"),
])
def test_missing_loop_contract_fields_are_rejected(kind, field):
    data = loop_document(kind)
    first_loop(data).pop(field)
    with pytest.raises(SemanticJsonError, match=field):
        parse_semantic_json(data)


@pytest.mark.parametrize("mutate", [
    lambda loop: loop.update(loop_body_statements=None),
    lambda loop: loop.update(completed_statements={}),
    lambda loop: loop.update(loop_index_variable=True),
    lambda loop: loop["loop_first_index"].update(source_pin_id="invalid"),
    lambda loop: loop["loop_last_index"].update(arguments=[{}]),
    lambda loop: loop["completed_statements"][0].update(kind="unknown"),
    lambda loop: loop["loop_body_statements"][1]["then_statements"][0].update(source_node_id="invalid"),
])
def test_invalid_bounds_children_and_completed_are_rejected(mutate):
    data = loop_document()
    mutate(first_loop(data))
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


@pytest.mark.parametrize("mutate", [
    lambda loop: loop.update(loop_element_variable=12),
    lambda loop: loop["loop_array"].update(source_pin_id="bad"),
    lambda loop: loop["loop_array"]["type"].update(container_kind="unknown"),
    lambda loop: loop["loop_array"]["type"]["element_type_info"].update(kind="unknown"),
    lambda loop: loop["loop_array"]["type"].update(element_type_info=[]),
    lambda loop: loop["loop_element_type"].update(is_reference="yes"),
])
def test_invalid_foreach_array_and_element_metadata_are_rejected(mutate):
    data = loop_document("for_each_loop")
    mutate(first_loop(data))
    with pytest.raises(SemanticJsonError):
        parse_semantic_json(data)


def test_unused_foreach_element_and_legacy_container_metadata_are_preserved():
    data = loop_document("for_each_loop")
    loop = first_loop(data)
    loop["loop_element_variable"] = None
    loop["loop_array"]["type"].pop("element_type_info")
    assert parse_semantic_json(data) is data


@pytest.mark.parametrize("kind", ["expression", "return", "break"])
def test_non_loop_cannot_hide_nonempty_body_or_completed(kind):
    data = loop_document()
    first_loop(data)["kind"] = kind
    with pytest.raises(SemanticJsonError, match="only loop statements"):
        parse_semantic_json(data)


def test_nested_loop_statement_depth_is_bounded():
    data = loop_document()
    root = first_loop(data)
    template = copy.deepcopy(root)
    current = root
    for _ in range(65):
        child = copy.deepcopy(template)
        current["loop_body_statements"] = [child]
        current = child
    with pytest.raises(SemanticJsonError, match="statement nesting exceeds 64"):
        parse_semantic_json(data)


def test_nested_container_type_depth_is_bounded():
    data = loop_document("for_each_loop")
    current = first_loop(data)["loop_array"]["type"]
    template = copy.deepcopy(first_loop(data)["loop_element_type"])
    for _ in range(65):
        child = copy.deepcopy(template)
        current.update(container_kind="array", element_type_info=child)
        current = child
    with pytest.raises(SemanticJsonError, match="type nesting exceeds 64"):
        parse_semantic_json(data)


@pytest.mark.parametrize("field", ["element_type_info", "key_type_info", "value_type_info"])
def test_every_present_container_child_type_is_validated(field):
    data = loop_document("for_each_loop")
    array_type = first_loop(data)["loop_array"]["type"]
    array_type[field] = copy.deepcopy(first_loop(data)["loop_element_type"])
    array_type[field]["is_const"] = "not-a-boolean"
    with pytest.raises(SemanticJsonError, match=field):
        parse_semantic_json(data)


def test_empty_serializer_loop_fields_on_ordinary_statements_remain_compatible():
    data = loop_document()
    call = first_loop(data)["completed_statements"][0]
    blank = copy.deepcopy(first_loop(data)["loop_body_statements"][1]["then_statements"][0])
    blank.update(kind="expression", expression=call["expression"])
    data["functions"][0]["blocks"][0]["statements"] = [blank]
    assert parse_semantic_json(data) is data


@pytest.mark.parametrize("event_graph", [False, True])
@pytest.mark.parametrize("with_break", [False, True])
def test_uobject_foreach_entry_cfg_and_local_references_roundtrip(tmp_path, event_graph, with_break):
    data = loop_document("for_each_loop", "cfg")
    data["source"]["parent_class_path"] = "/Script/CoreUObject.Object"
    function = data["functions"][0]
    function.update(name="OnForEach" if event_graph else "RunForEach", is_event_graph=event_graph)
    header = function["blocks"][0]["statements"][0]
    header.update(loop_index_variable="ForEachArrayIndex", loop_element_variable="ForEachArrayElement")
    body = function["blocks"][1]["statements"]
    brk = copy.deepcopy(body[1]["then_statements"][0])
    call = copy.deepcopy(body[0])
    local = copy.deepcopy(call["expression"]["arguments"][0])
    local.update(kind="variable_reference", literal_value="", source_pin_id=None,
                 variable_name="ForEachArrayIndex" if event_graph else "ForEachArrayElement")
    local["type"].update(kind="int32")
    conversion = copy.deepcopy(call["expression"])
    conversion.update(function_name="Conv_IntToString", function_owner_path="/Script/Engine.KismetStringLibrary",
                      arguments=[local])
    conversion["type"].update(kind="string")
    context = copy.deepcopy(local)
    context.update(variable_name="this")
    context["type"].update(kind="object", type_path="/Script/CoreUObject.Object")
    call["expression"].update(function_name="PrintString", function_owner_path="/Script/Engine.KismetSystemLibrary",
                              arguments=[context, conversion])
    function["blocks"][1]["statements"] = [call, brk] if with_break else [call]
    parsed = parse_semantic_json(json.loads(json.dumps(data)))
    assert parsed["functions"][0] == function
    target = tmp_path / "foreach-entry.semantic.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    assert validate_file(target, check_provider=True)["provider_status"] == "mocked"


@pytest.mark.parametrize("depth", [1, 2, 3])
def test_uobject_custom_event_nested_range_loops_keep_completed_and_entry(tmp_path, depth):
    data = loop_document("for_loop")
    data["source"]["parent_class_path"] = "/Script/CoreUObject.Object"
    function = data["functions"][0]
    function.update(name="OnLoops", is_event_graph=True)
    outer = first_loop(data)
    outer["loop_body_statements"] = [outer["loop_body_statements"][0]]
    template = copy.deepcopy(outer)
    current = outer
    for level in range(1, depth):
        child = copy.deepcopy(template)
        child["loop_index_variable"] = "InnerIndex" if level == 1 else f"LoopIndex{level}"
        current["loop_body_statements"] = [child]
        current = child
    parsed = parse_semantic_json(json.loads(json.dumps(data)))
    assert parsed["functions"][0] == function
    target = tmp_path / "custom-event-forloop.semantic.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    assert validate_file(target, check_provider=True)["provider_status"] == "mocked"
