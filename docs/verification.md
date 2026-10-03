# 検証と再現手順

C++の実行結果だけを記録します。既存SpecQR各版のCI成功はC++の成功件数に含めません。範囲は [feature parity](feature-parity.md)、出典は [provenance](provenance.md) にあります。

## 通常のbuild / test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
(cd build && ctest -C Release --output-on-failure)
python3 tools/verify-cli.py --cli build/specqr-cli
python3 tools/verify-consumer.py --config Release
python3 tools/verify-consumer.py --config Release --shared
python3 tools/audit-public.py
```

WindowsではCLIを `build/Release/specqr-cli.exe` と指定します。通常のC++テストにはPythonも外部packageも不要です。consumerテストは一時prefixへinstall後、prefixを移動し元build directoryを削除してから、別projectの `find_package` だけでbuild・実行します。

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSPECQR_SANITIZER=address-undefined
cmake --build build-asan --parallel
(cd build-asan && ctest --output-on-failure)
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DSPECQR_SANITIZER=thread
cmake --build build-tsan --parallel
(cd build-tsan && ctest --output-on-failure)
```

Sanitizerは対応するGCC/Clangを使います。macOSではAddressSanitizerのleak検出を無効にして実行し、Linux CIでは有効にします。ThreadSanitizerはmacOS Clangで実行します。無効化されたscopeを成功数へ含めません。

## 独立比較とdecode

この節だけがNode/Python/Javaと外部test依存を使います。Nayuki1.8.0、ZXing-C++3.1.1、ZXing Java3.5.4はversion/hashを固定し、libraryやconsumerへlinkしません。

```sh
git clone https://github.com/SpecQR/SpecQR.git baseline
git -C baseline checkout 15ad15e5c770ea0e39072f8f88b2733018f02ffd
npm ci --prefix tools --ignore-scripts
python3 tools/verify-conformance.py --adapter build/specqr-conformance --baseline baseline
python3 tools/verify-decode.py --adapter build/specqr-conformance --decoder cpp --dependency-dir .tools/zxing-cpp
python3 tools/verify-decode.py --adapter build/specqr-conformance --decoder java --dependency-dir .tools/zxing-java
```

JavaはJDK17以上を用意してください。decodeスクリプトの初回はtest依存を指定した隔離directoryに取得します。既に配置済みなら `--no-install` が使えます。ネットワーク接続はこの明示的な開発手順に限られます。

7300個のexact matrix比較は、公開API2868、raw codeword4320、Structured Append112（22set）です。公開ケースのうち2240は独立Nayukiとも比較し、その中で40version×4ECC×8maskの全1280組合せを実行します。matrixだけでなくdata/ECC/interleave済codewordsを比較します。容量640、境界estimate1920、GF積65536、RS次数255、不正入力48も別に照合します。

ZXing-C++のstrict検証は706matrixと750個の実PNG（default scale8）、97805184画素を確認します。135SAheader、152種類のFNC1-second indicatorのmanual/options両経路304symbol、5つのSAset/44symbolを含み、両decode経路でVersion 1–40を実行します。PNGの全RGBA画素、quiet zone、PNG CRC、zlib decodeを確認してからscannerへ渡します。matrixからのtest rasterと生成された実PNGは別に数えます。

最初の公開コミット `a9a73c260eabae90a0a03b33f1dd634c3ea3c7dd` の [独立検証CI](https://github.com/SpecQR/SpecQR-CPP/actions/runs/37123663410/job/111204575993) では、上記exact比較とZXing-C++検証が成功しました。ZXing Javaは446matrix・446個のscale3実PNG、10008270画素を検証し、32symbolで各3個のdata codeword誤りを訂正しました。両decode経路でVersion 1–40を実行し、135SAheaderと5set/44symbolから10回の独立再結合を検証しています。[Javaの実測report](evidence/zxing-java-ci.json) はcommit・run/job URL・adapter hashを含みます。raw decoder出力は926件で、strict成功924件と別枠のscale8検出失敗2件を独立に照合しました。

### ECIのdecoder API差分

初回のZXing-C++検証では35個のECIケース×2経路の計70件が、`symbology_identifier`へ `]Q2` を期待したため失敗しました。デコードされた全bytesは一致し、検出失敗ではありません。この最初の失敗を別reportに保存します。

同じECI3/20/26/170のpayloadを独立Nayukiでも生成し、C++と同じmatrixになることを確認しました。ZXing-C++3.1.1は両方のmatrixで `symbology_identifier == ]Q1` を返す一方、`TextMode.ECI` の文字列先頭に `]Q2` とUTF-8へ変換したECI26の表現を返します。修正したtestはbytes・base property・ECI textをすべて検査し、70件の追加ECI確認と4つの独立controlを実行します。JavaのECI modifierは直接 `]Q2` を検査します。scaleや検出hintは変更していません。

### Javaのdefault scale8診断

Javaのstrict実PNG検出はscale3で、`PURE_BARCODE`やmatrixへのfallbackを使いません。別にdefault scale8の特定画像と同じ画素の独立PNGを生成し、両方の結果を保存します。そこでの検出拒否はstrict成功へ含めません。PNGファイルとJSONはCI artifactに保持し、異なる結果になった場合はgateを失敗させます。同一画素のcontrolにも現れるdecoderの検出制限と、encoderの誤りを区別します。 実際の上記CIでは `SPECQR / 12345 %`、Version 4/L/mask 0、scale8のC++ PNGと、全画素一致を確認した独立grayscale controlがともに `NotFoundException` となりました（検出失敗2・成功0）。同じC++ PNGをZXing-C++は正しいpayloadとして読み取りました。このJavaの2失敗は446件のstrict PNG成功に含めていません。

[実行済みJava CI証跡](evidence/zxing-java-ci.json) では446matrix、446実PNG、32個の破損symbol（各3codewordの訂正）が成功しました。全40version、135SAheader、5set/44symbol、両経路10回の独立結合、10008270画素を検証しています。default scale8の本体PNGと独立controlは両方 `NotFoundException`（失敗2、成功0）であり、924個のstrict成功に含めていません。同じdefault PNGはZXing-C++で完全なpayloadが読めました。初回CIでWindows CLIのLF/CRLF差分も検出し、stdoutをbinary modeに修正しています。libraryの16source/headerのhashは変えていません。

## 実行結果と限界

ローカル実行・source hash・独立reviewの証跡は `docs/evidence/` と [integrated-review](integrated-review.md) に記録します。CIはWindows MSVC、Linux GCC/Clang、macOS ClangそれぞれDebug/Release、ASan/UBSan、TSan、独立比較の11jobを実行します。各コミットのterminal結果と実行ログは [GitHub Actions](https://github.com/SpecQR/SpecQR-CPP/actions/workflows/ci.yml) に残ります。ローカル成功はCI未実行のOS/runtimeの成功を意味しません。

有限の合成入力に対する検証です。ISO認証、すべてのscanner、物理印刷、camera、すべてのC++標準ライブラリ/OS、ABI安定性、完全なGS1/WHATWG/IDNAの準拠を保証しません。失敗・未実行・意図的差分はpassと混同しません。
