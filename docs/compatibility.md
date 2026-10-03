# C++ のAPIと意図的な差分

最低言語規格はC++17です。`std::string_view`、`std::optional`、値型を使い、C ABIは公開しません。API互換性・ABIのstable保証を表すreleaseではありません。例外を有効にした標準的なC++環境を対象とします。

文字列はstrict UTF-8です。過長形式、surrogate、U+10FFFFを超える値、途切れたsequenceは `specqr::Error` で拒否します。文字列のNULを終端扱いしないため、NULを含める場合は長さを保持した `std::string` / `string_view` を渡してください。任意binaryは `std::vector<std::uint8_t>` を使います。ECIはQR内の宣言であり、入力bytesのcharset変換ではありません。

高水準FNC1/GS1でliteral `%`を含む場合、AutoはByteを選びます。Alphanumericの強制は曖昧な意味変換を避けるため拒否します。低水準 `Segment::fnc1()` とalphanumericの組合せはQRのescape規則（`%`はseparator、`%%`はliteral percent）を使います。基準JSの危険な文字列解釈をそのまま複製しません。

GS1 Digital Linkはbounded catalogに対するdeterministic parserです。完全なWHATWG URL / IDNA実装ではありません。Unicode hostnameは呼出側でASCII/punycode表現へ変換してください。IDNA/UTS46を含めないURL hostnameだけの制限で、QR本文のUnicode・絵文字・Kanjiは対象外です。GS1値のpath位置の `.` / `..` は値消失を避けるため拒否し、query位置で有効な場合は保存します。詳しいURL制約は [GS1](gs1.md) を参照してください。

QRCode、Segment、画像、SA結果はconst accessorで所有dataを公開します。コピーした結果は独立した値です。複数threadが同じ結果を読み取れます。アプリが同じC++オブジェクトへ同時に代入・破棄したり、同じoutput stream / Canvasを同期せず書き換えたりする操作は呼出側で同期してください。

JavaScriptのcanvas/DOM、Blob、Object URLの代わりにportable RGBA、stream、application-owned Canvasを提供します。PNGの圧縮byte列はJS/C#と異なり、PNGの画素とQRの論理内容で比較します。QR Model 2以外、embedded/no-exception環境、WASM、特定標準ライブラリのABI互換、すべてのscannerやprinterでの結果は主張しません。
