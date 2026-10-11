"""レビュー記録をローカルJSONとして保持するストレージ。

保存先はサーバー管理ディレクトリに固定し、IDからパスを組み立てない。
"""
from __future__ import annotations

import json
import os
from pathlib import Path
from tempfile import NamedTemporaryFile
from uuid import uuid4

_STORE_ENV = "BLUEPRINT_TO_CPP_REVIEW_STORE"


def store_directory() -> Path:
    """環境変数で指定したレビュー専用ディレクトリを使用する。"""
    return Path(os.environ.get(_STORE_ENV, "review_records")).resolve()


def save_record(record: dict) -> dict:
    """一時ファイルを同じディレクトリ内で置換し、途中書き込みを公開しない。"""
    directory = store_directory()
    directory.mkdir(parents=True, exist_ok=True)
    record_id = uuid4().hex
    payload = {"id": record_id, **record}
    temp_path: Path | None = None
    try:
        with NamedTemporaryFile(mode="w", encoding="utf-8", dir=directory, prefix=".review-", suffix=".tmp", delete=False) as temp:
            temp_path = Path(temp.name)
            json.dump(payload, temp, ensure_ascii=False, indent=2)
            temp.flush()
            os.fsync(temp.fileno())
        os.replace(temp_path, directory / f"{record_id}.json")
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)
    return payload


def load_record(record_id: str) -> dict | None:
    """API側で形式検証したIDだけを受け取る。"""
    if len(record_id) != 32 or any(character not in "0123456789abcdef" for character in record_id):
        return None
    path = store_directory() / f"{record_id}.json"
    try:
        with path.open("r", encoding="utf-8") as file:
            return json.load(file)
    except FileNotFoundError:
        return None



def list_records() -> list[dict]:
    """レビュー専用ディレクトリの正規IDファイルのみ列挙する。

    一時ファイルや想定外のファイルは無視し、壊れたJSONも公開しない。
    """
    directory = store_directory()
    if directory.is_dir() is False:
        return []
    records: list[dict] = []
    for path in directory.glob("*.json"):
        record_id = path.stem
        if len(record_id) != 32 or any(char not in "0123456789abcdef" for char in record_id):
            continue
        try:
            # ディレクトリ内のファイルだけを読み込み、レコードIDの一致も検証する。
            record = load_record(record_id)
            if isinstance(record, dict) and record.get("id") == record_id:
                records.append(record)
        except (ValueError, OSError):
            continue
    return records
