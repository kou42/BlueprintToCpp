# C1: 実Blueprint変換と親クラス解決

## 対応範囲

`FBPParentClassResolver` はGraph IRが保持する親クラスパスから `UClass` を取得し、生成C++に必要な情報を解決する。

- `AActor`派生であること
- ネイティブクラスであること
- `GetPrefixCPP()` とクラス名から得るC++クラス名が安全な識別子であること
- `IncludePath` または `ModuleRelativePath` から公開ヘッダーを取得できること

Generator、Semantic Lowering、Actor location系Loweringと関数呼び出しEmitterは共通Resolverを使用する。`AActor`固定文字列による判定は使用しない。

## 安全な拒否

- `UObject`などActor派生でない親クラス
- 存在しないクラスパス
- 公開ヘッダーを解決できないクラス
- Blueprint生成クラスを直接の親に持つBlueprint

Blueprint生成親を最寄りのネイティブ祖先へ置き換えると、親Blueprintの変数・関数・コンポーネント・Construction Scriptを失う。そのため、依存Blueprintを先に変換する仕組みができるまでは診断付きで拒否する。

## 実Blueprint縦断

`BlueprintAIConverter.Conversion.RealBlueprint.PawnPrintString` はEditor APIで次を実行する。

1. `APawn`派生の一時Blueprintを作成する。
2. Event BeginPlayとPrintStringを配置・接続し、文字列、画面/ログ、色、Duration、Keyを設定する。
3. Blueprintをコンパイルし、Graph IR → Semantic IR → C++ Generatorを実行する。
4. `APawn`継承と `GameFramework/Pawn.h`、PrintString引数を検査する。
5. 生成 `.h/.cpp` を `Saved/BlueprintToCppGenerated/RealPawnPrintString` へ保存する。

2026-10-14のUE 5.7ローカル検証では、プラグインのEditorビルドとAutomation Test 44/44件が成功した。上記の実Blueprint由来ファイルを隔離 `RunUAT BuildPlugin` へ渡し、UHT/UBTも成功した。

- artifact fingerprint: `50a95a7f8958d62c31569f9cb25d43b2a7a83765cebe6c3a88a77fbe0d7f3a79`
- build evidence: `D:\UE\MyProject2\Saved\BlueprintToCppVerification\v-50a95a7f-3cbc`

UHT/UBT成功はコンパイル可能性の証拠であり、Blueprintと生成クラスの実行時出力一致はまだ証明しない。
