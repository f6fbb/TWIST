/*
 * twist.c - classic "twist" frames: message, CRC-16, scrambling, LDPC,
 * Manchester chips; and the receive side (LLR, decode, check).
 *
 * Message layout (MSB first):
 *   short (type S): 28-bit callsign                       + CRC-16 = 44
 *   long  (type L): 28-bit callsign + 25-bit Maidenhead    + CRC-16 = 69
 * CRC-16/CCITT (poly 0x1021) over the message bits, initial value
 * depending on the type (S: 0xFFFF, L: 0x1D0F) so that a frame of one
 * type can never check as the other. The k bits are then XORed with a
 * type-specific pseudo-random sequence (no all-zero codeword, frames of
 * different types decorrelated), LDPC-encoded, and sent as classic does:
 * one "on" start chip, then each coded bit as Manchester (0 -> on,off ;
 * 1 -> off,on).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "twist.h"
#include "twist_tables.h"
#include "twcall.h"

static ldpc_t g_code[2];
static int g_loaded = 0;

const ldpc_t *tw_code(int type)
{
    if (!g_loaded) {
        if (ldpc_load(&g_code[TW_TYPE_S], TWS_N, TWS_K, TWS_CHECK_DEG, TWS_CHECK_VAR, TWS_G, TWS_PIVOT) != 0 ||
            ldpc_load(&g_code[TW_TYPE_L], TWL_N, TWL_K, TWL_CHECK_DEG, TWL_CHECK_VAR, TWL_G, TWL_PIVOT) != 0) {
            fprintf(stderr, "twist: invalid LDPC tables\n");
            exit(3);
        }
        g_loaded = 1;
    }
    return &g_code[type == TW_TYPE_S ? TW_TYPE_S : TW_TYPE_L];
}

int tw_ncoded(int type) { return type == TW_TYPE_S ? TWS_N : TWL_N; }

static void prbs(int type, unsigned char *seq, int k)
{
    uint32_t s = type == TW_TYPE_S ? 0x5A5A1u : 0x3C3C7u;
    int i;
    for (i = 0; i < k; i++) {
        uint32_t b = ((s >> 0) ^ (s >> 2) ^ (s >> 3) ^ (s >> 5)) & 1u;   /* x^20 LFSR taps */
        seq[i] = (unsigned char)(s & 1u);
        s = (s >> 1) | (b << 19);
    }
}

static uint16_t crc16(const unsigned char *bits, int nb, uint16_t init)
{
    uint16_t c = init;
    int i;
    for (i = 0; i < nb; i++) {
        int fb = ((c >> 15) & 1) ^ (bits[i] & 1);
        c = (uint16_t)(c << 1);
        if (fb) c ^= 0x1021;
    }
    return c;
}

int tw_pack_loc(const char *loc, uint32_t *v)
{
    char l[7] = { 0 };
    int n = (int)strlen(loc), i, sub = 0;
    if (n != 4 && n != 6) return -1;
    for (i = 0; i < n; i++) l[i] = (char)toupper((unsigned char)loc[i]);
    if (l[0] < 'A' || l[0] > 'R' || l[1] < 'A' || l[1] > 'R') return -1;
    if (!isdigit((unsigned char)l[2]) || !isdigit((unsigned char)l[3])) return -1;
    if (n == 6) {
        if (l[4] < 'A' || l[4] > 'X' || l[5] < 'A' || l[5] > 'X') return -1;
        sub = 1 + (l[4] - 'A') * 24 + (l[5] - 'A');
    }
    *v = (uint32_t)(((((l[0] - 'A') * 18 + (l[1] - 'A')) * 10 + (l[2] - '0')) * 10 + (l[3] - '0')) * 577 + sub);
    return 0;
}

int tw_unpack_loc(uint32_t v, char out[7])
{
    int sub, sq, f;
    if (v >= 18u * 18u * 100u * 577u) return -1;
    sub = (int)(v % 577); v /= 577;
    sq = (int)(v % 100); v /= 100;
    f = (int)v;
    out[0] = (char)('A' + f / 18); out[1] = (char)('A' + f % 18);
    out[2] = (char)('0' + sq / 10); out[3] = (char)('0' + sq % 10);
    if (sub) {
        sub--;
        out[4] = (char)('A' + sub / 24); out[5] = (char)('A' + sub % 24); out[6] = 0;
    } else out[4] = 0;
    return 0;
}

static void put_bits(unsigned char *b, uint32_t v, int nb)
{
    int i;
    for (i = 0; i < nb; i++) b[i] = (unsigned char)((v >> (nb - 1 - i)) & 1u);
}
static uint32_t get_bits(const unsigned char *b, int nb)
{
    uint32_t v = 0; int i;
    for (i = 0; i < nb; i++) v = (v << 1) | b[i];
    return v;
}

int tw_kbits(int type) { return type == TW_TYPE_S ? 44 : 69; }

int tw_encode(int type, const char *call, const char *loc, unsigned char *chips)
{
    const ldpc_t *code = tw_code(type);
    char norm[7];
    uint32_t pc, pl = 0;
    unsigned char v[TW_MAXK], seq[TW_MAXK], cw[LDPC_MAXN];
    int nm, k = tw_kbits(type), i;
    uint16_t crc;
    if (code->k != k) return -1;
    if (tw_normalize_call(call, norm) != 0 || tw_pack_call(norm, &pc) != 0) return -1;
    put_bits(v, pc, 28);
    nm = 28;
    if (type == TW_TYPE_L) {
        if (!loc || tw_pack_loc(loc, &pl) != 0) return -1;
        put_bits(v + 28, pl, 25);
        nm = 53;
    }
    crc = crc16(v, nm, type == TW_TYPE_S ? 0xFFFF : 0x1D0F);
    put_bits(v + nm, crc, 16);
    prbs(type, seq, k);
    for (i = 0; i < k; i++) v[i] ^= seq[i];
    ldpc_encode(code, v, cw);
    chips[0] = 1;
    for (i = 0; i < code->n; i++) {
        chips[1 + 2 * i] = cw[i] ? 0 : 1;
        chips[2 + 2 * i] = cw[i] ? 1 : 0;
    }
    return 0;
}

/* ln I0(z), z >= 0 */
static double ln_i0(double z)
{
    if (z < 3.75) {
        double t = z / 3.75, t2 = t * t;
        return log(1.0 + t2 * (3.5156229 + t2 * (3.0899424 + t2 * (1.2067492 + t2 * (0.2659732
                   + t2 * (0.0360768 + t2 * 0.0045813))))));
    } else {
        double t = 3.75 / z;
        return z - 0.5 * log(z) + log(0.39894228 + t * (0.01328592 + t * (0.00225319 + t * (-0.00157565
                   + t * (0.00916281 + t * (-0.02057706 + t * (0.02635537 + t * (-0.01647633 + t * 0.00392377))))))));
    }
}

double tw_llr(const float *ea, const float *eb, int n, double mu, float *llr, int mode)
{
    /* ea[i], eb[i]: raw energies of the two chips of coded bit i
     * (<0 = missing). Noise-normalized: x = e / mu, exponential(1) for
     * noise. Signal energy per chip S estimated from E[d^2] = S^2+2S+2. */
    double s2 = 0.0, S;
    int i, nv = 0;
    for (i = 0; i < n; i++) {
        if (ea[i] < 0 || eb[i] < 0) continue;
        double d = (eb[i] - ea[i]) / mu;
        s2 += d * d; nv++;
    }
    if (nv == 0) { for (i = 0; i < n; i++) llr[i] = 0; return 0; }
    s2 /= nv;
    S = s2 > 2.0 ? -1.0 + sqrt(s2 - 1.0) : 0.0;
    if (S < 0.05) S = 0.05;
    for (i = 0; i < n; i++) {
        double a, b;
        if (ea[i] < 0 || eb[i] < 0) { llr[i] = 0; continue; }
        a = ea[i] / mu; b = eb[i] / mu;
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (mode == 1) llr[i] = (float)(S * (b - a));          /* linear approximation */
        else llr[i] = (float)(ln_i0(2.0 * sqrt(b * S)) - ln_i0(2.0 * sqrt(a * S)));
    }
    return S;
}

int tw_check(int type, const unsigned char *msgbits, char call[7], char loc[7])
{
    const ldpc_t *code = tw_code(type);
    unsigned char v[TW_MAXK], seq[TW_MAXK];
    int k = tw_kbits(type), nm = type == TW_TYPE_S ? 28 : 53, i;
    uint16_t crc;
    char raw[7];
    if (code->k != k) return -1;
    prbs(type, seq, k);
    for (i = 0; i < k; i++) v[i] = msgbits[i] ^ seq[i];
    crc = crc16(v, nm, type == TW_TYPE_S ? 0xFFFF : 0x1D0F);
    if ((uint32_t)crc != get_bits(v + nm, 16)) return -1;
    if (tw_unpack_call(get_bits(v, 28), raw) != 0) return -1;
    {   /* trim */
        int s = 0, e = 5, j = 0;
        while (s < 6 && raw[s] == ' ') s++;
        while (e >= s && raw[e] == ' ') e--;
        while (s <= e) call[j++] = raw[s++];
        call[j] = 0;
    }
    loc[0] = 0;
    if (type == TW_TYPE_L && tw_unpack_loc(get_bits(v + 28, 25), loc) != 0) return -1;
    return 0;
}
