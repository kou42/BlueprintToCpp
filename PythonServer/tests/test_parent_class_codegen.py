import pytest

from app.schemas import parse_snapshot
from app.code_generator import generate_cpp, GenerationError
from app.branch_codegen import generate_branch_cpp
from app.variable_codegen import generate_variable_cpp
from app.node_registry import UnsupportedNodeError
from test_codegen import begin_play_snapshot
from test_branch_codegen import fixture as branch_fixture
from test_variable_codegen import snapshot as variable_snapshot


@pytest.mark.parametrize("parent_path, class_name, parent_cpp, header", [
    ("/Script/Engine.Actor", "ATestActor", "AActor", "GameFramework/Actor.h"),
    ("/Script/Engine.Pawn", "ATestPawn", "APawn", "GameFramework/Pawn.h"),
    ("/Script/Engine.Character", "ATestCharacter", "ACharacter", "GameFramework/Character.h"),
    ("/Script/Engine.ActorComponent", "UTestComponent", "UActorComponent", "Components/ActorComponent.h"),
    ("/Script/Engine.SceneComponent", "UTestScene", "USceneComponent", "Components/SceneComponent.h"),
])
def test_begin_play_parent_variants(parent_path, class_name, parent_cpp, header):
    raw = begin_play_snapshot()
    raw["parent_class"] = parent_path
    result = generate_cpp(parse_snapshot(raw), class_name)
    assert f"class {class_name} : public {parent_cpp}" in result["files"][0]["content"]
    assert f'#include "{header}"' in result["files"][0]["content"]
    assert "Super::BeginPlay();" in result["files"][1]["content"]


def test_branch_component_parent():
    raw = branch_fixture()
    raw["parent_class"] = "/Script/Engine.ActorComponent"
    result = generate_branch_cpp(parse_snapshot(raw), "UTestComponent")
    assert "class UTestComponent : public UActorComponent" in result["files"][0]["content"]


def test_variable_component_parent():
    raw = variable_snapshot()
    raw["parent_class"] = "/Script/Engine.ActorComponent"
    result = generate_variable_cpp(parse_snapshot(raw), "UTestComponent")
    assert "class UTestComponent : public UActorComponent" in result["files"][0]["content"]


def test_mismatched_prefix_rejected():
    raw = begin_play_snapshot()
    raw["parent_class"] = "/Script/Engine.ActorComponent"
    with pytest.raises(GenerationError, match="U-prefix"):
        generate_cpp(parse_snapshot(raw), "ATestActor")


def test_unverified_parent_rejected():
    raw = begin_play_snapshot()
    raw["parent_class"] = "/Script/UMG.UserWidget"
    with pytest.raises(UnsupportedNodeError, match="unsupported parent class"):
        generate_cpp(parse_snapshot(raw), "UTestWidget")


@pytest.mark.parametrize("parent_path, requirement", [
    ("/Script/UMG.UserWidget", "widget-tree, binding and animation"),
    ("/Script/Engine.AnimInstance", "anim graph, state-machine and generated-class"),
    ("/Script/CoreUObject.Object", "use Unreal Semantic conversion"),
    ("/Script/Engine.DataAsset", "use Unreal Semantic conversion"),
    ("/Script/Engine.SaveGame", "use Unreal Semantic conversion"),
    ("/Script/MyPlugin.NativeObject", "use Unreal Semantic conversion"),
])
@pytest.mark.parametrize("generator, fixture", [
    (generate_cpp, begin_play_snapshot),
    (generate_branch_cpp, branch_fixture),
    (generate_variable_cpp, variable_snapshot),
])
def test_offline_generators_explain_parent_capabilities(parent_path, requirement, generator, fixture):
    raw = fixture()
    raw["parent_class"] = parent_path
    with pytest.raises(UnsupportedNodeError, match=requirement):
        generator(parse_snapshot(raw), "UTestObject")
