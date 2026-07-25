/* Generates function-level shared test vectors for DPCM_ACMapper/DPCM_ACDeMapper
 * (original/source/AC_BitPlaneCoding.c), linked against the real object file.
 *
 * Structurally different from the DC-side DPCM_DCMapper/DeMapper (see
 * gen_dpcm_vectors.c): BitMaxAC/MappedAC are `WORD16` (unsigned short, not
 * DWORD32/unsigned long), and there is no `-(short)(...)` sign-flip step at
 * all -- BitMaxAC is always a non-negative bit-depth count. Because WORD16
 * has lower rank than `int`, C's integer promotion converts it to *signed*
 * int for the theta subtraction (unlike DWORD32, which stays unsigned), so
 * the unsigned-wraparound bug found on the DC side is not expected to
 * apply here. This generator exists to confirm that empirically rather
 * than assume it from the type analysis alone.
 *
 * ac_depth_encoder only supports N in {2,3,4,5} (anything else is
 * BPE_DATA_ERROR), so that's the full N domain, unlike DC's N up to 16.
 *
 * Output is plain text, one line per (N, sequence) combo:
 *   <N> <size> <raw_csv> <mapped_csv> <decoded_csv>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"

extern void DPCM_ACMapper(BitPlaneBits *BlockInfo, int size, short N);
extern void DPCM_ACDeMapper(BitPlaneBits *BlockCodingInfo, int size, short N);

static void gen_ramp(WORD16 *out, int size, int modulus) {
    for (int i = 0; i < size; i++)
        out[i] = (WORD16)(i % modulus);
}

static void gen_extremes(WORD16 *out, int size, int modulus) {
    for (int i = 0; i < size; i++)
        out[i] = (i % 2 == 0) ? 0 : (WORD16)(modulus - 1);
}

static void gen_mid_boundary(WORD16 *out, int size, int modulus) {
    int mid = modulus / 2;
    int deltas[] = {0, 1, -1, 2, -2, 3, -3, 0};
    for (int i = 0; i < size; i++) {
        int v = mid + deltas[i % 8];
        if (v < 0) v = 0;
        if (v >= modulus) v = modulus - 1;
        out[i] = (WORD16)v;
    }
}

static void run_case(FILE *out, short N, int size, WORD16 *raw) {
    BitPlaneBits *mapper_blocks = calloc(size, sizeof(BitPlaneBits));
    for (int i = 0; i < size; i++)
        mapper_blocks[i].BitMaxAC = raw[i];
    DPCM_ACMapper(mapper_blocks, size, N);

    BitPlaneBits *demapper_blocks = calloc(size, sizeof(BitPlaneBits));
    for (int i = 0; i < size; i++)
        demapper_blocks[i].MappedAC = mapper_blocks[i].MappedAC;
    DPCM_ACDeMapper(demapper_blocks, size, N);

    fprintf(out, "%d %d ", N, size);
    for (int i = 0; i < size; i++)
        fprintf(out, "%u%s", (unsigned)(uint16_t)raw[i], (i + 1 < size) ? "," : " ");
    for (int i = 0; i < size; i++)
        fprintf(out, "%u%s", (unsigned)(uint16_t)mapper_blocks[i].MappedAC, (i + 1 < size) ? "," : " ");
    for (int i = 0; i < size; i++)
        fprintf(out, "%u%s", (unsigned)(uint16_t)demapper_blocks[i].BitMaxAC, (i + 1 < size) ? "," : "");
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

    short depths[] = {2, 3, 4, 5};
    for (size_t d = 0; d < sizeof(depths) / sizeof(depths[0]); d++) {
        short N = depths[d];
        int modulus = 1 << N;
        int size = modulus < 32 ? modulus * 2 : 32; /* repeat small domains to stress theta transitions */

        WORD16 *raw = calloc(size, sizeof(WORD16));

        gen_ramp(raw, size, modulus);
        run_case(out, N, size, raw);

        gen_extremes(raw, size, modulus);
        run_case(out, N, size, raw);

        gen_mid_boundary(raw, size, modulus);
        run_case(out, N, size, raw);

        free(raw);
    }

    fclose(out);
    return 0;
}
