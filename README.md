# compare — CCSDS BPE C/Rust 互換性実証リポジトリ

`original/`（U. Nebraska 製 CCSDS 122.0 Bit Plane Encoder の C 参照実装、Aaron Kiely 氏によるバグ修正版）と、`bpe-rs/`（その Rust 移植版、[isas-yamamoto/bpe-rs](https://github.com/isas-yamamoto/bpe-rs)）が**完全に互換**であることを実証・継続検証するためのリポジトリ。

## 構成

```
original/source/   C 参照実装（無改変。BPE_TRACE ビルド用のフックのみ追加）
bpe-rs/             Rust 移植版（git submodule）
verify/             検証ハーネス一式
.github/workflows/  CI（push/PR ごとに自動検証）
```

## 互換性の検証内容

### 1. 全パイプラインのバイト一致（`verify/run_compat.sh`）

既知のエッジケース（`original/readme_kielymods.rtf` に記録された Kiely 氏のバグ修正箇所）を含むテストマトリクスで、両実装のエンコード出力・相互デコード出力がバイト単位で一致することを確認する。

```sh
verify/run_compat.sh              # 標準マトリクス
verify/run_compat.sh --include-slow   # + 2^15 ブロック/セグメント超の回帰ケース（低頻度用）
```

カバーする範囲: 通常画像、全ゼロ画像、最小サイズ(17×17)、セグメント境界（総ブロック数 15/16/17/31/32/33、末尾1ブロックセグメント）、16bit画素×エンディアン、符号付き画素、integer/float DWT、複数レート。

### 2. 関数レベルの突き合わせ（`verify/run_unit_vectors.py`）

Rice 符号化・2の補数変換・DPCM DC マッピング・パターンマッピングについて、C参照実装のオブジェクトコードに直接リンクしたジェネレータ（`verify/c_unit_tests/`）で全表現可能値（または境界を突いた入力列）を網羅したベクタを生成し、bpe-rs 側の対応関数がバイト単位で同じ結果を出すことを確認する。個々の画像がたまたま踏む値だけでなく、表現可能な全入力域・既知の境界ケースをカバーする。Rice は暗号化バイト列の一致だけでなく、C側が生成したバイト列を Rust 側でデコードして値が復元できるかも確認する（クロスデコード）。

### 3. ステージ境界のトレース比較（`verify/compare_traces.py`）

将来なんらかの不一致が出た際に「どの段階で分岐したか」を特定するため、DWT変換直後・ブロック文字列構築直後の中間値を両実装からダンプして diff する。既定ビルドの動作には影響しない（環境変数 `BPE_TRACE_DIR` 未設定時は完全に無効）。

## ローカルでの実行

```sh
git submodule update --init --recursive   # 初回のみ
verify/run_compat.sh
verify/run_unit_vectors.py
```

## CI

`.github/workflows/compat.yml` が push・PR ごとに上記 1・2 を自動実行する。2^15 ブロック超の回帰ケースは週次スケジュール/手動実行のみ（低頻度・低リスクのため）。

## これまでに見つかった実際の不一致

- **float DWT の丸め誤差**（bpe-rs コミット `7f461c3`、ローカルのみ・未push）: C参照実装の `(int)(v + 0.5)` は `0.5` が `double` リテラルであるため加算が倍精度で行われるが、Rust 側は単精度で加算していた。値の小数部が 0.5 の1ULP以内に来る稀なケースでのみ丸め結果が分岐する。`verify/run_compat.sh` の `-t 0` ケースで発見・修正済み。

- **DPCM DC マッピングの整数幅バグ2件**（bpe-rs コミット `8b9cb92`、ローカルのみ・未push）: DC 深度 N=16 のときのみ発現する2つの不一致を `verify/run_unit_vectors.py` の DPCM ベクタで発見。(1) `-(short)(...)`: C は負値化の前に16bit `short` へ縮小するため、中間値がちょうど 32768 になる場合（N=16でブロックの生値の最上位ビットが立っている時）に `short` キャストが -32768 に折り返り、符号が反転した +32768 になる。(2) `theta = min(ShiftedDC - X_Min, X_Max - ShiftedDC)`: `ShiftedDC`/`X_Max` は C では符号なし `DWORD32` なので、(1)によって `ShiftedDC` が `[X_Min,X_Max]` の範囲外に出ると引き算が符号なしで折り返り、負値の代わりに巨大な値になる。C の `min()` はその巨大値で比較してしまう。どちらも `verify/run_compat.sh` の合成テスト画像では踏まなかった境界値で、関数レベルベクタで初めて発見された。

## 更なる詳細

アルゴリズム自体の解説（DWT・DC/AC符号化・Rice符号化など）は bpe-rs 側のドキュメントを参照:

- [bpe-rs/docs/algorithm_ja.md](bpe-rs/docs/algorithm_ja.md) — 全体地図
- [bpe-rs/docs/verify_ja.md](bpe-rs/docs/verify_ja.md) — 検証手順の考え方
- [bpe-rs/docs/code_reading_ja.md](bpe-rs/docs/code_reading_ja.md) — 実装対応表
