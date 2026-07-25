/* Generates function-level shared test vectors for ConvTwosComp/DeConvTwosComp
 * (original/source/DC_EnDeCoding.c), linked against the real object file.
 *
 * For leftmost = 2..16 (matching bpe-rs's dc/twos_comp.rs "full-range
 * roundtrip" unit test range), encodes every representable signed value and
 * records the resulting unsigned code. verify/run_unit_vectors.py compares
 * this against bpe-rs's own conv_twos_comp for a byte-exact function-level
 * cross-check, independent of whatever values a real image happens to
 * produce through the full pipeline.
 *
 * Output is plain text, one line per leftmost width:
 *   <leftmost> <lo> <hi> <csv of encoded values for v = lo..=hi>
 */
#include <stdio.h>
#include <stdlib.h>
#include "global.h"

extern DWORD32 ConvTwosComp(long Original, short leftmost);

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <output_manifest.json>\n", argv[0]);
        return 1;
    }
    FILE *manifest = fopen(argv[1], "w");
    if (!manifest) {
        perror("fopen manifest");
        return 1;
    }

    for (short leftmost = 2; leftmost <= 16; leftmost++) {
        long lo = -(1L << (leftmost - 1));
        long hi = (1L << (leftmost - 1)) - 1;

        fprintf(manifest, "%d %ld %ld ", leftmost, lo, hi);
        for (long v = lo; v <= hi; v++) {
            DWORD32 encoded = ConvTwosComp(v, leftmost);
            fprintf(manifest, "%lu%s", (unsigned long)encoded, (v < hi) ? "," : "");
        }
        fprintf(manifest, "\n");
    }
    fclose(manifest);
    return 0;
}
