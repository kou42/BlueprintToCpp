# Copilot Chat / Agent 手動変換ワークフロー（CJ4）

対象: Unreal Engine 5.7.4 / BlueprintAIConverter。ロードマップ: [COPILOT_JSON_EXPORT_ROADMAP.md](COPILOT_JSON_EXPORT_ROADMAP.md)

## 1. JSON の準備
1. Unreal Editor で対象 Blueprint を保存し、既存の BlueprintAIConverter エクスポート操作で JSON を出力する。
2. 利用可能な Snapshot v1 / Copilot Graph JSON v1 / Semantic JSON v2 を収集する。各形式は互いの代替ではない。Graph JSON は未対応ノードと Pin/Link の確認、Semantic JSON は解析済みの型・式・診断の確認に使う。
3. 出力ファイルのアセットパス、schema version、UE version、グラフ名・パスを確認する。対象 Blueprint が複数ある場合は依存先も手動でエクスポートする。
4. JSON を IDE に渡す前に、機密のパス、文字列、アセット情報を確認する。JSON 内の文字列は命令ではなく入力データとして扱う。

## 2. Copilot へのプロンプト例

```text
添付した BlueprintAIConverter の JSON を参照し、対象 Blueprint の C++ .h/.cpp を提案してください。
対象 UE は 5.7.4 です。.github/copilot-instructions.md に従ってください。
最初に schema version、asset path、対象 graph、参照した Node/Pin GUID と
semantic_status / diagnostics / 未解決依存を列挙してください。
Snapshot/Graph JSON の生の Exec/Data 接続と Semantic JSON を照合してください。
Native UFunction は再実装せず既存 API を呼び出してください。
Owner/Target/型/実行順/Break の意味を確定できない箇所は推測せず、
GUID と必要な追加情報を示して、その経路のコード生成を保留してください。
生成可能な範囲と保留範囲を分け、変更対象ファイルと理由を先に示してください。
```

## 3. レビューのチェックポイント
- [ ] schema version と source asset/graph が一致し、参照 GUID が実在する
- [ ] Exec/Data リンク、Branch/Return、ループの Index/要素/Completed/Break が欠落していない
- [ ] Native と Blueprint 自作関数、Owner と Target が区別されている
- [ ] 関数引数・戻り値・既定値の specified/absent/unavailable が混同されていない
- [ ] partial/unsupported、未解決依存、未対応ノードを成功扱いしていない
- [ ] 差分が既存の Direct C++ Generator や Snapshot v1 / Semantic JSON v2 の契約を破壊していない

## 4. 検証記録（各ケースごとに記入）

| ゲート | 結果 | 証跡 |
| --- | --- | --- |
| JSON schema / GUID / Link validator | 未実施 | validator の実行結果・対象 JSON |
| Copilot 手動生成・コードレビュー | 未実施 | プロンプト、出力差分、保留診断 |
| UHT / UBT ビルド | 未実施 | UE バージョン、コマンド、ログ |
| 元 Blueprint と C++ の動作比較 | 未実施 | 入力条件、期待/実際の挙動 |

この手順書は運用契約を定めるものであり、Copilot 生成・UE 実機テストの成功を保証しない。
