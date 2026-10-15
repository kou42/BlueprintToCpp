# Blueprint ForLoop対応

更新日: 2026-10-16

## 対応済み範囲

Blueprint標準Macroの`ForLoop`について、Macro参照グラフ名・パスをGraph IRとCopilot Graph JSONへ保存する。Semantic IRでは`for_loop`文として開始Index、終了Index、LoopBody、Completedを独立して保持する。

現在C++へ変換できる形状は次に限定する。

- `BeginPlay`から`ForLoop`へ接続されている
- `FirstIndex`と`LastIndex`が未接続の`int32`リテラルである
- `LoopBody`が1件以上の直列な`PrintString`へ接続されている
- `Completed`は未接続、または単一の`PrintString`へ接続されている
- Index出力は未使用、または`Conv_IntToString`を経由してLoopBodyの`PrintString.InString`へ接続されている
- BranchはLoopBody直下のboolリテラル条件で、True/Falseが各1件の`PrintString`である
- Blueprintメンバー変数、比較条件Branch、Break、ネストしたループを含まない

生成するC++はBlueprint標準`ForLoop`と同様に終了Indexを含む。

```cpp
for (int32 LoopIndex = FirstIndex; LoopIndex <= LastIndex; ++LoopIndex)
{
    // LoopBody
}
// Completedに接続された処理
```

## 安全上の扱い

対応形状から外れる接続を推測で変換しない。Indexの未知のデータ接続、複数の後続処理、未知のBodyノードなどを検出した場合は診断を返し、C++生成を停止する。

## 次の対応

1. Index出力をLoopBody内の一般的な式・関数引数で使用する
2. 複数のCompleted後続処理を保持する
3. LoopBody内の比較条件Branchとネストを構造化する
4. `ForEachLoop`と配列要素・Array Indexを対応する
5. Break付きループと`WhileLoop`を対応する
