/* Generates function-level shared test vectors for PatternMapping/DeMappingPattern
 * (original/source/PatternCoding.c), linked against the real object file.
 *
 * Exercises every (sym_len, type) combination bpe-rs's own pattern/mapping.rs
 * unit tests cover, over the full representable sym_val domain, so both the
 * mapping tables themselves and the type-dependent branch selection (TranD,
 * TypeCi, TranHi/TypeHij) are cross-checked bit-for-bit against the C
 * reference rather than only against each other.
 *
 * Output is plain text, one line per combo:
 *   <sym_len> <type> <num_values> <mapped_csv>
 */
#include <stdio.h>
#include <stdlib.h>
#include "global.h"

extern void PatternMapping(StrSymbolDetails *StrSymbol);

typedef struct {
    int sym_len;
    int type;
} Combo;

static const Combo COMBOS[] = {
    {1, ENUM_TYPE_P},
    {2, ENUM_TYPE_P},
    {3, ENUM_TRAN_B},
    {3, ENUM_TRAN_D},
    {4, ENUM_TYPE_CI},
    {4, ENUM_TRAN_HI},
    {4, ENUM_TYPE_HIJ},
};

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

    size_t n_combos = sizeof(COMBOS) / sizeof(COMBOS[0]);
    for (size_t c = 0; c < n_combos; c++) {
        Combo combo = COMBOS[c];
        int domain = 1 << combo.sym_len;

        fprintf(out, "%d %d %d ", combo.sym_len, combo.type, domain);
        for (int v = 0; v < domain; v++) {
            StrSymbolDetails sym;
            sym.sym_val = (UCHAR8)v;
            sym.sym_len = (UCHAR8)combo.sym_len;
            sym.type = (UCHAR8)combo.type;
            sym.sym_mapped_pattern = 0;
            sym.sign = 0;
            PatternMapping(&sym);
            fprintf(out, "%d%s", (int)sym.sym_mapped_pattern, (v + 1 < domain) ? "," : "");
        }
        fprintf(out, "\n");
    }
    fclose(out);
    return 0;
}
