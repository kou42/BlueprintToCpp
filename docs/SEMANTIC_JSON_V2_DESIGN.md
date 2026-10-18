# Semantic JSON v2 設計仕様（J0）

状態: **J0当初の設計記録（現在は一部実装済み）**。本文中の「現行」「未実装」「J1受け入れ条件」は設計当時の記述であり、現在の実装状況を示すものではありません。最新の監査は [CJ0_JSON_AUDIT.md](CJ0_JSON_AUDIT.md)、実機往復検証は [J2-5-editor-roundtrip.md](J2-5-editor-roundtrip.md)、関数メタデータの追加契約は [CJ1_FUNCTION_METADATA_CONTRACT.md](CJ1_FUNCTION_METADATA_CONTRACT.md) を参照してください。  
対象: Unreal Engine 5.7.4 / BlueprintAIConverter / PythonServer

## 目的と非目標

既存のSnapshot JSON v1（`FBPSnapshotExporter`）を変更せず、型付きSemantic IRをAIや検証器へ受け渡すための、独立したバージョン付きJSON形式を定義する。Direct / AI / Hybrid の共通参照形式とする。

- J0では形式を定義するのみ。シリアライザ、AI Provider、Hybridの実装はJ1以降。
- Semantic JSONは「Blueprint全体を完全に表現できる」とは宣言しない。解決できた意味情報と未解決情報を区別する。
- Graph IRの完全な接続関係が必要な処理は、元のSnapshot v1またはGraph IRを参照する。Semantic JSONだけで完全復元できると仮定しない。

## 現行実装との対応

| 既存 | 現状 | v2への方針 |
| --- | --- | --- |
| `FBPSnapshotExporter` | schema_version=1、グラフ、ノード、ピン、リンク、変数 | 変更しない。入力由来情報として参照 |
| `FBPGraphIR` | AssetPath、ParentClassPath、Graphs、ExecEdges、DataEdges、Diagnostics | グラフ・ノード・ピンの出典IDを保持 |
| `FBPSemanticIR` | Functions、Blocks、Statements、Expressions、Diagnostics | 解決済みの意味情報をv2へ直列化 |
| `PythonServer/app/schemas.py` | Snapshot v1を厳密に検証 | v2は別パーサ・バリデータとする |
| `PythonServer/app/ir_builder.py` | graph_only IR | Semantic JSONと同一視しない |

## トップレベル契約

```json
{
  "schema_version": 2,
  "format": "blueprint_semantic_ir",
  "producer": {
    "name": "BlueprintAIConverter",
    "engine_version": "5.7.4"
  },
  "source": {
    "asset_path": "/Game/BP_Test.BP_Test",
    "parent_class_path": "/Script/Engine.Actor",
    "snapshot_schema_version": 1
  },
  "semantic_status": "partial",
  "functions": [],
  "diagnostics": []
}
```

- `schema_version` は整数2、`format` は固定文字列。
- `semantic_status`: `complete` / `partial` / `unsupported`。ここでのcompleteは**対象として解析した範囲**についてのみ意味解決できたことを表す。Blueprint全体の完全変換保証ではない。
- `producer` と `source` は必須。アセットパスは参照情報であり、外部AIへ送信する際は機密性に注意する。
- `functions` と `diagnostics` は空配列でも必須。未対応情報を黙って削除してcompleteにしない。
- 将来の破壊的変更は別のschema_versionで行う。未知のenum値・必須フィールド欠落はバリデーションエラー。

## 関数・文・式

```json
{
  "name": "ReceiveBeginPlay",
  "return_type": {"kind": "void", "type_path": "", "is_reference": false, "is_const": false},
  "blocks": [{
    "source_entry_node_id": "00000000-0000-0000-0000-000000000001",
    "statements": [{
      "kind": "expression",
      "source_node_id": "00000000-0000-0000-0000-000000000002",
      "assignment_target": null,
      "expression": {
        "kind": "literal",
        "type": {"kind": "string", "type_path": "", "is_reference": false, "is_const": false},
        "source_node_id": "00000000-0000-0000-0000-000000000002",
        "source_pin_id": "00000000-0000-0000-0000-000000000003",
        "literal_value": "Hello",
        "variable_name": null,
        "function_name": null,
        "function_owner_path": null,
        "arguments": []
      }
    }]
  }]
}
```

上記はフィールド形状の例であり、PrintStringの完全なLowering結果ではない。

- `functions[]`: `name`、`return_type`、`blocks` を必須とする。将来の引数・関数属性は明示的な拡張として定義する。
- `statements[].kind`: 現行の `expression` / `assignment` / `return`。未対応の分岐・ループは推測で構造化しない。
- `expression.kind`: `literal` / `variable_reference` / `function_call`。引数は再帰的な式配列。現行IRの表現可能範囲に合わせる。
- `type.kind`: `unresolved` / `void` / `bool` / `int32` / `int64` / `float` / `double` / `string` / `name` / `text` / `object` / `struct`。具体型は`type_path`に保持する。
- 任意情報の欠落と空文字列を区別するため、適用外の名称・代入先は`null`とする。未解決の型は`unresolved`を使う。
- ノード・ピンIDは元のGUIDを正規化した文字列とし、同一変換内で一貫させる。元のIDが存在しない場合は`null`を許可し、診断へ理由を記録する。偽のGUIDを作らない。

## 診断と安全な失敗

```json
{
  "severity": "error",
  "code": "UNSUPPORTED_NODE",
  "graph_name": "EventGraph",
  "node_id": "00000000-0000-0000-0000-000000000002",
  "pin_id": null,
  "message": "このノードの意味は未解決です"
}
```

- `severity`: `info` / `warning` / `error`。 `code`は機械判定用、`message`は人間向け。
- 現行`FBPSemanticDiagnostic`にはseverity/codeがないため、J1では診断変換規則を明示する。既存の`HasErrors()`が全診断をエラー扱いする現状を踏まえ、勝手にwarningへ格下げしない。
- `partial` / `unsupported`、`unresolved`、error診断がある入力から、検証済みC++を生成したと主張しない。
- AIが推定した型・関数・制御フローは元データと区別し、未検証の推定を自動採用しない。

## AI Providerへの受け渡し

Semantic JSONをProvider共通入力とし、プロンプト・モデル設定・応答・生成ファイル・診断を別オブジェクトで管理する。APIキーやトークンをJSONへ埋め込まない。モデルへ送信する前にアセット名、パス、文字列リテラル等の機密性を確認できるようにする。レスポンスのファイルパスは保存前に検証し、任意のパスへの書き込みを防ぐ。

## J1の受け入れ条件

- [ ] UE側にSemantic JSON専用Serializerを追加し、Snapshot v1出力の互換性を保持する
- [ ] Python側にv2専用の厳密なParser/Validatorを追加する
- [ ] `FBPSemanticIR`の現行フィールドを欠落なく直列化する
- [ ] source node/pin GUID、型、式ツリー、関数、文、診断のテストを追加する
- [ ] 未解決型、未対応ノード、不正enum、欠落フィールド、重複ID等を検出する
- [ ] 同じ入力に対して安定したJSONを生成する（配列順・フィールド順を規定）
- [ ] 代表ケースのSnapshot/Goldenテストを追加する
- [ ] UE 5.7.4でのAutomation Test、出力JSONのPython検証を実行する

## 後続の設計課題

1. Graph IRへのリンクとSemantic IRの出典をどの粒度で保持するか。
2. 変数定義、関数引数、CDO初期値、制御フロー、イベント固有メタデータのスキーマ拡張。
3. AIとDirectの比較単位、Hybridの安全な部分変換境界。
4. 生成コードのUHT/UBT、実行時意味一致、モデル間差分の自動検証。

J0は仕様の固定ではなく、J1実装前に実例とテストで検証するための初期契約である。
