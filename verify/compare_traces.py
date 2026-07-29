#!/usr/bin/env python3
"""Stage-boundary trace diff: pinpoints exactly which pipeline stage a C/Rust
divergence first appears in, instead of only knowing the final .bpe or raw
output differs.

Builds original/source/bpe_trace (-DBPE_TRACE) and the normal bpe-rs release
binary (whose stage dumps are always compiled in, gated at runtime by the
BPE_TRACE_DIR env var -- see bpe-rs/src/trace.rs), encodes with C, then runs
both encode and decode (of the C-produced stream) with BPE_TRACE_DIR set,
and diffs each stage file.

Seams currently traced (see original/source/waveletbpe.c, bpe_encoder.c,
bpe_decoder.c, and bpe-rs/src/wavelet/mod.rs, encoder.rs, decoder.rs for the
call sites):
  encode side (integer, exact text match):
  - dwt_forward: the transformed image immediately after CoeffRegroup(F97),
    i.e. exactly what feeds into BuildBlockString.
  - block_string: the 8x8-block-reordered array immediately before the
    per-segment DC/AC entropy coding loop.
  decode side (float, numeric match -- see NOTE on floating precision below):
  - adjust_output: per-segment post-AdjustOutPut/adjust_output coefficient
    values (appended across segments).
  - reassembled: the full-image coefficient array right before
    CoeffDegroupFloating/coeff_degroup_floating.
  - post_idwt_level2, post_idwt_level1: the image after each of the inverse
    9/7 lifting's 3 levels (levels=3 fixed) finishes, coarsest first --
    bisects which level first introduces a divergence between reassembled
    (pre-transform) and post_idwt (post-transform). See lifting_97f.c /
    lifting97f.rs.
  - post_idwt: the image right after DWT_ReverseFloating/dwt_reverse_floating,
    before ImageWriteFloat/image_write_float (same array level0's lifting
    pass leaves behind, so there's no separate post_idwt_level0 seam).

This intentionally stops short of tracing per-gaggle DC/AC internals: those
primitives (Rice coding, DPCM mapping, two's-complement conversion, pattern
mapping) already have exhaustive shared-vector function-level tests
(verify/run_unit_vectors.py), which give stronger, more precise evidence
than a coarse stage dump would. If a full-pipeline byte mismatch survives
past block_string (encode) or reassembled (decode) with no upstream trace
divergence, the bug is somewhere in that entropy-coding stage and warrants
extending this script with an additional seam there.

NOTE on floating precision (decode-side seams only): these compare `f32`
values numerically, not as exact text (C's `%.9e` and Rust's `{:.9e}`
format exponents differently -- `e+01` vs `e1` -- so a naive text diff
would flag every line as different even when numerically identical). Exact
numeric equality is the default and always holds now: a rate-limited
float-DWT (`-t 0`) decode used to occasionally hit a 1-ULP difference here,
which was originally misdiagnosed as an unresolvable gcc/rustc rounding
difference but turned out to be a real f32-vs-f64 addition-order bug in
inverse_lifting97f (see INVESTIGATION_LOG.md §3.3), since fixed. --float-tol
is kept around for chasing any future divergence of this kind, not because
one is currently expected.

Usage: verify/compare_traces.py <raw_image> <width> <height> [--float-tol=N] [bpe args...]
Example: verify/compare_traces.py testdata/baseline_256.raw 256 256 -t 0 -r 0.1 -s 64
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
C_SRC = ROOT / "original" / "source"
RUST_DIR = ROOT / "bpe-rs"

ENCODE_SEAMS = [
    ("dwt_forward", "dwt_forward_c.txt", "dwt_forward_rust.txt", "int"),
    ("block_string", "block_string_c.txt", "block_string_rust.txt", "int"),
]
DECODE_SEAMS = [
    ("adjust_output", "adjust_output_c.txt", "adjust_output_rust.txt", "float"),
    ("reassembled", "reassembled_c.txt", "reassembled_rust.txt", "float"),
    # Only meaningful for float DWT (-t 0): dumped after each of the inverse
    # 9/7 lifting's 3 levels finishes (levels=3 is fixed), coarsest first.
    # level0's post-state is the same array post_idwt already dumps, so
    # there's no separate post_idwt_level0 file -- see lifting_97f.c /
    # lifting97f.rs. Added to bisect what used to be a 1-ULP gcc/rustc
    # decode divergence (INVESTIGATION_LOG.md §3.3): previously
    # reassembled matched but post_idwt didn't, with no visibility into
    # which of the 3 levels' lifting calls was responsible. That divergence
    # is fixed now (turned out to be a real f32-vs-f64 addition-order bug
    # in inverse_lifting97f, not a compiler difference), but these seams
    # stay as general-purpose infrastructure for the next such investigation.
    ("post_idwt_level2", "post_idwt_level2_c.txt", "post_idwt_level2_rust.txt", "float"),
    ("post_idwt_level1", "post_idwt_level1_c.txt", "post_idwt_level1_rust.txt", "float"),
    ("post_idwt", "post_idwt_c.txt", "post_idwt_rust.txt", "float"),
]


def build():
    subprocess.run(["make", "-C", str(C_SRC), "-B", "bpe_trace"], check=True, capture_output=True)
    subprocess.run(
        ["cargo", "build", "--release", "--quiet"],
        cwd=str(RUST_DIR),
        check=True,
        env={"CARGO_TARGET_DIR": str(RUST_DIR / "target"), **os.environ},
    )


def first_divergence(path_a, path_b, kind, float_tol):
    lines_a = path_a.read_text().splitlines()
    lines_b = path_b.read_text().splitlines()
    if len(lines_a) != len(lines_b):
        return f"length mismatch: C={len(lines_a)} Rust={len(lines_b)}"
    for idx, (a, b) in enumerate(zip(lines_a, lines_b)):
        if kind == "int":
            if a != b:
                return f"index {idx}: C={a} Rust={b}"
        else:
            fa, fb = float(a), float(b)
            if abs(fa - fb) > float_tol:
                return f"index {idx}: C={fa!r} Rust={fb!r} (diff={fa - fb!r})"
    return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--float-tol")]
    float_tol = 0.0
    for a in sys.argv[1:]:
        if a.startswith("--float-tol="):
            float_tol = float(a.split("=", 1)[1])

    if len(args) < 3:
        print(__doc__)
        return 1
    raw, width, height = args[0], args[1], args[2]
    extra_args = args[3:]

    build()
    c_bin = C_SRC / "bpe_trace"
    rust_bin = RUST_DIR / "target" / "release" / "bpe"

    # dir="/tmp": see run_compat.sh's comment -- macOS's default $TMPDIR is
    # long enough on its own to overflow the C reference's fixed 100-byte
    # StringBuffer once c_bpe below is passed as a -o path.
    with tempfile.TemporaryDirectory(dir="/tmp") as trace_dir:
        env = {"BPE_TRACE_DIR": trace_dir, **os.environ}
        common = ["-w", width, "-h", height] + extra_args
        c_bpe = f"{trace_dir}/out_c.bpe"

        subprocess.run(
            [str(c_bin), "-e", raw, "-o", c_bpe] + common, env=env, check=True, capture_output=True
        )
        subprocess.run(
            [str(rust_bin), "-e", raw, "-o", f"{trace_dir}/out_rust.bpe"] + common,
            env=env,
            check=True,
            capture_output=True,
        )

        trace_path = Path(trace_dir)
        diverged = False

        for label, c_file, rust_file, kind in ENCODE_SEAMS:
            c_path, rust_path = trace_path / c_file, trace_path / rust_file
            if not c_path.exists() or not rust_path.exists():
                print(f"{label}: SKIP (trace file missing)")
                continue
            diff = first_divergence(c_path, rust_path, kind, float_tol)
            if diff is None:
                print(f"{label}: MATCH")
            else:
                print(f"{label}: DIVERGES ({diff})")
                diverged = True

        # Decode-side seams need an already-encoded stream; decode the
        # C-produced one with both implementations.
        subprocess.run(
            [str(c_bin), "-d", c_bpe, "-o", f"{trace_dir}/c_from_c.raw"],
            env=env,
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [str(rust_bin), "-d", c_bpe, "-o", f"{trace_dir}/rust_from_c.raw"],
            env=env,
            check=True,
            capture_output=True,
        )

        for label, c_file, rust_file, kind in DECODE_SEAMS:
            c_path, rust_path = trace_path / c_file, trace_path / rust_file
            if not c_path.exists() or not rust_path.exists():
                print(f"{label}: SKIP (trace file missing -- integer DWT doesn't produce these)")
                continue
            diff = first_divergence(c_path, rust_path, kind, float_tol)
            if diff is None:
                print(f"{label}: MATCH")
            else:
                print(f"{label}: DIVERGES ({diff})")
                diverged = True

        return 1 if diverged else 0


if __name__ == "__main__":
    sys.exit(main())
