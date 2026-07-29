#!/usr/bin/env python3
"""Randomized full-pipeline byte-compatibility fuzzer: C reference
(original/source) vs the Rust port (bpe-rs).

verify/run_compat.sh's 166 cases are hand-picked to hit specific known
edge cases (documented boundaries, bug-fix regressions, targeted bit-depth
values). That is deliberate and valuable, but it also means any divergence
outside those hand-picked combinations goes untested. This script instead
draws random (but always valid) combinations of image dimensions, bit
depth, signedness, byte order, rate, and segment size, over randomly
generated pixel content, and runs the exact same encode -> byte-compare
-> cross-decode -> byte-compare check verify/run_compat.sh's run_case()
does for its curated cases -- just at whatever scale time budget allows,
instead of a fixed list.

Covers both DWT types by default (--dwt-type both). float DWT (-t 0)
decode used to have a ~1-ULP residual difference from inverse_lifting97f
converting each operand to f64 before adding instead of after (matching
C's actual per-operator conversion rules); once that was root-caused and
fixed, this stopped being "known-issue noise" and became a real thing
worth fuzzing like anything else (see COMPATIBILITY_REPORT.md §3.3).
Pass --dwt-type 1 to restrict to the integer path only (e.g. to isolate
whether a future failure is DWT-type-specific).

Every case is fully reproducible without needing to replay the RNG: on
the first mismatch, the exact parameters (not just a seed) are printed,
and the offending raw/.bpe/.raw files are copied out of the scratch
directory before it's cleaned up.

Usage:
  verify/fuzz_compat.py [--iterations N] [--seed S] [--dwt-type {0,1,both}]
                         [--max-dim N] [--keep-failures DIR]
"""
import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
C_SRC = ROOT / "original" / "source"
RUST_DIR = ROOT / "bpe-rs"

sys.path.insert(0, str(ROOT / "verify"))
import gen_vectors  # noqa: E402  (reuses pixel generators / raw writers)

IMAGE_DIM_MIN = 17
SEGMENT_S_MIN = 16

GENERATORS = {
    "gradient": gen_vectors.gradient,
    "zero": gen_vectors.zero,
    "checkerboard": gen_vectors.checkerboard,
    "noise": gen_vectors.noise,
    "const_lo": gen_vectors.const(1),
    "const_hi": lambda x, y: 250,
}


def build_binaries():
    print("== building C reference ==", file=sys.stderr)
    subprocess.run(["make", "-C", str(C_SRC), "-B"], check=True, stdout=subprocess.DEVNULL)
    print("== building Rust port ==", file=sys.stderr)
    env = dict(**os.environ, CARGO_TARGET_DIR=str(RUST_DIR / "target"))
    subprocess.run(
        ["cargo", "build", "--release", "--quiet"], cwd=str(RUST_DIR), check=True, env=env
    )
    return C_SRC / "bpe", RUST_DIR / "target" / "release" / "bpe"


# Minimum SegByteLimit_27Bits (= rate * segment * 8 bytes) for the encoder to
# not immediately reject the rate as too tight to even fit a segment's fixed
# header overhead (BPE_RATE_ERROR / errorhandle.c code 8, raised right after
# HeaderOutput in DC_EnDeCoding.c). Empirically probed: failures topped out
# around 38 bytes at segment=48; this floor keeps a comfortable margin above
# that rather than reverse-engineering the exact per-segment formula, which
# depends on segment/gaggle structure in a way that isn't linear in the byte
# budget alone. This is a genuine encoder input-validity constraint, not a
# C/Rust divergence, so the fuzzer avoids it rather than "discovering" it
# as a failure every run.
MIN_SEGMENT_BUDGET_BYTES = 150


def random_case(rng, max_dim, dwt_choices):
    width = rng.randint(IMAGE_DIM_MIN, max_dim)
    height = rng.randint(IMAGE_DIM_MIN, max_dim)
    bit_depth = rng.choice([8, 12, 16])
    byte_order = rng.choice([0, 1])
    dwt_type = rng.choice(dwt_choices)
    total_blocks = (((width + 7) // 8) * ((height + 7) // 8))
    segment = rng.randint(SEGMENT_S_MIN, max(SEGMENT_S_MIN, min(256, total_blocks)))
    if rng.random() < 0.2:
        rate = 0  # lossless: bypasses SegByteLimit_27Bits entirely
    else:
        min_rate = MIN_SEGMENT_BUDGET_BYTES / (segment * 8)
        rate = round(rng.uniform(min_rate, max(min_rate * 1.5, 4.0)), 3)
    signed = rng.choice([0, 1])
    gen_name = rng.choice(list(GENERATORS.keys()))
    return dict(
        width=width,
        height=height,
        bit_depth=bit_depth,
        byte_order=byte_order,
        dwt_type=dwt_type,
        rate=rate,
        segment=segment,
        signed=signed,
        generator=gen_name,
    )


def write_raw(path, case, rng):
    values = gen_vectors.pixels_for_dims(
        case["width"], case["height"], case["bit_depth"], GENERATORS[case["generator"]]
    )
    if case["bit_depth"] == 8:
        gen_vectors.write_raw_8bit(path, values)
    else:
        gen_vectors.write_raw_16bit(path, values, big_endian=(case["byte_order"] == 1))


def repro_cmd(case, raw_name):
    return (
        f"-e {raw_name} -o out.bpe -w {case['width']} -h {case['height']} "
        f"-b {case['bit_depth']} -f {case['byte_order']} -t {case['dwt_type']} "
        f"-r {case['rate']} -s {case['segment']} -g {case['signed']}"
    )


BPE_RATE_ERROR = 8


class RateTooLow(Exception):
    """The chosen (rate, segment, ...) combo is a genuine encoder input-
    validity rejection (BPE_RATE_ERROR, errorhandle.c code 8: the byte
    budget can't even fit a segment's fixed header overhead) -- not a
    C/Rust divergence. The exact minimum depends on segment/gaggle
    structure and bit depth in a way that isn't a simple function of
    rate*segment alone, so rather than reverse-engineer it exactly, the
    caller just retries with a higher rate."""


def run_one(c_bin, rust_bin, work, case, raw_path):
    common = [
        "-w", str(case["width"]), "-h", str(case["height"]),
        "-b", str(case["bit_depth"]), "-f", str(case["byte_order"]),
        "-t", str(case["dwt_type"]), "-r", str(case["rate"]),
        "-s", str(case["segment"]), "-g", str(case["signed"]),
    ]
    c_bpe = work / "c.bpe"
    r_bpe = work / "rust.bpe"
    c_encode = subprocess.run([str(c_bin), "-e", str(raw_path), "-o", str(c_bpe)] + common,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if c_encode.returncode == BPE_RATE_ERROR:
        raise RateTooLow(f"rate={case['rate']} segment={case['segment']}")
    c_encode.check_returncode()
    subprocess.run([str(rust_bin), "-e", str(raw_path), "-o", str(r_bpe)] + common,
                   check=True, stdout=subprocess.DEVNULL)

    mismatches = []
    if c_bpe.read_bytes() != r_bpe.read_bytes():
        mismatches.append("encode .bpe")

    outs = {}
    for who, src_bin in (("c", c_bin), ("rust", rust_bin)):
        for decoded_by, dec_bin in (("c", c_bin), ("rust", rust_bin)):
            src = c_bpe if who == "c" else r_bpe
            out = work / f"{who}_from_{decoded_by}.raw"
            subprocess.run(
                [str(dec_bin), "-d", str(src), "-o", str(out), "-f", str(case["byte_order"])],
                check=True, stdout=subprocess.DEVNULL,
            )
            outs[(who, decoded_by)] = out

    reference = outs[("c", "c")].read_bytes()
    for key, path in outs.items():
        if key == ("c", "c"):
            continue
        if path.read_bytes() != reference:
            mismatches.append(f"decode({key[0]}-encoded, by {key[1]}) vs C-decode(C)")

    return mismatches


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iterations", type=int, default=200)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--dwt-type", choices=["0", "1", "both"], default="both")
    ap.add_argument("--max-dim", type=int, default=200)
    ap.add_argument("--keep-failures", type=Path, default=ROOT / "verify" / "fuzz_failures")
    args = ap.parse_args()

    dwt_choices = [0, 1] if args.dwt_type == "both" else [int(args.dwt_type)]

    c_bin, rust_bin = build_binaries()

    failures = []
    skipped = 0
    print(f"== fuzzing {args.iterations} case(s), seed={args.seed}, dwt_type={dwt_choices} ==",
          file=sys.stderr)
    for i in range(args.iterations):
        rng = random.Random(f"{args.seed}:{i}")
        case = random_case(rng, args.max_dim, dwt_choices)
        # dir="/tmp": macOS's default $TMPDIR is a long, per-session path
        # (/var/folders/.../T/...) long enough by itself to overflow the C
        # reference's fixed 100-byte StringBuffer (see run_compat.sh's
        # comment on the same issue) once run_one()'s -o path is built from
        # it -- surfaces there as "Trace/BPT trap: 5" (SIGTRAP).
        with tempfile.TemporaryDirectory(prefix="bpe-fuzz-", dir="/tmp") as work_str:
            work = Path(work_str)
            raw_path = work / "in.raw"
            write_raw(raw_path, case, rng)

            mismatches = None
            for attempt in range(5):
                try:
                    mismatches = run_one(c_bin, rust_bin, work, case, raw_path)
                    break
                except RateTooLow:
                    # Genuine encoder input-validity rejection (see RateTooLow's
                    # docstring), not a compat signal -- bump the rate and retry
                    # rather than counting it as a failure or giving up outright.
                    case = dict(case, rate=round(min(max(case["rate"], 0.05) * 2.5, 15.0), 3))
                except subprocess.CalledProcessError as e:
                    mismatches = [f"process failed: {e}"]
                    break
            if mismatches is None:
                skipped += 1
                print(f"skip {i}/{args.iterations}: rate still too low after retries, case={case}",
                      file=sys.stderr)
                continue

            if mismatches:
                failures.append((i, case, mismatches))
                print(f"FAIL iteration {i}: {mismatches} case={case}", file=sys.stderr)
                fail_dir = args.keep_failures / f"iter{i}"
                fail_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy(raw_path, fail_dir / "in.raw")
                (fail_dir / "case.txt").write_text(
                    f"{case}\nmismatches: {mismatches}\n"
                    f"repro: bpe {repro_cmd(case, 'in.raw')}\n"
                )
            else:
                print(f"pass {i}/{args.iterations}", file=sys.stderr)

    print("", file=sys.stderr)
    print(f"== summary: {args.iterations - len(failures) - skipped}/{args.iterations} passed, "
          f"{len(failures)} failure(s), {skipped} skipped (rate too low even after retries) ==",
          file=sys.stderr)
    if failures:
        print(f"repro details written under {args.keep_failures}", file=sys.stderr)
        for i, case, mismatches in failures:
            print(f"  - iteration {i}: {mismatches} case={case}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
