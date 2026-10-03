# SpecQR-CPP

[![CI](https://github.com/SpecQR/SpecQR-CPP/actions/workflows/ci.yml/badge.svg)](https://github.com/SpecQR/SpecQR-CPP/actions/workflows/ci.yml)

SpecQR-CPP is a from-scratch, dependency-free C++17 QR Code Model 2 generator. It includes planning, GS1, Structured Append, SVG and PNG output with no third-party runtime libraries.

SpecQRのC++版です。Version 1–40、L/M/Q/H、全mask、Numeric・Alphanumeric・UTF-8・binary・Kanji、mixed最適化、ECI/FNC1に対応します。QR符号化とportable PNG/SVGを標準ライブラリのみで実装しています。

## Buildと利用

C++17 compilerとCMake 3.16以上を使います。通常のbuildはnetworkやpackage downloadを必要としません。

```sh
git clone https://github.com/SpecQR/SpecQR-CPP.git
cd SpecQR-CPP
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
(cd build && ctest -C Release --output-on-failure)
cmake --install build --config Release --prefix ./install
```

WindowsではVisual StudioのC++ workloadを用意してください。CMakeのmulti-config generatorでは実行fileが `build/Release/` に置かれます。

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

別projectではinstall prefixを `CMAKE_PREFIX_PATH` に指定します。

```cmake
find_package(SpecQR CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE specqr::specqr)
```

静的libraryがdefaultです。共有libraryは `-DBUILD_SHARED_LIBS=ON`。不要な付属targetは `SPECQR_BUILD_TESTS`、`SPECQR_BUILD_CLI`、`SPECQR_BUILD_EXAMPLES` を `OFF` にできます。実行時に外部QR、zlib、libpng、ICU、Boostを必要としません。CMake project versionは開発識別用の `0.1.0` で、stable releaseの宣言ではありません。

## APIの使い方

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

`matrix()` は `[y][x]`、trueがdarkです。返却dataはconstで保持され、並行読み取りできます。文字列はstrict UTF-8、raw binaryは `std::vector<std::uint8_t>` を使います。入力・option失敗はcode付きの `specqr::Error`、`estimate` の容量不足は `ok == false` です。ECIはcharset宣言であり、bytesの変換ではありません。

```cpp
const auto qr = specqr::QRCode::generate_segments({
    specqr::Segment::eci(26),
    specqr::Segment::numeric("123456789"),
    specqr::Segment::byte(u8"Hello 😀")
});
```

GS1は `specqr/gs1.hpp`、分割QRは `specqr/structured_append.hpp`、描画警告は `specqr/scan.hpp` をincludeします。高水準GS1/FNC1のliteral `%`をseparatorに変えてしまう動作は防ぎます。低水準FNC1 alphanumericのescapeと、Digital Linkのdot-only path制約は [互換性](docs/compatibility.md) を確認してください。

Digital LinkのUnicodeドメイン名だけは、呼出側でASCII/punycodeへ変換して渡します。IDNA/UTS46変換は含めません。QR本文のUnicode・絵文字・Kanjiの対応とは別のURL正規化上の制限です。完全なWHATWG URL/GS1互換を主張しません。

## CLI

```sh
./build/specqr-cli --format png --output hello.png 'Hello SpecQR'
./build/specqr-cli --estimate --ecc H 'Hello SpecQR'
./build/specqr-cli --binary --input payload.bin --format svg --output binary.svg
./build/specqr-cli --help
```

入力file/stdinを改行除去せずに読みます。PNGは `--output` が必要です。exit statusは成功0、入力/描画/IO失敗2、planning容量不足3です。

## 文書と検証範囲

- [API](docs/api.md)、[GS1](docs/gs1.md)、[Structured Append](docs/structured-append.md)、[Rendering](docs/rendering.md)
- [Feature parity](docs/feature-parity.md)、[意図的な差分](docs/compatibility.md)、[resource safety](docs/resource-safety.md)
- [実行済み検証と制限](docs/verification.md)、[独立統合review](docs/integrated-review.md)
- [出典・依存](docs/provenance.md)、[MIT License](LICENSE)

テストは基準SpecQRとのexact比較、独立Nayuki、独立decoder、sanitizerを分けて扱います。既存のC#やSwiftのCI成功をC++の実行結果として数えません。

QR Model 2専用です。Micro QR/rMQR、decoder本体、全GS1認証、実機camera/印刷保証、logo装飾、C ABIは対象外です。GitHub sourceの公開で、registry配布やstable tag/releaseは行っていません。
