# CJ0 Blueprintゴールデンケース

更新日: 2026-10-18

この文書では、CJ1〜CJ2でExporterを拡張する際にUnreal Editor上で作成する検証ケースを定義する。CJ0-G01とCJ0-G02の基本ケースをAutomationで検証する。CJ0-G03は入力・戻り値・out引数・未接続Entry Pinの数値既定値についてテストを追加した。ただし型や既定値パターンの網羅は未完了であり、CJ0-G04以降を対応済みとして扱ってはならない。CJ0-G01は一時BlueprintからのCopilot Graph JSON v1保存・再読込とGUID/Exec接続の検証も行うが、テストは保存ファイルを削除するため、実プロジェクト由来のGolden Fixture収集を完了したことにはならない。

| ID | Blueprintの構成 | Snapshot v1 | Graph IR | Semantic v2 | Automation Test |
| --- | --- | --- | --- | --- | --- |
| CJ0-G01 | Actorの`BeginPlay -> PrintString("CJ0 golden")` | 抽出済み | 抽出済み | 部分対応 | `BlueprintAIConverter.CopilotJson.CJ0.MinimalNativeCallGolden` |
| CJ0-G02 | 引数なし・戻り値voidのBlueprint関数（Entry→PrintString） | 未検証 | 抽出とGUID/Exec接続を検査 | 反射宣言を検査（本体の意味一致は未検証） | `BlueprintAIConverter.CopilotJson.CJ0.VoidFunctionGolden` |
| CJ0-G03 | 入力、既定値、out引数、戻り値を持つBlueprint関数 | 未検証 | int入力→int戻り値のData接続を検査 | 反射署名・引数順序・戻り値・out引数・数値既定値の限定検証 | `BlueprintAIConverter.CopilotJson.CJ0.FunctionSignatureGolden`（限定） |
| CJ0-G04 | 同一アセット内の別Blueprint関数を呼び出す | 生Call・リンクを抽出予定 | 生Call・リンクを抽出予定 | 対応するLoweringがなければunsupported | 未実装 |
| CJ0-G05 | Pure式をBranchへ入力し、両分岐がReturnへ到達 | 生Exec/Dataリンクを抽出予定 | 正規化済みEdgeを抽出予定 | 形状によりpartialまたはunsupported | 未実装 |
| CJ0-G06 | EntryとReturnの間に未対応ノードを接続 | 生ノード・リンクを抽出予定 | 生ノード・リンクを抽出予定 | ノードGUID付きunsupported | 未実装 |

各ケースでは、アセットパス、グラフパス・GUID、ノードGUID、Pin GUID、UEバージョン、Exporterのスキーマバージョン、診断を記録する。スキーマで区別可能な場合は、空値・値なし・取得不可をそれぞれ別の状態として記録する。

Copilotの出力をゴールデンの正解データとして使用してはならない。ゴールデン成果物は、Blueprintから抽出されたJSONとUnrealオブジェクトまで遡れる追跡情報である。


---

## Copilot Graph JSON v1 の検証手順（旧 CJ0_JSON_VALIDATION_GUIDE.md から移管）

### 構造検証

`cd PythonServer` から以下を実行する。

```bash
python -m pytest tests/test_copilot_graph_json_validator.py
python -m app.validate_copilot_graph_json /path/to/BP.copilot.graph.json
```

検証器はCopilot Graph JSON v1のGUID、リンク参照、診断配列、関数識別子の基本的な整合性を検査する。**Blueprintの意味的な完全性やC++生成の正しさは判定しない**。

### Golden Fixtureの取得

1. UE 5.7.4で本書のCJ0-G01〜CJ0-G06、および `CJ0_JSON_AUDIT.md` に記録された旧G01〜G07を作成する。
2. `Saved/BlueprintCopilotJson/`に出力したJSONを保管する。実測JSONと手書きの合成Fixtureを混同しない。
3. アセット名、UEバージョン、Export操作、テスト結果を記録する。GUIDは元の値とリンク関係を維持し、必要な匿名化は整合性を保って行う。
4. 上記バリデータを実行する。失敗時はJSONを自動修復せず、Node/Pin GUIDと診断を記録する。
5. 各ケースの期待フィールドと実測JSONを照合する。旧監査ケース（G01〜G07）と現行ケース（CJ0-G01〜CJ0-G06）は一対一対応しないため、IDに加えてBlueprint構成とケース名を記録する。
6. JSON構造検証、UE Automation、UHT/UBT、動作比較は別々の結果として残す。

### 現状

- ソースベースのバリデータ・合成テストは追加済み。
- CJ0-G01〜G03の一時Blueprint由来Copilot Graph JSONはAutomation内で生成・再読込・削除する。CJ0-G02は関数の反射宣言も照合するが、Snapshot v1と生成C++の動作一致はこのテストでは確認しない。実プロジェクトのBlueprintから取得し保存したGolden Fixtureは**未収集**。
- 今回追加したCJ0-G03のGolden Automationは**UE 5.7.4で未実行**。Pythonテストも今回新たに実行していない。過去のCJトラック全体の実機検証記録とは区別する。
