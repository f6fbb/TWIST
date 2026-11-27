/*
 * pi4mettab.c - Metric table for the PI4 Fano decoder
 *
 * Ported from getmet4.f90 (WSJT-X, K1JT), NOT from mettab.c/genmet.f90
 * which are specific to WSPR's IQ-correlation demodulation scheme. PI4
 * shares JT4's demodulation architecture (power difference between
 * FFT tones, normalized then quantized to a signed byte - cf.
 * amp=30.0 in extract4.f90), so this is the table that applies.
 *
 * Indexing convention:
 *   Fortran: mettab(-128:127, 0:1)
 *   Here:    mettab[0..1][0..255], where C index i corresponds to
 *            Fortran index (i - 128).
 *
 * Build:
 *   gcc -O2 -Wall -c pi4mettab.c
 */

#include <math.h>
#include "pi4mettab.h"

/* Empirical table xx0(0:255), extracted verbatim from getmet4.f90 */
static const float xx0[256] = {
     1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,
     1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,
     1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,
     1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,
     1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,
     1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,  1.000f,
     0.988f,  1.000f,  0.991f,  0.993f,  1.000f,  0.995f,  1.000f,  0.991f,
     1.000f,  0.991f,  0.992f,  0.991f,  0.990f,  0.990f,  0.992f,  0.996f,
     0.990f,  0.994f,  0.993f,  0.991f,  0.992f,  0.989f,  0.991f,  0.987f,
     0.985f,  0.989f,  0.984f,  0.983f,  0.979f,  0.977f,  0.971f,  0.975f,
     0.974f,  0.970f,  0.970f,  0.970f,  0.967f,  0.962f,  0.960f,  0.957f,
     0.956f,  0.953f,  0.942f,  0.946f,  0.937f,  0.933f,  0.929f,  0.920f,
     0.917f,  0.911f,  0.903f,  0.895f,  0.884f,  0.877f,  0.869f,  0.858f,
     0.846f,  0.834f,  0.821f,  0.806f,  0.790f,  0.775f,  0.755f,  0.737f,
     0.713f,  0.691f,  0.667f,  0.640f,  0.612f,  0.581f,  0.548f,  0.510f,
     0.472f,  0.425f,  0.378f,  0.328f,  0.274f,  0.212f,  0.146f,  0.075f,
     0.000f, -0.079f, -0.163f, -0.249f, -0.338f, -0.425f, -0.514f, -0.606f,
    -0.706f, -0.796f, -0.895f, -0.987f, -1.084f, -1.181f, -1.280f, -1.376f,
    -1.473f, -1.587f, -1.678f, -1.790f, -1.882f, -1.992f, -2.096f, -2.201f,
    -2.301f, -2.411f, -2.531f, -2.608f, -2.690f, -2.829f, -2.939f, -3.058f,
    -3.164f, -3.212f, -3.377f, -3.463f, -3.550f, -3.768f, -3.677f, -3.975f,
    -4.062f, -4.098f, -4.186f, -4.261f, -4.472f, -4.621f, -4.623f, -4.608f,
    -4.822f, -4.870f, -4.652f, -4.954f, -5.108f, -5.377f, -5.544f, -5.995f,
    -5.632f, -5.826f, -6.304f, -6.002f, -6.559f, -6.369f, -6.658f, -7.016f,
    -6.184f, -7.332f, -6.534f, -6.152f, -6.113f, -6.288f, -6.426f, -6.313f,
    -9.966f, -6.371f, -9.966f, -7.055f, -9.966f, -6.629f, -6.313f, -9.966f,
    -5.858f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f,
    -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f,
    -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f,
    -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f,
    -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f,
    -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f, -9.966f
};

/* Equivalent of Fortran's nint(): round to nearest integer, ties
 * rounded away from zero (like the standard NINT). */
static int nint(float x)
{
    return (x >= 0.0f) ? (int)(x + 0.5f) : (int)(x - 0.5f);
}

/*
 * pi4_getmet() - Fills mettab[2][256] and computes ndelta.
 *
 * mettab[0][i] corresponds to mettab(i-128, 0) in Fortran (Rx index,
 * Tx symbol=0). mettab[1][i] corresponds to mettab(i-128, 1) (Tx=1).
 *
 * Line-by-line translation of getmet4.f90:
 *
 *   bias=0.5 ; scale=50 ; ndelta=nint(3.4*scale)
 *   do i=0,255
 *      xx=xx0(i)
 *      if(i.ge.160) xx=xx0(160) - (i-160)*6.822/65.3
 *      mettab(i-128,0)=nint(scale*(xx-bias))
 *      if(i.ge.1) mettab(128-i,1)=mettab(i-128,0)
 *   enddo
 *   mettab(-128,1)=mettab(-127,1)
 */
void pi4_getmet(int mettab[2][256], int *ndelta)
{
    const float bias  = 0.5f;
    const int   scale = 50;
    int i;
    float xx;

    *ndelta = nint(3.4f * (float)scale);

    for (i = 0; i < 256; i++) {
        xx = xx0[i];
        if (i >= 160) {
            xx = xx0[160] - (float)(i - 160) * 6.822f / 65.3f;
        }
        mettab[0][i] = nint((float)scale * (xx - bias));

        if (i >= 1) {
            /* mettab(128-i,1) = mettab(i-128,0)
             * C index for (128-i): (128-i)+128 = 256-i             */
            mettab[1][256 - i] = mettab[0][i];
        }
    }

    /* mettab(-128,1) = mettab(-127,1)  ->  C indices 0 and 1 */
    mettab[1][0] = mettab[1][1];
}
