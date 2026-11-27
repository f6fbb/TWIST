/*
 * resample.c - Resampling via Fourier interpolation
 *
 * Method: FFT of the whole signal, symmetric truncation/extension of
 * the spectrum to the new length (equivalent to scipy.signal.resample),
 * inverse FFT. Suited to a band-limited signal - which is the case
 * here since radio audio is already filtered by the receiver (SSB, a
 * few kHz wide).
 *
 * Build:
 *   gcc -O2 -Wall -c resample.c
 *   gcc ... resample.o -lfftw3 -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>
#include "pi4resample.h"
#include "twbb.h"              /* tw_fftw_lock: FFTW planner shared with the rest of libtwist */

int resample_to_rate(const double *in, long n_in, int fs_in, int fs_out,
                      double **out, long *n_out)
{
    long n, m, half, k, ksrc, kdst;
    fftw_complex *x_in, *X, *Y, *y_out;
    fftw_plan p1, p2;
    double *result;
    double scale;

    if (fs_in == fs_out) {
        result = (double *)malloc(sizeof(double) * (size_t)n_in);
        if (!result) return -1;
        memcpy(result, in, sizeof(double) * (size_t)n_in);
        *out = result;
        *n_out = n_in;
        return 0;
    }

    n = n_in;
    m = (long)llround((double)n * (double)fs_out / (double)fs_in);
    if (n < 2 || m < 2) return -1;

    x_in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)n);
    X    = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)n);
    if (!x_in || !X) { fftw_free(x_in); fftw_free(X); return -1; }

    for (k = 0; k < n; k++) {
        x_in[k][0] = in[k];
        x_in[k][1] = 0.0;
    }

    tw_fftw_lock();

    p1 = fftw_plan_dft_1d((int)n, x_in, X, FFTW_FORWARD, FFTW_ESTIMATE);

    tw_fftw_unlock();
    fftw_execute(p1);
    tw_fftw_lock(); fftw_destroy_plan(p1); tw_fftw_unlock();
    fftw_free(x_in);

    Y = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)m);
    if (!Y) { fftw_free(X); return -1; }
    for (k = 0; k < m; k++) { Y[k][0] = 0.0; Y[k][1] = 0.0; }

    /* nmin = the smaller of the two lengths -> defines the common
     * band that is kept (no aliasing on decimation, no artifact on
     * interpolation). */
    half = (n < m ? n : m) / 2;

    for (k = 0; k <= half; k++) {           /* positive frequencies + DC */
        Y[k][0] = X[k][0];
        Y[k][1] = X[k][1];
    }
    for (k = 1; k < half; k++) {            /* negative frequencies */
        ksrc = n - k;
        kdst = m - k;
        Y[kdst][0] = X[ksrc][0];
        Y[kdst][1] = X[ksrc][1];
    }

    fftw_free(X);

    y_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)m);
    if (!y_out) { fftw_free(Y); return -1; }

    tw_fftw_lock();

    p2 = fftw_plan_dft_1d((int)m, Y, y_out, FFTW_BACKWARD, FFTW_ESTIMATE);

    tw_fftw_unlock();
    fftw_execute(p2);
    tw_fftw_lock(); fftw_destroy_plan(p2); tw_fftw_unlock();
    fftw_free(Y);

    result = (double *)malloc(sizeof(double) * (size_t)m);
    if (!result) { fftw_free(y_out); return -1; }

    scale = 1.0 / (double)n;   /* inverse-FFT normalization (size n, not m) */
    for (k = 0; k < m; k++) {
        result[k] = y_out[k][0] * scale;
    }
    fftw_free(y_out);

    *out = result;
    *n_out = m;
    return 0;
}
