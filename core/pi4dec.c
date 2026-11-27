/*
 * pi4dec.c - Final assembly of the PI4 decoder
 *
 * Takes the raw symbol metrics (output of pi4_symbol_metrics, in
 * transmission order) and recovers the text message:
 *   1. Deinterleaving (inverse of the bit-reversal interleaving)
 *   2. RMS normalization + quantization to a signed byte (as in
 *      extract4.f90, amp=30.0)
 *   3. Convolutional decoding via fano() (fano.c, unchanged)
 *   4. Inverse base-38 source decoding -> text
 *
 * Build:
 *   gcc -O2 -Wall -c pi4dec.c
 *   gcc ... pi4dec.o pi4mettab.o fano.o tab.o -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "pi4.h"
#include "pi4mettab.h"
#include "fano.h"
#include "pi4dec.h"

#define PI4_AMP_EXTRACT 30.0
/* NOTE: raised to 2000000 during a diagnostic test (see README,
 * "Sensitivity / SNR" section) to check whether the Fano decoder was
 * the limiting factor at low SNR - confirmed it was not (marginal
 * gain). Restored here to a reasonable default; raising it further
 * hardly helps as long as PI4_AMP/PI4_NCHIPS/PI4_AMP_EXTRACT and the
 * metric table are not recalibrated for PI4 (see README). */
#define PI4_FANO_MAXCYCLES 100000u

/* Deinterleaving (exact inverse of pi4_interleave_fixed in pi4enc.c) */
static void pi4_deinterleave_f(const float *rx, float *natural, int n)
{
    int i, bitNo, j, p = 0;

    for (i = 0; i <= 255; i++) {
        j = 0;
        for (bitNo = 0; bitNo < 8; bitNo++) {
            if ((i >> bitNo) & 1) j |= 1 << (7 - bitNo);
        }
        if (p < n && j < n) {
            natural[p++] = rx[j];
        }
    }
}

/* Inverse base-38 source decoding: 42-bit integer -> 8 characters */
static void pi4_source_decode(uint64_t n, char msg_out[PI4_MSGLEN + 1])
{
    int i;
    char tmp[PI4_MSGLEN];

    for (i = PI4_MSGLEN - 1; i >= 0; i--) {
        tmp[i] = PI4_ALPHABET[n % PI4_NCHARS];
        n /= PI4_NCHARS;
    }
    memcpy(msg_out, tmp, PI4_MSGLEN);
    msg_out[PI4_MSGLEN] = '\0';
}

static int nint(double x)
{
    return (x >= 0.0) ? (int)(x + 0.5) : (int)(x - 0.5);
}

int pi4_decode_message(const float sym[PI4_NSYM], char msg_out[PI4_MSGLEN + 1],
                        long *fano_metric)
{
    float natural[PI4_NSYM];
    unsigned char qbytes[PI4_NSYM];
    unsigned char data[(PI4_NCONV + 7) / 8];   /* 9 bytes for 73 bits */
    int mettab[2][256];
    int ndelta;
    double ave, sq, rms, v;
    int i, n;
    unsigned int metric, cycles, maxnp;
    uint64_t sourceEnc;
    int rc;

    /* 1. Deinterleaving */
    pi4_deinterleave_f(sym, natural, PI4_NSYM);

    /* 2. RMS normalization + quantization (see extract4.f90) */
    ave = 0.0;
    for (i = 0; i < PI4_NSYM; i++) ave += natural[i];
    ave /= PI4_NSYM;

    sq = 0.0;
    for (i = 0; i < PI4_NSYM; i++) {
        double d = natural[i] - ave;
        sq += d * d;
    }
    rms = sqrt(sq / (PI4_NSYM - 1));
    if (rms < 1e-9) rms = 1e-9;

    for (i = 0; i < PI4_NSYM; i++) {
        v = (natural[i] - ave) / rms;
        n = nint(PI4_AMP_EXTRACT * v);
        if (n < -127) n = -127;
        if (n > 127) n = 127;
        qbytes[i] = (unsigned char)(n + 128);   /* mettab convention: index = value + 128 */
    }

    /* 3. Convolutional decoding (Fano) */
    pi4_getmet(mettab, &ndelta);

    rc = fano(&metric, &cycles, &maxnp, data, qbytes,
              PI4_NCONV, mettab, ndelta, PI4_FANO_MAXCYCLES);

    if (fano_metric) *fano_metric = (long)metric;
    if (rc != 0) {
        return -1;   /* decoder timeout */
    }

    /* 4. Extract the 42 useful bits (the 9 bytes of data hold the
     *    first 72 of 73 bits - more than enough, since the useful
     *    information fits in the first 42) and decode to text. */
    sourceEnc = 0;
    for (i = 0; i < PI4_NINFO; i++) {
        int byte_i = i / 8;
        int bit_i  = 7 - (i % 8);
        int bit = (data[byte_i] >> bit_i) & 1;
        sourceEnc = (sourceEnc << 1) | (uint64_t)bit;
    }

    pi4_source_decode(sourceEnc, msg_out);

    return 0;
}
