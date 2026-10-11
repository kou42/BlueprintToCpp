"""単純なC++メンバー関数の型・引数・修飾子差分を調べる。

完全なC++パーサーではなく、1行の関数宣言に限定する。
"""
from __future__ import annotations

import re
from typing import Any

from .output_comparison import GenerationArtifact

_FUNCTION = re.compile(
    r"^\s*(?:(?:virtual|static|inline|FORCEINLINE)\s+)*"
    r"([A-Za-z_][\w:<>,*&\s]*?)\s+([A-Za-z_]\w*)\s*"
    r"\(([^()]*)\)\s*(const\b\s*)?(override\b\s*)?(?:;|\{)\s*$"
)
_CLASS = re.compile(r"^\s*(?:class|struct)\s+(?:\w+_API\s+)?([A-Za-z_]\w*)\b")


def _normalize(value: str) -> str:
    return re.sub(r"\s+", " ", value.strip())


def extract_signatures(content: str) -> dict[str, Any]:
    """クラス内の1行関数宣言のみ解析し、候補外は診断する。"""
    signatures: dict[str, dict[str, str | bool]] = {}
    warnings: list[str] = []
    current_class: str | None = None
    for line_number, line in enumerate(content.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith(("//", "#", "UFUNCTION", "UPROPERTY", "UCLASS", "USTRUCT")):
            continue
        class_match = _CLASS.match(line)
        if class_match is not None:
            current_class = class_match.group(1)
            continue
        if current_class is None:
            continue
        if stripped.startswith("};"):
            current_class = None
            continue
        if "(" not in stripped or stripped.startswith(("if ", "for ", "while ", "return ", "GENERATED_BODY")):
            continue
        match = _FUNCTION.match(line)
        if match is None:
            # 解析できない関数候補は一致扱いせずレビュー対象として残す。
            warnings.append(f"line {line_number}: unsupported function-like declaration")
            continue
        return_type, name, args, const, override = match.groups()
        key = f"{current_class}::{name}"
        # オーバーロードは同名キーを持つため、必ず未解析として報告する。
        if key in signatures:
            warnings.append(f"line {line_number}: overloaded function {key} is ambiguous")
            continue
        signatures[key] = {
            "return_type": _normalize(return_type),
            "arguments": _normalize(args),
            "const": const is not None,
            "override": override is not None,
        }
    return {"signatures": signatures, "diagnostics": warnings}


def compare_signatures(direct: GenerationArtifact, ai: GenerationArtifact) -> dict[str, Any]:
    if direct.origin != "direct" or ai.origin != "ai":
        raise ValueError("comparison: expected direct and ai artifacts")
    if direct.status != "generated" or ai.status != "generated":
        return {"status": "unavailable", "differences": [], "diagnostics": ["Both generators must succeed."]}

    def collect(artifact: GenerationArtifact) -> tuple[dict[str, dict], list[str]]:
        signatures: dict[str, dict] = {}
        warnings: list[str] = []
        for file in artifact.files:
            if file["path"].lower().endswith((".h", ".hpp")):
                parsed = extract_signatures(file["content"])
                for key, value in parsed["signatures"].items():
                    qualified = f"{file['path'].casefold()}:{key}"
                    signatures[qualified] = value
                warnings.extend(f"{file['path']}: {message}" for message in parsed["diagnostics"])
        return signatures, warnings

    left, left_warnings = collect(direct)
    right, right_warnings = collect(ai)
    differences = []
    for key in sorted(left.keys() | right.keys()):
        if key not in left:
            differences.append({"function": key, "kind": "ai_only"})
        elif key not in right:
            differences.append({"function": key, "kind": "direct_only"})
        else:
            for field in ("return_type", "arguments", "const", "override"):
                if left[key][field] != right[key][field]:
                    differences.append({"function": key, "kind": "signature_changed", "field": field,
                                        "direct": left[key][field], "ai": right[key][field]})
    return {
        "status": "incomplete" if left_warnings or right_warnings else (
            "different" if differences else "no_detected_difference"
        ),
        "differences": differences,
        "diagnostics": [
            "Single-line heuristic signature scan; no AST, overload resolution or semantic verification.",
            *(f"direct: {message}" for message in left_warnings),
            *(f"ai: {message}" for message in right_warnings),
        ],
    }
