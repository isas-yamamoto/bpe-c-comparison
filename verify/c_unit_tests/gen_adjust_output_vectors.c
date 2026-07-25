/* Generates function-level shared test vectors for AdjustOutPut
 * (original/source/AdjustOutput.c), linked against the real object file.
 *
 * AdjustOutPut is by far the largest and least-covered function in the
 * codebase (~3579 of the ~7300 total core-algorithm lines): it repairs
 * partially-decoded coefficients when a rate-limited decode stops mid
 * bit-plane. Driving it only through full encode/decode roundtrips (as
 * verify/run_compat.sh's rate sweeps do) means the exact stop location
 * (stoppedstage, block position, and especially the within-block
 * X/Y_LocationStopDecoding coordinates) is whatever the bitstream's byte
 * boundaries happen to produce -- most of the function's ~40-leaf decision
 * trees per stage are effectively unreachable that way.
 *
 * This generator instead constructs the function's inputs directly and
 * calls it in isolation, the same technique gen_dpcm_vectors.c /
 * gen_rice_vectors.c use for their (much smaller) targets. Per a full
 * structural analysis of the function (both the INTEGER_WAVELET and
 * FLOAT_WAVELET halves, confirmed structurally identical except numeric
 * type/bias), the only BitPlaneBits fields it reads/writes are
 * ShiftedDC, DecodingDCRemainder, PtrBlockAddress (int 8x8), and
 * PtrBlockAddressFloating (float 8x8); DecodingStopLocations.
 * TotalBitsReadThisTime/LocationFind are unused by this function.
 *
 * For each (DWTType, stoppedstage, b_DC-branch, X_Location, Y_Location)
 * combination, builds 3 blocks (block 1 is "at" BlockNoStopDecoding, block
 * 0 is "before", block 2 is "after" -- covering all three i-vs-BlockNo
 * relations every stage branches on) with an 8x8 coefficient grid mixing
 * positive/negative/zero values (and independently-signed int vs float
 * copies per cell, to catch any accidental coupling between the two
 * representations), calls AdjustOutPut, and dumps the resulting int+float
 * arrays for all 3 blocks.
 *
 * Output is plain text, one line per combo:
 *   <dwt_type> <stoppedstage> <b_dc_case> <x_loc> <y_loc> <int_csv> <float_csv>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"

extern void AdjustOutPut(StructCodingPara *PtrCoding, BitPlaneBits *BlockCodingInfo);

#define TOTAL_BLOCKS 3
#define BLOCK_NO 1

/* Deterministic sign/magnitude pattern: varies by (block, m, n) so int and
 * float copies of the same cell can independently be positive, negative,
 * or (occasionally) zero. (0,0) is always the DC cell AdjustOutPut skips
 * via its "continue" guard, so its value doesn't matter for this function. */
static long int_val(int block, int m, int n) {
    int v = (block * 7 + m * 3 + n * 5) % 11 - 5; /* range roughly -5..5 */
    return (long)v;
}
static float float_val(int block, int m, int n) {
    /* Different phase from int_val so some cells have opposite sign / one
     * is zero while the other isn't. */
    int v = (block * 5 + m * 7 + n * 2) % 9 - 4;
    return (float)v;
}

static void run_case(FILE *out, int dwt_type, int stoppedstage, int b_dc_case, int x_loc, int y_loc) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part1.BitDepthDC_5Bits = 8;
    header.Header.Part3.S_20Bits = TOTAL_BLOCKS;
    header.Header.Part4.DWTType = dwt_type;

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;
    coding.RateReached = TRUE;
    coding.QuantizationFactorQ = (b_dc_case == 0) ? 0 : 5;
    coding.DecodingStopLocations.BitPlaneStopDecoding = (b_dc_case == 0) ? 0 : 3;
    coding.DecodingStopLocations.BlockNoStopDecoding = BLOCK_NO;
    coding.DecodingStopLocations.stoppedstage = (unsigned char)stoppedstage;
    coding.DecodingStopLocations.X_LocationStopDecoding = (char)x_loc;
    coding.DecodingStopLocations.Y_LocationStopDecoding = (char)y_loc;

    long *int_storage = calloc(TOTAL_BLOCKS * BLOCK_SIZE * BLOCK_SIZE, sizeof(long));
    float *float_storage = calloc(TOTAL_BLOCKS * BLOCK_SIZE * BLOCK_SIZE, sizeof(float));
    long **int_rows = calloc(TOTAL_BLOCKS * BLOCK_SIZE, sizeof(long *));
    float **float_rows = calloc(TOTAL_BLOCKS * BLOCK_SIZE, sizeof(float *));
    for (int i = 0; i < TOTAL_BLOCKS * BLOCK_SIZE; i++) {
        int_rows[i] = int_storage + i * BLOCK_SIZE;
        float_rows[i] = float_storage + i * BLOCK_SIZE;
    }

    BitPlaneBits blocks[TOTAL_BLOCKS];
    memset(blocks, 0, sizeof(blocks));
    for (int b = 0; b < TOTAL_BLOCKS; b++) {
        blocks[b].PtrBlockAddress = &int_rows[b * BLOCK_SIZE];
        blocks[b].PtrBlockAddressFloating = &float_rows[b * BLOCK_SIZE];
        blocks[b].ShiftedDC = (DWORD32)(100 + b);
        blocks[b].DecodingDCRemainder = 0.0f;
        for (int m = 0; m < BLOCK_SIZE; m++) {
            for (int n = 0; n < BLOCK_SIZE; n++) {
                int_rows[b * BLOCK_SIZE + m][n] = int_val(b, m, n);
                float_rows[b * BLOCK_SIZE + m][n] = float_val(b, m, n);
            }
        }
    }

    AdjustOutPut(&coding, blocks);

    fprintf(out, "%d %d %d %d %d ", dwt_type, stoppedstage, b_dc_case, x_loc, y_loc);
    for (int b = 0; b < TOTAL_BLOCKS; b++)
        for (int m = 0; m < BLOCK_SIZE; m++)
            for (int n = 0; n < BLOCK_SIZE; n++) {
                int last = (b == TOTAL_BLOCKS - 1 && m == BLOCK_SIZE - 1 && n == BLOCK_SIZE - 1);
                fprintf(out, "%ld%s", int_rows[b * BLOCK_SIZE + m][n], last ? "" : ",");
            }
    fprintf(out, " ");
    for (int b = 0; b < TOTAL_BLOCKS; b++)
        for (int m = 0; m < BLOCK_SIZE; m++)
            for (int n = 0; n < BLOCK_SIZE; n++) {
                int last = (b == TOTAL_BLOCKS - 1 && m == BLOCK_SIZE - 1 && n == BLOCK_SIZE - 1);
                fprintf(out, "%.9e%s", float_rows[b * BLOCK_SIZE + m][n], last ? "" : ",");
            }
    fprintf(out, "\n");

    free(int_storage);
    free(float_storage);
    free(int_rows);
    free(float_rows);
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

    int dwt_types[] = {INTEGER_WAVELET, FLOAT_WAVELET};
    for (size_t d = 0; d < 2; d++) {
        for (int stage = 1; stage <= 4; stage++) {
            for (int b_dc = 0; b_dc < 2; b_dc++) {
                for (int x = 0; x < BLOCK_SIZE; x++) {
                    for (int y = 0; y < BLOCK_SIZE; y++) {
                        run_case(out, dwt_types[d], stage, b_dc, x, y);
                    }
                }
            }
        }
    }

    fclose(out);
    return 0;
}
