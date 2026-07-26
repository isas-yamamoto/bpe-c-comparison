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


def noise(x, y):
    # Deterministic pseudo-random (no periodicity, unlike checkerboard, and
    # no monotonic structure, unlike gradient), for a third, independent
    # coefficient-distribution shape when hunting AdjustOutPut branches.
    # Plain hash of (x, y), not `random`, so it reproduces identically on
    # every run without a stored seed.
    h = (x * 374761393 + y * 668265263) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFF


def make_single_bump(bx, by, base=128, bump=1):
    # A single perturbed pixel against an otherwise flat field. Where that
    # pixel sits controls the resulting max-AC-coefficient magnitude
    # (BitDepthAC_5Bits) after the wavelet transform -- and thus which of
    # ACDepthEncoder/ACDepthDecoder's N-dependent branches (AC_BitPlaneCoding.c)
    # get exercised. Found empirically (see COMPATIBILITY_REPORT.md): an
    # interior bump lands on BitDepthAC==2 (N==2), while a bump at the image's
    # very last pixel lands on BitDepthAC==1 (the single-bit-plane path that
    # bypasses ACDepthEncoder/Decoder entirely).
    def gen(x, y):
        return base + bump if (x, y) == (bx, by) else base

    return gen


def checkerboard_period1_max(x, y):
    # Sharpest possible spatial frequency (period 1) at full amplitude.
    # Combined with a 16-bit bit depth (see ac_depth5_16bit_64 below) this
    # pushes BitDepthAC_5Bits into the 16-31 range (N==5) -- max-amplitude
    # 8-bit content tops out around BitDepthAC~11 (N==4), never reaching N==5.
    return (1 << 16) - 1 if (x + y) % 2 == 0 else 0


def const(value):
    # DC_EnDeCoding.c's BitDepthDC_5Bits tracks the *absolute* magnitude of
    # the DC/LL coefficient (roughly pixel value x 8), not inter-block
    # variation -- so unlike ac_depth's approach (a lone perturbed pixel), a
    # small BitDepthDC needs the whole image near a small constant value.
    def gen(x, y):
        return value

    return gen


def negative_pixel_block(bg, block_value, block_w=8, block_h=8):
    # A background value with one top-left block overridden -- used (as a
    # signed image) to control DC_Min/DC_Max's sign combination in
    # DC_EnDeCoding.c's Max_DC computation.
    def gen(x, y):
        return block_value if (x < block_w and y < block_h) else bg

    return gen


def sparse_bump_per_block(background, bump, block_size=8):
    # A low, near-flat background (small BitDepthDC/DC magnitude) with one
    # bumped pixel per 8x8 block (large local AC energy, high BitDepthAC) --
    # found empirically to decouple the two bit-depths enough to satisfy
    # DC_EnDeCoding.c's `QuantizationFactorQ_prime = BitDepthDC - 3` branch
    # (needs BitDepthDC > 3 but BitDepthDC - (1+(BitDepthAC>>1)) <= 1, i.e. a
    # high-AC/low-DC combination that neither a flat image nor a full-swing
    # checkerboard -- where AC and DC bit-depth track each other too closely
    # -- ever reaches). See COMPATIBILITY_REPORT.md.
    def gen(x, y):
        if x % block_size == block_size // 2 and y % block_size == block_size // 2:
            return bump
        return background

    return gen


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

    # --- deterministic noise (third, independent coefficient distribution shape) ---
    cases.append(build_case("noise_256", 256, 256, generator=noise, note="deterministic pseudo-random"))

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

    # --- AC bit-depth (N) boundary control: AC_BitPlaneCoding.c's
    # ACDepthEncoder/ACDepthDecoder branch on N (a bit-length-of-a-bit-length
    # derived from BitDepthAC_5Bits), and ACBpeEncoding/ACBpeDecoding take an
    # entirely different single-bit-plane path when BitDepthAC_5Bits==1. Both
    # were unreachable by the existing baseline/checkerboard/noise images,
    # which all land on N==3 or N==4 (BitDepthAC in 4-15). See
    # COMPATIBILITY_REPORT.md for how these specific values were found. ---
    cases.append(
        build_case(
            "ac_depth1_64",
            64,
            64,
            generator=make_single_bump(63, 63),
            note="single +1 bump at the last pixel: BitDepthAC_5Bits==1 (single-bit-plane AC path)",
        )
    )
    cases.append(
        build_case(
            "ac_depth2_64",
            64,
            64,
            generator=make_single_bump(10, 10),
            note="single +1 bump at an interior pixel: BitDepthAC_5Bits==2 (N==2 branch)",
        )
    )
    cases.append(
        build_case(
            "ac_depth5_16bit_64",
            64,
            64,
            bit_depth=16,
            generator=checkerboard_period1_max,
            note="period-1 max-amplitude 16-bit checkerboard: BitDepthAC_5Bits==17 (N==5 branch)",
        )
    )

    # --- DC bit-depth (N) boundary control: DC_EnDeCoding.c's DC entropy
    # coder branches on N = max(BitDepthDC_5Bits - QuantizationFactorQ, 1),
    # with the same N==2/N<=4 low-Max_k/ID_Length branches as the AC side
    # above -- unreachable by any existing image (whose BitDepthDC lands
    # around 12-13, giving N in the 8-10 range even under quantization).
    # Found empirically: a near-black constant image drops BitDepthDC low
    # enough that a modest lossy rate (which sets QuantizationFactorQ) pushes
    # N down into 2-4. See COMPATIBILITY_REPORT.md for the search. ---
    cases.append(
        build_case(
            "dc_depth_n2_64",
            64,
            64,
            generator=const(1),
            note="use with -r 0.5: constant value 1 pushes DC coding's N to 2",
        )
    )
    cases.append(
        build_case(
            "dc_depth_n4_64",
            64,
            64,
            generator=const(2),
            note="use with -r 1.0: constant value 2 pushes DC coding's N to 4",
        )
    )

    # --- Negative-DC Max_DC bit-depth computation: DC_EnDeCoding.c's
    # BitDepthDC_5Bits calculation has a separate code path (and an
    # exact-power-of-two adjustment) when the segment's dominant DC value is
    # negative -- never reached by any unsigned or DC-positive image above.
    # Found empirically: a signed image whose value is itself a negative
    # power of two (so the DC coefficient's magnitude, after the wavelet's
    # integer scaling, lands exactly on a bit-depth boundary) combined with a
    # mixed-sign image (a small negative block against a larger positive
    # background, forcing the "DC_Max positive but dominated by a larger
    # negative magnitude" branch) together cover every sub-branch. ---
    cases.append(
        build_case(
            "dc_negpow_64",
            64,
            64,
            generator=const(-128),
            note="use with -g 1 -r 0: constant -128 hits the exact-power-of-two Max_DC adjustment",
        )
    )
    cases.append(
        build_case(
            "dc_mixed_sign_64",
            64,
            64,
            generator=negative_pixel_block(20, -120),
            note="use with -g 1 -r 0: one 8x8 block at -120 against a +20 background hits the remaining negative-Max_DC branches",
        )
    )
    cases.append(
        build_case(
            "dc_qprime_lo_64",
            64,
            64,
            bit_depth=16,
            generator=sparse_bump_per_block(2, 50),
            note="use with -r 0: near-flat background 2 + a single bumped-to-50 pixel per 8x8 block hits QuantizationFactorQ_prime = BitDepthDC - 3",
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
