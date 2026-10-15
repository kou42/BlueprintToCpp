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
