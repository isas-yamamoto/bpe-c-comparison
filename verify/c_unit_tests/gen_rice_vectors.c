/* Generates function-level shared test vectors for RiceCoding (original/source/ricecoding.c).
 *
 * Links against the real ricecoding.o/bitsIO.o/errorhandle.o so the vectors
 * reflect the actual reference implementation, not a reimplementation of it.
 * For every (bit_length, option) combo bpe-rs's own rice.rs unit tests cover,
 * encodes the full representable value domain [0, 2^bit_length) in sequence,
 * flushes the trailing partial byte, and records the resulting bytes.
 *
 * Output is plain text, one line per combo (no JSON parser dependency on
 * the Rust side, which has no crates.io dependencies):
 *   <bit_length> <opt0> <opt1> <opt2> <num_values> <bytes_hex>
 *
 * verify/run_unit_vectors.py builds and runs this; bpe-rs's own rice.rs
 * test suite (see the shared_vectors test) re-encodes the same value
 * sequence and asserts a byte-exact match against verify/vectors/rice_vectors.txt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "global.h"

extern void RiceCoding(short InputVal, short BitLength, UCHAR8 *Option, StructCodingPara *PtrCoding);
extern void BitsOutput(StructCodingPara *Ptr, DWORD32 bit, int length);

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

    size_t n_combos = sizeof(COMBOS) / sizeof(COMBOS[0]);
    for (size_t c = 0; c < n_combos; c++) {
        Combo combo = COMBOS[c];
        int domain = 1 << combo.bit_length;

        HeaderStruct header;
        memset(&header, 0, sizeof(header));
        header.Header.Part2.SegByteLimit_27Bits = 0; /* no rate limit */

        BitStream bits;
        memset(&bits, 0, sizeof(bits));
        bits.CodeWord_Length = 8;

        StructCodingPara coding;
        memset(&coding, 0, sizeof(coding));
        coding.Bits = &bits;
        coding.PtrHeader = &header;
        coding.SegmentFull = FALSE;

        char tmp_path[] = "/tmp/bpe_rice_vec_XXXXXX";
        int fd = mkstemp(tmp_path);
        if (fd < 0) {
            perror("mkstemp");
            return 1;
        }
        close(fd);
        bits.F_Bits = fopen(tmp_path, "wb");

        for (int v = 0; v < domain; v++) {
            UCHAR8 option[3] = {(UCHAR8)combo.option[0], (UCHAR8)combo.option[1], (UCHAR8)combo.option[2]};
            RiceCoding((short)v, (short)combo.bit_length, option, &coding);
        }
        /* flush trailing partial byte, matching SegmentBufferFlushEncoder */
        if (bits.CodeWordAlighmentBits != 0) {
            int shift = bits.CodeWord_Length - bits.CodeWordAlighmentBits;
            BitsOutput(&coding, 0, shift);
        }
        fclose(bits.F_Bits);

        FILE *rf = fopen(tmp_path, "rb");
        unsigned char buf[4096];
        size_t len = fread(buf, 1, sizeof(buf), rf);
        fclose(rf);
        remove(tmp_path);

        fprintf(manifest, "%d %d %d %d %d ", combo.bit_length, combo.option[0], combo.option[1],
                combo.option[2], domain);
        for (size_t i = 0; i < len; i++)
            fprintf(manifest, "%02x", buf[i]);
        fprintf(manifest, "\n");
    }
    fclose(manifest);
    return 0;
}
