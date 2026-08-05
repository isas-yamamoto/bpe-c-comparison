# C/Rust 互換性検証レポート

[English](COMPATIBILITY_REPORT.md)

`original/`（U. Nebraska 製 CCSDS 122.0 Bit Plane Encoder、Aaron Kiely 氏によるバグ修正版 C 参照実装）と `bpe-rs/`（その Rust 移植版）が互換であることを、どのような観点で・どう試験し・その結果何が分かったかをまとめる。試験ハーネスは `verify/` に、実行方法は [README.md](README.md) に記載している。

作成時点のコミット: 本リポジトリ（[bpe-c-comparison](https://github.com/isas-yamamoto/bpe-c-comparison)）`a60d17e` 以降 / [bpe-rs](https://github.com/isas-yamamoto/bpe-rs) `e85a306`。

**スコープの定義**: 本レポートが検証する「互換性」は、CCSDS 122.0 の仕様書そのものへの適合ではなく、この特定のC参照実装（`original/source/`、Aaron Kiely 氏によるバグ修正版）の実際の挙動とのバイト互換である。参照実装自体に含まれる実装上の癖・タイプミスと見られる箇所（例: [INVESTIGATION_LOG.md](INVESTIGATION_LOG.md) §4で扱う`TypeC<<(1<<(3-i))`）も、仕様との整合性を検証・修正する対象ではなく、「Rust側が同じ挙動を再現できているか」の検証対象として扱っている——目的が参照実装の置き換えである以上、この方針は意図的な選択である。

**ファイル構成**: このファイルは現状のリファレンス（下記§0・§1・§2・§5・§6と「再現方法」）であり、状態が変わるたびに上書きされ、比較的短く保たれる。この結論に至った時系列の調査ログ——個々のバグ調査・カバレッジ計測ラウンド・未解決項目の全て——は、伴侶ファイル **[INVESTIGATION_LOG.md](INVESTIGATION_LOG.md)** に§3・§4・§7として記録している（両者が1つのファイルだった頃と同じ節番号をそのまま維持しているため、既存の「（§3.3）」のような引用は全て両ファイルを通してそのまま有効——「同じページの下の方を見る」ではなく「INVESTIGATION_LOG.mdを見る」という意味に変わっただけ）。そちらは追記され続けるだけのファイルで、こちらは随時書き換わる。

## 0. 要約：どこまで互換で、どこから差異があるか

本レポートは長いため、まず結論だけをここにまとめる。詳細・根拠は各セクション参照。**継続的な検証結果の推移は [verify/results/history.md](verify/results/history.md) に記録している。**

| 分類 | 範囲 | 状態 |
|---|---|---|
| **完全互換を確認済み** | 整数・float 両DWTでの全パイプライン166/166ケース（§2.1、既知の1ULP残存差だった5ケース含め全レート値でPASS）／Rice・2の補数変換・DPCM(DC・AC)・パターンマッピング・`AdjustOutPut`・`CodingOptions`・`ACDepthEncoder`/`Decoder`・`DCEntropyEncoder`/`Decoder`の関数レベル全数検証9/9（§2.2）／ランダム化fuzz試験、両DWT込みで3500ケース（§2.3、CIで継続実行）／`readme_kielymods`記載の外部バグ指摘8項目（§6）／コア15ファイル全ての実効カバレッジ100.00%（行・分岐とも、INVESTIGATION_LOG.md §4）／x86-64（Linux, gcc）に加えARM64（macOS/Apple Silicon, Apple clang）でも178/178 PASS確認（INVESTIGATION_LOG.md §3.10） | 不一致 **0件** |
| **既知の差異** | なし。旧版はfloat DWTデコードに「コンパイラの丸め実装差」による1ULP残存差があるとしていたが、実際はbpe-rs側の浮動小数点精度バグだったと判明し修正済み（INVESTIGATION_LOG.md §3.3） | — |
| **検証対象外** | ファイルI/O失敗等のエラーパス、CLIから構造的に到達不能な設定項目（`CustomWtFlag`・`TransposeImg`等、確定デッドコード356行、INVESTIGATION_LOG.md §4） | 意図的にスコープ外 |

### この文書の読み方

本レポートはかつて「現状どうなっているか」と「どう調べてこの結論に至ったか」を1つのファイルに混在させていたが、目的別に2ファイルへ分割した（上記「ファイル構成」参照）。知りたいことに応じて読む場所を絞ると早い。

| 知りたいこと | 読む場所 | 備考 |
|---|---|---|
| 結論だけ知りたい | 上の要約表、§5 総合評価（本ファイル） | これで大抵足りる |
| どういう試験をしているか（方法論） | §1 検証観点、§2 試験内容と結果（本ファイル） | 増減の少ない安定した内容 |
| 見つかったバグの一覧・現状 | INVESTIGATION_LOG.md §3.8 対応状況まとめ（表） | §3.1〜3.7・3.9・3.10は個々の発見に至った調査の経緯・詳細——表で足りなければ該当節へ |
| Cコードのどこまで踏めているか（数値） | INVESTIGATION_LOG.md §4内の「最終結果」「到達可能性内訳」「分岐到達可能性内訳」の3つの表 | §4のそれ以外の節は調査の経緯（読み飛ばし可） |
| 今後やること | INVESTIGATION_LOG.md §7 今後の課題 | 打ち消し線＝対応済み |
| 精度を犠牲にして互換性を取った箇所の一覧 | [PRECISION_TRADEOFFS.ja.md](PRECISION_TRADEOFFS.ja.md) | 浮動小数点演算順序・整数折り返しなど、「数学的に正しい/精度が良い」実装よりCとの一致を優先した具体箇所 |

§0・§1・§2・§5・§6（本ファイル）は「現状」の記述なので毎回まるごと読んでも負担は少ない。INVESTIGATION_LOG.mdの§3・§4・§7は分量が多いが、上の表の通り**結論部分（表）だけ拾えば経緯を追う必要はない**——経緯は監査証跡として残しているだけで、読者が毎回読み通すことは想定していない。

## 1. 検証観点

「互換」を単一の指標では測れないため、性質の異なる5つの観点を組み合わせている。

| # | 観点 | 何を検証するか | なぜ必要か |
|---|------|----------------|-----------|
| 1 | 全パイプラインのバイト一致 | 実画像を両実装でエンコード/デコードし、出力バイト列が完全一致するか | 最終的に利用者が気にするのはこれだけ。だが「たまたま今のテスト画像で一致した」を「常に一致する」と混同しないよう、既知の境界値を突く画像を意図的に混ぜる |
| 2 | 既知エッジケースの網羅 | `original/readme_kielymods.rtf` に記録された Kiely 氏の実バグ修正箇所（単一ブロックセグメント、全ゼロ画像、ブロック数境界、エンディアン等）を再現する入力を用意し、そこでも一致するか | 通常の自然画像はバグの温床になりやすい境界値をほぼ踏まない。エッジケースを明示的に作らないと「一致した」の意味が薄くなる |
| 3 | 関数レベルの全数検証 | Rice 符号化・2の補数変換・DPCM DC マッピング・パターンマッピング・`CodingOptions`を、C参照実装のオブジェクトコードに直接リンクしたジェネレータで表現可能な入力域の全体（またはそれに近い範囲）にわたって突き合わせる | 観点1・2は「その画像が生成する値」しか通らない。個々の関数が受け得る値の全域はカバーできない。実際、後述のDPCMバグは通常のテスト画像では一切踏めなかった |
| 4 | ランダム化fuzz試験 | 画像サイズ・ビット深度・符号・バイト順・レート・セグメントサイズ・画素内容をランダムに組み合わせ、観点1と同じバイト一致チェックを大量に繰り返す（`verify/fuzz_compat.py`） | 観点1・2は人手で選んだ組み合わせしか通らない。「カバレッジ100%」は分岐が一度でも実行されたことしか意味せず、同じ分岐を通る別の値での不一致は排除できない——これを補う |
| 5 | ステージ境界のトレース比較 | DWT変換直後・ブロック文字列構築直後の中間値を両実装からダンプしdiffする（`verify/compare_traces.py`） | 観点1で不一致が出た際に「エンコーダのどの段階で分岐したか」を即座に特定するための診断手段。それ自体は pass/fail の判定基準ではない |

上記に加え、**コードカバレッジ計測**（INVESTIGATION_LOG.md §4）で「観点1・2がCコードのどれだけの範囲を実際に踏んでいるか」を定量化し、上記の観点だけでは見えない「まだ検証できていない領域」を可視化した。

## 2. 試験内容と結果

### 2.1 全パイプライン・バイト一致（`verify/run_compat.sh`）

166ケース（うち1件は `--include-slow` 指定時のみの低頻度回帰ケース）。全ケースで **エンコード出力バイト一致・相互デコード出力バイト一致（C→Rust, Rust→C 双方向）** を確認。

| ケース | 内容 |
|---|---|
| baseline_256 × (t=0,1) × (r=0, 1.0, 4.0) | 通常のグラデーション画像。integer/float DWT × 可逆/非可逆レート |
| all_zero_64 | 全ゼロ画像（`leftmost==1` 特殊扱いバグの回帰） |
| minimal_17x17 | 最小画像サイズ（`IMAGE_WIDTH_MIN`/`IMAGE_ROWS_MIN`） |
| single_trailing_block_48x24 | readme記載の「末尾セグメントが1ブロック」репро（`-s 17`） |
| blocks_15 / 16 / 17→18 / 31→32 / 32 / 33 | 総ブロック数が GAGGLE_SIZE(16) の境界に来る画像（17, 31 は幾何的に構成不能なため隣接値に代替、理由は `verify/gen_vectors.py` のコメント参照） |
| pixels16_f0 / f1 × (t=0,1) | 16bit画素 × リトル/ビッグエンディアン × integer/float DWT（float DWT側は `ImageWriteFloat` の16bit分岐が、それまで一切踏まれていなかった） |
| pixels12_f0 × (t=0,1) | 12bit画素（16bit語に格納）× 両DWT。`-b 16` は4bit幅のヘッダフィールドに収まらず常に0（「既定16bit」）扱いになるため、pixels16だけでは踏めない明示的ビット深度分岐を突く |
| signed_32 / signed_32(float) / signed16_32 × (t=0,1) | 符号付き画素を8bit・8bit+floatDWT・16bit(両DWT)の4通りで検証（`ImageWrite`/`ImageWriteFloat` の符号付き分岐が signed_32 単体だと8bit・整数DWTしか踏めていなかった） |
| signed16_32 の明示的ビット深度（`-b 12`）× (t=0,1) | signed16_32の既定 `-b 16` は内部で0（既定16bit）扱いになるため踏めない、符号付き16bit語版の明示的ビット深度分岐（`PixelMax`計算の別枝）を突く |
| デコード側バイト順の反転（4ケース） | `PixelByteOrder` はビットストリームヘッダに含まれない、呼び出しごとのCLI設定（INVESTIGATION_LOG.md §3.7参照）であり、エンコード時の値とは独立にデコード時の出力順を選べる。従来はエンコードと同じ順でしかデコードしていなかったため、`ImageWrite`/`ImageWriteFloat` の「バイト順反転が必要」分岐が全く踏まれていなかった。pixels16_f0・signed16_32、両DWTで反転させたデコードのC/Rust一致を確認 |
| baseline_256 レート制限デコードの掃引（18ケース） | セグメントサイズ64（既定256より細かく分割）× t=1で10段階・t=0で8段階のレート値（0.05〜3.0bpp）を掃引し、`AdjustOutPut`（レート制限時の途中停止デコード）を多数の異なる停止位置で駆動する。t=0側は全8値含め不一致なし（旧版は2値を既知の残存ULP差として除外していたが、INVESTIGATION_LOG.md §3.3の根本原因修正後は全値PASS） |
| checkerboard_256 レート制限デコードの掃引（20ケース） | 同じ掃引を高コントラストのチェッカーボード画像（大振幅・両符号のAC係数）に対しても実施。t=0側の全10値を含め不一致なし |
| noise_256 レート制限デコードの掃引（17ケース） | 同じ掃引を決定論的な擬似ランダム画像（周期性も単調構造も持たない、3つ目の独立した係数分布形状）に対しても実施。t=0側は全3値含め不一致なし（旧版はINVESTIGATION_LOG.md §3.3の根本原因修正前の残存差により除外していた） |
| large_segment_slow | 1セグメント33,000ブロック（`short i` オーバーフローの回帰、週次CIのみ） |
| ac_depth1 / ac_depth2 / ac_depth5_16bit × (t=0,1) | AC係数の最大振幅（`BitDepthAC_5Bits`）を狙って作った64×64画像3種（値1・2・17）。`ACDepthEncoder`/`ACDepthDecoder`（AC_BitPlaneCoding.c）のN依存分岐（N==2, N==5）と、`BitDepthAC_5Bits==1`時の専用1ビット面パスは、通常のbaseline/checkerboard/noise画像がN==3,4にしか収まらないため一切踏めていなかった |
| ac_depth2 / ac_depth5_16bit のレート制限掃引（8ケース） | 上記2画像をセグメントサイズ16（画像全体は64ブロック=4セグメント相当）× 4段階のレートで掃引し、`ACBpeEncoding`/`ACGaggleDecoding` 内のセグメント途中打ち切り分岐を追加で駆動 |
| dc_depth_n2 / dc_depth_n4 / dc_negpow / dc_mixed_sign（4ケース） | DC係数の実効ビット深度（`N = BitDepthDC_5Bits − QuantizationFactorQ`）とMax_DCの符号を狙って作った64×64画像4種（ほぼ真っ黒な定数値1・2、符号付きの2のべき乗負数-128、背景+20に1ブロックだけ-120）。`DCEntropyEncoder`/`Decoder`のN==2/N<=4分岐と、`Max_DC`が負の場合のビット深度計算（2のべき乗ちょうどの補正含む）は通常の画像では一切踏めていなかった |
| dc_qprime_lo_64（1ケース） | 低い一定背景値（2）に8×8ブロックごと1画素だけ振幅50の孤立バンプを置いた64×64・16bit画像。`QuantizationFactorQ_prime = BitDepthDC - 3`分岐（`BitDepthDC > 3`かつAC/DC深度差が小さい場合）は、AC/DC深度が連動しがちな通常画像では一切踏めていなかった |
| セグメントサイズの多様化掃引（60ケース、整数DWTのみ） | baseline/checkerboard/noise の256×256画像 × セグメントサイズ5段階（16, 20, 24, 32, 48）× レート4段階（0.5, 0.8, 1.2, 2.0）。`StagesDeCodingGaggles1/2/3`（StagesCodingGaggles.c）がレート制限の停止位置（`X/Y_LocationStopDecoding`）を記録する数十箇所のチェックポイントは、セグメントサイズ64固定のレート掃引だけでは大半しか踏めていなかった。float DWT（`-t 0`）はINVESTIGATION_LOG.md §3.3の根本原因修正後は同じ格子でも不一致が出ないことを確認済みだが、`StagesCodingGaggles.c`のカバレッジ目的自体は整数DWTだけで達成できるため、掃引は意図的に整数DWTのみに留めている（INVESTIGATION_LOG.md §4参照） |

**結果: 166/166 PASS**（2026-07-26 時点、デコード側バイト順反転ケース追加時に初回は12件FAIL — INVESTIGATION_LOG.md §3.7参照）。

### 2.2 関数レベルの全数検証（`verify/run_unit_vectors.py`）

| 対象関数 | C参照実装 | 網羅範囲 | 検証方法 |
|---|---|---|---|
| `RiceCoding`/`RiceDecoding` | ricecoding.c | bit_length 1–4 × 有効な option の組（計10通り）× 各の表現可能値全域 | Rust encode がCのバイト列と完全一致。さらにCが生成したバイト列を Rust decode に読ませ、元の値列を正しく復元できるかも確認（クロスデコード） |
| `ConvTwosComp` | DC_EnDeCoding.c | leftmost = 2–16 × 各幅の表現可能値全域 | Rust `conv_twos_comp` の出力がCと完全一致 |
| `DPCM_DCMapper`/`DPCM_DCDeMapper` | DC_EnDeCoding.c | N = 4, 8, 16 × {単調増加, 両極端の交互, 中間値付近での振動} の3系列＋固定シーケンス1本（計10系列） | Rust `dpcm_dc_mapper`/`dpcm_dc_demapper` の出力（MappedDC・復元ShiftedDC）がCと完全一致 |
| `PatternMapping` | PatternCoding.c | (sym_len, type) の全7通り × 各の表現可能値全域 | Rust `pattern_mapping` の出力がCと完全一致 |
| `DPCM_ACMapper`/`DPCM_ACDeMapper` | AC_BitPlaneCoding.c | N = 2, 3, 4, 5（`ac_depth_encoder` が許容する全深度）× 同様の3系列 | Rust `dpcm_ac_mapper`/`dpcm_ac_demapper` の出力がCと完全一致（不一致なし、INVESTIGATION_LOG.md §3.4参照） |
| `AdjustOutPut` | AdjustOutput.c | DWTType(2) × stoppedstage(1–4) × b_DC分岐(3) × ブロック内停止座標 X/Y(8×8) × 値の符号variant(4) = 6144通り全数 | Rust `adjust_output` の出力（ブロック内int/float係数、3ブロック分）がCと完全一致（不一致なし、INVESTIGATION_LOG.md §3.5・§3.6参照） |
| `CodingOptions` | PatternCoding.c | sym_len 2/3/4 × 全carrying type、単一シンボル全域＋sym_len 3のペア全64通り＋sym_len 4の(a,b,c)三つ組全4096通り＋INVESTIGATION_LOG.md §4項目8で発見した2つの手作りケース（328/329行目の偽方向） = 4230通り | Rust `coding_options` の出力（Option[0..3]）がCと完全一致（不一致なし） |
| `ACDepthEncoder`/`ACDepthDecoder` | AC_BitPlaneCoding.c | `ACDepthEncoder`が支援するN(2-5)全域を代表するBitDepthAC_5Bits値×1〜3ガグル相当のセグメントサイズ5通り×BitMaxAC分布3パターン（ramp/extremes/mid-boundary） = 120通り | Rust `ac_depth_encoder`の出力バイト列がCと完全一致。さらにCが生成したバイト列を`ac_depth_decoder`に読ませ、元のBitMaxAC列を正しく復元できるかも確認（クロスデコード） |
| `DCEntropyEncoder`/`DCEntropyDecoder` | DC_EnDeCoding.c | `QuantizationFactorQ_prime`の4分岐のうちN≥2で到達可能な全パターン×Nブラケット4種（2/≤4/≤8/>8）×セグメントサイズ5通り×ShiftedDC分布3パターン＝75通り（§4.3.3追加ビットプレーンの有無も両方含む） | Rust `dc_entropy_encoder`の出力バイト列がCと完全一致。追加ビットプレーンが無いケースのみ`dc_entropy_decoder`でのクロスデコードも確認（理由は本文参照） |

**結果: 9/9 テストグループ PASS**（ただし後述の通り、DC側DPCM は1周目で2件の不一致を検出・修正した上での PASS）。

`ACDepthEncoder`/`DCEntropyEncoder`のテストベクタ生成中、`CustomWtLL3_2bits`（LL3サブバンドの既定ウェイト値）を誤って0で初期化していたことが判明した——`HeaderInilization`はこれを常に`3`にハードコードしており（`CustomWtFlag`が常時FALSEなのとは別の話で、こちらは0にはならない）、`DCEntropyEncoder`は`QuantizationFactorQ`・`numaddbitplanes`双方の計算でこの値を`max()`に使う。ジェネレータ側をこの値で修正したところ、bpe-rs側の実装（`dc/entropy.rs`の`dc_entropy_encoder`自体）は最初から正しく`custom_wt_ll3`を折り込んでおり、実装バグではなくテストハーネスの初期化漏れだったと確認できた。

### 2.3 ランダム化fuzz試験（`verify/fuzz_compat.py`）

§2.1の166ケースは既知のエッジケースを狙った手作りマトリクスであり、そこに含まれない入力の組み合わせは原理的に未検証のまま残る。これを補うため、画像サイズ・ビット深度・符号・バイト順・レート・セグメントサイズ・画素内容パターンをランダムに組み合わせ、§2.1と同じエンコード一致＋相互デコード一致チェックを大量に繰り返すfuzzハーネスを追加した。整数・float両DWTを対象とする（既定 `--dwt-type both`）。旧版はfloat DWTをINVESTIGATION_LOG.md §3.3の「既知の1ULP残存差」でノイズになるとして除外していたが、根本原因を修正済みの今はfloat DWTも他と同様に有意義な検証対象であり除外する理由がない。エンコーダが構造的に受理できないレート×セグメントサイズの組み合わせ（`BPE_RATE_ERROR`、C/Rustの差異ではなく入力妥当性の問題）はレートを引き上げて自動的にリトライする。

CIには2段階で組み込んだ: push/PRごとに固定シード50ケースの軽量スモークテスト、週次スケジュール/手動実行で乱数シード5000ケースの深掘りfuzz（`deep-fuzz`ジョブ）。手元では両DWT込みで500ケース（`--seed 123`）・3000ケース（`--seed 999`）を実行し不一致0件を確認済み。

### 2.4 ステージ境界トレース比較（`verify/compare_traces.py`）

エンコード側（DWT変換直後・ブロック文字列構築直後）に加え、デコード側にレベル単位の逆DWT境界（`post_idwt_level2`/`post_idwt_level1`）を含む5箇所（`adjust_output` 出力・セグメント再組立後・逆DWTレベル2/1完了後・逆DWT直後）を diff する仕組みを整備。整数DWTでは全段一致。float DWTもINVESTIGATION_LOG.md §3.3の根本原因修正後は全段一致する。このレベル単位の切り分けは、まさにその1ULP差の原因（`inverse_lifting97f`のどのレベルで発生しているか）を特定する過程で新設したもので、恒常的な pass/fail 判定というより、次に同様の不一致が出た際の一次切り分け手段として用意している。

## 5. 総合評価

- **通常のエンコード/デコード経路（レート制限なし、または軽度の非可逆圧縮）については、高い確度で互換性を確認できている。** バイト一致・関数全数検証の両方が PASS しており、整数DWTでは実行した範囲についての不一致は 0 件。
- **関数レベル検証・レート掃引の両方が独立に実バグを発見した**: DC側DPCMの2件は§2.2（関数レベル）で、逆DWT精度不一致は§2.1のレート掃引で発見。「バイト一致テストが通っている」ことは「あらゆる入力・条件で一致する」ことを意味しないという前提が、性質の異なる2つの手段で別々に裏付けられた。
- **float DWT のデコードにあった1ULPレベルの残存差は、根本原因を特定し完全に修正済み**（INVESTIGATION_LOG.md §3.3）。当初は「gcc/rustcのコンパイラ差による解消不能な残存差」と誤診断していたが、実際はbpe-rs側の`inverse_lifting97f`がCの演算子単位の型変換規則（f32のまま加算してからf64へ昇格）を再現できていなかったことが原因の、修正可能な実装バグだった。修正後は整数・float両DWTで不一致0件（178/178ケース、fuzz試験3500ケース）。
- **レート制限時の途中停止デコード（`AdjustOutPut`）は、全パイプライン試験の限界を関数直接呼び出しで突破し、行65.02%→96.90%まで伸びた**（INVESTIGATION_LOG.md §3.5・§4）。ブロック内の停止座標(`X/Y_LocationStopDecoding`、8×8=64通り)はビットストリームの端数依存で全パイプライン試験からはほぼ到達不能だったが、C参照実装のオブジェクトコードに直接リンクして関数を単体駆動する手法（Rice/DPCM/パターンマッピングで既に実績のある手法）に切り替えたことで、DWTType×stoppedstage×DC分岐×X/Y座標の全1024通りを直接網羅でき、不一致は0件だった。
- **さらに`AdjustOutPut`の残る95行（値の符号バリエーション不足が原因と判明）を、テスト値に符号反転・位相シフトの`variant`次元を追加して6144通りに拡張することで行97.35%→99.55%まで伸ばし、最後の16行も制御フロー解析で確定デッドコード（冗長ガードの`else`節）と証明した**（INVESTIGATION_LOG.md §3.6・§4）。`AdjustOutPut.c`は最終的に実効カバレッジ100%（デッドコードを除けば完全網羅）に到達し、この1関数だけでコアアルゴリズム全体の約49%の行数を占めるため、コア15ファイル全体の実効カバレッジ（最終的に100.00%、INVESTIGATION_LOG.md §4）を大きく押し上げる要因となった。
- **コア15ファイル全体の行カバレッジは95.14%（分岐86.79%）に達し、当初目標としていた90%を大きく上回った。** `AdjustOutPut`の直接呼び出しハーネス追加・値多様化に加え、`StagesCodingGaggles.c`・AC/DC側・`PatternCoding.c`向けの直接呼び出しハーネス（INVESTIGATION_LOG.md §4で詳述）が積み重なった結果であり、残るギャップは`AC_BitPlaneCoding.c`・`BPEBlockCoding.c`各1行のみというほぼ天井に近い水準にある。
- **`PatternCoding.c`の4-bit Rice分割オプション選択チェーンに残っていた5分岐を全て解消した（INVESTIGATION_LOG.md §4「分岐到達可能性内訳」項目8）。** 1分岐（328行目の偽方向）は反例となる具体的なシンボル値の組み合わせを新たに発見しテストへ追加、残り4分岐（330・334・335・336行目）は「4つの実数のうち必ずどれかは他の3つ以下」という恒真命題と推移律から構造的到達不能と証明した。これによりコア15ファイル全てが実効カバレッジ100.00%（行・分岐とも）に到達し、本レポートに残っていた「未証明」項目はなくなった。
- **確定デッドコード（356行、全体の4.9%）を除いた「到達可能コード」ベースの実効カバレッジは100.00%に達する**（INVESTIGATION_LOG.md §4の到達可能性内訳表）。生の95.14%という数値だけでは「残りはテストの工夫で埋まるのか、そもそも埋まらないのか」が判断できないため、ファイルごとに未実行行を「確定デッドコード／エラーパス」と「到達可能な残存ギャップ」に手作業で分類した。当初は真の伸びしろが全体の約1.0%（70行）残っていたが、`AdjustOutPut`と同じ「直接呼び出し＋全数掃引」の手法を`StagesCodingGaggles.c`・AC/DC側・`PatternCoding.c`にも適用した結果、そのほとんどを解消（実際にカバー）するか、確定デッドコードであると証明（`gdb`による外部からの状態強制で検証）でき、最後まで残っていた`AC_BitPlaneCoding.c`・`BPEBlockCoding.c`各1行も制御フロー解析（前者は呼び出し先が出力関数を一切持たないことの確認、後者はシンボルスロットの消費順序の追跡）で確定デッドコードと証明し、コア15ファイル全体で真に未解明の残存ギャップは**ゼロ**になった。
- **`StagesCodingGaggles.c`（レート制限デコードの停止位置を記録する側のコード）は、セグメントサイズ自体を変える掃引（前ラウンド、行85.93%→88.85%）に続き、`StagesDeCodingGaggles1/2/3`を直接呼び出しビット予算を全数掃引するハーネスを新設したことで行88.85%→92.80%・分岐83.69%→87.06%まで伸びた**（INVESTIGATION_LOG.md §4）。残る42行は全て、より狭いチェックポイントが必ず先に横取りするという構造的redundancy、または座標選択の`else`分岐がその入室条件と数学的に矛盾するという2パターンで確定デッドコードと証明でき、実効カバレッジ100%に到達した。
- **カバレッジ計測方式そのものに`gcov`の`.gcda`マージ不整合という計測バグがあったことを発見・修正した**（INVESTIGATION_LOG.md §4）。共有する`.c`ファイルをバイナリごとに再コンパイルしていたため、後発のリンクが先行する実行の`.gcda`を正しくマージせず、過去のラウンドで報告していた数値（コア15ファイル全体で行75.96%等）は全て過小集計だった。全対象ファイルを一度だけコンパイルし全バイナリで共有オブジェクトを使い回す方式に改め、本レポートの数値はその訂正後の測定である。
- **`bpe_decoder.c` は画素フォーマット・DWTType・バイト順の組み合わせを網羅的に増やすことで行66.18%→87.87%・分岐50.00%→79.17%まで伸びた**（INVESTIGATION_LOG.md §4）。特に「デコード時のバイト順は独立したCLI設定でありエンコード時の値とは無関係」という点を突いた追加ケースが、Rustの実バグ（INVESTIGATION_LOG.md §3.7）の発見に直結した。残る約12%は`TransposeImg`分岐・無効化済みのダンプ関数・`UseFill`ゲートのフィル読み飛ばしループ・エラーパスで、いずれも確認済みの到達不能コードか本レポートの対象外（INVESTIGATION_LOG.md §4参照）。
- **`bpe_encoder.c` は画素フォーマットの多様性（符号付き・非既定ビット深度）を増やすことで既に大きく伸びていた**（+8〜9pt、前ラウンド）。`gcov` の注釈を直接読んで未実行行を特定し、CLIから到達可能なものだけを狙い撃ちする手法が有効だった。
- **`header.c` の残り約22%（分岐で約44%）はテストでは埋まらないデッドコードと断定した**。これ以上このファイルにテストケースを追加する意味はない。
- **AC側のDPCMは関数レベル検証を追加し、バグなしを確認した**（INVESTIGATION_LOG.md §3.4）。DC側と同様の懸念は解消された。
- **AC側の残る主要関数（`ACGaggleEncoding`/`ACGaggleDecoding`/`ACDepthEncoder`/`ACDepthDecoder`）は、AC係数振幅（`BitDepthAC_5Bits`）を狙って作った3種の画像で行79.92%→86.07%・分岐71.88%→80.73%まで伸び、その後`ACGaggleDecoding`の直接呼び出し掃引で行86.48%・分岐82.81%まで伸びた**（INVESTIGATION_LOG.md §4）。副産物として`BPEBlockCoding.c`も微増した。残るギャップは`OptDCSelect`/`OptACSelect`が常にTRUEなため到達不能なヒューリスティック分岐（`header.c`のデッドコードと同種）と、`N>5`の防御的`ErrorMsg`（数学的に到達不能）で、`ACBpeEncoding`内最後の1行（`BlockScanEncode`呼び出し直後の`SegmentFull`早期return）も、`BlockScanEncode`が出力関数を一切呼ばないため直前の同一チェックと数学的に等価と証明でき、`AC_BitPlaneCoding.c`は実効カバレッジ100%に到達した。
- **DC側（`DC_EnDeCoding.c`）も同様のN境界・負のMax_DC問題を狙って作った4種の画像で行83.04%→93.27%・分岐70.73%→86.18%まで伸び、さらに`dc_qprime_lo_64`（低背景値+孤立バンプ）画像と`DCEntropyDecoder`の返り値解析で行93.86%・分岐87.80%まで伸びて実効カバレッジ100%に到達した**（INVESTIGATION_LOG.md §4）。DC側の`N`はAC側と違いビット幅で数学的に上限があるわけではないが、`QuantizationFactorQ_prime`の選定式が`N`を常に10以下に収めるよう設計されており、`N>10`の防御的`ErrorMsg`は構造的に到達不能と確認した。
- **`bitsIO.c`（残り19行）は追加調査の結果、全て確定的に到達不能と確認し、これ以上テストケースを追加しても伸びないと判断した**（INVESTIGATION_LOG.md §4）。`CodeWord_Length`16/24/32bit分岐は`CodewordLength_2Bits`常時0による既知のデッドコードと同一原因。`RateReached==TRUE`早期returnは全ソースgrepで`RateReached`/`SegmentFull`が常に同時に設定・リセットされることを確認し、手前の`SegmentFull`チェックで必ず先に返ることが判明。`length>32`分岐は16bit画素という参照実装のビット深度上限から来る係数レンジの実測（最大`BitDepthDC_5Bits`19）を根拠に、実質到達不能と判断した。セグメント末尾のフィル読み飛ばしループ（2行）は、`dc_qprime_lo_64`テスト追加の副産物として実際にカバーされた。
- **カバレッジを上げる作業そのものが、それまで隠れていたRustの実バグ（デコード側`-f`の無視、INVESTIGATION_LOG.md §3.7）を発見する契機になった。** これは、コードカバレッジの穴を埋める行為が単なる数字の改善ではなく、試験マトリクス自体の見落とし（=Rust側のバグを隠していたパターン）を暴く実質的な検証行為でもあることを示している。
- **総じて「互換性が証明された」と言えるのは、本レポートに記載した試験範囲について**であり、無条件に「あらゆる入力について完全互換」と数学的に証明された状態ではない（INVESTIGATION_LOG.md §7項目13参照）。ただし、既知の差異は現時点で**ゼロ**である（float DWTの1ULP残存差もこの節で修正済み）。今後のテスト拡充（特にまだ手つかずのAC側関数）によって、この保証の範囲はさらに広げられる設計になっている（`verify/` の各スクリプトは同じパターンで拡張可能）。
- **本レポートの大半の測定は単一のビルド環境（1つのgccバージョン・1つのrustcバージョン・x86-64ベースラインターゲット・FMA無効）で行われているが、Apple M5（ARM64/macOS・Apple clang）でのクロスアーキテクチャ検証を1件実施し、実際に新規バグを発見・修正した**（INVESTIGATION_LOG.md §3.10）。FMA命令がベースISAに含まれるARM64では、Cコンパイラの既定のFP_CONTRACT設定がforward/inverse両方のfloat DWT計算をFMA融合し、Rust側（自動融合しない）と広範囲に食い違っていた——`-ffp-contract=off`をMakefileに固定して修正済み。この一件は、「単一環境でしか検証していない」という以前の限界指摘が的確だったことの実証であると同時に、その限界が実際に埋まったことも示す。ただし異なるgcc/rustcバージョンでの再現性、他のARM実装（Apple Silicon以外）、他の最適化レベル（`-O0`/`-O3`等）は依然**未検証**であり、既知の限界として明記する。

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

**結論: 8項目すべて修正済みで、コード変更は不要だった。** 唯一 #6 は文書が示す修正後の文字列と一致しないが、実装がより堅牢な形（`min()` によるキャップ）にリファクタされており、境界ケースの回避という点で機能的に同等以上。この照合によって `original/source/*.c` に対する追加のコード変更は発生しておらず、本レポートおよびINVESTIGATION_LOG.mdの試験結果・カバレッジ評価はそのまま有効。

## 再現方法

```sh
git submodule update --init --recursive
verify/run_compat.sh --include-slow      # §2.1
verify/run_unit_vectors.py                # §2.2
verify/fuzz_compat.py --iterations 2000                  # §2.3
verify/compare_traces.py <raw> <w> <h> [--float-tol=N] [bpe args...]   # §2.4、切り分け用
```

カバレッジ計測（INVESTIGATION_LOG.md §4）は、対象の全 `.c` ファイルを**一度だけ** `gcc -O0 --coverage -c` でコンパイルし、`bpe` 本体と `verify/c_unit_tests/` の全ジェネレータをこの共有オブジェクト群に対して（再コンパイルせず）`--coverage` 付きでリンクした上で、両方を同一ディレクトリで実行し、コア15ファイルに対し `gcov -b` を実行して行った（INVESTIGATION_LOG.md §4の「計測方法の訂正」参照。共有ソースをバイナリごとに再コンパイルすると `.gcda` のマージが壊れるため注意）。恒常的なCIには組み込んでいない（計測用の一時ビルドが必要なため）。
