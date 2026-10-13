<a id="c-のapiと意図的な差分"></a>

# C++ の API と意図的な差分

最低言語規格は C++17 です。`std::string_view`、`std::optional`、値型を使い、C ABI は公開しません。API 互換性・ABI の stable 保証を表す release ではありません。例外を有効にした標準的な C++ 環境を対象とします。

文字列は strict UTF-8 です。過長形式、surrogate、U+10FFFF を超える値、途切れた sequence は `specqr::Error` で拒否します。文字列の NUL を終端扱いしないため、NUL を含める場合は長さを保持した `std::string` / `string_view` を渡してください。任意 binary は `std::vector<std::uint8_t>` を使います。ECI は QR 内の宣言であり、入力 bytes の charset 変換ではありません。

高水準 FNC1/GS1 で literal `%` を含む場合、Auto は Byte を選びます。Alphanumeric の強制は曖昧な意味変換を避けるため拒否します。低水準 `Segment::fnc1()` と alphanumeric の組合せは QR の escape 規則（`%` は separator、`%%` は literal percent）を使います。基準 JS の危険な文字列解釈をそのまま複製しません。

GS1 Digital Link は bounded catalog に対する deterministic parser です。完全な WHATWG URL / IDNA 実装ではありません。Unicode hostname は呼出側で ASCII/punycode 表現へ変換してください。IDNA/UTS46 を含めない URL hostname だけの制限で、QR 本文の Unicode・絵文字・Kanji は対象外です。GS1 値の path 位置の `.` / `..` は値消失を避けるため拒否し、query 位置で有効な場合は保存します。詳しい URL 制約は [GS1](gs1.md) を参照してください。

QRCode、Segment、画像、SA 結果は const accessor で所有 data を公開します。コピーした結果は独立した値です。複数 thread が同じ結果を読み取れます。アプリが同じ C++ オブジェクトへ同時に代入・破棄したり、同じ output stream / Canvas を同期せず書き換えたりする操作は呼出側で同期してください。

JavaScript の canvas/DOM、Blob、Object URL の代わりに portable RGBA、stream、application-owned Canvas を提供します。PNG の圧縮 byte 列は JS/C# と異なり、PNG の画素と QR の論理内容で比較します。QR Code Model 2 以外、embedded/no-exception 環境、WASM、特定標準ライブラリの ABI 互換、すべての scanner や printer での結果は主張しません。
