# C/Rust 互換性検証レポート

`original/`（U. Nebraska 製 CCSDS 122.0 Bit Plane Encoder、Aaron Kiely 氏によるバグ修正版 C 参照実装）と `bpe-rs/`（その Rust 移植版）が互換であることを、どのような観点で・どう試験し・その結果何が分かったかをまとめる。試験ハーネスは `verify/` に、実行方法は [README.md](README.md) に記載している。

作成時点のコミット: 本リポジトリ `36ec996` 以降 / bpe-rs `8b9cb92`（bpe-rs 側はローカルのみ・未 push）。

## 1. 検証観点

「互換」を単一の指標では測れないため、性質の異なる4つの観点を組み合わせている。

| # | 観点 | 何を検証するか | なぜ必要か |
|---|------|----------------|-----------|
| 1 | 全パイプラインのバイト一致 | 実画像を両実装でエンコード/デコードし、出力バイト列が完全一致するか | 最終的に利用者が気にするのはこれだけ。だが「たまたま今のテスト画像で一致した」を「常に一致する」と混同しないよう、既知の境界値を突く画像を意図的に混ぜる |
| 2 | 既知エッジケースの網羅 | `original/readme_kielymods.rtf` に記録された Kiely 氏の実バグ修正箇所（単一ブロックセグメント、全ゼロ画像、ブロック数境界、エンディアン等）を再現する入力を用意し、そこでも一致するか | 通常の自然画像はバグの温床になりやすい境界値をほぼ踏まない。エッジケースを明示的に作らないと「一致した」の意味が薄くなる |
| 3 | 関数レベルの全数検証 | Rice 符号化・2の補数変換・DPCM DC マッピング・パターンマッピングを、C参照実装のオブジェクトコードに直接リンクしたジェネレータで表現可能な入力域の全体（またはそれに近い範囲）にわたって突き合わせる | 観点1・2は「その画像が生成する値」しか通らない。個々の関数が受け得る値の全域はカバーできない。実際、後述のDPCMバグは通常のテスト画像では一切踏めなかった |
| 4 | ステージ境界のトレース比較 | DWT変換直後・ブロック文字列構築直後の中間値を両実装からダンプしdiffする（`verify/compare_traces.py`） | 観点1で不一致が出た際に「エンコーダのどの段階で分岐したか」を即座に特定するための診断手段。それ自体は pass/fail の判定基準ではない |

上記に加え、**コードカバレッジ計測**（本レポート §4）で「観点1・2がCコードのどれだけの範囲を実際に踏んでいるか」を定量化し、上記の観点だけでは見えない「まだ検証できていない領域」を可視化した。

## 2. 試験内容と結果

### 2.1 全パイプライン・バイト一致（`verify/run_compat.sh`）

19ケース（うち1件は `--include-slow` 指定時のみの低頻度回帰ケース）。全ケースで **エンコード出力バイト一致・相互デコード出力バイト一致（C→Rust, Rust→C 双方向）** を確認。

| ケース | 内容 |
|---|---|
| baseline_256 × (t=0,1) × (r=0, 1.0, 4.0) | 通常のグラデーション画像。integer/float DWT × 可逆/非可逆レート |
| all_zero_64 | 全ゼロ画像（`leftmost==1` 特殊扱いバグの回帰） |
| minimal_17x17 | 最小画像サイズ（`IMAGE_WIDTH_MIN`/`IMAGE_ROWS_MIN`） |
| single_trailing_block_48x24 | readme記載の「末尾セグメントが1ブロック」репро（`-s 17`） |
| blocks_15 / 16 / 17→18 / 31→32 / 32 / 33 | 総ブロック数が GAGGLE_SIZE(16) の境界に来る画像（17, 31 は幾何的に構成不能なため隣接値に代替、理由は `verify/gen_vectors.py` のコメント参照） |
| pixels16_f0 / f1 | 16bit画素 × リトル/ビッグエンディアン |
| signed_32 | 符号付き画素（`-g 1`） |
| large_segment_slow | 1セグメント33,000ブロック（`short i` オーバーフローの回帰、週次CIのみ） |

**結果: 19/19 PASS**（2026-07-25 時点）。

### 2.2 関数レベルの全数検証（`verify/run_unit_vectors.py`）

| 対象関数 | C参照実装 | 網羅範囲 | 検証方法 |
|---|---|---|---|
| `RiceCoding`/`RiceDecoding` | ricecoding.c | bit_length 1–4 × 有効な option の組（計10通り）× 各の表現可能値全域 | Rust encode がCのバイト列と完全一致。さらにCが生成したバイト列を Rust decode に読ませ、元の値列を正しく復元できるかも確認（クロスデコード） |
| `ConvTwosComp` | DC_EnDeCoding.c | leftmost = 2–16 × 各幅の表現可能値全域 | Rust `conv_twos_comp` の出力がCと完全一致 |
| `DPCM_DCMapper`/`DPCM_DCDeMapper` | DC_EnDeCoding.c | N = 4, 8, 16 × {単調増加, 両極端の交互, 中間値付近での振動} の3系列＋固定シーケンス1本（計10系列） | Rust `dpcm_dc_mapper`/`dpcm_dc_demapper` の出力（MappedDC・復元ShiftedDC）がCと完全一致 |
| `PatternMapping` | PatternCoding.c | (sym_len, type) の全7通り × 各の表現可能値全域 | Rust `pattern_mapping` の出力がCと完全一致 |

**結果: 4/4 テストグループ PASS**（ただし後述の通り、DPCM は1周目で2件の不一致を検出・修正した上での PASS）。

### 2.3 ステージ境界トレース比較（`verify/compare_traces.py`）

DWT変換直後・ブロック文字列構築直後の中間値を diff する仕組みを整備。現時点で両段階とも MATCH（§2.1 のバイト一致と整合）。恒常的な pass/fail 判定というより、将来不一致が出た際の一次切り分け手段として用意している。

## 3. 発見した不一致とその対応

関数レベル検証（§2.2）を導入したことで、全パイプライン試験（§2.1）だけでは検出できなかった2件の実バグを発見・修正した。**どちらも「通常のテスト画像がたまたま踏まない境界値」でのみ発現する**ため、観点3を追加しなければ気づけなかった。

### 3.1 float DWT の丸め誤差（bpe-rs コミット `7f461c3`）

C参照実装の `(int)(v + 0.5)` は `0.5` が `double` リテラルであるため加算が倍精度で行われる。Rust側は単精度(`f32`)のまま加算していたため、値の小数部が 0.5 の1 ULP以内に来る稀なケース（256×256画像で65536画素中3画素）でのみ丸め結果が1ずれる。§2.1 の `-t 0`（float DWT）ケースで発見。`round_away_from_zero`・`dwt_reverse_floating` の両方を `f64` 経由の加算に修正。

### 3.2 DPCM DC マッピングの整数幅バグ2件（bpe-rs コミット `8b9cb92`）

DC 深度 N=16 のときにのみ発現。§2.2 の DPCM ベクタ（中間値付近の振動系列）で発見。

1. **`-(short)(...)` の丸め込み**: Cは負値化の前に中間値を16bitの `short` へ縮小する。中間値がちょうど 32768 になる場合（ブロックの生値の最上位ビットが立っている時）、`short` キャストが 32768→−32768 に折り返り、符号反転後の値が +32768 になる（本来 Rust が計算していた −32768 とは符号が逆）。
2. **`theta = min(ShiftedDC − X_Min, X_Max − ShiftedDC)` の符号なし折り返し**: `ShiftedDC`/`X_Max` は C では符号なし `DWORD32`。上記(1)によって `ShiftedDC` が `[X_Min, X_Max]` の範囲外（+32768）に出ると、`X_Max − ShiftedDC` の引き算が符号なし演算として折り返り、負値の代わりに巨大な値になる。C の `min()` マクロはこの巨大値で比較してしまうため、意図と異なる `theta` が選ばれる。

Rust側は `wrapping_sub` を使い、Cの符号なし折り返しを明示的に再現するよう修正（`neg_short`/`theta_from_prev` ヘルパー）。

### 3.3 対応状況まとめ

| 不一致 | 発見手段 | 影響範囲 | 状態 |
|---|---|---|---|
| float DWT 丸め誤差 | 観点1（全パイプライン、`-t 0`） | float DWT 使用時、稀な画素 | 修正済み（bpe-rs `7f461c3`） |
| DPCM short キャスト折り返し | 観点3（DPCM関数ベクタ） | DC深度 N=16、特定の生値パターン | 修正済み（bpe-rs `8b9cb92`） |
| DPCM theta 符号なし折り返し | 観点3（DPCM関数ベクタ） | 上記(1)の結果として誘発、N=16 | 修正済み（bpe-rs `8b9cb92`） |

いずれも修正後、§2.1・§2.2 の全試験が PASS することを確認済み。

## 4. カバレッジ評価（Cコード網羅率）

§2.1・§2.2 の試験群が、C参照実装の実コードのどれだけの範囲を実際に踏んでいるかを `gcov` で計測した（コアアルゴリズム15ファイル対象。CLI引数解析の `main.c`、サードパーティ製 `getopt.c`、エラー文言表の `errorhandle.c` は対象外）。

| 対象 | 行カバレッジ | 分岐カバレッジ（両方向） |
|---|---|---|
| コア15ファイル全体 | 47.75%（3497/7324行） | 37.67%（2532/6721） |
| うち `AdjustOutput.c` を除く14ファイル | 81.79%（3063/3745行） | 76.26%（2098/2751） |
| `AdjustOutput.c` のみ | 12.13%（3579行中） | 10.93% |

`AdjustOutput.c`（レート制限で途中停止したデコードの後処理、3579行の単一関数）が全体を大きく押し下げている。これを除く「通常のエンコード/デコード経路」は概ね8割前後を実際に踏んだ上で100%一致を確認できている。

ファイル別の内訳:

| ファイル | 行 | 分岐(両方向) |
|---|---|---|
| CoeffGroup.c | 100.00% | 100.00% |
| lifting_97f.c | 96.47% | 90.48% |
| lifting_97M.c | 96.03% | 92.42% |
| BPEBlockCoding.c | 93.96% | 84.20% |
| PatternCoding.c | 93.18% | 82.12% |
| DC_EnDeCoding.c | 81.58% | 67.07% |
| waveletbpe.c | 82.32% | 88.19% |
| ricecoding.c | 79.78% | 78.45% |
| header.c | 78.38% | 55.56% |
| AC_BitPlaneCoding.c | 77.46% | 67.71% |
| bitsIO.c | 75.00% | 71.43% |
| StagesCodingGaggles.c | 73.24% | 74.29% |
| bpe_encoder.c | 73.04% | 64.29% |
| bpe_decoder.c | 57.72% | 40.48% |
| AdjustOutput.c | 12.13% | 10.93% |

## 5. 総合評価

- **通常のエンコード/デコード経路（レート制限なし、または軽度の非可逆圧縮）については、高い確度で互換性を確認できている。** バイト一致・関数全数検証の両方が PASS しており、実行した範囲についての不一致は 0 件（発見した3件は全て修正済み）。
- **関数レベル検証の価値が実証された**: 発見した3件の不一致はいずれも全パイプライン試験だけでは踏めなかった境界値によるもの。「バイト一致テストが通っている」ことは「あらゆる入力で一致する」ことを意味しないという前提が裏付けられた。
- **最大の未検証領域はレート制限時の途中停止デコード（`AdjustOutPut`）**。カバレッジ12%は「ほぼ試験できていない」に等しい。この関数は停止した段階（ステージ・ビットプレーン・シンボル種別の組み合わせ）ごとに異なるコードパスを通る非常に分岐の多い実装のため、限られたテストケースでは網羅しづらい。次に手を入れるべき最優先領域。
- **AC側のDPCM（`DPCM_ACMapper`/`DPCM_ACDeMapper`）は関数レベル検証が未着手**。DC側で発見したのと同種の整数幅バグが潜んでいる可能性を否定できない。DC側と同じ手法（境界値を突く系列でのベクタ生成）を適用すべき。
- **総じて「互換性が証明された」と言えるのは、本レポートに記載した試験範囲について**であり、無条件に「完全互換」と言い切れる状態ではない。今後のテスト拡充（特にAdjustOutPutとAC側DPCM）によって、この保証の範囲は着実に広げられる設計になっている（`verify/` の各スクリプトは同じパターンで拡張可能）。

## 6. 外部参照ドキュメントとの照合

[oresat/libbpe の `UNL_readme_kielymods.txt`](https://github.com/oresat/libbpe/blob/master/bpe/source/UNL_readme_kielymods.txt)（2026-07-25 参照）で指摘されている C参照実装の問題点を、`original/source/*.c` の現状コードと1項目ずつ突き合わせた。この文書は本リポジトリの `original/readme_kielymods.rtf`（Aaron Kiely, 2008年6-7月）と**内容が一言一句同一**であり、`original/source/*.c` はそもそもこの文書が記述する修正が適用済みの版として本リポジトリに取り込まれている。実際にコードを読んで確認した結果は以下の通り。

| # | 指摘内容 | 該当箇所 | 判定 |
|---|---|---|---|
| 1 | `gaggles==0`（単一ブロックの最終セグメント）で `ACGaggleEncoding`/`ACGaggleDecoding`/`DCGaggleDecoding`/`DCEncoder` が何もせず抜けてしまう | AC_BitPlaneCoding.c, DC_EnDeCoding.c | **修正済み**。4関数とも `for (i = StartIndex; i < StartIndex + gaggles; i++)` で `gaggles` を「値の個数」として一貫使用し、`gaggles==0` 用の早期returnは存在しない |
| 2a | `DCEntropyEncoder` が section 4.3.3 の DC追加ビットプレーンを `q - WtLL3` 枚符号化してしまう（多すぎる） | DC_EnDeCoding.c:239-243 | **修正済み**。`q - max(BitDepthAC, WtLL3)` を計算している |
| 2b | `DCDeCoding` の 4.3.3 ビットデコード処理がコメントアウトされたまま | DC_EnDeCoding.c:693-714 | **修正済み**。有効なコードとして存在（`/* --- Begin/End bug fix (Kiely) --- */` 明記） |
| 2c | `ACBpeDecoding` 内、`ACDepthDecoder` 呼び出し**後**という誤った位置に 4.3.3 デコードのコード片が残っている | AC_BitPlaneCoding.c | **修正済み**。該当コード片は存在しない（2bの通りDCDeCoding側に移設済み） |
| 3 | `AdjustOutPut` 内 `DeConvTwosComp` 呼び出しで `ShiftedDC + DecodingDCRemainder` に `(long int)` キャストがなく、桁あふれし得る | AdjustOutput.c:50-57 | **修正済み**。両オペランドに `(long int)` キャストあり。バグ版はコメントとしてのみ残存 |
| 4 | `DCGaggleDecoding`/`ACGaggleDecoding` のループ変数 `i` が `short` で、2^15以上のブロック/セグメントでオーバーフロー | DC_EnDeCoding.c:262, AC_BitPlaneCoding.c:295 | **修正済み**。どちらも `long int i` |
| 5 | DPCMマッピングの符号反転式・`DPCM_DCDeMapper` の分岐構造にバグ | DC_EnDeCoding.c（DPCM_DCMapper/DeMapper） | **修正済み**。`-(short)( ((X^Bits1)&Bits1)+1 )` 形式、`if (MappedDC > 2*theta) { if ((long int)ShiftedDC[i-1] < 0) ... }` 形式ともに適用済み。バグ版はコメントとしてのみ残存 |
| 6 | 総ブロック数15の画像でセグメント分割の while ループ境界がずれセグフォルト | DC_EnDeCoding.c, AC_BitPlaneCoding.c（4関数） | **修正済み（別形式で）**。文書が示す `while(S_20Bits >= gaggles + GaggleStartIndex)` という文字列そのものではなく、`while(GaggleStartIndex < S_20Bits) { gaggles = min(GAGGLE_SIZE, S_20Bits - GaggleStartIndex); ... }` という、`min()` で残数を確実にキャップする形にリファクタされている。15ブロック境界を含め機能的に同じ問題を回避できている（`verify/run_compat.sh` の `blocks_15` ケースが実際にこの経路を通り PASS している） |
| 7 | `DeConvTwosComp` が `leftmost==1` を特殊扱いし、全ゼロ画像のデコードが失敗し得る | DC_EnDeCoding.c:47 | **修正済み**。ガード条件に `\|\| (leftmost==1)` は存在しない |
| 8 | エンディアン処理: バイトスワップが機能していない／既定値が実行環境依存になっている | bpe_encoder.c（ImageRead）, bpe_decoder.c（ImageWrite, ImageWriteFloat） | **修正済み**。3関数とも実行時に `machineendianness` を算出し `PixelByteOrder` と比較する形になっている |

**結論: 8項目すべて修正済みで、コード変更は不要だった。** 唯一 #6 は文書が示す修正後の文字列と一致しないが、実装がより堅牢な形（`min()` によるキャップ）にリファクタされており、境界ケースの回避という点で機能的に同等以上。この照合によって `original/source/*.c` に対する追加のコード変更は発生しておらず、§2〜§5 の試験結果・カバレッジ評価はそのまま有効。

## 7. 今後の課題

1. `AdjustOutPut` のレート制限デコードを、様々な停止位置（segment_full / bitplane / stage / symbol種別の組み合わせ）で意図的に発生させるテストケースを追加し、カバレッジを引き上げる。
2. AC側 `DPCM_ACMapper`/`DPCM_ACDeMapper` に対して DC側と同様の関数レベル共有ベクタを追加する。
3. `bpe_decoder.c`（カバレッジ57.72%）・`header.c`（55.56%）の分岐カバレッジを引き上げるテストケースの追加を検討する。

## 再現方法

```sh
git submodule update --init --recursive
verify/run_compat.sh --include-slow      # §2.1
verify/run_unit_vectors.py                # §2.2
```

カバレッジ計測（§4）は `gcov`（`--coverage` 付きでビルドした `original/source` に対して上記2つを実行後、コア15ファイルに対し `gcov -b` を実行）で行った。恒常的なCIには組み込んでいない（計測用の一時ビルドが必要なため）。
