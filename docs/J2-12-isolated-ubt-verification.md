# J2-12: 生成C++の隔離UHT/UBT検証

## 目的

生成アーティファクトのC++を既存プロジェクトの `Source` やプラグインへコピーせず、検証専用プラグインとして `RunUAT BuildPlugin` に渡す。成功時は、同じアーティファクトfingerprintに対するUHT/UBT証跡をレビュー経路へ渡せる形で返す。

## 実行方法

```powershell
python -m app.ubt_verification <artifact.json> `
  --origin direct `
  --engine-root "C:\Program Files\Epic Games\UE_5.7" `
  --output-root "D:\UE\MyProject2\Saved\BlueprintToCppVerification"
```

インストール済みのコンソールスクリプト `blueprint-to-cpp-verify` でも同じ引数を使用できる。

## 安全境界

- 入力は共通の生成結果Validatorで正規化し、`generated` 状態だけを受理する。
- MVPは平坦なパスのヘッダー1個とソース1個だけを受理する。絶対パス、親ディレクトリ参照、追加拡張子は拒否する。
- 検証ごとに出力先直下へ新規ワークスペースを作り、既存のプロジェクトや `Source` を上書きしない。
- subprocessは引数配列、`shell=False`、1〜3600秒の明示タイムアウトで実行する。
- Windowsのパス長制限に収めるため、内部プラグイン名・モジュール名・ワークスペース名には短い固定形式を使う。

## 証跡

各ワークスペースへ次を保存する。

- `request.json`: origin、artifact fingerprint、対象ファイル、タイムアウト
- `stdout.log` / `stderr.log`: AutomationToolの出力
- `result.json`: 終了状態、終了コード、ワークスペース、レビュー証跡

`BuildPlugin` が成功した場合だけ `uht=passed` と `ubt=passed` を返す。失敗時は `ubt=failed` のみを返し、ログ解析だけでUHT結果を推測しない。タイムアウトやプロセス開始エラーでもログと結果を保存し、実行結果が確定しなかったUHT/UBT証跡を作らない。

## ローカル検証結果

2026-10-13にUE 5.7で最小の `AActor` 派生クラスを検証し、`RunUAT BuildPlugin` が終了コード0で完了した。

- fingerprint: `588a690eca16bdbf4cc0be8b297659170c59fd1290ed0040a01ad07d96205f2e`
- 保存先: `D:\UE\MyProject2\Saved\BlueprintToCppVerification\v-588a690e-3145`
- 結果: UHT passed / UBT passed

これは隔離された最小生成物のコンパイル成功を示す。元Blueprintとの実行時の意味的一致や、すべてのGenerator出力の成功は示さない。

## 次の境界

次段階では、Direct/AI生成APIの実アーティファクトをこのハーネスへ渡し、返された証跡を既存のレビュー記録へ明示的に登録する。サーバー自身が実行した証跡と外部申告は混同しない。
