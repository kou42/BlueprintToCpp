# BlueprintToCpp ロードマップ

最終更新: 2026-10-22

## 目的と原則

Blueprintの処理を特定のActorやPrintStringに依存せず、検証可能なSemantic IRを経由してC++へ変換する。対応範囲は段階的に広げるが、**一時的な専用実装を完成扱いして放置しない**。

- 対応済みとは「実装・回帰テスト・既存経路との整合性を確認した状態」を指す。UEビルド／Automation Test未実施の変更は検証待ちとする。
- PrintStringは最初の検証対象であり、最終的な変換単位ではない。新しい関数を増やすたびにPrintString専用分岐をコピーしない。
- Actor固有のノードやBeginPlayはクラス固有のPolicyとして扱い、一般のUObject関数に混入させない。
- 未対応のノード・署名・データフローは曖昧に変換せず、理由が分かる診断を返す。
- 既存のActor／ActorComponent変換を維持する。機能拡張は原則として縦断テストを先に追加する。

## 現状（コード調査に基づく。テスト実行結果ではない）

- `FBPParentClassResolver::ResolveParent` はUObject系のネイティブ親クラスを解決できる。
- `FBPNativeSubclassGenerator` は汎用的な派生UCLASSの骨格を生成できるが、Blueprintグラフの完全変換とは別経路。
- `FBPSemanticLowering::Lower` はPrintString、Branch、ForLoop、ForEachLoop、Actor位置操作などのパターンをグラフ全体のノード有無で振り分けている。
- 通常関数／Custom Eventの `Entry -> PrintString` 経路とUObject統合テストは既存。ただし一般的なノードの組み合わせには未対応。
- `FBPSemanticCodeGenerator::Generate` はSemantic関数をちょうど1個、BasicBlockをちょうど1個に制限している。
- `EmitExpressionStatement` には `FBPReflectedCallResolver` を使う共通呼び出し経路があるが、PrintStringは先に専用Emitterへ分岐する。ForEachLoopにもPrintString固有処理が残る。
- Branch／ForLoop／ForEachLoopの既存LoweringはBeginPlay前提の経路を含む。
- PR #109 はUObject派生クラスの回帰テストと通常関数の振り分け改善を提案中。**mainへの反映やUEビルド成功は未確認**。

## 優先順位と完了条件

### P0: 共通関数呼び出し経路を完成させる

- [ ] PrintString専用Emitterと共通 `FBPReflectedCallResolver` 経路の対応差分を列挙する（WorldContext、引数のデフォルト値、文字列・数値・真偽値・色、変数、ループ内Index/Element、必要なinclude）。
- [ ] Reflectionで `UFunction` の所有者、呼び出し対象、パラメータ方向・型、戻り値、デフォルト値を検証する共通Call IR／Emitterの責務を定める。
- [ ] PrintStringを共通Emitterで生成できるようにし、専用分岐は段階的に削除する。特殊なWorldContextなどは関数名直書きではなく、根拠のあるメタデータ／明示的Policyで扱う。
- [ ] PrintString以外の少なくとも2種類の関数（例: 静的関数とインスタンス関数）を同じ経路で生成・検証する。
- [ ] PrintStringの既存ケースとForEachLoopのIndex/Element・Completed経路の回帰テストを通す。
- [ ] `EmitPrintStringStatement` やPrintString専用分岐を残す場合は、理由・移行先・削除条件をこのファイルの「残存専用処理」に記録する。

**完了条件:** PrintStringだけを特別扱いせずに、複数の異なるUFunction呼び出しを共通経路で生成できること。PrintStringの機能退行がないこと。

### P1: グラフ単位から関数単位のLoweringへ

- [ ] BeginPlay、Function Entry、Custom Eventを共通Entry解析で区別し、Reflection署名を確認する。
- [ ] Blueprint全体のノード有無によるパターン振り分けを、関数グラフ単位の検証とStatement Loweringへ移行する。
- [ ] UObject通常関数で `Branch -> PrintString`、`ForLoop -> 関数呼び出し` を変換できる統合テストを追加する。
- [ ] ActorのBeginPlayおよびActor位置操作はActor専用Policyで検証し、非Actorへ誤適用しない。
- [ ] 未対応グラフや複数の実行Entryを黙って無視せず、診断付きで拒否する。

**完了条件:** UObject通常関数で制御フローと共通関数呼び出しを組み合わせられ、Actorの既存変換も維持されること。

### P2: 複数関数・制御フローの生成

- [ ] `Generate` から単一関数制約を除去し、関数ごとに宣言・定義・includeを組み立てる。
- [ ] BasicBlock間の接続、Branch合流、Loop、Returnの意味をSemantic IRで明示する。
- [ ] 同名関数、override、引数／戻り値、可視性、重複宣言を検証する。
- [ ] 複数関数を持つUObject Blueprintを一つのUCLASSとして生成する統合テストを追加する。

**完了条件:** 複数の通常関数・イベントを一つのクラスに安全に生成できること。

### P3: 対応クラス・ノードの拡張

- [ ] ActorComponent／SceneComponentのライフサイクルと通常関数の組み合わせを検証する。
- [ ] UObject派生の各種ネイティブクラスへ拡張する。
- [ ] Widget／Animation Blueprintなど特殊な生成クラスは固有の検証要件を調査して段階的に追加する。
- [ ] Python側の固定親クラス許可リストとUnreal側Reflection解決の差を整理する。
- [ ] 旧 `FBPActorCodeGenerator` のParity用途を確認し、置き換え完了後に削除を検討する。

## 残存専用処理（完了まで追跡）

| 箇所 | 現状・問題 | 移行先／削除条件 |
| --- | --- | --- |
| `BPSemanticCodeGenerator.cpp::EmitPrintStringStatement` | PrintStringの専用出力 | 共通UFunction Call Emitterで既存ケースがすべて通れば削除 |
| `BPSemanticCodeGenerator.cpp::EmitExpressionStatement` | PrintStringを共通解決より先に分岐 | 共通Call解決に一本化後、分岐を削除 |
| `BPSemanticCodeGenerator.cpp` のForEachLoop出力 | PrintString名を直接判定 | ループ値を一般の式として渡せれば専用判定を削除 |
| `BPSemanticLowering.cpp::Lower` | ノードの有無でパターンを選択 | 関数単位のEntry／Statement Loweringへ移行後に撤去 |
| Branch／ForLoop／ForEachLoop Lowering | BeginPlay前提の検証 | 共通Entry解析とクラス固有Policyへ分離後に整理 |
| `BPSemanticCodeGenerator.cpp::Generate` | 単一関数・単一Block制限 | 複数関数／CFG出力とテストが通れば撤去 |

## 検証ゲート

1. **静的確認:** UFunction署名、WorldContext、型、include、親クラスPolicyの検証に抜けがないこと。
2. **単体テスト:** 既存PrintStringケースに加え、PrintString以外の共通関数呼び出しを検証すること。
3. **統合テスト:** UObject通常関数／Custom Event、Actor BeginPlay、Component、Branch／Loop、複数グラフの拒否動作を確認すること。
4. **UE実行確認:** UHT／UBTビルドとEditor Automation Testを実行し、結果をPRへ記録すること。実行していない場合は明記すること。
5. **完了判定:** 「残存専用処理」の該当行を削除または明示的な理由付きで更新し、チェックボックスを実際のテスト結果に合わせて更新すること。

## 運用ルール

- この `ROADMAP.md` を方針・未完了課題の単一の記録先とし、同じ内容の設計MDを乱立させない。
- PR単位で該当チェック項目と「残存専用処理」を更新する。コードだけ実装してチェック項目を放置しない。
- 特定の関数名をハードコードする例外を追加した場合、理由・一般化の方法・削除条件を同じPRで記載する。
- 未実装、実装済み、テスト済み、main反映済みを混同しない。
