/* Coverage-only direct-call driver for RiceDecoding (original/source/ricecoding.c).
 * No Rust counterpart, no output file -- unlike gen_rice_vectors.c (which
 * exhaustively exercises RiceCoding/encode's full value domain per
 * (bit_length, option) combo), no generator called RiceDecoding directly:
 * the C reference's decode side only ever got coverage from whatever bit
 * patterns real pipeline test images happened to produce. This sweeps every
 * possible leading bit pattern through RiceDecoding for the same 10
 * (bit_length, option) combos the encode-side generator uses, so every
 * branch in its nested value-decode tree gets a chance to fire.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "global.h"

extern void RiceDecoding(DWORD32 *decoded, short BitLength, UCHAR8 *splitOption, StructCodingPara *Ptr);

typedef struct {
    int bit_length;
    int option[3];
} Combo;

static const Combo COMBOS[] = {
    {1, {0, 0, 0}},
    {2, {0, 0, 0}},
    {2, {1, 0, 0}},
    {3, {0, 0, 0}},
    {3, {0, 1, 0}},
    {3, {0, 3, 0}},
    {4, {0, 0, 0}},
    {4, {0, 0, 1}},
    {4, {0, 0, 2}},
    {4, {0, 0, 3}},
};
#define NCOMBOS (int)(sizeof(COMBOS) / sizeof(COMBOS[0]))

int main(void) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));

    BitStream bits;
    memset(&bits, 0, sizeof(bits));

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.Bits = &bits;
    coding.PtrHeader = &header;

    unsigned char buf[3];
    long total_calls = 0;
    for (int c = 0; c < NCOMBOS; c++) {
        UCHAR8 splitOption[3] = {(UCHAR8)COMBOS[c].option[0], (UCHAR8)COMBOS[c].option[1],
                                  (UCHAR8)COMBOS[c].option[2]};
        /* Exhaustively sweep every 20-bit leading pattern via an in-memory
         * "file" (fmemopen) -- individual reads in the decode tree never
         * exceed a handful of bits, so this covers every reachable path
         * with room to spare, without the syscall overhead of real files. */
        for (unsigned int pattern = 0; pattern < (1u << 20); pattern++) {
            buf[0] = (unsigned char)((pattern >> 16) & 0xFF);
            buf[1] = (unsigned char)((pattern >> 8) & 0xFF);
            buf[2] = (unsigned char)(pattern & 0xFF);
            FILE *bf = fmemopen(buf, sizeof(buf), "rb");

            bits.F_Bits = bf;
            bits.CodeWordAlighmentBits = 0;
            bits.ByteBuffer_4Bytes = 0;
            bits.SegBitCounter = 0;
            coding.RateReached = FALSE;
            coding.SegmentFull = FALSE;
            header.Header.Part2.SegByteLimit_27Bits = 0;

            DWORD32 decoded = 0;
            RiceDecoding(&decoded, (short)COMBOS[c].bit_length, splitOption, &coding);
            total_calls++;
            fclose(bf);
        }
    }
    fprintf(stderr, "gen_rice_decode_coverage: %ld calls\n", total_calls);
    return 0;
}
