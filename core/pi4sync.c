/*
 * sync_pi4.c - PI4 synchronization (DT/DF search) - v3, cached grid
 *
 * v2 (see CHANGELOG) fixed sensitivity by making the search fully
 * coherent: for every candidate time offset (DT), every symbol gets
 * its own exactly-aligned, zero-padded FFT window. That is correct
 * but wasteful: consecutive DT candidates are spaced dt_step apart
 * (typically PI4_TSYM/10, i.e. a symbol's window shifts by only 10%
 * between neighbors), so v2 recomputed 146 fresh FFTs per DT
 * candidate even though the vast majority of those windows are
 * identical to ones already computed for a neighboring candidate.
 *
 * v3 removes that redundancy - and ONLY that redundancy - by
 * observing that every window any candidate ever needs starts at
 * istart = idt*dt_step_samples + j*PI4_NSPS for some integer DT index
 * idt and symbol index j (0..145). Choosing dt_step so that it evenly
 * divides a symbol's duration (PI4_NSPS samples) - which the code
 * below enforces automatically, shrinking it if needed - makes every
 * such window start land on a single shared grid, spaced
 * grid_step = PI4_NSPS / N samples apart. Each grid position's FFT is
 * then computed exactly ONCE and reused by every (DT candidate,
 * symbol) pair that needs it, via the index mapping
 * k = idt + j * (PI4_NSPS / grid_step).
 *
 * This is an EXACT optimization, not an approximation or a coarser
 * pre-filtering stage: for a given dt_step, v3 computes precisely the
 * same per-window spectra as v2 would, just without recomputing any
 * of them twice. Sensitivity is therefore unchanged (verified against
 * v2 - see tests/test_sync_v2_v3_equivalence.c); what changes is that
 * searching a much larger dt_max becomes practical. Roughly:
 *   v2 FFT count  = n_dt_steps * PI4_NSYM
 *   v3 FFT count  = n_dt_steps + (PI4_NSYM-1) * (PI4_NSPS/grid_step)
 * e.g. for the default dt_step (grid_step = PI4_NSPS/10) and
 * dt_max = 2 s, v3 needs ~1570 FFTs where v2 needed ~17500 (~11x
 * fewer); for dt_max = 60 s, v3 needs ~5050 where a naive v2-style
 * search would have needed ~530000 (~100x fewer) - the saving grows
 * with dt_max, which is exactly what makes scanning a much wider (or
 * whole-file) time uncertainty affordable.
 *
 * Trade-off: the full grid of spectra is kept in memory for the
 * search (see pi4_sync()'s allocation) - roughly
 * (dt_max + message_duration) * PI4_FS / grid_step * PI4_SYNC_NH2 * 8
 * bytes, e.g. ~50 MB for the default dt_max=2s, ~165 MB for dt_max=60s,
 * ~280 MB for dt_max=120s. Time, not memory, remains the practical
 * limit for very large dt_max on a typical machine.
 *
 * Build:
 *   gcc -O2 -Wall -c sync_pi4.c
 *   gcc ... sync_pi4.o -lfftw3 -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fftw3.h>
#include "pi4.h"
#include "twbb.h"              /* tw_fftw_lock: FFTW planner shared with the rest of libtwist */
#include "pi4sync.h"

/* Per-symbol FFT: PI4_NSPS (2000) real samples, zero-padded to
 * PI4_SYNC_NFFT2. Chosen so that the bin step between adjacent tones
 * (n2) is an exact integer: 8192 = 4*2048 -> DFBIN2 = PI4_DFBIN/4,
 * n2 = PI4_KFACTOR*4 = 160 (verified exact, no rounding error). */
#define PI4_SYNC_NFFT2  8192
#define PI4_SYNC_NH2    (PI4_SYNC_NFFT2 / 2)
#define PI4_SYNC_DFBIN2 (PI4_FS / (double)PI4_SYNC_NFFT2)
#define PI4_SYNC_N2     (PI4_KFACTOR * (PI4_SYNC_NFFT2 / 2048))

/* ------------------------------------------------------------------ */
/* For a frequency hypothesis "ipk2" (tone-0 bin, in units of          */
/* PI4_SYNC_DFBIN2), computes the correlation score against the sync   */
/* vector, for both polarities (flip +1/-1). Reads symbol j's spectrum */
/* from the shared grid cache at index (idt + j*step_ratio) instead of */
/* a locally-recomputed per-DT array - see the file header.            */
/* ------------------------------------------------------------------ */
static double pi4_score(const double *grid_spec, long idt, long step_ratio,
                         int ipk2, const double *pr, int *flip_out)
{
    int j, m;
    double x = 0.0;
    int n2 = PI4_SYNC_N2;

    for (j = 0; j < PI4_NSYM; j++) {
        const double *sp = grid_spec + (idt + (long)j * step_ratio) * PI4_SYNC_NH2;
        double s[4];
        int oob = 0;
        for (m = 0; m < 4; m++) {
            int b = ipk2 + m * n2;
            if (b < 0 || b >= PI4_SYNC_NH2) { oob = 1; break; }
            s[m] = sp[b];
        }
        if (oob) return 0.0;
        double a = (s[1] > s[3] ? s[1] : s[3]) - (s[0] > s[2] ? s[0] : s[2]);
        x += a * pr[j];
    }

    if (x >= 0.0) { *flip_out = 1; return 2.0 * x; }
    else          { *flip_out = -1; return -2.0 * x; }
}

/* ------------------------------------------------------------------ */
/* pi4_sync() - see sync_pi4.h                                         */
/* ------------------------------------------------------------------ */
int pi4_sync(const double *dat, int npts, double nfqso, double ntol,
             double dt_max, double dt_step,
             double *dtx, double *dfx, double *ccfpeak, int *flip)
{
    static double pr[PI4_NSYM];
    static int first = 1;
    double *win_in;
    fftw_complex *win_out;
    fftw_plan plan;
    double *grid_spec;   /* [n_grid][PI4_SYNC_NH2], contiguous */
    long grid_step, step_ratio;
    long dt_max_samples, n_dt_steps, n_grid, n_valid;
    long k;
    int j, ia2, ib2, ipk2;
    long idt;
    double best_ccf = -1.0;
    double best_dt = 0.0;
    int best_ipk2 = -1, best_flip = 1;

    if (first) {
        for (j = 0; j < PI4_NSYM; j++) pr[j] = 2.0 * PI4_SYNCVEC[j] - 1.0;
        first = 0;
    }

    /* Snap dt_step to the nearest exact divisor of a symbol's sample
     * count (PI4_NSPS), shrinking (never growing) if it doesn't
     * already divide evenly, so every window any DT/symbol pair needs
     * lands exactly on the shared grid - see the file header. */
    grid_step = (long)lround(dt_step * PI4_FS);
    if (grid_step < 1) grid_step = 1;
    if (grid_step > PI4_NSPS) grid_step = PI4_NSPS;
    while (PI4_NSPS % grid_step != 0) grid_step--;
    step_ratio = PI4_NSPS / grid_step;

    dt_max_samples = (long)lround(dt_max * PI4_FS);
    n_dt_steps = dt_max_samples / grid_step + 1;
    n_grid = n_dt_steps + (PI4_NSYM - 1) * step_ratio;

    win_in  = (double *)fftw_malloc(sizeof(double) * PI4_SYNC_NFFT2);
    win_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (PI4_SYNC_NFFT2 / 2 + 1));
    tw_fftw_lock();
    plan = fftw_plan_dft_r2c_1d(PI4_SYNC_NFFT2, win_in, win_out, FFTW_ESTIMATE);
    tw_fftw_unlock();

    grid_spec = (double *)malloc(sizeof(double) * (size_t)n_grid * PI4_SYNC_NH2);
    if (!grid_spec) {
        tw_fftw_lock(); fftw_destroy_plan(plan); tw_fftw_unlock();
        fftw_free(win_in);
        fftw_free(win_out);
        return -1;
    }

    /* Build the grid: one FFT per distinct window position, computed
     * once. Stops early (n_valid) if the window would run past the
     * end of the buffer - candidates needing indices beyond that are
     * simply skipped in the search below. */
    n_valid = 0;
    for (k = 0; k < n_grid; k++) {
        long istart = k * grid_step;
        if (istart + PI4_NSPS > npts) break;

        for (j = 0; j < PI4_NSPS; j++) win_in[j] = dat[istart + j];
        for (j = PI4_NSPS; j < PI4_SYNC_NFFT2; j++) win_in[j] = 0.0;

        fftw_execute(plan);

        {
            double *sp = grid_spec + k * PI4_SYNC_NH2;
            for (j = 0; j < PI4_SYNC_NH2; j++) {
                double re = win_out[j][0];
                double im = win_out[j][1];
                sp[j] = re * re + im * im;
            }
        }
        n_valid = k + 1;
    }

    tw_fftw_lock(); fftw_destroy_plan(plan); tw_fftw_unlock();
    fftw_free(win_in);
    fftw_free(win_out);

    ia2 = (int)lround((nfqso - ntol) / PI4_SYNC_DFBIN2);
    ib2 = (int)lround((nfqso + ntol) / PI4_SYNC_DFBIN2);
    if (ia2 < 0) ia2 = 0;
    if (ib2 > PI4_SYNC_NH2 - 1 - 3 * PI4_SYNC_N2) ib2 = PI4_SYNC_NH2 - 1 - 3 * PI4_SYNC_N2;

    for (idt = 0; idt < n_dt_steps; idt++) {
        if (idt + (long)(PI4_NSYM - 1) * step_ratio >= n_valid) {
            break;   /* this and all later (larger) idt run past the buffer */
        }

        for (ipk2 = ia2; ipk2 <= ib2; ipk2++) {
            int fl;
            double ccf = pi4_score(grid_spec, idt, step_ratio, ipk2, pr, &fl);
            if (ccf > best_ccf) {
                best_ccf = ccf;
                best_dt = idt * (double)grid_step / PI4_FS;
                best_ipk2 = ipk2;
                best_flip = fl;
            }
        }
    }

    free(grid_spec);

    if (best_ipk2 < 0) return -1;

    *dtx = best_dt;
    *dfx = best_ipk2 * PI4_SYNC_DFBIN2;
    *ccfpeak = best_ccf;
    *flip = best_flip;

    return 0;
}
