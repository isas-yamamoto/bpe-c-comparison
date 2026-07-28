/* Generates function-level shared test vectors for CodingOptions
 * (original/source/PatternCoding.c), linked against the real object file.
 *
 * CodingOptions picks the cheapest Rice-split option per gaggle by summing,
 * across every sym_len==2/3/4 symbol in the gaggle, how many bits each
 * candidate option would cost, then walking an `else if` tie-break chain
 * over those sums. Which option comes out cheapest depends on the exact
 * distribution of mapped pattern values across potentially many symbols --
 * something a full encode/decode pipeline sweep (verify/run_compat.sh)
 * only lands on by chance, and something gen_codingoptions_coverage.c
 * (this directory) already drives directly for *C-side coverage*, but
 * without a Rust counterpart to cross-check against (see that file's
 * top comment). This generator reuses the same direct-call technique but
 * writes a vectors file so bpe-rs's own `coding_options` can be checked
 * bit-for-bit against the real C `CodingOptions`, the same pattern
 * gen_adjust_output_vectors.c / gen_pattern_vectors.c use elsewhere.
 *
 * Each line covers one (sym_len, type) combination in isolation (matching
 * how CodingOptions computes Option[0]/[1]/[2] independently per length),
 * with a sweep of symbol-value multisets:
 *   - sym_len 2/3: single symbols over the full domain, plus every
 *     (a, b) pair for sym_len 3 -- exhaustive enough with only 3 counters
 *     to force every tie-break direction (see gen_codingoptions_coverage.c's
 *     analysis).
 *   - sym_len 4: single symbols over the full domain, an exhaustive
 *     (a, b, c) triple sweep (4096 combos), plus two hand-derived cases
 *     documented in COMPATIBILITY_REPORT.md §4 item 8 that no generic
 *     sweep above reaches: {8,8,8,8,3,3,3} (mapped patterns {0,0,0,0,6,6,6}
 *     via bit4_pattern_TypeCi) drives bitsCounter_4Bits to (25,26,24,28),
 *     landing on the else-if chain's third clause's first condition being
 *     false (line ~328) -- previously unreachable by any sweep in this
 *     project; {8,3} (patterns {0,6}) hits the adjacent direction at line
 *     ~329 the same way.
 *
 * Output is plain text, one line per case:
 *   <sym_len> <type> <n> <sym_val_csv> <option0> <option1> <option2>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"

extern void CodingOptions(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo, UINT32 BlocksInGaggle,
                           UCHAR8 Option[]);

static FILE *out;

static void run_case(int sym_len, int type, int n, const int *sym_vals) {
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

    fprintf(out, "%d %d %d ", sym_len, type, n);
    for (int i = 0; i < n; i++)
        fprintf(out, "%d%s", sym_vals[i], (i + 1 < n) ? "," : "");
    fprintf(out, " %d %d %d\n", Option[0], Option[1], Option[2]);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <output.txt>\n", argv[0]);
        return 1;
    }
    out = fopen(argv[1], "w");
    if (!out) {
        perror("fopen");
        return 1;
    }

    /* sym_len 2: trivial single-comparison option[0]; a handful of single
     * symbols is enough to exercise both directions. */
    for (int v = 0; v < 4; v++) {
        int vals[1] = {v};
        run_case(2, ENUM_TYPE_P, 1, vals);
    }

    /* sym_len 3: single symbols over both carrying types, then every pair. */
    int types3[] = {ENUM_TYPE_P, ENUM_TRAN_D};
    for (int t = 0; t < 2; t++)
        for (int v = 0; v < 8; v++) {
            int vals[1] = {v};
            run_case(3, types3[t], 1, vals);
        }
    for (int a = 0; a < 8; a++)
        for (int b = 0; b < 8; b++) {
            int vals[2] = {a, b};
            run_case(3, ENUM_TYPE_P, 2, vals);
        }

    /* sym_len 4: single symbols over all three carrying types, then an
     * exhaustive triple sweep. */
    int types4[] = {ENUM_TYPE_CI, ENUM_TRAN_HI, ENUM_TYPE_HIJ};
    for (int t = 0; t < 3; t++)
        for (int v = 0; v < 16; v++) {
            int vals[1] = {v};
            run_case(4, types4[t], 1, vals);
        }
    for (int a = 0; a < 16; a++)
        for (int b = 0; b < 16; b++)
            for (int c = 0; c < 16; c++) {
                int vals[3] = {a, b, c};
                run_case(4, ENUM_TYPE_CI, 3, vals);
            }

    /* Hand-derived cases for tie-break directions no generic sweep above
     * reaches (COMPATIBILITY_REPORT.md §4 item 8). */
    {
        int vals[7] = {8, 8, 8, 8, 3, 3, 3};
        run_case(4, ENUM_TYPE_CI, 7, vals);
    }
    {
        int vals[2] = {8, 3};
        run_case(4, ENUM_TYPE_CI, 2, vals);
    }

    fclose(out);
    return 0;
}
