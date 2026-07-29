# compare — CCSDS BPE C/Rust 互換性検証リポジトリ

[English](README.md)

`original/`（U. Nebraska 製 CCSDS 122.0 Bit Plane Encoder の C 参照実装、Aaron Kiely 氏によるバグ修正版）と、`bpe-rs/`（その Rust 移植版、[isas-yamamoto/bpe-rs](https://github.com/isas-yamamoto/bpe-rs)）が互換であることを、広範な試験・関数レベル全数検証・コードカバレッジ計測によって継続的に確認しているリポジトリ。**これは全入力値について形式的に証明したものではない** — テストされた範囲についての高い確度の経験的確認である。保証の範囲・既知の限界・今後さらに確度を上げる手段は下記「どこまで互換で、どこから差異があるか」と [COMPATIBILITY_REPORT.ja.md](COMPATIBILITY_REPORT.ja.md) §5「総合評価」を参照。

## どこまで互換で、どこから差異があるか

| 分類 | 範囲 | 状態 |
|---|---|---|
| **完全互換を確認済み** | 整数・float 両DWTでの全パイプライン166/166ケース／Rice・2の補数変換・DPCM(DC・AC)・パターンマッピング・`AdjustOutPut`・`CodingOptions`・`ACDepthEncoder`/`Decoder`・`DCEntropyEncoder`/`Decoder`の関数レベル全数検証9/9／ランダム化fuzz試験、両DWT込み（CIで継続実行）／`readme_kielymods`記載の外部バグ指摘8項目／コア15ファイル全ての実効カバレッジ100.00%（行・分岐とも） | 不一致 **0件** |
| **既知の差異** | なし | — |
| **検証対象外** | ファイルI/O失敗等のエラーパス、CLIから構造的に到達不能な設定項目（確定デッドコード356行） | 意図的にスコープ外 |

詳細な根拠・試験内容・カバレッジ評価は **[COMPATIBILITY_REPORT.ja.md](COMPATIBILITY_REPORT.ja.md)** を参照。過去のフル検証ラウンドとの比較・推移は **[verify/results/history.ja.md](verify/results/history.ja.md)** に記録している。

## 構成

```
original/source/   C 参照実装（無改変。BPE_TRACE ビルド用のフックのみ追加。第三者コード、NOTICE参照）
bpe-rs/             Rust 移植版（git submodule）
verify/             検証ハーネス一式
.github/workflows/  CI（push/PR ごとに自動検証）
```

各試験が具体的に何を・どういう観点で検証しているか（166ケースの内訳、関数レベル全数検証の対象、カバレッジ評価の方法まで）は **[COMPATIBILITY_REPORT.ja.md](COMPATIBILITY_REPORT.ja.md)** の「§1 検証観点」「§2 試験内容と結果」に一つずつ記載している。ここ（README）では概要のみ扱う。

## ローカルでの実行

```sh
git submodule update --init --recursive   # 初回のみ
verify/run_compat.sh
verify/run_unit_vectors.py
verify/fuzz_compat.py --iterations 500    # ランダム化fuzz試験（任意、--seedで再現可能）
```

## CI

`.github/workflows/compat.yml` が push・PR ごとに `verify/run_compat.sh`（全パイプラインのバイト一致）・`verify/run_unit_vectors.py`（関数レベルの突き合わせ）・`verify/fuzz_compat.py --iterations 50 --seed 0`（軽量fuzzスモークテスト）を自動実行する。2^15 ブロック超の回帰ケースと5000ケースの深掘りfuzz（`deep-fuzz`ジョブ、乱数シード）は週次スケジュール/手動実行のみ（低頻度・低リスクのため）。

`.github/workflows/bpe-rs-poll.yml` は毎日 bpe-rs の `main` を確認し、submodule の pin より進んでいれば同様の検証を行い、通れば submodule bump PR を自動作成する（失敗時はPRを作らずワークフローが失敗するのみ）。詳細は [verify/results/history.ja.md](verify/results/history.ja.md)。

## これまでに見つかった実際の不一致

float DWT の丸め誤差、DPCM DC マッピングの整数幅バグ2件を発見・修正済み。詳細な原因分析は [COMPATIBILITY_REPORT.ja.md](COMPATIBILITY_REPORT.ja.md) の「発見した不一致とその対応」を参照。

## ライセンス

`original/` 以下は第三者のライセンス済みコードであり、本リポジトリの他の部分（`verify/`、`COMPATIBILITY_REPORT.md` 等の独自成果物）とはライセンスが異なる。内訳・出典・各コンポーネントの権利関係は **[NOTICE](NOTICE)** を参照。
