"""Semantic IRの宣言と生成C++ヘッダーを保守的に照合する。

正規表現で検出できる単純な宣言のみ対象とし、C++の正しさを証明しない。
"""
from __future__ import annotations

from .cpp_signature_diff import extract_signatures
from .cpp_structure_diff import extract_declarations
from .output_comparison import GenerationArtifact
from .semantic_json import parse_semantic_json


def audit_semantic_declarations(semantic: dict, artifact: GenerationArtifact) -> dict:
    """存在確認を主目的とし、推測できない型・宣言は未検証として残す。"""
    parsed = parse_semantic_json(semantic)
    if artifact.status != "generated":
        return {"status": "unavailable", "missing": [], "diagnostics": [
            "Generation did not produce C++ files; declaration audit was skipped."]}

    variables: set[str] = set()
    functions: set[str] = set()
    warnings: list[str] = []
    headers = 0
    for file in artifact.files:
        if file["path"].lower().endswith((".h", ".hpp")):
            headers += 1
            declarations = extract_declarations(file["content"])
            for declaration in declarations["variables"]:
                variables.add(declaration.split(": ", 1)[0])
            signatures = extract_signatures(file["content"])
            functions.update(key.rsplit("::", 1)[-1] for key in signatures["signatures"])
            warnings.extend(f"{file['path']}: {message}" for message in signatures["diagnostics"])

    missing: list[dict[str, str]] = []
    for variable in parsed.get("variables", []):
        if variable["name"] not in variables:
            missing.append({"scope": "variable", "name": variable["name"]})
    for function in parsed["functions"]:
        # Blueprintのイベントや継承関数は生成ヘッダーに現れないこともある。
        # 欠落を確定エラーにせず、レビュー対象の候補としてのみ報告する。
        if function["name"] not in functions:
            missing.append({"scope": "function", "name": function["name"]})

    if headers == 0:
        warnings.append("No C++ header was available for declaration scanning.")
    warnings.append("Heuristic declaration scan only; inherited members, macros, overloads, "
                    "parameter types and runtime behavior are not verified.")
    return {
        "status": "review_required" if missing else "incomplete",
        "missing": missing,
        "diagnostics": warnings,
    }
