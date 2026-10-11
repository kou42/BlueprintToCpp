"""Direct / AIの生成結果を共通化し、保存せずに差分を調べる。

テキスト一致は意味の正しさやビルド成功を保証しない。
"""
from __future__ import annotations

from dataclasses import dataclass
from difflib import unified_diff
from hashlib import sha256
from typing import Any

from .generated_output_validator import GeneratedOutputError, validate_generation_result


@dataclass(frozen=True)
class GenerationArtifact:
    origin: str
    status: str
    files: tuple[dict[str, str], ...]
    diagnostics: tuple[str, ...]
    fingerprint: str


def normalize_artifact(origin: str, result: Any) -> GenerationArtifact:
    """既存のDirect辞書とAI Provider結果を同じ検証済み形式に揃える。"""
    if origin not in ("direct", "ai"):
        raise GeneratedOutputError("origin: expected direct or ai")
    if isinstance(result, dict):
        status = result.get("status")
        raw_files = result.get("files")
        diagnostics = result.get("diagnostics", [])
    else:
        status = getattr(result, "status", None)
        raw_files = getattr(result, "files", None)
        diagnostics = getattr(result, "diagnostics", ())
    files = validate_generation_result(status, raw_files)
    if not isinstance(diagnostics, (list, tuple)) or any(
        not isinstance(item, str) for item in diagnostics
    ):
        raise GeneratedOutputError("diagnostics: expected string list")
    # ハッシュは生成物の内容から計算し、ファイルの返却順には依存させない。
    digest = sha256()
    for file in sorted(files, key=lambda item: item["path"].casefold()):
        for value in (file["path"], file["content"]):
            data = value.encode("utf-8")
            digest.update(len(data).to_bytes(8, "big"))
            digest.update(data)
    return GenerationArtifact(origin, status, files, tuple(diagnostics), digest.hexdigest())


def compare_artifacts(direct: GenerationArtifact, ai: GenerationArtifact) -> dict[str, Any]:
    """両経路が生成成功した場合だけDiffを計算し、それ以外は比較保留にする。"""
    if direct.origin != "direct" or ai.origin != "ai":
        raise GeneratedOutputError("comparison: expected direct and ai artifacts")
    if direct.status != "generated" or ai.status != "generated":
        return {
            "status": "unavailable",
            "equivalent": None,
            "files": [],
            "diagnostics": [
                "Both Direct and AI must generate files before comparison.",
                *direct.diagnostics,
                *ai.diagnostics,
            ],
        }

    direct_files = {file["path"].casefold(): file for file in direct.files}
    ai_files = {file["path"].casefold(): file for file in ai.files}
    comparisons = []
    for key in sorted(direct_files.keys() | ai_files.keys()):
        left = direct_files.get(key)
        right = ai_files.get(key)
        path = left["path"] if left is not None else right["path"]
        if left is None:
            kind = "ai_only"
        elif right is None:
            kind = "direct_only"
        elif left["content"] == right["content"]:
            kind = "identical"
        else:
            kind = "modified"
        # 差分はレビュー用であり、意味的同値性を判定するものではない。
        diff = ""
        if kind == "modified":
            diff = "".join(unified_diff(
                left["content"].splitlines(keepends=True),
                right["content"].splitlines(keepends=True),
                fromfile="direct/" + left["path"],
                tofile="ai/" + right["path"],
            ))
        comparisons.append({"path": path, "kind": kind, "diff": diff})
    identical = all(file["kind"] == "identical" for file in comparisons)
    return {
        "status": "identical" if identical else "different",
        "equivalent": None,
        "files": comparisons,
        "diagnostics": ["Text equality does not establish C++ correctness or semantic equivalence."],
    }
