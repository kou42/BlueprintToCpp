# Copilot Graph JSON スキーマ v1

## 目的

Unreal Engine の Blueprint グラフ構造を、GitHub Copilot Chat/Agent に参照させるための補助データとして出力する。C++の自動生成を保証するものではない。

- 形式識別子: `blueprint_copilot_graph`
- `schema_version`: `1`
- 出力先: `Saved/BlueprintCopilotJson/<アセットパスをファイル名化>.copilot.graph.json`
- 既存の Semantic JSON v2 および Snapshot v1 とは独立した形式
- `semantic_status`: 現段階では常に `partial`

## ルートのフィールド

| キー | 型 | 内容 |
| --- | --- | --- |
| `schema_version` | number | スキーマバージョン |
| `format` | string | 形式識別子 |
| `engine_version` | string | 出力時のUEバージョン |
| `asset_path` | string | Blueprintのアセットパス |
| `parent_class_path` | string | 親クラスのパス |
| `semantic_status` | string | 意味解析の完成度。現在は `partial` |
| `graphs` | array | グラフ一覧 |
| `diagnostics` | string[] | 抽出時の診断メッセージ |
| `semantic_declarations` | object (optional) | Semantic JSON v2に準拠する宣言・診断情報。Semantic IR指定時に追加 |

## グラフ

各 `graphs[]` には以下を含む。

- `name`, `path`, `kind`: グラフ識別情報
- `nodes`: ノード配列
- `exec_edges`: 実行フロー接続
- `data_edges`: データフロー接続

### ノード

`nodes[]` のフィールド:

- `id`: ノードGUID
- `class_path`, `title`: ノードクラスと表示名
- `function_name`, `function_owner_path`, `function_path`: 呼び出し先の関数参照
- `function_resolved`: UFunction参照の解決可否
- `function_native`, `function_static`, `function_pure`, `function_const`: 解決済み関数の属性
- `variable_name`: 変数ノードで参照される変数名
- `pins`: Pin配列

`function_resolved=false` の場合、関数名・表示名だけでC++呼び出し先を確定しないこと。

### Pin

`pins[]` のフィールド:

- `id`, `name`: Pin識別情報
- `direction`: `input` または `output`
- `category`, `subcategory`, `subcategory_object_path`: Pin型情報
- `default_value`, `default_object_path`, `default_text`: 既定値表現

既定値の空文字列は、値が空であることと値が存在しないことを区別するため、文字列として保持する。

### 接続

`exec_edges[]` と `data_edges[]` は同じ構造:

| キー | 型 | 内容 |
| --- | --- | --- |
| `source_node_id` | string/null | 接続元ノードGUID |
| `source_pin_id` | string/null | 接続元Pin GUID |
| `target_node_id` | string/null | 接続先ノードGUID |
| `target_pin_id` | string/null | 接続先Pin GUID |

出力時には、各グラフ内のノードGUIDとPin GUIDの有効性・重複、接続先の実在を検証する。不正な場合はエラーとして書き出しを中止する。

## 出力順序

差分の安定化のため、グラフは `path` 順、ノードとPinは `id` の文字列順、接続は `source_node_id / source_pin_id / target_node_id / target_pin_id` 順、診断は文字列順に出力する。

## Copilotでの利用上の注意

1. JSONをCopilot Chat/Agentに渡し、必要な `.h/.cpp` を生成させる。
2. `semantic_status=partial` を前提に、未解決ノード・関数参照・型を推測だけで埋めない。
3. Blueprint特有のライフサイクル、イベント、依存アセットを別途確認する。
4. 生成コードはUnreal Engine上でビルドし、元のBlueprintと動作を比較する。

## 現時点の制限

- Blueprintの全機能を意味的に再構成するものではない。
- 関数・変数の宣言情報は `semantic_declarations.functions` / `semantic_declarations.variables` に格納する。未解決型や既定値の未取得はそのまま保持し、完全なC++宣言を保証しない。
- `semantic_declarations` は既存Semantic JSON v2のオブジェクトを含むため、将来のスキーマ変更では互換性を確認する。
- JSONの構造と出力順序に関するAutomation Testは追加済みだが、UE 5.7.4での実行確認は未実施。
