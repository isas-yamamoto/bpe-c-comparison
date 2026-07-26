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
 * 3. AC_BitPlaneCoding.c's ACGaggleDecoding (line ~310) and
 *    DC_EnDeCoding.c's DCGaggleDecoding (line ~272-273): the "uncoded_flag"
 *    check, where the decoded min_k equals every one of its ID_Length
 *    bits set to 1 (the encoder's signal that this gaggle used the
 *    uncoded representation). run_acgaggledecoding_sweep's own 0xAA bit
 *    source only happens to produce an all-1s pattern for ID_Length==1
 *    (min_k==1); ID_Length 2/3/4 need min_k==3/7/15, which requires an
 *    all-1s (0xFF) bit source instead.
 * 4. PatternCoding.c's RefBitsDe: the SegmentFull/RateReached early-return
 *    guards in front of each of the Parent/Children/GrandChildren refine
 *    sections (run_refbitsde_sweep's all-zero RefineBits never enters any
 *    of them, since each is itself gated on that section's *SymbolLength
 *    being > 0), and the RateReached-triggered stop-location recorders
 *    inside each section's inner loop -- reached the same way
 *    gen_stages_gaggles_coverage.c reaches its own checkpoints: a tiny
 *    bit-budget that runs dry mid-loop.
 */
#include <stdio.h>
#include <string.h>
#include "global.h"

extern void RefBitsDe(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo);
extern void ACGaggleDecoding(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo, int StartIndex, int gaggles,
                              int Max_k, short ID_Length);
extern void DCGaggleDecoding(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo, int StartIndex, int gaggles,
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

static void run_uncoded_flag_sweep(void) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part2.SegByteLimit_27Bits = 256;

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;
    coding.N = 4;

    BitStream bits;
    memset(&bits, 0, sizeof(bits));
    coding.Bits = &bits;

    make_bitfile("residual_bits_c.bin", 0xFF);
    FILE *f = fopen("residual_bits_c.bin", "rb");
    bits.F_Bits = f;

    BitPlaneBits blocks[2];
    memset(blocks, 0, sizeof(blocks));

    for (int id_len = 1; id_len <= 4; id_len++) {
        coding.RateReached = FALSE;
        coding.SegmentFull = FALSE;
        coding.DecodingAllowedBitsSizeInSegment = 1000000;
        bits.SegBitCounter = 0;
        bits.CodeWordAlighmentBits = 0;
        rewind(f);
        ACGaggleDecoding(&coding, blocks, 0, 2, 6, (short)id_len);

        coding.RateReached = FALSE;
        coding.SegmentFull = FALSE;
        coding.DecodingAllowedBitsSizeInSegment = 1000000;
        bits.SegBitCounter = 0;
        bits.CodeWordAlighmentBits = 0;
        rewind(f);
        DCGaggleDecoding(&coding, blocks, 0, 2, 6, (short)id_len);
    }
    fclose(f);
    remove("residual_bits_c.bin");
}

static void run_refbitsde_full_sweep(void) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part3.S_20Bits = 1;

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;
    coding.BitPlane = 5;

    BitStream bits;
    memset(&bits, 0, sizeof(bits));
    coding.Bits = &bits;

    make_bitfile("residual_bits_d.bin", 0xAA);
    FILE *f = fopen("residual_bits_d.bin", "rb");
    bits.F_Bits = f;

    long store[BLOCK_SIZE * BLOCK_SIZE];
    long *rows[BLOCK_SIZE];
    memset(store, 0, sizeof(store));
    for (int r = 0; r < BLOCK_SIZE; r++)
        rows[r] = store + r * BLOCK_SIZE;

    /* section: 0=Parent, 1=Children, 2=GrandChildren */
    for (int section = 0; section < 3; section++) {
        /* segfull_mode: 0=neither set (proceed into the loop and sweep
         * budget), 1=SegmentFull TRUE (early-return via first clause),
         * 2=RateReached TRUE (early-return via second clause). */
        for (int segfull_mode = 0; segfull_mode < 3; segfull_mode++) {
            for (DWORD32 k = 0; k <= 60; k++) {
                BitPlaneBits blk;
                memset(&blk, 0, sizeof(blk));
                blk.PtrBlockAddress = rows;
                blk.BitMaxAC = 20;
                if (section == 0) {
                    blk.RefineBits.RefineParent.ParentSymbolLength = 3;
                    blk.RefineBits.RefineParent.ParentRefSymbol = 0x7; /* all 3 bits pending */
                } else if (section == 1) {
                    blk.RefineBits.RefineChildren.ChildrenSymbolLength = 4;
                    blk.RefineBits.RefineChildren.ChildrenRefSymbol = 0xFFF;
                } else {
                    blk.RefineBits.RefineGrandChildren[0].GrandChildrenSymbolLength = 4;
                    blk.RefineBits.RefineGrandChildren[0].GrandChildrenRefSymbol = 0xFFFF;
                }

                coding.SegmentFull = (segfull_mode == 1) ? TRUE : FALSE;
                coding.RateReached = (segfull_mode == 2) ? TRUE : FALSE;
                coding.DecodingStopLocations.LocationFind = FALSE;
                coding.DecodingStopLocations.BitPlaneStopDecoding = -1;
                coding.DecodingAllowedBitsSizeInSegment = k;
                header.Header.Part2.SegByteLimit_27Bits = 256;
                bits.SegBitCounter = 0;
                bits.CodeWordAlighmentBits = 0;
                rewind(f);
                RefBitsDe(&coding, &blk);
            }
        }
    }
    fclose(f);
    remove("residual_bits_d.bin");
}

int main(void) {
    run_refbitsde_sweep();
    run_acgaggledecoding_sweep();
    run_uncoded_flag_sweep();
    run_refbitsde_full_sweep();
    fprintf(stderr, "gen_residual_gaps_coverage: done\n");
    return 0;
}
