# Security

不正な UTF-8、option、GS1、Structured Append、resource 境界を validate します。入力由来の失敗は通常 `specqr::Error` で返します。メモリ不足等の標準ライブラリ例外は別に扱ってください。

QR 生成は入力の信用性や送信先 URL の安全性を保証しません。GS1 は対応 AI catalog に限る validation です。SA parity は誤り検出用の XOR であり認証ではありません。画像 decoder や network アクセスは library に含まれません。

脆弱性は GitHub の private vulnerability reporting が利用可能ならそちらへ報告してください。利用できない場合は credential や実ユーザー payload を含めず、最小の合成入力と影響範囲を maintainer に知らせてください。修正は既存の安全契約と回帰テストを維持して検証します。
