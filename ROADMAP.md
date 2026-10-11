# BlueprintToCpp 開発ロードマップ

対象: Unreal Engine 5.7.4 / BlueprintAIConverter / PythonServer  
更新日: 2026-10-12

## 最終目標

Unreal Editor上でBlueprintを選択し、グラフの意味を保ちながらC++クラスの宣言・実装を生成、プレビュー、保存、ビルド検証できるツールを開発する。

**重要な方針変更:** PrintStringなどのノードごとに専用のC++生成器を増やすのではなく、**Graph IR → Semantic IR → C++ Generator** を基本構造とする。Unreal Engineのリフレクション（UFunction / FProperty）を活用して通常の関数呼び出しを汎用化し、特殊ノードだけに専用変換規則を設ける。

すべてのBlueprintを一度にサポートすることは目標にしない。変換対象を明示し、意味を保証できない場合は診断付きで拒否する。

## 目標アーキテクチャ

```text
Blueprint Asset
     |
     v
UEdGraph / UEdGraphNode / UEdGraphPin
     |
     v
Graph IR（既存: ノード・ピン・接続を保持）
     |
     v
Semantic Analyzer / Type Resolver
     |     UFunction / FProperty / Blueprint metadata
     v
Semantic IR（新規: 式・文・呼び出し・変数・制御フロー）
     |
     v
C++ Generator（宣言・式・文・関数・クラス）
     |
     v
.h / .cpp → Editor Preview / Save → UBT検証（将来）

別経路: Snapshot JSON Exporter（維持）
比較対象: PythonServer / Python Generator（移行期間中は維持）
```

## 現状（2026-10-12時点）

| 領域 | 状況 | 補足 |
| --- | --- | --- |
| Blueprintの直接解析とGraph IR | **限定的に実装済み** | ノード・ピン・Exec/Dataエッジを収集。汎用型解決は未実装 |
| C++生成MVP | **限定的に実装済み** | ActorのBeginPlay → PrintStringを対象とする専用Analyzer |
| PrintStringのオプション | **実装済み・UE実機未検証** | TextColorのRGBA解析、Duration、Key、画面/ログ設定 |
| 標準グラフ・イベントの判定 | **限定的に実装済み** | 空Construction Script、未接続Event Tickの一部を許容 |
| Editorプレビュー・保存 | **実装済み・実機での全面検証は未完** | Convert to C++ (Preview)、プロジェクトSourceへの保存経路 |
| Snapshot JSON / PythonServer | **維持** | 比較・デバッグ・回帰検証に活用 |
| Semantic IR / 汎用関数変換 | **未実装** | 次の最優先課題 |
| UE 5.7.4での生成C++ビルド・動作一致 | **未確認** | 実機検証を必須とする |

GitHubへのコミットとUEでのコンパイル成功・動作確認は区別する。上記の「実装済み」は実機での正常動作を保証しない。

## 更新版フェーズ

| フェーズ | 優先度 | テーマ | 主な作業と完了条件 |
| --- | --- | --- | --- |
| Phase 0 | **最優先** | 現行MVPの安定化・ベースライン | UE 5.7.4でBeginPlay→PrintStringを生成・ビルド・実行し、文字色や未接続Tickを含めて元BPとの動作一致を確認。回帰テストを整備 |
| Phase 1 | **最優先** | Semantic IR設計 | Graph IRを維持しつつ、型付き式、関数呼び出し、変数参照、代入、文、基本ブロック、診断情報の表現を定義。既存MVPを移行 |
| Phase 2 | **高** | 型解決と汎用UFunction呼び出し | UFunction / FPropertyから所有クラス、引数、戻り値、const、参照、静的/メンバー呼び出しを解決。対象関数のC++呼び出しを汎用生成し、代表的なUE APIで検証 |
| Phase 3 | **高** | 式・変数・データフロー | リテラル、数値演算、比較、変換、Get/Set Variable、ローカル/メンバー変数、純粋ノードの依存順を扱う。循環・副作用を診断 |
| Phase 4 | **高** | 制御フロー構造化 | Branch、Sequence、Return、For/WhileなどをCFGから構造化し、実行順序と分岐の意味を保存 |
| Phase 5 | **高** | Blueprint自作関数 | FunctionGraphsを関数宣言/定義に変換。引数、戻り値、ローカル変数、関数間呼び出し、再帰・参照を段階的に対応 |
| Phase 6 | **中** | イベント・クラス構造 | 複数イベント、イベントディスパッチャ、継承、プロパティ、コンポーネント、必要なUCLASS/UPROPERTY/UFUNCTION宣言を拡張 |
| Phase 7 | **中** | 特殊ノードの変換 | Cast、SpawnActor、配列/Map/Set、構造体、Interface、Macro展開、Timeline、Delay等を意味論と実装難度に応じて個別対応 |
| Phase 8 | **並行** | Editor統合・UBT・品質保証 | 診断UI、生成差分、保存安全性、ビルド検証、Blueprint/C++動作比較、回帰テストを継続強化 |

**Phase 8の検証は全フェーズで並行実施する。** Phase 0完了を確認したらPhase 1→2を優先し、個別ノード追加は原則としてSemantic IR経由で行う。

## 汎用変換と特殊変換の境界

| 区分 | 対象例 | 方針 |
| --- | --- | --- |
| 汎用関数呼び出し | 通常のUK2Node_CallFunction、C++で定義されたBlueprintCallable関数 | UFunction/FPropertyから署名・引数・戻り値を解決。Blueprintメタデータによる隠し引数・呼び出し規約は検証する |
| Blueprint定義の自作関数 | FunctionGraphsとその呼び出し | 関数本体をSemantic IRへ変換し、生成クラスのC++メンバー関数として出力 |
| 制御フロー | Branch、Sequence、Loop、Return | Execグラフを解析し、C++の文・ブロックへ構造化 |
| 純粋ノード・データ | 数値演算、比較、変数、型変換 | 型付き式として生成し、依存関係と評価回数を保持 |
| 特殊ノード | SpawnActor、Dynamic Cast、Macro、Delay、Timeline | 標準のUFunction呼び出しだけでは表せない意味を専用Loweringで処理 |

**注意:** UFunctionの存在だけでC++呼び出しが正しく生成できるとは限らない。CustomThunk、BlueprintInternalUseOnly、WorldContext、AutoCreateRefTerm、ExpandEnumAsExecs、Latentなどは適合性チェックや個別Loweringが必要。Blueprint内の自作関数と、既存C++関数の呼び出しも別の課題として扱う。

## 直近のマイルストーン

### M0: PrintString MVPを実機で安定化
- [ ] UE 5.7.4で生成したActorの.h/.cppがコンパイルできる
- [ ] BeginPlayの文字列、TextColor、Duration、Key、画面/ログ設定が一致する
- [ ] 空Construction Script、未接続Tick、接続済み未対応ノードで安全に成功/失敗する
- [ ] 成功/失敗ケースの自動テストと再現用Blueprintを整備する

### M1: Semantic IRの最小縦断実装
- [ ] 型・式・関数呼び出し・文・基本ブロックのデータ構造を設計
- [ ] Graph IRからSemantic IRへBeginPlay→PrintStringをLowering
- [ ] 既存のPrintString専用Analyzerとの結果一致をテスト
- [ ] 新Generatorで同等の.h/.cppを生成し、UEでビルド確認

### M2: 汎用関数呼び出しの実証
- [ ] UFunction/FProperty解決とC++署名の検証を実装
- [ ] 引数の既定値・参照/出力引数・ターゲット/静的呼び出しを処理
- [ ] GetActorLocation、SetActorLocation等の代表例を検証
- [ ] 未対応メタデータ・特殊関数を明示的に拒否

### M3: 自作関数への最短経路
- [ ] 四則演算・比較・変数・ReturnをSemantic IRで生成
- [ ] 引数と戻り値を持つ単純なBlueprint FunctionGraphをC++へ変換
- [ ] 関数呼び出しの参照解決と複数関数の生成を検証

## 実装・品質の原則

1. **意味の保存**: 実行順序、型、既定値、副作用、イベントの意味を推測で省略しない。
2. **安全な失敗**: 未対応ノードやピンを黙って無視しない。グラフ名・ノード・理由を診断へ出す。
3. **責務の分離**: Graph IR、Semantic Analyzer、Lowering、C++ Generator、Editor UIを分離する。
4. **小さな縦断実装**: 大規模な一括実装より、実際にビルド・動作確認できる変換ケースを増やす。
5. **回帰テスト**: 単体テスト、Snapshot比較、生成コードのUEビルド、元Blueprintとの動作一致を併用する。
6. **既存機能の維持**: Snapshot JSON Exporter、PythonServer、Editor UIを段階移行中も残す。
7. **UEバージョン依存性**: Blueprintコンパイラ内部APIを活用する場合はUE 5.7.4での公開範囲・安定性を確認する。
8. **コード規約**: C++のifは波括弧を必須とし、null判定を明示する。重要な設計判断は日本語コメントで説明する。

## 次の着手対象

**Phase 0のUE実機検証を完了させた上で、Phase 1のSemantic IR最小実装に着手する。** 以後のノード追加は原則としてSemantic IRを経由させ、PrintString専用ロジックの横展開を避ける。
