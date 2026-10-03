# 実装の出典と依存

SpecQR-CPP は SpecQR の公開契約を基に新しく記述した C++17 実装です。QR encoder、Reed–Solomon、行列配置、mask、PNG framing、CRC-32、Adler-32、stored DEFLATE をこのリポジトリで実装しています。第三者QRライブラリのruntime sourceの転用やwrapperではありません。

2026-10-03 に remote HEAD と実sourceを確認した比較基準:

| Repository | Commit | 用途 |
| --- | --- | --- |
| [SpecQR JavaScript](https://github.com/SpecQR/SpecQR/tree/15ad15e5c770ea0e39072f8f88b2733018f02ffd) | `15ad15e5c770ea0e39072f8f88b2733018f02ffd` | API契約・比較oracle。package `3.0.0-rc.2` |
| [SpecQR Swift](https://github.com/SpecQR/SpecQR-Swift/tree/0ef9613fe8f1ecd687da76ce797b4ef896afa477) | `0ef9613fe8f1ecd687da76ce797b4ef896afa477` | 移植の安全性・portable rendering・SA |
| [SpecQR CSharp](https://github.com/SpecQR/SpecQR-CSharp/tree/057c4b3f25e52c4786a8f94c744ff884eedcecfa) | `057c4b3f25e52c4786a8f94c744ff884eedcecfa` | 移植契約・percent/dot safety correction |
| [Conformance Lab](https://github.com/SpecQR/SpecQR-Conformance-Lab/tree/72ad78c979327e3e261526ea3c0a164efa4ab390) | `72ad78c979327e3e261526ea3c0a164efa4ab390` | vectors・coverageとnon-claim |

同じSpecQR所有のMITプロジェクトの著作権表記を [LICENSE](../LICENSE) に保持しています。容量・ブロック構成はModel 2の定数表です。有料仕様書の本文は含めません。

Kanji対応表は基準JSのWHATWG Shift_JIS decode / first-hit policyに沿うQR対象範囲の静的mappingです。OSのcodepage、locale、iconv、ICU、Boostは利用しません。生成手順とtableをsourceで確認できます。

ライブラリの実行時依存はC++17標準ライブラリのみです。CMakeはbuild/packageにだけ使います。libpng、zlib、外部codec、動的download、network access、telemetryはありません。PNGは非圧縮DEFLATE blockのため画像ファイルは比較的大きくなります。

NayukiやZXing、Node/Python/Java等のoracle・decoderは明示的な開発検証に限ります。`tools/` のtest依存をlibraryにlinkしたり、通常consumerのbuildでdownloadしたりしません。基準SpecQRの一致と、独立encoder/decoderの一致は別々に集計します。

この公開対象はGitHub source repositoryです。パッケージregistry、stable tag/release、JSのrelease channelは変更していません。
