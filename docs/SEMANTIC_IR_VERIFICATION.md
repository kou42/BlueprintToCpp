# Semantic IR / Shadow Mode 検証手順（UE 5.7.4）

この文書は `ROADMAP.md` の M0 / M1 と Phase 8 の検証手順をまとめたものです。
**GitHubへのコミットだけでは、UE 5.7.4でのビルド・実行成功を意味しません。**

## 前提

- Unreal Engine **5.7.4** がインストールされている。
- このブランチの `Plugins/BlueprintAIConverter` を含むC++プロジェクトを開ける。
- プロジェクトのC++モジュールがビルド可能である。
- 既存のBlueprintを変更する前にバックアップを取る。

## 1. コンパイルと自動テスト

1. UE 5.7.4でC++プロジェクトをビルドする。最初のエラーについて、ファイル名・行番号・全文を記録する。
2. Editorを起動し、**Tools → Test Automation**（バージョンやレイアウトにより表示箇所は異なる）を開く。
3. `BlueprintAIConverter.SemanticIR` 以下のAutomation Testを実行する。
4. `BlueprintAIConverter` 以下の既存テストも実行し、旧Generatorへの回帰がないか確認する。
5. 失敗テストについて、テスト名・期待値・実際の結果・Output Logを保存する。

主要な対象:

- `SemanticIR.Foundation`: IRのデータ構造
- `SemanticIR.Lowering`: PrintStringのLoweringと拒否条件
- `SemanticIR.Generator`: 新旧のヘッダー・ソース一致、オプション、未対応式
- `SemanticIR.Parity`: Shadow比較、一致・差異検出・失敗時の分離

## 2. Editor実機検証

Actor派生Blueprintを作成し、Event BeginPlayからPrint StringへExec接続する。
文字列、TextColor、Duration、Key、bPrintToScreen、bPrintToLogを設定する。

1. Content Browserで対象Blueprintを**1つだけ**選び、**Convert to C++ (Preview)** を実行する。
2. 旧Generatorによるヘッダーとソースが表示されることを確認する。
3. Output Logの `LogBlueprintAIConverter` で `Semantic IR parity passed` を確認する。
4. `Semantic IR parity failed` が出た場合は、差異の種類とテスト入力を記録する。**新Generatorへ切り替えない**。
5. **Save New Files...** で新規ファイルのみ保存でき、既存ファイルを上書きしないことを確認する。
6. **Save to Module...** では選択モジュールの `Public` / `Private` 配下へ出力し、UEで再ビルドする。
7. 元Blueprintと生成Actorを同じ条件で実行し、表示文字列、画面/ログ、色、Duration、Keyの挙動を比較する。

## 2a. Semantic専用プレビュー（新規）

Content BrowserでActor Blueprintを1つ選択し、**Convert to C++ (Semantic Preview)** を実行する。
このメニューはGraph IR → Semantic IR → C++ Generatorを直接使用する。
旧メニューの **Convert to C++ (Preview)** は引き続き旧Generatorの出力を表示する。

1. 両メニューで同じBlueprintをプレビューし、ヘッダー名・ソース名・生成内容を比較する。
2. PrintStringの文字列、色、Duration、Key、画面/ログ設定を変更し、両経路の一致を確認する。
3. 未対応のデータ接続や追加ノードを含むBlueprintでは、Semantic Previewが診断を表示して停止することを確認する。
4. Semantic Previewは表示時にファイルを書き込まない。ただし、**既存のプレビュー画面にある保存ボタンは利用可能**なので、保存する場合は出力先と既存ファイル保護を確認する。
5. 旧プレビューのShadow比較ログも併せて確認し、差異があれば新経路への正式切り替えを保留する。

## 3. 拒否ケース

- 未接続Event Tick: 変換成功すること
- 空のUserConstructionScript: 変換成功すること
- 実行エッジ付きConstruction Script: 診断付きで失敗すること
- Print StringへのData接続: 診断付きで失敗すること
- 追加の実行ノード、Blueprintメンバー変数: 診断付きで失敗すること
- 不正な色・Duration・bool: 診断付きで失敗すること

## 4. 合格基準と未完了事項

- プラグインがUE 5.7.4でコンパイルできる
- SemanticIRおよび既存のAutomation Testがすべて成功する
- 対応BlueprintでShadow比較が一致する
- 生成されたC++がUHT/UBTを通過する
- 元Blueprintと生成Actorの実行結果が一致する
- 未対応Blueprintを黙って変換せず診断できる

**このチェックリストは検証手順であり、実施済みの証跡ではありません。**
合格するまでは旧Generatorを本番経路として維持する。
