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

## 動作と制限事項

- スキーマバージョン、グラフ内のノードID・ピンID、リンク先、ピン方向、双方向リンクの整合性、基本的なピンカテゴリを検証します。
- ピンIDを保持したまま、**並列の**実行（Exec）エッジとデータ（Data）エッジを保存します。循環も許容します。
- 将来の変換処理に備え、元ノードの識別情報を保持したグラフ専用IRを出力します。
- エントリーノードは**ヒューリスティック**に判定します。実行出力エッジを持ち、実行入力エッジを持たないノードが対象です。出力リンクのない単独イベントは列挙されません。
- UEのワイルドカード型、オブジェクト／クラスの継承関係、real/floatのサブカテゴリ、コンテナ型はまだ解決しません。カテゴリの検証は意図的に基本的なものに限定しています。
- Blueprintコンパイラ相当の意味解析、構造化制御フロー、純粋ノードの評価、遅延アクション、マクロ、C++生成はこの解析器の対象外です。
- HTTPエンドポイントはローカル開発用です。認証やレート制限はないため、信頼できないネットワークには公開しないでください。
