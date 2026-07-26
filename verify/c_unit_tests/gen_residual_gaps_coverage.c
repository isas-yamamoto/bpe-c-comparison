/* Coverage-only direct-call driver for a handful of specific residual-gap
 * lines identified in COMPATIBILITY_REPORT.md's reachability breakdown.
 * Like gen_stages_gaggles_coverage.c, this has no Rust counterpart and
 * produces no output file -- it exists purely to exercise lines that full
 * encode/decode-pipeline sweeps can't reach because the exact trigger
 * condition is either too narrow (a specific bit-budget cutoff mid-call)
 * or requires state that only arises across multiple bitplane calls within
 * a segment (a stop location already latched by an earlier bitplane).
 *
 * 1. PatternCoding.c's RefBitsDe, line ~516/518: an unconditional (no
 *    LocationFind gate) per-BlockSeq recorder that only runs when a stop
 *    location was already latched by an *earlier* bitplane's call (so
 *    RateReached/LocationFind/BitPlaneStopDecoding all carry over already
 *    set) and this bitplane's call has nothing pending to refine.
 * 2. AC_BitPlaneCoding.c's ACGaggleDecoding, line ~319: a plain RateReached
 *    early-return right after reading the first (uncoded/i==0) gaggle
 *    value -- reachable by a tiny bit-budget that runs out during that
 *    exact read.
 */
#include <stdio.h>
#include <string.h>
#include "global.h"

extern void RefBitsDe(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo);
extern void ACGaggleDecoding(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo, int StartIndex, int gaggles,
                              int Max_k, short ID_Length);

static void make_bitfile(const char *path, unsigned char byte) {
    FILE *f = fopen(path, "wb");
    for (int i = 0; i < 256; i++)
        fputc(byte, f);
    fclose(f);
}

static void run_refbitsde_sweep(void) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part3.S_20Bits = 3;

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;

    BitStream bits;
    memset(&bits, 0, sizeof(bits));
    coding.Bits = &bits;

    make_bitfile("residual_bits_a.bin", 0xAA);
    FILE *f = fopen("residual_bits_a.bin", "rb");
    bits.F_Bits = f;

    BitPlaneBits blocks[3];
    long store[3 * BLOCK_SIZE * BLOCK_SIZE];
    long *rows[3 * BLOCK_SIZE];
    memset(blocks, 0, sizeof(blocks));
    memset(store, 0, sizeof(store));
    for (int r = 0; r < 3 * BLOCK_SIZE; r++)
        rows[r] = store + r * BLOCK_SIZE;
    for (int b = 0; b < 3; b++) {
        blocks[b].PtrBlockAddress = &rows[b * BLOCK_SIZE];
        blocks[b].BitMaxAC = 20;
        /* All RefineBits fields zero: nothing pending to refine, so the
         * per-BlockSeq loop falls through every earlier (gated, returning)
         * checkpoint straight to the trailing unconditional recorder. */
    }

    for (int bpsd = 0; bpsd <= 10; bpsd++) {
        coding.RateReached = TRUE;
        coding.SegmentFull = FALSE;
        coding.DecodingStopLocations.BitPlaneStopDecoding = (char)bpsd;
        coding.DecodingStopLocations.LocationFind = TRUE;
        coding.DecodingAllowedBitsSizeInSegment = 1000000;
        coding.BitPlane = 5;
        rewind(f);
        RefBitsDe(&coding, blocks);
    }
    fclose(f);
    remove("residual_bits_a.bin");
}

static void run_acgaggledecoding_sweep(void) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;
    coding.N = 4;

    BitStream bits;
    memset(&bits, 0, sizeof(bits));
    coding.Bits = &bits;

    make_bitfile("residual_bits_b.bin", 0xAA);
    FILE *f = fopen("residual_bits_b.bin", "rb");
    bits.F_Bits = f;

    BitPlaneBits blocks[2];
    memset(blocks, 0, sizeof(blocks));

    for (int id_len = 1; id_len <= 4; id_len++) {
        for (int k = 0; k <= 40; k++) {
            coding.RateReached = FALSE;
            coding.SegmentFull = FALSE;
            coding.DecodingAllowedBitsSizeInSegment = k;
            header.Header.Part2.SegByteLimit_27Bits = 256;
            bits.SegBitCounter = 0;
            bits.CodeWordAlighmentBits = 0;
            rewind(f);
            ACGaggleDecoding(&coding, blocks, 0, 2, 6, (short)id_len);
        }
    }
    fclose(f);
    remove("residual_bits_b.bin");
}

int main(void) {
    run_refbitsde_sweep();
    run_acgaggledecoding_sweep();
    fprintf(stderr, "gen_residual_gaps_coverage: done\n");
    return 0;
}
