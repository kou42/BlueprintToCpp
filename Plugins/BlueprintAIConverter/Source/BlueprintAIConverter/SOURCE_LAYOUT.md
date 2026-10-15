# BlueprintAIConverter Source構成

このモジュールでは、`Public`と`Private`を同じ機能分類で整理する。新しいファイルは、主な責務に対応するフォルダへ配置する。

| フォルダ | 責務 |
| --- | --- |
| `IR` | Graph IR・Semantic IRのデータ構造と構築処理 |
| `Export` | Snapshot JSON・Semantic JSON・Graph Traceの出力 |
| `Analysis` | リフレクション解析、型・関数・Pin・リンクの解決 |
| `Lowering` | Graph IRからSemantic IRへの変換と引数Lowering |
| `CodeGen` | C++コード、式、引数、出力値の生成 |
| `Validation` | Direct経路とSemantic経路の比較・検証 |
| `Editor` | プレビュー、保存などEditor専用処理。`Private`にのみ配置 |
| `Tests/Integration` | 実際のBlueprintをEditor APIで組み立てる結合テスト |
| `Tests` | テスト専用の公開UObject型 |

`BlueprintAIConverterModule.cpp`はモジュールのエントリポイントであるため、`Private`直下に残す。

公開ヘッダーのカテゴリは`BlueprintAIConverter.Build.cs`でinclude pathへ明示的に登録している。このため、既存コードの`#include "BPGraphIR.h"`のような短いinclude名は引き続き利用できる。
