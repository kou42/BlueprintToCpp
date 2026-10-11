# Phase 4 — 型付きBranchのMVP

Phase 3の制限付き生成器を拡張し、`ReceiveBeginPlay`の後に**単一の**`K2Node_IfThenElse`を配置できるようにします。条件は**リンクされていないboolリテラルのCondition**に限り、True/Falseの各分岐先には最大1つの`PrintString`を配置できます。

新しい`typed_ir.py`は型付きの`BoolLiteral`と分岐元ピンの識別情報を保持します。`generator_dispatch.py`はBranchノードの有無に応じて厳格な生成器を選択します。変換失敗時に黙って別の生成器へフォールバックすることはありません。APIは引き続き`POST /generate`です。

## テスト

```bash
cd PythonServer
python -m pip install -e ".[test,api]"
python -m pytest -q
```

## 意図的に後回しにしている項目

- リンクされたBranch条件、変数、数値式、型変換。
- 汎用的な制御フローグラフ（CFG）の構造化、入れ子の分岐、合流点、ループ。
- 任意の関数呼び出しとシグネチャ解決。
- UE固有のピンのデフォルト値の意味、およびPrintStringのオプション入力への完全対応。
- UBTコンパイル、実行時の動作一致検証、AI支援によるコード生成。

この段階ではUE 5.7エンジンでのコンパイルもPythonテストの実行も行われていません。
