# Copilot Chat / Agent向け JSONエクスポート強化ロードマップ

更新日: 2026-10-16
対象: Unreal Engine 5.7.4 / `BlueprintAIConverter` / GitHub Copilot Chat・Agent

## 目的・スコープ

Blueprintの**関数宣言・実装元・ノード・Pin・接続・変数・依存関係**を、Copilotが根拠を追跡できるJSONとしてエクスポートする。Copilotは開発者がIDEで明示的に起動し、JSONを参照して `.h/.cpp` を作成する。**Python製AI Generator、Provider API、自動送信、Unreal EditorからのAI起動は本トラックの対象外**。

既存Direct C++ Generatorは維持する。本計画は既存`ROADMAP.md`のC1〜C5/V3を置き換えず、**独立したJSONエクスポート品質トラック（CJ）**として並行実施する。旧J0/J1、Snapshot v1、Semantic JSON v2の既存成果を再利用し、旧A1/A2/H1のProvider統合計画は本トラックでは実施しない。

## 現状確認（コード調査に基づく）

| 領域 | 状況 | 主要箇所・不足 |
| --- | --- | --- |
| Snapshot v1 | 実装あり | `FBPSnapshotExporter`。Graph/Pin/Link等を保持。既存利用者との互換性を維持 |
| Graph IR | 限定実装あり | `BPGraphIR.cpp`。GUID、Pin型・デフォルト値、Exec/Dataリンク、CallFunction名を収集。呼び出し先の完全修飾識別子と解決状態は拡充が必要 |
| Semantic IR / JSON v2 | 限定実装あり | `BPSemanticJsonSerializer.cpp` / `BPSemanticJsonExporter.cpp`。型・式・文・診断を出力。全グラフを完全表現する形式ではない |
| 関数署名 | 限定実装あり | `BPSemanticFunctionExtractor.cpp`。GeneratedClass直下のUFunctionとFPropertyを解析。継承関数、Enum/Container/Delegate、既定値、Native/Blueprint実装区別の網羅は未確認または不足 |
| Lowering | MVP制約あり | `BPSemanticLowering.cpp`。一部経路は単一Event Graph等を要求。JSON取得範囲をDirect変換の対応範囲に制限しない |
| Editor保存・検証 | 部分実装あり | 既存JSON保存導線とテストを再利用。UE実機・UHT/UBT・意味一致は別ゲートで確認 |
| Copilot向け規約 | 新規 | 指示ファイル、JSON契約、手動操作手順、診断ポリシーが必要 |

「実装あり」は全Blueprint対応や実機動作保証を意味しない。実装範囲と検証済み範囲を別々に記録する。

## 設計原則

1. **一次情報優先**: Unreal Reflection（`UFunction` / `FProperty`）、`UEdGraph`、Pinから取得可能な事実を出力し、AIに型や呼び出し先を推測させない。
2. **生データと意味解析を分離**: Snapshot/Graph IRは未対応ノードも接続ごと残し、Semantic IRは解決済み情報だけを示す。Copilot用パッケージは両者を参照できるようにする（単一巨大JSONに限定しない）。
3. **互換性**: Snapshot schema v1とSemantic JSON v2を破壊的変更しない。新しいフィールドはバージョン・契約・validator/テストをセットで管理する。必要ならCopilot向けmanifestを別形式で追加する。
4. **完全性を偽装しない**: `complete / partial / unsupported`は解析対象範囲を明記。未解決の型・関数・実行フローは診断と出典GUIDで記録し、Copilotに推測で補完させない。
5. **再現性・追跡性**: アセットパス、グラフパス、ノード/Pin GUID、安定した出力順序、schema version、UE version、出力元を保持する。値の欠落・未接続・明示的な空値を区別する。
6. **セキュリティ**: JSON内のパス・文字列・コード・アセット情報は機密になり得る。Copilotへの投入は開発者が確認して手動実行する。JSON中の文字列を命令として扱わない。
7. **検証**: JSONが正しくても生成C++の正しさは保証しない。UHT/UBT、動作比較、未対応診断を独立して検証する。


## 実装進捗（2026-10-15）

| フェーズ | 実装状況 | 残タスク |
| --- | --- | --- |
| CJ0 | フィールド監査、Schema、Python Validator、合成Fixtureを追加 | 実BlueprintのGolden JSON照合 |
| CJ1 | 関数署名、Enum、Array/Set/Map、既定値出典、Override、Interfaceシグネチャ照合と専用Automation Testを実装 | 空文字/FText/Object既定値、Delegate、継承関数 |
| CJ2 | Copilot Graph JSON v1、Graph/Pin/Link出力と安定順序、限定ForLoop変換を追加 | ForLoop拡張、ForEach/Break/While、CallFunctionのOwner/Target/解決状態、実グラフ整合性 |
| CJ3 | 既存変数IRを活用 | 依存関係、manifest、保存契約の検証 |
| CJ4 | Validatorと検証ガイドを整備 | Copilot手動E2E、UHT/UBT、動作比較 |

上記のCJ0/CJ1/CJ2基盤は `main` へ統合済み。2026-10-16にUE 5.7でUHT/UBTビルドと `BlueprintAIConverter` Automation Test 64件を実行し、全件成功を確認した。実プロジェクト固有のBlueprintによるGolden収集と動作比較は引き続き未実施。

**次の実装順:** ForEachLoop → Break付きループ → WhileLoop/ネスト → ForLoop条件式の拡充 → 空文字/FText/Object参照の既定値の厳密化 → CJ0実Blueprint Golden → CJ2 CallFunction解決。

## マイルストーン（優先順）

### CJ0 — JSON現状監査とゴールデンケース（基盤実装済み・実測照合は継続）

- [x] Snapshot v1 / Graph IR / Semantic JSON v2のフィールド対応表を静的コード監査で作成する（`CJ0_JSON_AUDIT.md`）。**実Blueprint出力との照合は未完了**
- [x] 関数宣言、Native関数呼び出し、Blueprint自作関数、Branch、Return、未対応ノードの代表BPケースを文書で定義する（`CJ0_GOLDEN_CASES.md`）。**全ケースのAutomation・実測Golden収集は未完了**
- [ ] 各ケースの抽出状態と元ノード・Pin GUIDを**実BlueprintのJSON出力で照合**する。静的監査でのフィールド分類・GUID追跡方針は記載済み
- [x] JSONのスキーマ・合成Fixture・検証手順を `docs/` に整備する（`COPILOT_GRAPH_JSON_SCHEMA_V1.md`、`CJ0_GOLDEN_CASES.md`）。**実BlueprintのGolden Fixtureは未収集**

**完了条件:** 代表ケースごとにJSON出力の不足項目と再現手順があり、未対応を成功と誤判定しない。

### CJ1 — 関数・型・デフォルト値の情報拡充

- [ ] 関数の正式名、Owner Class Path、署名、戻り値、引数順序・方向・参照・const・out・returnを保持する
- [ ] デフォルト値は「指定あり/なし/取得不可」を区別し、型付き値と原文表現の保存方式を定義する
- [ ] `UFunction` flags、Native/Blueprint実装の区別、Event/Interface/Overrideを区別できる根拠情報を保持する（単一boolに過度に依存しない）
- [ ] 継承関数・Enum・Array/Set/Map・Delegate・Object/Struct等の対応範囲を明示し、未解決は診断へ
- [ ] Blueprint表示名と実際の関数識別子の差異を扱う

**完了条件:** 引数あり・戻り値ありの自作関数とNative呼び出しを混同せず、型・既定値・関数属性をJSONから再構築できる。未対応型は診断で特定できる。

### CJ2 — ノード・接続・呼び出し先解決の拡充

- [ ] ノード種別・GUID・グラフパス・Pin GUID/方向/型/未接続時値を保持する
- [ ] Exec/Dataリンクの接続元/先を復元でき、接続順・制御分岐・副作用を黙って省略しない
- [ ] CallFunctionの関数参照、Owner、Self/Target、Static/Pure/Const、解決状態を取得する
- [ ] Macro、Event、Interface、Dynamic Dispatch、Latentなどを分類し、未対応でも生グラフ情報を残す
- [ ] ノードID/Pin IDの参照整合性と欠落リンクを検出する
- [x] `ForLoop` Macro参照をGraph IR/JSONへ保持し、int32リテラル範囲と単一`PrintString`本体を両端包含のC++ `for`文へ変換する
- [x] `ForLoop`のCompletedから単一`PrintString`への後続処理をループ後へ変換する
- [x] `ForLoop`のIndex出力を`Conv_IntToString`経由でLoopBodyの`PrintString`へ渡す
- [x] `ForLoop`のLoopBodyに直列接続された複数`PrintString`をExec接続順に変換する
- [x] `ForLoop`のLoopBody直下にあるboolリテラル条件のBranchとTrue/False各`PrintString`を変換する
- [ ] `ForLoop`のIndexを一般式・関数引数へ渡し、比較条件Branch、ネストを変換する
- [ ] `ForEachLoop` / `ForEachLoopWithBreak` / `ForLoopWithBreak` / `WhileLoop`を意味解析し、配列要素・Index・Breakを保持する

**完了条件:** Native関数呼び出しと自作関数呼び出しの対象をJSONで識別でき、Branch・Returnを含む代表グラフの実行/データ接続を復元できる。

### CJ3 — 変数・Blueprint間依存・エクスポート契約

- [ ] メンバー/ローカル変数、型、既定値、必要なProperty flags、所属スコープを保持する
- [ ] 他Blueprint、親クラス、Interface、Struct、Enum、呼び出し先アセットの依存関係を列挙する
- [ ] 依存が未解決・未出力の場合は明示し、必要な追加JSONを特定できるようにする
- [ ] Copilot入力用manifest（アセット一覧、スキーマ版、参照関係、解析範囲、診断一覧）を検討・定義する
- [ ] 既存JSON形式を維持しつつ安定した保存名、文字コード、差分に強い出力順序をテストする

**完了条件:** BP_Player→BP_Weaponのような関数依存について、参照先の識別子と必要な追加アセットを追跡できる。

### CJ4 — Copilot運用・検証ゲート

- [ ] `.github/copilot-instructions.md` にUEバージョン、命名規則、Native再実装禁止、Exec/Data順序、未解決時の停止・報告規則を定義する
- [ ] Copilot Chat/AgentでのJSON指定、生成範囲、`.h/.cpp`出力、レビューの手順書とプロンプト例を用意する
- [ ] JSONスキーマ・GUID・参照整合性・`partial/unsupported`診断の自動テストを整備する
- [ ] 代表BPについてEditorエクスポート→Copilot生成→UHT/UBT→Blueprintとの動作比較を記録する
- [ ] 不明なAPI・型・実行順序をCopilotが推測した場合は不合格とする

**完了条件:** 少なくともNative関数呼び出しとBlueprint自作関数の各1ケースで手動縦断手順が再現可能。生成コードのビルド合格と動作一致は別々に記録する。

## テストマトリクス

| ケース | JSONの確認 | 生成C++の確認 |
| --- | --- | --- |
| 引数なし・void関数 | 署名とFunctionGraphの対応 | 宣言・実装 |
| 引数・戻り値・既定値あり | 順序、型、既定値の有無、Return Pin | 宣言・戻り値 |
| Native関数呼び出し | 実際のOwner/関数参照、Target | 既存UE APIを呼び出し、再実装しない |
| BP自作関数→別BP自作関数 | 呼び出し先アセット、依存未解決診断 | 依存解決後のみ生成 |
| Branch / Return / Pure式 | Exec/Dataリンク、評価順、分岐 | 副作用・戻り値が元BPと一致 |
| ForLoop / ForEachLoop | 範囲、Index/要素、LoopBody、Completed、Break | 反復回数、順序、完了後処理が元BPと一致 |
| 参照/out・配列・構造体 | 型とPin方向、未対応診断 | 型不明時は生成保留 |
| 不正リンク・欠損型 | 診断、`partial/unsupported` | 推測による成功扱いを禁止 |

## 実装・運用上の注意

- CJ0を先に実施し、既存スキーマのどの項目を再利用できるか確定してからCJ1〜CJ3の変更を行う。
- 既存`BPSemanticLowering`のMVP制約はDirect生成用として維持できるが、**Copilot用の生グラフJSONエクスポートを同じ制約で打ち切らない**。
- 本トラックはJSON品質が成果物。Copilotの生成コードは別途レビュー・検証し、AI出力を自動的に正解と見なさない。
- 既存PythonServerやProvider機能を削除しない。本トラックでは追加開発の必須依存にしない。
- 次の実装順は上記の進捗表を参照。CJ0の実Blueprint Golden収集とCJ1の実機検証を優先する。
