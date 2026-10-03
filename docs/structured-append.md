# Structured Append

SpecQR splits text, raw bytes or manual segments into 2–16 QR symbols and validates complete decoded sets. It does not decode images.

`<specqr/structured_append.hpp>` は、共通パリティを持つ複数シンボルの生成と、デコーダーから受け取ったパーツの検証・結合を提供します。高水準 API は指定バージョン範囲のうち分割できる最小バージョンを選び、各パーツを入る限り大きく取ります。全シンボルのバージョンと誤り訂正レベルは共通です。

```cpp
#include <specqr/structured_append.hpp>
#include <specqr/render.hpp>
#include <fstream>
#include <string>

int main() {
    specqr::StructuredAppendOptions options;
    options.qr_options.version = 1;
    options.qr_options.ecc = specqr::Ecc::M;
    options.max_symbols = 16;
    options.diagnostics = true;

    const auto result = specqr::generate_structured_append(
        std::string(100, 'x'), options);
    for (std::size_t i = 0; i < result.symbols().size(); ++i) {
        std::ofstream out("part-" + std::to_string(i + 1) + ".png", std::ios::binary);
        specqr::save_png(result.symbols()[i], out);
    }
    // result.total(), parity(), byte_length(), input_length(), diagnostics()
}
```

`generate_structured_append` は UTF-8 `std::string_view` とバイナリ `std::vector<std::uint8_t>` のオーバーロードを持ちます。バイナリ入力には `Auto` または `Byte` モードを指定してください。入力がヘッダーを含む 1 シンボルに収まるだけの場合は、通常の `QRCode::generate` を使うよう `INVALID_INPUT` を返します。指定範囲で分割できない場合は `DATA_TOO_LONG` です。

テキストは Unicode スカラー値の境界で分割し、UTF-8 の途中で切断しません。これは見た目上の書記素境界とは異なります。例えば結合文字や絵文字シーケンスが別のシンボルになる場合があります。結合すれば元の UTF-8 バイト列に戻ります。

## 手動セグメント

```cpp
std::vector<specqr::Segment> segments{
    specqr::Segment::numeric("123456"),
    specqr::Segment::byte(std::string(80, 'x')),
    specqr::Segment::kanji("漢字"),
    specqr::Segment::byte(std::vector<std::uint8_t>{0, 255, 128, 29})
};
specqr::StructuredAppendOptions options;
options.qr_options.version = 1;
options.split_units = specqr::SplitUnitsDetail::Full;
const auto result = specqr::generate_segments_structured_append(segments, options);
```

数値・英数字・漢字のセグメントは 1 個の分割単位として保持し、その途中では分けません。バイトセグメントは、テキストなら Unicode スカラー値単位、バイナリなら 1 バイト単位で分けられます。元のモードと順番を保持します。大きすぎる不可分セグメントは `DATA_TOO_LONG` になります。

既定の `SplitUnitsDetail::Summary` では全分割単位の一覧を保持しません。`Full` は手動セグメントにだけ指定でき、`diagnostics().split_units` に元セグメント・バイト位置などを保存します。各シンボルの診断には、インデックス、パリティ、バイト範囲、入力または分割単位の範囲、容量、選択マスクが含まれます。

## パリティと結合

SpecQR 各版との整合のため、パリティは元の **UTF-8 バイト列**の XOR です。漢字モードのテキストも UTF-8 で計算し、バイナリセグメントは元のバイト値を使います。Shift_JIS に変換した後のバイト列で計算する方式とは異なる場合があるため、外部システムとの交換時はこの契約を確認してください。

```cpp
const auto parity = specqr::structured_append_parity("ABCD");
std::vector<specqr::StructuredAppendPart> decoded{
    {2, 2, parity, "CD"},
    {1, 2, parity, "AB"}
};
const auto joined = specqr::merge_structured_append_parts(decoded);
// *joined.text() == "ABCD"; joined.bytes() は元の UTF-8 バイト列
// joined.diagnostics().parity_check.matches() == true
```

`structured_append_segments_parity(segments)` は手動セグメントの共通パリティを求めます。生のデコード結果は `StructuredAppendPart(index, total, parity, bytes)` に渡し、テキストとして解釈しません。バイナリ結合の `text()` は `std::nullopt` です。

結合は 2–16 パーツ、1 始まりの有効なインデックス、全パーツの同じ total・parity・データ型、重複・欠落がないこと、本文から再計算したパリティの一致を検証します。順番は任意です。失敗は `INVALID_INPUT` を返し、部分結果は返しません。画像の検出やデコードは行わず、読み取り器ごとの Structured Append メタデータの公開形式は吸収しません。

## 制限と低水準ヘッダー

- メッセージと結合結果は最大 1,000,000 バイトです。手動入力は最大 32,768 個の非空データセグメントです。QR の実容量制限はこれより小さく、別に判定します。
- 高水準の分割は GS1、FNC1、ECI、既存の Structured Append ヘッダー、ECC 自動引き上げと組み合わせられません。手動入力の制御セグメントも受理しません。
- 手動分割では `mode = Auto`、`optimize_segments = true` を維持してください。この設定は元の手動モードを変更する意味ではありません。
- `input_length()` はテキストで Unicode スカラー値数、バイナリでバイト数、手動で元セグメント数です。`byte_length()` は上記のパリティに使う元バイト数です。
- 結果とパーツは変更用の参照を公開せず、入力データを所有します。複数スレッドからの読み取りに可変共有状態を使いません。

既に分割位置が決まっている場合は、`Options::structured_append = StructuredAppendHeader{index, total, parity}` または先頭の `Segment::structured_append(index, total, parity)` を使えます。この低水準 API はヘッダーを付けるだけで、他のシンボルの本文・個数・パリティを検証しません。ヘッダーの `sequence_indicator()` は `(index - 1) * 16 + (total - 1)` です。
