# CJ0 Blueprintゴールデンケース

更新日: 2026-10-16

この文書では、CJ1〜CJ2でExporterを拡張する際にUnreal Editor上で作成する検証ケースを定義する。現在自動化されているのは最初のケースだけであり、残りのケースを対応済みとして扱ってはならない。

| ID | Blueprintの構成 | Snapshot v1 | Graph IR | Semantic v2 | Automation Test |
| --- | --- | --- | --- | --- | --- |
| CJ0-G01 | Actorの`BeginPlay -> PrintString("CJ0 golden")` | 抽出済み | 抽出済み | 部分対応 | `BlueprintAIConverter.CopilotJson.CJ0.MinimalNativeCallGolden` |
| CJ0-G02 | 引数なし・戻り値voidのBlueprint関数 | 生グラフを抽出予定 | 生グラフを抽出予定 | 部分対応 | 未実装 |
| CJ0-G03 | 入力、既定値、out引数、戻り値を持つBlueprint関数 | 生グラフ・Pinを抽出予定 | 生グラフ・Pinを抽出予定 | 署名のみ部分対応 | 未実装 |
| CJ0-G04 | 同一アセット内の別Blueprint関数を呼び出す | 生Call・リンクを抽出予定 | 生Call・リンクを抽出予定 | 対応するLoweringがなければunsupported | 未実装 |
| CJ0-G05 | Pure式をBranchへ入力し、両分岐がReturnへ到達 | 生Exec/Dataリンクを抽出予定 | 正規化済みEdgeを抽出予定 | 形状によりpartialまたはunsupported | 未実装 |
| CJ0-G06 | EntryとReturnの間に未対応ノードを接続 | 生ノード・リンクを抽出予定 | 生ノード・リンクを抽出予定 | ノードGUID付きunsupported | 未実装 |

各ケースでは、アセットパス、グラフパス・GUID、ノードGUID、Pin GUID、UEバージョン、Exporterのスキーマバージョン、診断を記録する。スキーマで区別可能な場合は、空値・値なし・取得不可をそれぞれ別の状態として記録する。

Copilotの出力をゴールデンの正解データとして使用してはならない。ゴールデン成果物は、Blueprintから抽出されたJSONとUnrealオブジェクトまで遡れる追跡情報である。


---

## Copilot Graph JSON v1 の検証手順（旧 CJ0_JSON_VALIDATION_GUIDE.md から移管）


## 構造検証

`cd PythonServer` から以下を実行する。

```bash
python -m pytest tests/test_copilot_graph_json_validator.py
python -m app.validate_copilot_graph_json /path/to/BP.copilot.graph.json
```

検証器はCopilot Graph JSON v1のGUID、リンク参照、診断配列、関数識別子の基本的な整合性を検査する。**Blueprintの意味的な完全性やC++生成の正しさは判定しない**。

## Golden Fixtureの取得

1. UE 5.7.4で本書のCJ0-G01〜CJ0-G06、および `CJ0_JSON_AUDIT.md` に記録された旧G01〜G07を作成する。
2. `Saved/BlueprintCopilotJson/`に出力したJSONを保管する。実測JSONと手書きの合成Fixtureを混同しない。
3. アセット名、UEバージョン、Export操作、テスト結果を記録する。GUIDは元の値とリンク関係を維持し、必要な匿名化は整合性を保って行う。
4. 上記バリデータを実行する。失敗時はJSONを自動修復せず、Node/Pin GUIDと診断を記録する。
5. 旧監査ケース（G01〜G07）の各構成。現行のCJ0-G01〜CJ0-G06とはIDと内容が一対一対応しないため、必ずケース名も記録するをそれぞれ検査する。
6. JSON構造検証、UE Automation、UHT/UBT、動作比較は別々の結果として残す。

## 現状

- ソースベースのバリデータ・合成テストは追加済み。
- 実Blueprintから取得したGolden Fixtureは**未収集**。
- UE 5.7.4実機検証とPythonテスト実行結果は**未確認**。
