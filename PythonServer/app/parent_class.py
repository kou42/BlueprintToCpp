"""Fail-closed parent-class metadata for the restricted Python MVP generators.

Unlike the Unreal C++ resolver, this offline service cannot inspect UClass metadata.
Only verified native parents are listed here; unknown paths must be rejected.
"""
from dataclasses import dataclass
import re

from .node_registry import UnsupportedNodeError


@dataclass(frozen=True)
class ParentClass:
    cpp_name: str
    header: str
    prefix: str


# Unreal の反射情報にアクセスできないため、確認済みのネイティブ親のみ許可する。
# This registry is only for the legacy snapshot -> BeginPlay C++ generators.
# Semantic JSON validation/roundtrip does not use it: native UObject parents
# exported by Unreal retain their paths without claiming offline C++ support.
_SUPPORTED_PARENTS = {
    "/Script/Engine.Actor": ParentClass("AActor", "GameFramework/Actor.h", "A"),
    "/Script/Engine.Pawn": ParentClass("APawn", "GameFramework/Pawn.h", "A"),
    "/Script/Engine.Character": ParentClass("ACharacter", "GameFramework/Character.h", "A"),
    "/Script/Engine.ActorComponent": ParentClass("UActorComponent", "Components/ActorComponent.h", "U"),
    "/Script/Engine.SceneComponent": ParentClass("USceneComponent", "Components/SceneComponent.h", "U"),
}


def resolve_parent(parent_path: str, class_name: str) -> ParentClass:
    parent = _SUPPORTED_PARENTS.get(parent_path)
    if parent is None:
        if parent_path == "/Script/UMG.UserWidget":
            reason = "Widget Blueprint requires widget-tree, binding and animation conversion"
        elif parent_path == "/Script/Engine.AnimInstance":
            reason = "Animation Blueprint requires anim graph, state-machine and generated-class conversion"
        else:
            reason = "offline BeginPlay/header metadata unavailable; use Unreal Semantic conversion for native UObject functions"
        raise UnsupportedNodeError(
            f"unsupported parent class {parent_path!r}: {reason}")
    # 循環importを避けるため、エラー型は検証時に取得する。
    from .code_generator import GenerationError
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", class_name):
        raise GenerationError("class_name must be a valid C++ identifier")
    if not class_name.startswith(parent.prefix) or len(class_name) <= len(parent.prefix):
        raise GenerationError(f"class_name must use {parent.prefix}-prefix for {parent.cpp_name}")
    return parent
