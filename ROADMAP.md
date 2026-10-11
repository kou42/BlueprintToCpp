# BlueprintToCpp 開発ロードマップ

対象: Unreal Engine 5.7.4 / BlueprintAIConverter / PythonServer  
更新日: 2026-10-11

## 最終目標

Unreal EditorでBlueprintを選択し **Convert to C++** を実行するだけで、Blueprintグラフの解析、C++中間表現（IR）の構築、`.h` / `.cpp` の生成、プレビュー、将来的なUnrealBuildTool（UBT）検証まで行えるツールを目指す。

**基本方針: 変換の本経路はUnreal EngineのC++プラグイン内で完結させ、既存のSnapshot JSON生成機能は削除せず維持する。** JSONはデバッグ、回帰テスト、Python版との比較、外部ツール連携に利用する。PythonServerもC++版が十分に検証されるまで残す。

> **状態の区別**: 「実装済み」「PRで修正済み」「ローカル実行確認済み」「UEでコンパイル確認済み」は区別する。現時点でUE 5.7.4のプラグイン起動と`BP_ConvertTest`のJSON Snapshot出力は確認済み。Pythonからの生成は実Snapshotで実行され、PrintStringのオプションピンに対する未対応エラーが段階的に判明している。生成コードのUEビルドと動作一致は未確認。

## 目標アーキテクチャ

```text
Unreal Editor / Blueprint Asset
            |
            v
    C++ Blueprint Analyzer
            |
            v
       C++ IR Builder
            |
            v
      Blueprint C++ IR
            |
       +----+-------------------+
       |                        |
       v                        v
 C++ Code Generator      Snapshot JSON Exporter
       |                 （既存機能を維持）
       v                        |
 .h / .cpp               デバッグ・回帰テスト
       |
       v
 Editor Preview / Save / UBT Validation（段階的に追加）

PythonServer: 移行期間中の比較対象・検証ツールとして維持
```

**設計原則**
- C++本経路はJSONの保存・再読み込みを必要としない。
- JSON Exporterは独立した機能として存続させ、既存Snapshotスキーマとの互換性を維持する。
- グラフ取得、IR構築、ノード変換、ファイル出力、Editor UIの責務を分離する。
- 未対応ノードや意味が不明なピンを黙って無視せず、診断情報として通知する。
- 既存Blueprintの意味を保持できる範囲から順に実装する。

## 新ロードマップ（C++ネイティブ化）

| フェーズ | テーマ | 現状 | 主な作業・完了条件 |
| --- | --- | --- | --- |
| Phase 1 | 既存実装の調査・移行設計 | **次の着手対象** | UEプラグイン、SnapshotExporter、Python解析・IR・生成器、Build.csを調査。責務・依存関係・移行順を確定する |
| Phase 2 | C++ IRとBlueprint直接解析 | 未着手 | `UEdGraph` / `UEdGraphNode` / `UEdGraphPin`からノード・ピン・型・Exec/Data依存を取得し、JSONを経由せずIRを構築する |
| Phase 3 | C++コード生成MVP | 未着手 | BeginPlay → リテラルPrintStringをC++で変換し、`.h` / `.cpp`を生成する。オプションピンを正しく解釈しUE 5.7.4でビルド・動作確認する |
| Phase 4 | 制御フロー・変数・式 | 未着手 | Variable Set/Get、Branch、比較、四則演算、Sequence、Loopを依存関係に沿って追加する |
| Phase 5 | Unreal Editor UI・ファイル出力 | 未着手 | Blueprint選択、Convert to C++、クラス名・モジュール指定、プレビュー、保存先、上書き確認、警告表示を統合する |
| Phase 6 | JSON互換性・Python比較・回帰検証 | 継続課題 | 既存JSON出力を維持し、実SnapshotとPython/C++生成結果を比較する。UEビルド・動作一致テストを蓄積する |
| Phase 7 | 高度なBlueprint・UBT検証 | 未着手 | 関数、複数イベント、Cast、配列、構造体、マクロ、Latent Action等を拡張し、UBT検証・修正支援を追加する |

Phase 6の検証は最終段階まで待たず、**Phase 2以降の各フェーズと並行して実施する**。Phase 5のUIもMVPを優先して早期に部分導入してよい。

## 直近のマイルストーン

### M0: 現行Python版と実Snapshotのベースラインを確保

- `BP_ConvertTest`の実Snapshot JSONを保存し、再現可能なテスト入力として扱う。
- Pythonの構文不整合修正、未接続イベント対応、PrintStringの`WorldContextObject`、`bPrintToScreen`、`bPrintToLog`対応は **PR #10で修正提案済み**。mainへの反映とテスト結果は別途確認する。
- `TextColor`、`Duration`、`Key`の明示値は未対応の可能性があり、意味を検証して段階的に実装する。
- Pythonテストと実Snapshot生成結果を確認し、C++版との比較基準を確定する。

### M1: C++直接解析・IRの完成

- 既存UEプラグインの構造、依存モジュール、命名規則を確認してから実装する。
- Blueprintノード・ピン・型・リンク・イベントシグネチャを取得する。
- Exec/Dataエッジ、循環、未接続イベント、関数グラフの扱いを定義する。
- **完了条件**: BeginPlay → PrintStringのグラフをJSONを介さずC++ IRにできる。

### M2: C++生成MVPの実機成功

- IRからActor派生クラスのヘッダーとソースを生成する。
- PrintStringの文字列、画面・ログ表示、その他オプションを正しく扱う。
- 出力ファイルの配置、モジュールAPI指定、生成ヘッダー、必要な依存モジュールを検証する。
- **完了条件**: `BP_ConvertTest`相当の生成コードがUE 5.7.4でコンパイルでき、BeginPlayの観測可能な動作が一致する。

### M3: Editorからのワンクリック変換

- Blueprint選択 → C++生成 → プレビュー → 保存をEditorから操作できるようにする。
- 既存ファイルの上書き防止、未対応ノードの一覧、エラー箇所の特定を提供する。
- **完了条件**: PythonやJSONの手動操作なしで、EditorからC++ファイルを出力できる。

## 対応ノードの拡張順序

| 優先度 | 対応対象 | 変換例・目的 |
| --- | --- | --- |
| P0 | BeginPlay / Print String | `UKismetSystemLibrary::PrintString`、引数の意味保存 |
| P1 | Variable Set / Get | 変数宣言、参照、代入 |
| P1 | 比較演算 | `Health > 0` |
| P1 | Branch | `if / else` |
| P2 | 四則演算 | `+ - * /` |
| P2 | Sequence | 逐次実行 |
| P2 | For Loop / While | `for` / `while` |
| P2 | Function / Return | C++メンバー関数 |
| P3 | Cast / Object Reference | 型変換・参照 |
| P3 | 配列 / 構造体 / Macro | データ構造・グラフ展開 |
| P3 | Spawn Actor / Component | Unreal API呼び出し |
| P3 | Timeline / Delay | 非同期処理・状態管理 |

## 既存機能の扱い

| 機能 | 方針 |
| --- | --- |
| UEプラグインのSnapshot JSON Exporter | **維持**。C++生成とは独立して利用可能にする |
| JSONスキーマ | 互換性維持を基本とし、拡張時はバージョン管理する |
| Python Parser / Graph IR | 移行期間中は維持し、比較・回帰テストに利用する |
| Python C++ Generator | 当面維持。C++版が十分に検証されるまで削除しない |
| C++直接解析 / C++ IR / Generator | 新しい主経路として実装する |
| UBT Validation / AI Support | 後続フェーズで検討する |

## 各フェーズの品質基準

1. **既存機能の維持**: Snapshot JSON出力が引き続き動作する。
2. **グラフ意味の保持**: 実行順序、データ依存、ピン既定値、型情報を正しく扱う。
3. **安全な未対応処理**: 未対応ノード・ピンは明示的にエラーや警告として通知する。
4. **回帰テスト**: 最小グラフ、実Snapshot、境界値、異常な接続をテストする。
5. **UE実機確認**: 生成したC++をUE 5.7.4でコンパイルし、元Blueprintと動作を比較する。
6. **移行時の比較可能性**: Python版とC++版の出力差分を把握できる状態を保つ。

## 現時点の実装状況と注意事項

- **確認済み**: UE 5.7.4でのプラグイン起動、`BP_ConvertTest`のSnapshot JSON出力。
- **Python側に既存実装あり**: Snapshot検証、グラフIR、限定的なBeginPlay/PrintString、Branch、変数関連の生成器。
- **修正PRあり**: [PR #10](https://github.com/kou42/BlueprintToCpp/pull/10) でPython構文、未接続イベント、PrintStringの一部ピン対応とテストを追加。マージ・テスト実施状況は別途確認する。
- **文書更新PRあり**: [PR #12](https://github.com/kou42/BlueprintToCpp/pull/12) でPythonServer文書の日本語化とC++生成手順を追加。
- **未検証**: Pythonの全テスト成功、生成C++のUEビルド、BlueprintとC++の動作一致。
- **今後の注意**: UE APIや生成コードの署名・Build.csの依存は、既存実装とUE 5.7.4の実機で確認してから確定する。

**次の開発目標: Phase 1の既存実装調査とC++移行設計を完了し、JSON Exporterを残したままC++ IR基盤に着手する。**
