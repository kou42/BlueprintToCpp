# J2-5: Editor → Semantic JSON v2 → Python → Mock Provider 検証

## 検証対象

本PRのGolden JSONは **契約テスト用の手製フィクスチャ** です。Unreal Editorが実際に出力した証跡ではありません。CIで検証できるのはPython validator・Mock Providerまでで、UE 5.7.4のビルド・Editor実行・外部AI疎通は含まれません。

## Editorでの手動往復確認

1. UE 5.7.4でBlueprintAIConverterをビルドしてEditorを起動する。
2. Actor派生Blueprintを用意し、int変数 `Health=0`、FString変数 `Title=""`、BeginPlay → Print Stringなどの接続を設定する。Blueprintをコンパイル・保存する。
3. Content Browserで対象Blueprintを右クリックし、`Copy Semantic JSON v2 (Preview)` を選択。コピー内容をUTF-8のJSONファイルとして保存する。
4. 同じBlueprintに `Export Semantic JSON v2` を実行し、`Saved/BlueprintSemanticJson` 以下のファイルを確認する。
5. 両方のファイルを次のコマンドで検証する（実ファイル名に置換）。

```bash
cd PythonServer
python -m app.semantic_roundtrip --provider mock /path/to/copied.json /path/to/exported.json
pytest -q tests/test_semantic_json.py tests/test_semantic_roundtrip.py
```

6. `status: ok` と `provider_status: mocked` を確認する。Mockは**C++を生成しない**。 `semantic_status: unsupported` の場合は `provider_status: rejected` が期待値。
   Python側は検証済みJSON全体をキー順・空白に依存しない形式へ正規化し、`semantic_fingerprint`（SHA-256）を生成する。同じ入力がDirect/AI比較、レビュー、外部検証証跡へ伝播していることを確認する。
7. コピー版と保存版を比較する際は `source.asset_path`、`variables`、`functions`、`graph_traces`、`diagnostics` を照合する。エンジン側のコンパイルや編集によるGUID・順序変化を考慮し、ファイルの単純なバイト一致だけで判断しない。
8. 未対応型、空文字列、0、コンテナ型、未コンパイルBlueprint、異なる関数シグネチャも確認する。

## 完了判定

- Python CIでGolden JSON・破損JSON・旧形式互換の検証が成功する。
- UE Automation Testを実際に実行し、結果をPRに記録する。
- Editorでコピー・保存の双方からJSONを採取し、上記CLIで検証したログをPRに記録する。
- 必要なら実Editor JSONを匿名化して追加フィクスチャ化する。実出力を確認するまで「Editor往復検証完了」とは扱わない。
- `semantic_fingerprint` は入力の追跡IDであり、Semantic JSONの正しさ、生成C++との意味的一致、UHT/UBT成功を証明するものではない。
