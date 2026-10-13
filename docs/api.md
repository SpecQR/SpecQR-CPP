# API

公開 header は `include/specqr/` です。利用者は private `src/` を include しません。namespace は `specqr`、GS1 は `specqr::gs1` です。

| API | 内容 |
| --- | --- |
| `QRCode::generate(string_view, Options)` | strict UTF-8 text から生成 |
| `QRCode::generate(vector<uint8_t>, Options)` | raw bytes から生成 |
| `QRCode::generate_segments(vector<Segment>, Options)` | manual data/control segments |
| `QRCode::estimate(text/bytes, Options)` | matrix、RS、mask を作らない計画 |
| `QRCode::analyze_segments(segments, Options)` | manual segments の計画 |
| `QRCode::capacity(version, ecc, mode, control_bits)` | data 容量と mode 別最大文字数 |
| `QRCode::matrix()` / `module(x,y)` | const `[y][x]` matrix / bounds-checked module |
| `data_codewords()` / `codewords()` | padding 済 data / interleave 済 data+ECC |
| `planning()` / `diagnostics()` | 選択 version・ECC・segment・mask 理由と counts |

`Options` の default は ECC M、version auto 1–40、mask auto、mode Auto、mixed 最適化 ON、boost OFF です。`version`/`mask` は optional 整数で、未指定が auto。固定 version を選ぶ場合も `min_version <= max_version` 等の option 自体は検証されます。

Mode は Numeric、Alphanumeric、Byte、Kanji または Auto です。manual control は `Segment::eci(0..999999)`、`fnc1()`、`fnc1_second(indicator)`、`structured_append(index,total,parity)`。SA index は 1-based、total は 2–16。control 順序と競合を validate します。

`Segment::byte(string_view)` は UTF-8 を検証し、`Segment::byte(vector<uint8_t>)` は binary をコピーします。Kanji は決定的 QR 対象 table の文字に限定します。ECI 指定時は auto で Kanji を選ばず、入力文字列は UTF-8 のままです。

`Plan` は独立した値で編集可能ですが、`QRCode` 内部の計画は const accessor です。`ok == false` は容量不足、`selected_version` が空の場合は auto 範囲に収まりません。`evaluated_version` は容量を報告した version、`remaining_bits()` は負にもなります。未生成 plan の mask/codewords は存在しません。描画 warning の計算は `render_diagnostics` を別途呼びます。

`specqr::Error` は `std::invalid_argument` を継承し、`code()` と message を持ちます。capacity error では required/capacity bits、version の追加情報があります。invalid UTF-8、mode、version、control、GS1、SA 等を catch できます。メモリ不足の `std::bad_alloc` やユーザー stream 由来の例外はこの error へ変換しません。

Digital Link API の Unicode ドメイン名は事前に ASCII/punycode へ変換してください。IDNA/UTS46 は実装範囲外です。QR 本文の strict UTF-8・絵文字・Kanji はそのまま利用でき、この hostname 制約の対象ではありません。対応 AI catalog と具体的 URL 差分は [GS1](gs1.md) を参照してください。
