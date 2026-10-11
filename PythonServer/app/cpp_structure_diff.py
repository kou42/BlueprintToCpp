"""C++の限定的な宣言構造を比較する補助検査。

正規表現ベースの暫定解析であり、C++の構文解析・意味的同値判定ではない。
"""
from __future__ import annotations

import re
from typing import Any

from .output_comparison import GenerationArtifact

# 関数・変数の宣言を限定的に抽出する。複雑なテンプレート等は判定対象外。
_FUNCTION = re.compile(
    r"^\s*(?:virtual\s+|static\s+|inline\s+)*"
    r"([A-Za-z_][\w:<>,*&\s]*?)\s+"
    r"([A-Za-z_]\w*)\s*\(([^()]*)\)\s*(?:const\s*)?(?:;|\{)\s*$"
)
_VARIABLE = re.compile(
    r"^\s*(?:static\s+|const\s+)*"
    r"([A-Za-z_][\w:<>,*&\s]*?)\s+([A-Za-z_]\w*)\s*(?:=[^;]*)?;\s*$"
)
_CLASS = re.compile(r"^\s*(?:class|struct)\s+(?:\w+_API\s+)?([A-Za-z_]\w*)\b")
_SKIP_PREFIXES = ("#", "//", "UPROPERTY", "UFUNCTION", "UCLASS", "GENERATED_BODY", "using ", "typedef ")


def extract_declarations(content: str) -> dict[str, list[str]]:
    """単純な1行宣言のみ抽出。解析対象外を黙って同値とはみなさない。"""
    classes: set[str] = set()
    functions: set[str] = set()
    variables: set[str] = set()
    for line in content.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(_SKIP_PREFIXES):
            continue
        class_match = _CLASS.match(line)
        if class_match is not None:
            classes.add(class_match.group(1))
            continue
        function_match = _FUNCTION.match(line)
        if function_match is not None:
            return_type, name, args = function_match.groups()
            normalized_args = re.sub(r"\s+", " ", args.strip())
            normalized_type = re.sub(r"\s+", " ", return_type.strip())
            functions.add(f"{name}({normalized_args}): {normalized_type}")
            continue
        variable_match = _VARIABLE.match(line)
        if variable_match is not None:
            var_type, name = variable_match.groups()
            normalized_type = re.sub(r"\s+", " ", var_type.strip())
            variables.add(f"{name}: {normalized_type}")
    return {
        "classes": sorted(classes),
        "functions": sorted(functions),
        "variables": sorted(variables),
    }


def compare_declarations(direct: GenerationArtifact, ai: GenerationArtifact) -> dict[str, Any]:
    """双方のファイルから宣言候補を比較し、未解析範囲は必ず警告する。"""
    if direct.origin != "direct" or ai.origin != "ai":
        raise ValueError("comparison: expected direct and ai artifacts")
    if direct.status != "generated" or ai.status != "generated":
        return {"status": "unavailable", "differences": [], "diagnostics": ["Both generators must succeed."]}

    def collect(artifact: GenerationArtifact) -> dict[str, set[str]]:
        result: dict[str, set[str]] = {"classes": set(), "functions": set(), "variables": set()}
        for file in artifact.files:
            # ヘッダーの宣言だけを対象にし、関数本体やローカル変数を誤認しない。
            if file["path"].lower().endswith((".h", ".hpp")):
                extracted = extract_declarations(file["content"])
                for category in result:
                    result[category].update(extracted[category])
        return result

    left, right = collect(direct), collect(ai)
    differences = []
    for category in ("classes", "functions", "variables"):
        for value in sorted(left[category] - right[category]):
            differences.append({"category": category, "declaration": value, "kind": "direct_only"})
        for value in sorted(right[category] - left[category]):
            differences.append({"category": category, "declaration": value, "kind": "ai_only"})
    return {
        "status": "different" if differences else "no_detected_difference",
        "differences": differences,
        "diagnostics": [
            "Heuristic header-only declaration scan; no AST, function-body, control-flow or semantic verification.",
            "No detected difference does not imply equivalent or correct C++.",
        ],
    }
