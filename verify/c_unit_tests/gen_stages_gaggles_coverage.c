/* Coverage-only direct-call driver for StagesDeCodingGaggles1/2/3
 * (original/source/StagesCodingGaggles.c). Not part of the C/Rust
 * compatibility vector suite (no Rust counterpart, no output file) --
 * its only purpose is to exercise, via direct function calls, the
 * RateReached-triggered "record stop location and return" checkpoints
 * that full-pipeline rate-limited decode sweeps could only reach a
 * fraction of (see COMPATIBILITY_REPORT.md).
 *
 * Approach: these checkpoints all key off PtrCoding->RateReached, which
 * BitsRead() (bitsIO.c) flips to TRUE the instant the bit budget
 * (DecodingAllowedBitsSizeInSegment) is exhausted -- checked per single
 * bit read. Given a real (if synthetic) bit source, sweeping that budget
 * from 0 upward and calling the target function fresh each time makes it
 * run dry at a different point on every iteration, landing on whichever
 * checkpoint immediately follows that read. Crossed with a handful of
 * structural variants (DWTType, custom-weight thresholds, and pre-set
 * "already decoded" hit-history bit patterns to route through both the
 * read-new-symbol and already-found/refine branches at every level), an
 * exhaustive small-budget sweep hits effectively every reachable
 * checkpoint without needing to hand-derive bit offsets per line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"

extern void StagesDeCodingGaggles1(StructCodingPara *PtrCoding, BitPlaneBits *BlockCodingInfo,
                                    UCHAR8 BlocksInGaggles, UCHAR8 *CodeOptionsAllGaggles,
                                    BOOL *FlagCodeOptionOutput);
extern void StagesDeCodingGaggles2(StructCodingPara *PtrCoding, BitPlaneBits *BlockCodingInfo,
                                    UCHAR8 BlocksInGaggles, UCHAR8 *CodeOptionsAllGaggles,
                                    BOOL *FlagCodeOptionOutput);
extern void StagesDeCodingGaggles3(StructCodingPara *PtrCoding, BitPlaneBits *BlockCodingInfo,
                                    UCHAR8 BlocksInGaggles, UCHAR8 *CodeOptionsAllGaggles,
                                    BOOL *FlagCodeOptionOutput);

#define NBLOCKS 2
#define MAXK 150

static void make_bitfile(const char *path, int pattern) {
    /* pattern 0: all-zero bits: 1: all-one bits; 2: alternating 10101010 */
    FILE *f = fopen(path, "wb");
    unsigned char byte;
    if (pattern == 0)
        byte = 0x00;
    else if (pattern == 1)
        byte = 0xFF;
    else
        byte = 0xAA;
    for (int i = 0; i < 2048; i++)
        fputc(byte, f);
    fclose(f);
}

static void reset_coding(StructCodingPara *coding, FILE *f, DWORD32 budget_bits, UCHAR8 bitplane) {
    coding->RateReached = FALSE;
    coding->SegmentFull = FALSE;
    coding->DecodingStopLocations.LocationFind = FALSE;
    coding->DecodingStopLocations.BitPlaneStopDecoding = -1;
    coding->DecodingStopLocations.BlockNoStopDecoding = 0;
    coding->DecodingStopLocations.X_LocationStopDecoding = -1;
    coding->DecodingStopLocations.Y_LocationStopDecoding = -1;
    coding->DecodingAllowedBitsSizeInSegment = budget_bits;
    coding->BitPlane = bitplane;
    coding->Bits->SegBitCounter = 0;
    coding->Bits->TotalBitCounter = 0;
    coding->Bits->CodeWordAlighmentBits = 0;
    coding->Bits->ByteBuffer_4Bytes = 0;
    rewind(f);
    coding->PtrHeader->Header.Part2.SegByteLimit_27Bits = 256;
}

typedef struct {
    UCHAR8 typep, tranb, trand, trangi;
    UCHAR8 typeci[3], tranhi[3], typehij[3][4];
} HitPreset;

static void apply_hit_preset(BitPlaneBits *b, const HitPreset *p) {
    memset(&b->StrPlaneHitHistory, 0, sizeof(b->StrPlaneHitHistory));
    b->StrPlaneHitHistory.TypeP = p->typep;
    b->StrPlaneHitHistory.TranB = p->tranb;
    b->StrPlaneHitHistory.TranD = p->trand;
    b->StrPlaneHitHistory.TranGi = p->trangi;
    for (int k = 0; k < 3; k++) {
        b->StrPlaneHitHistory.TypeCi[k].TypeC = p->typeci[k];
        b->StrPlaneHitHistory.TranHi[k].TranH = p->tranhi[k];
        for (int j = 0; j < 4; j++)
            b->StrPlaneHitHistory.TypeHij[k].TypeHij[j].TranH = p->typehij[k][j];
    }
}

static HitPreset PRESETS[8 * 8 * 8];
static int NPRESETS_ACTUAL;

/* Systematically sweep every (TypeP, TranD, TranGi) combination (the three
 * "which of 3 children already found" fields that gate the top-level
 * coordinate-selection if/elseif/else clusters). Deeper-level
 * (TypeCi/TranHi/TypeHij) fields get one fixed partial-fill pattern so
 * their own counter-qualifying loops see a mix of found/not-found bits. */
static void build_presets(void) {
    static const UCHAR8 deep3[3] = {5, 10, 3};
    static const UCHAR8 deep3b[3] = {10, 3, 5};
    static const UCHAR8 hij4[3][4] = {{5, 10, 3, 12}, {9, 6, 15, 0}, {3, 12, 5, 10}};
    int n = 0;
    for (int typep = 0; typep < 8; typep++) {
        for (int trand = 0; trand < 8; trand++) {
            for (int trangi = 0; trangi < 8; trangi++) {
                HitPreset *p = &PRESETS[n++];
                p->typep = (UCHAR8)typep;
                p->tranb = 1;
                p->trand = (UCHAR8)trand;
                p->trangi = (UCHAR8)trangi;
                for (int k = 0; k < 3; k++) {
                    p->typeci[k] = deep3[k];
                    p->tranhi[k] = deep3b[k];
                    for (int j = 0; j < 4; j++)
                        p->typehij[k][j] = hij4[k][j];
                }
            }
        }
    }
    NPRESETS_ACTUAL = n;
}
#define NPRESETS NPRESETS_ACTUAL

int main(void) {
    build_presets();

    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part3.S_20Bits = NBLOCKS;

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;

    BitStream bits;
    memset(&bits, 0, sizeof(bits));
    coding.Bits = &bits;

    long total_calls = 0;
    for (int pattern = 0; pattern < 3; pattern++) {
        char path[64];
        snprintf(path, sizeof(path), "stages_bits_%d.bin", pattern);
        make_bitfile(path, pattern);
        FILE *f = fopen(path, "rb");
        bits.F_Bits = f;

        for (int dwt = 0; dwt < 2; dwt++) {
            header.Header.Part4.DWTType = (BOOL)dwt;
            for (int wtlevel = 0; wtlevel < 2; wtlevel++) {
                UCHAR8 wt = wtlevel == 0 ? 0 : 3;
                header.Header.Part4.CustomWtHL1_2bits = wt;
                header.Header.Part4.CustomWtLH1_2bits = wt;
                header.Header.Part4.CustomWtHH1_2bits = wt;
                header.Header.Part4.CustomWtHL2_2bits = wt;
                header.Header.Part4.CustomWtLH2_2bits = wt;
                header.Header.Part4.CustomWtHH2_2bits = wt;
                header.Header.Part4.CustomWtHL3_2bits = wt;
                header.Header.Part4.CustomWtLH3_2bits = wt;
                header.Header.Part4.CustomWtHH3_2bits = wt;

                for (int pi = 0; pi < NPRESETS; pi++) {
                    for (int bp = 0; bp < 2; bp++) {
                        UCHAR8 bitplane = bp == 0 ? 5 : 13;
                        header.Header.Part1.SegmentCount_8Bits = 28;

                        for (DWORD32 k = 0; k <= MAXK; k++) {
                            BitPlaneBits blocks[NBLOCKS];
                            long int_storage[NBLOCKS * BLOCK_SIZE * BLOCK_SIZE];
                            long *int_rows[NBLOCKS * BLOCK_SIZE];
                            memset(blocks, 0, sizeof(blocks));
                            memset(int_storage, 0, sizeof(int_storage));
                            for (int r = 0; r < NBLOCKS * BLOCK_SIZE; r++)
                                int_rows[r] = int_storage + r * BLOCK_SIZE;
                            for (int b = 0; b < NBLOCKS; b++) {
                                blocks[b].PtrBlockAddress = &int_rows[b * BLOCK_SIZE];
                                blocks[b].BitMaxAC = 20;
                                apply_hit_preset(&blocks[b], &PRESETS[pi]);
                            }

                            UCHAR8 codeopts[3] = {0, 0, 0};
                            BOOL flagopts[3] = {FALSE, FALSE, FALSE};
                            reset_coding(&coding, f, k, bitplane);
                            StagesDeCodingGaggles1(&coding, blocks, NBLOCKS, codeopts, flagopts);
                            total_calls++;

                            memset(codeopts, 0, sizeof(codeopts));
                            memset(flagopts, 0, sizeof(flagopts));
                            for (int b = 0; b < NBLOCKS; b++)
                                apply_hit_preset(&blocks[b], &PRESETS[pi]);
                            reset_coding(&coding, f, k, bitplane);
                            StagesDeCodingGaggles2(&coding, blocks, NBLOCKS, codeopts, flagopts);
                            total_calls++;

                            memset(codeopts, 0, sizeof(codeopts));
                            memset(flagopts, 0, sizeof(flagopts));
                            for (int b = 0; b < NBLOCKS; b++)
                                apply_hit_preset(&blocks[b], &PRESETS[pi]);
                            reset_coding(&coding, f, k, bitplane);
                            StagesDeCodingGaggles3(&coding, blocks, NBLOCKS, codeopts, flagopts);
                            total_calls++;
                        }
                    }
                }
            }
        }
        fclose(f);
        remove(path);
    }

    /* Dedicated pass for the SegmentCount_8Bits==28 && BitPlane==13 &&
     * BlockSeq==10 no-op scaffold (`BlockSeq = BlockSeq;`) in
     * StagesDeCodingGaggles3 -- needs >=11 blocks in the gaggle, which the
     * combinatorial sweep above (NBLOCKS==2) never provides. */
    {
        enum { BIGN = 12 };
        header.Header.Part3.S_20Bits = BIGN;
        header.Header.Part1.SegmentCount_8Bits = 28;
        header.Header.Part4.DWTType = INTEGER_WAVELET;
        make_bitfile("stages_bits_scaffold.bin", 2);
        FILE *f = fopen("stages_bits_scaffold.bin", "rb");
        bits.F_Bits = f;

        BitPlaneBits blocks[BIGN];
        long int_storage[BIGN * BLOCK_SIZE * BLOCK_SIZE];
        long *int_rows[BIGN * BLOCK_SIZE];
        memset(blocks, 0, sizeof(blocks));
        memset(int_storage, 0, sizeof(int_storage));
        for (int r = 0; r < BIGN * BLOCK_SIZE; r++)
            int_rows[r] = int_storage + r * BLOCK_SIZE;
        for (int b = 0; b < BIGN; b++) {
            blocks[b].PtrBlockAddress = &int_rows[b * BLOCK_SIZE];
            blocks[b].BitMaxAC = 20;
            apply_hit_preset(&blocks[b], &PRESETS[0]);
            blocks[b].StrPlaneHitHistory.TranB = 1;
        }
        UCHAR8 codeopts[3] = {0, 0, 0};
        BOOL flagopts[3] = {FALSE, FALSE, FALSE};
        reset_coding(&coding, f, 100000, 13);
        StagesDeCodingGaggles3(&coding, blocks, BIGN, codeopts, flagopts);
        total_calls++;
        fclose(f);
        remove("stages_bits_scaffold.bin");
    }

    fprintf(stderr, "gen_stages_gaggles_coverage: %ld calls\n", total_calls);
    return 0;
}
