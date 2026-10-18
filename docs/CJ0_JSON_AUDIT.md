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


---

## 補足: Graph IR と Semantic JSON v2 の詳細フィールド監査（2026-10-15時点）

以下は旧 `CJ0_JSON_FIELD_AUDIT.md` の監査記録を移管したものです。時点の異なる実装状況は最新の仕様と混同せず、実機未検証の判定を維持します。


対象: `kou42/BlueprintToCpp` main（2026-10-15確認）  
関連計画: [Copilot JSONエクスポートロードマップ](COPILOT_JSON_EXPORT_ROADMAP.md)  
状態: **ソース監査済み／実Blueprint出力とUE実機の照合は未実施**

## 監査方法と判定

`BPGraphIR.cpp`、`BPSemanticFunctionExtractor.cpp`、`BPSemanticIR.h`、`BPSemanticJsonSerializer.cpp`、`BPSemanticJsonExporter.cpp` の実装を照合した。以下の「あり」は**コードに収集または出力処理が存在する**という意味であり、実際のBlueprintで検証済みという意味ではない。

- **あり**: 該当の値を収集し、該当形式で出力する処理がある
- **部分**: 一部の情報のみ保持、またはLowering/型対応などの制約あり
- **なし**: 調査対象の経路で保持・出力されない
- **未検証**: UE 5.7.4で実際のエクスポート結果を照合していない

## フィールド対応表

| 情報 | Graph IR（メモリ内） | Semantic JSON v2（出力） | 判定・次の作業 |
| --- | --- | --- | --- |
| Blueprintアセット・親クラス | `AssetPath` / `ParentClassPath` | `source.asset_path` / `parent_class_path` | あり |
| グラフ名・パス・種別 | `Graphs[].Name/Path/Kind` | `graph_traces[].name/path/kind` | あり（追跡用） |
| ノードGUID・クラス・表示タイトル | `Nodes[].Id/ClassPath/Title` | `graph_traces[].node_ids`のみ | **部分**。v2にノード詳細はない |
| Pin GUID・名前・方向・型・既定値 | `Pins[].Id/Name/Direction/Category/SubCategory/SubCategoryObjectPath/DefaultValue/DefaultObjectPath/DefaultText` | `graph_traces[].pin_ids`のみ | **部分**。v2にPin詳細はない |
| Exec/Dataリンク | `ExecEdges` / `DataEdges` | `graph_traces[].exec_edges/data_edges` | あり。出力元・先のNode/Pin GUIDを保持 |
| CallFunction名・Owner | `FunctionName` / `FunctionOwnerPath`（対象解決時） | 解決済み式の`function_name/function_owner_path` | **部分**。全Callノードの識別子や解決状態は出力されない |
| Variableノードの参照名・Self | `VariableName` / `bVariableSelfContext` | 変数参照式や変数宣言（同一情報とは限らない） | **部分** |
| 関数名・Owner・署名の由来 | Graph IRにはFunctionGraph名のみ | `functions[].name/owner_class_path/signature_source` | **部分**。Owner/Sourceは署名抽出済み関数にのみ出力 |
| 引数名・型・out/return | Graph IRのPin情報 | `functions[].parameters[].name/type/is_out/is_return` | **部分**。GeneratedClass直下のUFunctionのみ反射抽出 |
| 戻り値 | Graph IRのPin情報 | `functions[].return_type` | あり（未解決型あり） |
| 引数デフォルト値 | Pinの`DefaultValue`等 | `functions[].parameters[]`に項目なし | **なし**。Pin値とUFunctionメタデータの統合が必要 |
| Native/Blueprint・関数flags | CallFunctionのOwner/Nameのみ | `functions[]`に項目なし | **なし**。反射flags/定義元の取得が必要 |
| Enum/Container/Delegate型 | Pinカテゴリ等 | `type.kind=unresolved`になる場合あり | **部分**。Extractorは未対応型を推測しない |
| メンバー変数の型・既定値 | `BlueprintVariableCount`のみ | `variables[]`に型・flags・既定値可否・値 | **部分**。SerializerはIR内の一部フィールドのみ出力 |
| 文・式・Branch/Return | 生のノード・接続 | `functions[].blocks[].statements`、`then_statements/else_statements` | **部分**。Lowering対応範囲に限定 |
| 未解決・エラー診断 | `Diagnostics[]`文字列 | `diagnostics[]`（severity/code/graph/node/pin/message） | **部分**。Graph IR診断がすべて伝搬するかは別途検証 |
| 解析完全性 | 明示的な状態なし | `semantic_status` | **部分**。現在はerrorなら`unsupported`、それ以外は`partial`。completeを出さない |
| JSONの保存 | 該当なし | `Saved/BlueprintSemanticJson/<asset>.semantic.json` | あり。UTF-8 BOMなし |

### 重要な境界

- `BPGraphIRBuilder::Build` は `UbergraphPages`、`FunctionGraphs`、`MacroGraphs` を走査する。ただし、現状のSemantic JSON v2はGraph IRの**全ノード詳細をシリアライズするものではない**。
- `FBPSemanticFunctionExtractor::Append` は `Function->GetOuterUClass() == GeneratedClass` の関数だけを対象とする。親クラスのNative関数の**呼び出し**はGraph IR側で収集しても、同じ関数が`functions[]`に列挙されるとは限らない。
- `parameters[]` は `CPF_Parm` のPropertyを走査し、return Propertyも `is_return=true` で含める。利用側は戻り値を通常の入力引数と誤認しないこと。
- `graph_traces[]`は出典GUIDと接続の追跡用であり、単独ではPin名・型・デフォルト値やノードの意味を復元できない。
- `BPSemanticJsonExporter`はJSONを保存するが、**Copilotへ送信しない**。保存成功と意味的な完全性は別である。

## 最小ゴールデンケース（CJ0）

以下は**これから実Blueprintで作成・検証するテスト仕様**であり、出力実測結果ではない。GUIDは固定値を仮定せず、アセットごとに取得した実値を比較する。

| ID | Blueprintの作成内容 | JSONで検査する点 | 現時点の見込み |
| --- | --- | --- | --- |
| G01 | ActorのBeginPlay→PrintString | Eventノード、CallFunction、Exec接続、文字列Pin既定値 | v2のノード/Pin詳細不足 |
| G02 | float引数2個・float戻り値の`CalculateDamage` FunctionGraph | 関数署名、引数順、戻り値、Return、既定値の有無 | 署名は一部あり、既定値は不足 |
| G03 | `GetActorLocation`→`SetActorLocation` | Native関数Owner、Target、Pure、Exec/Data接続 | Ownerは一部あり、flags不足 |
| G04 | bool条件のBranch→各経路の処理 | True/False Pin、接続、実行順 | TraceのみではPin意味不足 |
| G05 | Blueprint自作関数から別Blueprintの関数を呼ぶ | 呼び出し先のAsset/Class/Function、依存解決状態 | 依存関係は未対応 |
| G06 | 未対応のMacroまたはDelegateを含むグラフ | 生ノード・Pin・接続、診断、partial/unsupported | 生Graph IRは保持、v2は情報不足 |
| G07 | 参照/out引数、Enum/Array引数 | Pin型・引数方向・未解決型診断 | 未対応型あり |

## 再現・検証手順

1. UE 5.7.4のテストプロジェクトでG01〜G07を作成し、アセット名とUEバージョンを記録する。
2. 既存のSemantic JSONコピー/保存操作で各アセットをエクスポートする。必要なら既存Snapshot v1も別ファイルとして保存する。
3. `schema_version=2`、`format=blueprint_semantic_ir`、`semantic_status`、`source`、`functions`、`graph_traces`、`diagnostics`を確認する。
4. ノード・Pin GUIDの参照整合性、Exec/Dataエッジの端点、関数署名とBlueprint Editor表示を比較する。**出力がないフィールドを推測して埋めない**。
5. G01〜G07のJSONを個別にGolden Fixtureとして保存し、個人情報・アセットの機密情報を除去したうえで差分テストを行う。GUIDや出力順序の安定性も検証する。
6. UE Automation TestとJSON validatorの実行結果を別々に記録する。Copilot生成コードのUHT/UBT・動作比較はCJ4で実施する。

### 検証記録テンプレート

| ID | UE Export | Schema検証 | Node/Pin完全性 | 関数署名 | 診断適切性 | 備考 |
| --- | --- | --- | --- | --- | --- | --- |
| G01〜G07 | 未実施 | 未実施 | 未実施 | 未実施 | 未実施 | 実機結果で更新 |

## CJ1着手時の優先事項

1. **関数の署名・デフォルト値・Native/Blueprint属性**を既存IRと互換性を保って拡張する。
2. **CallFunctionの完全修飾参照と解決状態**を保持し、Native呼び出しとBlueprint実装を混同しない。
3. Copilot用に生Graph IRのノード/Pin詳細を渡す方法を決定する。既存Semantic JSON v2へ無断で破壊的変更を加えず、別manifestまたは新バージョンを検討する。

**CJ0の完了条件はまだ未達**: フィールド監査とテストケース定義は完了したが、実Blueprint JSONとの照合・Golden Fixture・UE実機検証は未実施。
