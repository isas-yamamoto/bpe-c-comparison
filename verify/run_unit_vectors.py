#!/usr/bin/env python3
"""Function-level shared-vector check: builds the C reference's individual
coding primitives (Rice coding, two's-complement conversion) as standalone
programs linked against the real original/source object files, has them
generate verify/vectors/*.txt, then runs bpe-rs's own test suite -- which
has inline tests (in src/rice.rs and src/dc/twos_comp.rs) that read those
same files and assert their own encode of the identical input sequence is
byte-exact against what the C reference produced.

This is deliberately function-level, not full-pipeline: it exercises every
representable value for each (bit_length, option) Rice combo and every
two's-complement width, independent of whatever values a synthetic test
image happens to produce through the full encode/decode pipeline.

Usage: verify/run_unit_vectors.py
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
C_SRC = ROOT / "original" / "source"
C_TESTS = ROOT / "verify" / "c_unit_tests"
VECTORS_DIR = ROOT / "verify" / "vectors"
RUST_DIR = ROOT / "bpe-rs"

RICE_OBJS = ["ricecoding.c", "bitsIO.c", "errorhandle.c"]
TWOSCOMP_OBJS = [
    "DC_EnDeCoding.c",
    "errorhandle.c",
    "header.c",
    "waveletbpe.c",
    "lifting_97M.c",
    "lifting_97f.c",
    "CoeffGroup.c",
    "bitsIO.c",
    "ricecoding.c",
]


def build_and_run(gen_name, extra_sources, out_name):
    work = VECTORS_DIR / "_build"
    work.mkdir(parents=True, exist_ok=True)
    binary = work / gen_name
    cmd = ["gcc", "-O2", "-Wall", "-I", str(C_SRC), str(C_TESTS / f"{gen_name}.c")]
    cmd += [str(C_SRC / s) for s in extra_sources]
    cmd += ["-o", str(binary), "-lm"]
    subprocess.run(cmd, check=True)
    VECTORS_DIR.mkdir(exist_ok=True)
    subprocess.run([str(binary), str(VECTORS_DIR / out_name)], check=True)
    print(f"generated {out_name}")


def main():
    build_and_run("gen_rice_vectors", RICE_OBJS, "rice_vectors.txt")
    build_and_run("gen_twoscomp_vectors", TWOSCOMP_OBJS, "twoscomp_vectors.txt")

    print("== running bpe-rs shared-vector tests ==")
    result = subprocess.run(
        ["cargo", "test", "--quiet", "shared_vectors", "--", "--include-ignored"],
        cwd=str(RUST_DIR),
    )
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
