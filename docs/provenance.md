# 実装の出典と依存

SpecQR-CPP は SpecQR の公開契約を基に新しく記述した C++17 実装です。QR encoder、Reed–Solomon、行列配置、mask、PNG framing、CRC-32、Adler-32、stored DEFLATE をこのリポジトリで実装しています。第三者 QR ライブラリの runtime source の転用や wrapper ではありません。

2026-10-03 に remote HEAD と実 source を確認した比較基準:

| Repository | Commit | 用途 |
| --- | --- | --- |
| [SpecQR JavaScript](https://github.com/SpecQR/SpecQR/tree/15ad15e5c770ea0e39072f8f88b2733018f02ffd) | `15ad15e5c770ea0e39072f8f88b2733018f02ffd` | API 契約・比較 oracle。package `3.0.0-rc.2` |
| [SpecQR Swift](https://github.com/SpecQR/SpecQR-Swift/tree/0ef9613fe8f1ecd687da76ce797b4ef896afa477) | `0ef9613fe8f1ecd687da76ce797b4ef896afa477` | 移植の安全性・portable rendering・SA |
| [SpecQR CSharp](https://github.com/SpecQR/SpecQR-CSharp/tree/057c4b3f25e52c4786a8f94c744ff884eedcecfa) | `057c4b3f25e52c4786a8f94c744ff884eedcecfa` | 移植契約・percent/dot safety correction |
| [Conformance Lab](https://github.com/SpecQR/SpecQR-Conformance-Lab/tree/72ad78c979327e3e261526ea3c0a164efa4ab390) | `72ad78c979327e3e261526ea3c0a164efa4ab390` | vectors・coverage と non-claim |

同じ SpecQR 所有の MIT プロジェクトの著作権表記を [LICENSE](../LICENSE) に保持しています。容量・ブロック構成は Model 2 の定数表です。有料仕様書の本文は含めません。

Kanji 対応表は基準 JS の WHATWG Shift_JIS decode / first-hit policy に沿う QR 対象範囲の静的 mapping です。OS の codepage、locale、iconv、ICU、Boost は利用しません。生成手順と table を source で確認できます。

ライブラリの実行時依存は C++17 標準ライブラリのみです。CMake は build/package にだけ使います。libpng、zlib、外部 codec、動的 download、network access、telemetry はありません。PNG は非圧縮 DEFLATE block のため画像ファイルは比較的大きくなります。

Nayuki や ZXing、Node/Python/Java 等の oracle・decoder は明示的な開発検証に限ります。`tools/` の test 依存を library に link したり、通常 consumer の build で download したりしません。基準 SpecQR の一致と、独立 encoder/decoder の一致は別々に集計します。

この公開対象は GitHub source repository です。パッケージ registry、stable tag/release、JS の release channel は変更していません。
