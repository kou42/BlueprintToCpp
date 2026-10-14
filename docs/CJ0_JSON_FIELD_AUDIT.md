# CJ0: JSONフィールド監査・ゴールデンケース定義

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
