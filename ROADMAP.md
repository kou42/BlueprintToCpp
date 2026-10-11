# BlueprintToCpp 開発ロードマップ

対象: Unreal Engine 5.7 / BlueprintAIConverter / PythonServer

## 目標

Blueprintを選択して **Convert to C++** を実行するだけで、Snapshotの取得、解析、C++コード生成、プレビュー、UnrealBuildToolによるビルド検証まで行えるツールを目指す。

> **状態の区別**: 「実装済み」は「統合テスト済み」ではない。実機確認できたのは、UE 5.7.4でのプラグインのビルドエラー修正後の起動と、`BP_ConvertTest` のJSON Snapshot出力。PythonからのC++生成および生成コードのUEビルドは未検証。

## フェーズ別ロードマップ

| フェーズ | 項目 | 現状 | 主な作業 |
| --- | --- | --- | --- |
| Phase 1 | Blueprint Snapshot Exporter | 実装済み・実機確認済み | UE 5.7.4でJSON出力成功 |
| Phase 2 | Python Parser / Graph IR | 実装済み・要修正 | Python構文不整合の解消、実Snapshotによる検証 |
| Phase 3 | 基本C++ Generator | 実装済み・要修正 | BeginPlay / PrintStringの実Snapshot対応 |
| Phase 4 | Branch / Typed Variables | 部分実装 | 条件分岐と変数代入の統合・テスト |
| Phase 5 | Data Flow / Expressions | 未実装 | VariableGet、比較、四則演算 |
| Phase 6 | Control Flow / Functions | 未実装 | Sequence、Loop、関数、複数イベント |
| Phase 7 | Unreal Editor UI | 未実装 | ワンクリック変換、C++プレビュー |
| Phase 8 | UBT Validation / AI Support | 未実装 | コンパイル検証、修正支援 |

## 最優先マイルストーン: BP_ConvertTestのEnd-to-End変換

実際にUE 5.7.4からエクスポートした `BP_ConvertTest` を回帰テストの基準にする。

1. **Python基盤の修正**: `schemas.py`、`generator_dispatch.py` 等の不正な改行・構文不整合を解消し、`pytest` を実行可能にする。
2. **実Snapshotの解析**: 未接続のTick、ActorBeginOverlap、Construction Scriptを適切に扱い、グラフ解析と変換対象の選択を分離する。
3. **Print Stringの意味保存**: `InString`、`Duration`、`TextColor`、画面・ログ出力フラグなどを検証し、対応するC++を生成する。未対応設定は明示的にエラーにする。
4. **UE 5.7.4で検証**: 生成された `.h` / `.cpp` を対象プロジェクトへ組み込み、UnrealBuildToolでビルドし、BeginPlayの動作を比較する。

**完了条件**: 実Snapshotのパース・C++生成が成功し、生成コードがUE 5.7.4でコンパイルでき、元Blueprintと観測可能な挙動が一致すること。

## 対応ノードの拡張順序

| 優先度 | 対応対象 | 変換例・目的 |
| --- | --- | --- |
| P0 | BeginPlay / Print String | `UKismetSystemLibrary::PrintString` |
| P1 | Variable Set / Get | `Health = 100;` |
| P1 | 比較演算 | `Health > 0` |
| P1 | Branch | `if / else` |
| P2 | 四則演算 | `+ - * /` |
| P2 | Sequence | 逐次実行 |
| P2 | For Loop / While | `for` / `while` |
| P2 | Function / Return | C++メンバー関数 |
| P3 | Cast / Object Reference | 型変換・参照 |
| P3 | Spawn Actor / Component | Unreal API呼び出し |
| P3 | Timeline / Delay | 非同期処理・状態管理 |

## 将来のUnreal Editor UI

1. Content BrowserでBlueprintを選択する。
2. `Convert to C++` を実行する。
3. 対象C++クラス名とモジュールを指定する。
4. 生成した `.h` / `.cpp` をプレビューする。
5. 非対応ノード・警告を確認する。
6. 生成・ビルド検証を実行する。

例: `BP_ConvertTest` → `AConvertTest` → `MyProject2` モジュール。

## 各フェーズ共通の完了条件

- **Pythonテスト成功**: 実際のUE 5.7.4 Snapshotを使った解析・生成テストを追加する。
- **C++ビルド成功**: 生成コードがUnrealBuildToolでコンパイルできる。
- **動作一致**: 元のBlueprintと生成C++が同じ結果を出す。
- **非対応処理の明示**: 変換できないノードや設定を黙って無視しない。
- **回帰テスト**: 新機能追加後も既存のBlueprintが変換できる。

## 現在の注意事項

- `BP_ConvertTest` のSnapshot出力は確認済みだが、Python解析・C++生成の統合成功は未確認。
- 現行のPythonコードには構文不整合が確認されており、修正が必要。
- 生成器は未接続イベント、Construction Script、Print Stringの追加パラメータをまだ十分に処理できない。
- AIによる変換・UBTによる自動検証は未実装。

**次の開発目標: `BP_ConvertTest` のEnd-to-End変換成功。**
