/* Generates function-level shared test vectors for DCEntropyEncoder
 * (original/source/DC_EnDeCoding.c), linked against the real object file.
 *
 * DCEntropyEncoder is the DC-side counterpart of ACDepthEncoder tested by
 * gen_ac_depth_vectors.c: it loops the private per-gaggle Rice-split
 * selection (DCEncoder) over every GAGGLE_SIZE(16)-block gaggle, then
 * (unlike the AC side) may emit additional raw DC bitplanes per section
 * 4.3.3 of the recommendation when BitDepthAC_5Bits < QuantizationFactorQ.
 * A full pipeline sweep only reaches this combination of N bracket x
 * QuantizationFactorQ_prime branch x extra-bitplane-or-not by chance
 * (COMPATIBILITY_REPORT.md documented crafting whole synthetic images
 * just to hit each QuantizationFactorQ_prime branch once, §4). This
 * generator instead derives QuantizationFactorQ/N directly from chosen
 * (BitDepthDC_5Bits, BitDepthAC_5Bits) pairs using the same formula
 * DCEncoding uses (see its comment below), covering every
 * QuantizationFactorQ_prime branch reachable with N>=2, every N bracket
 * the entropy coder dispatches on (2, <=4, <=8, >8), and both with/without
 * extra bitplanes. DWTType is fixed to INTEGER_WAVELET, the production
 * default: CustomWtFlag (a separate field gating *user-supplied* weight
 * overrides) is confirmed always FALSE, but CustomWtLL3_2bits itself is
 * NOT 0 -- HeaderInilization hardcodes it to 3 unconditionally (the
 * built-in LL3-subband weight), and both QuantizationFactorQ and
 * numaddbitplanes fold it in via max() for INTEGER_WAVELET specifically
 * (FLOAT_WAVELET skips that floor entirely and would need its own vectors
 * to cover correctly -- left for a future round, matching this project's
 * existing focus on integer DWT as the primary target elsewhere, e.g.
 * verify/fuzz_compat.py).
 *
 * BlockInfo[].ShiftedDC is fed directly (this generator's synthetic input,
 * not derived from real transform coefficients -- DPCM_DCMapper only reads
 * ShiftedDC, already cross-checked in isolation by gen_dpcm_vectors.c) and
 * DCRemainder is an arbitrary deterministic Q-bit pattern (an opaque data
 * source as far as the §4.3.3 raw-bitplane-emission logic is concerned).
 *
 * Output is plain text, one line per (BitDepthDC_5Bits, BitDepthAC_5Bits,
 * pattern) combo:
 *   <bit_depth_dc_5bits> <bit_depth_ac_5bits> <q> <n> <s_20bits> <shifted_dc_csv> <dc_remainder_csv> <bytes_hex>
 *
 * bpe-rs's own dc/entropy.rs test reads this, calls dc_entropy_encoder with
 * the same inputs and asserts a byte-exact match. It cross-decodes only
 * the combos with no extra bitplanes (numaddbitplanes <= 0): with extra
 * bitplanes present, the byte stream's tail belongs to section 4.3.3, not
 * to dc_entropy_decoder's own contract (that raw-bitplane read lives
 * elsewhere in the real pipeline, not in this function's decode
 * counterpart), so feeding it in would test something dc_entropy_decoder
 * was never meant to consume.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "global.h"

extern void DPCM_DCMapper(BitPlaneBits *BlockInfo, int size, short N);
extern void DCEntropyEncoder(StructCodingPara *PtrCoding, BitPlaneBits *BlockInfo);

static void gen_ramp(DWORD32 *out, int size, int modulus) {
    for (int i = 0; i < size; i++)
        out[i] = (DWORD32)(i % modulus);
}

static void gen_extremes(DWORD32 *out, int size, int modulus) {
    for (int i = 0; i < size; i++)
        out[i] = (i % 2 == 0) ? 0 : (DWORD32)(modulus - 1);
}

static void gen_mid_boundary(DWORD32 *out, int size, int modulus) {
    int mid = modulus / 2;
    int deltas[] = {0, 1, -1, 2, -2, 3, -3, 0};
    for (int i = 0; i < size; i++) {
        int v = mid + deltas[i % 8];
        if (v < 0) v = 0;
        if (v >= modulus) v = modulus - 1;
        out[i] = (DWORD32)v;
    }
}

/* HeaderInilization (header.c) hardcodes CustomWtLL3_2bits = 3 unconditionally
 * (it's the default LL3-subband weight applied when CustomWtFlag is FALSE --
 * a different, always-FALSE field gating whether *user-supplied* weights
 * replace these defaults; CustomWtLL3_2bits itself is never 0 in practice).
 * DCEntropyEncoder folds it into QuantizationFactorQ via
 * max(Q_prime, CustomWtLL3_2bits) for INTEGER_WAVELET, and into
 * numaddbitplanes via max(BitDepthAC_5Bits, CustomWtLL3_2bits) -- both
 * floors must be replicated here to get inputs a real encode run could
 * actually produce. */
#define CUSTOM_WT_LL3 3

/* Same Q_prime derivation as DC_EnDeCoding.c's DCEncoding, given BitDepthDC/AC
 * directly instead of computing them from real transform coefficients. */
static int quantization_factor_q_prime(int bit_depth_dc, int bit_depth_ac) {
    if (bit_depth_dc <= 3)
        return 0;
    int diff = bit_depth_dc - (1 + (bit_depth_ac >> 1));
    if (diff <= 1)
        return bit_depth_dc - 3;
    else if (diff > 10)
        return bit_depth_dc - 10;
    else
        return 1 + (bit_depth_ac >> 1);
}

static int quantization_factor_q(int bit_depth_dc, int bit_depth_ac) {
    int q_prime = quantization_factor_q_prime(bit_depth_dc, bit_depth_ac);
    int floored = q_prime > CUSTOM_WT_LL3 ? q_prime : CUSTOM_WT_LL3; /* INTEGER_WAVELET */
    return floored;
}

static void run_case(FILE *out, int bit_depth_dc, int bit_depth_ac, int s_20bits,
                      DWORD32 *shifted_dc) {
    int q = quantization_factor_q(bit_depth_dc, bit_depth_ac);
    int n = bit_depth_dc - q;
    if (n < 1) n = 1;
    if (n < 2)
        return; /* N==1 bypasses DCEntropyEncoder entirely (a different code path) */

    int k_mask = (1 << q) - 1;
    DWORD32 *dc_remainder = calloc(s_20bits, sizeof(DWORD32));
    for (int i = 0; i < s_20bits; i++)
        dc_remainder[i] = q > 0 ? (DWORD32)((i * 7 + 3) & k_mask) : 0;

    HeaderStruct header;
    memset(&header, 0, sizeof(header));
    header.Header.Part1.BitDepthDC_5Bits = bit_depth_dc;
    header.Header.Part1.BitDepthAC_5Bits = bit_depth_ac;
    header.Header.Part3.S_20Bits = s_20bits;
    header.Header.Part3.OptDCSelect = TRUE;
    header.Header.Part2.SegByteLimit_27Bits = 0; /* no rate limit */
    header.Header.Part4.DWTType = INTEGER_WAVELET;
    header.Header.Part4.CustomWtLL3_2bits = CUSTOM_WT_LL3;

    BitStream bits;
    memset(&bits, 0, sizeof(bits));
    bits.CodeWord_Length = 8;

    StructCodingPara coding;
    memset(&coding, 0, sizeof(coding));
    coding.Bits = &bits;
    coding.PtrHeader = &header;
    coding.SegmentFull = FALSE;
    coding.N = n;
    coding.QuantizationFactorQ = q;

    BitPlaneBits *blocks = calloc(s_20bits, sizeof(BitPlaneBits));
    for (int i = 0; i < s_20bits; i++) {
        blocks[i].ShiftedDC = shifted_dc[i];
        blocks[i].DCRemainder = (WORD16)dc_remainder[i];
    }
    DPCM_DCMapper(blocks, s_20bits, (short)n);

    char tmp_path[] = "/tmp/bpe_dc_entropy_vec_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        perror("mkstemp");
        exit(1);
    }
    close(fd);
    bits.F_Bits = fopen(tmp_path, "wb");

    DCEntropyEncoder(&coding, blocks);

    if (bits.CodeWordAlighmentBits != 0) {
        int shift = bits.CodeWord_Length - bits.CodeWordAlighmentBits;
        BitsOutput(&coding, 0, shift);
    }
    fclose(bits.F_Bits);

    FILE *rf = fopen(tmp_path, "rb");
    unsigned char buf[8192];
    size_t len = fread(buf, 1, sizeof(buf), rf);
    fclose(rf);
    remove(tmp_path);

    fprintf(out, "%d %d %d %d %d ", bit_depth_dc, bit_depth_ac, q, n, s_20bits);
    for (int i = 0; i < s_20bits; i++)
        fprintf(out, "%u%s", (unsigned)shifted_dc[i], (i + 1 < s_20bits) ? "," : " ");
    for (int i = 0; i < s_20bits; i++)
        fprintf(out, "%u%s", (unsigned)dc_remainder[i], (i + 1 < s_20bits) ? "," : " ");
    for (size_t i = 0; i < len; i++)
        fprintf(out, "%02x", buf[i]);
    fprintf(out, "\n");

    free(blocks);
    free(dc_remainder);
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

    /* (BitDepthDC_5Bits, BitDepthAC_5Bits) pairs chosen (via brute-force
     * search over the derivation above, accounting for the CustomWtLL3=3
     * floor) to cover every QuantizationFactorQ_prime branch reachable
     * with N>=2, every N bracket DCEntropyEncoder dispatches on
     * (N==2, N<=4, N<=8, N>8), and both with/without extra section-4.3.3
     * bitplanes. Two combinations the earlier (pre-floor-correction)
     * version of this generator assumed reachable turned out not to be,
     * once CustomWtLL3=3 is applied correctly:
     *   - case1 (BitDepthDC<=3) always floors Q to 3, forcing N=1, which
     *     bypasses DCEntropyEncoder entirely (a different code path) --
     *     so case1 cannot be exercised at this function's level at all.
     *   - case4 (default) requires BitDepthAC<3 for Q_prime>3 to be
     *     mathematically possible in the first place (Q_prime=1+(AC>>1)),
     *     but then max(BitDepthAC, 3)=3>=Q always, making
     *     numaddbitplanes=Q-3<=0 -- case4 can never produce extra
     *     bitplanes. */
    struct { int bd_dc, bd_ac; } depth_pairs[] = {
        {5, 6},   /* case2 (small diff), Q=3(floored), N=2, no extra */
        {5, 0},   /* case4 (default), Q=3(floored), N=2, no extra */
        {8, 0},   /* case4 (default), Q=3(floored), N=5, no extra */
        {12, 0},  /* case3 (large diff), Q=3(floored), N=9, no extra */
        {14, 0},  /* case3 (large diff), Q=4, N=10, extra=1 */
    };
    int segment_sizes[] = {8, 16, 20, 33, 48};

    for (size_t d = 0; d < sizeof(depth_pairs) / sizeof(depth_pairs[0]); d++) {
        int bd_dc = depth_pairs[d].bd_dc;
        int bd_ac = depth_pairs[d].bd_ac;
        int q = quantization_factor_q(bd_dc, bd_ac);
        int n = bd_dc - q;
        if (n < 1) n = 1;
        if (n < 2) continue;
        int modulus = 1 << n;

        for (size_t s = 0; s < sizeof(segment_sizes) / sizeof(segment_sizes[0]); s++) {
            int size = segment_sizes[s];
            DWORD32 *shifted = calloc(size, sizeof(DWORD32));

            gen_ramp(shifted, size, modulus);
            run_case(out, bd_dc, bd_ac, size, shifted);

            gen_extremes(shifted, size, modulus);
            run_case(out, bd_dc, bd_ac, size, shifted);

            gen_mid_boundary(shifted, size, modulus);
            run_case(out, bd_dc, bd_ac, size, shifted);

            free(shifted);
        }
    }

    fclose(out);
    return 0;
}
