/*
 * pi4snr.c - SNR estimation from the demodulated signal
 *
 * See pi4snr.h for the principle. Reuses the same coherent
 * correlation as pi4demod.c (NCHIPS=1), but computes all 4 tone
 * powers (not just the 2 data=0/1 candidates) to separate signal
 * from noise.
 *
 * Build:
 *   gcc -O2 -Wall -c pi4snr.c -lm
 */

#include <math.h>
#include "pi4.h"
#include "pi4snr.h"

static void pi4_symbol_tone_powers(const float *dat, long istart, double dfx, double p[4])
{
    const double dt = 1.0 / PI4_FS;
    const double twopi = 2.0 * M_PI;
    int m, i;

    for (m = 0; m < 4; m++) {
        double f = dfx + (double)m * PI4_TONESPACING;
        double dphi = twopi * dt * f;
        double phi = 0.0, cre = 0.0, cim = 0.0;

        for (i = 0; i < PI4_NSPS; i++) {
            double s = dat[istart + i];
            phi += dphi;
            cre += s * cos(phi);
            cim -= s * sin(phi);
        }
        p[m] = cre * cre + cim * cim;
    }
}

double pi4_estimate_snr(const float *dat, int npts, double dtx, double dfx)
{
    long istart0 = (long)lround(dtx * PI4_FS);
    long j;
    double p_signal_sum = 0.0, p_noise_sum = 0.0;
    double ps, n0, snr_lin, snr_db;

    if (istart0 < 0 || istart0 + (long)PI4_NSYM * PI4_NSPS > npts) {
        return -99.0;   /* invalid window - estimation impossible */
    }

    for (j = 0; j < PI4_NSYM; j++) {
        double p[4];
        long istart = istart0 + j * PI4_NSPS;
        int m, mmax;
        double pmax, psum3;

        pi4_symbol_tone_powers(dat, istart, dfx, p);

        mmax = 0; pmax = p[0];
        for (m = 1; m < 4; m++) if (p[m] > pmax) { pmax = p[m]; mmax = m; }

        psum3 = 0.0;
        for (m = 0; m < 4; m++) if (m != mmax) psum3 += p[m];

        p_signal_sum += pmax;
        p_noise_sum  += psum3 / 3.0;
    }

    {
        double p_strong_avg = p_signal_sum / PI4_NSYM;
        double p_noise_avg  = p_noise_sum  / PI4_NSYM;
        double n = (double)PI4_NSPS;

        if (p_strong_avg <= p_noise_avg || p_noise_avg <= 0.0) {
            return -99.0;   /* no signal detectable above the noise */
        }

        ps = 2.0 * (p_strong_avg - p_noise_avg) / (n * n);
        n0 = 2.0 * p_noise_avg / (n * PI4_FS);
        snr_lin = ps / (n0 * 2500.0);
        if (snr_lin <= 0.0) return -99.0;
        snr_db = 10.0 * log10(snr_lin);
    }

    return snr_db;
}
