# Precision vs. Compatibility Trade-offs

[日本語版](PRECISION_TRADEOFFS.ja.md)

As defined at the top of [COMPATIBILITY_REPORT.md](COMPATIBILITY_REPORT.md), the "compatibility" this project verifies is byte-for-byte agreement with this specific C reference implementation's (`original/source/`) actual behavior — not conformance to the CCSDS 122.0 spec. This file lists the concrete places where that principle showed up in code: where a mathematically "more correct" or "more precise" implementation was deliberately rejected in favor of matching C's behavior, including cases where C's own result is the less precise one. The goal is to replace the reference implementation with bpe-rs, so a replacement that returns a different answer isn't an "improvement" — it's incompatibility. That ordering of priorities is consistent throughout.

**Update**: items 1 and 3 below (the two sites that are bpe-rs *behavior*, not a C-side build flag) are no longer unconditional. bpe-rs added a `strict_c_compat` switch — on by default, matching C bit-for-bit exactly as described below — with a `--fix-c-quirks` CLI flag to opt into the corrected/more-precise alternative instead. Everything below describes the default (`strict_c_compat = true`); it's what `verify/run_compat.sh` exercises, since it never passes the flag.

For the detailed root-cause analysis and fix for each item, see the corresponding section of [INVESTIGATION_LOG.md](INVESTIGATION_LOG.md).

## 1. Inverse DWT (float) pairwise addition order (INVESTIGATION_LOG.md §3.3)

- **C's behavior**: in expressions like `0.0406894 * (x[r_idx+1] + x[r_idx-1])` in `inversef97f`, C's "usual arithmetic conversions" apply per-operator, not to the whole expression. So the addition inside the parentheses (both operands `float`) happens **in single precision first**, and only promotes to double precision once multiplied by the unsuffixed (`double`) coefficient.
- **The mathematically "better" alternative**: converting each addend to `f64` before adding (`f(x[r_idx+1]) + f(x[r_idx-1])`) is more precise, since no rounding error is introduced at the addition step.
- **What was chosen**: bpe-rs originally used this more precise order, which produced a rare 1-ULP difference from C (up to 5 of 28 rate values on noise_256). It was **deliberately changed** to match C's order — add in f32, then promote — and that's still the default. A `strict_c_compat` flag (`--fix-c-quirks` on the CLI) now makes the more-precise f64-accumulation order available again, for use when C interop isn't a goal.
- **Trade-off**: mathematical precision is traded away for byte-exact agreement with C by default; `--fix-c-quirks` trades that agreement back for precision.

## 2. Disabling FMA (fused multiply-add) contraction — `-ffp-contract=off` (INVESTIGATION_LOG.md §3.10)

- **C's behavior**: `original/source/makefile` originally didn't specify `-ffp-contract`, leaving it to the compiler's default FP_CONTRACT setting. On targets where FMA is part of the base ISA (ARM64/Apple Silicon, etc.), `a*b+c` gets fused into a single FMA instruction with one rounding step — **more precise** than a separate multiply and add with two roundings.
- **Rust-side constraint**: bpe-rs's `f32`/`f64` `*` and `+` operators are always evaluated separately and never auto-fused (a guarantee of Rust's basic arithmetic operators).
- **What was chosen**: added `-ffp-contract=off` to C's build flags, explicitly disabling FMA fusion on the C side. Affects both the forward path (`forwardf97f`, encoding) and the inverse path (decoding).
- **Trade-off**: on hardware where FMA is available, C could have computed a more precise result — that precision was deliberately given up to match Rust. On the x86-64 baseline target (no FMA instructions at all) this is effectively a no-op, but on ARM64 etc. it actually changes the computed result.

## 3. Reproducing DPCM DC-mapping integer wraparound (INVESTIGATION_LOG.md §3.2)

A two-stage integer overflow that only manifests at DC depth N=16.

- **`-(short)(...)` truncation**: before negating, C narrows the intermediate value to a 16-bit `short`. When that intermediate value is exactly 32768, the `short` cast wraps 32768 → −32768, so the negated value becomes +32768 instead of the mathematically correct −32768.
- **Unsigned wraparound in `theta`**: once `ShiftedDC` is pushed out of range by the above, the subtraction between C's unsigned `DWORD32` values wraps around, and the `min()` macro ends up comparing against an unintended huge value instead of a negative one.
- **What was chosen**: bpe-rs uses `wrapping_sub` to explicitly reproduce this wraparound behavior via `neg_short`/`theta_from_prev` helpers, gated by the same `strict_c_compat` switch as item 1 (default on; `--fix-c-quirks` switches to plain signed arithmetic instead).
- **Trade-off**: not floating-point precision per se, but the same structural choice — "the numerically correct result" is passed over by default in favor of "the same result as C's overflow bug." Unlike item 1, this one also changes what gets written to the bitstream (the mapped value is Rice-coded directly), so `--fix-c-quirks` here means the encoder and decoder no longer produce a format any C-side tool can read, not just a differently-rounded pixel value.

## 4. The `TypeC<<(1<<(3-i))` typo (`BPEBlockCoding.c`, INVESTIGATION_LOG.md §4)

- **C's behavior**: a comparison expression that appears to be a typo for `&` (against a 4-bit field `TypeC`) written as `<<` instead. It's mathematically always true, so the branch it seems to have intended to guard (the "all hits already recorded" case) is structurally unreachable.
- **What was chosen**: left unfixed; the Rust side is treated as a target for reproducing this same unreachable pattern, not for correcting it.
- **Note**: control-flow analysis proved this branch is confirmed dead code — unreachable with any real data — so it **does not actually affect output bytes**. Unlike items 1–3, this isn't an instance of "precision traded for compatibility"; it's listed here for reference as an example of the same underlying policy: don't fix C's defects.

## What's deliberately excluded from this list

- **The float DWT rounding fix (INVESTIGATION_LOG.md §3.1)**: adjusting `(int)(v+0.5)` to add in f64 improved precision **and** agreement with C at the same time (Rust had been computing in single precision, which was both less precise than C and a mismatch). Not an instance of trading away precision, so it's excluded.
- **The `DCGaggleDecoding`/`ACGaggleDecoding` loop-variable overflow fix** (`short i` → `long int i`, COMPATIBILITY_REPORT.md §6): this was a bug fix to the C reference implementation itself by Aaron Kiely; bpe-rs was implemented with the correct width from the start. Not a case where precision and compatibility pulled in different directions.

## Summary

Items 1 and 2 are floating-point rounding/operation-order trade-offs, item 3 is reproducing integer overflow, and item 4 is letting an obvious C typo stand. What they share: whenever "a more accurate computation" would mean "a different answer than C," this project has consistently chosen the latter — matching C, by default. As stated in the scope definition at the top of [COMPATIBILITY_REPORT.md](COMPATIBILITY_REPORT.md), since the goal is to replace the reference implementation, that ordering of priorities is deliberate; items 1 and 3 are opt-out (`--fix-c-quirks`) for callers who value precision over C interop more than compatibility.
