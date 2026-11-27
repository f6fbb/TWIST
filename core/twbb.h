#ifndef TWBB_H
#define TWBB_H

#include <fftw3.h>

/*
 * twbb.h - Front end: brings the search band down to a narrow
 * complex baseband signal, at a low sample rate chosen so that one
 * classic chip is an exact (power-of-two) number of samples.
 *
 * Method: one FFT of the whole recording, keep only the bins of the
 * band of interest (an ideal, brick-wall band-pass filter), move them
 * around 0 Hz, one small inverse FFT. Exact, linear-phase, and time-
 * aligned: sample m of the baseband signal is exactly time
 * m / fs_bb from the start of the file, so DT needs no correction.
 *
 * The same code handles real SSB audio (only the positive-frequency
 * half of the spectrum exists) and complex I/Q captures (both halves,
 * baseband frequencies may be negative).
 *
 * Scaling: a real carrier A*cos() comes out with complex amplitude
 * A/2; real white noise of variance s^2 comes out with complex
 * variance s^2 * fs_bb / fs. Both ratios are the same, so SNRs
 * measured on the baseband signal are directly those of the input.
 */

typedef struct {
    fftw_complex *z;    /* baseband samples, fftw_malloc()'d            */
    long n;             /* number of samples                            */
    double fs;          /* baseband sample rate (Hz)                    */
    double f_center;    /* input frequency (Hz) that now sits at 0 Hz -
                           the requested center, rounded to the FFT
                           bin grid (error < fs_in / (2 * n_fft))       */
} tw_bb_t;

/* Real audio input. Returns 0 on success, -1 on error. */
int tw_baseband_real(const double *x, long npts, int fs_in,
                        double f_center, int fs_bb, tw_bb_t *bb);

/* Complex I/Q input (I = real part, Q = imaginary part). */
int tw_baseband_iq(const double *I, const double *Q, long npts, int fs_in,
                      double f_center, int fs_bb, tw_bb_t *bb);

void tw_baseband_free(tw_bb_t *bb);

/* FFTW's planner (fftw_plan_*, fftw_destroy_plan) is not thread-safe;
 * fftw_execute is. Every plan of the library is made and destroyed
 * between these two calls, so that decoders may run in several threads
 * at once (the application's tests do; a "double free or corruption"
 * crashed one, 2026-11-01). */
void tw_fftw_lock(void);
void tw_fftw_unlock(void);

/* Same thing in two steps, for extracting MANY narrow bands from one
 * recording (wideband scan): the forward FFT of the whole input is
 * computed once (tw_spectrum_*), then each band costs only a small
 * inverse FFT (tw_baseband_extract). All bands extracted from one
 * spectrum share its baseband rate fs_bb. */
typedef struct {
    fftw_complex *X;    /* forward FFT of the input                     */
    long N;             /* FFT length                                   */
    long nx;            /* valid bins (N/2+1 for real input, N for I/Q) */
    int  wraps;         /* 1 for I/Q (negative frequencies exist)       */
    int  fs_in, fs_bb;
    long K;             /* baseband length before trimming              */
    long n_real;        /* baseband samples covering the real recording */
} tw_spectrum_t;

int tw_spectrum_real(const double *x, long npts, int fs_in, int fs_bb, tw_spectrum_t *sp);
int tw_spectrum_iq(const double *I, const double *Q, long npts, int fs_in, int fs_bb,
                      tw_spectrum_t *sp);
int tw_baseband_extract(const tw_spectrum_t *sp, double f_center, tw_bb_t *bb);
void tw_spectrum_free(tw_spectrum_t *sp);

#endif /* TWBB_H */
