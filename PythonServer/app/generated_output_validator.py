"""Direct / AI / Hybridで再利用する生成ファイルの構造・パス検証。

これは保存前の防御層であり、C++のコンパイル成功や安全性を証明しない。
"""
from __future__ import annotations

import re
from pathlib import PurePosixPath
from typing import Any


class GeneratedOutputError(ValueError):
    """生成ファイルが出力契約を満たしていない。"""


MAX_FILES = 32
MAX_CONTENT_LENGTH = 2_000_000
MAX_PATH_LENGTH = 240
ALLOWED_EXTENSIONS = frozenset((".h", ".hpp", ".cpp"))
SAFE_COMPONENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]*$")


def validate_generated_files(files: Any) -> tuple[dict[str, str], ...]:
    """保存処理に渡す前にファイル集合を検証する。ディスクへの書き込みはしない。"""
    if not isinstance(files, (list, tuple)):
        raise GeneratedOutputError("files: expected list or tuple")
    if len(files) > MAX_FILES:
        raise GeneratedOutputError("files: too many generated files")

    seen: set[str] = set()
    validated: list[dict[str, str]] = []
    for index, file in enumerate(files):
        label = f"files[{index}]"
        if isinstance(file, dict):
            if set(file) != {"path", "content"}:
                raise GeneratedOutputError(f"{label}: expected path and content")
            path, content = file["path"], file["content"]
        else:
            # AI ProviderのGeneratedFileなど、既存の型付き結果を受け付ける。
            if not hasattr(file, "path") or not hasattr(file, "content"):
                raise GeneratedOutputError(f"{label}: missing path or content")
            path, content = file.path, file.content

        if not isinstance(path, str) or not path or len(path) > MAX_PATH_LENGTH:
            raise GeneratedOutputError(f"{label}.path: invalid length or type")
        if not isinstance(content, str) or not content.strip():
            raise GeneratedOutputError(f"{label}.content: expected non-empty text")
        if len(content) > MAX_CONTENT_LENGTH or "\x00" in content:
            raise GeneratedOutputError(f"{label}.content: invalid size or NUL byte")

        # Windowsの絶対パス・ドライブ指定・UNC・混合区切りを一律拒否する。
        if "\\" in path or ":" in path or path.startswith("/") or path.startswith("~"):
            raise GeneratedOutputError(f"{label}.path: absolute or non-portable path")
        parts = path.split("/")
        if any(part in ("", ".", "..") or SAFE_COMPONENT.fullmatch(part) is None
               for part in parts):
            raise GeneratedOutputError(f"{label}.path: unsafe path component")
        if PurePosixPath(path).suffix not in ALLOWED_EXTENSIONS:
            raise GeneratedOutputError(f"{label}.path: unsupported extension")
        # Windowsで同じファイルを指す表記を重複扱いする。
        folded = path.casefold()
        if folded in seen:
            raise GeneratedOutputError(f"{label}.path: duplicate output path")
        seen.add(folded)
        validated.append({"path": path, "content": content})
    return tuple(validated)


def validate_generation_result(status: Any, files: Any) -> tuple[dict[str, str], ...]:
    """生成結果の状態とファイル集合の整合性を検証する。"""
    if status not in ("generated", "mocked", "rejected"):
        raise GeneratedOutputError("status: unknown generation status")
    validated = validate_generated_files(files)
    if status == "generated" and not validated:
        raise GeneratedOutputError("generated status requires output files")
    if status != "generated" and validated:
        raise GeneratedOutputError("non-generated status must not contain files")
    return validated
