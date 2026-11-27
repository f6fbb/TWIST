/*
 * twcall.c - Callsigns: normalization, 28-bit packing (the format of
 * the classic frame, from the F4GCB / 7L1RLL reference code - GPL, see
 * CREDITS.md - reused by the TWIST frames and the PI4 checks) and the
 * ITU-structure plausibility check. Protocol-neutral (twcall.h).
 */

#include <string.h>
#include <ctype.h>
#include "twcall.h"

/* Space=0, A..Z=1..26, 0..9=27..36 (chr_norm_classic() in the
 * reference code). Returns -1 for anything else. */
static int chr_norm(char c)
{
    if (c >= '0' && c <= '9') return c - '0' + 27;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c == ' ') return 0;
    return -1;
}

int tw_normalize_call(const char *in, char out[7])
{
    char tmp[8];
    int len = 0;
    int i;

    while (*in == ' ') in++;
    while (in[len] && in[len] != ' ' && len < 7) {
        tmp[len] = (char)toupper((unsigned char)in[len]);
        len++;
    }
    if (in[len] && in[len] != ' ') return -1;      /* more than 7 chars */
    if (len == 0) return -1;
    tmp[len] = '\0';

    /* "the third character must always be a number" */
    if (len < 3 || !(tmp[2] >= '0' && tmp[2] <= '9')) {
        if (len > 5) return -1;                   /* no room to shift */
        memmove(tmp + 1, tmp, (size_t)len + 1);
        tmp[0] = ' ';
        len++;
    }
    if (len > 6) return -1;
    for (i = len; i < 6; i++) tmp[i] = ' ';
    tmp[6] = '\0';

    memcpy(out, tmp, 7);
    {
        uint32_t dummy;
        return tw_pack_call(out, &dummy);
    }
}

int tw_pack_call(const char norm[7], uint32_t *packed)
{
    int c0 = chr_norm(norm[0]);
    int c1 = chr_norm(norm[1]);
    int c2 = chr_norm(norm[2]);
    int c3 = chr_norm(norm[3]);
    int c4 = chr_norm(norm[4]);
    int c5 = chr_norm(norm[5]);
    uint32_t code;

    if (c0 < 0) return -1;                      /* space, letter or digit */
    if (c1 < 1) return -1;                      /* letter or digit        */
    if (c2 < 27) return -1;                     /* digit                  */
    if (c3 < 0 || c3 > 26) return -1;           /* space or letter        */
    if (c4 < 0 || c4 > 26) return -1;
    if (c5 < 0 || c5 > 26) return -1;

    code = (uint32_t)c0;
    code = code * 36 + (uint32_t)(c1 - 1);
    code = code * 10 + (uint32_t)(c2 - 27);
    code = code * 27 + (uint32_t)c3;
    code = code * 27 + (uint32_t)c4;
    code = code * 27 + (uint32_t)c5;
    *packed = code;
    return 0;
}

int tw_unpack_call(uint32_t packed, char out[7])
{
    uint32_t v = packed;
    uint32_t c5 = v % 27; v /= 27;
    uint32_t c4 = v % 27; v /= 27;
    uint32_t c3 = v % 27; v /= 27;
    uint32_t c2 = v % 10; v /= 10;
    uint32_t c1 = v % 36; v /= 36;
    uint32_t c0 = v;
    uint32_t cs[3];
    int i;

    if (c0 > 36) return -1;

    out[0] = (c0 == 0) ? ' ' : (c0 <= 26) ? (char)('A' + c0 - 1) : (char)('0' + c0 - 27);
    out[1] = (c1 <= 25) ? (char)('A' + c1) : (char)('0' + c1 - 26);
    out[2] = (char)('0' + c2);
    cs[0] = c3; cs[1] = c4; cs[2] = c5;
    for (i = 0; i < 3; i++)
        out[3 + i] = (cs[i] == 0) ? ' ' : (char)('A' + cs[i] - 1);
    out[6] = '\0';

    /* A space in the middle of the suffix ("AB1C D") cannot come out
     * of the reference encoder (it pads on the right only) - treat it
     * as invalid, it is free extra error detection. Same for a
     * leading space followed by a non-letter/digit (never happens:
     * c1 is always a letter/digit by construction). */
    if (out[3] == ' ' && (out[4] != ' ' || out[5] != ' ')) return -1;
    if (out[4] == ' ' && out[5] != ' ') return -1;
    return 0;
}

/* Letter+digit ITU prefixes in use (the digit then being the prefix's
 * own second character, e.g. "A6", "E7", "T7"). */
static const char *LD_PREFIXES[] = {
    "A2","A3","A4","A5","A6","A7","A8","A9","C2","C3","C4","C5","C6","C7","C8","C9",
    "D2","D3","D4","D5","D6","D7","D8","D9","E2","E3","E4","E5","E6","E7",
    "H2","H3","H4","H6","H7","H8","H9","J2","J3","J4","J5","J6","J7","J8",
    "L2","L3","L4","L5","L6","L7","L8","L9","P2","P3","P4","P5",
    "S2","S3","S5","S6","S7","S8","S9","T2","T3","T4","T5","T6","T7","T8",
    "V2","V3","V4","V5","V6","V7","V8","Z2","Z3","Z6","Z8", NULL
};

int tw_call_plausible(const char norm[7])
{
    char a = norm[0], b = norm[1];
    int a_digit = (a >= '0' && a <= '9'), b_digit = (b >= '0' && b <= '9');
    int i;

    if (norm[3] == ' ') return 0;                 /* no suffix at all          */
    if (a == ' ')                                 /* one-character prefix:     */
        return strchr("BFGIKMNRW", b) != NULL;    /* single-letter ITU blocks  */
    if (a == 'Q') return 0;                       /* Q block is not allocated  */
    if (a_digit) {
        if (a == '0') return 0;
        if (a == '1') return b == 'A';            /* 1A (SMOM) only            */
        return !b_digit;                          /* 2E, 4X, 9A...             */
    }
    if (b_digit) {
        for (i = 0; LD_PREFIXES[i]; i++)
            if (LD_PREFIXES[i][0] == a && LD_PREFIXES[i][1] == b) return 1;
        return 0;
    }
    return 1;                                     /* two-letter prefix         */
}

