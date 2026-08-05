# bpe-c-comparison — CCSDS BPE C/Rust Compatibility Verification Repository

[日本語版](README.ja.md)

A repository that continuously verifies compatibility between `original/` (the C reference implementation of the CCSDS 122.0 Bit Plane Encoder from U. Nebraska, bug-fixed version by Aaron Kiely) and `bpe-rs/` (its Rust port, [isas-yamamoto/bpe-rs](https://github.com/isas-yamamoto/bpe-rs)), through extensive testing, exhaustive function-level verification, and code coverage measurement. **This is not a formal proof for all possible inputs** — it is a high-confidence empirical confirmation over the tested range. See "How far is compatible, and where do differences start" below and [COMPATIBILITY_REPORT.md](COMPATIBILITY_REPORT.md) §5 "Overall assessment" for the scope of the guarantee, known limitations, and further ways to increase confidence.

## How far is compatible, and where do differences start

| Category | Scope | Status |
|---|---|---|
| **Confirmed fully compatible** | Full-pipeline 166/166 cases across both integer and float DWT / Exhaustive function-level verification 9/9 for Rice, two's-complement conversion, DPCM (DC & AC), pattern mapping, `AdjustOutPut`, `CodingOptions`, `ACDepthEncoder`/`Decoder`, `DCEntropyEncoder`/`Decoder` / Randomized fuzz testing, both DWT types (continuously run in CI) / 8 externally reported bug items from `readme_kielymods` / Effective coverage 100.00% (both line and branch) across all 15 core files | **0** mismatches |
| **Known differences** | None | — |
| **Out of scope** | Error paths such as file I/O failures, configuration items structurally unreachable from the CLI (356 lines of confirmed dead code) | Intentionally out of scope |

For the detailed rationale, test contents, and coverage evaluation, see **[COMPATIBILITY_REPORT.md](COMPATIBILITY_REPORT.md)**. Comparisons and trends across past full-verification rounds are recorded in **[verify/results/history.md](verify/results/history.md)**.

## Layout

```
original/source/   C reference implementation (unmodified except for BPE_TRACE build hooks. Third-party code, see NOTICE)
bpe-rs/             Rust port (git submodule)
verify/             The verification harness
.github/workflows/  CI (automatic verification on every push/PR)
```

Exactly what each test verifies and from what angle (the breakdown of the 166 cases, the targets of exhaustive function-level verification, the coverage evaluation method) is documented one by one in **[COMPATIBILITY_REPORT.md](COMPATIBILITY_REPORT.md)** §1 "Verification perspectives" and §2 "Test contents and results". This README covers only the summary.

## Running locally

```sh
git submodule update --init --recursive   # first time only
verify/run_compat.sh
verify/run_unit_vectors.py
verify/fuzz_compat.py --iterations 500    # randomized fuzz test (optional, reproducible via --seed)
```

## CI

`.github/workflows/compat.yml` automatically runs `verify/run_compat.sh` (full-pipeline byte compatibility), `verify/run_unit_vectors.py` (function-level cross-checks), and `verify/fuzz_compat.py --iterations 50 --seed 0` (lightweight fuzz smoke test) on every push and PR. The >2^15-blocks-per-segment regression case and the 5000-case deep fuzz (`deep-fuzz` job, random seed) only run on a weekly schedule or manual dispatch (low frequency, low risk).

`.github/workflows/bpe-rs-poll.yml` checks bpe-rs's `main` daily, and if it has moved past the submodule's pin, runs the same verification and automatically opens a submodule-bump PR if it passes (on failure, no PR is opened and the workflow simply fails). See [verify/results/history.md](verify/results/history.md) for details.

## Real mismatches found so far

A float DWT rounding error and two integer-width bugs in DPCM DC mapping were found and fixed. See "Discovered mismatches and their resolution" in [INVESTIGATION_LOG.md](INVESTIGATION_LOG.md) for the detailed root-cause analysis.

## License

`original/` is third-party licensed code, and its license differs from the rest of this repository (original work such as `verify/` and `COMPATIBILITY_REPORT.md`). See **[NOTICE](NOTICE)** for the breakdown, provenance, and rights of each component.
