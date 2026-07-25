#!/usr/bin/env python3
"""Stage-boundary trace diff: pinpoints exactly which pipeline stage a C/Rust
divergence first appears in, instead of only knowing the final .bpe differs.

Builds original/source/bpe_trace (-DBPE_TRACE) and the normal bpe-rs release
binary (whose stage dumps are always compiled in, gated at runtime by the
BPE_TRACE_DIR env var -- see bpe-rs/src/trace.rs), runs both encoders on the
same input with BPE_TRACE_DIR set, and diffs each stage file.

Seams currently traced (see original/source/waveletbpe.c and bpe_encoder.c,
and bpe-rs/src/wavelet/mod.rs and encoder.rs for the call sites):
  - dwt_forward: the transformed image immediately after CoeffRegroup(F97),
    i.e. exactly what feeds into BuildBlockString.
  - block_string: the 8x8-block-reordered array immediately before the
    per-segment DC/AC entropy coding loop.

This intentionally stops at the wavelet/block-reorder boundary rather than
also tracing per-gaggle DC/AC internals: those primitives (Rice coding, DPCM
mapping, two's-complement conversion, pattern mapping) already have
exhaustive shared-vector function-level tests (verify/run_unit_vectors.py),
which give stronger, more precise evidence than a coarse stage dump would.
If a full-pipeline byte mismatch survives past block_string with no
upstream trace divergence, the bug is somewhere in that entropy-coding
stage and warrants extending this script with an additional seam there.

Usage: verify/compare_traces.py <raw_image> <width> <height> [bpe args...]
Example: verify/compare_traces.py testdata/baseline_256.raw 256 256 -t 0 -r 0
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
C_SRC = ROOT / "original" / "source"
RUST_DIR = ROOT / "bpe-rs"

SEAMS = [
    ("dwt_forward", "dwt_forward_c.txt", "dwt_forward_rust.txt"),
    ("block_string", "block_string_c.txt", "block_string_rust.txt"),
]


def build():
    subprocess.run(["make", "-C", str(C_SRC), "-B", "bpe_trace"], check=True, capture_output=True)
    subprocess.run(
        ["cargo", "build", "--release", "--quiet"],
        cwd=str(RUST_DIR),
        check=True,
        env={"CARGO_TARGET_DIR": str(RUST_DIR / "target"), **__import__("os").environ},
    )


def first_divergence(path_a, path_b):
    lines_a = path_a.read_text().splitlines()
    lines_b = path_b.read_text().splitlines()
    if len(lines_a) != len(lines_b):
        return f"length mismatch: C={len(lines_a)} Rust={len(lines_b)}"
    for idx, (a, b) in enumerate(zip(lines_a, lines_b)):
        if a != b:
            return f"index {idx}: C={a} Rust={b}"
    return None


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    raw, width, height = sys.argv[1], sys.argv[2], sys.argv[3]
    extra_args = sys.argv[4:]

    build()
    c_bin = C_SRC / "bpe_trace"
    rust_bin = RUST_DIR / "target" / "release" / "bpe"

    with tempfile.TemporaryDirectory() as trace_dir:
        common = ["-w", width, "-h", height, "-o", f"{trace_dir}/out.bpe"] + extra_args
        subprocess.run(
            [str(c_bin), "-e", raw] + common,
            env={"BPE_TRACE_DIR": trace_dir, **__import__("os").environ},
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [str(rust_bin), "-e", raw] + common,
            env={"BPE_TRACE_DIR": trace_dir, **__import__("os").environ},
            check=True,
            capture_output=True,
        )

        trace_path = Path(trace_dir)
        diverged = False
        for label, c_file, rust_file in SEAMS:
            c_path, rust_path = trace_path / c_file, trace_path / rust_file
            if not c_path.exists() or not rust_path.exists():
                print(f"{label}: SKIP (trace file missing)")
                continue
            diff = first_divergence(c_path, rust_path)
            if diff is None:
                print(f"{label}: MATCH")
            else:
                print(f"{label}: DIVERGES ({diff})")
                diverged = True

        return 1 if diverged else 0


if __name__ == "__main__":
    sys.exit(main())
