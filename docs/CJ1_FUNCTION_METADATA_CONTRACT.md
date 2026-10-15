# CJ1: 関数宣言と既定値の取得状態

## 追加フィールド（Semantic JSON v2）

`functions[]` のうち `signature_source=generated_class_reflection` の関数に `function_attributes_source=generated_class_reflection` を追加する。既存の `is_native` / `is_static` / `is_const` / `is_blueprint_event` は維持する。

`functions[].parameters[]` には次を追加する。

| フィールド | 値 | 意味 |
| --- | --- | --- |
| `default_value_source` | `cpp_default_metadata` | `CPP_Default_<Param>` メタデータから取得 |
| `default_value_source` | `unavailable` | この取得経路では値を取得できない |
| `default_value_status` | `specified` | メタデータが存在する（空文字も有効な値） |
| `default_value_status` | `unavailable` | メタデータがない。既定値が存在しないとは断定しない |

`has_default_value` と `default_value` は従来どおり残す。**`unavailable` は `not_specified` と同義ではない**。Blueprint FunctionGraphのEntry Pin由来の既定値や呼び出しPinの既定値は、この段階では統合していない。

## 既存構造との関係

- Snapshot v1、Semantic JSON v2の`schema_version`、Copilot Graph JSON v1を変更しない。
- Copilot Graph JSONの`semantic_declarations` はSemantic JSON v2をそのまま内包するため、追加情報を利用できる。
- Native関数の呼び出し先はGraph IRのノード参照で判断する。GeneratedClass直下の宣言一覧だけで、継承Native関数がすべて列挙されたと誤認しない。

## 未対応・検証

Array/Set/Map/Delegate型、Blueprint FunctionGraphの引数既定値、継承UFunctionの署名統合、Override/Interface判定は引き続き課題。型や値を推測して埋めない。

UE Automation Test: `BlueprintAIConverter.SemanticJson.SerializeV2`。UE 5.7.4実機での実行、ビルド、実Blueprintとの照合は未実施。

## Enum型の追加対応

`FEnumProperty` は `type.kind=enum` と `type.type_path=<UEnumの正式パス>` を出力する。`FByteProperty` は `Enum` 参照が存在する場合のみ同様に扱う。通常の `uint8` は依然として `unresolved` となり、既存の診断が残る。

この拡張はEnum値のシリアライズやC++コード生成の対応を意味しない。UE実機で `FEnumProperty`、`TEnumAsByte`、通常の `uint8` を別々に確認すること。

## 再帰コンテナ型

`FArrayProperty` / `FSetProperty` は `type.kind=array/set` と `element_type`、`FMapProperty` は `type.kind=map` と `key_type` / `value_type` を出力する。子型も通常の `type` オブジェクトと同じ形式で再帰的に保持する。

例: `TMap<FString, TArray<int32>>` は `map(key_type=string, value_type=array(element_type=int32))`。子型が未解決の場合も `unresolved` を残し、関数パラメータに診断を追加する。これらは**宣言の抽出**であり、コンテナの既定値・C++生成への対応を意味しない。

再帰的なJSON型は共有所有の子型ポインタで保持するため、IRを複製する際に子型を変更する処理では共有の影響に注意する。UE実機ではArray/Set/Map、ネスト型、未対応のDelegate要素、空コンテナ既定値を検証する。

## FunctionGraph Entry Pinからの既定値（追加）

GeneratedClass直下の関数について、`CPP_Default_<Param>` がない非Return引数に限り、同名FunctionGraph内の `UK2Node_FunctionEntry` 出力Pinを照合する。Pin名が引数名と一致し、未接続かつ `DefaultValue` が非空の場合だけ `default_value_source=function_entry_pin` / `default_value_status=specified` とする。

新しい `default_value_source_node_id` / `default_value_source_pin_id` は出典GUID（それ以外はnull）。`CPP_Default_` を優先し、空文字や接続中のPinを値が指定された証拠とはみなさない。Pinの`DefaultObject` / `DefaultTextValue` やUserDefinedPinの既定値との照合は未対応であり、UE 5.7.4で実Blueprintを用いた確認が必要。

**重要:** Entry Pinに表示される値が関数宣言のデフォルト引数と同じ意味かどうかはBlueprintのノード種別とコンパイル状態に依存し得るため、現段階ではCopilotがC++デフォルト引数を無条件に生成する根拠にしない。

## Overrideの識別

GeneratedClass直下の`UFunction`に対して、親クラス階層の`FindFunctionByName`で同名の関数を探索する。存在する場合は`is_override=true`、`overridden_function_path`に親関数の正式パスを記録する。存在しない場合は`false`と`null`。既存の`is_native`、`is_blueprint_event`とは独立した属性として保持する。

`function_attributes_source`には`signature_source`をそのまま使用する。Interface由来の実装やBlueprint FunctionGraphにおけるユーザー定義のオーバーライド表現については追加検証が必要であり、`is_override`だけでInterface実装を判定しない。

## Interface関数の照合（暫定）

`Blueprint->ImplementedInterfaces` に直接登録されたInterfaceを調べ、GeneratedClass直下の関数と同名の`UFunction`がある場合、`is_interface_implementation=true`、`interface_class_path`、`interface_function_path`を出力する。未一致は`false`と`null`。複数Interfaceに同名関数がある場合は曖昧として診断し、`false`と`null`を出力する。

**制約:** 現状は関数名のみの照合であり、引数型・戻り値・継承Interface・Blueprintコンパイル状態との厳密な一致検証は未実装。したがって`is_interface_implementation`は現段階では「直接宣言されたInterfaceとの名前一致候補」であり、CopilotがC++の`override`を無条件に生成する根拠にはしない。実Blueprintで同名関数・複数Interface・継承Interfaceを確認すること。

## Interface関数シグネチャ照合の強化

直接宣言されたInterfaceとの関数名一致候補に対し、`UFunction`の`FUNC_Static`/`FUNC_Const`、`CPF_Parm`の宣言順・個数・引数名、`FProperty::SameType`による型、`CPF_ReturnParm`/`CPF_OutParm`/`CPF_ReferenceParm`/`CPF_ConstParm`を比較する。一致しない候補は`is_interface_implementation`を立てず、warning診断を記録する。複数の一致候補が残る場合は従来どおり曖昧として扱う。

この判定はReflection上の宣言一致を示すもので、Blueprintの実装グラフの有無や実際の呼び出し成功を証明するものではない。UE 5.7.4では引数名変更、型変更、out/ref/const差、複数Interface、Event形式のInterface関数、継承Interfaceをテストする。特にBlueprintでの関数名・Pin名のコンパイル後正規化差を確認すること。
