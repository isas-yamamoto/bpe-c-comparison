#!/usr/bin/env python3
"""Generate synthetic raw test images for the C/Rust BPE compatibility matrix.

Covers the normal path plus every edge case documented in
original/readme_kielymods.rtf (single-trailing-block segment, all-zero image,
block-count boundaries around GAGGLE_SIZE=16, endianness, signed pixels).

Block-count boundary cases need BOTH real dimensions >= 17 (IMAGE_WIDTH_MIN /
IMAGE_ROWS_MIN in original/source/global.h) AND a specific total post-padding
8x8 block count. Since padded-rows/8 and padded-cols/8 must each be >= 3 to
satisfy the >=17 minimum, a prime target block count with no factor pair
having both factors >= 3 (e.g. 17, 31) is geometrically impossible -- this
script detects that and bumps to the next feasible count, recording the
substitution in the manifest instead of silently skipping it.

Output: <repo_root>/testdata/<case_name>.raw + testdata/manifest.json
"""
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTDIR = ROOT / "testdata"
BLOCK_SIZE = 8


def dims_for_block_count(target):
    """Find (block_rows, block_cols) with block_rows*block_cols == n >= target,
    both factors >= 3, bumping n upward if target itself is infeasible.
    Prefers the factor pair closest to a square, for a more realistic image
    shape and faster wavelet transform than a maximally thin rectangle."""
    n = target
    while True:
        best = None
        for block_rows in range(3, int(n ** 0.5) + 1):
            if n % block_rows == 0 and n // block_rows >= 3:
                best = (block_rows, n // block_rows)
        if best is None:
            for block_rows in range(3, n + 1):
                if n % block_rows == 0 and n // block_rows >= 3:
                    best = (block_rows, n // block_rows)
                    break
        if best is not None:
            return n, best[0], best[1]
        n += 1


def pixels_for_dims(width, height, bit_depth, generator):
    max_val = (1 << bit_depth) - 1
    return [generator(x, y) & max_val for y in range(height) for x in range(width)]


def gradient(x, y):
    return (x * 3 + y * 5) // 2


def zero(x, y):
    return 0


def checkerboard(x, y):
    # Sharp high-frequency alternation: produces large-magnitude AC
    # wavelet coefficients of both signs (a smooth gradient skews toward
    # small/one-signed coefficients), for exercising AdjustOutPut's
    # positive/negative/zero coefficient branches under rate limiting.
    return 255 if (x // 4 + y // 4) % 2 == 0 else 0


def write_raw_8bit(path, values):
    path.write_bytes(bytes(values))


def write_raw_16bit(path, values, big_endian):
    fmt = ">H" if big_endian else "<H"
    with open(path, "wb") as f:
        for v in values:
            f.write(struct.pack(fmt, v))


def build_case(name, width, height, bit_depth=8, byte_order=0, generator=gradient, note=""):
    values = pixels_for_dims(width, height, bit_depth, generator)
    raw_path = OUTDIR / f"{name}.raw"
    if bit_depth == 8:
        write_raw_8bit(raw_path, values)
    else:
        write_raw_16bit(raw_path, values, big_endian=(byte_order == 1))
    return {
        "name": name,
        "raw": raw_path.name,
        "width": width,
        "height": height,
        "bit_depth": bit_depth,
        "byte_order": byte_order,
        "note": note,
    }


def main():
    include_slow = "--include-slow" in sys.argv
    OUTDIR.mkdir(exist_ok=True)
    cases = []

    # --- baseline ---
    cases.append(build_case("baseline_256", 256, 256, note="baseline gradient"))

    # --- all-zero image (leftmost==1 special-case bug regression) ---
    cases.append(build_case("all_zero_64", 64, 64, generator=zero, note="all-zero image"))

    # --- high-contrast checkerboard (both-signed, large-magnitude AC coefficients) ---
    cases.append(build_case("checkerboard_256", 256, 256, generator=checkerboard, note="high-contrast checkerboard"))

    # --- minimal image size (IMAGE_WIDTH_MIN / IMAGE_ROWS_MIN == 17) ---
    cases.append(build_case("minimal_17x17", 17, 17, note="minimum valid dimensions"))

    # --- single-trailing-block segment repro from readme_kielymods.rtf ---
    cases.append(build_case("single_trailing_block_48x24", 48, 24, note="use with -s 17: last segment = 1 block"))

    # --- total block-count boundaries around GAGGLE_SIZE (16) ---
    boundary_notes = []
    for target in (15, 16, 17, 31, 32, 33):
        n, block_rows, block_cols = dims_for_block_count(target)
        width = block_cols * BLOCK_SIZE
        height = block_rows * BLOCK_SIZE
        name = f"blocks_{target}" if n == target else f"blocks_{target}_as_{n}"
        note = f"target block count {target}"
        if n != target:
            note += f" infeasible (needs two factors >=3), substituted {n} ({block_rows}x{block_cols} blocks)"
            boundary_notes.append(note)
        cases.append(build_case(name, width, height, note=note))

    # --- 16-bit pixels x endianness ---
    for byte_order in (0, 1):
        cases.append(
            build_case(
                f"pixels16_f{byte_order}",
                32,
                32,
                bit_depth=16,
                byte_order=byte_order,
                note=f"16-bit pixels, -f {byte_order}",
            )
        )

    # --- signed pixels (-g 1) uses same raw data as baseline; case list in run_compat.sh adds -g ---
    cases.append(build_case("signed_32", 32, 32, note="use with -g 1"))

    # --- signed 16-bit pixels: ImageWrite/ImageWriteFloat's signed-16bit
    # branch is otherwise never exercised (signed_32 above is 8-bit only) ---
    cases.append(
        build_case(
            "signed16_32",
            32,
            32,
            bit_depth=16,
            note="use with -g 1: exercises the signed 16-bit pixel path",
        )
    )

    # --- non-power bit depth (12-bit values in 16-bit words): exercises the
    # PixelBitDepth_4Bits != 0 branch (pixels16 above always uses the
    # PixelBitDepth_4Bits == 0 "default 16-bit" branch, since -b 16 doesn't
    # fit in that 4-bit header field and wraps to 0) ---
    cases.append(
        build_case(
            "pixels12_f0",
            32,
            32,
            bit_depth=12,
            note="exercises the explicit (non-zero) PixelBitDepth_4Bits branch",
        )
    )

    if include_slow:
        # --- >2^15 blocks in a single segment (regression guard for the short-int overflow bug) ---
        n, block_rows, block_cols = dims_for_block_count(33000)
        width = block_cols * BLOCK_SIZE
        height = block_rows * BLOCK_SIZE
        cases.append(
            build_case(
                "large_segment_slow",
                width,
                height,
                note=f"{n} blocks ({block_rows}x{block_cols}) in one segment; use with -s {n}",
            )
        )

    manifest = {"cases": cases, "notes": boundary_notes}
    (OUTDIR / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(f"Generated {len(cases)} test images into {OUTDIR}")
    for note in boundary_notes:
        print("NOTE:", note)


if __name__ == "__main__":
    main()
