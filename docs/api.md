# API

公開headerは `include/specqr/` です。利用者はprivate `src/` をincludeしません。namespaceは `specqr`、GS1は `specqr::gs1` です。

| API | 内容 |
| --- | --- |
| `QRCode::generate(string_view, Options)` | strict UTF-8 textから生成 |
| `QRCode::generate(vector<uint8_t>, Options)` | raw bytesから生成 |
| `QRCode::generate_segments(vector<Segment>, Options)` | manual data/control segments |
| `QRCode::estimate(text/bytes, Options)` | matrix、RS、maskを作らない計画 |
| `QRCode::analyze_segments(segments, Options)` | manual segmentsの計画 |
| `QRCode::capacity(version, ecc, mode, control_bits)` | data容量とmode別最大文字数 |
| `QRCode::matrix()` / `module(x,y)` | const `[y][x]` matrix / bounds-checked module |
| `data_codewords()` / `codewords()` | padding済data / interleave済data+ECC |
| `planning()` / `diagnostics()` | 選択version・ECC・segment・mask理由とcounts |

`Options` のdefaultはECC M、version auto 1–40、mask auto、mode Auto、mixed最適化ON、boost OFFです。`version`/`mask`はoptional整数で、未指定がauto。固定versionを選ぶ場合も `min_version <= max_version` 等のoption自体は検証されます。

ModeはNumeric、Alphanumeric、Byte、KanjiまたはAutoです。manual controlは `Segment::eci(0..999999)`、`fnc1()`、`fnc1_second(indicator)`、`structured_append(index,total,parity)`。SA indexは1-based、totalは2–16。control順序と競合をvalidateします。

`Segment::byte(string_view)` はUTF-8を検証し、`Segment::byte(vector<uint8_t>)` はbinaryをコピーします。Kanjiは決定的QR対象tableの文字に限定します。ECI指定時はautoでKanjiを選ばず、入力文字列はUTF-8のままです。

`Plan` は独立した値で編集可能ですが、`QRCode`内部の計画はconst accessorです。`ok == false` は容量不足、`selected_version`が空の場合はauto範囲に収まりません。`evaluated_version`は容量を報告したversion、`remaining_bits()`は負にもなります。未生成planのmask/codewordsは存在しません。描画warningの計算は `render_diagnostics` を別途呼びます。

`specqr::Error` は `std::invalid_argument` を継承し、`code()`とmessageを持ちます。capacity errorではrequired/capacity bits、versionの追加情報があります。invalid UTF-8、mode、version、control、GS1、SA等をcatchできます。メモリ不足の `std::bad_alloc` やユーザーstream由来の例外はこのerrorへ変換しません。

Digital Link APIのUnicodeドメイン名は事前にASCII/punycodeへ変換してください。IDNA/UTS46は実装範囲外です。QR本文のstrict UTF-8・絵文字・Kanjiはそのまま利用でき、このhostname制約の対象ではありません。対応AI catalogと具体的URL差分は [GS1](gs1.md) を参照してください。
