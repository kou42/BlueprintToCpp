
import json
from pathlib import Path

from app.schemas import parse_snapshot
from app.generator_dispatch import generate_supported_cpp

# ① BlueprintのSnapshot JSON
SNAPSHOT_PATH = Path(
    r"D:\UE\MyProject2\Saved\BlueprintSnapshots\GameThirdPersonBlueprintsBP_ConvertTest.BP_ConvertTest.json"
)

# ② C++の出力先
OUTPUT_DIR = Path(
    r"D:\UE\MyProject2\Source\MyProject"
)

# ③ 生成するC++クラス名
CLASS_NAME = "AConvertTest"


def main():
    # Snapshotファイルの存在を確認
    if not SNAPSHOT_PATH.is_file():
        raise FileNotFoundError(
            f"Snapshotが見つかりません: {SNAPSHOT_PATH}"
        )

    # Snapshot JSONを読み込む
    with SNAPSHOT_PATH.open(
        "r", encoding="utf-8-sig"
    ) as file:
        raw_snapshot = json.load(file)

    # JSONをBlueprint Snapshotオブジェクトに変換
    snapshot = parse_snapshot(raw_snapshot)

    # BlueprintからC++を生成
    result = generate_supported_cpp(
        snapshot,
        class_name=CLASS_NAME,
    )

    # 出力先フォルダを作成
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # 生成された .h / .cpp を保存
    for generated_file in result["files"]:
        filename = generated_file["path"]
        content = generated_file["content"]

        output_path = OUTPUT_DIR / filename

        output_path.write_text(
            content,
            encoding="utf-8",
        )

        print(f"生成完了: {output_path}")

    # 警告を表示
    for warning in result.get("warnings", []):
        print(f"警告: {warning}")

    print("C++生成処理が完了しました。")


if __name__ == "__main__":
    main()
