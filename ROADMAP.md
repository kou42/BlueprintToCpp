# BlueprintToCpp 開発ロードマップ

対象: Unreal Engine 5.7.4 / BlueprintAIConverter / PythonServer  
更新日: 2026-10-12（社内利用を想定したDirect/AI二重生成・差分検証ワークフローを優先方針に追加）

## 最終目標

Unreal Editor上でBlueprintを選択し、グラフの意味を保ちながらC++クラスの宣言・実装を生成、プレビュー、保存、ビルド検証できるツールを開発する。

**重要な方針変更:** PrintStringなどのノードごとに専用のC++生成器を増やすのではなく、**Graph IR → Semantic IR → C++ Generator** を基本構造とする。Unreal Engineのリフレクション（UFunction / FProperty）を活用して通常の関数呼び出しを汎用化し、特殊ノードだけに専用変換規則を設ける。

すべてのBlueprintを一度にサポートすることは目標にしない。変換対象を明示し、意味を保証できない場合は診断付きで拒否する。

## 社内利用を想定した優先ワークフロー（新方針）

**中心機能は「DirectとAIによる独立したC++生成 → 差分比較 → 過不足・意味検証 → 人による採用判断」** とする。GitHubでのバージョン管理やGitHub Actionsを利用できない社内環境でも、Unreal Editorとローカルファイル／許可済みAI接続だけで動作する設計とする。開発用GitHub CI（PR #49）は任意の品質保証手段であり、利用時の必須要件ではない。

```text
Blueprint Asset
  ├─ Graph IR → Semantic IR → Direct Generator → Direct .h/.cpp
  └─ Semantic JSON v2 → AI Provider → AI .h/.cpp
                                           |
                         ファイル対応付け・テキスト差分
                                           |
                    宣言・型・処理順・副作用・過不足の意味検証
                                           |
                      一致 ──────────────── 差分あり
                        |                       |
                 共通検証へ             診断・修正候補を提示
                        |                       |
                        |                  人が修正・再比較
                        └──────────┬────────────┘
                                   |
                  C++静的検査・UHT/UBT・BP動作比較
                                   |
                      人による承認 → 安全な保存
```

- **一致は合格ではない。** テキストが一致しても両経路に共通の欠落や誤りがあり得る。差分ゼロは共通検証に進む条件であり、自動承認条件ではない。
- **差分は即失敗ではない。** 書式・命名・コード構造の違いと、引数・戻り値・条件分岐・副作用・呼び出し順などの意味差分を分けて表示する。機械的に同値性を証明できないものは「要確認」とする。
- **片側失敗は未比較。** Direct未対応・AI未設定・通信失敗・JSONがpartial/unsupportedの場合、無理に一致判定せず、未生成・未比較・検証保留を明示する。AIだけの結果を自動採用しない。
- **修正の追跡可能性。** Blueprint識別子、Semantic JSON、両方の生成物、診断、差分、検証結果、採用した修正、実行日時、Provider/モデル情報（秘密情報は除外）をローカル保存できるようにする。機密情報をログへ無断出力しない。
- **AI利用ポリシー。** 社内Blueprintのアセットパス・型・ロジック等は機密になり得る。外部AIへの送信は組織の承認と利用条件を確認した場合のみ許可し、ローカルLLM・社内承認済みProvider・AI無効の選択肢を確保する。
- **GitHub Copilot。** 利用可能性は未確認。IDE上での差分レビュー・修正支援候補として調査する。Copilot Chatが利用できても、BlueprintToCppから任意のJSONを送信する正式なProvider APIが利用できるとは限らないため、既存OpenAI APIと同一視しない。組織の契約・利用権限・送信ポリシー・提供APIを確認してから統合を判断する。
- **Hybridの位置付け。** 当面は自動的なC++の文字列結合ではなく、Direct/AIの比較・修正候補提示を優先する。安全なSemantic IR単位の部分統合は、検証基盤完成後に検討する。

### 新しい優先マイルストーン

| 順序 | ID | 実装内容と完了条件 |
| --- | --- | --- |
| 1 | D1: 生成結果の共通表現 | Direct/AIのファイル・診断・入力ハッシュ・生成元・失敗状態を同じ形式で保持。変換経路は独立 |
| 2 | D2: ファイル／行Diff | .h/.cppを対応付け、追加・削除・変更・片側欠落を表示。改行差などを区別し、再現可能な差分レポートを出力 |
| 3 | D3: 構造・過不足比較 | クラス・関数・変数・型・引数・戻り値・呼び出し・条件・実行順・副作用を比較。判定不能は要確認とし、過剰な同値判定をしない |
| 4 | D4: 修正とレビュー | 差分の理由・修正候補・人の承認・再比較を記録。生成ファイルの自動上書きを避ける |
| 5 | V2: 最終検証 | 共通Validator、C++静的検査、UHT/UBT、Blueprintとの実行時比較を段階実装。失敗・未実施を明示 |
| 6 | E2: Editor統合とローカル履歴 | 2経路の実行・比較画面、差分／診断表示、承認後保存、ローカル監査記録。GitHub不要で利用可能 |
| 並行 | P1: 社内AI接続調査 | Copilotの利用形態とAPI可否、社内承認済みProvider、ローカルLLM、データ持ち出し制約を調査 |

**着手順:** D1 → D2 → D3 → D4 → V2 → E2。P1と既存Directの機能拡張・検証は並行。既存J0/J1/A1/A2/H1/E1/V1は履歴と長期計画として維持し、当面の実装優先度は本節を優先する。

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

## 追加目標: Direct / AI / Hybrid の3モード変換

既存の **Graph IR → Semantic IR → C++ Generator** をDirectモードの中核として維持しつつ、Blueprintの情報をJSONへ出力して汎用AIでC++へ変換する独立経路を並行開発する。最終的に次の3モードを提供する。

| モード | 入力・変換 | 基本方針 |
| --- | --- | --- |
| Direct | Graph IR → Semantic IR → 既存C++ Generator | 決定的な規則で生成。未対応部分は明示診断 |
| AI | Blueprint Snapshot / Semantic JSON → AI Provider → C++ | OpenAI・Anthropic・Gemini・ローカルLLM等を差し替え可能にし、生成物を検証 |
| Hybrid | Semantic IRを基準にDirectとAIを組み合わせる | Directで保証できる部分を優先し、未対応部分のみAIに委譲。境界・依存・副作用を検証 |

### JSONとAI変換の設計原則

- 既存の `FBPSnapshotExporter` とPythonServerのSnapshot schema v1は互換性を維持する。AI向けの新しいSemantic JSONは別スキーマ・バージョンとして追加し、既存形式を無断で変更しない。
- 元ノード・ピンのGUID、グラフパス、Exec/Dataリンク、型、既定値、変数・関数・クラス情報、診断を可能な限り保持し、生成C++との追跡可能性を確保する。
- Graph IRの生の接続情報と、Semantic IRの構造化された式・文・制御フローを区別する。意味を解決できない情報をAI向けJSONで勝手に補完しない。
- AI Providerは共通インターフェースとし、プロンプト構築・モデル呼び出し・構造化応答の検証・エラー処理を分離する。APIキーはソース・ログ・Snapshotに含めない。
- AIが生成したコードを無条件に採用しない。生成結果の構文・型・Unreal API・依存関係・UHT/UBT・実行時の意味一致を段階的に検証する。
- DirectとAIの出力一致は正しさの証明ではない。異なる結果は診断として残し、検証できない候補は自動採用しない。
- Hybridではノード単位の文字列貼り合わせを避け、式の評価順、副作用、スコープ、制御フロー、型境界を保持した安全な単位で統合する。
- 既存のEditor Preview / SaveとPythonServerを維持し、モード選択を段階的に追加する。AI未設定・通信失敗時にもDirectは動作可能にする。

### JSON・AI並行開発マイルストーン

| マイルストーン | 優先度 | 作業・完了条件 |
| --- | --- | --- |
| J0: 現状整理とスキーマ設計 | **最優先・並行** | Snapshot v1、Graph IR、Semantic IR、PythonServerの差分を整理。Semantic JSON v2の仕様、互換性、診断・追跡IDを定義 |
| J1: Semantic JSON出力 | **高・並行** | 既存Exporterを壊さず、Semantic IRからバージョン付きJSONを出力。代表グラフの往復・Snapshot・不正入力テスト |
| A1: AI完全変換の最小縦断 | **高・J1後** | 共通Provider、モックProvider、構造化応答、BeginPlay→PrintStringの.h/.cpp生成と失敗時診断 |
| A2: 複数AI対応と比較 | **中・A1後** | Provider切替、モデル設定、タイムアウト・再試行・トークン上限・費用配慮、Direct/AIの差分・診断表示 |
| H1: Hybridの限定縦断 | **高・A1後** | 安全な部分変換境界を定義し、Direct未対応の限定ケースをAIで補完。未検証候補は拒否 |
| V1: 3モード共通検証 | **継続・並行** | スキーマ検証、生成C++静的検査、UHT/UBT、Blueprintとの実行結果比較、回帰ケースを整備 |
| E1: Editor UI統合 | **中・検証後** | Direct / AI / Hybridの選択、JSONプレビュー・保存、生成差分・診断表示、安全な保存を追加 |

**従来の実装順（長期計画）:** J0 → J1 → A1 → A2 / H1 → E1。直近は前述のD1→D2→D3→D4→V2→E2を優先し、V1は全段階で継続する。既存Phase 0〜8のDirect変換開発は止めず、JSON/AI系を並行トラックとして扱う。

## 現状（2026-10-12時点）

| 領域 | 状況 | 補足 |
| --- | --- | --- |
| Blueprintの直接解析とGraph IR | **限定的に実装済み** | ノード・ピン・Exec/Dataエッジを収集。汎用型解決は未実装 |
| C++生成MVP | **限定的に実装済み** | ActorのBeginPlay → PrintStringを対象とする専用Analyzer |
| PrintStringのオプション | **実装済み・UE実機未検証** | TextColorのRGBA解析、Duration、Key、画面/ログ設定 |
| 標準グラフ・イベントの判定 | **限定的に実装済み** | 空Construction Script、未接続Event Tickの一部を許容 |
| Editorプレビュー・保存 | **実装済み・実機での全面検証は未完** | Convert to C++ (Preview)、プロジェクトSourceへの保存経路 |
| Snapshot JSON / PythonServer | **既存v1を維持・JSON/AI機能を段階追加** | Semantic JSON v2の設計・Serializer・Validator・Editorコピー/保存、Mock/OpenAI Provider、生成結果ValidatorをPR #43〜#48で実装。実機・実API未検証 |
| Semantic IR / PrintString変換 | **最小縦断実装済み・実機動作未検証** | 型付きIR、Lowering、C++ Generator、比較テストを追加。汎用UFunction変換は未実装 |
| Phase 2 UFunction呼び出し基盤 | **限定実装済み・UE実機未検証** | 反射署名・ピン検証、GetActorLocation接続Lowering、bool既定値、未接続HitResultの宣言とK2_SetActorLocation文生成、出力ピンBindingPlanを実装。接続済みHitResultから単一VariableSetへの限定Loweringとテストを追加（UE未検証）。実変換経路への統合は未完了 |
| Blueprintメンバー変数・初期値 | **未対応** | 現行MVPはBlueprintVariableCount != 0を拒否。変数定義とCDOの値取得、型別C++初期化生成が必要 |
| Actor以外のBlueprint | **未対応** | 直接Actor親のMVP制限あり。Component、UObject、Widget、Anim等は段階的に拡張 |
| Semantic IR Editor統合 | **実装済み・実機動作未検証** | 旧経路Shadow比較と独立したSemantic Previewメニューを追加 |
| UE 5.7.4での生成C++ビルド・動作一致 | **一部未確認** | ユーザーからビルドは問題なさそうとの報告あり。Automation Test結果・生成C++ビルド・実行時の一致は未確認 |

GitHubへのコミットとUEでのコンパイル成功・動作確認は区別する。ユーザーからのビルド状況の共有は参考情報として記録し、Automation Test・生成C++のUHT/UBT・実行時の意味一致を別途検証する。

## JSON/AIトラックの実装更新（2026-10-12）

| 項目 | 関連PR | 実装状況・残課題 |
| --- | --- | --- |
| J0: Semantic JSON v2仕様 | #43 | スキーマ設計を文書化。実装との整合性レビューは継続 |
| J1: JSON Serializer / Python検証 | #44 | 型・式・GUID・診断の出力と入力検証を実装。UEビルド・テスト実行は未確認 |
| J1 / E1一部: Editor導線 | #45 | Content BrowserからJSONコピー・保存。未対応Loweringは診断を記録。実機検証未実施 |
| A1: Provider境界とMock | #46 | 独立したAI API、Mock、テストコードを追加。実行結果は未確認 |
| V1一部: 出力検証 | #47 | Direct/AI APIに共通ファイルパス・内容・状態Validatorを追加。C++構文・ビルド検証は未対応 |
| A1一部: OpenAI Provider | #48 | 環境変数による認証、HTTP通信、応答検証、通信モックテスト。実API疎通未実施 |
| A2 / H1 / E1全体 | 今後 | 複数Provider比較、Hybridの安全な部分変換、3モード統合UIは未実装 |

**統合順序:** #43 → #44 → #45 → #46 → #47 → #48。積み重ねPRのため、先行PRをmainへマージした後、後続PRのbaseをmainへ変更して差分とCIを再確認する。

**完了判定上の注意:** 「実装済み」はコミットが存在することを示し、機能完成や検証合格を意味しない。Pythonのpytest、UE 5.7.4でのAutomation Test・UHT/UBT、実AI疎通、BlueprintとC++の意味一致は別途確認する。特にSemantic JSONがpartialのときはAI生成結果を自動採用しない。

### 次の検証ゲート
- [ ] PythonServerの全pytestを実行し、既存Direct APIの回帰を確認（Python 3.10 / 3.12のGitHub Actionsを追加。初回実行結果待ち）
- [ ] UE 5.7.4でSerializer・Editor Export・Automation Testのコンパイルと動作を確認
- [ ] JSON v2をPython validatorに渡す往復テストと診断ケースを確認
- [ ] OpenAI Providerの認証・通信失敗・不正応答・出力拒否を実環境で検証
- [ ] 生成C++のUHT/UBTと元Blueprintの動作一致を確認
- [ ] A2・H1に着手する前に、出力保存・レビューの安全境界を明確化

## 更新版フェーズ

| フェーズ | 優先度 | テーマ | 主な作業と完了条件 |
| --- | --- | --- | --- |
| Phase 0 | **最優先** | 現行MVPの安定化・ベースライン | UE 5.7.4でBeginPlay→PrintStringを生成・ビルド・実行し、文字色や未接続Tickを含めて元BPとの動作一致を確認。回帰テストを整備 |
| Phase 1 | **最優先** | Semantic IR設計 | Graph IRを維持しつつ、型付き式、関数呼び出し、変数参照、代入、文、基本ブロック、診断情報の表現を定義。既存MVPを移行 |
| Phase 2 | **高** | 型解決と汎用UFunction呼び出し | UFunction / FPropertyから所有クラス、引数、戻り値、const、参照、静的/メンバー呼び出しを解決。対象関数のC++呼び出しを汎用生成し、代表的なUE APIで検証 |
| Phase 3 | **高** | 式・変数・データフロー | リテラル、数値演算、比較、変換、Get/Set Variable、ローカル/メンバー変数、純粋ノードの依存順を扱う。変数の型・属性・初期値をVariable IRへ取り込み、CDO値の取得と安全なC++初期化を段階実装。循環・副作用を診断 |
| Phase 4 | **高** | 制御フロー構造化 | Branch、Sequence、Return、For/WhileなどをCFGから構造化し、実行順序と分岐の意味を保存 |
| Phase 5 | **高** | Blueprint自作関数 | FunctionGraphsを関数宣言/定義に変換。引数、戻り値、ローカル変数、関数間呼び出し、再帰・参照を段階的に対応 |
| Phase 6 | **中** | イベント・クラス構造 | 複数イベント、イベントディスパッチャ、継承、プロパティ、コンポーネント、必要なUCLASS/UPROPERTY/UFUNCTION宣言を拡張。Actor派生 → ActorComponent/SceneComponent → UObject → Widget → Anim Blueprintの順にクラス固有のライフサイクル・生成規則を追加 |
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
- [x] 型・式・関数呼び出し・文・基本ブロックのデータ構造を設計
- [x] Graph IRからSemantic IRへBeginPlay→PrintStringをLowering
- [x] 既存のPrintString専用Analyzerとの比較テストを追加（テスト実行の合格確認は別途）
- [x] Semantic IRからActorの.h/.cppを生成する経路と旧経路の比較機構を実装
- [x] Shadow ModeとSemantic専用Editor Previewを追加
- [ ] 新旧生成結果のAutomation Test合格を確認
- [ ] 新Generatorで生成した.h/.cppのUEビルド・実行一致を確認

### M2: 汎用関数呼び出しの実証
- [x] UFunction/FProperty解決と署名・ピンの限定検証を実装（UEでのテスト合格は未確認）
- [ ] 引数の既定値・参照/出力引数・ターゲット/静的呼び出しを処理
- [ ] GetActorLocation、SetActorLocation等の代表例を検証
- [x] 未対応メタデータ・特殊関数を限定的に明示拒否（対象拡張とUE検証は継続）

### M3-V: メンバー変数と初期値の変換（Phase 3の先行縦断実装）
- [ ] Graph IRとは独立したVariable IRに、変数名・FProperty型・Blueprintメタデータ・編集/公開属性・初期値の出典を保持する
- [ ] `UBlueprint::NewVariables` と `GeneratedClass` / Class Default Object（CDO）の対応を検証し、継承元プロパティとBlueprint定義変数を区別する
- [ ] bool / 整数 / float / double / FString / FName / FText の既定値を型安全にC++へ出力する
- [ ] FVector / FRotator / FTransform / FLinearColor、Enumの初期値を対応する型とコンストラクタ表現で生成する
- [ ] UPROPERTYの編集・公開・カテゴリ等を実際のメタデータから復元し、ヘッダー宣言とコンストラクタ初期化を必要に応じて使い分ける
- [ ] 配列・Set・Map・ユーザー定義Struct、Object/アセット参照は依存関係とロード規則を確認して段階対応する
- [ ] 変換不能な値・未対応の属性・親クラス既定値との差分を黙って破棄せず診断し、Blueprintと生成C++のCDO値を比較するAutomation Testを追加する
- [ ] 既存の `BlueprintVariableCount != 0` 制限は安全な変数生成経路が完成するまで維持し、限定ケースから解除する

### M6-C: Actor以外のBlueprint対応（Phase 6）
- [ ] Actor専用Generator/Emitterの前提を整理し、UObject共通の型解決・変数・初期値生成とクラス別コード生成を分離する
- [ ] AActor派生の親クラスを保持する生成を検証する
- [ ] UActorComponent / USceneComponent、UObject、UUserWidget、UAnimInstanceを順次対応し、ライフサイクルや特殊グラフは個別に検証する
- [ ] 未対応のBlueprint種類を明示診断し、Actor MVPと旧プレビューの既定動作を維持する

### M3: 自作関数への最短経路
- [ ] 四則演算・比較・変数・ReturnをSemantic IRで生成
- [ ] 引数と戻り値を持つ単純なBlueprint FunctionGraphをC++へ変換
- [ ] 関数呼び出しの参照解決と複数関数の生成を検証

### M2の直近作業（PR #31〜#41・UE未検証）
- [x] `FBPConnectedFunctionCallEmitter` とAutomation Testを追加し、CallFunctionのGUID・署名・ピン検証を統合
- [x] bool既定値、FHitResult参照引数、GetActorLocation → K2_SetActorLocationの限定生成を追加
- [x] 出力ピンと後続入力を対応付けるBindingPlanをEmitterへ統合し、Exec順序・変数スコープを限定検証
- [x] 接続済みSweepHitResultから単一の反射済みVariableSetへの代入Loweringと正常系/拒否系テストを追加
- [x] 未接続selfを許可し、接続self・型不一致・不正な追加入力を拒否する検証を追加
- [ ] UE 5.7.4でEmitter/VariableSetのAutomation Testを実行し、生成文・診断・UHT/UBTを確認
- [ ] 生成対象Blueprintの実際のOwnerClassを渡す変換経路を設計・統合する
- [ ] ReturnValue、複数Consumer、一般的な関数・変数型のデータフローを段階的に実装する

**現時点の注意:** PR #41の接続済み出力LoweringはEmitter単体の限定ケースであり、既定のActor Generator / Semantic Previewへは未統合。Blueprint固有メンバー変数を含むグラフはMVPの制限により引き続き拒否する。UE 5.7.4のビルド・Automation Test・生成コードの動作一致は未確認。

## 実装・品質の原則

1. **意味の保存**: 実行順序、型、既定値、副作用、イベントの意味を推測で省略しない。
2. **安全な失敗**: 未対応ノードやピンを黙って無視しない。グラフ名・ノード・理由を診断へ出す。
3. **責務の分離**: Graph IR、Semantic Analyzer、Lowering、C++ Generator、Editor UIを分離する。
4. **小さな縦断実装**: 大規模な一括実装より、実際にビルド・動作確認できる変換ケースを増やす。
5. **回帰テスト**: 単体テスト、Snapshot比較、生成コードのUEビルド、元Blueprintとの動作一致を併用する。
6. **既存機能の維持**: Snapshot JSON Exporter、PythonServer、Editor UIを段階移行中も残す。
7. **UEバージョン依存性**: Blueprintコンパイラ内部APIを活用する場合はUE 5.7.4での公開範囲・安定性を確認する。
8. **コード規約**: C++のifは波括弧を必須とし、null判定を明示する。重要な設計判断は日本語コメントで説明する。

## 次の着手対象（2026-10-12更新）

**優先方針: レビュー・統計APIの拡張をいったん区切り、Blueprint → Semantic JSON v2 の生成情報を強化する。** D1〜D4・V2のレビュー基盤は維持し、今後の追加実装は実際のBlueprint情報をAI/Directへ渡す品質向上に集中する。

### JSON生成強化ロードマップ

| 順序 | ID | 内容 | 完了条件 |
| --- | --- | --- | --- |
| 1 | J2-1: 変数の意味情報 | Semantic JSON v2にBlueprint定義の変数名・型・型パス・属性・出典を追加。CDO由来の初期値は出典と取得可否を区別 | 既存SerializerとPython validatorを同時更新し、bool・数値・文字列の代表ケース、未対応型の診断、安定した順序をテスト |
| 2 | J2-2: 初期値の表現 | bool/int/float/double/FString/FName/FTextから着手し、構造体・列挙型・参照型は段階対応 | 値の欠落・未取得・明示的な空値を区別。CDO取得値とBlueprint宣言の差異を診断し、往復テストを追加 |
| 3 | J2-3: 関数シグネチャ | 関数引数・戻り値・参照/const・所有クラス情報をv2に追加 | UFunction/FPropertyの反射情報との対応、未解決型の診断、複数関数のテスト |
| 4 | J2-4: グラフ追跡情報 | Graph IRとの参照、元ノード/ピンGUID、Exec/Data接続、未対応ノードの出典を補強 | Snapshot v1のリンク情報を消さず、v2の意味情報と区別して照合可能にする |
| 5 | J2-5: Editor往復検証 | UEのJSON保存・コピー → Python validator → AI Provider入力まで検証 | UE 5.7.4 Automation Test、Golden JSON、Python側の不正入力・互換性テストを実行 |
| 6 | J2-6: C++生成連携 | 拡張JSONをDirect/AI比較・診断・生成結果追跡へ接続 | 未解決情報を自動補完せず、UHT/UBTとBlueprint動作比較を別途検証 |

**直近の実装単位はJ2-1**。最初に `FBPSemanticIR`、`FBPSemanticJsonSerializer`、`FBPSnapshotExporter`、`PythonServer/app/semantic_json.py` と既存テストを確認し、必要な型と出典を最小限のスキーマ拡張として設計する。既存Snapshot v1は変更しない。Semantic JSON v2の互換性が崩れる変更は明示的にバージョン管理し、旧JSONの扱いをテストで固定する。

### レビュー基盤の到達点と残課題

- PR #43〜#48: Semantic JSON v2 Serializer/Validator、Editor出力、AI Providerと出力検証の基盤を統合済み。UE実機・実AI疎通は未検証。
- PR #49〜#70: Direct/AI差分、構造比較、レビュー記録、外部UHT/UBT・動作証跡、統計・推移・ダッシュボード、索引共有と回帰テストを段階的に統合済み。
- PR #70: GitHub Actions成功後にmainへマージ済み。レビュー要約と検証レポートの証跡索引を共有。
- 残課題: 実際のUE 5.7.4ビルド・Automation Test、生成C++のUHT/UBT、Blueprintとの実行結果一致、実AI疎通。GitHub ActionsのPythonテスト成功はこれらの実機検証の代替ではない。
- 集計性能の実測・表示UI・Editor統合は継続課題とするが、JSON生成強化より優先しない。

Semantic Previewは実験的な経路として維持し、旧Generatorの既定経路は実機検証が完了するまで切り替えない。
