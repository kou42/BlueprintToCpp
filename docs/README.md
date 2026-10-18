# docs ドキュメント案内

このディレクトリには、BlueprintToCppの仕様・実装計画・検証手順・実測証跡を置く。ファイル名や過去の進捗表だけで現在の実装・検証完了を判断しないこと。

## 最初に読む資料

- [全体ロードマップ](../ROADMAP.md): Direct C++変換を中心とする全体方針と優先順位。
- [Copilot JSONエクスポート・ロードマップ](COPILOT_JSON_EXPORT_ROADMAP.md): 独立したCJ0〜CJ4トラックの範囲と進捗。全体ロードマップを置き換えない。
- [Copilot Chat手動ワークフロー](COPILOT_CHAT_WORKFLOW.md): JSONをIDEへ渡す際の操作とレビュー・検証ゲート。

## JSON契約・設計・監査

- [Copilot Graph JSON v1スキーマ](COPILOT_GRAPH_JSON_SCHEMA_V1.md): Graph JSONのデータ契約。
- [CJ0 JSON監査](CJ0_JSON_AUDIT.md): Snapshot、Graph JSON、Semantic JSONの静的フィールド監査と旧G01〜G07仕様。旧フィールド監査を統合済み。
- [CJ0 Goldenケース・検証手順](CJ0_GOLDEN_CASES.md): 現行CJ0-G01〜G06とJSON validatorの操作。旧検証ガイドを統合済み。旧G01〜G07とはケースIDが異なる。
- [CJ1関数メタデータ契約](CJ1_FUNCTION_METADATA_CONTRACT.md): 関数署名・既定値等の契約。
- [Semantic JSON v2設計記録](SEMANTIC_JSON_V2_DESIGN.md): J0時点の設計案。現行の実装完了状態を示す資料ではない。

## Direct変換と検証

- [親クラス解決](C1-parent-class-resolution.md): Actor親クラス解決の対応範囲・拒否条件と実機証跡。
- [ForLoop対応](FOR_LOOP_SUPPORT.md): 現在の限定対応形状と未対応範囲。
- [Semantic IR検証](SEMANTIC_IR_VERIFICATION.md): UE EditorでのSemantic Preview・Shadow Modeの検証手順。
- [Editor往復検証](J2-5-editor-roundtrip.md): Semantic JSON v2のEditor→Python往復確認と実測証跡。
- [隔離UHT/UBT検証](J2-12-isolated-ubt-verification.md): 生成C++の隔離ビルド手順と実測証跡。

## 整理時の注意

1. **仕様・計画・検証手順・実測結果を区別する。** 単体テスト、UE Automation、UHT/UBT、元Blueprintとの実行時比較は別の合格条件である。
2. 監査時点の「未実装」「未検証」は後続の実装・テスト結果で更新され得る。コード・テスト・最新の実測記録を照合する。
3. 削除・統合前にリポジトリ内の参照を確認し、GUID、fingerprint、検証コマンド、未達条件などの固有情報を失わない。
