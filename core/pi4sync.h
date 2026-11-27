#ifndef SYNC_PI4_H
#define SYNC_PI4_H

#include "pi4.h"

/*
 * pi4_sync() - Searches for PI4 synchronization (DT/DF) via coherent,
 * symbol-by-symbol correlation (FFT exactly aligned on each
 * PI4_NSPS-sample symbol) against PI4_SYNCVEC.
 *
 *   dat      : audio data, sampled at PI4_FS (12000 Hz)
 *   npts     : number of samples in dat
 *   nfqso    : center frequency of the search range (Hz)
 *   ntol     : half-width of the search range (Hz)
 *   dt_max   : upper bound of the time-offset search (s). If the
 *              offset of a message's start is known to within a few
 *              seconds, a small dt_max (1-3 s) keeps the search fast.
 *              If it isn't - e.g. scanning a whole recording for an
 *              unknown frame start - dt_max can now be set to cover
 *              the entire file: thanks to the shared-grid
 *              optimization (see sync_pi4.c's file header), searching
 *              a full ~120 s recording takes on the order of 2-3 s,
 *              not tens of seconds.
 *   dt_step  : resolution of the time sweep (s). Must stay fine enough
 *              not to miss the exact symbol alignment - typically
 *              PI4_TSYM/8 to PI4_TSYM/20. Internally snapped to the
 *              nearest exact divisor of a symbol's sample count if it
 *              doesn't already divide evenly (shrunk, i.e. resolution
 *              only ever gets finer than requested, never coarser).
 *
 *   dtx     [out] : time offset of the message start (s), directly
 *                   usable as "dtx" in pi4_symbol_metrics()
 *   dfx     [out] : frequency (Hz) of tone 0, directly usable as
 *                   "nfreq" in pi4_symbol_metrics()
 *   ccfpeak [out] : strength of the correlation peak
 *   flip    [out] : +1 or -1, sync polarity found
 *
 * Returns: 0 if a sync peak was found, -1 otherwise (no
 * [dt_candidate, dt_candidate + message_duration] window fits in the
 * buffer, or invalid search range).
 *
 * Computational cost: roughly O(dt_max/grid_step + PI4_NSYM) FFTs of
 * size 8192 (each window position computed once, shared across every
 * DT/symbol pair that needs it - see sync_pi4.c), plus
 * O(n_dt_steps * n_freq_candidates * PI4_NSYM) cheap comparisons.
 * Memory: roughly (dt_max + message_duration) * PI4_FS / grid_step *
 * PI4_SYNC_NH2 * 8 bytes - about 50 MB for dt_max=2s, 165 MB for
 * dt_max=60s, 280 MB for dt_max=120s (with the default dt_step).
 */
int pi4_sync(const double *dat, int npts, double nfqso, double ntol,
             double dt_max, double dt_step,
             double *dtx, double *dfx, double *ccfpeak, int *flip);

#endif /* SYNC_PI4_H */
