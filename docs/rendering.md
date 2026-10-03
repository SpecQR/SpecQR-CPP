# 描画とスキャン診断

SpecQR renders SVG, RGBA and PNG using only the C++17 standard library. Diagnostics provide scanning advice, not a scanner guarantee.

`<specqr/render.hpp>` は SVG、RGBA8、PNG、data URL とアプリケーション用描画アダプターを提供します。PNG のチャンク、CRC-32、Adler-32、zlib ヘッダー、非圧縮 DEFLATE ブロックはライブラリ内で実装しており、libpng・zlib・OS の画像 API は必要ありません。非圧縮方式のため、一般的な圧縮 PNG よりファイルは大きくなります。

```cpp
#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <fstream>

int main() {
    const auto code = specqr::QRCode::generate("Hello, SpecQR!");
    specqr::RenderOptions options;
    options.scale = 8;                 // 1 モジュールのピクセル数
    options.margin = 4;                // 静穏領域のモジュール数
    options.title = "SpecQR の例";     // UTF-8 の SVG title

    std::ofstream png("hello.png", std::ios::binary);
    specqr::save_png(code, png, options);
    std::ofstream svg("hello.svg", std::ios::binary);
    specqr::save_svg(code, svg, options);

    const auto image = specqr::to_rgba(code, options);
    const auto& pixels = image.pixels(); // 行優先 RGBA8。const 参照
    const auto png_url = specqr::to_png_data_url(code, options);
    const auto svg_url = specqr::to_svg_data_url(code, options);
    (void)pixels; (void)png_url; (void)svg_url;
}
```

| API | 戻り値・動作 |
| --- | --- |
| `to_svg(code, options)` | 自己完結した SVG 文字列 |
| `to_rgba(code, options)` | `RgbaImage`。`width()`、`height()`、`pixels()` |
| `to_png(code, options)` | PNG の `std::vector<std::uint8_t>` |
| `to_svg_data_url(code, options)` | UTF-8 のパーセント符号化 data URL |
| `to_png_data_url(code, options)` | Base64 の PNG data URL |
| `save_svg`、`save_png` | 呼び出し元の `std::ostream` に書き込み。ストリームは閉じない |
| `draw(code, canvas, options)` | `Canvas` の `resize` と `fill_rectangle` を呼ぶ |

色は `#RGB`、`#RGBA`、`#RRGGBB`、`#RRGGBBAA`、`black`、`white`、`transparent` に対応します。英字の大小と前後の ASCII 空白を許容します。RGBA はアルファで乗算していない *straight alpha* です。SVG では色を正規化し、透明度を `fill-opacity` に分離します。`title` は XML 1.0 に使える UTF-8 だけを受理し、XML 特殊文字をエスケープします。

`RenderOptions::width` を指定すると `scale` より優先されます。幅は `code.size() + 2 * margin` の正の整数倍である必要があります。小数ピクセルへの補間は行いません。

```cpp
specqr::RenderOptions options;
options.width = (code.size() + 8) * 3; // margin=4、1 モジュール=3px
options.foreground = "#112233";
options.background = "white";
options.print_dpi = 300;
```

`print_dpi` は診断の物理寸法計算に用います。PNG の解像度メタデータを追加する設定ではありません。

## 入力・メモリーの契約

- `margin >= 0`、`scale >= 1`。`width` を使う場合も `scale` 自体の検証を行います。
- 描画座標は符号付き `int` の上限以下です。PNG、RGBA、`Canvas` は 1 辺 2,048 ピクセル以下です。最大 RGBA バッファーは 16 MiB です。PNG ではこれに符号化用・出力用バッファーが加わります。
- 色・タイトルはそれぞれ 65,536 **UTF-8 バイト**以下です。C# 版の文字数単位と異なります。
- DPI は有限の正数であり、計算する物理寸法も有限である必要があります。
- オプションのエラーは `specqr::Error`、コードは `INVALID_RENDER` です。出力ストリームの失敗は `std::ios_base::failure` です。メモリー不足などの標準例外はそのまま伝播します。
- `save_*` はオプション検証と出力バイト列の生成を終えてからストリームに書き込みます。I/O 失敗による途中までの書き込みは取り消しません。
- `RgbaImage` のピクセルは変更用参照を公開しません。コピーした画像やバイト列は QR 結果に影響しません。

## スキャン診断

`<specqr/scan.hpp>` では、生成済み `QRCode` と生成前の `Plan` を調べられます。`Plan` の診断は容量超過時にも `evaluated_version` を使い、ピクセル単位の警告を省略します。外部から変更した `Plan` についても、バージョン 1–40 と非負のビット数を検証します。

```cpp
#include <specqr/scan.hpp>

const auto diagnostics = specqr::render_diagnostics(code, options);
for (const auto& warning : diagnostics.warning_details) {
    // warning.code, warning.severity, warning.message, warning.details
}
const auto planned = specqr::render_diagnostics(
    specqr::QRCode::estimate("Hello, SpecQR!"), options);
```

診断には静穏領域、コントラスト比、アルファ、反転色、容量余裕、印刷モジュール寸法、ラスター倍率が含まれます。推奨値は静穏領域 4 モジュール、コントラスト比 4.5 以上（強いコントラストは 7 以上）、印刷モジュール幅 0.25 mm 以上、ラスター倍率 3 以上です。透過色の合成先やカメラ・印刷・画像処理は予測できないため、実際の用途で読み取り検証してください。

描画が受理しない色でも、診断は `COLOR_CONTRAST_UNKNOWN` を返せます。診断は SVG／PNG の生成を代行しません。スキャナーごとの差異と実行した検証結果は検証資料を参照してください。
