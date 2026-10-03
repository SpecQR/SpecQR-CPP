# 独立統合レビュー

Independent pre-publication review of the C++17 implementation. No unresolved blocking defect remains in the reviewed source snapshot. Local execution, reviewed evidence, and unrun platforms are distinguished below.

2026-10-03、実装担当とは別のレビュアーが公開 API、全 runtime source、CMake、CLI、利用例と文書を確認しました。レビュアーは runtime source を直接変更せず、指摘を担当者が修正しました。専用の [review_tests.cpp](../tests/review_tests.cpp) はレビュアーが独立に作成しています。

最終検証の前後で、30 個の source・header・test・CLI・CMake・example file の SHA-256 が変化していないことを確認しました。[ファイル別 SHA-256](evidence/review-source-sha256.json) を相対パス順の compact JSON にして得た集合識別子は `4c3cec8e8457e9966deb49ba45621966e914e448e52de183c083bfe99f43e581` です。文書や外部 decoder 用スクリプトはこの識別子の対象外です。

## 修正を確認した指摘

1. **公開された診断値の符号付き整数 overflow。** 編集可能な `Plan` / `Capacity` と SA 診断に `INT_MIN` / `INT_MAX` を渡すと、元の算術ヘルパーは overflow し得ました。SA の `sequence_index()` は reviewer の実行で UBSan が `INT_MIN - 1` を検出し、終了コード 134 で停止しました。現在は減算・乗算より前に `int64_t` へ変換し、描画診断は不正な評価バージョン・負のビット数を拒否します。最終 sanitizer 実行で再発しませんでした。
2. **move 後の値型の不整合。** `QRCode` の移動元が空の行列なのに以前の寸法を使うと、module 参照が範囲外になり得ました。現在は実際の格納行列で寸法・境界を判定します。`Segment` は文字数だけが残る移動元から不正な count/payload を出力し得たため、明示的な move により移動元を空の Byte segment に戻します。9 種類の data/control 移動テストも統合実行しました。
3. **Windows CLI の文字・バイト保持。** 標準入力の CRT text mode は CRLF / `0x1A` を変換し得るため、binary mode に変更しました。日本語・絵文字の引数は `wmain` で UTF-16 を厳密に UTF-8 へ変換し、ファイル名は `filesystem::u8path` を使います。Unix で 29 CLI checks を実行しました。Windows 専用分岐の実行は Windows CI の責務であり、ローカル成功から推定しません。
4. **GS1 の過度に狭い URL 受理範囲。** 当初の 139 fixture 差分をすべて元の入力・結果と照合しました。安全に扱える空の `#` / base `?`、query の backslash、prefix/base の dot 正規化、ASCII host escape、userinfo、IPv4 の標準的な URL 正規化などを復元しました。空の userinfo/password の正規化も実際の Node URL 出力と比較して修正しています。不正 payload の置換や削除は復元していません。
5. **統合時の検証・package 導線。** 描画エラーの例外契約、整数型変更に伴う narrowing、GS1 test の `Threads::Threads` link、空の CLI file path 拒否を確認しました。GS1 fixture の NUL 期待値を C 文字列で切り詰める問題と、IPv4/IPv6 診断・有効な `0x...z` host の誤分類も修正されています。

## レビュアーが実行した検証

環境は macOS 27.0 arm64、Apple Clang 21.0.0、CMake 3.31.10 です。Release と Debug + ASan/UBSan を別 build directory に構築しました。UBSan は `halt_on_error=1` で実行し、最終 run に sanitizer 診断はありません。Apple の leak detector はこの結果に含めません。

| テスト | Release | Debug + ASan/UBSan |
| --- | ---: | ---: |
| `specqr-unit` | 3,855 checks 成功 | 3,855 checks 成功 |
| `specqr-gs1` | 1,416 cases / 14,074 assertions 成功 | 1,416 cases / 14,074 assertions 成功 |
| `specqr-extensions` | 820 checks 成功 | 820 checks 成功 |
| `specqr-core-edges` | 1,247,996 assertions 成功 | 1,247,996 assertions 成功 |
| `specqr-review` | 8,139 assertions 成功 | 8,139 assertions 成功 |

両構成で CTest 5/5、失敗 0、case の skip 0 です。集計単位は各 harness が表示する単位であり、case 数を assertion 数と混同しません。

さらに、次を実行しました。

- CLI の 29 checks。UTF-8 日本語・絵文字の引数と stdin の同一行列、Unicode file path、raw byte 保持、引数エラー、出力を開く前の検証を含みます。
- Release の static / shared 両 package を一時 prefix に install し、別 project の `find_package(SpecQR CONFIG REQUIRED)` consumer を build・実行。元の library build を削除し、install prefix を移動した状態でも成功しました。
- `examples/basic.cpp` を実行し、出力 SVG を XML parser で確認。
- 公開・依存 audit。実行時点の 57 files に禁止した binary、個人パス、credential、第三者 runtime dependency pattern はありませんでした。最終公開 tree の audit は公開担当が再実行します。

専用 review tests では strict UTF-8 の scalar/byte 同値性、不正列、ECI の幅境界、高水準 FNC1 `%`、XML escaping、描画の整数・DPI・メモリ境界、不正 option で stream に書かないこと、40 個の混在 Unicode SA 入力について greedy prefix と planner/merge の一致を検査しています。手動 Byte SA の 300-byte 入力、count-width、summary/full 同値性、dot-only query の保存も対象です。

追加した 4,000 個の構造を持つ URI 変異ケースは、authority・IPv4/IPv6・userinfo・slash/空白修復・dot prefix/value・percent escape の分岐を通ります。受理した URI は正規化の冪等性、primary と GS1 値、未知 query のバイト列と順序を保持することを要求し、拒否は validator と一致することを要求します。単に scheme 不正で終わるランダム入力だけではありません。

統合した core-edge tests では 1,112,064 Unicode scalars、6,953 Kanji entries、100,000 malformed-input fuzz trials を実行しています。GS1 は別に 2,000 property iterations、1,000 任意 byte 入力の組、8 threads で合計 800 回の処理を含みます。

再実行例:

```sh
cmake -S . -B build-review -DCMAKE_BUILD_TYPE=Debug -DSPECQR_SANITIZER=address-undefined
cmake --build build-review --parallel
(cd build-review && UBSAN_OPTIONS=halt_on_error=1 ctest --output-on-failure -V)
cmake -S . -B build-review-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-review-release --parallel
(cd build-review-release && ctest --output-on-failure -V)
python3 tools/verify-cli.py --cli build-review-release/specqr-cli
python3 tools/verify-consumer.py --config Release
python3 tools/verify-consumer.py --config Release --shared
```

## 残る GS1 の意図的差分

1,411 baseline fixtures のうち 1,369 は元の結果を比較し、42 は [明示した差分 manifest](evidence/gs1-deltas.json) に対する assertion を実行します。42 件を baseline 完全一致と呼びません。

| 理由 | 元は受理、C++ は拒否 | 両方拒否、診断のみ変更 | 合計 |
| --- | ---: | ---: | ---: |
| 壊れた percent / UTF-8 を置換しない | 17 | 6 | 23 |
| GS1 payload の dot segment を削除しない | 5 | 1 | 6 |
| Unicode hostname の IDNA mapping は未実装 | 12 | 1 | 13 |
| 合計 | 34 | 8 | 42 |

操作別には、22 件が parse/validate の受理変更、10 件が normalization の受理変更、2 件が creation の受理変更、8 件が診断だけの変更です。QR の通常 UTF-8 入力と Unicode hostname の IDNA は別の機能です。後者は呼び出し側で ASCII IDNA へ変換する必要があります。GS1 の全 catalog・全 URL 規格への準拠は主張しません。

## ソース確認と実行範囲の限界

GF(256)、RS、interleaving、function/data module 配置、mask scoring、count-width、optimizer/preflight、Unicode、GS1、SA、PNG/SVG、公開結果の所有権を確認しました。標準ヘッダーと内部ヘッダー以外の runtime include/link はなく、third-party QR wrapper、libpng、zlib、ICU、Boost を必要としません。PNG の保存 DEFLATE・CRC・Adler は独自実装です。出典は [provenance](provenance.md) に分けています。

別担当の exact differential / Nayuki / decoder evidence と、初回 ECI decoder-property mismatch の保存方針を確認しましたが、その外部 executable をこの reviewer 自身が実行したとは数えません。これらの実測、Java の scale-8 control 診断、最終 GitHub CI と fresh remote clone は [verification](verification.md) と公開担当の証跡を参照してください。

この reviewer は Windows/Linux の実行、ThreadSanitizer、Java decoder、物理カメラ・印刷試験を実行していません。特に Windows `wmain` と MSVC/shared-library 分岐は実 CI 成功を確認してから公開作業を完了する必要があります。本レビューは指定 snapshot の有限の検査結果であり、将来の変更や全入力に対する無欠陥の証明ではありません。
