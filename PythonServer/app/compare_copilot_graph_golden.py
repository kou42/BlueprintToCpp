"""CJ0: UE Editorで実際に出力したCopilot Graph JSONの回帰比較。

両ファイルは同一Blueprintを同一状態でエクスポートした実測JSONを指定する。
GUIDを置換・正規化せず、接続と出典の変化を検出する。
"""
import argparse
import json
from pathlib import Path

from app.validate_copilot_graph_json import validate


def compare_golden(expected, actual):
    """構造検証後に値を再帰比較し、変更されたJSONパスを返す。"""
    errors = []
    for label, document in (("expected", expected), ("actual", actual)):
        errors.extend(f"{label}: {error}" for error in validate(document))
    if errors:
        return errors

    def visit(left, right, path):
        if type(left) is not type(right):
            errors.append(f"{path}: type changed")
        elif isinstance(left, dict):
            for key in sorted(left.keys() | right.keys()):
                child = f"{path}.{key}"
                if key not in left:
                    errors.append(f"{child}: unexpected field")
                elif key not in right:
                    errors.append(f"{child}: missing field")
                else:
                    visit(left[key], right[key], child)
        elif isinstance(left, list):
            if len(left) != len(right):
                errors.append(f"{path}: length {len(left)} -> {len(right)}")
            for index in range(min(len(left), len(right))):
                visit(left[index], right[index], f"{path}[{index}]")
        elif left != right:
            errors.append(f"{path}: value changed")

    visit(expected, actual, "$")
    return errors


def main():
    parser = argparse.ArgumentParser(description="Compare two actual Copilot Graph JSON exports")
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    args = parser.parse_args()
    with args.expected.open(encoding="utf-8-sig") as stream:
        expected = json.load(stream)
    with args.actual.open(encoding="utf-8-sig") as stream:
        actual = json.load(stream)
    differences = compare_golden(expected, actual)
    for difference in differences:
        print(difference)
    if differences:
        return 1
    print("Golden JSON matches")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
