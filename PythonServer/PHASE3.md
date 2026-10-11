# Phase 3 — 制限付きC++コード生成器

Phase 3のMVPは、未対応の処理を黙って通さず、明示的に失敗させる設計です。基本的な対応対象は、Actorを親とするBlueprint、単一のEventGraph、Blueprint変数なし、単一の`ReceiveBeginPlay`イベントです。このイベントの後に、リテラルの`InString`値を使用する`UKismetSystemLibrary::PrintString`の直列呼び出しを配置できます。

未接続、分岐、純粋、循環、遅延などの未対応ノードは、黙って省略せずエラーにします。ただし、後続の開発で未接続イベントと追加グラフの扱いは一部緩和されています。最新の対応範囲は実装とテストを参照してください。

## CLI/API

解析APIの起動方法：

```bash
cd PythonServer
python -m pip install -e ".[api,test]"
uvicorn app.main:app --host 127.0.0.1 --port 8000
python -m pytest -q
```

`POST /generate` は `{"snapshot": <Phase 1のJSON>, "class_name": "AMyActor"}` を受け取り、メモリ上で2つのファイル（`.h`と`.cpp`）を返します。プロジェクトへの書き込みやコンパイルは**行いません**。

Phase 1の抽出器は、`UK2Node_Event`の`EventReference.GetMemberName()`を`function_name`として出力するよう拡張されています。この情報がない場合、生成器はイベントを曖昧なものとして拒否します。

## 安全性と今後の課題

- これは最小限の決定的なコード生成器であり、**汎用的なBlueprintからC++への変換器ではありません**。
- API呼び出しではLLMを使用せず、機密情報やプロジェクトのソースファイルにもアクセスしません。
- 明示的に設定されたPrintStringのオプションピンは拒否します。Blueprintのすべてのバージョンでデフォルト動作が一致する保証はありません。
- 生成クラスにはプロジェクト固有の`MODULE_API`エクスポート指定がなく、適切なUEモジュールへの組み込みが必要です。
- 対応ノードを増やす前に、ピンの意味解決、関数シグネチャ検証、制御フローの低レベル表現への変換、UBT検証を実装する必要があります。
- UE 5.7でのコンパイルと生成コードのビルドは未実施です。
