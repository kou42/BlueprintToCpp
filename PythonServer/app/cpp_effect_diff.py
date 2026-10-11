"""C++関数本体の単純な呼び出し順・代入候補を比較する補助解析。

制御フローや式の意味は解析せず、扱えない行があればincompleteとする。
"""
from __future__ import annotations

import re
from typing import Any

from .output_comparison import GenerationArtifact

_START = re.compile(r"^\s*(?:[\w:<>,*&]+\s+)+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)?)\s*\([^;{}]*\)\s*(?:const\s*)?\{\s*$")
_CALL = re.compile(r"^\s*([A-Za-z_]\w*(?:(?:::|\.|->)[A-Za-z_]\w*)*)\s*\(([^;{}]*)\)\s*;\s*$")
_ASSIGN = re.compile(r"^\s*([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)\s*([+*/%-]?=)\s*([^;{}]+);\s*$")


def extract_effects(content: str) -> dict[str, Any]:
    """単純な関数本体だけを解析し、未対応の制御構造は診断する。"""
    functions: dict[str, list[dict[str, str]]] = {}
    diagnostics: list[str] = []
    active: str | None = None
    depth = 0
    for line_number, line in enumerate(content.splitlines(), 1):
        stripped = line.strip()
        if active is None:
            match = _START.match(line)
            if match is not None:
                active = match.group(1)
                if active in functions:
                    diagnostics.append(f"line {line_number}: duplicate function {active}")
                else:
                    functions[active] = []
                depth = 1
            continue
        if stripped == "}":
            depth -= 1
            if depth == 0:
                active = None
            continue
        if stripped == "" or stripped.startswith("//"):
            continue
        # 分岐・ループ・ネストしたブロックを直列処理と誤認しない。
        if "{" in stripped or "}" in stripped or stripped.startswith(("if ", "if(", "for ", "for(", "while ", "while(", "switch ", "switch(")):
            diagnostics.append(f"line {line_number}: unsupported control flow in {active}")
            depth += stripped.count("{") - stripped.count("}")
            if depth <= 0:
                active = None
            continue
        if depth != 1:
            continue
        call = _CALL.match(line)
        assignment = _ASSIGN.match(line)
        if call is not None:
            functions[active].append({"kind": "call", "target": call.group(1), "expression": call.group(2).strip()})
        elif assignment is not None:
            functions[active].append({"kind": "assignment", "target": assignment.group(1),
                                      "expression": assignment.group(2) + " " + assignment.group(3).strip()})
        else:
            diagnostics.append(f"line {line_number}: unsupported statement in {active}")
    if active is not None:
        diagnostics.append(f"unterminated function: {active}")
    return {"functions": functions, "diagnostics": diagnostics}


def compare_effects(direct: GenerationArtifact, ai: GenerationArtifact) -> dict[str, Any]:
    """副作用の可能性がある文の順序を比較する。実際の副作用は断定しない。"""
    if direct.origin != "direct" or ai.origin != "ai":
        raise ValueError("comparison: expected direct and ai artifacts")
    if direct.status != "generated" or ai.status != "generated":
        return {"status": "unavailable", "differences": [], "diagnostics": ["Both generators must succeed."]}

    def collect(artifact: GenerationArtifact) -> tuple[dict[str, list], list[str]]:
        result: dict[str, list] = {}
        warnings: list[str] = []
        for file in artifact.files:
            if file["path"].lower().endswith(".cpp"):
                parsed = extract_effects(file["content"])
                for name, statements in parsed["functions"].items():
                    result[f"{file['path'].casefold()}:{name}"] = statements
                warnings.extend(f"{file['path']}: {message}" for message in parsed["diagnostics"])
        return result, warnings

    left, left_warnings = collect(direct)
    right, right_warnings = collect(ai)
    differences = []
    for name in sorted(left.keys() | right.keys()):
        if name not in left:
            differences.append({"function": name, "kind": "ai_only"})
        elif name not in right:
            differences.append({"function": name, "kind": "direct_only"})
        elif left[name] != right[name]:
            differences.append({"function": name, "kind": "statement_sequence_changed",
                                "direct": left[name], "ai": right[name]})
    return {
        "status": "incomplete" if left_warnings or right_warnings else (
            "different" if differences else "no_detected_difference"
        ),
        "differences": differences,
        "diagnostics": [
            "Heuristic call/assignment order only; no control-flow, side-effect or semantic proof.",
            *(f"direct: {message}" for message in left_warnings),
            *(f"ai: {message}" for message in right_warnings),
        ],
    }
