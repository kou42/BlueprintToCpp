"""Editorから保存・コピーしたSemantic JSON v2をローカルで検証するCLI。

使用例:
    python -m app.semantic_roundtrip Saved/BlueprintSemanticJson/BP_Test.json
    python -m app.semantic_roundtrip --provider mock Saved/BlueprintSemanticJson/BP_Test.json
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from .ai_provider import MockAIProvider
from .semantic_json import SemanticJsonError, parse_semantic_json


def validate_file(path: Path, *, check_provider: bool = False) -> dict:
    """JSONの読み込み、Semantic契約検証、任意でMock Provider境界まで検査する。"""
    try:
        with path.open("r", encoding="utf-8-sig") as stream:
            raw = json.load(stream)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SemanticJsonError(f"{path}: cannot read Semantic JSON: {exc}") from exc
    semantic = parse_semantic_json(raw)
    result = {
        "asset_path": semantic["source"]["asset_path"],
        "semantic_status": semantic["semantic_status"],
        "variables": len(semantic.get("variables", [])),
        "functions": len(semantic["functions"]),
        "graph_traces": len(semantic.get("graph_traces", [])),
    }
    if check_provider:
        # 外部APIを呼ばず、Providerの入力契約まで確認する。
        provider_result = MockAIProvider().generate(semantic)
        if provider_result.files:
            raise SemanticJsonError("Mock Provider must not generate files")
        result["provider_status"] = provider_result.status
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate an exported Semantic JSON v2 file")
    parser.add_argument("paths", type=Path, nargs="+", help="Exported JSON file(s)")
    parser.add_argument("--provider", choices=("mock",), help="Check the AI Provider input boundary")
    args = parser.parse_args(argv)
    failed = False
    for path in args.paths:
        try:
            report = validate_file(path, check_provider=args.provider == "mock")
            print(json.dumps({"file": str(path), "status": "ok", **report}, ensure_ascii=False))
        except SemanticJsonError as exc:
            print(json.dumps({"file": str(path), "status": "error", "message": str(exc)}, ensure_ascii=False))
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
