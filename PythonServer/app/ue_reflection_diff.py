"""Unreal Reflectionマクロの限定的な差分抽出。

C++プリプロセッサを実行せず、単一行のマクロと次の宣言だけを扱う。
"""
from __future__ import annotations

import re
from typing import Any

from .output_comparison import GenerationArtifact

_MACRO = re.compile(r"^\s*(UCLASS|USTRUCT|UFUNCTION|UPROPERTY)\s*\((.*)\)\s*$")
_CLASS = re.compile(r"^\s*(?:class|struct)\s+(?:\w+_API\s+)?([A-Za-z_]\w*)\b")
_FUNCTION = re.compile(r"^\s*(?:(?:virtual|static|inline)\s+)*([\w:<>,*&\s]+?)\s+([A-Za-z_]\w*)\s*\(([^()]*)\)\s*(?:const\s*)?(?:override\s*)?(?:;|\{)\s*$")
_VARIABLE = re.compile(r"^\s*([\w:<>,*&\s]+?)\s+([A-Za-z_]\w*)\s*(?:=[^;]*)?;\s*$")


def _normalize(value: str) -> str:
    return re.sub(r"\s+", " ", value.strip())


def extract_reflection(content: str) -> dict[str, Any]:
    """直後の単一行宣言と対応できたマクロだけを抽出する。"""
    found: dict[str, str] = {}
    warnings: list[str] = []
    pending: tuple[str, str] | None = None
    for line_number, line in enumerate(content.splitlines(), 1):
        stripped = line.strip()
        if stripped == "" or stripped.startswith("//"):
            continue
        macro = _MACRO.match(line)
        if macro is not None:
            if pending is not None:
                warnings.append(f"line {line_number}: previous reflection macro has no matched declaration")
            pending = (macro.group(1), _normalize(macro.group(2)))
            continue
        if pending is None:
            continue
        kind, specifiers = pending
        match = _CLASS.match(line) if kind in ("UCLASS", "USTRUCT") else (
            _FUNCTION.match(line) if kind == "UFUNCTION" else _VARIABLE.match(line)
        )
        if match is None:
            warnings.append(f"line {line_number}: unsupported declaration after {kind}")
        else:
            if kind in ("UCLASS", "USTRUCT"):
                name = match.group(1)
            elif kind == "UFUNCTION":
                name = f"{match.group(2)}({_normalize(match.group(3))}): {_normalize(match.group(1))}"
            else:
                name = f"{match.group(2)}: {_normalize(match.group(1))}"
            found[f"{kind}:{name}"] = specifiers
        pending = None
    if pending is not None:
        warnings.append("trailing reflection macro has no declaration")
    return {"declarations": found, "diagnostics": warnings}


def compare_reflection(direct: GenerationArtifact, ai: GenerationArtifact) -> dict[str, Any]:
    """反射マクロの欠落・specifierの違いを報告する。"""
    if direct.origin != "direct" or ai.origin != "ai":
        raise ValueError("comparison: expected direct and ai artifacts")
    if direct.status != "generated" or ai.status != "generated":
        return {"status": "unavailable", "differences": [], "diagnostics": ["Both generators must succeed."]}

    def collect(artifact: GenerationArtifact) -> tuple[dict[str, str], list[str]]:
        result: dict[str, str] = {}
        diagnostics: list[str] = []
        for file in artifact.files:
            if file["path"].lower().endswith((".h", ".hpp")):
                extracted = extract_reflection(file["content"])
                for key, value in extracted["declarations"].items():
                    # 同名宣言を複数ファイルに持つ場合も、比較キーを衝突させない。
                    result[f"{file['path'].casefold()}:{key}"] = value
                diagnostics.extend(f"{file['path']}: {message}" for message in extracted["diagnostics"])
        return result, diagnostics

    left, left_warnings = collect(direct)
    right, right_warnings = collect(ai)
    differences = []
    for key in sorted(left.keys() | right.keys()):
        if key not in left:
            differences.append({"declaration": key, "kind": "ai_only", "ai_specifiers": right[key]})
        elif key not in right:
            differences.append({"declaration": key, "kind": "direct_only", "direct_specifiers": left[key]})
        elif left[key] != right[key]:
            differences.append({"declaration": key, "kind": "specifier_changed",
                                "direct_specifiers": left[key], "ai_specifiers": right[key]})
    diagnostics = [
        "Limited single-line reflection scan; no macro expansion or C++ semantic analysis.",
        *(f"direct: {message}" for message in left_warnings),
        *(f"ai: {message}" for message in right_warnings),
    ]
    return {
        "status": "incomplete" if left_warnings or right_warnings else (
            "different" if differences else "no_detected_difference"
        ),
        "differences": differences,
        "diagnostics": diagnostics,
    }
