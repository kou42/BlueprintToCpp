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

## Semantic JSON v2（J1・試験実装）

既存のSnapshot JSON v1とは独立して、UE側の `FBPSemanticJsonSerializer::Serialize(IR, EngineVersion, OutJson)` でSemantic IRをJSON v2に変換できます。**現時点ではEditorのメニューや保存先への接続は未実装**です。

Python側では以下のようにスキーマ検証できます。

```python
import json
from app.semantic_json import parse_semantic_json

with open("semantic.json", encoding="utf-8") as file:
    semantic = parse_semantic_json(json.load(file))
print(semantic["semantic_status"])
```

この検証はJSONの構造確認であり、C++生成やBlueprintとの動作一致を保証しません。現行IRでは解析範囲の網羅性が不明なため、診断がない場合も `semantic_status=partial` として出力します。未対応・診断ありの場合は `unsupported` です。詳細は `docs/SEMANTIC_JSON_V2_DESIGN.md` を参照してください。

### UE EditorからのSemantic JSON出力（実験的）

Content BrowserでBlueprintを1つ選択し、右クリックから次を実行できます。

- **Copy Semantic JSON v2 (Preview)**: ファイル保存せずJSON文字列をクリップボードへコピーします。
- **Export Semantic JSON v2**: プロジェクトの `Saved/BlueprintSemanticJson/` に `.semantic.json` ファイルを保存します。

現時点のSemantic LoweringはBeginPlay → PrintStringのMVPに限定されます。未対応Blueprintでも診断を含むJSONが出力されます。出力成功はC++変換成功を意味しません。UE 5.7.4でのコンパイルとAutomation Testは未確認です。

## A1: AI Provider共通インターフェース（Mock）

実験用の `POST /ai/generate` は `{"semantic": <Semantic JSON v2>, "provider": "mock"}` を受け付けます。既存の `POST /generate` とは独立しています。

`mock` は構造検証だけを行い、外部AIへの通信やC++生成はしません。入力が `partial` または `complete` なら `status: "mocked"`、`unsupported` なら `status: "rejected"` を返し、どちらも `files: []` です。未知のProviderや不正JSONはHTTP 422になります。現段階では実AI Provider、APIキー管理、コード生成結果の検証は未実装です。

## 生成結果の共通Validator

`app.generated_output_validator` は `/generate` と `/ai/generate` のレスポンスを返す前にファイルパス・内容・状態を検証します。相対パスのみ許可し、拡張子は `.h`、`.hpp`、`.cpp` に限定します。ファイル数は最大32、各ファイルの文字数は最大2,000,000、パス長は最大240文字です。

これは保存前の入力検証であり、C++コードの安全性、UHT/UBTビルド、Blueprintとの意味一致、保存時のシンボリックリンク対策は保証しません。保存機能の実装時には保存先ディレクトリを固定し、書き込み直前にもパス検証が必要です。

## A1: OpenAI Provider（実験的）

`POST /ai/generate` の `provider` に `openai` を指定すると、OpenAI Chat Completions APIを使用します。利用前にPythonServerのプロセス環境変数へ `OPENAI_API_KEY` と `BLUEPRINT_TO_CPP_OPENAI_MODEL` を設定してください。キーをリポジトリへコミットしないでください。モデルはJSON object形式の応答に対応している必要があります。

```json
{"semantic": {"schema_version": 2, "format": "blueprint_semantic_ir", "producer": {"name": "BlueprintAIConverter", "engine_version": "5.7.4"}, "source": {"asset_path": "/Game/Test.Test", "parent_class_path": "/Script/Engine.Actor", "snapshot_schema_version": 1}, "semantic_status": "partial", "functions": [], "diagnostics": []}, "provider": "openai"}
```

この例はAPI形式の説明用であり、実際の変換にはUEからエクスポートした意味情報を使用してください。応答は既存の共通Validatorで検証されます。現段階ではC++のコンパイル、意味一致、APIの実通信は未検証です。特に `partial` は未解析領域を含むため、生成結果を自動適用しないでください。認証のないローカルAPIを外部ネットワークに公開しないでください。


## D1/D2: Direct / AI生成結果の差分比較API（実験的）

`POST /compare` は既存の `/generate` と `/ai/generate` のレスポンスから、`status`・`files`・`diagnostics` を取り出して比較します。**APIは生成やAI通信、ファイル保存を行いません。**

```json
{
  "direct": {"status": "generated", "files": [{"path": "Example.cpp", "content": "int x = 1;\n"}], "diagnostics": []},
  "ai": {"status": "generated", "files": [{"path": "Example.cpp", "content": "int x = 2;\n"}], "diagnostics": []}
}
```

返却値は `status`（`identical` / `different` / `unavailable`）、`files`（`identical` / `modified` / `direct_only` / `ai_only` と変更時の `diff`）、`diagnostics`、各出力のSHA-256 fingerprintです。`equivalent` は現段階では常に `null` であり、**テキスト一致はC++の正しさやBlueprintとの意味一致を保証しません。** Mockや片側失敗時は `unavailable` となります。パス・ファイル内容は既存Validatorで検証されます。社内コードの送信先を増やさないよう、ローカルホスト上での利用を想定します。


## D3: C++ヘッダー宣言の限定的な構造比較

`POST /compare` のレスポンスに `structure` を追加します。生成済みの両側の `.h` / `.hpp` から、単純なクラス名・関数宣言・変数宣言を抽出し、片側だけにある宣言を `differences` に返します。`structure.status` は `different`、`no_detected_difference`、`unavailable` のいずれかです。

この段階では正規表現による**限定的な補助検査**であり、複雑なテンプレート、複数行宣言、マクロ展開、関数本体、実行順、副作用、C++の意味的同値性は解析できません。`no_detected_difference` は合格や同値の証明ではありません。最終採用にはUHT/UBTとBlueprintの動作検証が必要です。


## D3: Unreal Reflection差分（試験実装）

`POST /compare` の `reflection` フィールドでは、`.h` / `.hpp` 内の単一行 `UCLASS` / `USTRUCT` / `UFUNCTION` / `UPROPERTY` と、その直後の宣言を対応付け、マクロの欠落やspecifier文字列の違いを検出します。対応できない複数行宣言などがあれば `status: incomplete` と診断を返します。マクロ展開、複雑な引数、C++ AST、関数本体や実行時の意味は検証できません。比較結果だけで自動承認しないでください。


## D3: 関数シグネチャ差分（試験実装）

`POST /compare` の `signatures` フィールドで、ヘッダー内の単純な1行メンバー関数宣言を比較します。戻り値・引数の文字列表現・`const`・`override` の差分を報告します。同名オーバーロードや複数行宣言など、正確に扱えない構文は `incomplete` と診断します。C++ ASTや実行時の意味的同値性の検証ではありません。


## D3: 関数内の処理順序比較（試験実装）

`POST /compare` の `effects` では、`.cpp` の単純な1行関数開始宣言と、その本体にある単純な関数呼び出し・代入文の**出現順序**を比較します。異なる順序や式は `statement_sequence_changed` として返します。分岐、ループ、複雑な式など未対応構文は `incomplete` と診断します。これは副作用の可能性を示すレビュー補助であり、実際の副作用、制御フロー、Blueprintとの動作一致を証明しません。


## D4: レビュー後の修正・再比較（保存なし）

`POST /review/recompare` は `original` に従来の `POST /compare` と同じ `direct` / `ai` を受け取り、`revision` に `target`（`direct` または `ai`）、`revised`（修正後の生成結果）、`note`（人間による修正理由）、`expected_direct_fingerprint`、`expected_ai_fingerprint` を受け取ります。指紋が元の生成結果と違う場合はHTTP 409を返します。正常時は `before` / `after` の全比較結果とレビュー情報を返します。

**重要:** 修正候補は検証しますが、ディスク保存、履歴永続化、AI自動修正、承認操作はしません。`review.approved` は常に `false` であり、差分が消えても正しさやBlueprintとの意味的同値性を保証しません。UHT/UBT・動作テストと人間による最終レビューが必要です。


## D4: レビュー履歴のJSON保存と人間による判定

`POST /review/records` に `{ "request": <POST /review/recompare のリクエスト>, "reviewer": "担当者" }` を送ると、再比較結果をJSONとして保存します。`GET /review/records/{id}` で取得できます。保存先は環境変数 `BLUEPRINT_TO_CPP_REVIEW_STORE` で指定し、省略時は実行ディレクトリの `review_records` です。修正前後のコードも保存するため、機密情報の扱いとディレクトリ権限に注意してください。

`POST /review/records/{id}/decision` には `reviewer`、`decision`（`approved` / `rejected`）、`reason`、`expected_revised_fingerprint` を指定します。判定は元のレビュー記録を上書きせず、独立したイベントとして保存されます。元のレビュー記録は `pending` のままです。**承認は人間の明示的な入力のみ**であり、UHT/UBT・Blueprint動作検証は別途必要です。

現段階のAPIには認証・アクセス制御・判定イベントの一覧取得・単一の最新承認状態集約・複数プロセス間の整合性保証はありません。**信頼できるローカル環境に限定**し、外部公開しないでください。


## D4: レビュー一覧と最新の判定

`GET /review/records?limit=50&offset=0` は保存済みレビューの概要を新しい順に返し、`latest_decision`（`pending` / `approved` / `rejected`）と判定件数を含みます。コード本体は一覧には含めません。`GET /review/records/{id}/status` は、そのレビューに紐づく承認・却下イベントを時系列で返します。最新判定は保存イベントの作成時刻とIDに基づき集約し、元のレビューJSONは変更しません。

ファイル数に比例して読み込みコストが増えるため、小規模なローカル利用向けです。認証・アクセス制御・複数プロセスでの厳密な順序保証はまだありません。外部公開しないでください。


## V2: UHT/UBTの外部検証証跡

`POST /review/records/{id}/verification` で、`reviewer`、`tool`（`uht` / `ubt`）、`outcome`（`passed` / `failed` / `not_run`）、`evidence_reference`、`summary`、`expected_revised_fingerprint` を送ると、そのレビューに対応する外部検証結果をJSONイベントとして保存します。`GET /review/records/{id}/verification` で各ツールの最新の提出結果と全イベントを取得できます。未提出の場合は `not_recorded` と表示します。

**注意:** APIはUHT/UBTを実行しません。結果は利用者が提出した申告値であり、真正性の検証・ログの読み込み・署名検証・自動承認は行いません。機密コードを扱うため、引き続き信頼できるローカル環境だけで使用してください。
