/* Generates function-level shared test vectors for ACDepthEncoder
 * (original/source/AC_BitPlaneCoding.c), linked against the real object
 * file.
 *
 * ACDepthEncoder is the public entry point that computes N from
 * BitDepthAC_5Bits, runs DPCM_ACMapper (already cross-checked in isolation
 * by gen_ac_dpcm_vectors.c), then loops ACGaggleEncoding -- the private,
 * per-gaggle Rice-split-option selection and bit output -- over every
 * GAGGLE_SIZE(16)-block gaggle in the segment. A full encode/decode
 * pipeline sweep (verify/run_compat.sh) drives this only through whatever
 * BitMaxAC distribution a given test image happens to produce; this
 * generator instead builds the BitMaxAC sequence directly (the same
 * technique gen_adjust_output_vectors.c / gen_codingoptions_vectors.c use
 * elsewhere), covering every N the encoder supports (2-5), multiple
 * segment sizes spanning 1-3 gaggles, and several BitMaxAC distributions
 * (ramp, alternating extremes, oscillation near mid-range) chosen to drive
 * different Rice-split tie-break outcomes across the gaggle loop.
 *
 * OptDCSelect (misleadingly named -- it also gates the AC-side option
 * selection) is fixed TRUE: HeaderInilization hardcodes it TRUE with no
 * CLI option to change it, so FALSE is confirmed dead code
 * (COMPATIBILITY_REPORT.md §4) and not worth vectors.
 *
 * Output is plain text, one line per (BitDepthAC_5Bits, S_20Bits, pattern)
 * combo:
 *   <bit_depth_ac_5bits> <s_20bits> <bitmaxac_csv> <bytes_hex>
 *
 * bpe-rs's own ac/depth.rs test reads this, calls ac_depth_encoder with
 * the same BitMaxAC sequence and asserts a byte-exact match, then feeds
 * bytes_hex to ac_depth_decoder and asserts the recovered BitMaxAC
 * sequence matches the original (cross-decode, same pattern
 * gen_rice_vectors.c's shared-vector test uses).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "global.h"

extern void ACDepthEncoder(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo);

static void gen_ramp(WORD16 *out, int size, int modulus) {
    for (int i = 0; i < size; i++)
        out[i] = (WORD16)(i % modulus);
}

static void gen_extremes(WORD16 *out, int size, int modulus) {
    for (int i = 0; i < size; i++)
        out[i] = (i % 2 == 0) ? 0 : (WORD16)(modulus - 1);
}

static void gen_mid_boundary(WORD16 *out, int size, int modulus) {
    int mid = modulus / 2;
    int deltas[] = {0, 1, -1, 2, -2, 3, -3, 0};
    for (int i = 0; i < size; i++) {
        int v = mid + deltas[i % 8];
        if (v < 0) v = 0;
        if (v >= modulus) v = modulus - 1;
        out[i] = (WORD16)v;
    }
}

static void run_case(FILE *out, UCHAR8 bit_depth_ac_5bits, int s_20bits, WORD16 *raw) {
    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part1.BitDepthAC_5Bits = bit_depth_ac_5bits;
    header.Header.Part3.S_20Bits = s_20bits;
    header.Header.Part3.OptDCSelect = TRUE;
    header.Header.Part2.SegByteLimit_27Bits = 0; /* no rate limit */

    BitStream bits;
    memset(&bits, 0, sizeof(bits));
    bits.CodeWord_Length = 8;

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.Bits = &bits;
    coding.PtrHeader = &header;
    coding.SegmentFull = FALSE;

    BitPlaneBits *blocks = calloc(s_20bits, sizeof(BitPlaneBits));
    for (int i = 0; i < s_20bits; i++)
        blocks[i].BitMaxAC = raw[i];

    char tmp_path[] = "/tmp/bpe_ac_depth_vec_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        perror("mkstemp");
        exit(1);
    }
    close(fd);
    bits.F_Bits = fopen(tmp_path, "wb");

    ACDepthEncoder(&coding, blocks);

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

    fprintf(out, "%d %d ", bit_depth_ac_5bits, s_20bits);
    for (int i = 0; i < s_20bits; i++)
        fprintf(out, "%u%s", (unsigned)raw[i], (i + 1 < s_20bits) ? "," : " ");
    for (size_t i = 0; i < len; i++)
        fprintf(out, "%02x", buf[i]);
    fprintf(out, "\n");

    free(blocks);
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

    /* One representative BitDepthAC_5Bits per N bracket (N = bit-length of
     * BitDepthAC_5Bits), plus the low/high boundary of each bracket where
     * distinct from the representative: N=2 -> [2,3], N=3 -> [4,7],
     * N=4 -> [8,15], N=5 -> [16,31] (ACDepthEncoder rejects N>5). */
    UCHAR8 bit_depths[] = {2, 3, 4, 7, 8, 15, 16, 31};
    int segment_sizes[] = {8, 16, 20, 33, 48};

    for (size_t d = 0; d < sizeof(bit_depths) / sizeof(bit_depths[0]); d++) {
        UCHAR8 bd = bit_depths[d];
        int n = 0;
        while ((bd >> n) > 0) n++;
        int modulus = 1 << n;

        for (size_t s = 0; s < sizeof(segment_sizes) / sizeof(segment_sizes[0]); s++) {
            int size = segment_sizes[s];
            WORD16 *raw = calloc(size, sizeof(WORD16));

            gen_ramp(raw, size, modulus);
            run_case(out, bd, size, raw);

            gen_extremes(raw, size, modulus);
            run_case(out, bd, size, raw);

            gen_mid_boundary(raw, size, modulus);
            run_case(out, bd, size, raw);

            free(raw);
        }
    }

    fclose(out);
    return 0;
}
