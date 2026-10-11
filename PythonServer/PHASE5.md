# Phase 5 — 型付き変数宣言とリテラルSetのMVP

Phase 1のSnapshotで`UK2Node_Variable`のメンバー名を保持し、パーサーとIRまで引き継ぐ機能を追加します。厳格な生成器は、Actorを直接の親とするBlueprintで、単一のBeginPlayイベントと、**リンクされていないリテラル**を代入するVariableSetノードの直列実行を処理します。対応するbool/int/int64型のUPROPERTYメンバーを出力し、float型は精度が明示されている場合のみ制限付きで対応します。

## 意図的な制約

- VariableGetは検出しますが、データ依存関係の解決と評価順序が実装されるまでは**拒否**します。
- リンクされた値、リテラルで表現できないBlueprintのデフォルト値、コンテナ、オブジェクト型、継承メンバー、分岐、ループは拒否します。
- Blueprint変数のメタデータやフラグは、まだUPROPERTY指定子に対応付けていません。生成されるEditAnywhere/BlueprintReadWrite宣言は暫定方針であり、本番利用前に確認が必要です。
- AIによる生成やUBTによるビルド検証は行いません。
- 生成コードを対象UEモジュールへ組み込む際に、追加作業が必要になる場合があります。

## テスト

```bash
cd PythonServer
python -m pip install -e ".[test,api]"
python -m pytest -q
```
