# GS1 / Digital Link

SpecQR-CPP provides a bounded, dependency-free GS1 toolkit. It validates 50 concrete AIs and preserves literal data through deterministic HTTP(S) Digital Link handling with strict payload validation; it does not claim full GS1 or IDNA/WHATWG URL conformance.

`<specqr/gs1.hpp>` の `specqr::gs1` 名前空間には、AI カタログ、チェックディジット、要素列、Digital Link の生成・解析・検証があります。外部ライブラリ、OS の URL API、ネットワークアクセスは使いません。

```cpp
#include <specqr/specqr.hpp>
#include <specqr/gs1.hpp>

const std::vector<specqr::gs1::Element> elements{
    {"01", "04912345678904"}, {"10", "100%"}, {"17", "251231"}
};
const auto raw = specqr::gs1::create_element_string(elements);
specqr::Options qr_options;
qr_options.gs1 = true;
const auto qr = specqr::QRCode::generate(raw, qr_options);

specqr::gs1::DigitalLinkOptions link_options;
link_options.base_url = "https://example.com";
const auto link = specqr::gs1::create_digital_link(elements, link_options);
// https://example.com/01/04912345678904/10/100%25?17=251231
const auto parsed = specqr::gs1::parse_digital_link(link);
```

## 対応 AI と検証範囲

`00`, `01`, `02`, `10`, `11`, `12`, `13`, `15`, `16`, `17`, `20`, `21`, `22`, `30`, `37`, `240`, `241`, `400`, `410`–`415`, `420`, `422`, `424`–`426`, `3100`–`3105`, `3200`–`3205`, `91`–`99` の 50 種です。`supported_ais()` は元の SpecQR と同じ順番でメタデータを返します。`ai_info()` は未知の AI に `nullptr` を返します。

- 値は印字可能 ASCII。AI ごとの桁数・文字種を検証し、先頭のゼロを維持します。
- GTIN / SSCC は modulo-10 チェックディジットを検証します。`calculate_*_check_digit()` は `char` を返します。
- 日付 AI は 6 桁の形だけを検証します。実在日付かどうかは判定しません。GLN は桁数だけを検証し、チェックディジットや登録状況を判定しません。
- 未対応 AI を許可するオプションはありません。`allow_unsupported_ai = true` はオプションエラーです。
- 可変長要素の直後に別の要素がある場合、`create_element_string()` は ASCII GS (`0x1D`) を挿入します。
- raw 解析では、末尾の可変長値が固定長 AI に見える文字列で終わると、区切り欠落として保守的に拒否します。このため、任意の可変長の末尾値について生成→raw 解析が必ず成功するとは限りません。これは上流の仕様です。要素の構造を保持できる場合は要素配列をそのまま渡してください。
- human-readable `(01)...(10)...` は `parse_human_readable()` で明示的に解析します。括弧は raw 値には使えません。

`validate_elements()` / `validate_element_string()` / `validate_digital_link()` は不正入力を `ok = false` と診断に変換します。メモリ確保失敗 (`std::bad_alloc`) は隠しません。診断の `offset` は UTF-8 **バイト位置**で、JavaScript / C# の UTF-16 位置とは異なります。メッセージ文言は C++ API に合わせており、互換性を判断するには `code` を使ってください。過大な AI / 値は診断に複製しません。

## Digital Link

primary AI は `00`, `01`, `414`。`01` の後ろには `10`, `21`, `22` を path qualifier として置けます。既定では適格な qualifier を入力順にパスへ、それ以外を AI 昇順のクエリへ配置します。`path_ais` が空の `std::vector<std::string>` なら、primary 以外をすべてクエリに置きます。同じ URI 内の AI 重複は拒否します。

`parse_digital_link()` は未知の非 AI クエリを `unknown_query` に順番を保って返します。`UnknownQueryPolicy::Reject` を指定すれば拒否できます。2–4 桁の未知の数値キーは未対応 AI として拒否します。クエリの `+` は空白です。エンコード済み NUL を含む未知のクエリ値は `std::string` の長さを維持して保持されます。C 文字列として取り扱う場合の切り詰めに注意してください。

`normalize_digital_link()` は **SpecQR 独自の決定的な整形**です。GS1 の canonical URI を保証しません。ホストとスキームの ASCII 大小文字、既定ポート、IPv6 表記、パスとクエリの配置、クエリ AI の順番を整えます。未知のクエリの相対順は保持します。通常の qualifier はパスへ移りますが、値が `.` / `..` の qualifier はクエリに残ります。

### データを失わないための安全規則

高水準の `Options::gs1` では、値に含まれるリテラル `%` が FNC1 のセパレータに変化しないモードを選択します。raw 要素列には `100%` をそのまま渡します。低水準の FNC1 + alphanumeric セグメントを自分で組む場合、QR の規則でリテラル `%` は `%%`、GS は単独の `%` です。この低水準規則と高水準 GS1 入力を混同しないでください。

Digital Link のパス値 `.` / `..` は URL 正規化で失われ得るため拒否します。`%2e` / `.%2E` など、1 回の percent decoding で dot segment になる入力も拒否します。GS1 primary より前の URL prefix と生成用 base URL の dot segment は通常の URL 規則で正規化します。primary より後ろの dot segment は正規化でデータを消さず拒否します。値そのものが文字列 `%2e` であれば生成時に `%252e` へエスケープされ、往復して保持されます。リテラル `.` / `..` は `path_ais = std::vector<std::string>{}` でクエリに置けます。

### 意図的な URL 境界

STL だけで挙動を固定するため、一般的なブラウザの WHATWG URL パーサの代用はしません。上流 JavaScript / C# と次の点が異なります。

| 入力 | C++ 版の規則 |
| --- | --- |
| 非 ASCII ホスト名 | 拒否。呼び出し元で IDNA ASCII / punycode に変換して渡す。IDNA 実装は含めない。 |
| ホスト名 | ASCII の percent encoding を復号し、小文字化。IDNA Unicode mapping は呼び出し元の責任。DNS の存在確認はしない。 |
| IPv4 | WHATWG の短縮形・16 進・8 進・単一整数表記も受け付け、4 個の 10 進数に正規化。積和とシフトはチェック済み。 |
| IPv6 | 角括弧が必須。IPv4 埋め込みを含む正しいアドレスを受け付け、最長ゼロ列を圧縮。ゾーン ID は対象外。 |
| URL の形 | HTTP(S) の slash 補完・前後 C0/空白除去・tab/CR/LF 除去・authority/path の backslash 変換は上流と同様。query 内の backslash は値として保持する。 |
| userinfo / credentials | percent encoding で保持。ライブラリはアクセスや認証を行わない。 |
| fragment | 非空 fragment は拒否。空の `#` は解析可能で、生成では上流同様に保持、normalization では除去。 |
| 生成用 base URL の query | 非空 query は拒否。空の `?` は許可し、生成時に置き換える。 |
| percent encoding / UTF-8 | パス・クエリ・未知のクエリを含め、壊れた escape、不完全列、overlong、surrogate、U+10FFFF 超過を拒否。U+FFFD に置換しない。 |
| dot segment | prefix / base URL は正規化。GS1 primary 以後は payload を失わないよう拒否。prefix に primary と同じ AI が現れて曖昧な場合も保守的に拒否。 |
| 複数の不正条件 | Unicode / escape / dot の安全検証が先なので、上流と最初の診断カテゴリが異なる場合がある。 |

## 資源上限と並行利用

1 回のテキスト入力 / 出力は 1,000,000 UTF-8 バイト以下、要素数は 16,384 以下です。パス分割とクエリ分割にも上限があり、分割前または分割中に確認します。チェックディジット計算は和を modulo-10 で保持し、入力長による整数 overflow を避けます。

公開ヘルパーに共有の変更可能な状態はありません。カタログは C++ の thread-safe な local static 初期化を使い、const 参照で返します。返される要素・診断・解析結果は所有権を持つ独立した値です。同じ結果オブジェクトを変更しながら別スレッドで読む場合の同期は呼び出し元が行ってください。

## 検証データ

上流 `SpecQR` commit `15ad15e5c770ea0e39072f8f88b2733018f02ffd` に基づく 1,411 個の GS1 fixture を、第三者ライブラリ不要の C++ アサーションとして収録しています。

- 1,369 件は元の結果を照合します。成功結果の値・AI メタデータ・解析配列、診断コードを比較し、英語メッセージ全文の一致は要求しません。
- 残る 42 件は下表の明示的な差分です。全 case ID・元入力・理由・変更前後の受理 / 診断は [GS1 差分 manifest](evidence/gs1-deltas.json) に公開しています。期待する新しいコードは `tests/tools/generate_gs1_fixtures.py` にも列挙しています。除外や黙ったスキップはありません。

| 差分 | 元は成功・C++ は拒否 | 元も拒否・診断のみ変更 | 合計 |
| --- | ---: | ---: | ---: |
| malformed percent encoding / UTF-8 を置換せず拒否 | 17 | 6 | 23 |
| GS1 payload の dot segment を削除せず拒否 | 5 | 1 | 6 |
| Unicode ホスト名の IDNA mapping は呼び出し元で実行 | 12 | 1 | 13 |
| 合計 | 34 | 8 | 42 |

- 追加で 2,000 回の決定的な要素列 / Digital Link / チェックディジット property 試験、1,000 組の任意バイト入力、8 スレッドで合計 800 回の並行処理、最大要素数、UTF-8 と percent encoding の境界、リテラル `%` と dot 値を検証します。

fixture 生成器は開発専用です。Python と、C# リポジトリにある `gs1-upstream.json` を明示的に与えた場合だけ使用します。通常の CMake ビルドとテストには Python も JSON パーサも必要ありません。
