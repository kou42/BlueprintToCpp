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
