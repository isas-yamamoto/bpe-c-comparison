/* Coverage-only direct-call driver for PatternCoding.c's CodingOptions,
 * PatternMapping, and DeMappingPattern. No Rust counterpart, no output
 * file -- exercises the Rice-split-option tie-break logic (bitsCounter_3Bits
 * / bitsCounter_4Bits comparisons) and the sym_len==4 type dispatch
 * (TYPE_CI vs TRAN_HI/TYPE_HIJ), none of which a full encode/decode
 * pipeline sweep reliably lands on both directions of: which coding
 * option comes out cheapest depends on the exact distribution of pattern
 * values across an entire gaggle's symbols, which real image content
 * doesn't hit systematically.
 *
 * CodingOptions calls PatternMapping itself, so sym_val is set to
 * whatever value maps (through PatternMapping's own lookup tables) to
 * the sym_mapped_pattern needed to land in a given bracket -- not the
 * mapped pattern value directly.
 */
#include <stdio.h>
#include <string.h>
#include "global.h"

extern void CodingOptions(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo, UINT32 BlocksInGaggle,
                           UCHAR8 Option[]);
extern void PatternMapping(StrSymbolDetails *StrSymbol);
extern void DeMappingPattern(StrSymbolDetails *StrSymbol);

static void run_symbols(int sym_len, int type, int n, const int *sym_vals) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.PtrHeader = &header;
    coding.BitPlane = 1;

    BitPlaneBits blk;
    memset(&blk, 0, sizeof(blk));
    blk.BitMaxAC = 20;
    for (int i = 0; i < n; i++) {
        blk.SymbolsBlock[i].type = (BOOL)type;
        blk.SymbolsBlock[i].sym_len = sym_len;
        blk.SymbolsBlock[i].sym_val = sym_vals[i];
    }
    UCHAR8 Option[3] = {0, 0, 0};
    CodingOptions(&coding, &blk, 1, Option);
}

int main(void) {
    /* Exhaustively sweep every single-symbol mapped pattern for len 2/3/4,
     * across all four symbol types that can carry that length, to hit as
     * much of the bracket structure as a single symbol can. */
    int types3[] = {ENUM_TYPE_P, ENUM_TRAN_D};
    for (int t = 0; t < 2; t++)
        for (int v = 0; v < 8; v++) {
            int vals[1] = {v};
            run_symbols(3, types3[t], 1, vals);
        }
    int types4[] = {ENUM_TYPE_CI, ENUM_TRAN_HI, ENUM_TYPE_HIJ};
    for (int t = 0; t < 3; t++)
        for (int v = 0; v < 16; v++) {
            int vals[1] = {v};
            run_symbols(4, types4[t], 1, vals);
        }

    /* Multi-symbol combinations, hand-derived to force each Rice-split
     * option (0/1/2/3) to be the unique cheapest, exercising every
     * tie-break comparison's both directions:
     * - len3: sym_val {0,6} -> mapped patterns {1,3} via bit3_pattern[]
     *   makes bitsCounter_3Bits[1] (option 1) the strict minimum.
     * - len4 (TypeCi): sym_val {3,3,4} -> mapped patterns {6,6,2} via
     *   bit4_pattern_TypeCi[] makes bitsCounter_4Bits[2] (option 2) the
     *   strict minimum. */
    {
        int vals[2] = {0, 6};
        run_symbols(3, ENUM_TYPE_P, 2, vals);
    }
    {
        int vals[3] = {3, 3, 4};
        run_symbols(4, ENUM_TYPE_CI, 3, vals);
    }
    /* 3x pattern0 + 2x pattern15 (mapped via bit4_pattern_TypeCi[]:
     * find sym_val where the table gives 0, and sym_val giving 15) --
     * forces bitsCounter_4Bits[3] (uncoded) to tie/beat [1] but lose to
     * [0], landing Option[2]=0 while exercising the first clause's
     * second condition (line ~317) false direction. bit4_pattern_TypeCi
     * = {10,1,3,6,2,5,9,12,0,8,7,13,4,14,11,15}: sym_val=8 -> pattern 0,
     * sym_val=15 -> pattern 15. */
    {
        int vals[5] = {8, 8, 8, 15, 15};
        run_symbols(4, ENUM_TYPE_CI, 5, vals);
    }
    /* 4x pattern0 + 3x pattern6 (bit4_pattern_TypeCi: sym_val=8 -> pattern
     * 0, sym_val=3 -> pattern 6) drives bitsCounter_4Bits to (25,26,24,28).
     * Neither bitsCounter_4Bits[3] (28) nor [0] (25) is the minimum, so the
     * first two `else if` clauses (PatternCoding.c:316,322) both fail and
     * control reaches the third clause's *first* condition
     * (`bitsCounter_4Bits[1] <= bitsCounter_4Bits[0]`, line ~328) -- which
     * is false here (26 > 25), a direction no other case in this file
     * exercises. Falls through to the fourth clause (bitsCounter_4Bits[2]
     * == 24 is the true minimum), landing Option[2]=2. */
    {
        int vals[7] = {8, 8, 8, 8, 3, 3, 3};
        run_symbols(4, ENUM_TYPE_CI, 7, vals);
    }

    /* A handful of other multi-symbol mixes to widen coverage of the
     * remaining pairwise tie-break directions without hand-deriving
     * every one -- systematically pairs every (pattern_a, pattern_b)
     * combination for len 3 and len 4. */
    for (int a = 0; a < 8; a++)
        for (int b = 0; b < 8; b++) {
            int vals[2] = {a, b};
            run_symbols(3, ENUM_TYPE_P, 2, vals);
        }
    for (int a = 0; a < 16; a += 3)
        for (int b = 0; b < 16; b += 3)
            for (int c = 0; c < 16; c += 5) {
                int vals[3] = {a, b, c};
                run_symbols(4, ENUM_TYPE_CI, 3, vals);
            }

    fprintf(stderr, "gen_codingoptions_coverage: done\n");
    return 0;
}
