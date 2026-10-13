"""Semantic関数シグネチャと単純なC++宣言を安全側で照合する。

引数の既定値・テンプレート・マクロ・オーバーロード等は未検証に留める。
"""
from __future__ import annotations

import re

from .cpp_signature_diff import extract_signatures
from .output_comparison import GenerationArtifact
from .semantic_json import parse_semantic_json

_CPP_TYPES = {
    "void": "void", "bool": "bool", "int32": "int32", "int64": "int64",
    "float": "float", "double": "double", "string": "FString",
    "name": "FName", "text": "FText",
}

# type_pathだけから任意のUClass/UScriptStructのC++接頭辞を推測しない。
# UEで表記が固定されている代表型だけを明示し、それ以外は未検証に残す。
_CPP_PATH_TYPES = {
    ("object", "/Script/CoreUObject.Object"): "UObject*",
    ("object", "/Script/Engine.Actor"): "AActor*",
    ("struct", "/Script/CoreUObject.Vector"): "FVector",
    ("struct", "/Script/CoreUObject.Vector2D"): "FVector2D",
    ("struct", "/Script/CoreUObject.Rotator"): "FRotator",
    ("struct", "/Script/CoreUObject.Transform"): "FTransform",
    ("struct", "/Script/CoreUObject.LinearColor"): "FLinearColor",
    ("struct", "/Script/CoreUObject.Color"): "FColor",
}


def _cpp_type(semantic_type: dict) -> str | None:
    kind = semantic_type["kind"]
    type_path = semantic_type["type_path"]
    if type_path:
        base = _CPP_PATH_TYPES.get((kind, type_path))
    else:
        base = _CPP_TYPES.get(kind)
    if base is None:
        return None
    if semantic_type["is_const"]:
        base = "const " + base
    if semantic_type["is_reference"]:
        base += "&"
    return base


def _normalize(value: str) -> str:
    return re.sub(r"\s+", "", value)


def audit_semantic_signatures(semantic: dict, artifact: GenerationArtifact) -> dict:
    """確実に解析できる引数・戻り値だけ比較し、曖昧なケースを不一致にしない。"""
    parsed = parse_semantic_json(semantic)
    if artifact.status != "generated":
        return {"status": "unavailable", "differences": [], "diagnostics": [
            "No generated C++ is available for signature auditing."]}

    found: dict[str, list[dict]] = {}
    diagnostics: list[str] = []
    for file in artifact.files:
        if file["path"].lower().endswith((".h", ".hpp")):
            extracted = extract_signatures(file["content"])
            for qualified, signature in extracted["signatures"].items():
                found.setdefault(qualified.rsplit("::", 1)[-1], []).append(signature)
            diagnostics.extend(f"{file['path']}: {message}" for message in extracted["diagnostics"])

    differences: list[dict[str, str]] = []
    for function in parsed["functions"]:
        # 反射情報がない旧形式は推測せず、明示的に未検証とする。
        if function.get("signature_source") != "generated_class_reflection":
            diagnostics.append(f"{function['name']}: no reflected signature")
            continue
        candidates = found.get(function["name"], [])
        if len(candidates) != 1:
            diagnostics.append(f"{function['name']}: expected one unambiguous C++ declaration")
            continue
        actual = candidates[0]
        expected_return = _cpp_type(function["return_type"])
        if expected_return is None:
            diagnostics.append(f"{function['name']}: unsupported return type")
        elif _normalize(expected_return) != _normalize(actual["return_type"]):
            differences.append({"function": function["name"], "field": "return_type",
                                "expected": expected_return, "actual": actual["return_type"]})

        parameters = [p for p in function["parameters"] if p["is_return"] is False]
        # C++側は単純な '型 名前' だけを対象にする。複雑な引数構文を
        # 分割すると誤判定を招くため、カンマや既定値のある宣言は保留する。
        raw_args = actual["arguments"].strip()
        parts = [] if raw_args in ("", "void") else raw_args.split(",")
        if len(parts) != len(parameters):
            differences.append({"function": function["name"], "field": "parameter_count",
                                "expected": str(len(parameters)), "actual": str(len(parts))})
            continue
        for index, (parameter, raw) in enumerate(zip(parameters, parts)):
            expected_type = _cpp_type(parameter["type"])
            if expected_type is None or "=" in raw or "<" in raw or ">" in raw:
                diagnostics.append(f"{function['name']}.{parameter['name']}: unsupported argument syntax or type")
                continue
            match = re.fullmatch(r"\s*(.+?)\s+([A-Za-z_]\w*)\s*", raw)
            if match is None:
                diagnostics.append(f"{function['name']}.{parameter['name']}: ambiguous argument")
                continue
            actual_type, actual_name = match.groups()
            if _normalize(expected_type) != _normalize(actual_type):
                differences.append({"function": function["name"], "field": f"parameter[{index}].type",
                                    "expected": expected_type, "actual": actual_type})
            if parameter["name"] != actual_name:
                # 引数名はC++の意味的互換性を左右しないが追跡上の差として示す。
                diagnostics.append(f"{function['name']}: argument name differs at {index}")

    diagnostics.append("Heuristic single-line comparison; overloads, owner class, UHT and ABI are unverified.")
    return {"status": "review_required" if differences else "incomplete",
            "differences": differences, "diagnostics": diagnostics}
