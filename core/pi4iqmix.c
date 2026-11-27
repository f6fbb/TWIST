/*
 * pi4iqmix.c - see pi4iqmix.h (from pi4d)
 *
 * Build:
 *   gcc -O2 -Wall -c iqmix.c
 *   gcc ... iqmix.o resample.o -lfftw3 -lm
 */

#include <stdlib.h>
#include <math.h>
#include <fftw3.h>
#include "pi4.h"
#include "pi4iqmix.h"
#include "pi4resample.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Brick-wall BANDPASS (FFT domain: zero every bin outside +/-[lo,hi]
 * Hz, inverse transform), applied in place after resampling.
 *
 * resample_to_rate() documents its own assumption plainly: it expects
 * an already band-limited signal, "the case for radio audio captures,
 * already filtered by the receiver". A candidate extracted from a
 * wideband I/Q capture has NOT been through any such filter.
 *
 * A real SSB receiver's IF filter (~300-2700 Hz) is shaped for VOICE
 * intelligibility - PI4 needs none of that: its 4 tones span only
 * ~700 Hz, all comfortably above 500 Hz. So rather than merely
 * matching a receiver's general-purpose bandwidth (the original,
 * lowpass-only version of this filter did that, 0-2300 Hz), a
 * bandpass tailored to where PI4 can actually be after remixing can
 * be narrower still - cutting more noise than even a genuine SSB
 * capture carries, not just matching it.
 */
static void bandpass_fft(double *x, long n, int fs, double lo_hz, double hi_hz)
{
    fftw_complex *buf;
    fftw_plan pf, pb;
    long k;

    buf = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)n);
    if (!buf) return;
    for (k = 0; k < n; k++) { buf[k][0] = x[k]; buf[k][1] = 0.0; }

    pf = fftw_plan_dft_1d((int)n, buf, buf, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(pf);
    fftw_destroy_plan(pf);

    for (k = 0; k < n; k++) {
        double f = (k <= n / 2) ? (k * (double)fs / n) : ((k - n) * (double)fs / n);
        double af = fabs(f);
        if (af < lo_hz || af > hi_hz) {
            buf[k][0] = 0.0;
            buf[k][1] = 0.0;
        }
    }

    pb = fftw_plan_dft_1d((int)n, buf, buf, FFTW_BACKWARD, FFTW_ESTIMATE);
    fftw_execute(pb);
    fftw_destroy_plan(pb);

    for (k = 0; k < n; k++) x[k] = buf[k][0] / (double)n;
    fftw_free(buf);
}

int iq_mix_to_audio(const double *I, const double *Q, long npts, int fs_in,
                     double carrier_freq_hz, double nfqso_target, int fs_out,
                     double **out, long *n_out)
{
    double *shifted;
    double df, dphi, phi;
    long i;
    int rc;

    if (npts <= 0) return -1;

    shifted = (double *)malloc(sizeof(double) * (size_t)npts);
    if (!shifted) return -1;

    /* Shift so the candidate's carrier (currently at carrier_freq_hz
     * relative to the I/Q center) lands at nfqso_target. */
    df = nfqso_target - carrier_freq_hz;
    dphi = 2.0 * M_PI * df / (double)fs_in;
    phi = 0.0;

    for (i = 0; i < npts; i++) {
        double c = cos(phi), s = sin(phi);
        /* Re( (I+jQ) * (c+js) ) = I*c - Q*s */
        shifted[i] = I[i] * c - Q[i] * s;
        phi += dphi;
        if (phi > 1.0e6 || phi < -1.0e6) {   /* keep phi bounded over long captures */
            phi = fmod(phi, 2.0 * M_PI);
        }
    }

    rc = resample_to_rate(shifted, npts, fs_in, fs_out, out, n_out);
    free(shifted);
    if (rc != 0) return rc;

    /* Bandpass back down to just where PI4 can actually be after
     * remixing - see bandpass_fft()'s comment. The candidate's tone0
     * sits at nfqso_target - 0.5*spacing; pi4d's narrow re-sync search
     * (after this mixing step) then looks +/-RESYNC_TOL around that -
     * so the 4 tones can appear anywhere from (tone0-RESYNC_TOL) to
     * (tone0+RESYNC_TOL+3*spacing). RESYNC_TOL must match the search
     * tolerance pi4d.c's I/Q path actually uses for that re-sync call
     * (currently a fixed 150 Hz there) - kept as an explicit constant
     * here, rather than silently duplicated, precisely so the two
     * don't drift apart unnoticed if one changes. MARGIN_HZ absorbs
     * filter roll-off and any residual coarse-scan error. */
    {
        const double RESYNC_TOL = 150.0;   /* must match pi4d.c's narrow_tol for -i */
        const double MARGIN_HZ = 50.0;
        double tone0 = nfqso_target - 0.5 * PI4_TONESPACING;
        double lo = tone0 - RESYNC_TOL - MARGIN_HZ;
        double hi = tone0 + RESYNC_TOL + 3.0 * PI4_TONESPACING + MARGIN_HZ;
        if (lo < 0.0) lo = 0.0;
        bandpass_fft(*out, *n_out, fs_out, lo, hi);
    }

    return 0;
}
