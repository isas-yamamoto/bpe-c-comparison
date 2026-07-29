# 比較結果の履歴

`COMPATIBILITY_REPORT.md` はその時点の検証結果のスナップショット（毎回上書き）であり、過去の推移を追えない。このファイルはフル検証ラウンド（`verify/run_compat.sh --include-slow` + `verify/run_unit_vectors.py` + カバレッジ計測）を実施するたびに1行追記し、推移を残すためのもの。

行の由来は2種類ある:

- **手動のフル検証ラウンド**: 下記の手順で人間が追記。行カバレッジ・分岐カバレッジまで含む完全な行になる。
- **自動追従（`.github/workflows/bpe-rs-poll.yml`）**: bpe-rsの`main`が動くたびに毎日ポーリングし、新しいコミットに対し `verify/run_compat.sh --include-slow` と `verify/run_unit_vectors.py` がPASSしたらsubmodule bump PRを自動作成、その中でこのファイルにも1行追記される（カバレッジ列は再計測しないため`—`）。FAILした場合はPRを作らずワークフローが失敗するだけなので、そのラウンドはここに記録されない — Actionsのポーラーの実行履歴側で確認する。

## 追記の仕方（手動ラウンド）

フル検証を実施したら、以下を1行追記する（新しい行を末尾に追加。既存行は編集しない）。

```sh
git log -1 --format='%H'                 # 本リポジトリのcommit
git -C bpe-rs log -1 --format='%H'       # bpe-rsのcommit
```

- **バイト一致**: `verify/run_compat.sh --include-slow` の結果（例: `166/166 PASS`）
- **関数レベル**: `verify/run_unit_vectors.py` の結果（例: `6/6 PASS`）
- **行カバレッジ/分岐カバレッジ**: `COMPATIBILITY_REPORT.md` §4 の生の値と、確定デッドコードを除いた実効カバレッジの値
- **既知差異**: その時点で残っている既知の差異（`COMPATIBILITY_REPORT.md` §0参照）。増減があればここで分かる
- **備考**: そのラウンドで新たに見つかった不一致・修正・カバレッジ改善など、次の人が差分を追うためのヒント

## 履歴

| 日付 | 本リポジトリcommit | bpe-rsコミット | バイト一致 | 関数レベル | 行カバレッジ(生/実効) | 分岐カバレッジ(生/実効) | 既知差異 | 備考 |
|---|---|---|---|---|---|---|---|---|
| 2026-07-28 | `f0b15ab` | `4ca3eb4` | 166/166 PASS | 6/6 PASS | 95.14% / 100.00% | 86.77% / 99.91% | float DWTの1ULP残存差（5/28件）、`PatternCoding.c`の5分岐が未証明 | 初回エントリ。`COMPATIBILITY_REPORT.md` 現行版の内容をそのまま記録。以前のラウンド（行カバレッジ97.93%→99.25%→99.91%等の推移）はgit履歴（`git log --oneline`）に残っているが、本ファイルには遡って記載していない |
| 2026-07-28 | `6c08a53`（このラウンドの変更を作った時点の親コミット、pre-commit） | `4ca3eb4`（変更なし） | 166/166 PASS（変更なし） | 6/6 PASS（変更なし） | 95.14% / 100.00%（変更なし） | 86.79% / 100.00%（前回86.77% / 99.91%） | float DWTの1ULP残存差（5/28件）のみ | `PatternCoding.c`の`CodingOptions`4-bit選択チェーンに残っていた5分岐を解消（`COMPATIBILITY_REPORT.md` §4項目8）。1分岐（328行目）は新しい反例（TYPE_CIパターン値0×4・6×3）を`verify/c_unit_tests/gen_codingoptions_coverage.c`に追加してカバー、残り4分岐（330・334・335・336行目）は「4実数のうち必ずどれかが他の3つ以下」という恒真命題＋推移律で構造的到達不能と証明。コア15ファイル全てが実効カバレッジ100.00%（行・分岐とも）に到達し、既知の差異はfloat DWTの1ULP残存差のみになった。パイプライン試験・関数レベル試験は無変更のため未再実行（バイト一致・関数レベル列は前回のまま） |
| 2026-07-28 | `bb5c517`（このラウンドの変更を作った時点の親コミット、pre-commit） | `e91f677`（bpe-rs、`4ca3eb4`から変更） | 166/166 PASS（変更なし） | **7/7 PASS**（`CodingOptions`追加） | 95.14% / 100.00%（変更なし） | 86.79% / 100.00%（変更なし） | float DWTの1ULP残存差（5/28件）のみ | 「100%カバレッジは全入力値の証明ではない」という指摘を受け、§7項目13の1・2に着手。(1) ランダム化fuzz試験`verify/fuzz_compat.py`を新設（整数DWT限定、`BPE_RATE_ERROR`は自動リトライ）、手元で2000ケース（`--seed 7`）不一致0件、CIに軽量スモーク(push/PR毎50ケース)＋週次深掘り(5000ケース)を統合。(2) `CodingOptions`の直接呼び出しによる全数（準）検証をbpe-rs側に追加（`verify/c_unit_tests/gen_codingoptions_vectors.c`新設、単一/ペア/三つ組4230通り、bpe-rs commit `e91f677`に`shared_vectors_match_c_reference`テスト追加）、不一致0件。`ACGaggleEncoding`/`ACGaggleDecoding`・`DCGaggleEncoding`/`DCGaggleDecoding`への同種拡張は未着手のまま残す（§7項目13参照） |
| 2026-07-28 | `e0a492f`（このラウンドの変更を作った時点の親コミット、pre-commit） | `fb494d3`（bpe-rs、`e91f677`から変更） | 166/166 PASS（変更なし） | **9/9 PASS**（`ACDepthEncoder`/`Decoder`・`DCEntropyEncoder`/`Decoder`追加） | 95.14% / 100.00%（変更なし） | 86.79% / 100.00%（変更なし） | float DWTの1ULP残存差（5/28件）のみ | §7項目13(2)の残り(`ACGaggleEncoding`/`DCGaggleEncoding`等)に対応。bpe-rs側の非公開gaggle関数自体は直接テストせず、公開されている一段上の`ACDepthEncoder`/`DCEntropyEncoder`を直接呼び出しで駆動（`gen_ac_depth_vectors.c`120通り・`gen_dc_entropy_vectors.c`75通り新設）、不一致なし。この過程でDC側テストベクタ生成時に`CustomWtLL3_2bits`（`HeaderInilization`が常に3にハードコードする値、0ではない）をジェネレータが誤って0初期化していたことが判明したが、bpe-rs本体（`dc_entropy_encoder`）は最初から正しく実装済みと確認——テストハーネス側の初期化漏れであり実装バグではなかった。 |
| 2026-07-29 | `e0b8ba7`（このラウンドの変更を作った時点の親コミット、pre-commit） | `fb494d3`（変更なし、bpe-rs側にコード変更なし） | 166/166 PASS（変更なし） | 9/9 PASS（変更なし） | 95.14% / 100.00%（変更なし） | 86.79% / 100.00%（変更なし） | float DWTの1ULP残存差（5/28件）のみ、解消せず | §7項目13の残り2項目に着手。(1) float DWTの1ULP残差(§3.3)解消を実測で試行: `-ffp-contract=off`（このビルドはgccデフォルトでFMA無効なため無関係と確認）・`-O0`（コンパイル時定数畳み込み仮説を検証、むしろ悪化したため棄却）いずれも未解決、既知の限界のまま(§3.3追記)。(2) ミューテーションテスト(§3.9)を`cargo-mutants`で実施——自動実行中にバックグラウンドタスク追跡の不具合でプロセスが検知漏れのまま動き続け`bpe-rs`のソースを変異させたまま放置する事故が発生、`kill`と`git status`確認で実害なく復旧。自動実行(1131変異、`--include-ignored`なし)は655件未検出だったが、その中から5件を手動で実ソースに適用し`--include-ignored`込みで再検証したところ4/5は共有ベクタ試験で検出、残り1件は出力バイト列を変えない数学的に等価な変異と判明——試験群の検出力は自動実行の数字が示すより高いと確認できた。bpe-rs側のコード変更は無し（検証のみ、全て復元済み）。 |
