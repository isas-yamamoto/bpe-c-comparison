/* Coverage-only direct-call driver for BPEBlockCoding.c's BlockScanEncode.
 * No Rust counterpart, no output file -- it exists purely to exercise a
 * handful of significance-map branches that depend on the exact wavelet
 * coefficient magnitude relative to the current bit-plane, or on
 * TranB/TranD hit-history bits that a full encode/decode pipeline sweep
 * only ever lands on incidentally:
 *
 * 1. Line ~68: AMPLITUDE(coeff) < (1 << BitPlane), the upper half of the
 *    TypeP significance test (coeff falls in [2^(BitPlane-1), 2^BitPlane)
 *    vs. is large enough to exceed the current bit-plane entirely).
 * 2. Line ~180: SymbolsBlock[si].sym_len != 0 -- false on a block's first
 *    call (fresh detection leaves the *current* si slot untouched: the
 *    hunt loop writes+advances past the slot it fills, or TranB was
 *    already 1 and the whole hunt is skipped). True only requires a
 *    *second* call on the same (not reset in between) BlockInfo, where
 *    TranB is already 1 (skipping the hunt again) but slot si==1 still
 *    holds the *previous* call's leftover sym_len -- this is real
 *    encoder behavior: BlockCodingInfo is calloc'd once per segment and
 *    reused bitplane over bitplane, never zeroed between BlockScanEncode
 *    calls.
 * 3. Line ~260: TranD & (1 << (2-k)) == 0 -- both directions fall out of
 *    a single call with a TranD preset that has one bit set and two
 *    clear (e.g. 0b010), hit across the k=0,1,2 loop in one pass.
 */
#include <stdio.h>
#include <string.h>
#include "global.h"

extern void BlockScanEncode(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo);

#define NBLOCKS 3

static void run_case(int bitplane, long amp_case1, long amp_typep_hit, int preset_tranb, int preset_trand) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part3.S_20Bits = NBLOCKS;
    header.Header.Part4.DWTType = FLOAT_WAVELET; /* skip the CustomWt guards entirely */

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;
    coding.BitPlane = (UCHAR8)bitplane;

    long store[NBLOCKS * BLOCK_SIZE * BLOCK_SIZE];
    long *rows[NBLOCKS * BLOCK_SIZE];
    memset(store, 0, sizeof(store));
    for (int r = 0; r < NBLOCKS * BLOCK_SIZE; r++)
        rows[r] = store + r * BLOCK_SIZE;

    BitPlaneBits blocks[NBLOCKS];
    memset(blocks, 0, sizeof(blocks));
    for (int b = 0; b < NBLOCKS; b++) {
        blocks[b].PtrBlockAddress = &rows[b * BLOCK_SIZE];
        blocks[b].BitMaxAC = 20;
    }

    /* Block 0: TypeP amplitude probe at (temp_x,temp_y) for i=0 -> (0,1). */
    rows[0 * BLOCK_SIZE + 0][1] = amp_case1;

    /* Block 1: same probe, different amplitude, to hit the opposite
     * direction of the "< (1<<BitPlane)" check. */
    rows[1 * BLOCK_SIZE + 0][1] = amp_typep_hit;

    /* Block 2: preset TranB/TranD hit-history directly, bypassing the
     * descendant-hunting loop that would otherwise compute them from
     * block contents -- lets a single call exercise whichever sym_len /
     * TranD-bit combination we want. */
    blocks[2].StrPlaneHitHistory.TranB = (UCHAR8)preset_tranb;
    blocks[2].StrPlaneHitHistory.TranD = (UCHAR8)preset_trand;

    BlockScanEncode(&coding, blocks);
}

int main(void) {
    /* BitPlane=3 -> Bit_Set_Plane range is [4,8). amp=5 is inside (both
     * halves of the TypeP test true); amp=20 is >=4 but not <8 (false). */
    run_case(3, 5, 20, /*preset_tranb=*/0, /*preset_trand=*/0);

    /* Two calls on the SAME (not reset) BitPlaneBits, exactly as the real
     * per-segment encoder loop drives BlockScanEncode once per bitplane:
     * call 1 freshly detects TranB (false direction of line ~180, as
     * above); call 2 sees TranB already 1 (hunt skipped again) but its
     * si==1 slot still holds call 1's leftover sym_len -- true direction. */
    {
        HeaderStruct header;
        memset(&header, 0, sizeof(header));
        header.Header.Part3.S_20Bits = 1;
        header.Header.Part4.DWTType = FLOAT_WAVELET;
        StructCodingPara coding;
        memset(&coding, 0, sizeof(coding));
        coding.PtrHeader = &header;

        long store[BLOCK_SIZE * BLOCK_SIZE];
        long *rows[BLOCK_SIZE];
        memset(store, 0, sizeof(store));
        for (int r = 0; r < BLOCK_SIZE; r++)
            rows[r] = store + r * BLOCK_SIZE;
        rows[2][2] = 100; /* in the k=2 grandchild region, amplitude has bit 2 (value 4) set */

        BitPlaneBits blk;
        memset(&blk, 0, sizeof(blk));
        blk.PtrBlockAddress = rows;
        blk.BitMaxAC = 20;

        coding.BitPlane = 3;
        BlockScanEncode(&coding, &blk);
        coding.BitPlane = 2;
        BlockScanEncode(&coding, &blk); /* blk carries TranB/SymbolsBlock state forward */
    }

    /* TranB preset TRUE with TranD preset to 0b010: exercises the
     * "TranD & (1<<(2-k)) == 0" check both ways in one pass (k=0,2 -> 0
     * bit clear -> true; k=1 -> bit set -> false), and sym_len==0 at
     * entry gives line ~180's false direction. */
    run_case(3, 0, 0, /*preset_tranb=*/1, /*preset_trand=*/2);

    fprintf(stderr, "gen_blockscan_coverage: done\n");
    return 0;
}
