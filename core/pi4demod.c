/*
 * pi4demod.c - PI4 symbol metric extraction
 *
 * Ported from the demodulation loop of decode4.f90 (WSJT-X, K1JT).
 * Unlike coarse synchronization (which uses an FFT, see sync4.f90 -
 * ported next), this stage is a time-domain quadrature correlator:
 * for each coded bit, we already know (via PI4_SYNCVEC) what the
 * expected sync bit is at that position, and therefore the two
 * candidate frequencies (tone if data=0, tone if data=1). We correlate
 * against these two local oscillators and compare the powers.
 *
 * Simplification relative to decode4.f90: PI4 has no A-G submodes like
 * JT4 (no loop over "ich"), so PI4_TONESPACING is used directly in
 * place of mode4*df.
 *
 * Parameters:
 *   - PI4_NCHIPS: empirically calibrated to 1 (fully coherent
 *     correlation) via a Monte-Carlo sensitivity sweep - see the
 *     detailed comment near its definition below, and
 *     tests/snr_isolated.c, tests/snr_sweep.c.
 *   - PI4_AMP (15.0, carried over from decode4.f90/JT4): has no
 *     measurable effect on the sensitivity threshold, since pi4dec.c
 *     renormalizes the metrics by their own RMS before quantization
 *     (so the absolute scale factor doesn't matter). Left as-is.
 *
 * Build:
 *   gcc -O2 -Wall -c pi4demod.c -lm
 */

#include <math.h>
#include "pi4.h"
#include "pi4demod.h"

/* --- Demodulation parameters -------------------------------------- */
#ifndef PI4_AMP
#define PI4_AMP     15.0
#endif
#ifndef PI4_NCHIPS
/* PI4_NCHIPS=1 (fully coherent correlation over the whole symbol,
 * no subdivision) - empirically validated via a Monte-Carlo
 * sensitivity sweep (see tests/snr_isolated.c and the README,
 * "Sensitivity / SNR" section). Subdividing into non-coherent "chips"
 * (carried over by analogy from decode4.f90/JT4, where it compensates
 * for Doppler in microwave EME on the wide-tone-spacing submodes)
 * badly degraded PI4 sensitivity: up to ~9 dB of loss with NCHIPS=40
 * compared to NCHIPS=1. PI4 targets VHF/UHF beacons with a short
 * symbol (166.7 ms) where this trade-off doesn't apply - non-coherent
 * combining here only costs performance. With NCHIPS=1, the measured
 * threshold (~-23/-24 dB) matches the theory (-22/-23 dB claimed by
 * the OZ2M doc). */
#define PI4_NCHIPS  1
#endif
#define PI4_NSPCHIP (PI4_NSPS / PI4_NCHIPS)

/*
 * pi4_symbol_metrics()
 *
 *   dat    : audio data, sampled at PI4_FS (12000 Hz)
 *   npts   : number of samples in dat
 *   dtx    : time offset found by synchronization (seconds, from the
 *            start of dat)
 *   nfreq  : frequency (Hz) corresponding to the reference tone "0",
 *            found by synchronization
 *   flip   : +1 or -1, sync polarity found (JT4 equivalent of * / #)
 *   sym    : output array, PI4_NSYM values (raw metric per coded bit,
 *            in transmission order - NOT yet deinterleaved)
 *
 * Returns: 0 on success, -1 if the requested window runs past the
 * dat buffer.
 */
int pi4_symbol_metrics(const float *dat, int npts, double dtx,
                        double nfreq, int flip, float sym[PI4_NSYM])
{
    const double dt = 1.0 / PI4_FS;
    const double twopi = 2.0 * M_PI;
    long istart, k;
    int j, nc, i, syncbit;
    double f0, f1, dphi, dphi1, phi, phi1;
    double c0re, c0im, c1re, c1im;
    double sq0, sq1, rsym;

    istart = (long)lround(dtx / dt);
    if (istart < 0) istart = 0;

    /* Rough check: we need PI4_NSYM * PI4_NSPS samples starting at
     * istart. */
    if (istart + (long)PI4_NSYM * PI4_NSPS > npts) {
        return -1;
    }

    k = istart;
    for (j = 0; j < PI4_NSYM; j++) {

        syncbit = PI4_SYNCVEC[j];

        if (flip > 0) {
            f0 = nfreq + (double)(syncbit)       * PI4_TONESPACING;
            f1 = nfreq + (double)(2 + syncbit)   * PI4_TONESPACING;
        } else {
            f0 = nfreq + (double)(1 - syncbit)   * PI4_TONESPACING;
            f1 = nfreq + (double)(3 - syncbit)   * PI4_TONESPACING;
        }

        dphi  = twopi * dt * f0;
        dphi1 = twopi * dt * f1;

        sq0 = 0.0;
        sq1 = 0.0;

        for (nc = 0; nc < PI4_NCHIPS; nc++) {
            phi  = 0.0;
            phi1 = 0.0;
            c0re = 0.0; c0im = 0.0;
            c1re = 0.0; c1im = 0.0;

            for (i = 0; i < PI4_NSPCHIP; i++) {
                double s = dat[k];
                phi  += dphi;
                phi1 += dphi1;
                /* Quadrature mixer: exp(-j*phi) */
                c0re += s * cos(phi);
                c0im -= s * sin(phi);
                c1re += s * cos(phi1);
                c1im -= s * sin(phi1);
                k++;
            }
            sq0 += c0re * c0re + c0im * c0im;
            sq1 += c1re * c1re + c1im * c1im;
        }

        rsym = PI4_AMP * (sq1 - sq0);
        sym[j] = (float)rsym;
    }

    return 0;
}
