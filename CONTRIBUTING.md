# Contributing

変更は日本語の説明を基本とし、README冒頭に短い英語の説明を置きます。C++17、runtime標準ライブラリのみ、決定的出力、入力境界の確認を維持してください。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
(cd build && ctest --output-on-failure)
```

符号化を変更するときは `tools/` の独立比較も実行し、失敗・未実行をpass件数へ含めないでください。安全性修正でbaselineと意図的に異なる場合は理由と回帰テストを残してください。検証手順は [docs/verification.md](docs/verification.md) にあります。

第三者QR runtime codeや依存ライブラリを追加しないでください。credential、個人情報、local path、download済みtool、build出力、raw個人ログをcommitしないでください。既存のSpecQR関連repositoryをこのprojectの変更に巻き込まないでください。
