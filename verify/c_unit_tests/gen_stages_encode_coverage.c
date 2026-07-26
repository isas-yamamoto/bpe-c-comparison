/* Coverage-only direct-call driver for StagesCodingGaggles.c's
 * StagesEnCodingGaggles1/2/3 (encode side). No Rust counterpart, no
 * output file -- exercises the sym_len==3/4 dispatch inside each
 * function's "first occurrence of this length in the gaggle" once-only
 * BitsOutput block, which needs at least one symbol of each length
 * actually present with the right `type` to reach: a full pipeline
 * sweep depends on which symbol lengths a given image's wavelet
 * coefficients happen to produce, and doesn't reliably hit every length
 * for every one of the three functions' distinct type sets.
 */
#include <stdio.h>
#include <string.h>
#include "global.h"

extern void StagesEnCodingGaggles1(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo, UCHAR8 BlocksInGaggles,
                                    UCHAR8 Option[], BOOL FlagCodeOptionOutput[]);
extern void StagesEnCodingGaggles2(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo, UCHAR8 BlocksInGaggles,
                                    UCHAR8 Option[], BOOL FlagCodeOptionOutput[]);
extern void StagesEnCodingGaggles3(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo, UCHAR8 BlocksInGaggles,
                                    UCHAR8 Option[], BOOL FlagCodeOptionOutput[]);

static void run_case(void (*fn)(StructCodingPara *, BitPlaneBits *, UCHAR8, UCHAR8[], BOOL[]), int type,
                      int sym_len) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part2.SegByteLimit_27Bits = 0;

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;
    coding.BitPlane = 1;

    BitStream bits;
    memset(&bits, 0, sizeof(bits));
    FILE *f = fopen("stages_encode_scratch.bin", "wb");
    bits.F_Bits = f;
    coding.Bits = &bits;

    BitPlaneBits blk;
    memset(&blk, 0, sizeof(blk));
    blk.BitMaxAC = 20;
    blk.SymbolsBlock[0].type = (BOOL)type;
    blk.SymbolsBlock[0].sym_len = sym_len;
    blk.SymbolsBlock[0].sym_val = 0;

    UCHAR8 Option[3] = {0, 1, 2};
    BOOL flagopts[3] = {FALSE, FALSE, FALSE};
    fn(&coding, &blk, 1, Option, flagopts);
    fclose(f);
    remove("stages_encode_scratch.bin");
}

int main(void) {
    int lens[] = {2, 3, 4};
    /* StagesEnCodingGaggles1's TYPE_P dispatch is a switch(sym_len){case
     * 1:2:3:...default:ErrorMsg} -- sym_len==4 is invalid for TYPE_P
     * (only 3 parent positions exist), so only sweep 2/3 here. */
    for (int i = 0; i < 2; i++)
        run_case(StagesEnCodingGaggles1, ENUM_TYPE_P, lens[i]);
    for (int i = 0; i < 3; i++) {
        run_case(StagesEnCodingGaggles2, ENUM_TRAN_B, lens[i]);
        run_case(StagesEnCodingGaggles2, ENUM_TRAN_D, lens[i]);
        run_case(StagesEnCodingGaggles2, ENUM_TYPE_CI, lens[i]);
    }
    for (int i = 0; i < 3; i++)
        run_case(StagesEnCodingGaggles3, ENUM_TRAN_GI, lens[i]);

    fprintf(stderr, "gen_stages_encode_coverage: done\n");
    return 0;
}
