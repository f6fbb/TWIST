#include <math.h>
#include <stdio.h>
#include "synth.h"

#include "pi4.h"

/* PI4: 146 4-FSK symbols of message msg (8 characters), continuous phase,
 * tone 0 at carrier - spacing/2 (OZ2M convention for "carrier") */
void pi4_make_symbols(const char *msg, unsigned char *symbols);
void synth_add_pi4(double *x, long n, int fs, const char *msg, double t0, double carrier,
                   double amp, double phase)
{
    unsigned char sym[PI4_NSYM];
    double ph = phase, tsym = PI4_TSYM, f0 = carrier - 0.5 * PI4_TONESPACING;
    long i0 = (long)lround(t0 * fs), i, k;
    char m[PI4_MSGLEN + 1];
    snprintf(m, sizeof m, "%-8s", msg);
    pi4_make_symbols(m, sym);
    for (k = 0; k < PI4_NSYM; k++) {
        long a = i0 + (long)lround(k * tsym * fs), b = i0 + (long)lround((k + 1) * tsym * fs);
        double w = 2.0 * M_PI * (f0 + sym[k] * PI4_TONESPACING) / fs;
        for (i = a; i < b; i++) {
            if (i >= 0 && i < n) x[i] += amp * cos(ph);
            ph += w;
        }
    }
}
