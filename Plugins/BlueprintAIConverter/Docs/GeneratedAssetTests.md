# 検証用Blueprint自動生成テスト

この文書は `BlueprintAIConverter.GeneratedAssets` のAutomation Testをまとめたものです。
テストはEditor上で実際のBlueprintを構築し、`/Game/BlueprintToCppTests` に保存します。
生成される`.uasset`はGitHubにコミットされる静的なバイナリではありません。

## テスト一覧

| テスト名（末尾） | 保存されるアセット | 主な検証 |
| --- | --- | --- |
| Variables | BP_Test_Variables | 宣言変数、Graph IR、Semantic JSON |
| Branch | BP_Test_Branch | 条件分岐、Graph JSON、C++ |
| ArithmeticComparison | BP_Test_ArithmeticComparison | 算術・比較・分岐、C++ |
| ForLoop | BP_Test_ForLoop | 標準ForLoopマクロ、C++ |
| FunctionCall | BP_Test_FunctionCall | ネイティブ関数呼び出し、JSON、C++ |
| ForEachLoop | BP_Test_ForEachLoop | 標準ForEachLoop、配列接続、Graph JSON |

## 一括実行

Unreal EditorのSession Frontend > Automationで `BlueprintAIConverter.GeneratedAssets` を
検索して、該当テストをまとめて実行します。

Editorコマンドラインでも次のように実行できます。

```text
UnrealEditor-Cmd.exe <YourProject>.uproject -ExecCmds="Automation RunTests BlueprintAIConverter.GeneratedAssets; Quit" -unattended -nop4 -log
```

実行後はContent Browserの `BlueprintToCppTests` フォルダでBPを確認します。
Copilot Graph JSONは `Saved/BlueprintCopilotJson`、Semantic JSONは既存Exporterの
保存先に出力されます。各Automation Testのログに出力ファイルパスを記録します。

## 現時点の制約

- Unreal Engine 5.7でのビルドおよびAutomation Test実行は未実施です。
- FunctionCallはネイティブ `PrintString` が対象です。Blueprintユーザー定義関数の
  入出力Pinを含む検証は未実装です。
- ForEachLoopは実BP生成・Graph JSON出力までが対象です。Semantic/C++変換の
  全経路は別途検証が必要です。
- Graph JSONの厳密比較はFunctionCall/ForEachLoopの接続GUIDをGraph IRと
  比較する方式です。別途、期待するノード名・Pin名による独立検証を追加する余地があります。
- テストは同名の既存アセットを更新するため、`BlueprintToCppTests` に
  手作業で編集したデータを置かないでください。

## 今後の実装

1. ユーザー定義関数のEntry/Resultノード、引数、戻り値と呼び出しノードの生成
2. ForEachLoopのSemantic Lowering/C++生成を実BP経由で検証
3. Node/Pinの独立期待値比較、失敗時の差分ログ
4. UEビルド・Automation Test一括実行と不具合修正
