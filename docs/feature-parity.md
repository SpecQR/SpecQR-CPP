# 機能対応と受入条件

実際に確認した [比較基準](provenance.md) の公開 API と source を基にしたチェックリストです。検証の件数・失敗・未実行範囲は [verification](verification.md) に分けて記録します。

| 機能 | C++ API / 対応方針 | 受入条件 |
| --- | --- | --- |
| QR Model 2 V1–40、L/M/Q/H | `QRCode`, `Options`, `Capacity` | 全 version × ECC × mask 行列、data/ECC codewords |
| mask 0–7 / auto、version range、ECC boost | `mask`, `version`, `min_version`, `max_version`, `boost_ecc` | 固定・auto penalty/tie・範囲境界 |
| Numeric / alphanumeric / UTF-8 / binary | `Mode`, string_view / vector overload | count-width、容量±1、空・不正入力 |
| QR Kanji | `Segment::kanji`, `Mode::Kanji` | 決定的 Unicode–Shift_JIS 表、対象外拒否、decode |
| mixed 最適化 / manual segments | `optimize_segments`, `generate_segments` | JSとの行列比較、Unicode scalar境界 |
| ECI / FNC1 first・second / SA header | `Segment` factories / `Options` | 順序、重複、競合、不正enum、境界 |
| planning / estimate / capacity / diagnostics | `estimate`, `analyze_segments`, `capacity` | overflow結果、version・mode・mask理由、未生成状態 |
| GS1 element strings / AI catalog / check digits | `specqr::gs1` | bounded catalog、長さ、区切り、GTIN/SSCC |
| GS1 Digital Link | `specqr::gs1` create/parse/validate/normalize | path/query、重複、escape、dot-only安全性 |
| high-level FNC1 literal percent | safe byte mode / unsafe明示拒否 | `%`をseparatorに変更しない |
| Structured Append split | text / bytes / manual segments | 2–16、容量・Unicode境界、version選択 |
| SA parity / merge / diagnostics | parity helpers / validated parts | UTF-8 XOR、欠落・重複・混在・parity不一致拒否 |
| matrix / SVG / PNG / data URLs / RGBA | portable free functions | geometry、CRC/Adler、実PNG decode |
| canvas adapter / scan warnings | application-owned `Canvas`, scan API | quiet zone、contrast、print/module幅 |
| CLI / examples / CMake export | `specqr-cli`, `specqr::specqr` | malformed CLI、fresh install consumer |
| C++17 / thread safety / allocation safety | const結果、checked bounds | ASan/UBSan、concurrency、malformed/fuzz |
| Windows MSVC / Linux GCC・Clang / macOS Clang | CMake / Actions | Debug・Release、consumer、CI完走 |
| 日本語中心docs / MIT / provenance | README・docs・LICENSE | 依存監査、公開情報、実測claim |

JavaScriptのDOM、Blob、Object URLはC++のAPIには持ち込みません。RGBAと抽象Canvasをアプリケーション側の描画APIへ接続できます。C ABI、decoder本体、Micro QR、rMQR、logo overlay、全GS1規格の認証、印刷物や実機cameraでの保証は対象外です。
