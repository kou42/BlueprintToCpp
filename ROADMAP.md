# BlueprintToCpp ロードマップ

最終更新: 2026-10-31

## 目的と原則

Blueprintの処理を特定のActorやPrintStringに依存せず、検証可能なSemantic IRを経由してC++へ変換する。対応範囲は段階的に広げるが、**一時的な専用実装を完成扱いして放置しない**。

- 対応済みとは「実装・回帰テスト・既存経路との整合性を確認した状態」を指す。UEビルド／Automation Test未実施の変更は検証待ちとする。
- PrintStringは最初の検証対象であり、最終的な変換単位ではない。新しい関数を増やすたびにPrintString専用分岐をコピーしない。
- Actor固有のノードやBeginPlayはクラス固有のPolicyとして扱い、一般のUObject関数に混入させない。
- 未対応のノード・署名・データフローは曖昧に変換せず、理由が分かる診断を返す。
- 既存のActor／ActorComponent変換を維持する。機能拡張は原則として縦断テストを先に追加する。
- **C++生成だけでなくJSON出力も正式な成果物として扱う。** Semantic IRに新しい表現を追加したら、JSONシリアライズ・Python側検証・往復テストも同じ変更で追従させる。

## 現状（コード調査に基づく。テスト実行結果ではない）

- `FBPParentClassResolver::ResolveParent` はUObject系のネイティブ親クラスを解決できる。
- `FBPNativeSubclassGenerator` は汎用的な派生UCLASSの骨格を生成できるが、Blueprintグラフの完全変換とは別経路。
- `FBPSemanticLowering::Lower` の分類対象は、共通Entry監査で選んだ実行グラフの到達可能Statement領域に限定済み。ただしPrintString、Branch、Sequence、ForLoop、ForEachLoop、Actor位置操作などのパターン別Lowerer選択は残る。
- 通常関数／Custom Eventの共通Entry解析と、Branch、Sequence、ForLoopを含むStatement Loweringは既存。ただし一般的なノードの任意の組み合わせには未対応。
- `FBPSemanticCodeGenerator::Generate` は複数関数と複数BasicBlockを生成できる。明示CFGは直列Fallthrough、Branch合流、LoopのBody／Completedとback-edge、Returnを検証し、対応済みの構造化C++へ復元する。
- PrintStringは `FBPReflectedCallResolver` を使う共通呼び出し経路へ移行済み。WorldContext、文字列式、数値、色、FName、ループローカル参照をReflection署名と照合する。
- ForLoop／ForEachLoopのCFG出力は対応したが、複雑Loweringの一部にBeginPlay前提の検証が残る。
- `ResolveEntryFunction` がBeginPlay、Function Entry、Custom Eventの判定と反射署名復元を共通化した。単一PrintString、Sequence、単一Branch、通常関数の単一ForLoopがこの経路を利用する。
- SequenceはActor BeginPlayに加えて、UObject通常関数でも出力ピン順を保持して複数PrintStringをLowering・C++生成できる。新規変更ではリテラル入力のstatic／self native void呼び出しへ拡張した（UE検証待ち）。Function Resultは最後のSequence出力から到達する形だけを受理する。
- Semantic JSONは `BPSemanticJsonSerializer.cpp` で `schema_version: 2` を出力し、Pythonの `semantic_json.py` が同バージョンを検証する。複数関数とBasicBlock ID／終端／後続参照は追従済みだが、実アセット種別を広げたEditor export検証は未完了。
- 2026-10-29の `2961277`（Expand semantic control flow CFG）まで `main` へ反映済み。

## 優先順位と完了条件

### P0: 共通関数呼び出し経路を完成させる

- [x] PrintString専用Emitterと共通 `FBPReflectedCallResolver` 経路の対応差分を列挙する（WorldContext、引数のデフォルト値、文字列・数値・真偽値・色、変数、ループ内Index/Element、必要なinclude）。
- [x] Reflectionで `UFunction` の所有者、呼び出し対象、パラメータ方向・型、戻り値、デフォルト値を検証する共通Call IR／Emitterの責務を定める。
- [x] PrintStringを共通Emitterで生成できるようにし、専用分岐は段階的に削除する。特殊なWorldContextなどは関数名直書きではなく、根拠のあるメタデータ／明示的Policyで扱う。
- [x] PrintString以外の少なくとも2種類の関数（例: 静的関数とインスタンス関数）を同じ経路で生成・検証する。
- [x] PrintStringの既存ケースとForEachLoopのIndex/Element・Completed経路の回帰テストを通す。
- [x] `EmitPrintStringStatement` やPrintString専用分岐を残す場合は、理由・移行先・削除条件をこのファイルの「残存専用処理」に記録する。

**完了条件:** PrintStringだけを特別扱いせずに、複数の異なるUFunction呼び出しを共通経路で生成できること。PrintStringの機能退行がないこと。

### P1: グラフ単位から関数単位のLoweringへ

- [x] BeginPlay、Function Entry、Custom Eventを共通Entry解析で区別し、Reflection署名を確認する。
- [ ] Blueprint全体のノード有無によるパターン振り分けを、関数グラフ単位の検証とStatement Loweringへ移行する。
- [x] UObject通常関数で `Branch -> PrintString` を変換し、両枝のFunction Result到達も検証する統合テストを追加する。
- [x] UObject通常関数で `ForLoop -> 関数呼び出し` を変換できる統合テストを追加する。
- [x] ActorのBeginPlayおよびActor位置操作はActor専用Policyで検証し、非Actorへ誤適用しない。
- [x] 未対応グラフや複数の実行Entryを黙って無視せず、診断付きで拒否する。

**完了条件:** UObject通常関数で制御フローと共通関数呼び出しを組み合わせられ、Actorの既存変換も維持されること。

### P2: 複数関数・制御フローの生成

- [x] `Generate` から単一関数制約を除去し、関数ごとに宣言・定義・includeを組み立てる。
- [x] BasicBlock間の接続、Branch合流、Loop、Returnの意味をSemantic IRで明示する。
- [ ] 同名関数、override、引数／戻り値、可視性、重複宣言を検証する。
- [ ] 複数関数を持つUObject Blueprintを一つのUCLASSとして生成する統合テストを追加する。

**完了条件:** 複数の通常関数・イベントを一つのクラスに安全に生成できること。

#### P2進捗（2026-10-28、検証済み）

- 複数の通常関数／イベントをIR順に一つのUCLASSへ生成し、関数ごとの宣言・定義・依存includeを構築する。ForLoop／ForEachLoopを含む複数関数、Semantic JSONへの複数関数保持を回帰確認した。
- Semantic関数名の重複、クラス名との衝突、BlueprintNativeEventの`_Implementation`と通常関数の生成C++名衝突を、生成物公開前に診断付きで拒否する。
- 最終Return文を持つ非void関数を単一／複数関数経路で生成する。リテラル、関数引数参照、検証済みpure関数式を扱い、戻り値欠落、途中Return、型不一致、void関数からの値返却を拒否する。
- `FBPSemanticBasicBlock`にBlock ID、終端種別、後続Block IDを追加した。Generatorは明示的なFallthroughで接続された直列BasicBlockを順序検証して生成し、重複ID、未知／順序不一致の後続、曖昧な終端を拒否する。Branch／LoopによるBlock間CFG生成は未対応のため、P2の制御フロー項目は継続扱いとする。
- Semantic JSON v2へ`block_id`、`terminator`、`successor_block_ids`を追加し、旧v2入力を維持しながらPython側でGUID、重複、参照先を検証する。
- UE 5.7 UHT／UBTビルド成功。`BlueprintAIConverter` Automation Test全144件成功（Exit Code 0）。Python Semantic JSON／roundtrip対象36件成功。Python全234件は`test_behavior_api.py`の待機が継続したため中断し、今回の対象外テストを含む全件完走は未確認。

#### P2追加進捗（2026-10-30、検証済み）

- `2961277`でBranchのTrue／False後続と任意の合流Block、LoopのBody／Completed後続とBodyからHeaderへのback-edge、Return終端をSemantic CFGとして検証し、構造化C++へ復元する経路を追加した。Loop Body内Branchも明示Block接続から復元する。
- 関数署名生成で、引数名の重複・不正識別子、`DeclarationIndex`の負値・重複・欠番、複数または型不一致の戻り値パラメータ、フラグと矛盾するoverride／interfaceメタデータを診断付きで拒否する検証を追加した。
- P2の署名項目は、公開範囲をSemantic IRへ明示する設計、out／参照引数Policy、override元との完全な署名一致検証が残るため継続扱いとする。
- UE 5.7 UHT／UBTビルド成功。追加した`FunctionSignatureValidation`と`BlueprintAIConverter` Automation Test全148件成功（Exit Code 0）。今回の変更はJSON schemaを変更していないためPythonテストは未実施。

#### P2追加実装（2026-10-30、UE検証待ち）

- Generator入口でout値引数と非const参照引数を診断付きで拒否するPolicyを追加。関数本体の出力代入・データフローが未対応のため、出力を保証できない署名を生成しない。const参照入力は受理し、Reflectionが`CPF_OutParm`を併記したconst参照も入力として扱う。
- 参照戻り値、const戻り値、voidコンテナを拒否する。参照戻り値は寿命検証が未対応であり、const戻り値は現在の値返却Emitterで修飾を維持できないため対象外とする。
- `FunctionSignatureValidation`へout値／可変参照／out参照の拒否、const参照入力（OutParm併記を含む）の受理、戻り値Policyの拒否、失敗時の生成物非公開を確認する回帰ケースを追加。
- Semantic IRとJSON schemaは変更なし。JSONは未対応署名も情報を保持して出力し、C++生成の可否とは区別する。公開範囲の明示、override元との完全一致検証、出力引数の本対応は残るためP2チェック項目は継続扱い。
- UE環境がないためUHT／UBTとAutomation Testは未実施。PythonのSemantic JSON／roundtrip／signature audit／declaration auditは47件成功。`git diff --check`成功。

#### P2 override署名照合（2026-10-30、UE検証待ち）

- `BuildFunctionShape`で`SignatureSource`の文字列だけを信頼せず、ネイティブ親から同名UFunctionを取得し、override元の正規パス・戻り値・const/static属性・引数名／順序／型／方向を`FBPFunctionSignatureResolver`経由で照合する。
- ReflectionからC++の仮想関数契約を確認できるnative Blueprint eventの`_Implementation`を受理対象とする。private／final／static、BlueprintImplementableEvent、通常の非イベントUFunction、親に存在しない関数、出力方式の不一致は拒否する。非void overrideのSuper戻り値Policyは引き続き未対応。
- BeginPlayは既存のActor／Component lifecycle Policyを維持し、void戻り値とInterface指定なしも検証する。その他の非UFunction C++ lifecycleはReflectionで検証できないため拒否する。従来の`BeginDestroy`成功fixtureは、実UFunction照合の根拠がないため拒否期待へ修正した。
- Interface実装は、生成クラスが親クラスのみを継承する現状に合わせ、ネイティブ親が実装するInterfaceと宣言元署名を照合する。Blueprintが新規導入するInterfaceは、明示的なInterface継承生成を実装するまで診断付きで拒否する。
- テスト用native Blueprint eventと`OverrideSignature` Automation Testを追加。正常な宣言／Super呼び出し／JSON出力、署名・パス・親クラス・イベント属性の改変拒否、失敗時の生成物非公開を検証する。既存JSON v2は変更せず、Pythonでoverrideメタデータとconst参照のOutParm情報を保持するroundtripケースを追加。
- Python Semantic JSON／roundtrip／signature audit／declaration auditは49件成功。`git diff --check`成功。UE環境がないため、新規fixtureのUHT／UBTとAutomation Testは未実施。公開範囲のIR明示、Interface継承生成、より広いoverride形式の対応は残るためP2チェック項目は継続扱い。

#### P2 関数の公開範囲（2026-10-30、UE検証待ち）

- `FBPSemanticFunction::Visibility`に`Unspecified / Public / Protected / Private`を追加。生成クラスのUFunctionからアクセスフラグを共通抽出し、欠落・競合は関数名付きエラーにする。通常関数／Custom Eventの共通Entry解析は抽出済みの公開範囲を保持する。
- Generatorは明示的なpublic／protected／privateをC++宣言へ反映する。protectedの新規UFUNCTIONには`BlueprintProtected`も出力する。private native eventと未知のenum値は拒否し、生成物を公開しない。
- overrideは宣言元の公開範囲と照合し、改変を拒否する。旧IRの`Unspecified`は通常関数では従来のpublic、overrideでは検証済みの親宣言から補完する。BeginPlayは既存Policyのprotectedを維持し、明示的な公開範囲の改変を拒否する。Reflectionを経由しない既存Loweringや旧fixtureのIRは`Unspecified`を保持できる。
- JSON v2へ任意の`functions[].visibility`を追加し、`unspecified / public / protected / private`をシリアライズ・Python検証する。追加フィールドのためschema_versionは2を維持し、旧v2のフィールド省略も受理する。移行のための書き換えは不要で、省略と明示値はいずれも往復時に保持する。
- Reflectionからの抽出、異なる公開範囲の複数関数生成、protectedイベント、privateイベント拒否、override公開範囲不一致、JSONへの全enum値出力をAutomation Testへ追加。Pythonでは全公開範囲と旧入力のroundtrip、未知値・不正型の拒否を追加した。
- Python Semantic JSON／roundtrip／signature audit／declaration auditは62件成功、`git diff --check`成功。UE環境がないためUHT／UBTとAutomation Testは未実施。P2の署名項目はUE検証とInterface継承・残るoverride形式の対応が残るためチェックを継続する。

#### P2 複数通常関数＋Custom Eventの縦断経路（2026-10-30、UE検証待ち）

- 既存のグラフ別Lowering統合に、一つの実行グラフが一つの正常なSemantic関数へなることと、統合時の同名関数重複の検証を追加。失敗時は、それまでの関数だけでなく共有変数も公開せず、診断と入力のasset／parent情報だけを返す。
- `BlueprintAIConverter.SemanticIR.Lowering.MultipleFunctionGraphs`を拡充。実Editor APIでUObject Blueprintに通常関数2つ、Custom Event1つ、共有int32変数1つを構築・コンパイルし、公開範囲を保持して一つのUCLASSへ生成する。重複グラフと未対応関数の拒否、部分結果の非公開も検証する。
- 同テストでクラス情報と全Graph Traceを追加して実JSONをSavedへエクスポートし、再読込・再解析・同じIRからの再シリアライズ一致、関数／変数数、UObject親、公開範囲、Custom Event由来の保持を確認する。成功ログへ`SEMANTIC_ROUNDTRIP_ARTIFACT`を出力する。
- `app.semantic_editor_e2e`へ`--automation-test`を追加。既定のActor exportテストを維持しつつ、上記テストの成果物をPython schema／Mock Providerへ渡せる。選択したテストそのものの成功ログを要求し、別テストの成功ログや不正な選択文字列を拒否する。JSONレポートに選択名とGraph Trace数を追加した。
- 実UE確認には従来のE2Eコマンドへ`--automation-test BlueprintAIConverter.SemanticIR.Lowering.MultipleFunctionGraphs`を追加する。UE環境がないためUHT／UBT、実Editor Automation、実Editor export→Python E2Eは未実施であり、P2統合テスト項目はチェックを継続する。
- Python Semantic JSON／roundtrip／signature audit／declaration audit／Editor E2Eランナーテストは74件成功、`git diff --check`成功。PythonテストのEditor呼び出しは模擬し、複数関数JSONを実Python CLIとMock Providerへ渡す部分はsubprocessで実行した。実UE成功とは区別する。

### P2並行: Semantic JSON出力・Python連携（必須）

- [ ] EditorからのSemantic IR JSONエクスポートが、UObject通常関数・Custom Event・Actor BeginPlayで機能することを確認する。
- [ ] 共通Call IR、制御フロー、複数関数、変数、引数・戻り値、親クラス情報、診断をJSONで欠落なく表現する。C++出力だけを先行してJSON出力を置き去りにしない。
- [ ] `BPSemanticJsonSerializer` と `PythonServer/app/semantic_json.py` のスキーマ契約を同期する。互換性を壊す変更はバージョン方針と移行手順を明示する。
- [ ] JSON出力の決定性（同じ入力で安定した結果）、型・GUID・参照関係、エラー時の診断をテストする。
- [x] JSONのgolden test、Python側schema validation、export→parse→必要に応じたroundtrip／C++生成の統合テストを追加・更新する。
- [ ] C++生成に対応する機能がJSONでも扱えるかをPRレビューの必須確認項目にする。

**完了条件:** 対応済みのSemantic IR表現をJSONに損失なく出力し、Python側が同じスキーマで検証でき、対応する往復／回帰テストが通ること。

### P3: 対応クラス・ノードの拡張

- [ ] ActorComponent／SceneComponentのライフサイクルと通常関数の組み合わせを検証する。
- [ ] UObject派生の各種ネイティブクラスへ拡張する。
- [ ] Widget／Animation Blueprintなど特殊な生成クラスは固有の検証要件を調査して段階的に追加する。
- [ ] Python側の固定親クラス許可リストとUnreal側Reflection解決の差を整理する。
- [ ] 旧 `FBPActorCodeGenerator` のParity用途を確認し、置き換え完了後に削除を検討する。

## P0進捗サマリー（2026-10-24）

**状態: P0実装・回帰検証・実export成果物のPython CLI E2E自動化まで完了し、main反映済み。** PR #111、#112はいずれもマージ済み。

| 領域 | 実装済みの範囲 | 残る課題 |
| --- | --- | --- |
| 共通Call | Reflectionに基づく署名・対象・WorldContext検証、文字列式・数値・bool・LinearColor・FNameと公開ネイティブプリミティブ参照。末尾省略引数は`CPP_Default_*`が存在し、安全に型変換できる値だけを明示出力。static／instance双方を回帰確認 | 追加の対応範囲なし |
| ForEachLoop | int32 Index／Elementの共通Call引数化、型偽装と継承メンバー名衝突の拒否、Completedの共通式文Emitter経由、関数名非依存のネスト式参照追跡 | int32以外の要素型を扱う場合の一般化 |
| PrintString | WorldContextを含む全7引数をReflection署名で検証して共通Callから生成。専用Emitter／ディスパッチ／ForEach判定を削除し、既存Parityを維持 | 追加の対応範囲なし。共通Callの拡張に追従 |
| JSON／Python | KeyをReflectionどおりFNameとして出力。Editor Automationが生成した実成果物パスを取得し、Python `semantic_roundtrip` CLIとMock Provider境界まで検証するE2Eランナーを追加 | UObject通常関数／Custom Eventを含む実アセット範囲の拡張 |

### 検証状況の区分

- **ユーザー報告済み:** 2026-10-23時点でUEビルドに問題なし。ただし、報告以降の追加コミットを含む最新HEADのビルド成功を意味しない。
- **確認済み:** UE 5.7 UHT／UBTビルド成功。BlueprintAIConverter Automation Test全140件成功。既存PrintString Parity、ForEach正常系・拒否系、Reflection既定値の補完・必須引数省略拒否を含む。Python側schema／roundtrip／signature audit 39件成功。
- **判定:** P0の共通Call完了条件と実export→Python CLI E2Eを満たす。PRへ最終結果を記録できる状態。

### P0追加実装（2026-10-23、検証待ち）

- PrintStringのConv_IntToStringとConcat内整数演算で、SourcePinId付きint32メンバー参照をSemantic変数宣言または親クラスReflectionと照合する。未宣言・型偽装・コンテナ／参照／const修飾を拒否する。
- 整数演算経由のメンバー参照は検証後にthis->参照として出力する。SourcePinIdのないローカル参照は既存互換性を維持し、ループスコープ検証は次の作業で共通化する。
- UEビルド・Automation Test・JSON往復テストは未実施。P0完了とは判定しない。

- **追加変更（検証待ち）:** PrintStringの整数変換・Concat内整数演算で、SourcePinIdのないローカル整数参照をForLoop／ForEachLoopの有効なIndex／Element名に限定。ForLoopの式文にもLocalIndexを伝播。既存の正常系・拒否系Automation TestとUEビルドは未実施。

- **回帰テスト追加（実行待ち）:** `BPSemanticPrintStringScopeTests.cpp` にForLoop内Indexの正常系、未宣言Index拒否、Completedのスコープ外参照拒否を追加。併せてConcat内の文字列メンバー参照の誤った整数判定を修正。テスト実行・UEコンパイルは未確認。

- **ユーザー報告:** 前回コミットまでの動作チェックは問題なし（2026-10-23の会話）。確認対象の個別Automation Test名や実行ログは未受領。
- **追加回帰テスト（新規変更・実行待ち）:** `BPSemanticPrintStringScopeTests.cpp` にConcat→Conv_IntToString→Add_IntIntの正常系、未宣言ローカル名拒否、SourcePinId付き未宣言メンバー拒否を追加。既存のネスト式Emitterを公開Generate入口から検証する。Semantic IR／JSON schemaは変更なし。

- **ForEachネスト式対応（新規変更・検証待ち）:** ForEachLoopのPrintString専用「リテラル／直接Conv_IntToString」制限を除去し、既存の検証済み式Emitterに統一。Concat／整数演算の内部までElement使用を再帰走査し、必要な場合にのみ要素変数を宣言する。既存のループ変数・Reflection検証は維持。UEビルド／Automation Testは新コミットに対して未実施。

- **ForEach回帰テスト拡充（新規・検証待ち）:** `BPSemanticPrintStringScopeTests.cpp` にForEach内Concat→Conv_IntToString→Add_IntIntのElement正常系、Index正常系、Element未使用時の宣言省略、未宣言ローカル拒否、CompletedからのElement参照拒否を追加。公開Generate入口で検証。最新HEADのUEビルドとAutomation Testは未実施。

- **ネスト整数式の識別子検証（新規・検証待ち）:** Concat内部の整数変数参照でC++識別子を明示検証し、重複したメンバーReflection照合を整理。不正な識別子をSourcePinId付きメンバーとして与える拒否テストを追加。UEビルド／Automation Testは未実施。

- **整数式Emitter共通化（新規・検証待ち）:** PrintStringの直接Conv_IntToStringとConcat内部Conv_IntToStringで重複していた整数式再帰Emitterを`EmitPrintStringIntegerExpression`へ統合。int32型、コンテナ・参照修飾、ローカルスコープ、メンバーReflection、識別子、MathLibrary許可リスト、深さ制限を一箇所で検証。UEビルド／Automation Testは最新HEADでは未実施。

- **文字列式Emitter共通化（新規・検証待ち）:** PrintStringの文字列リテラル・メンバー参照・Concat_StrStr・Conv_IntToStringを`EmitPrintStringTextExpression`に統合。直接引数とConcat内部の型・識別子・Reflection・深さ制限を統一し、整数式は既存の共通ヘルパーを再利用。既存のPrintString/ForEach回帰テストで確認予定。最新HEADのUEビルド／Automation Testは未実施。

- **共通CallのFStringネスト式対応（新規・検証待ち）:** Reflection共通CallのFString引数に`EmitPrintStringTextExpression`を再利用し、Concat_StrStr／Conv_IntToString／整数式の検証済み再帰生成を導入。共通CallのForEach Index/ElementではSourcePinId付き参照をローカルと誤認しないよう拒否し、Element偽装の回帰テストを追加。共通FStringネスト式自体の正常系Automation Testおよび最新HEADのUEビルドは未実施。

### 次の実装・検証順序

1. P2の関数公開範囲、out／参照引数の拒否Policy、native Blueprint event override署名照合のUE検証を行う。Interface継承生成と残るoverride形式を段階的に拡張する。
2. 拡充した`MultipleFunctionGraphs`で、通常関数2つ＋Custom Eventを持つ実UObject BlueprintのUCLASS生成と実Editor export→Python E2EをUE環境で実行する。
3. 通常関数ForLoopの単一Loop／Body Call制約を整理し、多重ループ・Completed後続の共有Result到達を検証して拡張する。ForEachの一般Call対応、ネストBranchの接続条件式・戻り値対応も段階的に進める。
4. 対応済みCFGと複数関数について、実Editor exportからPython schema／roundtripまでの対象アセットを拡張する。

### 入れ子ForLoopの実Blueprint export（2026-10-31、Python検証済み・UE検証待ち）

- `BlueprintAIConverter.GeneratedAssets.NestedForLoop`を追加。専用の`BP_Test_NestedForLoop`アセットへ標準ForLoopマクロを2段接続し、外側1～3、内側2～4の境界、内側Body・内側Completed・外側Completedを持つ実Blueprintを構築する。
- 共通fixtureのLowering入口を`Lower(Blueprint, GraphIR, Semantic)`へ統一し、実Blueprintと共通Entry監査を経由する。既存の単一ForLoop／算術比較fixtureにも共通export検証を追加し、正常なIRへクラス情報と全Graph Traceを付加してSemantic JSONを保存・成果物通知する。新しいLowerer機能の追加ではなく、既存対応の実Blueprint縦断検証を拡張する変更。
- 入れ子のSemantic IRについて内側Loopと双方のCompleted文数を検査し、生成C++の内側Index名・境界値とBody→内側Completed→外側Completedの順序を確認する。Graph IRのコピーで不正な境界値・重複Exec edgeを作り、診断付き拒否と部分関数非公開を確認する。
- 実UEでは`python -m app.semantic_editor_e2e --editor <UnrealEditor-Cmd> --project <project.uproject> --automation-test BlueprintAIConverter.GeneratedAssets.NestedForLoop`を実行する。既存GeneratedAssetsテストと同様にuassetとGraph JSONも保存する。単一ForLoopと算術比較もそれぞれ`BlueprintAIConverter.GeneratedAssets.ForLoop`／`BlueprintAIConverter.GeneratedAssets.ArithmeticComparison`でexportを選択可能。
- Pythonに2件を追加。Editor呼び出しのみを模擬し、内側／外側Completedを持つ入れ子Loop JSONを実CLI／Mock Providerへ渡す正常系と、内側Completedの不正GUIDによるE2E失敗を確認する。関連Pythonテスト191件と`git diff --check`成功。
- 新規fixtureと既存fixtureの入口変更についてUHT／UBT・Automation Test・実Editor export E2EはUE環境がないため未実施。複雑Loopの共通Entry移行と専用Lowerer整理は引き続き未完了。JSON連携全体のチェックは実UE検証まで継続する。

### Break付きForEachの実Blueprint export（2026-10-31、Python検証済み・UE検証待ち）

- `BlueprintAIConverter.GeneratedAssets.ForEachLoopWithBreak`を追加。通常ForEachと共通の実Blueprint fixtureから標準`ForEachLoopWithBreak`マクロを生成し、int32配列Getter・Body Call・Body末尾からBreak入力へのExec接続・Completed Callを構築する。専用の`BP_Test_ForEachLoopWithBreak`アセットを使用する。
- コンパイル後の実Graph IRについて、Breakを含むExec edge数と全Node／Pin／接続のGraph JSON一致を検査する。Lowering後のBody末尾のBreak文、Semantic JSONの文数と`kind: break`、C++のBody→Break→Completedの出力順を確認し、共通ヘルパーでSavedへのSemantic JSON出力と成果物通知まで行う。
- 正常Graph IRのコピーでBreak edge欠落と不明な入力Pinを作り、Loweringの拒否、診断保持、部分関数を公開しないことを検査する。正常なexportのGraph Traceは改変しない。
- 実UEでは`python -m app.semantic_editor_e2e --editor <UnrealEditor-Cmd> --project <project.uproject> --automation-test BlueprintAIConverter.GeneratedAssets.ForEachLoopWithBreak`を実行する。既存GeneratedAssetsテストと同様にuassetとGraph JSONも保存する。
- Pythonに2件を追加。Editor呼び出しのみを模擬し、Break文を持つForEach CFGのJSONを実CLI／Mock Providerへ渡す正常系と、不正なBreak出典GUIDでE2Eが失敗する拒否系を確認する。関連Pythonテスト189件と`git diff --check`成功。
- 新規C++ fixtureのUHT／UBT・Automation Test・実Editor export E2EはUE環境がないため未実施。既存Lowererの機能追加ではなく、既存Break対応を実Blueprintとexportで検証する経路の追加として扱う。入れ子Loopの実export拡張、複雑Loopの共通Entry移行とUE実行確認は引き続き未完了。

### Loopの実Editor export経路（2026-10-31、Python検証済み・UE検証待ち）

- 実UObject Blueprintの通常関数`FunctionForLoopCall`と、Actor Blueprintの`GeneratedAssets.ForEachLoop`へSemantic JSONのSaved出力と`SEMANTIC_ROUNDTRIP_ARTIFACT`通知を追加。前者はBody／Completed／back-edgeを持つ3 Block CFG、後者は配列Getter・配列要素型・Body／Completedを含む既存Lowering／C++生成結果をそのまま保存する。
- Branchの保存検証を`Private/Tests/BPSemanticRoundtripTestUtils.h`の共通ヘルパーへ移行し、Loopでも使用する。入力IRのコピーへクラス情報・全Graph Traceを付加し、SerializerとExporterの一致、再読込・再解析、関数数・変数数・Graph Trace数・親クラスを検証する。検査に失敗した場合は成果物通知を出さない。
- ForEachと共通fixtureを使用する`GeneratedAssets.FunctionCall`にも同じexport検証を追加。GeneratedAssetsテストは従来どおり`/Game/BlueprintToCppTests`配下のuassetとGraph JSONも保存する。
- 実UEでは`python -m app.semantic_editor_e2e --editor <UnrealEditor-Cmd> --project <project.uproject> --automation-test BlueprintAIConverter.Conversion.RealBlueprint.FunctionForLoopCall`を実行する。選択名を`BlueprintAIConverter.GeneratedAssets.ForEachLoop`へ変更すると実ForEachを確認できる。
- Pythonに4件を追加。Editor呼び出しのみを模擬し、通常関数ForLoop CFGとActor ForEachの配列型ツリー／Completedを持つJSONを実Python CLIとMock Providerへ渡す。不明なCFG後続と不正な配列要素型でE2Eが失敗することも確認する。関連Pythonテスト187件と`git diff --check`成功。
- UE環境がないため、共通ヘルパーと追加export経路のUHT／UBT・Automation Test・実Editor E2Eは未実施。実export経路の準備とPython検証まで完了し、UE検証待ちとして扱う。Break／入れ子Loopを含む実export対象の拡張も残るため、JSON連携全体のチェックは継続する。

### Branchの実Editor export経路（2026-10-31、Python検証済み・UE検証待ち）

- `BPRealBranchConversionTests.cpp`の通常関数Branch共通fixtureとネストBranch fixtureに、実際にLowering・C++生成したIRのSavedへのエクスポートを追加。通常関数の`FunctionBranch`／`FunctionBranchNativeCalls`／`FunctionBranchStaticAndSelf`／`FunctionBranchMixedPrint`と、Actor BeginPlayの`NestedBranchNativeCalls`が対象。
- 共有テストヘルパーで実Blueprintのクラス情報と全Graph Traceを付加し、SerializerとExporterの内容一致、保存JSONの再読込・再解析、関数数・Graph Trace数・親クラスの保持を確認する。`SEMANTIC_ROUNDTRIP_ARTIFACT`を出力し、既存E2Eランナーから今回の成果物を選択できる。ネスト枝の拒否テストはGraph IRのコピーを改変し、正常なexportの追跡情報を汚さない。
- 実UEでは`python -m app.semantic_editor_e2e --editor <UnrealEditor-Cmd> --project <project.uproject> --automation-test BlueprintAIConverter.Conversion.RealBlueprint.FunctionBranchNativeCalls`で通常関数Branchを確認する。同じコマンドの選択名を`BlueprintAIConverter.Conversion.RealBlueprint.NestedBranchNativeCalls`へ変更するとネストBranchを確認できる。他の通常関数Branchケースも同様に選択可能。
- Python側に4件を追加。上記2つの選択名についてEditor呼び出しのみを模擬し、Branch CFG／ネストJSONを実Python CLIとMock Providerへ渡す正常系と、不正GUIDを持つ成果物によってE2Eが失敗する拒否系を確認する。関連Pythonテスト183件と`git diff --check`成功。
- UE環境がないため、C++ヘルパーのビルド、Automation Test、実Editor export E2Eは未実施。通常関数Branch／ネストBranchの実export経路は準備済みだが検証待ちとし、Loop系の実export対象拡張とJSON連携全体のチェックは継続する。

### Semantic JSONの再帰型ツリー同期（2026-10-31、Python検証済み・実UE待ち）

- `TypeToJson`とPythonの契約を照合し、未受理だった`enum`／`byte`／`delegate`／`multicast_delegate`を同期した。delegate署名パスなどの`type_path`もJSON往復で保持する。C++生成対応を増やす変更ではなく、既存Serializerの出力を受理する変更。
- `container_kind`の`none`／`array`／`set`／`map`、`inner_type`／`key_type`／`value_type`文字列、`element_type_info`／`key_type_info`／`value_type_info`の再帰構造を検証する。子型の不正な形状・属性・深さ64超過は、子フィールドまで含むパス付きで拒否する。
- 旧v2の追加フィールド省略、文字列だけのコンテナ型、未解決コンテナ根、nullの子型は引き続き受理する。型の構造検証とC++生成可否は分離し、Reflectionの型解決結果やコンテナ文字列と子型の意味的一致をPythonで推測しない。既存v2の出力契約への同期のためschema_versionは2を維持し、既存exportの移行は不要。
- `test_semantic_type_json.py`へ49件を追加。C++の既存`SerializeV2` fixtureに沿うMap／Set型ツリーを、変数・戻り値・引数・式・Call引数・ForEach要素型で検証し、JSON往復／Mock Provider、追加型種別、不正な子型、旧形式、深さ64の受理／65の拒否を確認した。
- 新規49件を含む関連Pythonテスト179件と`git diff --check`成功。C++コード変更なし。UE環境がないためUHT／UBT・Automation Test・実Editor export E2Eは未実施。JSON連携全体のチェック項目は、実export対象の拡張とUE検証が残るため継続する。

### Semantic JSONのLoop／Break同期（2026-10-31、Python検証済み・実UE待ち）

- 現在の実装を照合し、C++ Serializerが既に出力する`for_loop`／`for_each_loop`／`break`をPython側が未受理だった不整合を修正した。`semantic_json.py`で受理するStatement種別を同期し、Index名、First／Last式、配列式、Element名／型、Body／Completed配列を検証する。
- Body／Completed内もBranchと共通の再帰Statement検証へ渡し、子Statement・式・GUID・深さ上限64を検証する。通常文に非空のBody／Completed配列を隠した入力は拒否する。Serializerが通常文へ出力する空のループ情報と、旧v2での同フィールド省略は維持する。
- JSON v2の既存出力表現への受理拡張であり、schema_versionは2を維持する。構造検証とC++生成可能性は区別し、Breakのループスコープ、CFGの構造化可否、配列要素型の意味的一致はGenerator側の検証を引き続き使用する。型ツリー全体のPython検証拡充と実Editor exportによる確認は残る。
- `test_semantic_loop_json.py`へ35件を追加。ForLoop／ForEachの入れ子・Branch内ループ・明示CFG・Break・CompletedをJSON往復とMock Provider境界まで保持し、必須情報欠落、不正な子配列／式／型、深さ超過を拒否する。関連Pythonテスト130件成功。`git diff --check`成功。
- C++ Serializer／IRは変更なし。UE環境がないため、UHT／UBT・Automation Test・実Editor export E2Eは未実施。JSON連携全体のチェック項目は継続する。

### P1 複雑ForLoopのCustom Event対応（2026-10-30、UE検証待ち）

- 複雑ForLoopのイベント経路からBeginPlay必須条件を除去し、実Blueprintでは共通`ResolveEntryFunction`で署名・親クラス・void戻り値を検証する。UObjectのCustom Eventにも対応し、多重ループ・既存二重ループfallback・単一ループ経路が反射済み関数名／公開範囲／Entry種別を保持する。
- 各形状での`Function.Name = BeginPlay`固定生成を共通Entryテンプレートへ置き換え、PrintString解析も共通Entry対応Analyzerへ移行。Graph IRだけを入力する既存APIは、反射で署名を復元できないCustom Eventを拒否し、BeginPlay lifecycle Policyを維持する。
- 多重ループの汎用チェーン走査は最上位Loopが一つの形に限定し、その後のCallを当該LoopのCompletedへ正規化する。先行Printを保持し、内側Completedは親Body内の後続として配置する。これにより前処理と各Completedを持つ三重ループも受理し、実行順とIndexの有効範囲を維持する。
- 到達領域外からのExec流入を拒否する。循環、範囲リテラル不正、外側CompletedからのIndex参照、親クラス改変の既存検証も維持する。通常関数の単純ForLoop専用経路は維持し、通常関数の多重ループ・複数Body Call・Completed後続にはまだ拡張していない。
- `CustomEventForLoop`、`CustomEventNestedForLoop`、`CustomEventTripleForLoop`の実Blueprint Automation Testを追加。UObjectのイベント署名、深さ1／2／3、最内側Index、内側→外側Completedの単一生成、ノード／edge配列順に依存しない順序、拒否系と実JSON exportを確認する。既存Actor多重ループにも正規化変更の回帰確認が必要。
- Semantic IR／JSON schemaは変更なし。Custom Eventの深さ1／2／3のForLoop・Completed・メタデータをJSON往復／Mock Providerまで確認し、関連Python141件と`git diff --check`が成功。UE環境がないためUHT／UBT・Automation Test・実Editor export→Python E2Eは未実施。
- 通常関数の複雑ForLoop、一般Callや接続された条件式の統合、パターン別Lowerer選択は残るためP1全体の完了チェックは継続する。

### P1 ForEachの共通Entry化（2026-10-30、UE検証待ち）

- `LowerForEachLoopImpl`からネイティブ親のBeginPlay対応必須条件を除去し、`ResolveEntryFunction`で通常関数／Custom Event／BeginPlayの署名と親クラスを共通検証する。生成する関数名・Entry種別・公開範囲も保持し、PrintStringの既定値解析を共通Entry対応Analyzerへ切り替えた。
- UObject通常関数はvoidに限定し、唯一のFunction ResultへCompletedの末尾から到達することを要求する。Bodyからの途中Return、Result未到達、複数Result、Result後の継続を拒否する。Custom EventはResultを含まない形を維持する。
- 標準ForEach／ForEachWithBreakの正規マクロパス、int32配列の反射型、Entry→Loop／Body／Completed／Break接続、Index／Elementの寿命検証を維持する。到達領域外からのExec流入も拒否し、変数抽出エラーがある場合は関数を公開しない。
- `FunctionForEach`、`CustomEventForEach`、`FunctionForEachWithBreak`、`CustomEventForEachWithBreak`の実Blueprint Automation Testを追加。UObject通常関数のElementとCustom EventのIndex、ループ前Print、CompletedとBreakの順序、Actor lifecycle非混入を検証する。マクロパス／親クラス改変、Completedからのローカル参照、外部Exec流入、不正Result経路の拒否も追加。
- 各fixtureは実Blueprint由来のSemantic JSONを保存し、成果物パスをログへ出す。E2Eランナーへ`--automation-test BlueprintAIConverter.Conversion.RealBlueprint.FunctionForEach`などの正確なテスト名を渡して検証できる。配列メンバーのC++生成テストは既存Policyと同じDeclarationOnlyモードであり、配列既定値の再現を保証しない。
- Semantic IR／JSON schemaは変更なし。通常関数／Custom EventのForEach CFG、型付きIndex／Element参照、Breakの有無をJSON往復・Mock Providerまで検証するPythonケースを追加。前項までのJSON同期を含む関連138件と`git diff --check`が成功。UE環境がないためUHT／UBT・Automation Test・実Editor export→Python E2Eは未実施。
- 複雑ForLoopのBeginPlay依存、ForEach内の一般Call／任意式、int32以外の配列要素型、非void Returnは残るため、P1全体の完了チェックは継続する。

### P1 ネストBranchの共通Entry化（2026-10-30、UE検証待ち）

- `LowerBranchImpl`のネストBranchからBeginPlay限定条件を除去し、通常関数／Custom Eventも`ResolveEntryFunction`による反射署名・親クラス検証を使用する。boolリテラル条件、native void Call、構造化できる合流の既存Policyは維持する。
- 通常関数はvoidかつ到達可能なFunction Resultが一つの形に限定し、全経路が同じ末尾Resultへ到達することを走査して検証する。枝の途中で終わる経路、複数Result、Resultからの継続、循環、到達領域外からのExec流入は拒否する。非void戻り値、途中Return、複数の独立したResultを持つネスト関数は引き続き対象外。
- 共有Resultは全経路の検証後に末尾として一度だけ消費し、枝内の途中Returnとして出力しない。合流後の共通Callも既存の構造化走査で一度だけ生成する。Custom EventではResultを含まない形を維持する。
- 既存`NestedBranchNativeCalls`を共通fixtureへ整理し、`NestedBranchCustomEvent`、`NestedBranchFunction`、`NestedBranchFunctionJoin`を追加。UObjectのEntry種別とC++定義、Actor lifecycle非混入、合流後Callの単一生成、正常系JSON出力とResult／Exec不正形状の拒否を確認するAutomation Testを追加した。
- Semantic IR／JSON schemaは変更なし。UObject通常関数／Custom Eventの入れ子Branchと共通後続CallをJSON往復・Mock Providerまで保持するPythonケースを追加し、前項のLoop／Break同期を含む関連134件成功。`git diff --check`成功。UE環境がないためUHT／UBTとAutomation Testは未実施。
- 複雑Loopの共通Entry化、ネストBranchの接続条件式・非void Returnや任意Statement混在への拡張、パターン別Lowerer選択は残るため、P1全体の完了チェックは継続する。

### P2 JSON Loop／Breakの同期（2026-10-30、Python検証済み・UE検証待ち）

- PR #113のマージ（`036ccaf`）を確認し、そのmainから`feat/semantic-loop-json-validation`で継続作業。
- `semantic_json.py`へC++ Serializerが既に出力する`for_loop / for_each_loop / break`の検証を追加。ForLoopの開始／終了式、ForEachの配列式・要素変数／型、共通Index名、Body／Completedを保持し、子StatementをBranchと同じ再帰検証へ通す。明示CFGではBody／Completedが別Blockにある空の子配列も受理する。
- コンテナ型の任意フィールドと`element_type_info / key_type_info / value_type_info`を再帰検証。旧v2の型ツリー省略・nullと空のcontainer種別は維持し、存在する不正型・コンテナ種別・過剰な入れ子は拒否する。Statement／型ツリーの深さ上限は64。
- C++ Serializerが全Statementへ出力する空のBody／Completedや未使用ループ式・null変数名を引き続き受理する。非Loop文に非空Body／Completedを隠した入力は拒否する。JSON検証は構造確認であり、Breakの実行スコープ・境界式や配列要素の型互換・C++生成可否はGeneratorの検証と区別する。
- `BlueprintAIConverter.SemanticJson.LoopContracts`を追加。ForLoop内Branch→Break、Completedの共通Call、ForEachの要素型・配列型ツリー・ネストLoop・由来GUIDをC++ Serializer出力で確認し、Python検証用の成果物パスをログへ出す。これはSemantic IRを直接構築するSerializer契約fixtureであり、実Blueprint変換の成功を意味しない。
- 実UE確認時はE2Eコマンドへ`--automation-test BlueprintAIConverter.SemanticJson.LoopContracts`を指定できる。UHT／UBT・Automation Test・実Serializer出力→Python E2Eは本環境では未実施。
- PythonではForLoop／ForEachの構造化表現・入れ子・明示CFGとback-edge、Branch内Break、CompletedをJSON往復→Mock Providerまで確認。必須フィールド欠落、不正な子Statement／式／GUID／型ツリー、深さ超過と旧入力の互換性も検証し、関連132件成功。`git diff --check`成功。
- 既存v2表現の受理拡張でありschema_versionは2を維持。新しいIR表現やC++生成ロジックは追加していない。Statement種別の同期は進んだが、実Editor export範囲と型種別・参照関係の完全な契約監査は未完了のためJSON全体のチェックは継続する。

### P1 Branchの共通CallとJSON同期（2026-10-30、UE検証待ち）

- `LowerBranchImpl`の単一Branch／ネストBranchのCall式構築を`LowerEntryCallExpression`へ統合し、独立していたReflection解決・Target・リテラル引数処理を削除。`Lower`のBranch振り分けからPrintString必須条件を除去し、native static／self void Callだけの枝も扱う。
- 単一Branchのリテラル／数値比較条件、通常関数の両枝Result到達、ネストBranchの木構造／合流／循環／到達領域の検証を維持する。ネストBranchのBeginPlay・boolリテラル条件への限定は継続し、通常関数のネストBranchや任意のStatement混在は本変更の対象外。
- 実Blueprintの`FunctionBranchNativeCalls`（UObject・staticのみ）、`FunctionBranchStaticAndSelf`（Actor通常関数）、`FunctionBranchMixedPrint`、`NestedBranchNativeCalls`（Actor BeginPlay）を追加。既存`FunctionBranch`とfixtureを共有し、C++／JSON出力、両枝のResult到達、型／関数パス／static属性改変、ネスト葉の不正Call拒否と部分関数非公開を検証する。
- C++ Serializerが既に出力する`kind: branch`をPythonが未受理だった不整合を修正。`semantic_json.py`へ再帰Statement検証を追加し、bool条件、then／else配列、子Statement／式／GUID、深さ上限64を確認する。明示CFGの枝（子配列は空で後続Blockを参照）と、入れ子の枝の双方を保持する。通常文に非空の枝配列を隠した入力は拒否する。
- JSON v2の既存出力表現を受理する拡張であり、schema_versionは2を維持。既存の通常Statementと空の枝配列を持つ出力は引き続き受理し、移行時の書き換えは不要。Loop／Break StatementのPython同期は依然未完了のため、JSON全体のチェックは継続する。
- Branch CFG／入れ子／共通CallのJSON往復とMock Provider、不正な子Statement／条件／枝配列／深さ超過の拒否を追加。関連Pythonテスト95件と`git diff --check`が成功。UE環境がないためUHT／UBTとAutomation Testは未実施。

### P1 単一Entryからの直列Call Lowering（2026-10-30、UE検証待ち）

- `LowerSingleEntryPrintStringImpl`を`LowerSingleEntryCallsImpl`へ置き換え、通常関数／Custom Event／BeginPlayから単一または直列のnative void呼び出しをLoweringする。パターン選択からPrintString必須条件を除去し、BeginPlayの既存単一PrintString経路は保持する。
- `LowerEntryCallExpression`でSequenceと単一Entryの呼び出し式構築を共有。通常のstatic／selfリテラルCallは既存`LowerLiteralNativeCall`へ渡し、PrintStringのWorldContext／色／名前／既定値Analyzer分岐は一箇所へ集約した。共通メタデータ式Loweringへの移行・撤去条件は残存処理表で追跡する。
- ノード配列順ではなくThen→Executeの実Exec edgeから順序を決める。到達可能領域を対象に重複・不正ノードID、複数後続、循環、不正ピン、未消費の入出力Exec edge、接続されたデータ式／Target、未対応Statementを拒否する。非void関数の戻り値Loweringは未対応として明示拒否し、void通常関数の末尾Resultは許容する。
- 全CallとExec接続の検証、および変数抽出が成功するまで関数をOutIRへ追加しない。単一グラフの途中で不正なCallが見つかった場合も、部分的な関数本体を公開しない。
- `SingleNativeCall`、`FunctionCallChain`、`CustomEventCallChain`、`BeginPlayCallChain`、`MixedPrintCallChain`の実Blueprint Automation Testを追加。Graph IR内のノード／edge配列反転に依存しない実行順、C++・JSONの文数、重複後続・不正Execピン・循環・追加流入・Data接続・末尾関数パス改変の拒否を検証する。
- Semantic IR／JSON schemaは変更なし。既存のCall列roundtripを通常関数／Custom Event／BeginPlayのメタデータとstatic／self／PrintStringの組み合わせへ拡張し、関連Pythonテスト83件と`git diff --check`が成功。UE環境がないためUHT／UBTとAutomation Testは未実施。
- Branchの独立したCall Lowering、複雑LoopのBeginPlay依存、制御フローの任意混在、接続データ式と非void ReturnのLoweringは残るため、P1全体の完了チェックは継続する。

### P1 Sequenceの共通Call Lowering（2026-10-30、UE検証待ち）

- `LowerSequenceImpl`のStatement候補をPrintStringから一般のCallFunctionへ変更し、`Lower`のSequence振り分けからPrintString有無の条件を除去。通常関数／Custom Event／BeginPlayの共通Entry検証と、出力ピン順・最後のFunction Result到達の既存検証を維持する。
- `LowerLiteralNativeCall`を追加し、通常関数ForLoopの既存リテラル引数処理を抽出してSequenceと共有する。Reflection解決済みnative void関数のstatic／self呼び出し、bool／int32／有限float・doubleの非参照・非const単一値入力を対象とする。未対応データ接続、任意Target、private／アクセスできないprotected、関数パス不一致、重複・余分なピン、型／値不一致を診断付きで拒否する。
- PrintStringは既存のWorldContext／色／名前／既定値のAnalyzerを維持し、Sequence内で共通Callと混在できる。新たな専用Emitterは追加していない。残るAnalyzer分岐は、メタデータに基づく共通式Loweringへこれらの入力型・既定値検証を統合した時点で撤去する（下表へ追跡項目を追加）。
- 実Blueprintの`FunctionSequenceNativeCalls`（UObject・staticのみ）、`FunctionSequenceStaticAndSelf`（Actor・static＋self）、`FunctionSequenceMixedPrint`、`FunctionSequenceNumericCall`のAutomation Testを追加。出力順・所有者include・JSONのCall文保持、改ざんした関数パス／引数型／値／static属性／重複ピン／native属性、self以外の明示Target拒否を検証する。既存ForLoopも同じ共通ヘルパーを使用するためUE回帰確認が必要。
- Semantic IR／JSON schemaは変更なし。Pythonでstaticのみ／static＋self／PrintString混在のCall順・引数型・TargetをJSON往復とMock Providerまで保持するケースを追加。関連Pythonテスト77件と`git diff --check`が成功。UE環境がないためUHT／UBTとAutomation Testは未実施。
- パターン別Lowerer選択、単一Entry／BranchのCall Lowering統合、複雑LoopのBeginPlay依存、データ接続・より広い引数型対応は残るため、P1全体のチェックは継続する。

### P1追加実装（2026-10-26、検証済み）

- `Lower` のパターン分類対象をBlueprint全体から、共通Entry監査で確定した唯一の実行グラフへ限定。未接続の標準Event、Construction Script、補助グラフに存在するBranch／Sequence／ForLoop／ForEachLoopが、実際の関数のStatement Lowering選択へ干渉しないようにした。
- EntryからExec edgeを前向きに、各StatementへのData edgeを上流へ再帰走査し、到達可能なStatementと純粋式だけを分類対象にした。同じ関数グラフ内に残った未接続ノードもパターン選択へ干渉しない。
- 単一PrintStringの関数／Custom Event Lowererを到達可能ノード集合へ移行し、同じグラフ内と別の未接続グラフ内のBranch／Sequence／ForLoop／ForEachLoopを無視する回帰テストを追加。
- Sequence Lowererの対象グラフ選択、ノード検証、Data／Exec edge数検証を到達可能領域へ移行。同じグラフ内の孤立Branch／ForLoopと孤立Data islandを無視する一方、到達可能なPrintString入力への不正Data接続は引き続き拒否する統合テストを追加。
- Branch Lowererの対象グラフ選択、単一／ネストBranchのノード・Exec edge・Data edge・訪問数検証を到達可能領域へ移行。同じグラフ内の孤立Branch／ForLoopと孤立Data islandを無視し、到達可能なConditionへの不正Data接続は引き続き拒否する統合テストを追加。Loop系Lowererの統合は未完了のため、P1チェック項目は継続扱いとする。
- EntryなしForEachの既存診断fixtureは、active graphが存在しない場合に限り元ノードGUIDとMacro名を保持する互換診断を維持。active graphがある場合は未接続グラフを分類対象にしない。
- UE 5.7 UHT／UBTビルド成功。該当ForEach診断テストおよび`BlueprintAIConverter`全142件のAutomation Test成功（Exit Code 0）。

## P0作業記録（2026-10-22〜2026-10-23）

- 共通CallのReflection解決失敗時に、Resolverが返した具体的な診断を呼び出し元へ伝播する変更を追加。従来の一般的な未対応診断も維持する。
- 専用PrintStringと共通Callの差分を確認: PrintStringはWorldContextの`this`、文字列リテラル／メンバー／Concat／整数からの変換、bool、LinearColor、有限float、Key、ループIndexの式を扱う。一方、共通CallはReflectionでstatic/self/公開オブジェクトTarget、引数数・型・方向を確認するが、引数の出力は主にbool・数値リテラルに限定される。
- includeは共通経路でstatic所有クラスのIncludePathを取得する一方、PrintString関連ヘッダーには既存の補助処理がある。両者の統合にはWorldContextメタデータ、型付き式、必要ヘッダーの一元化が必要。
- 共通Call EmitterにReflectionで型検証したFStringリテラルのエスケープ付き出力を追加。文字列の式・変数・WorldContextには未対応であり、PrintString専用経路は維持する。
- WorldContextメタデータ付き静的UFunctionの先頭`this`を、Reflection署名で引数名・型・方向を検証したうえで共通Callに渡す経路を追加。既存PrintString専用分岐は引き続き優先されるため、完全移行ではない。
- WorldContext静的関数のResolver回帰テストを追加。実際の共通Emitter生成テストおよびUE実行は未実施。
- SignatureResolverでWorldContextを先頭のUObject*入力引数かつnative/staticとして検証し、DefaultToSelfは同一引数名に限り許可。PrintStringのReflection署名テストに先頭位置・型・方向の検証を追加。
- 共通CallでReflection署名の型パスと一致する`LinearColor`リテラルをRGBA有限値に解析し、`FLinearColor(...)`として出力する処理を追加。PrintString専用Emitter優先のため、この共通経路の生成テストは今後追加する。
- 共通CallでFStringメンバー変数をReflection検証済みの公開ネイティブ`FStrProperty`に限り`this->`参照として生成。型不一致のFString変数を拒否する回帰テストを追加。PrintString専用経路の変数参照は従来どおり維持する。
- WorldContextの実引数`this`がReflection上の期待UObjectクラスから派生していること、参照引数ではないことを共通Emitterで追加検証。署名回帰テストにも期待型と非参照属性を追加。
- 共通Callのfloat/doubleリテラルを数値解析後に正規化し、float範囲外を拒否するよう変更。指数表記への単純な`f`追加を廃止。生成テストとUEビルドは未実施。
- 共通Callで公開ネイティブbool/int32メンバー参照をReflection型検証付きで出力する経路を追加。Actorのbool参照と型不一致拒否のAutomation Testを追加（未実行）。
- 共通Callの指数表記float正規化とfloat範囲外拒否をActorのSetActorTickIntervalで検証するAutomation Testを追加（未実行）。
- 共通Callの公開ネイティブfloat/doubleメンバー参照をReflection型照合付きで追加。Actorのfloat引数参照とdouble偽装拒否のAutomation Testを追加（未実行）。
- ForEachの検証済みint32 Index/Elementを共通Callの引数として渡す経路を追加。要素参照時の宣言もPrintString以外に対応。既存PrintString専用検証は維持し、UEでの生成・回帰テストは未実施。
- ForEachのローカルIndex/Element名をint32以外の型として偽装した場合、メンバー参照にフォールバックせず拒否する検証を追加。Element型偽装のAutomation Testを追加（未実行）。正例テストは未追加。
- ForEach内の非PrintString共通Call（SetActorTickEnabled）の正常系、Element未使用時の宣言省略、Index/Element型偽装拒否のAutomation Testを追加（未実行）。Index/Elementを実引数として出力する正常系は別途必要。
- ForEach CompletedのPrintString専用リテラル制限を除去し、既存EmitExpressionStatementの検証へ統合。Completedの非PrintString共通Callとループ後の出力順を確認するAutomation Testを追加（未実行）。
- ForEachのIndex/ElementをPlayerController::SetMouseLocation(int32, int32)へ直接渡す正常系Automation Testを追加（未実行）。要素宣言と共通Callの出力を検査する。Reflection署名・親クラス解決・UEビルドの検証待ち。
- ユーザー報告: UEビルドは問題なし（Automation Testの実行結果は未確認）。ForEachのIndex/Elementと継承ネイティブプロパティの名前衝突をReflectionで拒否し、Actor::InitialLifeSpanを使う拒否テストを追加（新規変更は未検証）。
- PrintString専用Emitterの直接FString参照・ネスト文字列式でコンテナ/参照/const修飾を拒否。配列・参照・const拒否のAutomation Testを追加（未実行）。既存Blueprintメンバー参照の互換性は維持。
- PrintStringの直接FStringメンバー参照とConcat内のFString参照に対し、親クラスのネイティブプロパティが存在する場合はReflectionでFString型・公開指定を検証。Actor::InitialLifeSpanの型偽装拒否テストを追加（未実行）。Blueprint独自変数は従来経路を維持。
- PrintStringの直接/Concat内FString参照にSemantic IRの変数一覧を渡し、宣言済みBlueprint変数についてFString単一値・無修飾・C++型名を照合。int32変数のFString偽装拒否テストを追加（未実行）。未宣言名の一律拒否は既存互換性を踏まえ今後の課題。
- PrintStringの未宣言FStringメンバー名をSemantic変数一覧・親クラスReflectionの双方で確認し、両方に存在しない場合は拒否。関数先行文・ForLoop内の式文にも変数一覧を渡し、未宣言参照拒否テストを追加（未実行）。
- PrintStringの`Conv_IntToString`／整数演算／Concat内整数式を共通の再帰Emitterへ集約。Getter由来int32をSemantic変数宣言または親クラスReflectionと照合し、`SourcePinId`なしの参照を現在のForLoop／ForEachスコープに存在するIndex／Elementだけに限定。ネストしたForLoopでは外側・内側双方のアクティブIndexを追跡する。型偽装・未宣言ローカル拒否テストを追加し、全139件のAutomation Test成功を確認。
- PrintStringを`FBPReflectedCallResolver`経由の共通Callへ移行。FStringリテラル／Blueprint・ネイティブメンバー／Concat／Conv_IntToString、WorldContext、bool、LinearColor、float、FName KeyをReflection署名と照合して生成する。`EmitPrintStringStatement`、式文ディスパッチのPrintString分岐、ForEachのPrintString名判定を削除。KeyのSemantic型をStringからNameへ修正し、既存C++出力Parityを維持。
- `FBPFunctionSignatureResolver`で各パラメータの`CPP_Default_<ParameterName>`有無と値を署名配列へ保持。Semantic Callで末尾引数が省略された場合、既定値が存在し、bool／整数／有限float・double／FString／FName／LinearColor／null Objectとして安全に変換できるものだけを共通Emitterが明示出力する。途中省略、出力・未対応参照、既定値なしの必須引数は拒否する。
- `app.semantic_editor_e2e`を追加。UE Editor Automationを起動し、成功ログに含まれる今回のexport成果物を`app.semantic_roundtrip --provider mock`へ渡す。Editor失敗、成果物欠落、Python schema／Provider境界失敗を終了コード1にする。実環境で変数3件を含むSemantic JSONの一気通貫成功を確認。
- `FBPSemanticLowering::Lower`のパターン振り分け前に共通Entry監査を追加。接続を持つグラフについてactive Entryなし、同一グラフの複数active Entry、EntryとGraph Kindの不一致、複数の実行Entryグラフをグラフ名・ノードID付き診断で早期拒否する。未接続の標準Eventと空Construction Scriptは引き続き許容する。
- **検証済み:** 最新作業ツリーのUE 5.7ビルド、全142 Automation Test、Semantic関連Pythonテスト43件、実Editor export→Python CLI E2Eが成功。

## 残存専用処理（完了まで追跡）

| 箇所 | 現状・問題 | 移行先／削除条件 |
| --- | --- | --- |
| `BPSemanticLowering.cpp::Lower` | ノードの有無でパターンを選択 | 関数単位のEntry／Statement Loweringへ移行後に撤去 |
| `LowerEntryCallExpression`のPrintString Analyzer分岐 | 単一Entry／Sequence／BranchのWorldContext／色／名前／既定値検証を既存Analyzerへ集約。一般のリテラルnative callはForLoopと共通化（UE検証待ち） | メタデータ由来の共通式Loweringに入力型・既定値検証を統合後、関数名分岐を削除 |
| ネストBranch Lowering | 共通Entryに移行（UE検証待ち）。boolリテラル条件に限定し、通常関数は全経路が同じ末尾Resultへ到達するvoid形状のみ | 接続された条件式、途中Return／非void戻り値、任意Statement混在を明示CFGと署名検証で段階的に拡張 |
| 複雑ForLoop Lowering | 実BlueprintのBeginPlay／Custom Eventは共通Entryへ移行（UE検証待ち）。通常関数は単一Loop／Body Callに限定。Graph IRのみではBeginPlay Policyを維持 | 通常関数の多重ループ・Completed後続をResult到達監査付きで拡張し、パターン別走査を整理 |
| ForEachLoopのStatement／式Lowering | 共通Entryに対応（UE検証待ち）。int32配列・PrintString・Index／Element変換に限定 | 一般のCall／式Loweringと任意の要素型へReflection・スコープ検証を保って段階的に拡張 |
| `BPSemanticCodeGenerator.cpp::Generate` | 複数関数と明示CFGに対応済みだが、受理するCFG形状を構造化Branch／Loopへ限定 | 共通CFG走査と診断を拡張し、対応ノードを増やす際もBlock順から意味を推測しない |
| `BPSemanticCodeGenerator.cpp::BuildFunctionShape` | out値／可変参照／参照戻り値を未対応Policyで拒否し、native Blueprint event override元をReflection照合（UE検証待ち）。公開範囲をReflection／IR／C++／JSONへ保持（UE検証待ち）。Interface継承は未完了 | 公開範囲・署名PolicyのUE検証。出力代入・寿命検証を実装して拒否Policyを段階的に解除し、Interface継承と対応override形式を拡張 |
| `BPSemanticJsonSerializer.cpp`／`PythonServer/app/semantic_json.py` | schema_version 2。Branch／Loop／Breakと再帰Statement、コンテナ型ツリー・enum／byte／delegate種別は同期済み。通常関数Branch／ネストBranch／通常関数ForLoop／Actor ForEach／Break付きForEach／入れ子ForLoopの実export fixtureを追加（Python検証済み・実UE待ち） | 新IR表現ごとに両側を更新しgolden／roundtripで確認。準備済みの実Editor E2Eを実行し、共通Entry移行時も同じ対象で回帰確認する |

## 最新の検証結果

- 2026-10-31（PR作成時の統合確認）: `6f89a9d`（PR #114）までのmainへ統合。既存のLoop／Break・型ツリー検証、空container種別の互換性、共通Entry対応を保持し、追加のLoop export E2Eテストを`test_semantic_loop_export_json.py`へ分離した。内側Completedを親Bodyの後続へ置く最新IRに入れ子fixtureを同期。関連Pythonテスト202件と`git diff --check`成功。UHT／UBT・Automation Test・実Editor export E2Eは未実施。

- 2026-10-31: 標準ForLoopを2段接続する実Blueprint fixtureとSemantic JSON exportを追加。内側／外側Completedを持つJSONの実Python CLI連携2件を含む関連Pythonテスト191件と`git diff --check`成功。新規fixture・既存fixtureの入口変更のUHT／UBT・Automation Test・実Editor export E2Eは未実施。

- 2026-10-31: 標準ForEachLoopWithBreakを使う実Blueprint fixtureとSemantic JSON exportを追加。実Python CLIでの正常系・不正Break出典GUIDの拒否系2件を含む関連Pythonテスト189件と`git diff --check`成功。新規C++ fixtureのUHT／UBT・Automation Test・実Editor export E2Eは未実施。

- 2026-10-31: 通常関数ForLoopとActor ForEachの実export経路を追加し、Branchと保存検証を共通化。実Python CLIでの正常系・CFG後続／配列要素型の拒否系4件を含む関連Pythonテスト187件と`git diff --check`成功。UHT／UBT・Automation Test・実Editor export E2Eは未実施。

- 2026-10-31: 通常関数BranchとネストBranchの実BlueprintテストへSavedへのexportと成果物通知を追加。Editorを模擬して実Python CLIへ渡す4件を含む関連Pythonテスト183件と`git diff --check`成功。新規C++コードのビルド・Automation Test・実Editor export E2Eは未実施。

- 2026-10-31: 再帰型ツリーとenum／byte／delegate種別のPython契約を同期。新規49件を含む関連Pythonテスト179件と`git diff --check`成功。C++変更なし。UHT／UBT・Automation Test・実Editor export E2Eは未実施。

- 2026-10-31: Loop／Break StatementのPython受理と再帰検証を同期。新規35件を含む関連Pythonテスト130件と`git diff --check`成功。C++変更なし。UHT／UBT・Automation Test・実Editor export E2Eは未実施。

- 2026-10-30（新規変更、UE検証待ち）: 複雑ForLoopのイベント経路をUObject Custom Eventへ拡張し、多重ループの先行文・最上位Completedを正規化。実Blueprint回帰3ケースを追加、関連Pythonテスト141件成功。UHT／UBT・Automation Test・実Editor E2Eは未実施。

- 2026-10-30（新規変更、UE検証待ち）: ForEach／ForEachWithBreakをUObject通常関数・Custom Eventへ拡張し、Completedからの末尾Result到達を監査。実Blueprint回帰4ケースを追加、関連Pythonテスト138件成功。UHT／UBT・Automation Test・実Editor E2Eは未実施。

- 2026-10-30（新規変更、UE検証待ち）: ネストBranchをUObject通常関数／Custom Eventへ拡張し、通常関数の全経路の共有Result到達を監査。実Blueprint回帰3ケースを追加、Loop／Break同期を含む関連Pythonテスト134件成功。UHT／UBT・Automation Testは未実施。

- 2026-10-30（新規変更、UE検証待ち）: JSON v2のForLoop／ForEach／Breakと任意のコンテナ型ツリー検証を追加。関連Pythonテスト132件成功。LoopContracts Serializer fixtureを追加したが、UHT／UBT・Automation Test・実Serializer出力E2Eは未実施。

- 2026-10-30（新規変更、UE検証待ち）: Branchの単一・ネストCallを共通Loweringへ移行し、PythonにBranch文の再帰検証を追加。実Blueprint回帰4ケースを追加、関連Pythonテスト95件成功。UHT／UBT・Automation Testは未実施。

- 2026-10-30（新規変更、UE検証待ち）: 単一EntryのPrintString限定Loweringをstatic／selfの直列Call走査へ拡張。実Blueprint回帰5ケースを追加、関連Pythonテスト83件成功。UHT／UBT・Automation Testは未実施。

- 2026-10-30（新規変更、UE検証待ち）: SequenceをPrintString非依存のnative void callへ拡張し、通常関数ForLoopとリテラルCall Loweringを共通化。実Blueprint回帰4ケースを追加、関連Pythonテスト77件成功。UHT／UBT・Automation Testは未実施。

- 2026-10-30（新規変更、UE検証待ち）: 複数関数統合の重複・部分結果防止、通常関数2つ＋Custom Eventの実Blueprint export fixture、E2Eテスト選択を追加。関連Pythonテスト74件成功。UHT／UBT・Automation Test・実Editor export E2Eは未実施。

- 2026-10-30: 関数署名の引数名・宣言順、戻り値パラメータ、override／interfaceメタデータ整合性をGenerator入口で検証。UE 5.7 UHT／UBT成功、追加テスト成功、`BlueprintAIConverter` 全148件のAutomation Test成功（Exit Code 0）。
- 2026-10-29: `2961277`でBranch合流、Loop Body／Completedとback-edge、Returnを明示Semantic CFGとしてLowering／生成し、Loop Body内Branchを含む回帰テストを追加。
- 2026-10-26: `Lower` のパターン分類を共通Entry監査で確定した実行グラフの到達可能Statement領域へ限定し、同一／別グラフ内の未接続制御ノードによる誤振り分けを防止。単一PrintString、Sequence、Branch Lowererを同じ到達可能ノード集合へ移行。UE 5.7 UHT／UBT成功、`BlueprintAIConverter` 全142件のAutomation Test成功（Exit Code 0）。
- 2026-10-24: PrintStringをReflection共通Callへ移行し、専用Emitter・式文分岐・ForEach専用判定を削除。KeyをFName型としてSemantic IR／JSONへ保持しつつ既存C++出力Parityを維持。UE 5.7 UHT／UBT成功、全139 Automation Test成功（Exit Code 0）、PythonのSemantic JSON／roundtrip／signature audit 39件成功。
- 2026-10-24: `Lower`入口へ実行Entry監査を追加し、Entry欠落・同一グラフ複数Entry・Graph Kind不一致・複数実行グラフをパターン選択前に診断付きで拒否。UE 5.7ビルドと全141 Automation Test成功（Exit Code 0）。
- 2026-10-24: PrintStringのネスト整数式について、Blueprint／ネイティブint32メンバーとForLoop／ForEachローカルの参照元・型・スコープ検証を追加。UE 5.7 UHT／UBTビルド成功、`BlueprintAIConverter` 全139件のAutomation Test成功（Exit Code 0）。WorldContext署名テストを現在の明示的受理Policyへ同期し、公開ネイティブboolの共通Call回帰を専用テストActorで安定化。
- 2026-10-24: UE 5.7 `MyProject2Editor Win64 Development` のUHT／UBTビルド成功。
- 2026-10-24: Graph IRの親クラスと実Blueprintの親クラス一致を共通Entry解析とActor位置経路で検証。UObjectへのBeginPlay、UObject／ActorComponentへのActor位置APIの誤適用、および親クラス改ざんを診断付きで拒否する統合テスト成功。
- 2026-10-24: UObject通常関数の `ForLoop -> UGameplayStatics::SetSubtitlesEnabled(bool) -> Function Result` を反射署名とExec接続によりLowering。ForLoop内の式から所有クラスincludeを収集する経路と `FunctionForLoopCall` 統合テストも追加。
- 2026-10-24: UObject通常関数の `Branch -> PrintString -> Function Result` を共通Entry解析でLoweringし、両枝のResult欠落を拒否する `FunctionBranch` 統合テスト成功。
- 2026-10-24: `FunctionSequence`、`CustomEventSequence`、既存 `Sequence` のAutomation Test成功。
- 2026-10-24: `BlueprintAIConverter` 全129件のAutomation Test成功（Exit Code 0）。旧Kismet関数形式を期待していたForLoop算術式テストと、selfピンを欠いていたActorLocation／ConnectedFunctionCall fixtureも現在のGraph IR契約へ同期済み。

## 検証ゲート

1. **静的確認:** UFunction署名、WorldContext、型、include、親クラスPolicyの検証に抜けがないこと。
2. **単体テスト:** 既存PrintStringケースに加え、PrintString以外の共通関数呼び出しを検証すること。
3. **統合テスト:** UObject通常関数／Custom Event、Actor BeginPlay、Component、Branch／Loop、複数グラフの拒否動作を確認すること。
4. **JSON連携確認:** Semantic JSONシリアライズ、Pythonスキーマ検証、golden／roundtripを実施し、C++生成との差分を確認すること。
5. **UE実行確認:** UHT／UBTビルドとEditor Automation Testを実行し、結果をPRへ記録すること。実行していない場合は明記すること。
6. **完了判定:** 「残存専用処理」の該当行を削除または明示的な理由付きで更新し、チェックボックスを実際のテスト結果に合わせて更新すること。

## 運用ルール

- この `ROADMAP.md` を方針・未完了課題の単一の記録先とし、同じ内容の設計MDを乱立させない。
- PR単位で該当チェック項目と「残存専用処理」を更新する。コードだけ実装してチェック項目を放置しない。
- 特定の関数名をハードコードする例外を追加した場合、理由・一般化の方法・削除条件を同じPRで記載する。
- 未実装、実装済み、テスト済み、main反映済みを混同しない。
