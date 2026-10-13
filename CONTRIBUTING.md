# Contributing

変更は日本語の説明を基本とし、README 冒頭に短い英語の説明を置きます。C++17、runtime 標準ライブラリのみ、決定的出力、入力境界の確認を維持してください。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
(cd build && ctest --output-on-failure)
```

符号化を変更するときは `tools/` の独立比較も実行し、失敗・未実行を pass 件数へ含めないでください。安全性修正で baseline と意図的に異なる場合は理由と回帰テストを残してください。検証手順は [docs/verification.md](docs/verification.md) にあります。

第三者 QR runtime code や依存ライブラリを追加しないでください。credential、個人情報、local path、download 済み tool、build 出力、raw 個人ログを commit しないでください。既存の SpecQR 関連 repository をこの project の変更に巻き込まないでください。
