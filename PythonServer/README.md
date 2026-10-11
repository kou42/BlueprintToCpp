# Phase 2 — Python Blueprint Snapshot解析器

Phase 1のUE Editorプラグインが出力したJSONを読み込み、解析します。Python 3.10以降が必要です。

## インストールとテスト

```bash
cd PythonServer
python -m pip install -e ".[test,api]"
python -m pytest -q
```

## コマンドラインからの解析

```bash
python -m app.cli ../Saved/BlueprintSnapshots/example.json -o analyzed.json
```

## HTTP API

```bash
uvicorn app.main:app --host 127.0.0.1 --port 8000
```

`POST /analyze` は `{"snapshot": <Phase 1のJSONオブジェクト>}` を受け付けます。
`GET /health` はサービスの稼働状態を返します。

## BlueprintからC++を生成する方法

C++生成は、UE Editorで出力したSnapshot JSONをPythonServerに渡して行います。現在の`POST /generate`は生成したソースをJSONレスポンスとして返し、**ファイルへの自動保存やUEでのコンパイルは行いません**。

### 1. BlueprintのSnapshot JSONを出力する

UE EditorのBlueprintAIConverterプラグインから対象Blueprint（例：`BP_ConvertTest`）のSnapshotを出力します。出力先はUEプロジェクトの`Saved/BlueprintSnapshots/`です。ファイル名はBlueprintのアセットパスから作成されるため、実際の出力ファイル名を確認してください。

### 2. PythonServerを起動する

リポジトリのルートから実行します。

```powershell
cd PythonServer
python -m pip install -e ".[api,test]"
python -m uvicorn app.main:app --host 127.0.0.1 --port 8000
```

ブラウザで`http://127.0.0.1:8000/docs`を開くと、`POST /generate`を試せます。リクエストには**Snapshot JSON全体**と、生成するC++クラス名を指定します。

```json
{
  "snapshot": {
    "schema_version": 1,
    "engine_version": "5.7.4",
    "asset_path": "/Game/BP_ConvertTest.BP_ConvertTest",
    "parent_class": "/Script/Engine.Actor",
    "graphs": [],
    "variables": []
  },
  "class_name": "AConvertTest"
}
```

上記はリクエスト形式の説明用です。`graphs`が空のため、そのままではC++を生成できません。実際にはUE Editorから出力したSnapshotの内容で`snapshot`を置き換えてください。

成功時は`status: "generated"`と、`files`配列に`ConvertTest.h`および`ConvertTest.cpp`のファイル名と内容が返されます。未対応ノードなどがある場合はHTTP 422とエラーの詳細が返されます。

### 3. Pythonから直接生成し、.h / .cppを保存する

APIを使わず、以下のPythonコードを`PythonServer`フォルダから実行する方法もあります。Snapshotのパスと出力先は自身の環境に合わせて変更してください。

```python
import json
from pathlib import Path

from app.schemas import parse_snapshot
from app.generator_dispatch import generate_supported_cpp

snapshot_path = Path(r"C:\MyProject\Saved\BlueprintSnapshots\snapshot.json")
output_dir = Path(r"C:\MyProject\Source\MyProject")
output_dir.mkdir(parents=True, exist_ok=True)

raw = json.loads(snapshot_path.read_text(encoding="utf-8-sig"))
result = generate_supported_cpp(parse_snapshot(raw), class_name="AConvertTest")

for generated_file in result["files"]:
    output_path = output_dir / generated_file["path"]
    output_path.write_text(generated_file["content"], encoding="utf-8")
    print(f"生成完了: {output_path}")
```

このコードはファイルを書き込むだけで、UEプロジェクトへの組み込みやビルドの成功を保証するものではありません。

### 4. 現在の対応範囲と注意点

- `ReceiveBeginPlay`から始まる限定的な実行経路に対応します。
- `PrintString`は主に文字列リテラルに対応し、オプションピンの一部は未対応です。
- `Branch`と`VariableSet`には制限付きの生成器があります。`VariableGet`など未対応の処理はエラーになります。
- 生成コードの`MODULE_API`指定やUEモジュールへの組み込みは別途確認が必要です。
- UE 5.7.4でのコンパイルおよび動作一致は未検証です。

より詳しい制約は`PHASE3.md`、`PHASE4.md`、`PHASE5.md`を参照してください。

## 動作と制限事項

- スキーマバージョン、グラフ内のノードID・ピンID、リンク先、ピン方向、双方向リンクの整合性、基本的なピンカテゴリを検証します。
- ピンIDを保持したまま、**並列の**実行（Exec）エッジとデータ（Data）エッジを保存します。循環も許容します。
- 将来の変換処理に備え、元ノードの識別情報を保持したグラフ専用IRを出力します。
- エントリーノードは**ヒューリスティック**に判定します。実行出力エッジを持ち、実行入力エッジを持たないノードが対象です。出力リンクのない単独イベントは列挙されません。
- UEのワイルドカード型、オブジェクト／クラスの継承関係、real/floatのサブカテゴリ、コンテナ型はまだ解決しません。カテゴリの検証は意図的に基本的なものに限定しています。
- Blueprintコンパイラ相当の意味解析、構造化制御フロー、純粋ノードの評価、遅延アクション、マクロ、C++生成はこの解析器の対象外です。
- HTTPエンドポイントはローカル開発用です。認証やレート制限はないため、信頼できないネットワークには公開しないでください。
