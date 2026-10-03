# 開発用の独立検証

These tools are optional development checks. No oracle or decoder is linked into the C++ library or downloaded by its CMake build.

通常のライブラリ・CLI・CTest は C++17 標準ライブラリだけで build できます。ここでは別に Node、Python、Java、独立 encoder/decoder を使います。`package-lock.json` の Nayuki と hash 固定の ZXing wheel/JAR は開発用です。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
npm ci --prefix tools --ignore-scripts

git clone https://github.com/SpecQR/SpecQR.git baseline
git -C baseline checkout 15ad15e5c770ea0e39072f8f88b2733018f02ffd
python3 tools/verify-conformance.py \
  --adapter build/specqr-conformance --baseline baseline
python3 tools/verify-decode.py --adapter build/specqr-conformance \
  --decoder cpp --dependency-dir .tools/zxing-cpp
python3 tools/verify-decode.py --adapter build/specqr-conformance \
  --decoder java --dependency-dir .tools/zxing-java
```

Windows の複数構成 generator では `--adapter build/Release/specqr-conformance.exe` のように実行ファイルを指定します。Java は 17 以上、Python は decoder wheel がある version、Node は 18 以上を使用します。CI では Node 22・Python 3.11・Java 21 を使います。`--no-install` は hash 検証済みの既存 decoder だけを許可し、未準備なら検証を失敗させます。`SPECQR_DEV_NODE_MODULES` に既存の test package directory を指定することもできます。以前の project は変更しません。

## 比較の範囲

- `verify-conformance.py`: 2,868 public API 行列、22 Structured Append set の 112 行列、3 data pattern × 40 version × 4 ECC × 9 mask 選択の 4,320 行列。計 7,300 行列を固定 JS baseline と比較します。data codeword と ECC を含む interleaved codeword も比較します。
- Nayuki: public corpus の 2,240 行列を独立生成して比較します。このうち 1,280 は version 1–40 × L/M/Q/H × mask 0–7 の全組合せです。
- 容量: 640 mode/version/ECC 容量と 1,920 境界 estimate、48 malformed/UTF-8 input。内部演算は GF(256) の全 65,536 積と RS generator/remainder degree 1–255。
- ZXing-C++: 706 scalar/header シンボルの matrix と default scale 8 PNG、5 high-level Structured Append set の 44 PNG。FNC1-second の 152 application indicator を manual/high-level の両経路で検証します。version 1–40 の実 PNG を含みます。PNG は全 RGBA pixel と quiet zone を別に検査します。
- ZXing Java: ECI/FNC1-first/全 135 SA header、5 high-level SA set、32 個の 3-codeword 損傷訂正を検査します。PNG detection は明示的な scale 3、`PURE_BARCODE` や失敗後の matrix fallback は使いません。

実際に実行した件数と hash は `artifacts/*.json` に出ます。比較失敗で期待値を自動更新しません。`--skip-internal` は結果に省略を明記します。source が実行中に変わった場合、完全検証として成功扱いにしません。

## decoder の診断を分離する理由

`SPECQR / 12345 %`、version 4/L/mask 0、default scale 8 の実 PNG と、同じ module matrix を Python 標準ライブラリだけで描画した独立 grayscale control を比較します。Java が両方を検出できない場合も、その失敗数・結果を別の `defaultScaleDiagnostic` に残します。scale 3 の strict 成功や matrix 成功への置換はしません。ZXing-C++ では同じ default-scale PNG の payload を検証します。

ZXing-C++ 3.1.1 は ECI symbol でも `symbology_identifier` property を `]Q1` として返し、`TextMode.ECI` の text に `]Q2\\000026...` を返します。検証は property、raw bytes、ECI text の全てを assert します。Nayuki から直接作る ECI 3/20/26/170 の control でも同じ動作を確認し、`zxing-cpp-eci-controls.json` に残します。Java では `]Q2` property を直接検査します。

これらは有限の synthetic corpus の実行結果です。ISO/GS1 認証、実 camera・印刷物、任意の damage に対する保証ではありません。
