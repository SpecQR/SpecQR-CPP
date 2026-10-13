# SpecQR-CPP

[![CI](https://github.com/SpecQR/SpecQR-CPP/actions/workflows/ci.yml/badge.svg)](https://github.com/SpecQR/SpecQR-CPP/actions/workflows/ci.yml)

SpecQR-CPP is a from-scratch, dependency-free C++17 QR Code Model 2 generator. It includes planning, GS1, Structured Append, SVG and PNG output with no third-party runtime libraries.

SpecQR の C++ 版です。Version 1–40、L/M/Q/H、全 mask、Numeric・Alphanumeric・UTF-8・binary・Kanji、mixed 最適化、ECI/FNC1 に対応します。QR 符号化と portable PNG/SVG を標準ライブラリのみで実装しています。

<a id="buildと利用"></a>

## Build と利用

C++17 compiler と CMake 3.16 以上を使います。通常の build は network や package download を必要としません。

```sh
git clone https://github.com/SpecQR/SpecQR-CPP.git
cd SpecQR-CPP
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
(cd build && ctest -C Release --output-on-failure)
cmake --install build --config Release --prefix ./install
```

Windows では Visual Studio の C++ workload を用意してください。CMake の multi-config generator では実行 file が `build/Release/` に置かれます。

```cpp
#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <fstream>

int main() {
    specqr::Options options;
    options.ecc = specqr::Ecc::Q;
    const auto qr = specqr::QRCode::generate(u8"Hello 日本語", options);
    std::ofstream output("hello.png", std::ios::binary);
    specqr::save_png(qr, output);
}
```

別 project では install prefix を `CMAKE_PREFIX_PATH` に指定します。

```cmake
find_package(SpecQR CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE specqr::specqr)
```

静的 library が default です。共有 library は `-DBUILD_SHARED_LIBS=ON`。不要な付属 target は `SPECQR_BUILD_TESTS`、`SPECQR_BUILD_CLI`、`SPECQR_BUILD_EXAMPLES` を `OFF` にできます。実行時に外部 QR、zlib、libpng、ICU、Boost を必要としません。CMake project version は開発識別用の `0.1.0` で、stable release の宣言ではありません。

<a id="apiの使い方"></a>

## API の使い方

```cpp
const auto plan = specqr::QRCode::estimate("1234567890");
if (plan.ok) {
    // version、容量、mode・segment情報を描画せず確認
    const auto qr = specqr::QRCode::generate("1234567890");
    const auto svg = specqr::to_svg(qr);
    const auto png = specqr::to_png(qr);
    const auto data_url = specqr::to_png_data_url(qr);
    const bool dark = qr.module(0, 0); // x, y
}
```

`matrix()` は `[y][x]`、true が dark です。返却 data は const で保持され、並行読み取りできます。文字列は strict UTF-8、raw binary は `std::vector<std::uint8_t>` を使います。入力・option 失敗は code 付きの `specqr::Error`、`estimate` の容量不足は `ok == false` です。ECI は charset 宣言であり、bytes の変換ではありません。

```cpp
const auto qr = specqr::QRCode::generate_segments({
    specqr::Segment::eci(26),
    specqr::Segment::numeric("123456789"),
    specqr::Segment::byte(u8"Hello 😀")
});
```

GS1 は `specqr/gs1.hpp`、分割 QR は `specqr/structured_append.hpp`、描画警告は `specqr/scan.hpp` を include します。高水準 GS1/FNC1 の literal `%` を separator に変えてしまう動作は防ぎます。低水準 FNC1 alphanumeric の escape と、Digital Link の dot-only path 制約は [互換性](docs/compatibility.md) を確認してください。

Digital Link の Unicode ドメイン名だけは、呼出側で ASCII/punycode へ変換して渡します。IDNA/UTS46 変換は含めません。QR 本文の Unicode・絵文字・Kanji の対応とは別の URL 正規化上の制限です。完全な WHATWG URL/GS1 互換を主張しません。

## CLI

```sh
./build/specqr-cli --format png --output hello.png 'Hello SpecQR'
./build/specqr-cli --estimate --ecc H 'Hello SpecQR'
./build/specqr-cli --binary --input payload.bin --format svg --output binary.svg
./build/specqr-cli --help
```

入力 file/stdin を改行除去せずに読みます。PNG は `--output` が必要です。exit status は成功 0、入力/描画/IO 失敗 2、planning 容量不足 3 です。

## 文書と検証範囲

- [API](docs/api.md)、[GS1](docs/gs1.md)、[Structured Append](docs/structured-append.md)、[Rendering](docs/rendering.md)
- [Feature parity](docs/feature-parity.md)、[意図的な差分](docs/compatibility.md)、[resource safety](docs/resource-safety.md)
- [実行済み検証と制限](docs/verification.md)、[独立統合 review](docs/integrated-review.md)
- [出典・依存](docs/provenance.md)、[MIT License](LICENSE)

テストは基準 SpecQR との exact 比較、独立 Nayuki、独立 decoder、sanitizer を分けて扱います。既存の C# や Swift の CI 成功を C++ の実行結果として数えません。

QR Code Model 2 専用です。Micro QR/rMQR、decoder 本体、全 GS1 認証、実機 camera/印刷保証、logo 装飾、C ABI は対象外です。GitHub source の公開で、registry 配布や stable tag/release は行っていません。
