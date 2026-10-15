# CJ0 JSONフィールド監査・代表ケース仕様

更新日: 2026-10-15  
対象: Unreal Engine 5.7.4 / BlueprintAIConverter  
状態: **静的コード監査済み・UE実機未検証**

## 目的

[COPILOT_JSON_EXPORT_ROADMAP.md](COPILOT_JSON_EXPORT_ROADMAP.md) のCJ0として、既存の3形式とCopilot専用Graph JSONの出力事実・不足を整理する。以下は**Serializer/Extractorのコード調査結果**であり、実際のBlueprintをエクスポートした検証結果ではない。

## フォーマットとフィールド対応表

| 情報 | Snapshot v1 | Graph IR → Copilot Graph v1 | Semantic JSON v2 | 判定・課題 |
| --- | --- | --- | --- | --- |
| スキーマ | `schema_version: 1` | `schema_version: 1`, `format: blueprint_copilot_graph` | `schema_version: 2`, `format: blueprint_semantic_ir` | 抽出済み |
| エンジン・アセット | `engine_version`, `asset_path`, `parent_class` | `engine_version`, `asset_path`, `parent_class_path` | `producer.engine_version`, `source.asset_path`, `source.parent_class_path` | 抽出済み |
| グラフ | `graphs[].name/path/kind/guid` | `graphs[].name/path/kind` | 関数・基本ブロック中心 | Graph GUIDはCopilot側で欠落 |
| ノード | `graphs[].nodes[].id/class/title/x/y` | `graphs[].nodes[].id/class_path/title` | `source_node_id`（式・文） | 位置はSnapshotのみ |
| Pin | `id/name/direction/type/default_value/default_object/default_text/is_hidden` | `id/name/direction/category/subcategory/subcategory_object_path/default_value/default_object_path/default_text` | 式の`source_pin_id` | Container/参照/const/hiddenはCopilot側で欠落 |
| Pinリンク | `pins[].links[].node_id/pin_id` | `exec_edges[]`, `data_edges[]`（source/target node/pin ID） | 文・式にloweringされた範囲 | Copilot側は参照整合性検査あり、リンク順序は保持しない |
| 呼び出し先 | `function_name/function_path` | `function_name/function_owner_path/function_path/function_resolved/function_native/function_static/function_pure/function_const` | 式の`function_name/function_owner_path` | Copilot側で識別可能、Target/Selfは未明示 |
| 変数 | `variables[].name/guid/type/default_value/property_flags` | ノードの`variable_name` | 式の`variable_name` | メンバー変数一覧はSnapshotのみ |
| 関数署名 | 直接出力なし | `semantic_declarations`が渡された場合に内包 | `functions[].name/return_type/owner_class_path/signature_source/is_native/is_static/is_const/is_blueprint_event/parameters` | Semantic IR依存、全関数保証なし |
| 引数 | Pinから間接取得 | Pinから間接取得 | `parameters[].name/type/is_out/is_return/has_default_value/default_value` | 順序と型範囲・デフォルト取得可否の検証が必要 |
| 診断・完全性 | 明示的な状態なし | `semantic_status: partial`, `diagnostics[]`（文字列） | `semantic_status: partial/unsupported`, `diagnostics[]`（構造化） | `complete`は保証しない、診断形式の統一が必要 |

## 監査対象の実装

- `Private/BPSnapshotExporter.cpp`：Snapshot v1。PinリンクとBlueprint変数を出力。
- `Private/BPGraphIR.cpp`：Event/Function/Macro Graphの収集、CallFunctionの解決、Exec/Dataリンク収集。
- `Private/BPCopilotGraphJsonExporter.cpp`：Graph JSON v1。GUID整合性検査、ソート、UTF-8 BOMなし保存、任意のSemantic埋め込み。
- `Private/BPSemanticFunctionExtractor.cpp`：GeneratedClass直下のUFunctionとFPropertyを取得。継承関数は除外。Enum/Container/Delegateは未解決。
- `Private/BPSemanticJsonSerializer.cpp`：Semantic JSON v2。型・式・文・診断を出力。

## 代表Blueprintゴールデンケース（作成・実機検証待ち）

| ID | Blueprint構成 | 必須確認 | 現状判定 |
| --- | --- | --- | --- |
| G01 | 引数なし・voidの自作関数 | Function Graph、宣言名、Return | 部分対応：Graph取得と署名抽出は実装済み、実例未検証 |
| G02 | 引数・戻り値・既定値ありの自作関数 | 引数順、型、return、デフォルトの存在と値 | 部分対応：Reflection由来の署名あり、順序・既定値網羅未検証 |
| G03 | Native関数呼び出し | UFunctionのOwner/Path、Native、Target | 部分対応：解決済み関数情報あり、Target未明示 |
| G04 | BP自作関数→別BP自作関数 | 参照先アセットと依存関係、未解決診断 | 部分対応：関数パスあり、依存一覧なし |
| G05 | Branch→Pure式→Return | Exec/Dataリンク、分岐、評価順 | 部分対応：リンクあり、順序保証なし |
| G06 | 参照/out・配列・構造体 | Pin型、方向、コンテナ型、未解決診断 | 部分対応：Snapshotは詳細型あり、Semanticは型制限 |
| G07 | 未対応ノード・欠損参照 | 生ノード/Pin保持、診断、誤った成功扱い防止 | 部分対応：Graph保持・リンク検証あり、実例未検証 |

### 実機検証手順

1. UE 5.7.4 Editorで各G01〜G07のBlueprintを作成・コンパイル・保存する。
2. 同じアセットについてSnapshot v1、Copilot Graph JSON、Semantic JSON v2（利用可能な場合）を出力する。
3. 各JSONのschema version、asset/graph path、ノードGUID、Pin GUIDを原Blueprintと照合する。
4. Exec/Dataリンクの端点を検証し、未接続Pinの値と未解決関数・型を確認する。
5. ケース別に「抽出済み / 部分対応 / 欠落 / 対象外」を記録し、欠落箇所にGraph/Node/Pin GUIDを添える。
6. 同一Blueprintを再エクスポートして差分を確認する。UEビルド・生成C++動作検証は別ゲートとする。

### 記録テンプレート

```text
Case: G__
Asset path:
Graph path:
Node GUID:
Pin GUID:
Exporter / schema version:
Expected:
Actual:
Result: 抽出済み | 部分対応 | 欠落 | 対象外
Diagnostics:
Reproduction steps:
UE Editor version / test date:
```

## CJ1へ引き継ぐ優先課題

1. 関数署名の引数順序・方向・型・戻り値を実例で確定する。
2. `has_default_value=false`が「指定なし」と「取得不可」を区別できない問題を解消する。
3. Enum/Array/Set/Map/Delegate等の未解決型を出典付きで診断する。
4. Graph JSONの`semantic_status=partial`は解析完全性の保証ではないことを維持する。
5. Snapshot v1 / Semantic JSON v2の既存契約は破壊せず、Copilot向けの追加フィールドを検討する。

> 注意: この文書は実装の静的監査とゴールデンケース**仕様**であり、ゴールデンJSON実物やUE実機テスト結果を含まない。CJ0の完了判定には実出力との照合が必要。
