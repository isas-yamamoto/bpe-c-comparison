/* Generates function-level shared test vectors for DPCM_DCMapper/DPCM_DCDeMapper
 * (original/source/DC_EnDeCoding.c), linked against the real object file.
 *
 * DPCM_DCDeMapper is the function readme_kielymods.rtf singles out as the
 * highest-value target for bit-for-bit differential testing: its branch
 * structure (the `MappedDC > 2*theta` split) was rewritten as part of the
 * Kiely bugfix, not just a formula tweak.
 *
 * For several bit depths (N) and raw ShiftedDC sequences designed to stress
 * the theta-boundary branches (monotonic ramp, alternating extremes,
 * oscillation around the midpoint), records:
 *   - the input sequence as originally supplied,
 *   - MappedDC produced by DPCM_DCMapper,
 *   - ShiftedDC recovered by feeding that MappedDC into DPCM_DCDeMapper.
 *
 * verify/run_unit_vectors.py builds and runs this; bpe-rs's dc/dpcm.rs
 * shared_vectors test re-runs both functions on the same input/MappedDC
 * sequences and asserts byte-exact agreement with the C reference.
 *
 * Output is plain text, one line per (N, sequence) combo:
 *   <N> <size> <raw_csv> <mapped_csv> <decoded_csv>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"

extern void DPCM_DCMapper(BitPlaneBits *BlockInfo, int size, short N);
extern void DPCM_DCDeMapper(BitPlaneBits *BlockInfo, int size, short N);

static void gen_ramp(DWORD32 *out, int size, int modulus) {
    for (int i = 0; i < size; i++)
        out[i] = (DWORD32)(i % modulus);
}

static void gen_extremes(DWORD32 *out, int size, int modulus) {
    for (int i = 0; i < size; i++)
        out[i] = (i % 2 == 0) ? 0 : (DWORD32)(modulus - 1);
}

static void gen_mid_boundary(DWORD32 *out, int size, int modulus) {
    int mid = modulus / 2;
    int deltas[] = {0, 1, -1, 2, -2, 3, -3, 0};
    for (int i = 0; i < size; i++) {
        int v = mid + deltas[i % 8];
        if (v < 0) v = 0;
        if (v >= modulus) v = modulus - 1;
        out[i] = (DWORD32)v;
    }
}

static void run_case(FILE *out, short N, int size, DWORD32 *raw) {
    BitPlaneBits *mapper_blocks = calloc(size, sizeof(BitPlaneBits));
    for (int i = 0; i < size; i++)
        mapper_blocks[i].ShiftedDC = raw[i];
    DPCM_DCMapper(mapper_blocks, size, N);

    BitPlaneBits *demapper_blocks = calloc(size, sizeof(BitPlaneBits));
    for (int i = 0; i < size; i++)
        demapper_blocks[i].MappedDC = mapper_blocks[i].MappedDC;
    DPCM_DCDeMapper(demapper_blocks, size, N);

    /* DWORD32 is `unsigned long` (64-bit on this platform), but bpe-rs
     * represents ShiftedDC/MappedDC as `u32`. DPCM_DCMapper/DeMapper assign
     * negative `short`/`long` values into these fields without an explicit
     * mask, so C sign-extends to the full 64 bits while Rust's u32 only ever
     * holds 32. The low 32 bits are identical either way (sign extension
     * doesn't touch them), and that's the only part any downstream consumer
     * reads for the N<=16 depths tested here -- confirmed separately by the
     * full-pipeline byte-compat matrix passing across DC bit depths. Compare
     * the low 32 bits explicitly so this generator doesn't manufacture a
     * mismatch out of a width difference that has no behavioral effect. */
    fprintf(out, "%d %d ", N, size);
    for (int i = 0; i < size; i++)
        fprintf(out, "%lu%s", (unsigned long)(uint32_t)raw[i], (i + 1 < size) ? "," : " ");
    for (int i = 0; i < size; i++)
        fprintf(out, "%lu%s", (unsigned long)(uint32_t)mapper_blocks[i].MappedDC,
                (i + 1 < size) ? "," : " ");
    for (int i = 0; i < size; i++)
        fprintf(out, "%lu%s", (unsigned long)(uint32_t)demapper_blocks[i].ShiftedDC,
                (i + 1 < size) ? "," : "");
    fprintf(out, "\n");

    free(mapper_blocks);
    free(demapper_blocks);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <output.txt>\n", argv[0]);
        return 1;
    }
    FILE *out = fopen(argv[1], "w");
    if (!out) {
        perror("fopen");
        return 1;
    }

    short depths[] = {4, 8, 16};
    for (size_t d = 0; d < sizeof(depths) / sizeof(depths[0]); d++) {
        short N = depths[d];
        int modulus = 1 << N;
        int size = modulus < 64 ? modulus : 64;

        DWORD32 *raw = calloc(size, sizeof(DWORD32));

        gen_ramp(raw, size, modulus);
        run_case(out, N, size, raw);

        gen_extremes(raw, size, modulus);
        run_case(out, N, size, raw);

        gen_mid_boundary(raw, size, modulus);
        run_case(out, N, size, raw);

        free(raw);
    }

    /* Fixed hand-picked sequence matching bpe-rs's own dc/dpcm.rs unit test,
     * so this vector set also covers the exact case already known to work
     * internally, as a sanity cross-check. */
    {
        DWORD32 raw[] = {0, 1, 2, 255, 128, 127, 64, 200, 3, 250};
        int size = (int)(sizeof(raw) / sizeof(raw[0]));
        run_case(out, 8, size, raw);
    }

    fclose(out);
    return 0;
}
