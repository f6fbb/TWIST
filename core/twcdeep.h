#ifndef TWCDEEP_H
#define TWCDEEP_H

#include "twcdefs.h"
#include "twbb.h"
#include "twsync.h"

/*
 * twcdeep.h - "Deep search" for known callsigns.
 *
 * When the callsigns that may be on the air are known in advance
 * (beacons being monitored), there is no need to decode the message
 * blindly: each known callsign gives one exact 239-chip pattern, and
 * the receiver only has to decide whether that pattern is present.
 * That is a matched filter over the whole frame instead of 17
 * independent 3-bit decisions - a few dB more sensitive, especially
 * when the search window (time and frequency) is narrow.
 *
 * Statistic, for one callsign at one (start, frequency) hypothesis:
 *
 *      Z = sum_k s_k d_k / sqrt( sum_k d_k^2 )
 *
 * d_k = energy(second chip) - energy(first chip) of Manchester pair
 * k, s_k = +1/-1 the coded bit the callsign predicts. Z is self-
 * normalized: with noise only - or with some OTHER signal, however
 * strong - it behaves like a standard normal variable, so the
 * detection threshold depends only on how many independent
 * hypotheses are tested (time cells x frequency cells x callsigns)
 * and on the false-alarm rate accepted. It is computed from that,
 * not fixed: narrowing -f/-t/-d around a beacon's known frequency
 * and timing lowers the threshold and directly buys sensitivity.
 */

typedef struct twc_known {
    char          norm[7];               /* normalized callsign        */
    char          call[7];               /* trimmed, for display       */
    unsigned char chips[TWC_NCHIPS];
    signed char   s[TWC_NCODED];       /* +1 / -1 coded bits         */
} twc_known_t;

typedef struct {
    int    k;            /* index into the known list                   */
    long   p;            /* spectrogram position                        */
    int    b;            /* frequency bin                               */
    double t0, f0;       /* s, Hz (baseband)                            */
    double z;            /* map statistic                               */
} twc_deephit_t;

/* Prepares a known-callsign entry. Returns 0, -1 if not encodable. */
int twc_known_init(const char *call, twc_known_t *k);

/* Threshold on Z for a given number of effective independent
 * hypotheses and accepted false-alarm probability (per run). */
double twc_deep_threshold(double n_hyp, double fa);

/* Scans the spectrogram for every known callsign. Hits above the
 * computed threshold (returned in *z_th) are written to out, strongest
 * first, at most one per callsign per frame duration and frequency.
 * Returns the number of hits, or -1 on error. */
int twc_deep_search(const tw_spec_t *sp, const twc_known_t *known, int nknown,
                      double dt_min, double dt_max, double tol, double max_missing,
                      double fa, int max_hits, twc_deephit_t *out, double *z_th);

/* Refines a hit (start, frequency, drift) by maximizing Z computed
 * from exact chip energies; fills fit (e[], f, t_samp, drift, mu,
 * nvalid; score = refined Z). Returns 0, -1 if too many chips are
 * outside the recording. */
int twc_deep_refine(const tw_bb_t *bb, const tw_spec_t *sp,
                      const twc_known_t *k, const twc_deephit_t *h,
                      double drift_max, double max_missing, tw_fit_t *fit);

#endif /* TWCDEEP_H */
