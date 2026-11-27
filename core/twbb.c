/*
 * twbb.c - FFT-domain down-conversion of the search band to a
 * narrow complex baseband signal. See twbb.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twbb.h"

/* FFTW's planner is one for the whole process, shared by every library
 * of TWIST Console loaded in it (each with its own copy of this lock):
 * the first plan of each library makes it thread-safe in FFTW itself
 * (fftw_make_planner_thread_safe, libfftw3_threads), the lock keeps
 * the plans of one library in order as before. */
static void make_safe(void)
{
#ifndef TW_NO_FFTW_THREADS     /* (CMake without libfftw3_threads) */
    static volatile int done = 0;
    if (!done) { fftw_make_planner_thread_safe(); done = 1; }
#endif
}
#ifdef _WIN32
#include <windows.h>
static SRWLOCK fftw_planner = SRWLOCK_INIT;
void tw_fftw_lock(void)   { AcquireSRWLockExclusive(&fftw_planner); make_safe(); }
void tw_fftw_unlock(void) { ReleaseSRWLockExclusive(&fftw_planner); }
#else
#include <pthread.h>
static pthread_mutex_t fftw_planner = PTHREAD_MUTEX_INITIALIZER;
void tw_fftw_lock(void)   { pthread_mutex_lock(&fftw_planner); make_safe(); }
void tw_fftw_unlock(void) { pthread_mutex_unlock(&fftw_planner); }
#endif

static long gcd_l(long a, long b)
{
    while (b) { long t = a % b; a = b; b = t; }
    return a;
}

/* True if n has no prime factor above 7 (fast FFTW sizes). */
static int is_smooth(long n)
{
    static const long p[4] = { 2, 3, 5, 7 };
    int i;
    if (n < 1) return 0;
    for (i = 0; i < 4; i++) while (n % p[i] == 0) n /= p[i];
    return n == 1;
}

/* Smallest FFT length N >= npts such that N * fs_bb / fs_in is an
 * integer K, with both N and K FFT-friendly. */
static int choose_sizes(long npts, int fs_in, int fs_bb, long *pN, long *pK)
{
    long g = gcd_l(fs_in, fs_bb);
    long q = fs_in / g;             /* N must be a multiple of q */
    long r = fs_bb / g;             /* K = (N / q) * r           */
    long m = (npts + q - 1) / q;
    long guard;

    if (m < 1) m = 1;
    for (guard = 0; guard < 100000; guard++, m++) {
        long N = m * q, K = m * r;
        if (is_smooth(N) && is_smooth(K)) { *pN = N; *pK = K; return 0; }
    }
    return -1;
}

/* Common back half: X holds the forward FFT of the input (n_x bins
 * valid, indexable as X[k] for 0 <= k < n_x, the caller says whether
 * the spectrum wraps - complex input - or not - real input r2c). */
static int finish(const fftw_complex *X, long N, long n_x, int wraps,
                  int fs_in, double f_center, long K, int fs_bb, tw_bb_t *bb)
{
    long kc = lround(f_center * (double)N / fs_in);
    long kk;
    fftw_complex *Z;
    fftw_plan p;
    double scale = 1.0 / (double)N;

    Z = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)K);
    if (!Z) return -1;
    memset(Z, 0, sizeof(fftw_complex) * (size_t)K);

    for (kk = -K / 2; kk < K - K / 2; kk++) {
        long k = kc + kk;
        long dst = (kk + K) % K;
        if (wraps) {
            k %= N; if (k < 0) k += N;
        } else if (k < 0 || k >= n_x) {
            continue;
        }
        Z[dst][0] = X[k][0] * scale;
        Z[dst][1] = X[k][1] * scale;
    }

    tw_fftw_lock();
    p = fftw_plan_dft_1d((int)K, Z, Z, FFTW_BACKWARD, FFTW_ESTIMATE);
    tw_fftw_unlock();
    if (!p) { fftw_free(Z); return -1; }
    fftw_execute(p);
    tw_fftw_lock(); fftw_destroy_plan(p); tw_fftw_unlock();

    bb->z = Z;
    bb->n = K;
    bb->fs = fs_bb;
    bb->f_center = (double)kc * fs_in / (double)N;
    return 0;
}

int tw_spectrum_real(const double *x, long npts, int fs_in, int fs_bb, tw_spectrum_t *sp)
{
    double *buf;
    fftw_plan p;

    memset(sp, 0, sizeof *sp);
    if (choose_sizes(npts, fs_in, fs_bb, &sp->N, &sp->K) != 0) return -1;
    buf = (double *)fftw_malloc(sizeof(double) * (size_t)sp->N);
    sp->X = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)(sp->N / 2 + 1));
    if (!buf || !sp->X) { fftw_free(buf); tw_spectrum_free(sp); return -1; }
    memcpy(buf, x, sizeof(double) * (size_t)npts);
    if (sp->N > npts) memset(buf + npts, 0, sizeof(double) * (size_t)(sp->N - npts));
    tw_fftw_lock();
    p = fftw_plan_dft_r2c_1d((int)sp->N, buf, sp->X, FFTW_ESTIMATE);
    tw_fftw_unlock();
    if (!p) { fftw_free(buf); tw_spectrum_free(sp); return -1; }
    fftw_execute(p);
    tw_fftw_lock(); fftw_destroy_plan(p); tw_fftw_unlock();
    fftw_free(buf);
    sp->nx = sp->N / 2 + 1;
    sp->wraps = 0;
    sp->fs_in = fs_in;
    sp->fs_bb = fs_bb;
    sp->n_real = (long)floor((double)npts * fs_bb / fs_in);
    return 0;
}

int tw_spectrum_iq(const double *I, const double *Q, long npts, int fs_in, int fs_bb,
                      tw_spectrum_t *sp)
{
    fftw_plan p;
    long i;

    memset(sp, 0, sizeof *sp);
    if (choose_sizes(npts, fs_in, fs_bb, &sp->N, &sp->K) != 0) return -1;
    sp->X = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (size_t)sp->N);
    if (!sp->X) return -1;
    for (i = 0; i < npts; i++) { sp->X[i][0] = I[i]; sp->X[i][1] = Q[i]; }
    for (; i < sp->N; i++) { sp->X[i][0] = 0.0; sp->X[i][1] = 0.0; }
    tw_fftw_lock();
    p = fftw_plan_dft_1d((int)sp->N, sp->X, sp->X, FFTW_FORWARD, FFTW_ESTIMATE);
    tw_fftw_unlock();
    if (!p) { tw_spectrum_free(sp); return -1; }
    fftw_execute(p);
    tw_fftw_lock(); fftw_destroy_plan(p); tw_fftw_unlock();
    sp->nx = sp->N;
    sp->wraps = 1;
    sp->fs_in = fs_in;
    sp->fs_bb = fs_bb;
    sp->n_real = (long)floor((double)npts * fs_bb / fs_in);
    return 0;
}

int tw_baseband_extract(const tw_spectrum_t *sp, double f_center, tw_bb_t *bb)
{
    int rc;
    memset(bb, 0, sizeof *bb);
    rc = finish(sp->X, sp->N, sp->nx, sp->wraps, sp->fs_in, f_center, sp->K, sp->fs_bb, bb);
    if (rc == 0 && sp->n_real < bb->n) bb->n = sp->n_real;
    return rc;
}

void tw_spectrum_free(tw_spectrum_t *sp)
{
    if (sp->X) fftw_free(sp->X);
    memset(sp, 0, sizeof *sp);
}

int tw_baseband_real(const double *x, long npts, int fs_in,
                        double f_center, int fs_bb, tw_bb_t *bb)
{
    tw_spectrum_t sp;
    int rc;
    memset(bb, 0, sizeof *bb);
    if (tw_spectrum_real(x, npts, fs_in, fs_bb, &sp) != 0) return -1;
    rc = tw_baseband_extract(&sp, f_center, bb);
    tw_spectrum_free(&sp);
    return rc;
}

int tw_baseband_iq(const double *I, const double *Q, long npts, int fs_in,
                      double f_center, int fs_bb, tw_bb_t *bb)
{
    tw_spectrum_t sp;
    int rc;
    memset(bb, 0, sizeof *bb);
    if (tw_spectrum_iq(I, Q, npts, fs_in, fs_bb, &sp) != 0) return -1;
    rc = tw_baseband_extract(&sp, f_center, bb);
    tw_spectrum_free(&sp);
    return rc;
}

void tw_baseband_free(tw_bb_t *bb)
{
    if (bb->z) fftw_free(bb->z);
    memset(bb, 0, sizeof *bb);
}
