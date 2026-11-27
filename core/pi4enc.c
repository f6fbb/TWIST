/*
 * pi4enc.c - PI4 encoder (message -> 146 4-FSK symbols)
 *
 * Directly reuses fano.c/fano.h (K1JT/KA9Q) for the K=32, r=1/2
 * convolutional encoding - Layland-Lushbaugh polynomials, identical
 * to those required by the PI4 specification (Bo Hansen, OZ2M).
 *
 * Build:
 *   gcc -O2 -Wall -o pi4enc pi4enc.c fano.c -o pi4enc
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pi4.h"
#include "fano.h"

/* ------------------------------------------------------------------ */
/* Look up a character in the PI4 alphabet (space if not found)        */
/* ------------------------------------------------------------------ */
static int pi4_char_index(char c)
{
    const char *p = strchr(PI4_ALPHABET, c);
    if (p == NULL) {
        p = strchr(PI4_ALPHABET, ' ');
    }
    return (int)(p - PI4_ALPHABET);
}

/* ------------------------------------------------------------------ */
/* Source encoding: 8 characters -> 42-bit integer (base 38)           */
/* ------------------------------------------------------------------ */
static uint64_t pi4_source_encode(const char *msg)
{
    uint64_t n = 0;
    int i;
    char c;
    int ended = 0;   /* once '\0' is seen, stop reading further into msg[] */

    for (i = 0; i < PI4_MSGLEN; i++) {
        if (!ended) {
            c = msg[i];
            if (c == '\0') { ended = 1; c = ' '; }
        } else {
            c = ' ';                   /* pad with spaces */
        }
        n = n * PI4_NCHARS + (uint64_t)pi4_char_index(c);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Packs the 42 useful bits (MSB first) into a byte buffer, the rest   */
/* (tail + padding) staying at zero.                                   */
/* ------------------------------------------------------------------ */
#define PI4_ENC_NBYTES 10   /* ceil(73 bits / 8) = 10 bytes -> 160 raw symbols */

static void pi4_pack_bits(uint64_t sourceEnc, unsigned char *databytes)
{
    int k, bit;

    memset(databytes, 0, PI4_ENC_NBYTES);
    for (k = 0; k < PI4_NINFO; k++) {
        bit = (int)((sourceEnc >> (PI4_NINFO - 1 - k)) & 1ULL);
        if (bit) {
            databytes[k / 8] |= (unsigned char)(0x80 >> (k % 8));
        }
    }
    /* The remaining bits (K-1=31 tail + padding) stay at 0 */
}

/* ------------------------------------------------------------------ */
/* Bit-reversal interleaving (algorithm identical to interleave4.f90 / */
/* to the PI4 spec description), generalized to a length N.            */
/* ------------------------------------------------------------------ */
static void pi4_interleave_fixed(const unsigned char *in, unsigned char *out, int n)
{
    int i, bitNo, j, p;

    p = 0;
    for (i = 0; i <= 255; i++) {
        j = 0;
        for (bitNo = 0; bitNo < 8; bitNo++) {
            if ((i >> bitNo) & 1) {
                j |= 1 << (7 - bitNo);
            }
        }
        if (p < n && j < n) {
            out[j] = in[p++];
        }
    }
}

/* ------------------------------------------------------------------ */
/* Full pipeline: message -> PI4_NSYM symbols (values 0..3)            */
/* ------------------------------------------------------------------ */
void pi4_make_symbols(const char *msg, unsigned char *symbols /* [PI4_NSYM] */)
{
    uint64_t sourceEnc;
    unsigned char databytes[PI4_ENC_NBYTES];
    unsigned char rawsym[2 * 8 * PI4_ENC_NBYTES];   /* raw output of encode() */
    unsigned char convenc[PI4_NSYM];                /* 146 useful convolutional bits */
    unsigned char interl[PI4_NSYM];
    int i;

    sourceEnc = pi4_source_encode(msg);
    pi4_pack_bits(sourceEnc, databytes);

    encode(rawsym, databytes, PI4_ENC_NBYTES);   /* fano.c: 2*8*10 = 160 symbols */

    for (i = 0; i < PI4_NSYM; i++) {
        convenc[i] = rawsym[i];
    }

    pi4_interleave_fixed(convenc, interl, PI4_NSYM);

    for (i = 0; i < PI4_NSYM; i++) {
        symbols[i] = (unsigned char)(PI4_SYNCVEC[i] | (interl[i] << 1));
    }
}

/* ------------------------------------------------------------------ */
/* Test CLI: prints the symbols for verification                       */
/* ------------------------------------------------------------------ */
#ifdef PI4ENC_MAIN
int main(int argc, char *argv[])
{
    char msg[PI4_MSGLEN + 1];
    unsigned char symbols[PI4_NSYM];
    int i;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s CALLSIGN\n", argv[0]);
        return 1;
    }

    memset(msg, ' ', PI4_MSGLEN);
    msg[PI4_MSGLEN] = '\0';
    strncpy(msg, argv[1], PI4_MSGLEN);

    pi4_make_symbols(msg, symbols);

    printf("Source-encoded message: %llu\n",
           (unsigned long long)pi4_source_encode(msg));
    printf("Symbols (%d):\n", PI4_NSYM);
    for (i = 0; i < PI4_NSYM; i++) {
        printf("%d ", symbols[i]);
        if ((i + 1) % 20 == 0) printf("\n");
    }
    printf("\n");

    return 0;
}
#endif
