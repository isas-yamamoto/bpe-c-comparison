# compare — CCSDS BPE C/Rust 互換性実証リポジトリ

`original/`（U. Nebraska 製 CCSDS 122.0 Bit Plane Encoder の C 参照実装、Aaron Kiely 氏によるバグ修正版）と、`bpe-rs/`（その Rust 移植版、[isas-yamamoto/bpe-rs](https://github.com/isas-yamamoto/bpe-rs)）が**完全に互換**であることを実証・継続検証するためのリポジトリ。

## どこまで互換で、どこから差異があるか

| 分類 | 範囲 | 状態 |
|---|---|---|
| **完全互換を確認済み** | 整数DWT（`-t 1`、実運用の既定）での全パイプライン166/166ケース／Rice・2の補数変換・DPCM(DC・AC)・パターンマッピング・`AdjustOutPut`の関数レベル全数検証6/6／`readme_kielymods`記載の外部バグ指摘8項目／コア15ファイル全ての実効カバレッジ100.00%（行・分岐とも） | 不一致 **0件** |
| **既知の差異（残存・修正不能）** | float DWT（`-t 0`）でのデコード: コンパイラ（gcc/rustc）間の浮動小数点丸め実装差に起因する1ULPレベルの残存差（3画像・28レート値中5件で発現）。整数DWTは無関係 | バグではなくコンパイラ差と判断、実用上解消不能 |
| **検証対象外** | ファイルI/O失敗等のエラーパス、CLIから構造的に到達不能な設定項目（確定デッドコード356行） | 意図的にスコープ外 |

詳細な根拠・試験内容・カバレッジ評価は **[COMPATIBILITY_REPORT.md](COMPATIBILITY_REPORT.md)** を参照。過去のフル検証ラウンドとの比較・推移は **[verify/results/history.md](verify/results/history.md)** に記録している。

## 構成

```
original/source/   C 参照実装（無改変。BPE_TRACE ビルド用のフックのみ追加。第三者コード、NOTICE参照）
bpe-rs/             Rust 移植版（git submodule）
verify/             検証ハーネス一式
.github/workflows/  CI（push/PR ごとに自動検証）
```

各試験が具体的に何を・どういう観点で検証しているか（166ケースの内訳、関数レベル全数検証の対象、カバレッジ評価の方法まで）は **[COMPATIBILITY_REPORT.md](COMPATIBILITY_REPORT.md)** の「§1 検証観点」「§2 試験内容と結果」に一つずつ記載している。ここ（README）では概要のみ扱う。

## ローカルでの実行

```sh
git submodule update --init --recursive   # 初回のみ
verify/run_compat.sh
verify/run_unit_vectors.py
```

## CI

`.github/workflows/compat.yml` が push・PR ごとに `verify/run_compat.sh`（全パイプラインのバイト一致）と `verify/run_unit_vectors.py`（関数レベルの突き合わせ）を自動実行する。2^15 ブロック超の回帰ケースは週次スケジュール/手動実行のみ（低頻度・低リスクのため）。

`.github/workflows/bpe-rs-poll.yml` は毎日 bpe-rs の `main` を確認し、submodule の pin より進んでいれば同様の検証を行い、通れば submodule bump PR を自動作成する（失敗時はPRを作らずワークフローが失敗するのみ）。詳細は [verify/results/history.md](verify/results/history.md)。

## これまでに見つかった実際の不一致

float DWT の丸め誤差、DPCM DC マッピングの整数幅バグ2件を発見・修正済み。詳細な原因分析は [COMPATIBILITY_REPORT.md](COMPATIBILITY_REPORT.md) の「発見した不一致とその対応」を参照。

## ライセンス

`original/` 以下は第三者のライセンス済みコードであり、本リポジトリの他の部分（`verify/`、`COMPATIBILITY_REPORT.md` 等の独自成果物）とはライセンスが異なる。内訳・出典・各コンポーネントの権利関係は **[NOTICE](NOTICE)** を参照。
