# CJ0 JSON検証手順

## 構造検証

`cd PythonServer` から以下を実行する。

```bash
python -m pytest tests/test_copilot_graph_json_validator.py
python -m app.validate_copilot_graph_json /path/to/BP.copilot.graph.json
```

検証器はCopilot Graph JSON v1のGUID、リンク参照、診断配列、関数識別子の基本的な整合性を検査する。**Blueprintの意味的な完全性やC++生成の正しさは判定しない**。

## Golden Fixtureの取得

1. UE 5.7.4で`CJ0_JSON_FIELD_AUDIT.md`のG01〜G07を作成する。
2. `Saved/BlueprintCopilotJson/`に出力したJSONを保管する。実測JSONと手書きの合成Fixtureを混同しない。
3. アセット名、UEバージョン、Export操作、テスト結果を記録する。GUIDは元の値とリンク関係を維持し、必要な匿名化は整合性を保って行う。
4. 上記バリデータを実行する。失敗時はJSONを自動修復せず、Node/Pin GUIDと診断を記録する。
5. G01: BeginPlay→PrintString、G02: CalculateDamage、G03: Native呼び出し、G04: Branch、G05: 他BP関数、G06: Macro/Delegate、G07: out/Container型をそれぞれ検査する。
6. JSON構造検証、UE Automation、UHT/UBT、動作比較は別々の結果として残す。

## 現状

- ソースベースのバリデータ・合成テストは追加済み。
- 実Blueprintから取得したGolden Fixtureは**未収集**。
- UE 5.7.4実機検証とPythonテスト実行結果は**未確認**。
