/*
 * twcenc.c - classic message coding (transmit chain + the helpers the
 * decoder reuses to validate a frame).
 *
 * Ported from the F4GCB / 7L1RLL reference implementation (GPL; see
 * CREDITS.md). The original manipulates bits as ASCII '0'/'1'
 * strings; that has one visible consequence which must be kept for
 * compatibility: the CRC is computed over the ASCII *characters*
 * ('0' = 0x30, '1' = 0x31), 8 bits per character LSB first, not over
 * the raw bits. twc_crc() below reproduces that exactly.
 */

#include <string.h>
#include "twcenc.h"
#include "twcall.h"

/* CRC exactly as generate_crc() in the reference code: bits are fed
 * as ASCII characters, 8 bits per character, LSB first; a zero result
 * byte is replaced by 27 (high byte) / 43 (low byte). Returns the 16
 * result bits in wcrc[0..15] (wcrc[0] = MSB of the "byte2" half). */
static void twc_crc(const unsigned char *bits, int nbits, unsigned char wcrc[16])
{
    unsigned char w[16];
    int i, j, k;
    int byte1, byte2;

    memset(w, 0, sizeof w);
    for (i = 0; i < nbits; i++) {
        unsigned int c = bits[i] ? 0x31u : 0x30u;
        for (j = 0; j < 8; j++) {
            unsigned char b1, b2;
            if (j > 0) c >>= 1;
            b1 = (unsigned char)(c & 1u);
            b2 = (unsigned char)(b1 ^ w[0]);
            w[0] = (unsigned char)(b2 ^ w[1]);
            for (k = 1; k < 13; k++) w[k] = w[k + 1];
            w[13] = (unsigned char)(b2 ^ w[14]);
            w[14] = w[15];
            w[15] = b2;
        }
    }

    byte2 = 0;
    for (i = 0; i < 8; i++) byte2 += w[i] << i;
    if (byte2 == 0) byte2 = 27;
    byte1 = 0;
    for (i = 8; i < 16; i++) byte1 += w[i] << (i - 8);
    if (byte1 == 0) byte1 = 43;

    for (i = 0; i < 8; i++) {
        wcrc[7 - i]  = (unsigned char)((byte2 >> i) & 1);
        wcrc[15 - i] = (unsigned char)((byte1 >> i) & 1);
    }
}

void twc_build_vector(uint32_t packed, unsigned char vec[TWC_NVEC])
{
    unsigned char crc[16];
    int i;

    for (i = 0; i < TWC_NSYNCBITS; i++) vec[i] = 0;
    for (i = 0; i < TWC_NCALLBITS; i++)
        vec[4 + i] = (unsigned char)((packed >> (TWC_NCALLBITS - 1 - i)) & 1u);

    twc_crc(vec + 4, TWC_NCALLBITS, crc);
    for (i = 0; i < TWC_NCRC1; i++) vec[32 + i] = crc[i];

    twc_crc(vec + 4, TWC_NCALLBITS + TWC_NCRC1, crc);
    for (i = 0; i < TWC_NCRC2; i++) vec[48 + i] = crc[16 - TWC_NCRC2 + i];
}

int twc_check_vector(const unsigned char vec[TWC_NVEC], uint32_t *packed)
{
    unsigned char ref[TWC_NVEC];
    uint32_t p = 0;
    int i;

    for (i = 0; i < TWC_NSYNCBITS; i++)
        if (vec[i]) return -1;
    for (i = 0; i < TWC_NCALLBITS; i++)
        p = (p << 1) | (vec[4 + i] & 1u);
    twc_build_vector(p, ref);
    if (memcmp(ref, vec, TWC_NVEC) != 0) return -1;
    *packed = p;
    return 0;
}

int twc_encode(const char *call, unsigned char chips[TWC_NCHIPS], char norm_out[7])
{
    char norm[7];
    uint32_t packed;
    unsigned char vec[TWC_NVEC];
    unsigned char coded[TWC_NCODED];
    int g, j, idx;

    if (tw_normalize_call(call, norm) != 0) return -1;
    if (tw_pack_call(norm, &packed) != 0) return -1;
    if (norm_out) memcpy(norm_out, norm, 7);

    twc_build_vector(packed, vec);
    for (j = 0; j < TWC_NVEC; j++) vec[j] ^= TWC_PSEUDO[j];

    for (g = 0; g < TWC_NGROUPS; g++) {
        int data = (vec[3 * g] << 2) | (vec[3 * g + 1] << 1) | vec[3 * g + 2];
        for (j = 0; j < 7; j++) coded[7 * g + j] = TWC_WALSH[data][j];
    }

    chips[0] = 1;
    for (idx = 0; idx < TWC_NCODED; idx++) {
        unsigned char b = coded[twc_deinterleave_pos(idx)];
        chips[1 + 2 * idx] = (unsigned char)(b ? 0 : 1);
        chips[2 + 2 * idx] = (unsigned char)(b ? 1 : 0);
    }
    return 0;
}
