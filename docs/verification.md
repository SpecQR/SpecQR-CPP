# 検証と再現手順

C++ の実行結果だけを記録します。既存 SpecQR 各版の CI 成功は C++ の成功件数に含めません。範囲は [feature parity](feature-parity.md)、出典は [provenance](provenance.md) にあります。

<a id="通常のbuild--test"></a>

## 通常の build / test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
(cd build && ctest -C Release --output-on-failure)
python3 tools/verify-cli.py --cli build/specqr-cli
python3 tools/verify-consumer.py --config Release
python3 tools/verify-consumer.py --config Release --shared
python3 tools/audit-public.py
```

Windows では CLI を `build/Release/specqr-cli.exe` と指定します。通常の C++ テストには Python も外部 package も不要です。consumer テストは一時 prefix へ install 後、prefix を移動し元 build directory を削除してから、別 project の `find_package` だけで build・実行します。

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSPECQR_SANITIZER=address-undefined
cmake --build build-asan --parallel
(cd build-asan && ctest --output-on-failure)
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DSPECQR_SANITIZER=thread
cmake --build build-tsan --parallel
(cd build-tsan && ctest --output-on-failure)
```

Sanitizer は対応する GCC/Clang を使います。macOS では AddressSanitizer の leak 検出を無効にして実行し、Linux CI では有効にします。ThreadSanitizer は macOS Clang で実行します。無効化された scope を成功数へ含めません。

<a id="独立比較とdecode"></a>

## 独立比較と decode

この節だけが Node/Python/Java と外部 test 依存を使います。Nayuki 1.8.0、ZXing-C++ 3.1.1、ZXing Java 3.5.4 は version/hash を固定し、library や consumer へ link しません。

```sh
git clone https://github.com/SpecQR/SpecQR.git baseline
git -C baseline checkout 15ad15e5c770ea0e39072f8f88b2733018f02ffd
npm ci --prefix tools --ignore-scripts
python3 tools/verify-conformance.py --adapter build/specqr-conformance --baseline baseline
python3 tools/verify-decode.py --adapter build/specqr-conformance --decoder cpp --dependency-dir .tools/zxing-cpp
python3 tools/verify-decode.py --adapter build/specqr-conformance --decoder java --dependency-dir .tools/zxing-java
```

Java は JDK 17 以上を用意してください。decode スクリプトの初回は test 依存を指定した隔離 directory に取得します。既に配置済みなら `--no-install` が使えます。ネットワーク接続はこの明示的な開発手順に限られます。

7300 個の exact matrix 比較は、公開 API 2868、raw codeword 4320、Structured Append 112（22 set）です。公開ケースのうち 2240 は独立 Nayuki とも比較し、その中で 40 version × 4 ECC × 8 mask の全 1280 組合せを実行します。matrix だけでなく data/ECC/interleave 済 codewords を比較します。容量 640、境界 estimate 1920、GF 積 65536、RS 次数 255、不正入力 48 も別に照合します。

ZXing-C++ の strict 検証は 706 matrix と 750 個の実 PNG（default scale 8）、97805184 画素を確認します。135 SA header、152 種類の FNC1-second indicator の manual/options 両経路 304 symbol、5 つの SA set/44 symbol を含み、両 decode 経路で Version 1–40 を実行します。PNG の全 RGBA 画素、quiet zone、PNG CRC、zlib decode を確認してから scanner へ渡します。matrix からの test raster と生成された実 PNG は別に数えます。

最初の公開コミット `a9a73c260eabae90a0a03b33f1dd634c3ea3c7dd` の [独立検証 CI](https://github.com/SpecQR/SpecQR-CPP/actions/runs/37123663410/job/111204575993) では、上記 exact 比較と ZXing-C++ 検証が成功しました。ZXing Java は 446 matrix・446 個の scale 3 実 PNG、10008270 画素を検証し、32 symbol で各 3 個の data codeword 誤りを訂正しました。両 decode 経路で Version 1–40 を実行し、135 SA header と 5 set/44 symbol から 10 回の独立再結合を検証しています。[Java の実測 report](evidence/zxing-java-ci.json) は commit・run/job URL・adapter hash を含みます。raw decoder 出力は 926 件で、strict 成功 924 件と別枠の scale 8 検出失敗 2 件を独立に照合しました。

<a id="eciのdecoder-api差分"></a>

### ECI の decoder API 差分

初回の ZXing-C++ 検証では 35 個の ECI ケース×2 経路の計 70 件が、`symbology_identifier` へ `]Q2` を期待したため失敗しました。デコードされた全 bytes は一致し、検出失敗ではありません。この最初の失敗を別 report に保存します。

同じ ECI 3/20/26/170 の payload を独立 Nayuki でも生成し、C++ と同じ matrix になることを確認しました。ZXing-C++ 3.1.1 は両方の matrix で `symbology_identifier == ]Q1` を返す一方、`TextMode.ECI` の文字列先頭に `]Q2` と UTF-8 へ変換した ECI 26 の表現を返します。修正した test は bytes・base property・ECI text をすべて検査し、70 件の追加 ECI 確認と 4 つの独立 control を実行します。Java の ECI modifier は直接 `]Q2` を検査します。scale や検出 hint は変更していません。

<a id="javaのdefault-scale8診断"></a>

### Java の default scale 8 診断

Java の strict 実 PNG 検出は scale 3 で、`PURE_BARCODE` や matrix への fallback を使いません。別に default scale 8 の特定画像と同じ画素の独立 PNG を生成し、両方の結果を保存します。そこでの検出拒否は strict 成功へ含めません。PNG ファイルと JSON は CI artifact に保持し、異なる結果になった場合は gate を失敗させます。同一画素の control にも現れる decoder の検出制限と、encoder の誤りを区別します。 実際の上記 CI では `SPECQR / 12345 %`、Version 4/L/mask 0、scale 8 の C++ PNG と、全画素一致を確認した独立 grayscale control がともに `NotFoundException` となりました（検出失敗 2・成功 0）。同じ C++ PNG を ZXing-C++ は正しい payload として読み取りました。この Java の 2 失敗は 446 件の strict PNG 成功に含めていません。

[実行済み Java CI 証跡](evidence/zxing-java-ci.json) では 446 matrix、446 実 PNG、32 個の破損 symbol（各 3 codeword の訂正）が成功しました。全 40 version、135 SA header、5 set/44 symbol、両経路 10 回の独立結合、10008270 画素を検証しています。default scale 8 の本体 PNG と独立 control は両方 `NotFoundException`（失敗 2、成功 0）であり、924 個の strict 成功に含めていません。同じ default PNG は ZXing-C++ で完全な payload が読めました。初回 CI で Windows CLI の LF/CRLF 差分も検出し、stdout を binary mode に修正しています。library の 16 source/header の hash は変えていません。

## 実行結果と限界

ローカル実行・source hash・独立 review の証跡は `docs/evidence/` と [integrated-review](integrated-review.md) に記録します。CI は Windows MSVC、Linux GCC/Clang、macOS Clang それぞれ Debug/Release、ASan/UBSan、TSan、独立比較の 11 job を実行します。各コミットの terminal 結果と実行ログは [GitHub Actions](https://github.com/SpecQR/SpecQR-CPP/actions/workflows/ci.yml) に残ります。ローカル成功は CI 未実行の OS/runtime の成功を意味しません。

有限の合成入力に対する検証です。ISO 認証、すべての scanner、物理印刷、camera、すべての C++ 標準ライブラリ/OS、ABI 安定性、完全な GS1/WHATWG/IDNA の準拠を保証しません。失敗・未実行・意図的差分は pass と混同しません。
